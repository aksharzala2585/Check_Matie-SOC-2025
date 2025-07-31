#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <limits>
#include "chess.hpp"

using namespace chess;

// --------------------------------------------------
// Static evaluation components (material, mobility, king safety, pawn structure,
// space control, piece coordination). Returns centipawn score from White’s perspective.

// Material values in centipawns
static const std::unordered_map<PieceType::underlying, int> MATERIAL_VALUE = {
    {PieceType::PAWN, 100},
    {PieceType::KNIGHT, 320},
    {PieceType::BISHOP, 330},
    {PieceType::ROOK, 500},
    {PieceType::QUEEN, 900},
    {PieceType::KING, 0}};

// Penalties/bonuses in centipawns
static constexpr int ISOLATED_PAWN_PENALTY   = 20;
static constexpr int DOUBLED_PAWN_PENALTY    = 10;
static constexpr int PASSED_PAWN_BONUS       = 30;
static constexpr int MOBILITY_FACTOR         = 10;
static constexpr int KING_SAFETY_PENALTY     = 50;
static constexpr int SPACE_CONTROL_FACTOR    = 1;
static constexpr int COORDINATION_BONUS      = 10;

// Evaluate material: positive if White ahead, negative if Black ahead
int evaluateMaterial(const Board &board) {
    int eval = 0;
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN}) {
        PieceType pt(pt_under);
        int val = MATERIAL_VALUE.at(pt.internal());
        int wc  = board.pieces(pt, Color::WHITE).count();
        int bc  = board.pieces(pt, Color::BLACK).count();
        eval += (wc - bc) * val;
    }
    return eval;
}

// Evaluate mobility: (#White legal moves - #Black legal moves) * factor
int evaluateMobility(const Board &board) {
    Movelist movesSTM;
    movegen::legalmoves(movesSTM, board);
    int stmCount = movesSTM.size();

    int countWhite = 0, countBlack = 0;
    Board copy = board;
    if (board.sideToMove() == Color::WHITE) {
        countWhite = stmCount;
        copy.makeNullMove(); // flip to Black
        Movelist movesOpp;
        movegen::legalmoves(movesOpp, copy);
        countBlack = movesOpp.size();
    } else {
        countBlack = stmCount;
        copy.makeNullMove(); // flip to White
        Movelist movesOpp;
        movegen::legalmoves(movesOpp, copy);
        countWhite = movesOpp.size();
    }
    return (countWhite - countBlack) * MOBILITY_FACTOR;
}

// Evaluate king safety: count opponent attacks to squares around each king
int evaluateKingSafety(const Board &board) {
    int eval = 0;
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Square kingSq = board.kingSq(color);
        Bitboard around = attacks::king(kingSq);
        Color opp = ~color;
        int threats = 0;
        Bitboard temp = around;
        while (temp) {
            Square sq = temp.pop();
            if (board.isAttacked(sq, opp)) threats++;
        }
        eval += (color == Color::WHITE ? -threats : threats) * KING_SAFETY_PENALTY;
    }
    return eval;
}

