#include <iostream>
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
    {PieceType::PAWN,   100},
    {PieceType::KNIGHT, 320},
    {PieceType::BISHOP, 330},
    {PieceType::ROOK,   500},
    {PieceType::QUEEN,  900},
    {PieceType::KING,     0}
};

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
        int wc = board.pieces(pt, Color::WHITE).count();
        int bc = board.pieces(pt, Color::BLACK).count();
        eval += (wc - bc) * val;
    }
    return eval;
}

// Evaluate mobility: (#legal moves for side to move - #legal moves for opponent) * factor
int evaluateMobility(const Board &board) {
    // We want (#White legal moves) - (#Black legal moves)  multiplied by MOBILITY_FACTOR,
    // so that positive means White has greater mobility and negative means Black does.
    //
    // Using null-move: make a copy of the board, call makeNullMove() to flip side,
    // then generate legal moves for the flipped side.

    // Count legal moves for side-to-move first:
    Movelist movesSTM;
    movegen::legalmoves(movesSTM, board);
    int stmCount = movesSTM.size();

    int countWhite = 0;
    int countBlack = 0;

    if (board.sideToMove() == Color::WHITE) {
        // Side to move is White, so stmCount is White’s moves.
        countWhite = stmCount;
        // For Black moves: copy board, flip side via null move:
        Board copy = board;
        copy.makeNullMove();  // now Black to move
        Movelist movesOpp;
        movegen::legalmoves(movesOpp, copy);
        countBlack = movesOpp.size();
    } else {
        // Side to move is Black, so stmCount is Black’s moves.
        countBlack = stmCount;
        // For White moves: copy board, flip side via null move:
        Board copy = board;
        copy.makeNullMove();  // now White to move
        Movelist movesOpp;
        movegen::legalmoves(movesOpp, copy);
        countWhite = movesOpp.size();
    }

    return (countWhite - countBlack) * MOBILITY_FACTOR/3;
}
// Evaluate king safety: count threats near each king
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
            if (board.isAttacked(sq, opp)) {
                threats++;
            }
        }
        if (color == Color::WHITE) {
            eval -= threats * KING_SAFETY_PENALTY;
        } else {
            eval += threats * KING_SAFETY_PENALTY;
        }
    }
    return eval;
}

