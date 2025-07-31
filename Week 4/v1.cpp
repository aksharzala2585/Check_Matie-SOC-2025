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
#include <map>
using namespace chess;

using namespace std;
typedef std::array<int, 64> PST;
static const PST PST_PAWN = {
    0, 0, 0, 0, 0, 0, 0, 0,
    50, 50, 50, 50, 50, 50, 50, 50, // 2nd rank: encourage mobilization
    10, 10, 20, 30, 30, 20, 10, 10, // 3rd rank: slight center push bonus
    5, 5, 10, 25, 25, 10, 5, 5,     // 4th rank: strong if in centre
    0, 0, 0, 50, 50, 0, 0, 0,       // 5th rank
    5, -5, -10, 0, 0, -10, -5, 5,   // hanging pawns are bad
    5, 10, 10, -20, -20, 10, 10, 5, // discourage backward pawns
    0, 0, 0, 0, 0, 0, 0, 0};

static const PST PST_KNIGHT = {
    -40, -30, -20, -20, -20, -20, -30, -40,
    -30, -10,   0,  10,  10,   0, -10, -30,
    -20,   0,  20,  30,  30,  20,   0, -20,
    -20,  10,  30,  40,  40,  30,  10, -20,
    -20,   0,  20,  40,  40,  20,   0, -20,
    -20,  10,  10,  20,  20,  10,  10, -20,
    -30, -10,   0,   0,   0,   0, -10, -30,
    -40, -30, -20, -20, -20, -20, -30, -40
};

static const PST PST_BISHOP = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10, 5, 0, 0, 0, 0, 5, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    -10, 0, 10, 20, 20, 10, 0, -10,
    -10, 5, 5, 20, 20, 5, 5, -10,
    -10, 0, 10, 20, 20, 10, 0, -10,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -20, -10, -10, -10, -10, -10, -10, -20};

static const PST PST_ROOK = {
    0, 0, 5, 10, 10, 5, 0, 0,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    5, 10, 10, 10, 10, 10, 10, 5,
    0, 0, 0, 0, 0, 0, 0, 0};

static const PST PST_QUEEN = {
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0
};

static const PST PST_KING = {
    -30, -40, -40, -50, -50, -40, -40, -30, // opening: tuck back
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20, // 5th–6th rank: still safe
    -10, -20, -20, -20, -20, -20, -20, -10,
    20, 20, 0, 0, 0, 0, 20, 20, // endgame king: center
    20, 30, 10, 0, 0, 10, 30, 20};

