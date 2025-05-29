import json
import copy  # use it for deepcopy if needed
import math  # for math.inf
import logging
import sys
sys.setrecursionlimit(300000)
logging.basicConfig(format='%(levelname)s - %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S',
                    level=logging.INFO)

# Global variables in which you need to store player strategies (this is data structure that'll be used for evaluation)
# Mapping from histories (str) to probability distribution over actions
strategy_dict_x = {}
strategy_dict_o = {}

visited = {}  # Memoization to avoid repeated recursion calls in backward_induction

class History:
    def __init__(self, history=None):
        """
        # self.history : Eg: [0, 4, 2, 5]
            keeps track of sequence of actions played since the beginning of the game.
            Each action is an integer between 0-8 representing the square in which the move will be played as shown
            below.
              ___ ___ ____
             |_0_|_1_|_2_|
             |_3_|_4_|_5_|
             |_6_|_7_|_8_|

        # self.board
            empty squares are represented using '0' and occupied squares are either 'x' or 'o'.
            Eg: ['x', '0', 'x', '0', 'o', 'o', '0', '0', '0']
            for board
              ___ ___ ____
             |_x_|___|_x_|
             |___|_o_|_o_|
             |___|___|___|

        # self.player: 'x' or 'o'
            Player whose turn it is at the current history/board

        :param history: list keeps track of sequence of actions played since the beginning of the game.
        """
        if history is not None:
            self.history = history
            self.board = self.get_board()
        else:
            self.history = []
            self.board = ['0', '0', '0', '0', '0', '0', '0', '0', '0']
        self.player = self.current_player()

    def current_player(self):
        """ Player function
        Get player whose turn it is at the current history/board
        :return: 'x' or 'o' or None
        """
        total_num_moves = len(self.history)
        if total_num_moves < 9:
            if total_num_moves % 2 == 0:
                return 'x'
            else:
                return 'o'
        else:
            return None

    def get_board(self):
        """ Play out the current self.history and get the board corresponding to the history in self.board.

        :return: list Eg: ['x', '0', 'x', '0', 'o', 'o', '0', '0', '0']
        """
        board = ['0', '0', '0', '0', '0', '0', '0', '0', '0']
        for i in range(len(self.history)):
            if i % 2 == 0:
                board[self.history[i]] = 'x'
            else:
                board[self.history[i]] = 'o'
        return board

    def is_sorted_subset(self, a, b):
        i = j = 0
        while i < len(a) and j < len(b):
            if a[i] == b[j]:
                i += 1
                j += 1
            elif a[i] > b[j]:
                j += 1
            else:
                return False
        return i == len(a)

    def is_win(self):
        # check if the board position is a win for either players
        # Feel free to implement this in anyway if needed
        cor = [[0,1,2],[3,4,5],[6,7,8],[0,3,6], [1,4,7], [2,5,8], [0,4,8], [2,4,6]]
        x =[]
        o =[]
        b = self.board
        for key in range(9):
            if(b[key]=='o'):
                o.append(key)
            elif(b[key]=='x'):
                x.append(key)
        win = False
        for k in cor:
            win = self.is_sorted_subset(k,o)
            if(win):
                return win

        for k in cor:
            win = self.is_sorted_subset(k,x)
            if(win):
                return win
        return win
                
    def is_draw(self):
        if((not self.is_win()) and len(self.history)==9):
            return True
        return False

    def get_valid_actions(self):
        # get the empty squares from the board
        # Feel free to implement this in anyway if needed
        valid = []
        for i in range(9):
            if(self.board[i]=='0'):
                valid.append(i)
        return valid

    def is_terminal_history(self):
        # check if the history is a terminal history
        # Feel free to implement this in anyway if needed
        if(self.is_win() or self.is_draw()):
            return True
        return False

    def get_utility_given_terminal_history(self):
        # Feel free to implement this in anyway if needed
        if(self.is_draw()):
            return 0
        elif(self.is_win()):
            if(len(self.history)%2==1):
                return 1
            else:
                return -1
        pass

    def update_history(self, action):
        # In case you need to create a deepcopy and update the history obj to get the next history object.
        # Feel free to implement this in anyway if needed
        self.history.append(action)
        a = History(self.history.copy())
        self.history.pop()
        return a

