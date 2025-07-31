import sys
import time
import threading
import chess
import chess.polyglot
import numpy as np
import tensorflow as tf
from typing import Optional, List, Tuple
import os

# --- Constants ---
MATE_SCORE = 10000
MATE_THRESHOLD = MATE_SCORE - 100
# Scale factor to convert the model's output (e.g., in range [-10, 10]) to centipawns.
MODEL_SCORE_SCALE = 400

# Transposition Table Flags
TT_EXACT = 0
TT_ALPHA = 1
TT_BETA = 2

# --- Piece-Square Tables (PSTs) ---
# These tables assign scores to pieces based on their position.
# Scores are from White's perspective. Tables are flipped for Black.

piece_values = {
    chess.PAWN: 100, chess.KNIGHT: 320, chess.BISHOP: 330,
    chess.ROOK: 500, chess.QUEEN: 900, chess.KING: 20000
}

# Opening/Middlegame PSTs
pst_pawn_mg = [
    0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
    5,  5, 10, 25, 25, 10,  5,  5,
    0,  0,  0, 20, 20,  0,  0,  0,
    5, -5,-10,  0,  0,-10, -5,  5,
    5, 10, 10,-20,-20, 10, 10,  5,
    0,  0,  0,  0,  0,  0,  0,  0
]
pst_knight_mg = [
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
]
pst_bishop_mg = [
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
]
pst_rook_mg = [
    0,  0,  0,  0,  0,  0,  0,  0,
    5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    0,  0,  0,  5,  5,  0,  0,  0
]
pst_queen_mg = [
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
    0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
]
pst_king_mg = [
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20
]

# Endgame PSTs
pst_pawn_eg = [
    0,  0,  0,  0,  0,  0,  0,  0,
    80, 80, 80, 80, 80, 80, 80, 80,
    50, 50, 50, 50, 50, 50, 50, 50,
    30, 30, 30, 30, 30, 30, 30, 30,
    20, 20, 20, 20, 20, 20, 20, 20,
    10, 10, 10, 10, 10, 10, 10, 10,
    5,  5,  5,  5,  5,  5,  5,  5,
    0,  0,  0,  0,  0,  0,  0,  0
]
pst_king_eg = [
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50
]

# Map piece types to their respective PSTs
pst_mg = {
    chess.PAWN: pst_pawn_mg, chess.KNIGHT: pst_knight_mg, chess.BISHOP: pst_bishop_mg,
    chess.ROOK: pst_rook_mg, chess.QUEEN: pst_queen_mg, chess.KING: pst_king_mg
}
pst_eg = {
    chess.PAWN: pst_pawn_eg, chess.KNIGHT: pst_knight_mg, chess.BISHOP: pst_bishop_mg,
    chess.ROOK: pst_rook_mg, chess.QUEEN: pst_queen_mg, chess.KING: pst_king_eg
}

# --- Keras Model Input Generation ---
def gen_x_data(board: chess.Board) -> np.ndarray:
    x = np.zeros((8, 8, 12), dtype=np.int8)
    piece_map = {
        chess.PAWN: 0, chess.KNIGHT: 1, chess.BISHOP: 2,
        chess.ROOK: 3, chess.QUEEN: 4, chess.KING: 5
    }
    for square in chess.SQUARES:
        piece = board.piece_at(square)
        if piece:
            plane = piece_map[piece.piece_type] + (0 if piece.color == chess.WHITE else 6)
            rank, file = divmod(square, 8)
            if board.turn == chess.BLACK:
                rank = 7 - rank
            x[rank, file, plane] = 1

    castling = np.zeros((8, 8, 4), dtype=np.int8)
    if board.has_kingside_castling_rights(chess.WHITE): castling[:, :, 0] = 1
    if board.has_queenside_castling_rights(chess.WHITE): castling[:, :, 1] = 1
    if board.has_kingside_castling_rights(chess.BLACK): castling[:, :, 2] = 1
    if board.has_queenside_castling_rights(chess.BLACK): castling[:, :, 3] = 1

    en_passant = np.zeros((8, 8, 1), dtype=np.int8)
    if board.ep_square is not None:
        rank, col = divmod(board.ep_square, 8)
        if board.turn == chess.BLACK:
            rank = 7 - rank
        en_passant[rank, col, 0] = 1

    turn = np.ones((8, 8, 1), dtype=np.int8)
    halfmove = np.ones((8, 8, 1), dtype=np.float32) * min(board.halfmove_clock / 100.0, 1.0)
    fullmove = np.ones((8, 8, 1), dtype=np.float32) * min(board.fullmove_number / 100.0, 1.0)

    return np.concatenate([x, castling, en_passant, turn, halfmove, fullmove], axis=-1).astype(np.float32)

