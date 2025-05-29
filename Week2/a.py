import copy
import math
import logging
import sys
import json

# Increase recursion limit for deep search trees
sys.setrecursionlimit(1000)

# Setup logging
logging.basicConfig(format='%(levelname)s - %(asctime)s - %(message)s',
                    datefmt='%d-%b-%y %H:%M:%S',
                    level=logging.INFO)

# Global variables for memoization and tracking
board_positions_val_dict = {}
visited_histories_list = []
vals = {}  # stores alpha-beta values keyed by board_str
class History:
    def __init__(self, num_boards=2, history=None):
        """
        # self.history : Eg: [0, 4, 2, 5]
            keeps track of sequence of actions played since the beginning of the game.
            Each action is an integer between 0-(9n-1) representing the square in which the move will be played as shown
            below (n=2 is the number of boards).

             Board 1
              ___ ___ ____
             |_0_|_1_|_2_|
             |_3_|_4_|_5_|
             |_6_|_7_|_8_|

             Board 2
              ____ ____ ____
             |_9_|_10_|_11_|
             |_12_|_13_|_14_|
             |_15_|_16_|_17_|

        # self.boards
            empty squares are represented using '0' and occupied squares are 'x'.
            Eg: [['x', '0', 'x', '0', 'x', 'x', '0', '0', '0'], ['0', 0', '0', 0', '0', 0', '0', 0', '0']]
            for two board game

            Board 1
              ___ ___ ____
             |_x_|___|_x_|
             |___|_x_|_x_|
             |___|___|___|

            Board 2
              ___ ___ ____
             |___|___|___|
             |___|___|___|
             |___|___|___|

        # self.player: 1 or 2
            Player whose turn it is at the current history/board

        :param num_boards: Number of boards in the game of Notakto.
        :param history: list keeps track of sequence of actions played since the beginning of the game.
        """
        self.num_boards = num_boards
        if history is not None:
            self.history = history
            self.boards = self.get_boards()
        else:
            self.history = []
            self.boards = []
            for i in range(self.num_boards):
                # empty boards
                self.boards.append(['0', '0', '0', '0', '0', '0', '0', '0', '0'])
        # Maintain a list to keep track of active boards
        self.active_board_stats = self.check_active_boards()
        self.current_player = self.get_current_player()

    def get_boards(self):
        """ Play out the current self.history and get the boards corresponding to the history.

        :return: list of lists
                Eg: [['x', '0', 'x', '0', 'x', 'x', '0', '0', '0'], ['0', 0', '0', 0', '0', 0', '0', 0', '0']]
                for two board game

                Board 1
                  ___ ___ ____
                 |_x_|___|_x_|
                 |___|_x_|_x_|
                 |___|___|___|

                Board 2
                  ___ ___ ____
                 |___|___|___|
                 |___|___|___|
                 |___|___|___|
        """
        boards = []
        for i in range(self.num_boards):
            boards.append(['0', '0', '0', '0', '0', '0', '0', '0', '0'])
        for i in range(len(self.history)):
            board_num = math.floor(self.history[i] / 9)
            play_position = self.history[i] % 9
            boards[board_num][play_position] = 'x'
        return boards

    def check_active_boards(self):
        """ Return a list to keep track of active boards

        :return: list of int (zeros and ones)
                Eg: [0, 1]
                for two board game

                Board 1
                  ___ ___ ____
                 |_x_|_x_|_x_|
                 |___|_x_|_x_|
                 |___|___|___|

                Board 2
                  ___ ___ ____
                 |___|___|___|
                 |___|___|___|
                 |___|___|___|
        """
        active_board_stat = []
        for i in range(self.num_boards):
            if self.is_board_win(self.boards[i]):
                active_board_stat.append(0)
            else:
                active_board_stat.append(1)
        return active_board_stat

    @staticmethod
    def is_board_win(board):
        for i in range(3):
            if board[3 * i] == board[3 * i + 1] == board[3 * i + 2] != '0':
                return True

            if board[i] == board[i + 3] == board[i + 6] != '0':
                return True

        if board[0] == board[4] == board[8] != '0':
            return True

        if board[2] == board[4] == board[6] != '0':
            return True
        return False

    def get_current_player(self):
        """
        Get player whose turn it is at the current history/board
        :return: 1 or 2
        """
        total_num_moves = len(self.history)
        if total_num_moves % 2 == 0:
            return 1
        else:
            return 2

    def get_boards_str(self):
        boards_str = ""
        for i in range(self.num_boards):
            boards_str = boards_str + ''.join([str(j) for j in self.boards[i]])
        return boards_str

    def is_win(self):
        return all(state == 0 for state in self.active_board_stats)

    def get_valid_actions(self):
        # Feel free to implement this in anyway if needed
        valid = []
        for i in range(self.num_boards):
            if self.active_board_stats[i] == 1:
                board = self.boards[i]
                for j, pos in enumerate(board):
                    if pos == '0':
                        valid.append(i * 9 + j)
        return valid

    def is_terminal_history(self):
        # Feel free to implement this in anyway if needed
        return self.is_win()

    def get_value_given_terminal_history(self):
        # Feel free to implement this in anyway if needed
        return (self.current_player%2)*2 -1

