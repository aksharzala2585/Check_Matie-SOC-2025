import pygame
import random
import json
import argparse
import sys

# === Configuration ===
NUM_BOARDS = 2
BOARD_SPACING = 450   # pixels between left edges of consecutive boards
WINDOW_WIDTH = BOARD_SPACING * NUM_BOARDS
WINDOW_HEIGHT = 500

BOARD_COLOR = "black"
P1_COLOR = "red"
P2_COLOR = "blue"
LOSING_LINE_COLOR = "purple"
BG_COLOR = "white"

CLICK_DELAY = 500  # ms

# === Pygame Initialization ===
pygame.init()
screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
pygame.display.set_caption("Notakto (Misère Tic Tac Toe)")
clock = pygame.time.Clock()
arial_font = pygame.font.SysFont('arialunicode', 36)

# === Command-line Arguments ===
parser = argparse.ArgumentParser()
parser.add_argument('--BotPlayer', type=int, choices=[1,2], required=True, help='Player number (1 or 2) for bot')
parser.add_argument('--BotStrategyFile', type=str, required=True, help='JSON file with bot policy')
args = parser.parse_args()

with open(args.BotStrategyFile, 'r') as f:
    bot_strategy = json.load(f)
bot_player = args.BotPlayer

# === Game State ===
turn = True   # True=Player1 to move next, False=Player2
game_over = False
losing_line = None
loser = None
blank_screen = False

# 0='empty', 'x'='occupied'
board = ['0'] * (9 * NUM_BOARDS)
game_history = []      # list of action indices as strings

last_click_time = 0

# === Precompute mapping from action index → top-left pixel of that cell ===
board_index_to_coordinates_map = {}
for b in range(NUM_BOARDS):
    x_off = b * BOARD_SPACING
    base = b * 9
    for cell in range(9):
        row, col = divmod(cell, 3)
        board_index_to_coordinates_map[base + cell] = (
            x_off + 100 + col*100,
            100 + row*100
        )

def draw_board():
    screen.fill(BG_COLOR)
    if blank_screen and not game_over:
        return

    # Draw each board's grid
    for b in range(NUM_BOARDS):
        x_off = b * BOARD_SPACING
        # vertical lines
        for i in range(4):
            pygame.draw.line(screen, BOARD_COLOR,
                             (x_off + 100 + i*100, 100),
                             (x_off + 100 + i*100, 400), 5)
        # horizontal lines
        for i in range(4):
            pygame.draw.line(screen, BOARD_COLOR,
                             (x_off + 100, 100 + i*100),
                             (x_off + 400, 100 + i*100), 5)

    # Draw all Xs
    for action_str in game_history:
        idx = int(action_str)
        x,y = board_index_to_coordinates_map[idx]
        draw_x(x,y)

    # Outcome display
    if game_over:
        if loser:
            pygame.draw.line(screen, LOSING_LINE_COLOR, *losing_line, 15)
            txt = f"Player {loser} Loses!"
        else:
            txt = "Draw!"
        img = arial_font.render(txt, True, BOARD_COLOR)
        screen.blit(img, (WINDOW_WIDTH//2 - img.get_width()//2, 20))
    else:
        # turn indicator
        txt = f"Player {1 if turn else 2}'s turn (X)"
        color = P1_COLOR if turn else P2_COLOR
        img = arial_font.render(txt, True, color)
        screen.blit(img, (WINDOW_WIDTH//2 - img.get_width()//2, 20))

def draw_x(x, y):
    pygame.draw.line(screen, P1_COLOR, (x+25, y+25), (x+75, y+75), 10)
    pygame.draw.line(screen, P1_COLOR, (x+25, y+75), (x+75, y+25), 10)

def _board_has_triple(b):
    """Return True if board b has any 3-in-a-row of 'x'."""
    base = b*9
    lines = [
        (0,1,2),(3,4,5),(6,7,8),
        (0,3,6),(1,4,7),(2,5,8),
        (0,4,8),(2,4,6)
    ]
    for a,c,d in lines:
        if board[base+a]==board[base+c]==board[base+d]=='x':
            return True
    return False

def _line_coords_for_board(b):
    """Map each triple on board b to its pixel endpoints."""
    base = b*9
    raw = {
        (0,1,2): [(125,150),(375,150)],
        (3,4,5): [(125,250),(375,250)],
        (6,7,8): [(125,350),(375,350)],
        (0,3,6): [(150,125),(150,375)],
        (1,4,7): [(250,125),(250,375)],
        (2,5,8): [(350,125),(350,375)],
        (0,4,8): [(125,125),(375,375)],
        (2,4,6): [(375,125),(125,375)],
    }
    x_off = b * BOARD_SPACING
    # adjust keys and coordinates
    return {
        (base+a, base+c, base+d): [(x_off+u,v) for u,v in ends]
        for (a,c,d), ends in raw.items()
    }

def check_loss():
    """
    In Notakto you lose only if your move completes the *last* board.
    Returns True if this move killed the final board.
    """
    # boards live before
    live = [b for b in range(NUM_BOARDS) if not _board_has_triple(b)]
    # now find which of these just died
    just_killed = []
    for b in live:
        if _board_has_triple(b):
            just_killed.append(b)
    # any remain?
    still_live = [b for b in live if b not in just_killed]
    if just_killed and not still_live:
        # you killed last board → lose
        # pick first triple for line drawing
        b = just_killed[0]
        for (i,j,k), coords in _line_coords_for_board(b).items():
            if board[i]==board[j]==board[k]=='x':
                global losing_line
                losing_line = coords
                break
        return True
    return False

def check_draw():
    return all(c=='x' for c in board)

def make_move(idx):
    global game_over, loser, blank_screen
    board[idx] = 'x'
    game_history.append(str(idx))
    if check_loss():
        loser = 1 if turn else 2
        game_over = True
    elif check_draw():
        loser = None
        game_over = True

def pixel_to_index(mx,my):
    for idx,(x,y) in board_index_to_coordinates_map.items():
        if x < mx < x+100 and y < my < y+100:
            return idx
    return None

def bot_move():
    global turn
    hist = ''.join(game_history)
    if hist not in bot_strategy:
        hist = ''
    dist = bot_strategy.get(hist,{})
    choice = next((int(a) for a,p in dist.items() if p==1.0), None)
    if choice is None:
        valid = [i for i,c in enumerate(board) if c=='0']
        if not valid: return
        choice = random.choice(valid)
    make_move(choice)
    turn = not turn

# === Main Loop ===
while True:
    for e in pygame.event.get():
        if e.type==pygame.QUIT:
            pygame.quit(); sys.exit()
        if e.type==pygame.KEYDOWN:
            if e.key==pygame.K_y:
                # reset
                board = ['0']*(9*NUM_BOARDS)
                game_history.clear()
                turn=True; game_over=False; losing_line=None; loser=None; blank_screen=False
            elif e.key==pygame.K_n:
                pygame.quit(); sys.exit()

    # Bot's turn
    if not game_over and ((turn and bot_player==1) or (not turn and bot_player==2)):
        pygame.time.wait(300)
        bot_move()

    # Human's turn
    if not game_over and ((turn and bot_player!=1) or (not turn and bot_player!=2)):
        if pygame.mouse.get_pressed()[0]:
            mx,my = pygame.mouse.get_pos()
            idx = pixel_to_index(mx,my)
            if idx is not None and board[idx]=='0':
                make_move(idx)
                turn = not turn
                pygame.time.wait(CLICK_DELAY)

    draw_board()
    pygame.display.flip()
    clock.tick(60)
