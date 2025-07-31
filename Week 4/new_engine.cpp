#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <limits>
#include "chess.hpp"
#include <thread>
#include <mutex>
#include <atomic>
using namespace chess;

using namespace std;

// Current search depth
typedef std::array<int, 64> PST;

// Piece-square tables (heatmaps) for White; Black uses mirrored indices
static const PST PST_PAWN = {
    0, 0, 0, 0, 0, 0, 0, 0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
    5, 5, 10, 25, 25, 10, 5, 5,
    0, 0, 0, 20, 20, 0, 0, 0,
    5, -5, -10, 0, 0, -10, -5, 5,
    5, 10, 10, -20, -20, 10, 10, 5,
    0, 0, 0, 0, 0, 0, 0, 0};
static const PST PST_KNIGHT = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20, 0, 0, 0, 0, -20, -40,
    -30, 0, 10, 15, 15, 10, 0, -30,
    -30, 5, 15, 20, 20, 15, 5, -30,
    -30, 0, 15, 20, 20, 15, 0, -30,
    -30, 5, 10, 15, 15, 10, 5, -30,
    -40, -20, 0, 5, 5, 0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50};
static const PST PST_BISHOP = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10, 5, 0, 0, 0, 0, 5, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    -10, 0, 10, 10, 10, 10, 0, -10,
    -10, 5, 5, 10, 10, 5, 5, -10,
    -10, 0, 5, 10, 10, 5, 0, -10,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -20, -10, -10, -10, -10, -10, -10, -20};
static const PST PST_ROOK = {
    0, 0, 0, 0, 0, 0, 0, 0,
    5, 10, 10, 10, 10, 10, 10, 5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    0, 0, 0, 5, 5, 0, 0, 0};
static const PST PST_QUEEN = {
    -20, -10, -10, -5, -5, -10, -10, -20,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -10, 0, 5, 5, 5, 5, 0, -10,
    -5, 0, 5, 5, 5, 5, 0, -5,
    0, 0, 5, 5, 5, 5, 0, -5,
    -10, 5, 5, 5, 5, 5, 0, -10,
    -10, 0, 5, 0, 0, 0, 0, -10,
    -20, -10, -10, -5, -5, -10, -10, -20};
static const PST PST_KING = {
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
    20, 20, 0, 0, 0, 0, 20, 20,
    20, 30, 10, 0, 0, 10, 30, 20};

// Evaluate piece-square heatmap contribution
template <typename Table>
int heatContribution(const Table &pst, Square sq, Color color)
{
    int idx = (color == Color::WHITE ? sq.index() : 63 - sq.index());
    return (color == Color::WHITE ? pst[idx] : -pst[idx]);
}

