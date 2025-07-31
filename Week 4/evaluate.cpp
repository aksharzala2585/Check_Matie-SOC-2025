#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include "chess.hpp"

// Material values in centipawns
static const std::unordered_map<chess::PieceType::underlying, int> MATERIAL_VALUE = {
    {chess::PieceType::PAWN,   100},
    {chess::PieceType::KNIGHT, 320},
    {chess::PieceType::BISHOP, 330},
    {chess::PieceType::ROOK,   500},
    {chess::PieceType::QUEEN,  900},
    {chess::PieceType::KING,    0}  // King’s "value" is not used directly
};

// Penalties/bonuses in centipawns
static constexpr int ISOLATED_PAWN_PENALTY = 20;
static constexpr int DOUBLED_PAWN_PENALTY  = 10;
static constexpr int PASSED_PAWN_BONUS     = 30;
static constexpr int MOBILITY_FACTOR       = 10; // per legal move difference
static constexpr int KING_SAFETY_PENALTY   = 50; // per attacking piece near king
static constexpr int SPACE_CONTROL_FACTOR  = 1;  // per controlled square
static constexpr int COORDINATION_BONUS    = 10; // per protected piece

using namespace chess;

// Count material balance: positive means White ahead, negative means Black ahead.
int evaluateMaterial(const Board& board) {
    int eval = 0;
    for (auto pt_under : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                         PieceType::ROOK, PieceType::QUEEN}) {
        PieceType pt(pt_under);
        // count for White
        Bitboard wb = board.pieces(pt, Color::WHITE);
        int countW = wb.count();
        // count for Black
        Bitboard bb = board.pieces(pt, Color::BLACK);
        int countB = bb.count();
        int value = MATERIAL_VALUE.at(pt.internal());
        eval += (countW - countB) * value;
    }
    return eval;
}

// Mobility: number of legal moves for side to move and opponent
// We generate ALL legal moves (captures+quiet), count them, and compute difference.
int evaluateMobility(const Board& board) {
    Movelist movesW, movesB;
    // generate legal moves for White
    Board tmp = board;
    if (board.sideToMove() == Color::WHITE) {
        movegen::legalmoves(movesW, board);
        // For Black, we need to flip side: make a copy, switch side, generate
        Board bcopy = board;
        bcopy.setFen(board.getFen()); // ensure identical; sideToMove included
        bcopy = Board::fromFen(board.getFen()); // simpler: reconstruct
        // Actually easier: if sideToMove is White, we want Black’s mobility next:
        // Temporarily set side to move and generate:
        bcopy.setFen(board.getFen()); 
        // but FEN includes stm; to flip stm we can construct a new FEN:
        // simpler: make a copy and manually switch stm_ via library? There's no direct setter.
        // Instead, use Board copy and then make a null move? For simplicity, we generate mobility only for side to move:
        // We approximate opponent mobility by doing: make a null move: skip in this simple evaluator.
        // For demonstration, we only score side-to-move mobility minus opponent mobility roughly.
        // A full implementation would apply a null move or directly generate for ~stm_.
    } else {
        movegen::legalmoves(movesB, board);
    }
    // **Simplify**: We compute mobility difference as (#moves for side to move) - (#moves for opponent).
    // To get opponent moves, we create a copy, make a “pass” by flipping stm in FEN:
    // Extract FEN, flip 'w' <-> 'b', keep other fields unchanged.
    std::string fen = board.getFen();
    // Split FEN into parts
    auto parts = utils::splitString(fen, ' ');
    if (parts.size() >= 2) {
        std::string flipped_fen = parts[0] + " ";
        flipped_fen += (parts[1] == "w" ? "b" : "w");
        // Append castling, ep, halfmove, fullmove
        for (size_t i = 2; i < parts.size(); i++) {
            flipped_fen += " " + std::string(parts[i]);
        }
        Board oppBoard = Board::fromFen(flipped_fen);
        Movelist oppMoves;
        movegen::legalmoves(oppMoves, oppBoard);
        int sideMoves = (board.sideToMove() == Color::WHITE ? movesW.size() : movesB.size());
        int oppMovesCount = oppMoves.size();
        return (sideMoves - oppMovesCount) * MOBILITY_FACTOR;
    }
    return 0;
}