def sort_valid_actions(actions):
    # center first, then corners, then edges
    priority = {4:0, 0:1,2:1,6:1,8:1, 1:2,3:2,5:2,7:2}
    return sorted(actions, key=lambda a: priority.get(a%9,3))

def alpha_beta_pruning(h, alpha, beta, max_flag):
    visited_histories_list.append(tuple(h.history))
    key = h.get_boards_str()
    if key in vals:
        return vals[key]
    if h.is_terminal_history():
        v = h.get_value_given_terminal_history()
        vals[key] = v
        return v

    best = -math.inf if max_flag else math.inf
    for a in sort_valid_actions(h.get_valid_actions()):
        h.history.append(a)
        child = History(h.num_boards, h.history)
        v = alpha_beta_pruning(child, alpha, beta, not max_flag)
        h.history.pop()

        if max_flag:
            best = max(best, v)
            alpha = max(alpha, v)
        else:
            best = min(best, v)
            beta = min(beta, v)
        if beta <= alpha:
            break

    vals[key] = best
    return best

def solve_alpha_beta(num_boards):
    visited_histories_list.clear()
    vals.clear()
    root = History(num_boards, [])
    alpha_beta_pruning(root, -math.inf, math.inf, True)
    return root

def extract_policy(num_boards):
    """
    From the computed `vals`, walk every visited history,
    and for each non-terminal history pick the action
    that attains the stored value.  Build two dicts:
      policy1[boards_str] = { action_str: 1.0 or 0.0, ... }
      policy2[...]
    """
    policy1 = {}
    policy2 = {}
    seen = set(visited_histories_list)
    for hist in seen:
        h = History(num_boards, list(hist))
        key = h.get_boards_str()
        if h.is_terminal_history():
            continue

        player = h.current_player
        target = 1 if player==1 else -1  # we want to maximize for p1, minimize for p2
        best_action = None
        best_val = -math.inf if player==1 else math.inf

        for a in h.get_valid_actions():
            child = History(num_boards, h.history + [a])
            v = vals.get(child.get_boards_str())
            if v is None:
                continue
            if (player==1 and v>best_val) or (player==2 and v<best_val):
                best_val = v
                best_action = a

        if best_action is None:
            continue

        dist = { str(a): (1.0 if a==best_action else 0.0) for a in h.get_valid_actions() }
        if player==1:
            policy1[key] = dist
        else:
            policy2[key] = dist

    # write out JSON
    with open("policy_player1.json","w") as f:
        json.dump(policy1, f, indent=2)
    with open("policy_player2.json","w") as f:
        json.dump(policy2, f, indent=2)

    logging.info(f"Wrote {len(policy1)} states to policy_player1.json")
    logging.info(f"Wrote {len(policy2)} states to policy_player2.json")

if __name__=="__main__":
    logging.info("Solving Notakto (2 boards) with Alpha-Beta Pruning…")
    solve_alpha_beta(num_boards=2)
    logging.info(f"Visited {len(visited_histories_list)} distinct histories, stored {len(vals)} board‐values")
    extract_policy(num_boards=2)
    logging.info("Done.")