// Evaluate pawn structure: doubled, isolated penalties; passed pawn bonuses
int evaluatePawnStructure(const Board &board) {
    int eval = 0;
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Bitboard pawns = board.pieces(PieceType(PieceType::PAWN), color);
        for (int f = 0; f < 8; f++) {
            Bitboard fileMask = Bitboard(File(f));
            Bitboard thisFilePawns = pawns & fileMask;
            int countOnFile = thisFilePawns.count();
            // Doubled pawns
            if (countOnFile > 1) {
                int penalty = (countOnFile - 1) * DOUBLED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            // Isolated pawns
            bool hasLeft = (f > 0) && ((pawns & Bitboard(File(f - 1))).count() > 0);
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
                    Bitboard oppPawns = board.pieces(PieceType(PieceType::PAWN), ~color) & Bitboard(File(af));
                    Bitboard oppTmp = oppPawns;
                    while (oppTmp) {
                        Square osq = oppTmp.pop();
                        int orank = osq.rank();
                        if (color == Color::WHITE) {
                            if (orank > rank) {
                                isPassed = false;
                                break;
                            }
                        } else {
                            if (orank < rank) {
                                isPassed = false;
                                break;
                            }
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
    Bitboard attacksW = 0ull;
    Bitboard attacksB = 0ull;
    // White attacks
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
        PieceType pt(pt_under);
        Bitboard bb = board.pieces(pt, Color::WHITE);
        while (bb) {
            Square sq = bb.pop();
            Bitboard att = 0ull;
            switch (pt.internal()) {
                case PieceType::PAWN:   att = attacks::pawn(Color::WHITE, sq); break;
                case PieceType::KNIGHT: att = attacks::knight(sq); break;
                case PieceType::BISHOP: att = attacks::bishop(sq, occAll); break;
                case PieceType::ROOK:   att = attacks::rook(sq, occAll); break;
                case PieceType::QUEEN:  att = attacks::queen(sq, occAll); break;
                case PieceType::KING:   att = attacks::king(sq); break;
                default: break;
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
            Bitboard att = 0ull;
            switch (pt.internal()) {
                case PieceType::PAWN:   att = attacks::pawn(Color::BLACK, sq); break;
                case PieceType::KNIGHT: att = attacks::knight(sq); break;
                case PieceType::BISHOP: att = attacks::bishop(sq, occAll); break;
                case PieceType::ROOK:   att = attacks::rook(sq, occAll); break;
                case PieceType::QUEEN:  att = attacks::queen(sq, occAll); break;
                case PieceType::KING:   att = attacks::king(sq); break;
                default: break;
            }
            attacksB |= att;
        }
    }
    int ctrlW = attacksW.count();
    int ctrlB = attacksB.count();
    return (ctrlW - ctrlB) * SPACE_CONTROL_FACTOR;
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
                Bitboard att = 0ull;
                switch (pt.internal()) {
                    case PieceType::PAWN:   att = attacks::pawn(color, sq); break;
                    case PieceType::KNIGHT: att = attacks::knight(sq); break;
                    case PieceType::BISHOP: att = attacks::bishop(sq, occAll); break;
                    case PieceType::ROOK:   att = attacks::rook(sq, occAll); break;
                    case PieceType::QUEEN:  att = attacks::queen(sq, occAll); break;
                    case PieceType::KING:   att = attacks::king(sq); break;
                    default: break;
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
                Bitboard mask = Bitboard::fromSquare(sq);
                if ((friendlyAttacks & mask).count() > 0) {
                    protectedCount++;
                }
            }
        }
        if (color == Color::WHITE) {
            eval += protectedCount * COORDINATION_BONUS;
        } else {
            eval -= protectedCount * COORDINATION_BONUS;
        }
    }
    return eval;
}

// Wrapper: full static evaluation
int evaluatePosition(const Board &board) {
    int mat  = evaluateMaterial(board);
    int mob  = evaluateMobility(board);
    int ks   = evaluateKingSafety(board);
    int pst  = evaluatePawnStructure(board);
    int spc  = evaluateSpaceControl(board);
    int coord= evaluatePieceCoordination(board);
    int total = mat + mob + ks + pst + spc + coord;
    return total;
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

// Negamax with alpha-beta. Returns centipawn score from side-to-move’s perspective,
// in a framework where positive = good for White to move, negative = good for Black to move.
// We implement: value = (board.sideToMove()==White ? +eval : -eval) at leaf, and use negation on recursion.
int alphaBeta(Board &board, int depth, int alpha, int beta) {
    uint64_t key = board.hash();
    // Transposition lookup
    auto it = TT.find(key);
    if (it != TT.end()) {
        TTEntry &e = it->second;
        if (e.depth >= depth) {
            if (e.flag == TTEntry::EXACT) {
                return e.score;
            } else if (e.flag == TTEntry::LOWERBOUND) {
                alpha = std::max(alpha, e.score);
            } else if (e.flag == TTEntry::UPPERBOUND) {
                beta = std::min(beta, e.score);
            }
            if (alpha >= beta) {
                return e.score;
            }
        }
    }

    // Terminal test
    auto [reason, result] = board.isGameOver();
    if (reason != GameResultReason::NONE) {
        // If side to move has no legal moves:
        if (result == GameResult::LOSE) {
            // side to move is checkmated
            return -INF + 1;
        } else if (result == GameResult::DRAW) {
            return 0;
        } else if (result == GameResult::WIN) {
            // Very rare: side to move has immediate win?
            return INF - 1;
        }
    }
    if (depth == 0) {
        int eval = evaluatePosition(board);
        // Return from White’s perspective: if Black to move, negate
        return (board.sideToMove() == Color::WHITE ? eval : -eval);
    }

    // Generate legal moves
    Movelist moves;
    movegen::legalmoves(moves, board);
    if (moves.empty()) {
        // No moves but not caught above? Treat as draw
        return 0;
    }

    // Move ordering: try TT move first if available
    Move ttMove = Move::NO_MOVE;
    bool hasTTmove = false;
    if (it != TT.end() && it->second.bestMove.move() != Move::NO_MOVE) {
        ttMove = it->second.bestMove;
        hasTTmove = true;
    }
    std::vector<Move> orderedMoves;
    orderedMoves.reserve(moves.size());
    if (hasTTmove) {
        for (const Move &m : moves) {
            if (m == ttMove) {
                orderedMoves.push_back(m);
                break;
            }
        }
    }
    for (const Move &m : moves) {
        if (!(hasTTmove && m == ttMove)) {
            orderedMoves.push_back(m);
        }
    }

    int bestScore = -INF;
    Move bestMoveLocal = Move::NO_MOVE;

    for (const Move &move : orderedMoves) {
        board.makeMove(move);
        int score = -alphaBeta(board, depth - 1, -beta, -alpha);
        board.unmakeMove(move);
        if (score > bestScore) {
            bestScore = score;
            bestMoveLocal = move;
        }
        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break; // cutoff
        }
    }

    // Store in TT
    TTEntry entry;
    entry.depth = depth;
    entry.score = bestScore;
    entry.bestMove = bestMoveLocal;
    if (bestScore <= alpha) {
        entry.flag = TTEntry::UPPERBOUND;
    } else if (bestScore >= beta) {
        entry.flag = TTEntry::LOWERBOUND;
    } else {
        entry.flag = TTEntry::EXACT;
    }
    TT[key] = entry;

    return bestScore;
}

// Root driver: find best move at given depth
std::pair<int, Move> findBestMove(Board &board, int depth) {
    Movelist moves;
    movegen::legalmoves(moves, board);
    if (moves.empty()) {
        return {0, Move::NO_MOVE};
    }

    int alpha = -INF;
    int beta = INF;
    int bestScore = -INF;
    Move bestMove = Move::NO_MOVE;
    // Optionally reorder root moves (e.g., captures first), but here simple iteration
    for (const Move &m : moves) {
        board.makeMove(m);
        int score = -alphaBeta(board, depth - 1, -beta, -alpha);
        board.unmakeMove(m);
        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;
        }
    }
    return {bestScore, bestMove};
}

// --------------------------------------------------
// Main: read FEN, read depth, search, output best move and evaluation
int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string fen;
    if (!std::getline(std::cin, fen) || fen.empty()) {
        std::cerr << "Failed to read FEN\n";
        return 1;
    }
    Board board;
    try {
        board = Board::fromFen(fen);
    } catch (const std::exception &e) {
        std::cerr << "Error parsing FEN: " << e.what() << "\n";
        return 1;
    }

    int depth;
    std::cout << "Enter search depth: ";
    if (!(std::cin >> depth) || depth < 1) {
        std::cerr << "Invalid depth\n";
        return 1;
    }

    // Clear transposition table before search
    TT.clear();

    auto [score, bestMove] = findBestMove(board, depth);
    if (bestMove == Move::NO_MOVE) {
        std::cout << "No legal moves\n";
    } else {
        // Print best move in UCI
        std::string San = uci::moveToSan(board, bestMove);
        // Or SAN: std::string sanStr = uci::moveToSan(board, bestMove);
        double pawnScore = score / 100.0;
        std::cout << "Best move: " << San << "\n";
        std::cout << "Eval: " << (pawnScore >= 0 ? "+" : "") << pawnScore
                  << " at depth " << depth << "\n";
    }

    return 0;
}
