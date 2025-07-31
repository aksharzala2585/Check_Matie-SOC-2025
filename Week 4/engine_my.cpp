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

#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <limits>
#include "chess.hpp"

using namespace chess;

// Current search depth
typedef std::array<int, 64> PST;

// Piece-square tables (heatmaps) for White; Black uses mirrored indices
static const PST PST_PAWN = {
      0,   0,   0,   0,   0,   0,   0,   0,
     50,  50,  50,  50,  50,  50,  50,  50,
     10,  10,  20,  30,  30,  20,  10,  10,
      5,   5,  10,  25,  25,  10,   5,   5,
      0,   0,   0,  20,  20,   0,   0,   0,
      5,  -5, -10,   0,   0, -10,  -5,   5,
      5,  10,  10, -20, -20,  10,  10,   5,
      0,   0,   0,   0,   0,   0,   0,   0
};
static const PST PST_KNIGHT = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};
static const PST PST_BISHOP = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};
static const PST PST_ROOK = {
      0,   0,   0,   0,   0,   0,   0,   0,
      5,  10,  10,  10,  10,  10,  10,   5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
      0,   0,   0,   5,   5,   0,   0,   0
};
static const PST PST_QUEEN = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,   0,   0,  0,  0,   0,-10,
    -10,  0,   5,   5,  5,  5,   0,-10,
     -5,  0,   5,   5,  5,  5,   0, -5,
      0,  0,   5,   5,  5,  5,   0, -5,
    -10,  5,   5,   5,  5,  5,   0,-10,
    -10,  0,   5,   0,  0,  0,   0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};
static const PST PST_KING = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,   0,   0,   0,   0,  20,  20,
     20, 30,  10,   0,   0,  10,  30,  20
};

// Evaluate piece-square heatmap contribution
template<typename Table>
int heatContribution(const Table &pst, Square sq, Color color) {
    int idx = (color == Color::WHITE ? sq.index() : 63 - sq.index());
    return (color == Color::WHITE ? pst[idx] : -pst[idx]);
}

int evaluateHeatmap(const Board &board) {
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
    return score/4;
}

// Wrapper: full static evaluation with heatmap
static const std::unordered_map<PieceType::underlying, int> MATERIAL_VALUE = {
    {PieceType::PAWN, 100},
    {PieceType::KNIGHT, 320},
    {PieceType::BISHOP, 330},
    {PieceType::ROOK, 500},
    {PieceType::QUEEN, 900},
    {PieceType::KING, 0}};

// Penalties/bonuses in centipawns
static constexpr int ISOLATED_PAWN_PENALTY = 20;
static constexpr int DOUBLED_PAWN_PENALTY = 10;
static constexpr int PASSED_PAWN_BONUS = 30;
static constexpr int MOBILITY_FACTOR = 10;
static constexpr int KING_SAFETY_PENALTY = 50;
static constexpr int SPACE_CONTROL_FACTOR = 1;
static constexpr int COORDINATION_BONUS = 10;

// Evaluate material: positive if White ahead, negative if Black ahead
int evaluateMaterial(const Board &board)
{
    int eval = 0;
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN})
    {
        PieceType pt(pt_under);
        int val = MATERIAL_VALUE.at(pt.internal());
        int wc = board.pieces(pt, Color::WHITE).count();
        int bc = board.pieces(pt, Color::BLACK).count();
        eval += (wc - bc) * val;
    }
    return eval;
}


// Evaluate king safety: count opponent attacks to squares around each king
int evaluateKingSafety(const Board &board)
{
    int eval = 0;
    for (Color color : {Color::WHITE, Color::BLACK})
    {
        Square kingSq = board.kingSq(color);
        Bitboard around = attacks::king(kingSq);
        Color opp = ~color;
        int threats = 0;
        Bitboard temp = around;
        while (temp)
        {
            Square sq = temp.pop();
            if (board.isAttacked(sq, opp))
                threats++;
        }
        eval += (color == Color::WHITE ? -threats : threats) * KING_SAFETY_PENALTY;
    }
    return eval;
}

