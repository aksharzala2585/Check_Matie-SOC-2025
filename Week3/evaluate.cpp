#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include "chess.hpp"

// Material values in centipawns
static const std::unordered_map<chess::PieceType::underlying, int> MATERIAL_VALUE = {
    {chess::PieceType::PAWN, 100},
    {chess::PieceType::KNIGHT, 320},
    {chess::PieceType::BISHOP, 330},
    {chess::PieceType::ROOK, 500},
    {chess::PieceType::QUEEN, 900},
    {chess::PieceType::KING, 0} // King’s "value" is not used directly
};

// Penalties/bonuses in centipawns
static constexpr int ISOLATED_PAWN_PENALTY = 20;
static constexpr int DOUBLED_PAWN_PENALTY = 10;
static constexpr int PASSED_PAWN_BONUS = 30;
static constexpr int MOBILITY_FACTOR = 10;     // per move difference
static constexpr int KING_SAFETY_PENALTY = 50; // per threat near king
static constexpr int SPACE_CONTROL_FACTOR = 1; // per controlled square
static constexpr int COORDINATION_BONUS = 10;  // per protected piece

using namespace chess;

// Material evaluation: positive = White ahead, negative = Black ahead.
int evaluateMaterial(const Board &board)
{
    int eval = 0;
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN})
    {
        PieceType pt(pt_under);
        Bitboard wb = board.pieces(pt, Color::WHITE);
        Bitboard bb = board.pieces(pt, Color::BLACK);
        int countW = wb.count();
        int countB = bb.count();
        int value = MATERIAL_VALUE.at(pt.internal());
        eval += (countW - countB) * value;
    }
    return eval;
}

// Mobility: (#legal moves for side to move - #legal moves for opponent) * factor
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


// King safety: count threats near each king; penalize White threats positively negative for White, and vice versa
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
            {
                threats++;
            }
        }
        if (color == Color::WHITE)
        {
            eval -= threats * KING_SAFETY_PENALTY;
        }
        else
        {
            eval += threats * KING_SAFETY_PENALTY;
        }
    }
    return eval;
}

