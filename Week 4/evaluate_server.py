import sys
import numpy as np
import tensorflow as tf
import chess
from tensorflow.keras.models import load_model

model = load_model("chess_eval2.keras")

def gen_x_data(fen):
    board = chess.Board(fen)
    x = np.zeros((8, 8, 12), dtype=np.int8)
    piece_map = {
        chess.PAWN: 0, chess.KNIGHT: 1, chess.BISHOP: 2,
        chess.ROOK: 3, chess.QUEEN: 4, chess.KING: 5
    }
    for square in chess.SQUARES:
        piece = board.piece_at(square)
        if piece:
            plane = piece_map[piece.piece_type] + (0 if piece.color == chess.WHITE else 6)
            x[square // 8, square % 8, plane] = 1

    castling = np.zeros((8, 8, 4), dtype=np.int8)
    if board.has_kingside_castling_rights(chess.WHITE): castling[:, :, 0] = 1
    if board.has_queenside_castling_rights(chess.WHITE): castling[:, :, 1] = 1
    if board.has_kingside_castling_rights(chess.BLACK): castling[:, :, 2] = 1
    if board.has_queenside_castling_rights(chess.BLACK): castling[:, :, 3] = 1

    en_passant = np.zeros((8, 8, 1), dtype=np.int8)
    if board.ep_square is not None:
        row, col = divmod(board.ep_square, 8)
        en_passant[row, col, 0] = 1

    turn = np.ones((8, 8, 1), dtype=np.int8) * int(board.turn)
    halfmove = np.ones((8, 8, 1), dtype=np.float32) * min(board.halfmove_clock / 100.0, 1.0)
    fullmove = np.ones((8, 8, 1), dtype=np.float32) * min(board.fullmove_number / 100.0, 1.0)

    tensor = np.concatenate([x, castling, en_passant, turn, halfmove, fullmove], axis=-1)
    return tensor.astype(np.float32)

def main():
    try:
        n = int(sys.stdin.readline())
        fens = [sys.stdin.readline().strip() for _ in range(n)]
        tensors = np.stack([gen_x_data(fen) for fen in fens])
        scores = model.predict(tensors, verbose=0)
        for score in scores:
            print(score[0])
    except Exception as e:
        print("ERR", e, file=sys.stderr)

if __name__ == "__main__":
    main()