// Evaluate pawn structure: doubled, isolated penalties; passed pawn bonuses
int evaluatePawnStructure(const Board &board)
{
    int eval = 0;
    for (Color color : {Color::WHITE, Color::BLACK})
    {
        Bitboard pawns = board.pieces(PieceType::PAWN, color);
        for (int f = 0; f < 8; f++)
        {
            Bitboard fileMask = Bitboard(File(f));
            Bitboard thisFilePawns = pawns & fileMask;
            int countOnFile = thisFilePawns.count();
            // Doubled pawns
            if (countOnFile > 1)
            {
                int penalty = (countOnFile - 1) * DOUBLED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            // Isolated pawns
            bool hasLeft = (f > 0) && ((pawns & Bitboard(File(f - 1))).count() > 0);
            bool hasRight = (f < 7) && ((pawns & Bitboard(File(f + 1))).count() > 0);
            if (countOnFile > 0 && !hasLeft && !hasRight)
            {
                int penalty = countOnFile * ISOLATED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            // Passed pawns
            Bitboard tmp = thisFilePawns;
            while (tmp)
            {
                Square pSq = tmp.pop();
                int rank = pSq.rank();
                bool isPassed = true;
                for (int af = std::max(0, f - 1); af <= std::min(7, f + 1); af++)
                {
                    Bitboard oppPawns = board.pieces(PieceType::PAWN, ~color) & Bitboard(File(af));
                    Bitboard oppTmp = oppPawns;
                    while (oppTmp)
                    {
                        Square osq = oppTmp.pop();
                        int orank = osq.rank();
                        if ((color == Color::WHITE && orank > rank) ||
                            (color == Color::BLACK && orank < rank))
                        {
                            isPassed = false;
                            break;
                        }
                    }
                    if (!isPassed)
                        break;
                }
                if (isPassed)
                {
                    eval += (color == Color::WHITE ? PASSED_PAWN_BONUS : -PASSED_PAWN_BONUS);
                }
            }
        }
    }
    return eval;
}

// Evaluate space control: (# attacked squares by White - # attacked squares by Black) * factor
int evaluateSpaceControl(const Board &board)
{
    Bitboard occAll = board.occ();
    Bitboard attacksW = 0ull, attacksB = 0ull;
    // White attacks
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
    {
        PieceType pt(pt_under);
        Bitboard bb = board.pieces(pt, Color::WHITE);
        while (bb)
        {
            Square sq = bb.pop();
            Bitboard att;
            switch (pt.internal())
            {
            case PieceType::PAWN:
                att = attacks::pawn(Color::WHITE, sq);
                break;
            case PieceType::KNIGHT:
                att = attacks::knight(sq);
                break;
            case PieceType::BISHOP:
                att = attacks::bishop(sq, occAll);
                break;
            case PieceType::ROOK:
                att = attacks::rook(sq, occAll);
                break;
            case PieceType::QUEEN:
                att = attacks::queen(sq, occAll);
                break;
            case PieceType::KING:
                att = attacks::king(sq);
                break;
            default:
                att = 0ull;
                break;
            }
            attacksW |= att;
        }
    }
    // Black attacks
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
    {
        PieceType pt(pt_under);
        Bitboard bb = board.pieces(pt, Color::BLACK);
        while (bb)
        {
            Square sq = bb.pop();
            Bitboard att;
            switch (pt.internal())
            {
            case PieceType::PAWN:
                att = attacks::pawn(Color::BLACK, sq);
                break;
            case PieceType::KNIGHT:
                att = attacks::knight(sq);
                break;
            case PieceType::BISHOP:
                att = attacks::bishop(sq, occAll);
                break;
            case PieceType::ROOK:
                att = attacks::rook(sq, occAll);
                break;
            case PieceType::QUEEN:
                att = attacks::queen(sq, occAll);
                break;
            case PieceType::KING:
                att = attacks::king(sq);
                break;
            default:
                att = 0ull;
                break;
            }
            attacksB |= att;
        }
    }
    return (attacksW.count() - attacksB.count()) * SPACE_CONTROL_FACTOR;
}

// Evaluate piece coordination: count protected pieces
int evaluatePieceCoordination(const Board &board)
{
    int eval = 0;
    Bitboard occAll = board.occ();
    for (Color color : {Color::WHITE, Color::BLACK})
    {
        Bitboard friendlyAttacks = 0ull;
        // Build friendly attacks
        for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                              PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
        {
            PieceType pt(pt_under);
            Bitboard bb = board.pieces(pt, color);
            while (bb)
            {
                Square sq = bb.pop();
                Bitboard att;
                switch (pt.internal())
                {
                case PieceType::PAWN:
                    att = attacks::pawn(color, sq);
                    break;
                case PieceType::KNIGHT:
                    att = attacks::knight(sq);
                    break;
                case PieceType::BISHOP:
                    att = attacks::bishop(sq, occAll);
                    break;
                case PieceType::ROOK:
                    att = attacks::rook(sq, occAll);
                    break;
                case PieceType::QUEEN:
                    att = attacks::queen(sq, occAll);
                    break;
                case PieceType::KING:
                    att = attacks::king(sq);
                    break;
                default:
                    att = 0ull;
                    break;
                }
                friendlyAttacks |= att;
            }
        }
        // Count protected pieces
        int protectedCount = 0;
        for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                              PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
        {
            PieceType pt(pt_under);
            Bitboard bb = board.pieces(pt, color);
            while (bb)
            {
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
    return evaluateMaterial(board) + evaluateKingSafety(board) + evaluatePawnStructure(board) + evaluateSpaceControl(board) + evaluatePieceCoordination(board) + evaluateHeatmap(board);
}

// --------------------------------------------------
// Negamax search with alpha-beta and transposition table
#include "chess.hpp"
#include <bits/stdc++.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <chrono>
#include <future>
using namespace std;
using namespace chess;

int piece_value(const chess::Piece &pc)
{
    using U = chess::PieceType::underlying;
    U pt = pc.type().internal();

    switch (pt)
    {
    case U::PAWN:
        return 100;
    case U::KNIGHT:
        return 300;
    case U::BISHOP:
        return 320;
    case U::ROOK:
        return 500;
    case U::QUEEN:
        return 900;
    case U::KING:
        return 10000;
    default:
        return 0;
    }
}
int searchDepth = 5;
int eval(Board &board, int depth, int p,
         std::unordered_map<uint64_t, Move> &mp,
         std::unordered_map<uint64_t, std::pair<bool, bool>> &gameOver,
         int alpha, int beta)
{
    uint64_t key = board.hash();
    Movelist moves;
    movegen::legalmoves(moves, board);

    if (depth == 0)
    {
        int score = evaluatePosition(board);
        return (p == 1 ? score : -score);
    }

    bool isMaximizing = (board.sideToMove() == (p == 1 ? Color::WHITE : Color::BLACK));
    if (isMaximizing)
    {
        int best = std::numeric_limits<int>::min();
        for (const auto &move : moves)
        {
            board.makeMove(move);
            int val = eval(board, depth - 1, p, mp, gameOver, alpha, beta);
            board.unmakeMove(move);

            if (val > best)
            {
                best = val;
                if (depth == searchDepth)
                    mp[key] = move;
            }
            alpha = std::max(alpha, val);
            if (alpha >= beta)
                break;
        }
        return best;
    }
    else
    {
        int best = std::numeric_limits<int>::max();
        for (const auto &move : moves)
        {
            board.makeMove(move);
            int val = eval(board, depth - 1, p, mp, gameOver, alpha, beta);
            board.unmakeMove(move);

            if (val < best)
            {
                best = val;
                if (depth == searchDepth)
                    mp[key] = move;
            }
            beta = std::min(beta, val);
            if (beta <= alpha)
                break;
        }
        return best;
    }
}
// --------------------------------------------------
// UCI loop

int main()
{
    Board board = Board::fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    std::string input;
    unordered_map<uint64_t, Move> mp;
    mp.reserve(100000);
    unordered_map<uint64_t, pair<bool, bool>> gameOver;
    gameOver.reserve(100000);
    while (true)
    {
        // Print current position
        // Check for game over
        auto [reason, result] = board.isGameOver();
        if (reason != GameResultReason::NONE)
        {
            if (result == GameResult::WIN)
            {
                std::cout << (board.sideToMove() == Color::WHITE ? "Black" : "White") << " wins!\n";
            }
            else if (result == GameResult::DRAW)
            {
                std::cout << "Draw!\n";
            }
            else
            {
                std::cout << "Game over!\n";
            }
            break;
        }

        // Human move (assuming White is human)
        if (board.sideToMove() == Color::WHITE)
        {
            std::cout << "Your move (in UCI format, e.g. e2e4): ";
            std::cin >> input;

            if (input == "quit")
                break;

            if (!uci::isUciMove(input))
            {
                std::cout << "Invalid format.\n";
                continue;
            }

            Move m;
            try
            {
                m = uci::uciToMove(board, input);
            }
            catch (...)
            {
                std::cout << "Could not parse move.\n";
                continue;
            }

            Movelist legal;
            movegen::legalmoves(legal, board);
            bool legalMove = false;
            for (auto &lm : legal)
            {
                if (lm == m)
                {
                    legalMove = true;
                    break;
                }
            }
            if (!legalMove)
            {
                std::cout << "Illegal move.\n";
                continue;
            }

            board.makeMove(m);
        }
        else
        {
            std::cout << "Engine thinking...\n";
            int val = eval(board, searchDepth, board.sideToMove() == Color::WHITE ? 1 : 0, mp, gameOver, -10000, 10000);
            Move best = mp[board.hash()];
            if (best == Move::NO_MOVE)
            {
                std::cout << "No legal moves.\n";
                break;
            }
            std::cout << "Engine plays: " << uci::moveToUci(best) << "\n";
            board.makeMove(best);
        }
    }

    return 0;
}