int evaluateHeatmap(const Board &board)
{
    int score = 0;
    for (Color color : {Color::WHITE, Color::BLACK})
    {
        Bitboard bb;
        bb = board.pieces(PieceType::PAWN, color);
        while (bb)
            score += heatContribution(PST_PAWN, bb.pop(), color);
        bb = board.pieces(PieceType::KNIGHT, color);
        while (bb)
            score += heatContribution(PST_KNIGHT, bb.pop(), color);
        bb = board.pieces(PieceType::BISHOP, color);
        while (bb)
            score += heatContribution(PST_BISHOP, bb.pop(), color);
        bb = board.pieces(PieceType::ROOK, color);
        while (bb)
            score += heatContribution(PST_ROOK, bb.pop(), color);
        bb = board.pieces(PieceType::QUEEN, color);
        while (bb)
            score += heatContribution(PST_QUEEN, bb.pop(), color);
        bb = board.pieces(PieceType::KING, color);
        while (bb)
            score += heatContribution(PST_KING, bb.pop(), color);
    }
    return score / 4;
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
int maxDepth = 5;  // maximum search depth

// Transposition table: maps position hash to best move
static unordered_map<uint64_t, Move> tt;
static mutex ttMutex;  // protect tt

// MVV-LVA: Most Valuable Victim, Least Valuable Aggressor scoring
int mvvLvaScore(const Move &m, const Board &board) {
    Piece fromPiece = board.at(m.from());
    Piece toPiece   = board.at(m.to());
    int victimVal   = (toPiece == Piece::NONE ? 0 : MATERIAL_VALUE.at(toPiece.type().internal()));
    int attackerVal = MATERIAL_VALUE.at(fromPiece.type().internal());
    return victimVal * 100 - attackerVal;
}

// Core Negamax with alpha-beta and move ordering
int negamax(Board &board, int depth, int alpha, int beta) {
    uint64_t key = board.hash();
    if (depth == 0) {
        int eval = evaluatePosition(board);
        return (board.sideToMove() == Color::WHITE ? eval : -eval);
    }

    Movelist moves;
    movegen::legalmoves(moves, board);
    // TT move first
    {
        lock_guard<mutex> lock(ttMutex);
        if (tt.count(key)) {
            Move pv = tt[key];
            for (size_t i = 0; i < moves.size(); ++i) {
                if (moves[i] == pv) { swap(moves[0], moves[i]); break; }
            }
        }
    }
    // MVV-LVA sorting
    sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b) {
        return mvvLvaScore(a, board) > mvvLvaScore(b, board);
    });

    int bestScore = numeric_limits<int>::min();
    Move bestMove = Move::NO_MOVE;

    for (const auto &mv : moves) {
        board.makeMove(mv);
        int score = -negamax(board, depth - 1, -beta, -alpha);
        board.unmakeMove(mv);

        if (score > bestScore) {
            bestScore = score;
            bestMove = mv;
        }
        alpha = max(alpha, score);
        if (alpha >= beta) break;
    }

    if (bestMove != Move::NO_MOVE) {
        lock_guard<mutex> lock(ttMutex);
        tt[key] = bestMove;
    }
    return bestScore;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Board board = Board::fromFen(constants::STARTPOS);
    string input;

    while (true) {
        cout << board << "\n";
        auto [reason, result] = board.isGameOver();
        if (reason != GameResultReason::NONE) break;

        if (board.sideToMove() == Color::WHITE) {
            cout << "Your move (UCI): ";
            cin >> input;
            if (input == "quit") break;
            if (!uci::isUciMove(input)) { cout << "Invalid UCI format.\n"; continue; }
            Move m;
            try { m = uci::uciToMove(board, input); }
            catch (...) { cout << "Could not parse move.\n"; continue; }
            board.makeMove(m);
        } else {
            cerr << "Engine thinking...\n";
            Movelist rootMoves;
            movegen::legalmoves(rootMoves, board);
            int nRoots = rootMoves.size();
            vector<int> scores(nRoots, numeric_limits<int>::min());

            for (int depth = 1; depth <= maxDepth; ++depth) {
                for (int i = 0; i < nRoots; ++i) {
                    Board copy = board;
                    Move mv = rootMoves[i];
                    copy.makeMove(mv);
                    scores[i] = -negamax(copy, depth - 1, -100000, 100000);
                }

                // Select best
                int bestScore = numeric_limits<int>::min();
                Move bestMove = Move::NO_MOVE;
                for (int i = 0; i < nRoots; ++i) {
                    if (scores[i] > bestScore) {
                        bestScore = scores[i];
                        bestMove = rootMoves[i];
                    }
                }
                {
                    lock_guard<mutex> lock(ttMutex);
                    tt[board.hash()] = bestMove;
                }
                cerr << "Depth " << depth
                     << ": best=" << uci::moveToUci(bestMove)
                     << " score=" << bestScore << "\n";
            }

            Move chosen;
            {
                lock_guard<mutex> lock(ttMutex);
                chosen = tt[board.hash()];
            }
            cout << "Engine plays: " << uci::moveToUci(chosen) << "\n";
            board.makeMove(chosen);
        }
    }

    auto [fr, res] = board.isGameOver();
    if (res == GameResult::WIN)
        cout << ((board.sideToMove() == Color::WHITE) ? "Black" : "White") << " wins!\n";
    else if (res == GameResult::DRAW)
        cout << "Draw!\n";
    else
        cout << "Game over.\n";
    return 0;
}