vals = {}
has_val = {}

def eval(history_obj):
    h_str = str(history_obj.history)
    if h_str in has_val:
        return vals[h_str]
    if history_obj.is_terminal_history():
        v = history_obj.get_utility_given_terminal_history()
        has_val[h_str] = True
        vals[h_str] = v
    elif history_obj.player == 'x':
        value = -float('inf')
        for action in history_obj.get_valid_actions():
            ob = history_obj.update_history(action)
            value = max(value, eval(ob))
        has_val[h_str] = True
        vals[h_str] = value
    else:
        value = float('inf')
        for action in history_obj.get_valid_actions():
            ob = history_obj.update_history(action)
            value = min(value, eval(ob))
        has_val[h_str] = True
        vals[h_str] = value
    return vals[h_str]

def convert_history(h):
    s = ""
    for k in h:
        s += str(k)
    return s

def backward_induction(history_obj):
    """
    :param history_obj: Histroy class object
    :return: best achievable utility (float) for th current history_obj
    """
    global strategy_dict_x, strategy_dict_o, visited
    # TODO implement
    # (1) Implement backward induction for tictactoe
    # (2) Update the global variables strategy_dict_x or strategy_dict_o which are a mapping from histories to
    # probability distribution over actions.
    # (2a)These are dictionary with keys as string representation of the history list e.g. if the history list of the
    # history_obj is [0, 4, 2, 5], then the key is "0425". Each value is in turn a dictionary with keys as actions 0-8
    # (str "0", "1", ..., "8") and each value of this dictionary is a float (representing the probability of
    # choosing that action). Example: {”0452”: {”0”: 0, ”1”: 0, ”2”: 0, ”3”: 0, ”4”: 0, ”5”: 0, ”6”: 1, ”7”: 0, ”8”:
    # 0}}
    # (2b) Note, the strategy for each history in strategy_dict_x and strategy_dict_o is probability distribution over
    # actions. But since tictactoe is a PIEFG, there always exists an optimal deterministic strategy (SPNE). So your
    # policy will be something like this {"0": 1, "1": 0, "2": 0, "3": 0, "4": 0, "5": 0, "6": 0, "7": 0, "8": 0} where
    # "0" was the one of the best actions for the current player/history.

    h = history_obj
    u = convert_history(h.history)

    if u in visited:
        return
    visited[u] = True

    if h.is_terminal_history():
        return eval(history_obj) 

    # Recursive call for all children states
    for action in h.get_valid_actions():
        ob = h.update_history(action)
        backward_induction(ob)

    mini = float('inf')
    maxi = -float('inf')
    mini_action = -1
    maxi_action = -1

    for action in h.get_valid_actions():
        ob = h.update_history(action)
        a = eval(ob)
        if a < mini:
            mini = a
            mini_action = action
        if a > maxi:
            maxi = a
            maxi_action = action

    s = convert_history(h.history)
    if h.player == 'x':
        d = {}
        for i in range(9):
            if i != maxi_action:
                d[f'{i}'] = 0.0
            else:
                d[f'{i}'] = 1.0
        strategy_dict_x[s] = d
    else:
        d = {}
        for i in range(9):
            if i != mini_action:
                d[f'{i}'] = 0.0
            else:
                d[f'{i}'] = 1.0
        strategy_dict_o[s] = d

def solve_tictactoe():
    backward_induction(History())
    with open('./policy_x.json', 'w') as f:
        json.dump(strategy_dict_x, f)
    with open('./policy_o.json', 'w') as f:
        json.dump(strategy_dict_o, f)
    return strategy_dict_x, strategy_dict_o

if __name__ == "__main__":
    logging.info("Start")
    solve_tictactoe()
    logging.info("End")