// Pawn structure: doubled, isolated penalties; passed pawn bonuses
int evaluatePawnStructure(const Board &board)
{
    int eval = 0;
    for (Color color : {Color::WHITE, Color::BLACK})
    {
        Bitboard pawns = board.pieces(PieceType(PieceType::PAWN), color);
        for (int f = 0; f < 8; f++)
        {
            Bitboard fileMask = Bitboard(File(f));
            Bitboard thisFilePawns = pawns & fileMask;
            int countOnFile = thisFilePawns.count();
            // Doubled
            if (countOnFile > 1)
            {
                int penalty = (countOnFile - 1) * DOUBLED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            // Isolated
            bool hasLeft = (f > 0) && ((pawns & Bitboard(File(f - 1))).count() > 0);
            bool hasRight = (f < 7) && ((pawns & Bitboard(File(f + 1))).count() > 0);
            if (countOnFile > 0 && !hasLeft && !hasRight)
            {
                int penalty = countOnFile * ISOLATED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            // Passed pawn: for each pawn on this file
            Bitboard tmp = thisFilePawns;
            while (tmp)
            {
                Square pSq = tmp.pop();
                int rank = pSq.rank();
                bool isPassed = true;
                // Check opponent pawns in same or adjacent files ahead
                for (int af = std::max(0, f - 1); af <= std::min(7, f + 1); af++)
                {
                    Bitboard oppPawns = board.pieces(PieceType(PieceType::PAWN), ~color) & Bitboard(File(af));
                    Bitboard oppTmp = oppPawns;
                    while (oppTmp)
                    {
                        Square osq = oppTmp.pop();
                        int orank = osq.rank();
                        if (color == Color::WHITE)
                        {
                            if (orank > rank)
                            {
                                isPassed = false;
                                break;
                            }
                        }
                        else
                        {
                            if (orank < rank)
                            {
                                isPassed = false;
                                break;
                            }
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

// Space control: (# attacked squares by White - # attacked squares by Black) * factor
int evaluateSpaceControl(const Board &board)
{
    Bitboard occAll = board.occ();
    Bitboard attacksW = 0ull;
    Bitboard attacksB = 0ull;
    // White
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
    {
        PieceType pt(pt_under);
        Bitboard bb = board.pieces(pt, Color::WHITE);
        while (bb)
        {
            Square sq = bb.pop();
            Bitboard att = 0ull;
            switch (pt.internal())
            {
            case chess::PieceType::PAWN:
                att = attacks::pawn(Color::WHITE, sq);
                break;
            case chess::PieceType::KNIGHT:
                att = attacks::knight(sq);
                break;
            case chess::PieceType::BISHOP:
                att = attacks::bishop(sq, occAll);
                break;
            case chess::PieceType::ROOK:
                att = attacks::rook(sq, occAll);
                break;
            case chess::PieceType::QUEEN:
                att = attacks::queen(sq, occAll);
                break;
            case chess::PieceType::KING:
                att = attacks::king(sq);
                break;
            default:
                break;
            }
            attacksW |= att;
        }
    }
    // Black
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                          PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
    {
        PieceType pt(pt_under);
        Bitboard bb = board.pieces(pt, Color::BLACK);
        while (bb)
        {
            Square sq = bb.pop();
            Bitboard att = 0ull;
            switch (pt.internal())
            {
            case chess::PieceType::PAWN:
                att = attacks::pawn(Color::BLACK, sq);
                break;
            case chess::PieceType::KNIGHT:
                att = attacks::knight(sq);
                break;
            case chess::PieceType::BISHOP:
                att = attacks::bishop(sq, occAll);
                break;
            case chess::PieceType::ROOK:
                att = attacks::rook(sq, occAll);
                break;
            case chess::PieceType::QUEEN:
                att = attacks::queen(sq, occAll);
                break;
            case chess::PieceType::KING:
                att = attacks::king(sq);
                break;
            default:
                break;
            }
            attacksB |= att;
        }
    }
    int ctrlW = attacksW.count();
    int ctrlB = attacksB.count();
    return (ctrlW - ctrlB) * SPACE_CONTROL_FACTOR;
}

// Piece coordination: count protected pieces
int evaluatePieceCoordination(const Board &board)
{
    int eval = 0;
    Bitboard occAll = board.occ();
    for (Color color : {Color::WHITE, Color::BLACK})
    {
        // Build friendly attacks
        Bitboard friendlyAttacks = 0ull;
        for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                              PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
        {
            PieceType pt(pt_under);
            Bitboard bb = board.pieces(pt, color);
            while (bb)
            {
                Square sq = bb.pop();
                Bitboard att = 0ull;
                switch (pt.internal())
                {
                case chess::PieceType::PAWN:
                    att = attacks::pawn(color, sq);
                    break;
                case chess::PieceType::KNIGHT:
                    att = attacks::knight(sq);
                    break;
                case chess::PieceType::BISHOP:
                    att = attacks::bishop(sq, occAll);
                    break;
                case chess::PieceType::ROOK:
                    att = attacks::rook(sq, occAll);
                    break;
                case chess::PieceType::QUEEN:
                    att = attacks::queen(sq, occAll);
                    break;
                case chess::PieceType::KING:
                    att = attacks::king(sq);
                    break;
                default:
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
                Bitboard mask = Bitboard::fromSquare(sq);
                if ((friendlyAttacks & mask).count() > 0)
                {
                    protectedCount++;
                }
            }
        }
        if (color == Color::WHITE)
        {
            eval += protectedCount * COORDINATION_BONUS;
        }
        else
        {
            eval -= protectedCount * COORDINATION_BONUS;
        }
    }
    return eval;
}

int main()
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string fen;
    if (!std::getline(std::cin, fen))
    {
        std::cerr << "Failed to read FEN from input\n";
        return 1;
    }
    Board board;
    try
    {
        board = Board::fromFen(fen);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error parsing FEN: " << e.what() << "\n";
        return 1;
    }

    int materialEval = evaluateMaterial(board);
    int mobilityEval = evaluateMobility(board);
    int kingSafetyEval = evaluateKingSafety(board);
    int pawnStructEval = evaluatePawnStructure(board);
    int spaceControlEval = evaluateSpaceControl(board);
    int coordinationEval = evaluatePieceCoordination(board);

    int totalEval = materialEval + mobilityEval + kingSafetyEval + pawnStructEval + spaceControlEval + coordinationEval;

    std::cout << "Evaluation (centipawns): " << totalEval << "\n";
    double pawnUnits = totalEval / 100.0;
    std::cout << "Evaluation (pawns): " << (pawnUnits >= 0 ? "+" : "") << pawnUnits << "\n";

    std::cout << " Breakdown:\n";
    std::cout << "  Material:       " << materialEval << "\n";
    std::cout << "  Mobility:       " << mobilityEval << "\n";
    std::cout << "  King safety:    " << kingSafetyEval << "\n";
    std::cout << "  Pawn structure: " << pawnStructEval << "\n";
    std::cout << "  Space control:  " << spaceControlEval << "\n";
    std::cout << "  Coordination:   " << coordinationEval << "\n";

    return 0;
}