// Evaluate pawn structure: doubled, isolated penalties; passed pawn bonuses
int evaluatePawnStructure(const Board &board) {
    int eval = 0;
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Bitboard pawns = board.pieces(PieceType::PAWN, color);
        for (int f = 0; f < 8; f++) {
            Bitboard fileMask      = Bitboard(File(f));
            Bitboard thisFilePawns = pawns & fileMask;
            int countOnFile        = thisFilePawns.count();
            // Doubled pawns
            if (countOnFile > 1) {
                int penalty = (countOnFile - 1) * DOUBLED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            // Isolated pawns
            bool hasLeft  = (f > 0) && ((pawns & Bitboard(File(f - 1))).count() > 0);
            bool hasRight = (f < 7) && ((pawns & Bitboard(File(f + 1))).count() > 0);
            if (countOnFile > 0 && !hasLeft && !hasRight) {
                int penalty = countOnFile * ISOLATED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            // Passed pawns
            Bitboard tmp = thisFilePawns;
            while (tmp) {
                Square pSq = tmp.pop();
                int rank = pSq.rank();
                bool isPassed = true;
                for (int af = std::max(0, f - 1); af <= std::min(7, f + 1); af++) {
                    Bitboard oppPawns = board.pieces(PieceType::PAWN, ~color) & Bitboard(File(af));
                    Bitboard oppTmp   = oppPawns;
                    while (oppTmp) {
                        Square osq = oppTmp.pop();
                        int orank = osq.rank();
                        if ((color == Color::WHITE && orank > rank) ||
                            (color == Color::BLACK && orank < rank)) {
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

// Evaluate space control: (# attacked squares by White - # attacked squares by Black) * factor
int evaluateSpaceControl(const Board &board) {
    Bitboard occAll = board.occ();
    Bitboard attacksW = 0ull, attacksB = 0ull;
    // White attacks
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
        PieceType pt(pt_under);
        Bitboard bb = board.pieces(pt, Color::WHITE);
        while (bb) {
            Square sq = bb.pop();
            Bitboard att;
            switch (pt.internal()) {
                case PieceType::PAWN:   att = attacks::pawn(Color::WHITE, sq); break;
                case PieceType::KNIGHT: att = attacks::knight(sq);            break;
                case PieceType::BISHOP: att = attacks::bishop(sq, occAll);    break;
                case PieceType::ROOK:   att = attacks::rook(sq, occAll);      break;
                case PieceType::QUEEN:  att = attacks::queen(sq, occAll);     break;
                case PieceType::KING:   att = attacks::king(sq);              break;
                default:                att = 0ull;                          break;
            }
            attacksW |= att;
        }
    }
    // Black attacks
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
        PieceType pt(pt_under);
        Bitboard bb = board.pieces(pt, Color::BLACK);
        while (bb) {
            Square sq = bb.pop();
            Bitboard att;
            switch (pt.internal()) {
                case PieceType::PAWN:   att = attacks::pawn(Color::BLACK, sq); break;
                case PieceType::KNIGHT: att = attacks::knight(sq);            break;
                case PieceType::BISHOP: att = attacks::bishop(sq, occAll);    break;
                case PieceType::ROOK:   att = attacks::rook(sq, occAll);      break;
                case PieceType::QUEEN:  att = attacks::queen(sq, occAll);     break;
                case PieceType::KING:   att = attacks::king(sq);              break;
                default:                att = 0ull;                          break;
            }
            attacksB |= att;
        }
    }
    return (attacksW.count() - attacksB.count()) * SPACE_CONTROL_FACTOR;
}

// Evaluate piece coordination: count protected pieces
int evaluatePieceCoordination(const Board &board) {
    int eval = 0;
    Bitboard occAll = board.occ();
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Bitboard friendlyAttacks = 0ull;
        // Build friendly attacks
        for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                              PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
            PieceType pt(pt_under);
            Bitboard bb = board.pieces(pt, color);
            while (bb) {
                Square sq = bb.pop();
                Bitboard att;
                switch (pt.internal()) {
                    case PieceType::PAWN:   att = attacks::pawn(color, sq);   break;
                    case PieceType::KNIGHT: att = attacks::knight(sq);        break;
                    case PieceType::BISHOP: att = attacks::bishop(sq, occAll);break;
                    case PieceType::ROOK:   att = attacks::rook(sq, occAll);  break;
                    case PieceType::QUEEN:  att = attacks::queen(sq, occAll); break;
                    case PieceType::KING:   att = attacks::king(sq);          break;
                    default:                att = 0ull;                      break;
                }
                friendlyAttacks |= att;
            }
        }
        // Count protected pieces
        int protectedCount = 0;
        for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                              PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
            PieceType pt(pt_under);
            Bitboard bb = board.pieces(pt, color);
            while (bb) {
                Square sq = bb.pop();
                if ((friendlyAttacks & Bitboard::fromSquare(sq)).count() > 0)
                    protectedCount++;
            }
        }
        eval += (color == Color::WHITE ? protectedCount : -protectedCount) * COORDINATION_BONUS;
    }
    return eval;
}

// Wrapper: full static evaluation
int evaluatePosition(const Board &board)
{
    return evaluateMaterial(board)
         + evaluateMobility(board)
         + evaluateKingSafety(board)
         + evaluatePawnStructure(board)
         + evaluateSpaceControl(board)
         + evaluatePieceCoordination(board);
}

// --------------------------------------------------
// Negamax search with alpha-beta and transposition table

static constexpr int INF = 10000000;

struct TTEntry {
    int depth;
    int score;
    enum Flag { EXACT, LOWERBOUND, UPPERBOUND } flag;
    Move bestMove;
};
static std::unordered_map<uint64_t, TTEntry> TT;

int alphaBeta(Board &board, int depth, int alpha, int beta)
{
    uint64_t key = board.hash();
    auto it = TT.find(key);
    if (it != TT.end() && it->second.depth >= depth) {
        TTEntry &e = it->second;
        if (e.flag == TTEntry::EXACT)
            return e.score;
        if (e.flag == TTEntry::LOWERBOUND)
            alpha = std::max(alpha, e.score);
        else if (e.flag == TTEntry::UPPERBOUND)
            beta  = std::min(beta, e.score);
        if (alpha >= beta)
            return e.score;
    }

    auto [reason, result] = board.isGameOver();
    if (reason != GameResultReason::NONE) {
        if (result == GameResult::LOSE)  return -INF + 1;
        if (result == GameResult::DRAW)  return 0;
        if (result == GameResult::WIN)   return INF - 1;
    }
    if (depth == 0) {
        int eval = evaluatePosition(board);
        return (board.sideToMove() == Color::WHITE ? eval : -eval);
    }

    Movelist moves;
    movegen::legalmoves(moves, board);
    if (moves.empty())
        return 0; // no moves -> draw fallback

    Move ttMove = Move::NO_MOVE;
    if (it != TT.end() && it->second.bestMove.move() != Move::NO_MOVE)
        ttMove = it->second.bestMove;

    std::vector<Move> ordered;
    ordered.reserve(moves.size());
    if (ttMove != Move::NO_MOVE) {
        ordered.push_back(ttMove);
    }
    for (auto &m : moves) {
        if (m != ttMove)
            ordered.push_back(m);
    }

    int bestScore = -INF;
    Move bestMoveLocal = Move::NO_MOVE;

    for (auto &mov : ordered) {
        board.makeMove(mov);
        int score = -alphaBeta(board, depth - 1, -beta, -alpha);
        board.unmakeMove(mov);
        if (score > bestScore) {
            bestScore = score;
            bestMoveLocal = mov;
        }
        alpha = std::max(alpha, score);
        if (alpha >= beta)
            break;
    }

    TTEntry e{depth, bestScore, TTEntry::EXACT, bestMoveLocal};
    if (bestScore <= alpha)       e.flag = TTEntry::UPPERBOUND;
    else if (bestScore >= beta)   e.flag = TTEntry::LOWERBOUND;
    TT[key] = e;

    return bestScore;
}

std::pair<int, Move> findBestMove(Board &board, int depth)
{
    Movelist moves;
    movegen::legalmoves(moves, board);
    if (moves.empty()) return {0, Move::NO_MOVE};

    int alpha = -INF, beta = INF, bestScore = -INF;
    Move bestMove = Move::NO_MOVE;
    for (auto &m : moves) {
        board.makeMove(m);
        int score = -alphaBeta(board, depth - 1, -beta, -alpha);
        board.unmakeMove(m);
        if (score > bestScore) {
            bestScore = score;
            bestMove  = m;
        }
        alpha = std::max(alpha, score);
        if (alpha >= beta) break;
    }
    return { bestScore, bestMove };
}

// --------------------------------------------------
// UCI loop

int searchDepth = 4;

void handlePosition(Board &board, const std::string &args)
{
    std::istringstream iss(args);
    std::string token;
    if (!(iss >> token)) return;
    if (token == "startpos") {
#ifdef STARTPOS
        board = Board::fromFen(constants::STARTPOS);
#else
        board = Board::fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
#endif
    } else if (token == "fen") {
        std::string fenFull, part;
        for (int i = 0; i < 6 && (iss >> part); ++i) {
            if (i) fenFull += ' ';
            fenFull += part;
        }
        board = Board::fromFen(fenFull);
    }
    if (iss >> token && token == "moves") {
        while (iss >> token) {
            if (!uci::isUciMove(token)) break;
            Move m = uci::uciToMove(board, token);
            board.makeMove(m);
        }
    }
}

void handleGo(const Board &currentBoard)
{
    Board board = currentBoard;
    TT.clear();
    auto [score, best] = findBestMove(board, searchDepth);
    std::string uciStr = (best == Move::NO_MOVE ? "0000" : uci::moveToUci(best));
    std::cout << "bestmove " << uciStr << '\n';
}

int main() {
    Board board = Board::fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    std::string input;

    while (true) {
        // Print current position
        // Check for game over
        auto [reason, result] = board.isGameOver();
        if (reason != GameResultReason::NONE) {
            if (result == GameResult::WIN) {
                std::cout << (board.sideToMove() == Color::WHITE ? "Black" : "White") << " wins!\n";
            } else if (result == GameResult::DRAW) {
                std::cout << "Draw!\n";
            } else {
                std::cout << "Game over!\n";
            }
            break;
        }

        // Human move (assuming White is human)
        if (board.sideToMove() == Color::WHITE) {
            std::cout << "Your move (in UCI format, e.g. e2e4): ";
            std::cin >> input;

            if (input == "quit") break;

            if (!uci::isUciMove(input)) {
                std::cout << "Invalid format.\n";
                continue;
            }

            Move m;
            try {
                m = uci::uciToMove(board, input);
            } catch (...) {
                std::cout << "Could not parse move.\n";
                continue;
            }

            Movelist legal;
            movegen::legalmoves(legal, board);
            bool legalMove = false;
            for (auto &lm : legal) {
                if (lm == m) {
                    legalMove = true;
                    break;
                }
            }
            if (!legalMove) {
                std::cout << "Illegal move.\n";
                continue;
            }

            board.makeMove(m);
        } else {
            // Engine's turn (Black)
            std::cout << "Engine thinking...\n";
            TT.clear();
            auto [score, best] = findBestMove(board, searchDepth);
            if (best == Move::NO_MOVE) {
                std::cout << "No legal moves.\n";
                break;
            }
            std::cout << "Engine plays: " << uci::moveToUci(best) << "\n";
            board.makeMove(best);
        }
    }

    return 0;
}
