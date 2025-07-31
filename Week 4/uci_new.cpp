#include "chess.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <limits>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <algorithm>
#include <chrono>

using namespace chess;
using namespace std;

// Piece-Square Tables (PSTs)
typedef std::array<int, 64> PST;

static const PST PST_PAWN = {
    0, 0, 0, 0, 0, 0, 0, 0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
    5, 5, 10, 25, 25, 10, 5, 5,
    0, 0, 0, 20, 20, 0, 0, 0,
    5, -5, -10, 0, 0, -10, -5, 5,
    5, 10, 10, -20, -20, 10, 10, 5,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const PST PST_KNIGHT = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20, 0, 0, 0, 0, -20, -40,
    -30, 0, 10, 15, 15, 10, 0, -30,
    -30, 5, 15, 20, 20, 15, 5, -30,
    -30, 0, 15, 20, 20, 15, 0, -30,
    -30, 5, 10, 15, 15, 10, 5, -30,
    -40, -20, 0, 5, 5, 0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50
};

static const PST PST_BISHOP = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10, 5, 0, 0, 0, 0, 5, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    -10, 0, 10, 10, 10, 10, 0, -10,
    -10, 5, 5, 10, 10, 5, 5, -10,
    -10, 0, 5, 10, 10, 5, 0, -10,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -20, -10, -10, -10, -10, -10, -10, -20
};

static const PST PST_ROOK = {
    0, 0, 0, 0, 0, 0, 0, 0,
    5, 10, 10, 10, 10, 10, 10, 5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    0, 0, 0, 5, 5, 0, 0, 0
};

static const PST PST_QUEEN = {
    -20, -10, -10, -5, -5, -10, -10, -20,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -10, 0, 5, 5, 5, 5, 0, -10,
    -5, 0, 5, 5, 5, 5, 0, -5,
    0, 0, 5, 5, 5, 5, 0, -5,
    -10, 5, 5, 5, 5, 5, 0, -10,
    -10, 0, 5, 0, 0, 0, 0, -10,
    -20, -10, -10, -5, -5, -10, -10, -20
};

static const PST PST_KING = {
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
    20, 20, 0, 0, 0, 0, 20, 20,
    20, 30, 10, 0, 0, 10, 30, 20
};

// Material values
static const std::unordered_map<PieceType::underlying, int> MATERIAL_VALUE = {
    {PieceType::PAWN, 100},
    {PieceType::KNIGHT, 320},
    {PieceType::BISHOP, 330},
    {PieceType::ROOK, 500},
    {PieceType::QUEEN, 900},
    {PieceType::KING, 20000}
};

// Evaluation constants
static constexpr int ISOLATED_PAWN_PENALTY = 20;
static constexpr int DOUBLED_PAWN_PENALTY = 10;
static constexpr int PASSED_PAWN_BONUS = 30;
static constexpr int KING_SAFETY_PENALTY = 50;
static constexpr int MOBILITY_FACTOR = 1;

// Transposition Table Entry
enum TTFlag {
    EXACT,
    LOWER_BOUND,
    UPPER_BOUND
};

struct TTEntry {
    uint64_t key;
    Move bestMove;
    int score;
    int depth;
    TTFlag flag;
};

// Transposition Table
static std::vector<TTEntry> transpositionTable;
static std::mutex ttMutex;

// Function to resize the transposition table
void resizeTT(size_t mb) {
    size_t newSize = (mb * 1024 * 1024) / sizeof(TTEntry);
    lock_guard<mutex> lock(ttMutex);
    transpositionTable.assign(newSize, {});
}

// Function to clear the transposition table
void clearTT() {
    lock_guard<mutex> lock(ttMutex);
    for (auto& entry : transpositionTable) {
        entry = {};
    }
}

// Store and probe the transposition table
void storeTT(uint64_t key, Move bestMove, int score, int depth, TTFlag flag) {
    lock_guard<mutex> lock(ttMutex);
    if (transpositionTable.empty()) return;
    size_t index = key % transpositionTable.size();
    if (transpositionTable[index].depth <= depth) {
        transpositionTable[index] = {key, bestMove, score, depth, flag};
    }
}

TTEntry* probeTT(uint64_t key) {
    lock_guard<mutex> lock(ttMutex);
    if (transpositionTable.empty()) return nullptr;
    size_t index = key % transpositionTable.size();
    if (transpositionTable[index].key == key) {
        return &transpositionTable[index];
    }
    return nullptr;
}


int evaluateMaterial(const Board& board) {
    int eval = 0;
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP, PieceType::ROOK, PieceType::QUEEN}) {
        PieceType pt(pt_under);
        int val = MATERIAL_VALUE.at(pt.internal());
        int wc = board.pieces(pt, Color::WHITE).count();
        int bc = board.pieces(pt, Color::BLACK).count();
        eval += (wc - bc) * val;
    }
    return eval;
}