// Evaluate piece-square heatmap contribution
map<Board, Movelist> moveCache;
template <typename Table>
int heatContribution(const Table &pst, Square sq, Color color)
{
    int idx = (color == Color::WHITE ? sq.index() : 63 - sq.index());
    return (color == Color::WHITE ? pst[idx] : -pst[idx]);
}
int evaluateMobility(const Board &board)
{
    Movelist movesSTM;
    movegen::legalmoves(movesSTM, board);
    int stmCount = movesSTM.size();

    int countWhite = 0, countBlack = 0;
    Board copy = board;
    if (board.sideToMove() == Color::WHITE)
    {
        countWhite = stmCount;
        copy.makeNullMove();
        Movelist movesOpp;
        movegen::legalmoves(movesOpp, copy);
        countBlack = movesOpp.size();
    }
    else
    {
        countBlack = stmCount;
        copy.makeNullMove();
        Movelist movesOpp;
        movegen::legalmoves(movesOpp, copy);
        countWhite = movesOpp.size();
    }
    return (countWhite - countBlack) * 10;
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
    return score;
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
pair<bool, bool> isOver(Board &board, unordered_map<uint64_t, pair<bool, bool>> &gameOver)
{
    uint64_t key = board.hash();
    if (gameOver.find(key) != gameOver.end())
        return gameOver[key];
    auto [reason, result] = board.isGameOver();
    if (reason != GameResultReason::NONE)
    {
        if (reason == GameResultReason::CHECKMATE)
        {
            gameOver[key] = {true, 1};
            return {true, 1};
        }
        else
        {
            gameOver[key] = {true, 0};
            return {true, 0};
        }
    }
    return {false, 0};
}

unordered_map<uint64_t, int> vals;
// Wrapper: full static evaluation
int evaluatePosition(Board &board, unordered_map<uint64_t, pair<bool, bool>> &gameOver, int p)
{
    uint64_t key = board.hash();
    if (isOver(board, gameOver).first)
    {
        if (isOver(board, gameOver).second)
        {
            return board.sideToMove() == Color::WHITE? -1e5 : +1e5;
        }
        else
        {
            return 0;
        }
    }
    if (vals.find(key) != vals.end())
    {
        return vals[key];
    }
    vals[key] = evaluateMaterial(board) + evaluateKingSafety(board) + evaluatePawnStructure(board) + evaluateSpaceControl(board) + evaluatePieceCoordination(board) + evaluateHeatmap(board) + (evaluateMobility(board) / 10);
    // vals[key] *= board.sideToMove() == (p == 1 ? Color::WHITE : Color::BLACK) ? 1 : -1;
    // cout << vals[key] << endl;
    // cout << board.getFen() << endl;
    return vals[key];
}
int maxDepth = 6;

unordered_map<uint64_t, pair<bool, bool>> gameOver;
unordered_map<uint64_t, Move> mp;
int eval(Board &board,
         int depth,
         int p,
         unordered_map<uint64_t, Move> &mp,
         unordered_map<uint64_t, pair<bool, bool>> &gameOver,
         int alpha,
         int beta)
{
    uint64_t key = board.hash();

    // Terminal‐position check
    auto over = isOver(board, gameOver);
    if (over.first)
    {
        // checkmate → ±INF, draw → 0
        if (over.second)
            return (board.sideToMove() == (p == 1 ? Color::WHITE : Color::BLACK))
                       ? -100000
                       : +100000;
        else
            return 0;
    }

    // Generate legal moves
    Movelist moves;
    movegen::legalmoves(moves, board);

    // Maximizing for the original side (p==1 → White to move)
    if (board.sideToMove() == (p == 1 ? Color::WHITE : Color::BLACK))
    {
        int value = alpha;
        for (const auto &move : moves)
        {
            board.makeMove(move);
            int child;
            if (depth == 1)
            {
                child = evaluatePosition(board, gameOver, p);
                if (p == 0)
                {
                    child = -child;
                }
            }
            else
                child = eval(board, depth - 1, p, mp, gameOver, value, beta);

            board.unmakeMove(move);

            if (child > value)
            {
                value = child;
                mp[key] = move;
            }
            if (value >= beta) // β‐cutoff
                break;
        }
        // cout << "Depth " << depth << endl;
        // cout << "Value: " << value << endl;
        // cout << "Best move: " << uci::moveToUci(mp[key]) << endl;
        return value;
    }
    // Minimizing for the opponent
    else
    {
        int value = beta;
        for (const auto &move : moves)
        {
            board.makeMove(move);

            int child;
            if (depth == 1)
            {
                child = evaluatePosition(board, gameOver, p);
                if (p == 0)
                {
                    child = -child;
                }
            }
            else
                child = eval(board, depth - 1, p, mp, gameOver, alpha, value);

            board.unmakeMove(move);

            if (child < value)
            {
                value = child;
                mp[key] = move;
            }
            if (value <= alpha) // α‐cutoff
                break;
        }
        // cout << "Depth " << depth << endl;
        // cout << "Value: " << value << endl;
        // cout << "Best move: " << uci::moveToUci(mp[key]) << endl;
        return value;
    }
}
// }
// int negamax(Board &board,
//             int depth,
//             int color, // +1 = White to move, -1 = Black to move
//             unordered_map<uint64_t, Move> &pv_table,
//             unordered_map<uint64_t, pair<bool, bool>> &gameOver,
//             int alpha,
//             int beta)
// {
//     uint64_t key = board.hash();
//     auto [overRes, wdl] = isOver(board, gameOver);
//     if (overRes || depth == 0)
//     {
//         int leaf = evaluatePosition(board, gameOver);
//         return color * leaf;
//     }

//     Movelist moves;
//     movegen::legalmoves(moves, board);
//     int best = -1000000;
//     for (auto &mv : moves)
//     {
//         board.makeMove(mv);
//         int score = -negamax(board,
//                              depth - 1,
//                              -color,
//                              pv_table,
//                              gameOver,
//                              -beta,
//                              -alpha);
//         board.unmakeMove(mv);

//         if (score > best)
//         {
//             best = score;
//             pv_table[key] = mv;
//         }
//         alpha = max(alpha, score);
//         if (alpha >= beta)
//             break;
//     }
//     return best;
// }
// int eval(Board &board, int depth, int p, unordered_map<uint64_t, Move> &mp, unordered_map<uint64_t, pair<bool, bool>> &gameOver)
// {
//     uint64_t key = board.hash();
//     Movelist moves;
//     movegen::legalmoves(moves, board);
//     auto it = isOver(board, gameOver);
//     if (it.first)
//     {
//         if (it.second)
//             return 1e5;
//         else
//             return 0;
//     }
//     if (board.sideToMove() == (p == 1 ? Color::WHITE : Color::BLACK))
//     {
//         int mx = -1e5;
//         for (const auto &move : moves)
//         {
//             board.makeMove(move);
//             int val = 1;
//             if (depth == 1)
//             {
//                 val = evaluatePosition(board, gameOver, p);
//             }
//             else
//             {
//                 val = eval(board, depth - 1, p, mp, gameOver);
//             }
//             mx = max(mx, val);
//             board.unmakeMove(move);
//             if (val == mx)
//             {
//                 mp[key] = move;
//             }
//         }
//         return mx;
//     }
//     else
//     {
//         int mn = 1e5;
//         for (const auto &move : moves)
//         {
//             board.makeMove(move);
//             int val = 0;
//             if (depth == 1)
//             {
//                 val = evaluatePosition(board, gameOver, p);
//             }
//             else
//             {
//                 val = eval(board, depth - 1, p, mp, gameOver);
//             }
//             mn = min(mn, val);
//             board.unmakeMove(move);
//             if (val == mn)
//             {
//                 mp[key] = move;
//             }
//         }
//         return mn;
//     }
// }

atomic<bool> stopSearch(false);
int threads = 1;
int hashSize = 128;
int moveOverhead = 0;
string syzygyPath = "./syzygy";
bool uciShowWDL = false;

int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Board board;
    string line;

    while (getline(cin, line))
    {
        istringstream iss(line);
        string token;
        iss >> token;

        if (token == "uci")
        {
            cout << "id name SimpleEngine" << endl;
            cout << "id author Akshar" << endl;
            cout << "option name Move Overhead type spin default 0 min 0 max 5000" << endl;
            cout << "option name Hash type spin default 128 min 1 max 1024" << endl;
            cout << "option name Threads type spin default 1 min 1 max 4" << endl;
            cout << "option name SyzygyPath type string default ./syzygy" << endl;
            cout << "option name UCI_ShowWDL type check default false" << endl;
            cout << "uciok" << endl;
        }
        else if (token == "isready")
        {
            cout << "readyok" << endl;
        }
        else if (token == "ucinewgame")
        {
            board = Board::fromFen(constants::STARTPOS);
            gameOver.clear();
            mp.clear();
            vals.clear();
        }
        else if (token == "setoption")
        {
            string rest = line.substr(10);
            size_t namePos = rest.find("name ");
            size_t valuePos = rest.find("value ");
            if (namePos != string::npos)
            {
                string name = rest.substr(namePos + 5, (valuePos != string::npos ? valuePos : rest.size()) - (namePos + 5));
                string value;
                if (valuePos != string::npos)
                {
                    value = rest.substr(valuePos + 6);
                }
                auto trim = [](string &s)
                {
                    size_t a = s.find_first_not_of(' ');
                    size_t b = s.find_last_not_of(' ');
                    if (a == string::npos)
                        s.clear();
                    else
                        s = s.substr(a, b - a + 1);
                };
                trim(name);
                trim(value);
                if (name == "Hash")
                    hashSize = stoi(value);
                else if (name == "Threads")
                    threads = stoi(value);
                else if (name == "Move Overhead")
                    moveOverhead = stoi(value);
                else if (name == "SyzygyPath")
                    syzygyPath = value;
                else if (name == "UCI_ShowWDL")
                    uciShowWDL = (value == "true" || value == "1");
            }
        }
        else if (token == "position")
        {
            string sub;
            iss >> sub;
            if (sub == "startpos")
            {
                board = Board::fromFen(constants::STARTPOS);
                gameOver.clear();
                mp.clear();
                vals.clear();
                if (iss >> sub && sub == "moves")
                {
                    while (iss >> sub)
                    {
                        Move m = uci::uciToMove(board, sub);
                        board.makeMove(m);
                    }
                }
            }
            else if (sub == "fen")
            {
                string fen;
                getline(iss, fen);
                board = Board::fromFen(fen);
            }
        }
        else if (token == "go")
        {
            int rootColor = (board.sideToMove() == Color::WHITE ? +1 : 0);
            Move bestMove = Move::NO_MOVE;
            int bestEval = -1000000;

            for (int depth = 1; depth <= 5; ++depth)
            {
                int score = eval(board,
                                 depth,
                                 rootColor,
                                 mp,
                                 gameOver,
                                 -100000,
                                 +100000);
                bestMove = mp[board.hash()];
                bestEval = score;
                cout<< depth<<endl;
                cout << bestEval << "\n";
                cout << uci::moveToUci(bestMove) << "\n";
                cout << endl
                     << endl;
                if (abs(bestEval) > 90000)
                    break;
            }

            Move chosen = bestMove;

            board.makeMove(chosen);
            // cout << "Evaluations" << endl;
            // //    vals[key] = evaluateMaterial(board) + evaluateKingSafety(board) + evaluatePawnStructure(board) + evaluateSpaceControl(board) + evaluatePieceCoordination(board) + evaluateHeatmap(board) + (evaluateMobility(board) / 2);
            // cout << "HEATMAP " << evaluateHeatmap(board) << endl;
            // cout << "King Safety " << evaluateKingSafety(board) << endl;
            // cout << "Mobility " << evaluateMobility(board) << endl;
            // cout << "Material " << evaluateMaterial(board) << endl;
            // cout << "Pawn Structure " << evaluatePawnStructure(board) << endl;
            // cout << "Space control" << evaluateSpaceControl(board) << endl;
            cout << "bestmove " << uci::moveToUci(chosen);
            if (uciShowWDL)
                cout << " info wdl " << "0 0 0"; // stub WDL counts
            cout << endl;
        }
        // else if (token == "go")
        // {
        //     Movelist rootMoves;
        //     movegen::legalmoves(rootMoves, board);
        //     int nRoots = rootMoves.size();
        //     Move rootBest = Move::NO_MOVE; // <-- local storage
        //     int rootScore = numeric_limits<int>::min();
        //     vector<int> scores(nRoots);

        //     // iterative deepening
        //     for (int depth = 1; depth <= maxDepth; ++depth)
        //     {
        //         for (int i = 0; i < nRoots; ++i)
        //         {
        //             Board copy = board;
        //             copy.makeMove(rootMoves[i]);
        //             scores[i] = -negamax(copy, depth - 1,
        //                                  /*alpha=*/-1000000,
        //                                  /*beta=*/+1000000);
        //         }

        //         // pick the best move *this* iteration
        //         int bestScore = numeric_limits<int>::min();
        //         Move bestMove = Move::NO_MOVE;
        //         for (int i = 0; i < nRoots; ++i)
        //         {
        //             if (scores[i] > bestScore)
        //             {
        //                 bestScore = scores[i];
        //                 bestMove = rootMoves[i];
        //             }
        //         }

        //         // stash locally — do NOT write it into tt at the root
        //         rootBest = bestMove;
        //         rootScore = bestScore;
        //         cerr << "Depth " << depth
        //              << ": best=" << uci::moveToUci(rootBest)
        //              << " score=" << rootScore << "\n";

        //         // if it's a mate, stop deeper searching
        //         if (abs(rootScore) >= 900000 - maxDepth)
        //             break;
        //     }

        //     // now play the locally‑remembered rootBest — guaranteed legal
        //     board.makeMove(rootBest);
        //     cout << "bestmove " << uci::moveToUci(rootBest) << "\n";
        //     cout.flush();
        // }

        else if (token == "stop")
        {
            stopSearch = true;
        }
        else if (token == "quit")
        {
            break;
        }
        else
        {
            cerr << "info string Unknown command: " << line << endl;
        }
    }
    return 0;
}