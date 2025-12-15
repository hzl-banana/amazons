#!/usr/bin/env python3
"""
Visualize Amazons game state for debugging
"""

def visualize_board(board_state):
    """
    Visualize the board state
    board_state: 8x8 array where:
        0 = empty
        1 = black
        2 = white
        3 = arrow
    """
    print("  0 1 2 3 4 5 6 7")
    for y in range(7, -1, -1):
        print(f"{y}", end=" ")
        for x in range(8):
            val = board_state[x][y]
            if val == 0:
                print(".", end=" ")
            elif val == 1:
                print("B", end=" ")
            elif val == 2:
                print("W", end=" ")
            elif val == 3:
                print("X", end=" ")
        print()
    print()

def create_initial_board():
    """Create initial board state"""
    board = [[0 for _ in range(8)] for _ in range(8)]
    
    # Black pieces at (0,2), (2,0), (5,0), (7,2)
    board[0][2] = 1
    board[2][0] = 1
    board[5][0] = 1
    board[7][2] = 1
    
    # White pieces at (0,5), (2,7), (5,7), (7,5)
    board[0][5] = 2
    board[2][7] = 2
    board[5][7] = 2
    board[7][5] = 2
    
    return board

def apply_move(board, sx, sy, ex, ey, ax, ay, player):
    """Apply a move to the board"""
    board[ex][ey] = board[sx][sy]
    board[sx][sy] = 0
    board[ax][ay] = 3
    return board

if __name__ == "__main__":
    print("=== Initial Board State ===")
    board = create_initial_board()
    visualize_board(board)
    
    print("=== After Black's move: [7,2,6,2,4,0] ===")
    board = apply_move(board, 7, 2, 6, 2, 4, 0, 1)
    visualize_board(board)
    
    print("=== After White's move: [7,5,5,3,4,3] ===")
    board = apply_move(board, 7, 5, 5, 3, 4, 3, 2)
    visualize_board(board)