int evaluateKingSafety(const Board& board) {
    int eval = 0;
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Square kingSq = board.kingSq(color);
        Bitboard around = attacks::king(kingSq);
        Color opp = ~color;
        int threats = 0;
        Bitboard temp = around;
        while (temp) {
            Square sq = temp.pop();
            if (board.isAttacked(sq, opp)) {
                threats++;
            }
        }
        eval += (color == Color::WHITE ? -threats : threats) * KING_SAFETY_PENALTY;
    }
    return eval;
}

int evaluatePawnStructure(const Board& board) {
    int eval = 0;
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Bitboard pawns = board.pieces(PieceType::PAWN, color);
        for (int f = 0; f < 8; f++) {
            Bitboard fileMask = Bitboard(File(f));
            Bitboard thisFilePawns = pawns & fileMask;
            int countOnFile = thisFilePawns.count();
            if (countOnFile > 1) {
                int penalty = (countOnFile - 1) * DOUBLED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            bool hasLeft = (f > 0) && ((pawns & Bitboard(File(f - 1))).count() > 0);
            bool hasRight = (f < 7) && ((pawns & Bitboard(File(f + 1))).count() > 0);
            if (countOnFile > 0 && !hasLeft && !hasRight) {
                int penalty = countOnFile * ISOLATED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            Bitboard tmp = thisFilePawns;
            while (tmp) {
                Square pSq = tmp.pop();
                int rank = pSq.rank();
                bool isPassed = true;
                for (int af = std::max(0, f - 1); af <= std::min(7, f + 1); af++) {
                    Bitboard oppPawns = board.pieces(PieceType::PAWN, ~color) & Bitboard(File(af));
                    Bitboard oppTmp = oppPawns;
                    while (oppTmp) {
                        Square osq = oppTmp.pop();
                        int orank = osq.rank();
                        if ((color == Color::WHITE && orank > rank) || (color == Color::BLACK && orank < rank)) {
                            isPassed = false;
                            break;
                        }
                    }
                    if (!isPassed) break;
                }
                if (isPassed) {
                    eval += (color == Color::WHITE ? PASSED_PAWN_BONUS : -PASSED_PAWN_BONUS);
                }
            }
        }
    }
    return eval;
}

int evaluateMobility(const Board& board) {
    Movelist movesSTM;
    movegen::legalmoves(movesSTM, board);
    int stmCount = movesSTM.size();

    int countWhite = 0, countBlack = 0;
    Board copy = board;
    if (board.sideToMove() == Color::WHITE) {
        countWhite = stmCount;
        copy.makeNullMove();
        Movelist movesOpp;
        movegen::legalmoves(movesOpp, copy);
        countBlack = movesOpp.size();
    } else {
        countBlack = stmCount;
        copy.makeNullMove();
        Movelist movesOpp;
        movegen::legalmoves(movesOpp, copy);
        countWhite = movesOpp.size();
    }
    return (countWhite - countBlack) * MOBILITY_FACTOR;
}

template <typename Table>
int heatContribution(const Table& pst, Square sq, Color color) {
    int idx = (color == Color::WHITE ? sq.index() : 63 - sq.index());
    return (color == Color::WHITE ? pst[idx] : -pst[idx]);
}

int evaluateHeatmap(const Board& board) {
    int score = 0;
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Bitboard bb;
        bb = board.pieces(PieceType::PAWN, color);
        while (bb) score += heatContribution(PST_PAWN, bb.pop(), color);
        bb = board.pieces(PieceType::KNIGHT, color);
        while (bb) score += heatContribution(PST_KNIGHT, bb.pop(), color);
        bb = board.pieces(PieceType::BISHOP, color);
        while (bb) score += heatContribution(PST_BISHOP, bb.pop(), color);
        bb = board.pieces(PieceType::ROOK, color);
        while (bb) score += heatContribution(PST_ROOK, bb.pop(), color);
        bb = board.pieces(PieceType::QUEEN, color);
        while (bb) score += heatContribution(PST_QUEEN, bb.pop(), color);
        bb = board.pieces(PieceType::KING, color);
        while (bb) score += heatContribution(PST_KING, bb.pop(), color);
    }
    return score;
}

int evaluatePosition(const Board& board) {
    return evaluateMaterial(board) +
           evaluateKingSafety(board) +
           evaluatePawnStructure(board) +
           evaluateMobility(board) +
           evaluateHeatmap(board);
}

int mvvLvaScore(const Move& m, const Board& board) {
    Piece fromPiece = board.at(m.from());
    Piece toPiece = board.at(m.to());
    if (toPiece == Piece::NONE) return 0;
    int victimVal = MATERIAL_VALUE.at(toPiece.type().internal());
    int attackerVal = MATERIAL_VALUE.at(fromPiece.type().internal());
    return victimVal - attackerVal / 10;
}

void orderMoves(Movelist& moves, const Board& board, Move ttMove) {
    std::sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        if (a == ttMove) return true;
        if (b == ttMove) return false;
        return mvvLvaScore(a, board) > mvvLvaScore(b, board);
    });
}