class Engine:
    def __init__(self, model_path: str, book_path: str):
        self.model_path = model_path
        self.book_path = book_path
        self.model: Optional[tf.keras.Model] = None
        self.opening_book: Optional[chess.polyglot.MemoryMappedReader] = None
        self.board = chess.Board()
        
        self.transposition_table = {}
        self.nodes_searched = 0
        self.stop_search = False
        self.search_thread: Optional[threading.Thread] = None

        self.start_time = 0
        self.time_limit = float('inf')
        
        self.options = {
            "Move Overhead": 10, "Threads": 1, "Hash": 16,
            "SyzygyPath": "<empty>", "UCI_ShowWDL": False
        }

    def _log(self, message: str):
        print(f"info string {message}", file=sys.stderr, flush=True)

    def _load_model(self):
        if self.model is None:
            try:
                self._log(f"Loading Keras model from {self.model_path}")
                self.model = tf.keras.models.load_model(self.model_path, compile=False)
                dummy_input = np.expand_dims(gen_x_data(chess.Board()), axis=0)
                self.model.predict(dummy_input, verbose=0)
                self._log("Model loaded successfully.")
            except Exception as e:
                self._log(f"FATAL: Error loading model: {e}")
                sys.exit(1)

    def _load_opening_book(self):
        """Loads the Polyglot opening book if available."""
        if self.opening_book is None and os.path.exists(self.book_path):
            try:
                self._log(f"Loading opening book from {self.book_path}")
                self.opening_book = chess.polyglot.open_reader(self.book_path)
                self._log("Opening book loaded successfully.")
            except Exception as e:
                self._log(f"Warning: Could not load opening book: {e}")
                self.opening_book = None
        elif not os.path.exists(self.book_path):
            self._log("Info: No opening book found. Continuing without one.")


    def _get_game_phase(self) -> float:
        """Determines the game phase (0=opening, 1=endgame) based on material."""
        total_material = 2 * (4 * piece_values[chess.KNIGHT] + 4 * piece_values[chess.BISHOP] + 4 * piece_values[chess.ROOK] + 2 * piece_values[chess.QUEEN])
        current_material = 0
        for piece_type in [chess.KNIGHT, chess.BISHOP, chess.ROOK, chess.QUEEN]:
            current_material += len(self.board.pieces(piece_type, chess.WHITE)) * piece_values[piece_type]
            current_material += len(self.board.pieces(piece_type, chess.BLACK)) * piece_values[piece_type]
        
        phase = 1 - (current_material / total_material)
        return max(0, min(1, phase)) # Clamp between 0 and 1

    def _static_evaluate(self) -> int:
        """
        A more sophisticated handcrafted static evaluation function, inspired by
        common chess engine principles.
        Returns the score in centipawns from White's perspective.
        """
        if self.board.is_checkmate():
            return -MATE_SCORE if self.board.turn == chess.WHITE else MATE_SCORE
        if self.board.is_game_over():
            return 0

        score = 0
        game_phase = self._get_game_phase()

        # 1. Material and Piece-Square Tables
        for square in chess.SQUARES:
            piece = self.board.piece_at(square)
            if not piece:
                continue

            piece_score = piece_values[piece.piece_type]
            
            pst_mg_score = pst_mg[piece.piece_type][square if piece.color == chess.WHITE else (63 - square)]
            pst_eg_score = pst_eg[piece.piece_type][square if piece.color == chess.WHITE else (63 - square)]
            pst_score = (1 - game_phase) * pst_mg_score + game_phase * pst_eg_score

            if piece.color == chess.WHITE:
                score += piece_score + pst_score
            else:
                score -= (piece_score + pst_score)

        # 2. Bishop Pair Bonus
        if len(self.board.pieces(chess.BISHOP, chess.WHITE)) >= 2:
            score += 50
        if len(self.board.pieces(chess.BISHOP, chess.BLACK)) >= 2:
            score -= 50

        # 3. Pawn Structure, Rooks, and King Safety (Evaluated per side)
        for color in [chess.WHITE, chess.BLACK]:
            sign = 1 if color == chess.WHITE else -1
            
            my_pawns = self.board.pieces(chess.PAWN, color)
            opp_pawns = self.board.pieces(chess.PAWN, not color)
            
            # Doubled and Isolated Pawns
            for file_index in range(8):
                pawns_on_file = len(my_pawns.intersection(chess.BB_FILES[file_index]))
                if pawns_on_file > 1:
                    score -= 20 * (pawns_on_file - 1) * sign
            
            for pawn_sq in my_pawns:
                file_index = chess.square_file(pawn_sq)
                adjacent_files_mask = 0
                if file_index > 0: adjacent_files_mask |= chess.BB_FILES[file_index - 1]
                if file_index < 7: adjacent_files_mask |= chess.BB_FILES[file_index + 1]
                if not (my_pawns & adjacent_files_mask):
                    score -= 15 * sign

            # Passed Pawns Bonus
            passed_pawn_bonus = [0, 10, 20, 30, 50, 75, 100, 0]
            for pawn_sq in my_pawns:
                rank = chess.square_rank(pawn_sq)
                file = chess.square_file(pawn_sq)
                
                path_mask = chess.BB_FILES[file]
                if file > 0: path_mask |= chess.BB_FILES[file - 1]
                if file < 7: path_mask |= chess.BB_FILES[file + 1]

                in_front_mask = 0
                if color == chess.WHITE:
                    for r in range(rank + 1, 8): in_front_mask |= chess.BB_RANKS[r]
                else:
                    for r in range(rank - 1, -1, -1): in_front_mask |= chess.BB_RANKS[r]

                if not (opp_pawns & path_mask & in_front_mask):
                    bonus_rank = rank if color == chess.WHITE else 7 - rank
                    score += passed_pawn_bonus[bonus_rank] * sign

            # Rook Bonuses
            for rook_sq in self.board.pieces(chess.ROOK, color):
                file = chess.square_file(rook_sq)
                file_mask = chess.BB_FILES[file]
                
                is_semi_open = not (my_pawns & file_mask)
                is_open = is_semi_open and not (opp_pawns & file_mask)

                if is_open:
                    score += 25 * sign
                elif is_semi_open:
                    score += 15 * sign

            # King Safety
            king_sq = self.board.king(color)
            if king_sq is not None:
                # Penalty for attacks near the king
                king_attack_zone = chess.BB_KING_ATTACKS[king_sq]
                for piece_type in [chess.KNIGHT, chess.BISHOP, chess.ROOK, chess.QUEEN]:
                    attackers = self.board.attackers(not color, king_sq) & self.board.pieces(piece_type, not color)
                    if attackers:
                        penalty = [0, 0, 20, 30, 50, 80, 0][piece_type]
                        score -= penalty * sign
        
        # 4. Mobility
        temp_board = self.board.copy(stack=False)
        
        temp_board.turn = chess.WHITE
        score += 0.5 * temp_board.legal_moves.count() # Increased weight
        
        temp_board.turn = chess.BLACK
        score -= 0.5 * temp_board.legal_moves.count() # Increased weight
        return int(score)


    def _nn_evaluate(self) -> int:
        """Evaluates using only the neural network."""
        model_input = np.expand_dims(gen_x_data(self.board), axis=0)
        score = self.model.predict(model_input, verbose=0)[0][0]
        return int(score * MODEL_SCORE_SCALE)

    def _evaluate(self) -> int:
        """
        Hybrid evaluation: averages the static and NN evaluations.
        Returns the score from the perspective of the side to move.
        """
        if self.board.is_checkmate():
            return -MATE_SCORE
        if self.board.is_game_over():
            return 0
        
        static_score_white_pov = self._static_evaluate()
        static_score = static_score_white_pov if self.board.turn == chess.WHITE else -static_score_white_pov
        nn_score = self._nn_evaluate()
        # Give more weight to neural network evaluation (67% NN, 33% static)
        final_score = int(0.33 * static_score + 0.67 * nn_score)
        
        return final_score

    def _get_move_score(self, move: chess.Move) -> int:
        if move.promotion:
            return 10000 + piece_values.get(move.promotion, 0)
        
        if self.board.is_capture(move):
            victim = self.board.piece_at(move.to_square)
            victim_value = piece_values[chess.PAWN] if victim is None else piece_values[victim.piece_type]
            aggressor = self.board.piece_at(move.from_square)
            aggressor_value = piece_values[aggressor.piece_type]
            return victim_value * 10 - aggressor_value
        
        return 0

    def _sort_moves(self, moves: chess.LegalMoveGenerator) -> list[chess.Move]:
        return sorted(moves, key=self._get_move_score, reverse=True)

    def _quiescence_search(self, alpha: int, beta: int) -> int:
        self.nodes_searched += 1
        
        stand_pat = self._evaluate()
        if stand_pat >= beta:
            return beta
        alpha = max(alpha, stand_pat)

        moves = self._sort_moves(self.board.legal_moves)
        for move in moves:
            if self.board.is_capture(move) or move.promotion:
                self.board.push(move)
                score = -self._quiescence_search(-beta, -alpha)
                self.board.pop()

                if score >= beta:
                    return beta
                alpha = max(alpha, score)
        
        return alpha

    def _search(self, depth: int, ply: int, alpha: int, beta: int) -> Tuple[int, List[chess.Move]]:
        if self.stop_search or (time.time() - self.start_time) > self.time_limit:
            self.stop_search = True
            return 0, []

        if self.board.is_checkmate():
            return -(MATE_SCORE - ply), []
        if self.board.is_game_over():
            return 0, []

        alpha_orig = alpha
        best_pv = []
        
        zobrist_key = chess.polyglot.zobrist_hash(self.board)
        tt_entry = self.transposition_table.get(zobrist_key)
        if tt_entry and tt_entry['depth'] >= depth:
            tt_score = tt_entry['score']
            tt_move = tt_entry.get('move')
            
            if abs(tt_score) > MATE_THRESHOLD:
                tt_score = (tt_score/abs(tt_score)) * (abs(tt_score) - ply)

            if tt_entry['flag'] == TT_EXACT:
                return tt_score, [tt_move] if tt_move else []
            elif tt_entry['flag'] == TT_ALPHA:
                alpha = max(alpha, tt_score)
            elif tt_entry['flag'] == TT_BETA:
                beta = min(beta, tt_score)
            
            if alpha >= beta:
                return tt_score, [tt_move] if tt_move else []

        if depth == 0:
            return self._quiescence_search(alpha, beta), []

        self.nodes_searched += 1
        
        R = 3
        if depth >= R and ply > 0 and not self.board.is_check():
            self.board.push(chess.Move.null())
            null_score, _ = self._search(depth - 1 - R, ply + 1, -beta, -beta + 1)
            null_score = -null_score
            self.board.pop()
            if null_score >= beta:
                return beta, []

        best_score = -float('inf')
        best_move = None
        moves = self._sort_moves(self.board.legal_moves)
        
        for move in moves:
            self.board.push(move)
            score, pv = self._search(depth - 1, ply + 1, -beta, -alpha)
            score = -score
            self.board.pop()

            if self.stop_search:
                return 0, []

            if score > best_score:
                best_score = score
                best_move = move
                best_pv = [move] + pv
            
            alpha = max(alpha, best_score)
            if alpha >= beta:
                break

        tt_flag = TT_EXACT
        if best_score <= alpha_orig:
            tt_flag = TT_BETA
        elif best_score >= beta:
            tt_flag = TT_ALPHA

        if abs(best_score) > MATE_THRESHOLD:
            best_score = (best_score/abs(best_score)) * (abs(best_score) + ply)

        self.transposition_table[zobrist_key] = {
            'depth': depth, 'score': best_score, 'flag': tt_flag, 'move': best_move
        }

        return best_score, best_pv

    def _iterative_deepening_search(self, max_depth: int, max_time: float):
        self.start_time = time.time()
        self.time_limit = max_time
        self.stop_search = False
        
        best_move_overall = None
        
        for depth in range(1, max_depth + 1):
            start_depth_time = time.time()
            self.nodes_searched = 0
            
            score, pv = self._search(depth, 0, -float('inf'), float('inf'))

            if self.stop_search and depth > 1:
                self._log("Time limit reached, using previous result.")
                break
            
            best_move_overall = pv[0] if pv else None
            elapsed_time = time.time() - self.start_time
            
            uci_score = ""
            if abs(score) > MATE_THRESHOLD:
                moves_to_mate = (MATE_SCORE - abs(score) + 1) // 2
                uci_score = f"score mate {moves_to_mate if score > 0 else -moves_to_mate}"
            else:
                uci_score = f"score cp {score}"

            pv_str = " ".join([m.uci() for m in pv])
            nps = int(self.nodes_searched / (time.time() - start_depth_time + 1e-6))
            
            print(f"info depth {depth} {uci_score} nodes {self.nodes_searched} nps {nps} time {int(elapsed_time * 1000)} pv {pv_str}", flush=True)

            if abs(score) > MATE_THRESHOLD:
                self._log("Mate found. Stopping search.")
                break

        final_move = best_move_overall.uci() if best_move_overall else "0000"
        print(f"bestmove {final_move}", flush=True)

    def uci_loop(self):
        while True:
            line = sys.stdin.readline().strip()
            if not line: continue
            parts = line.split()
            command = parts[0]

            if command == "uci":
                print("id name KerasEngineHybrid")
                print("id author YourName")
                print("option name Move Overhead type spin default 10 min 0 max 5000")
                print("option name Hash type spin default 16 min 1 max 1024")
                print("option name Threads type spin default 1 min 1 max 128")
                print("option name SyzygyPath type string default <empty>")
                print("option name UCI_ShowWDL type check default false")
                print("uciok", flush=True)
            elif command == "isready":
                self._load_model()
                self._load_opening_book()
                print("readyok", flush=True)
            elif command == "setoption":
                self._handle_setoption(parts[1:])
            elif command == "ucinewgame":
                self.board.reset()
                self.transposition_table.clear()
            elif command == "position":
                self._handle_position(parts[1:])
            elif command == "go":
                self._handle_go(parts[1:])
            elif command == "stop":
                self.stop_search = True
                if self.search_thread: self.search_thread.join()
            elif command == "quit":
                self.stop_search = True
                if self.search_thread: self.search_thread.join()
                break
    
    def _handle_setoption(self, parts: list[str]):
        if parts[0] != "name": return
        name_parts, value_parts, seen_value = [], [], False
        for part in parts[1:]:
            if part == "value": seen_value = True; continue
            if not seen_value: name_parts.append(part)
            else: value_parts.append(part)
        name, value = " ".join(name_parts), " ".join(value_parts)

        if name in self.options:
            self.options[name] = value
            self._log(f"Set option {name} to {value}")
            if name == "Hash":
                self._log("Clearing transposition table due to Hash option change.")
                self.transposition_table.clear()
        else:
            self._log(f"Warning: Ignoring unsupported option: {name}")

    def _handle_position(self, parts: list[str]):
        moves_start_index = -1
        if parts[0] == "startpos":
            self.board.reset()
            moves_start_index = 1
        elif parts[0] == "fen":
            fen = " ".join(parts[1:7])
            try:
                self.board.set_fen(fen)
                moves_start_index = 7
            except ValueError: self._log(f"Invalid FEN: {fen}"); return
        else: return

        if len(parts) > moves_start_index and parts[moves_start_index] == "moves":
            for move_uci in parts[moves_start_index+1:]:
                try: self.board.push_uci(move_uci)
                except ValueError: self._log(f"Invalid move: {move_uci}")

    def _handle_go(self, parts: list[str]):
        if self.search_thread and self.search_thread.is_alive():
            self._log("Already searching!"); return

        # First, try to play a move from the opening book.
        if self.opening_book:
            try:
                book_move = self.opening_book.find(self.board).move
                self._log(f"Playing book move: {book_move.uci()}")
                print(f"bestmove {book_move.uci()}", flush=True)
                return
            except IndexError:
                # No book move found for this position.
                self._log("No book move found, starting search.")
                pass

        # Set a hard maximum depth of 2 as requested.
        max_depth = 1
        max_time = 60 # Default time
        
        # The engine will respect UCI time controls, but not depth settings > 2.
        if "depth" in parts:
            # Obey the UCI depth if it's lower than or equal to our max depth.
            requested_depth = int(parts[parts.index("depth") + 1])
            max_depth = min(max_depth, requested_depth)
            max_time = float('inf') # If depth is set, ignore time
        elif "movetime" in parts:
            max_time = int(parts[parts.index("movetime") + 1]) / 1000.0
        elif "wtime" in parts:
            time_idx = parts.index("wtime" if self.board.turn == chess.WHITE else "btime") + 1
            remaining_time_ms = int(parts[time_idx])
            move_overhead = int(self.options.get("Move Overhead", 10))
            max_time = (remaining_time_ms / (30 * 1000.0)) - (move_overhead / 1000.0)

        self.search_thread = threading.Thread(target=self._iterative_deepening_search, args=(max_depth, max_time))
        self.search_thread.start()

def main():
    # --- Set up paths for model and opening book ---
    try:
        script_dir = os.path.dirname(os.path.abspath(__file__))
    except NameError:
        script_dir = os.getcwd() 

    model_filename = "chess_eval2.h5"
    book_filename = "book.bin" # Common name for Polyglot books
    
    model_path = os.path.join(script_dir, model_filename)
    book_path = os.path.join(script_dir, book_filename)

    if not os.path.exists(model_path):
        print(f"info string FATAL: Model file not found at {model_path}", file=sys.stderr, flush=True)
        return

    engine = Engine(model_path=model_path, book_path=book_path)
    engine.uci_loop()

if __name__ == "__main__":
    main()
