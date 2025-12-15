# Amazons Game AI Bot

A C++ implementation of an AI bot for the Amazons (亚马逊棋) game to compete on the Botzone platform.

## Game Introduction

Amazons is a two-player strategy game created by Walter Zamkauska in 1988. It's played on an 8×8 chess board with each player controlling 4 pieces (Amazons). The game is used in computer game programming competitions due to its high complexity - the first move alone has over 2,176 possible options.

## Game Rules

1. **Board**: 8×8 grid with coordinates starting from (0,0) in the top-left
2. **Pieces**: Each player has 4 Amazons
   - Black pieces start at: (0,2), (2,0), (5,0), (7,2)
   - White pieces start at: (0,5), (2,7), (5,7), (7,5)
3. **Movement**: Each Amazon moves like a chess Queen (8 directions: horizontal, vertical, diagonal)
4. **Turn Structure**: 
   - Move one Amazon to a new position
   - Shoot an arrow from that Amazon's new position (also Queen-like movement)
   - The arrow becomes a permanent obstacle
5. **Victory**: A player wins when their opponent cannot make any legal moves

## Implementation Features

### Core Components
- **Move Generation**: Generates all legal moves using Queen-like movement patterns
- **Position Evaluation**: 
  - Territory control using flood-fill algorithm
  - Mobility counting (number of reachable squares)
  - Weighted combination favoring territory control
- **Search Algorithm**: 
  - Negamax with alpha-beta pruning
  - Iterative deepening for time management
  - Time-limited search (2.8 seconds per move)

### AI Strategy
The bot uses a combination of:
1. **Territory Control**: Prioritizes controlling larger areas of the board
2. **Mobility**: Maintains freedom of movement for pieces
3. **Look-ahead Search**: Uses alpha-beta pruning to search multiple moves ahead efficiently

## Building and Running

### Prerequisites
- C++11 compatible compiler (g++)
- JsonCpp library

### Build
```bash
make
```

### Test Locally
```bash
make test
```

### Clean
```bash
make clean
```

## Botzone Integration

This bot is designed to work with the Botzone platform's JSON I/O format:
- Reads game state from stdin as JSON
- Outputs move as JSON array: [start_x, start_y, end_x, end_y, arrow_x, arrow_y]

### Botzone Platform
- Website: https://www.botzone.org.cn
- Game: Amazons
- Leaderboard: https://botzone.org/game/ranklist/59463fb292a2ea07a5c6b5a8

## Algorithm Details

### Move Generation
Each move consists of:
1. Select an Amazon piece
2. Move it to any reachable square (Queen movement)
3. Shoot an arrow from new position to any reachable square

Average branching factor: ~1000 moves per position

### Evaluation Function
```
Score = (MyTerritory - OpponentTerritory) × 10 + (MyMobility - OpponentMobility)
```

Territory is calculated using flood-fill from each piece to count controlled empty squares.

### Search Algorithm
- Negamax with alpha-beta pruning for efficient search
- Iterative deepening to maximize search depth within time limit
- Early termination on time limit to ensure valid moves

## Future Enhancements
- Monte Carlo Tree Search (MCTS) implementation
- Opening book for common early game positions
- Endgame database for proven wins/losses
- Parallel search using multiple threads
- Neural network evaluation function

## References
- "An evaluation function for the game of amazons"
- "Amazons Discover Monte-Carlo"
- "The Monte-Carlo Approach in Amazons"

## License
This project is for educational and competition purposes on the Botzone platform