// King safety: count number of opponent attackers to the king’s vicinity (e.g., squares around king).
int evaluateKingSafety(const Board& board) {
    int eval = 0;
    Color stm = board.sideToMove();
    // Evaluate both kings: penalty if attacked squares near king
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Square kingSq = board.kingSq(color);
        // Collect squares around king: use attacks::king
        Bitboard around = attacks::king(kingSq);
        // Count opponent pieces attacking those squares
        Color opp = ~color;
        int threats = 0;
        Bitboard occ = board.occ();
        // For each square in 'around', if opp has an attack to it, increment
        while (around) {
            Square sq = around.pop();
            if (board.isAttacked(sq, opp)) {
                threats++;
            }
        }
        // Penalize more for side to move’s king if under threat; but also opponent’s king matters.
        if (color == Color::WHITE) {
            eval -= threats * KING_SAFETY_PENALTY;
        } else {
            eval += threats * KING_SAFETY_PENALTY;
        }
    }
    return eval;
}

// Pawn structure: penalties for isolated/doubled pawns, bonus for passed pawns
int evaluatePawnStructure(const Board& board) {
    int eval = 0;
    // For each side
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Bitboard pawns = board.pieces(PieceType::PAWN, color);
        // For file iteration: files 0..7
        for (int f = 0; f < 8; f++) {
            // Mask for this file
            Bitboard fileMask = Bitboard(File(f));
            Bitboard thisFilePawns = pawns & fileMask;
            int countOnFile = thisFilePawns.count();
            if (countOnFile > 1) {
                // doubled pawn penalty for each extra pawn beyond 1
                int penalty = (countOnFile - 1) * DOUBLED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            // Isolated: no pawns on adjacent files
            bool hasLeft = (f > 0) && bool(pawns & Bitboard(File(f-1)));
            bool hasRight = (f < 7) && bool(pawns & Bitboard(File(f+1)));
            if (countOnFile > 0 && !hasLeft && !hasRight) {
                // each pawn on this isolated file penalized
                int penalty = countOnFile * ISOLATED_PAWN_PENALTY;
                eval += (color == Color::WHITE ? -penalty : penalty);
            }
            // Passed pawn: no opposing pawns in same file or adjacent files ahead of it
            // For each pawn square:
            Bitboard tmp = thisFilePawns;
            while (tmp) {
                Square pSq = tmp.pop();
                bool isPassed = true;
                int rank = pSq.rank();
                // For White: look ranks > rank; for Black: ranks < rank
                for (int af = std::max(0, f-1); af <= std::min(7, f+1); af++) {
                    // Build mask of opposing pawns on that file ahead
                    Bitboard oppPawns = board.pieces(PieceType::PAWN, ~color) & Bitboard(File(af));
                    Bitboard maskAhead;
                    if (color == Color::WHITE) {
                        // squares with rank > current
                        for (int r = rank+1; r <= 7; r++) {
                            maskAhead |= Bitboard::fromSquare(Square(Rank(r), File(f))); // but file fixed
                        }
                        // Actually need per file: but we recompute below
                    } else {
                        for (int r = 0; r < rank; r++) {
                            maskAhead |= Bitboard::fromSquare(Square(Rank(r), File(f)));
                        }
                    }
                    // Simpler: iterate oppPawns and check rank relation:
                    Bitboard oppTmp = oppPawns;
                    while (oppTmp) {
                        Square osq = oppTmp.pop();
                        int orank = osq.rank();
                        if (color == Color::WHITE) {
                            if (orank > rank && std::abs(osq.file() - pSq.file()) <= 1) {
                                isPassed = false;
                                break;
                            }
                        } else {
                            if (orank < rank && std::abs(osq.file() - pSq.file()) <= 1) {
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

// Control of space: (# squares attacked by White) - (# squares attacked by Black)
int evaluateSpaceControl(const Board& board) {
    // For each side, gather attacked squares (excluding occupied by own pieces? We count attacked empty or enemy-occupied squares.)
    Bitboard occAll = board.occ();
    Bitboard attacksW = 0ull;
    Bitboard attacksB = 0ull;
    // White pieces
    Bitboard pw = board.occ() & board.us(Color::WHITE);
    // Actually iterate by piece type:
    for (auto pt : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                    PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
        Bitboard bb = board.pieces(pt, Color::WHITE);
        while (bb) {
            Square sq = bb.pop();
            Bitboard att;
            switch (pt.internal()) {
                case 0: // pawn
                    att = attacks::pawn(Color::WHITE, sq);
                    break;
                case 1: // knight
                    att = attacks::knight(sq);
                    break;
                case 2: // bishop
                    att = attacks::bishop(sq, occAll);
                    break;
                case 3: // rook
                    att = attacks::rook(sq, occAll);
                    break;
                case 4: // queen
                    att = attacks::queen(sq, occAll);
                    break;
                case 5: // king
                    att = attacks::king(sq);
                    break;
                default:
                    att = 0ull;
            }
            attacksW |= att;
        }
    }
    for (auto pt : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                    PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
        Bitboard bb = board.pieces(pt, Color::BLACK);
        while (bb) {
            Square sq = bb.pop();
            Bitboard att;
            switch (pt.internal()) {
                case 0:
                    att = attacks::pawn(Color::BLACK, sq);
                    break;
                case 1:
                    att = attacks::knight(sq);
                    break;
                case 2:
                    att = attacks::bishop(sq, occAll);
                    break;
                case 3:
                    att = attacks::rook(sq, occAll);
                    break;
                case 4:
                    att = attacks::queen(sq, occAll);
                    break;
                case 5:
                    att = attacks::king(sq);
                    break;
                default:
                    att = 0ull;
            }
            attacksB |= att;
        }
    }
    int ctrlW = attacksW.count();
    int ctrlB = attacksB.count();
    return (ctrlW - ctrlB) * SPACE_CONTROL_FACTOR;
}

// Piece coordination: count how many pieces are “protected” by another piece.
// For each piece, check if any friendly piece attacks its square. Sum bonuses.
int evaluatePieceCoordination(const Board& board) {
    int eval = 0;
    Bitboard occAll = board.occ();
    for (Color color : {Color::WHITE, Color::BLACK}) {
        Bitboard usOcc = board.us(color);
        Bitboard oppOcc = board.us(~color);
        // Precompute all attacks by friendly pieces:
        Bitboard friendlyAttacks = 0ull;
        // For each piece type except king? include king too.
        for (auto pt : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                        PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
            Bitboard bb = board.pieces(pt, color);
            while (bb) {
                Square sq = bb.pop();
                Bitboard att;
                switch (pt.internal()) {
                    case 0:
                        att = attacks::pawn(color, sq);
                        break;
                    case 1:
                        att = attacks::knight(sq);
                        break;
                    case 2:
                        att = attacks::bishop(sq, occAll);
                        break;
                    case 3:
                        att = attacks::rook(sq, occAll);
                        break;
                    case 4:
                        att = attacks::queen(sq, occAll);
                        break;
                    case 5:
                        att = attacks::king(sq);
                        break;
                    default:
                        att = 0ull;
                }
                friendlyAttacks |= att;
            }
        }
        // Now for each piece, if its square is in friendlyAttacks (excluding self-attacks?), count as protected.
        int protectedCount = 0;
        for (auto pt : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                        PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
            Bitboard bb = board.pieces(pt, color);
            while (bb) {
                Square sq = bb.pop();
                // Remove attacks from that piece itself: but since we included all pieces, a piece’s own attack might include its square? No: attacks on own square never include itself, so safe.
                if ((friendlyAttacks & Bitboard::fromSquare(sq)).any()) {
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

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string fen;
    if (!std::getline(std::cin, fen)) {
        std::cerr << "Failed to read FEN from input\n";
        return 1;
    }
    // Construct board from FEN
    Board board = Board::fromFen(fen); // uses setFenInternal internally :contentReference[oaicite:5]{index=5}

    // Evaluate
    int materialEval       = evaluateMaterial(board);
    int mobilityEval       = evaluateMobility(board);
    int kingSafetyEval     = evaluateKingSafety(board);
    int pawnStructEval     = evaluatePawnStructure(board);
    int spaceControlEval   = evaluateSpaceControl(board);
    int coordinationEval   = evaluatePieceCoordination(board);

    int totalEval = materialEval
                  + mobilityEval
                  + kingSafetyEval
                  + pawnStructEval
                  + spaceControlEval
                  + coordinationEval;

    // Output in centipawns
    std::cout << "Evaluation (centipawns): " << totalEval << "\n";
    // Also human-readable +/– pawn units
    double pawnUnits = totalEval / 100.0;
    std::cout << "Evaluation (pawns): " << (pawnUnits >= 0 ? "+" : "") << pawnUnits << "\n";

    // Detailed breakdown
    std::cout << " Breakdown:\n";
    std::cout << "  Material:       " << materialEval << "\n";
    std::cout << "  Mobility:       " << mobilityEval << "\n";
    std::cout << "  King safety:    " << kingSafetyEval << "\n";
    std::cout << "  Pawn structure: " << pawnStructEval << "\n";
    std::cout << "  Space control:  " << spaceControlEval << "\n";
    std::cout << "  Coordination:   " << coordinationEval << "\n";

    return 0;
}