// Quiescence Search
int quiescence(Board& board, int alpha, int beta) {
    int stand_pat = evaluatePosition(board);
    if (board.sideToMove() == Color::BLACK) {
        stand_pat = -stand_pat;
    }

    if (stand_pat >= beta) {
        return beta;
    }
    if (alpha < stand_pat) {
        alpha = stand_pat;
    }

    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, board);
    orderMoves(moves, board, Move::NO_MOVE);

    for (const auto& move : moves) {
        board.makeMove(move);
        int score = -quiescence(board, -beta, -alpha);
        board.unmakeMove(move);

        if (score >= beta) {
            return beta;
        }
        if (score > alpha) {
            alpha = score;
        }
    }
    return alpha;
}

// Principal Variation Search (PVS)
int pvs(Board& board, int depth, int alpha, int beta, bool isPV) {
    if (depth == 0) {
        return quiescence(board, alpha, beta);
    }

    uint64_t key = board.hash();
    TTEntry* ttEntry = probeTT(key);
    if (ttEntry && ttEntry->depth >= depth) {
        if (ttEntry->flag == EXACT) {
            return ttEntry->score;
        } else if (ttEntry->flag == LOWER_BOUND) {
            alpha = max(alpha, ttEntry->score);
        } else if (ttEntry->flag == UPPER_BOUND) {
            beta = min(beta, ttEntry->score);
        }
        if (alpha >= beta) {
            return ttEntry->score;
        }
    }

    Movelist moves;
    movegen::legalmoves(moves, board);
    
    if (moves.empty()) {
        return board.inCheck() ? -1000000 + board.fullMoveNumber() : 0;
    }

    Move bestMove = Move::NO_MOVE;
    if (ttEntry) {
        bestMove = ttEntry->bestMove;
    }
    orderMoves(moves, board, bestMove);


    for (size_t i = 0; i < moves.size(); ++i) {
        board.makeMove(moves[i]);
        int score;
        if (i == 0) {
            score = -pvs(board, depth - 1, -beta, -alpha, true);
        } else {
            score = -pvs(board, depth - 1, -alpha - 1, -alpha, false);
            if (score > alpha && score < beta) {
                score = -pvs(board, depth - 1, -beta, -alpha, true);
            }
        }
        board.unmakeMove(moves[i]);

        if (score > alpha) {
            alpha = score;
            bestMove = moves[i];
            if (alpha >= beta) {
                storeTT(key, bestMove, beta, depth, LOWER_BOUND);
                return beta;
            }
        }
    }
    
    storeTT(key, bestMove, alpha, depth, EXACT);
    return alpha;
}


void search(Board& board, int maxDepth) {
    int alpha = -1000000;
    int beta = 1000000;
    Move bestMove = Move::NO_MOVE;
    
    for (int depth = 1; depth <= maxDepth; ++depth) {
        Movelist moves;
        movegen::legalmoves(moves, board);
        orderMoves(moves, board, bestMove);
        int bestScore = -1000000;
        
        for(const auto& move : moves) {
            board.makeMove(move);
            int score = -pvs(board, depth - 1, -beta, -alpha, false);
            board.unmakeMove(move);
            
            if (score > bestScore) {
                bestScore = score;
                bestMove = move;
            }
        }
        
        cout << "info depth " << depth << " score cp " << bestScore << " pv " << uci::moveToUci(bestMove) << endl;
    }
    
    cout << "bestmove " << uci::moveToUci(bestMove) << endl;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    resizeTT(128); // Default hash size

    Board board;
    string line;

    while (getline(cin, line)) {
        istringstream iss(line);
        string token;
        iss >> token;

        if (token == "uci") {
            cout << "id name ProGamerEngine" << endl;
            cout << "id author You" << endl;
            cout << "option name Move Overhead type spin default 0 min 0 max 5000" << endl;
            cout << "option name Hash type spin default 128 min 1 max 1024" << endl;
            cout << "option name Threads type spin default 1 min 1 max 4" << endl;
            cout << "option name SyzygyPath type string default ./syzygy" << endl;
            cout << "option name UCI_ShowWDL type check default false" << endl;
            cout << "uciok" << endl;
        } else if (token == "isready") {
            cout << "readyok" << endl;
        } else if (token == "ucinewgame") {
            board = Board::fromFen(constants::STARTPOS);
            clearTT();
        } else if (token == "setoption") {
            string name, value;
            iss >> name >> name >> value >> value;
            if (name == "Hash") {
                resizeTT(stoi(value));
            }
        } else if (token == "position") {
            string sub;
            iss >> sub;
            if (sub == "startpos") {
                board = Board::fromFen(constants::STARTPOS);
                if (iss >> sub && sub == "moves") {
                    while (iss >> sub) {
                        Move m = uci::uciToMove(board, sub);
                        board.makeMove(m);
                    }
                }
            } else if (sub == "fen") {
                string fen;
                while (iss >> sub) {
                    fen += sub + " ";
                }
                board = Board::fromFen(fen);
            }
        } else if (token == "go") {
            int depth = 6; // Default search depth
            string depth_str;
            while(iss >> depth_str) {
                if(depth_str == "depth") {
                    iss >> depth;
                }
            }
            search(board, depth);
        } else if (token == "quit") {
            break;
        }
    }

    return 0;
}