#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <climits>
#include <cstdlib>
#include <cmath>
#include <jsoncpp/json/json.h>

using namespace std;

const int BOARD_SIZE = 8;
const int MAX_DEPTH = 4;
const int TIME_LIMIT = 2800; // 2.8 seconds for move calculation

// Direction vectors for queen-like movement (8 directions)
const int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
const int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};

// Player colors
const int EMPTY = 0;
const int BLACK = 1;
const int WHITE = 2;
const int ARROW = 3;

struct Move {
    int sx, sy;  // start position
    int ex, ey;  // end position
    int ax, ay;  // arrow position
    
    Move() : sx(-1), sy(-1), ex(-1), ey(-1), ax(-1), ay(-1) {}
    Move(int sx, int sy, int ex, int ey, int ax, int ay) 
        : sx(sx), sy(sy), ex(ex), ey(ey), ax(ax), ay(ay) {}
};

class AmazonsBoard {
private:
    int board[BOARD_SIZE][BOARD_SIZE];
    int myColor;
    int opponentColor;
    clock_t startTime;
    
public:
    AmazonsBoard() {
        memset(board, 0, sizeof(board));
        myColor = BLACK;
        opponentColor = WHITE;
    }
    
    void initialize(int color) {
        myColor = color;
        opponentColor = (color == BLACK) ? WHITE : BLACK;
        
        // Initialize board with starting positions
        // According to specs: 
        // Black pieces at (0,2), (2,0), (5,0), (7,2)
        // White pieces at (0,5), (2,7), (5,7), (7,5)
        board[0][2] = BLACK;
        board[2][0] = BLACK;
        board[5][0] = BLACK;
        board[7][2] = BLACK;
        
        board[0][5] = WHITE;
        board[2][7] = WHITE;
        board[5][7] = WHITE;
        board[7][5] = WHITE;
    }
    
    bool isValid(int x, int y) {
        return x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE;
    }
    
    bool isEmpty(int x, int y) {
        return isValid(x, y) && board[x][y] == EMPTY;
    }
    
    void makeMove(const Move& move) {
        board[move.ex][move.ey] = board[move.sx][move.sy];
        board[move.sx][move.sy] = EMPTY;
        board[move.ax][move.ay] = ARROW;
    }
    
    void undoMove(const Move& move, int originalPiece) {
        board[move.sx][move.sy] = board[move.ex][move.ey];
        board[move.ex][move.ey] = EMPTY;
        board[move.ax][move.ay] = EMPTY;
    }
    
    void applyOpponentMove(int sx, int sy, int ex, int ey, int ax, int ay) {
        board[ex][ey] = board[sx][sy];
        board[sx][sy] = EMPTY;
        board[ax][ay] = ARROW;
    }
    
    // Get all reachable positions from (x, y) using queen moves
    void getReachablePositions(int x, int y, vector<pair<int, int>>& positions) {
        for (int dir = 0; dir < 8; dir++) {
            int nx = x + dx[dir];
            int ny = y + dy[dir];
            
            while (isValid(nx, ny) && board[nx][ny] == EMPTY) {
                positions.push_back(make_pair(nx, ny));
                nx += dx[dir];
                ny += dy[dir];
            }
        }
    }
    
    // Generate all legal moves for current player
    vector<Move> generateMoves(int color) {
        vector<Move> moves;
        
        // Find all pieces of the current player
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (board[i][j] == color) {
                    // Get all positions this piece can move to
                    vector<pair<int, int>> destinations;
                    getReachablePositions(i, j, destinations);
                    
                    // For each destination, generate arrow placements
                    for (const auto& dest : destinations) {
                        int ex = dest.first;
                        int ey = dest.second;
                        
                        // Temporarily move the piece
                        board[ex][ey] = board[i][j];
                        board[i][j] = EMPTY;
                        
                        // Get all positions where arrow can be placed
                        vector<pair<int, int>> arrowPositions;
                        getReachablePositions(ex, ey, arrowPositions);
                        
                        for (const auto& arrow : arrowPositions) {
                            moves.push_back(Move(i, j, ex, ey, arrow.first, arrow.second));
                        }
                        
                        // Undo temporary move
                        board[i][j] = board[ex][ey];
                        board[ex][ey] = EMPTY;
                    }
                }
            }
        }
        
        return moves;
    }
    
    // Territory evaluation using flood fill from each piece
    int countTerritory(int color) {
        bool visited[BOARD_SIZE][BOARD_SIZE];
        memset(visited, false, sizeof(visited));
        
        int territory = 0;
        
        // Count all squares reachable by this color using queen moves
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (board[i][j] == color && !visited[i][j]) {
                    // Use BFS to find all reachable squares
                    vector<pair<int, int>> queue;
                    queue.push_back(make_pair(i, j));
                    visited[i][j] = true;
                    
                    while (!queue.empty()) {
                        pair<int, int> current = queue.back();
                        queue.pop_back();
                        int x = current.first;
                        int y = current.second;
                        
                        // Check all 8 queen-move directions
                        for (int dir = 0; dir < 8; dir++) {
                            int nx = x + dx[dir];
                            int ny = y + dy[dir];
                            
                            // Continue in this direction until blocked
                            while (isValid(nx, ny) && !visited[nx][ny]) {
                                if (board[nx][ny] == EMPTY) {
                                    visited[nx][ny] = true;
                                    territory++;
                                    queue.push_back(make_pair(nx, ny));
                                    // Continue along this direction
                                    nx += dx[dir];
                                    ny += dy[dir];
                                } else if (board[nx][ny] == color) {
                                    // Another piece of same color - mark visited and add to queue
                                    visited[nx][ny] = true;
                                    queue.push_back(make_pair(nx, ny));
                                    break;
                                } else {
                                    // Hit opponent or arrow - can't continue
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        return territory;
    }
    
    // Mobility evaluation - count number of moves
    int countMobility(int color) {
        int mobility = 0;
        
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (board[i][j] == color) {
                    vector<pair<int, int>> positions;
                    getReachablePositions(i, j, positions);
                    mobility += positions.size();
                }
            }
        }
        
        return mobility;
    }
    
    // Improved evaluation function with multiple factors
    int evaluate() {
        int myTerritory = countTerritory(myColor);
        int oppTerritory = countTerritory(opponentColor);
        int myMobility = countMobility(myColor);
        int oppMobility = countMobility(opponentColor);
        
        // Count piece positions (centrality bonus)
        int myCentrality = 0;
        int oppCentrality = 0;
        
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (board[i][j] == myColor) {
                    // Bonus for being near center (center is between 3 and 4)
                    // Distance from center: use distance from center of board
                    int distFromCenter = abs(i * 2 - 7) + abs(j * 2 - 7);
                    myCentrality += (14 - distFromCenter);
                } else if (board[i][j] == opponentColor) {
                    int distFromCenter = abs(i * 2 - 7) + abs(j * 2 - 7);
                    oppCentrality += (14 - distFromCenter);
                }
            }
        }
        
        // Weighted evaluation: territory > mobility > centrality
        int territoryScore = (myTerritory - oppTerritory) * 10;
        int mobilityScore = (myMobility - oppMobility) * 2;
        int centralityScore = (myCentrality - oppCentrality);
        
        return territoryScore + mobilityScore + centralityScore;
    }
    
    // Score a move for ordering (higher is better)
    int scoreMove(const Move& move, int color) {
        int score = 0;
        
        // Prefer moves that control the center (center is between 3 and 4)
        int centerDist = abs(move.ex * 2 - 7) + abs(move.ey * 2 - 7);
        score -= centerDist;
        
        // Prefer moves that restrict opponent mobility
        // (arrow placed near opponent pieces)
        int oppColor = (color == BLACK) ? WHITE : BLACK;
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (board[i][j] == oppColor) {
                    int dist = abs(move.ax - i) + abs(move.ay - j);
                    if (dist <= 3) {
                        score += 5;
                    }
                }
            }
        }
        
        return score;
    }
    
    // Generate moves with ordering for better pruning
    vector<Move> generateOrderedMoves(int color) {
        vector<Move> moves = generateMoves(color);
        
        // For small move counts, ordering overhead isn't worth it
        if (moves.size() < 50) {
            return moves;
        }
        
        // Score and sort moves
        vector<pair<int, Move>> scoredMoves;
        for (const Move& move : moves) {
            int score = scoreMove(move, color);
            scoredMoves.push_back(make_pair(score, move));
        }
        
        // Sort by score descending (best moves first)
        sort(scoredMoves.begin(), scoredMoves.end(), 
             [](const pair<int, Move>& a, const pair<int, Move>& b) {
                 return a.first > b.first;
             });
        
        // Extract moves
        vector<Move> orderedMoves;
        for (const auto& sm : scoredMoves) {
            orderedMoves.push_back(sm.second);
        }
        
        return orderedMoves;
    }
    
    // Alpha-beta negamax search
    int negamax(int depth, int alpha, int beta, int color) {
        // Check time limit
        if ((clock() - startTime) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT) {
            return evaluate();
        }
        
        if (depth == 0) {
            return evaluate();
        }
        
        vector<Move> moves = generateOrderedMoves(color);
        
        // Terminal state check
        if (moves.empty()) {
            return (color == myColor) ? -100000 : 100000;
        }
        
        int maxScore = INT_MIN;
        
        for (const Move& move : moves) {
            makeMove(move);
            
            int score = -negamax(depth - 1, -beta, -alpha, 
                                (color == BLACK) ? WHITE : BLACK);
            
            undoMove(move, color);
            
            maxScore = max(maxScore, score);
            alpha = max(alpha, score);
            
            if (alpha >= beta) {
                break; // Beta cutoff
            }
            
            // Time check
            if ((clock() - startTime) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT) {
                break;
            }
        }
        
        return maxScore;
    }
    
    // Find best move using iterative deepening
    Move findBestMove() {
        startTime = clock();
        
        vector<Move> moves = generateOrderedMoves(myColor);
        
        if (moves.empty()) {
            return Move(); // No valid moves
        }
        
        if (moves.size() == 1) {
            return moves[0];
        }
        
        Move bestMove = moves[0];
        int bestScore = INT_MIN;
        
        // Try iterative deepening
        for (int depth = 1; depth <= MAX_DEPTH; depth++) {
            int alpha = INT_MIN;
            int beta = INT_MAX;
            Move depthBestMove = bestMove;
            int depthBestScore = INT_MIN;
            
            for (const Move& move : moves) {
                makeMove(move);
                
                int score = -negamax(depth - 1, -beta, -alpha, opponentColor);
                
                undoMove(move, myColor);
                
                if (score > depthBestScore) {
                    depthBestScore = score;
                    depthBestMove = move;
                }
                
                alpha = max(alpha, score);
                
                // Check time limit
                if ((clock() - startTime) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT) {
                    return bestMove;
                }
            }
            
            // Update best move if we completed this depth
            if (depthBestScore > bestScore) {
                bestScore = depthBestScore;
                bestMove = depthBestMove;
            }
            
            // Check time for next depth
            if ((clock() - startTime) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT / 2) {
                break;
            }
        }
        
        return bestMove;
    }
    
    void printBoard() {
        for (int j = BOARD_SIZE - 1; j >= 0; j--) {
            for (int i = 0; i < BOARD_SIZE; i++) {
                if (board[i][j] == EMPTY) cout << ". ";
                else if (board[i][j] == BLACK) cout << "B ";
                else if (board[i][j] == WHITE) cout << "W ";
                else if (board[i][j] == ARROW) cout << "X ";
            }
            cout << endl;
        }
        cout << endl;
    }
};

int main() {
    AmazonsBoard game;
    
    string str;
    int turnID;
    
    // Read all input
    string input;
    while (getline(cin, str)) {
        input += str;
    }
    
    Json::Reader reader;
    Json::Value root;
    
    if (!reader.parse(input, root)) {
        return 1;
    }
    
    turnID = root["responses"].size();
    
    // Determine my color (BLACK plays first, WHITE plays second)
    int myColor = (turnID % 2 == 0) ? BLACK : WHITE;
    game.initialize(myColor);
    
    // Replay all previous moves
    for (int i = 0; i < turnID; i++) {
        Json::Value request;
        Json::Value response;
        
        if (i < root["requests"].size()) {
            request = root["requests"][i];
        }
        if (i < root["responses"].size()) {
            response = root["responses"][i];
        }
        
        if (i % 2 == 0) {
            // Black's turn
            if (!response.isNull() && response.isArray() && response.size() == 6) {
                int sx = response[0].asInt();
                int sy = response[1].asInt();
                int ex = response[2].asInt();
                int ey = response[3].asInt();
                int ax = response[4].asInt();
                int ay = response[5].asInt();
                
                if (myColor == BLACK) {
                    // This is my previous move, skip
                } else {
                    // This is opponent's move
                    game.applyOpponentMove(sx, sy, ex, ey, ax, ay);
                }
            }
        } else {
            // White's turn
            if (!request.isNull() && request.isArray() && request.size() == 6) {
                int sx = request[0].asInt();
                int sy = request[1].asInt();
                int ex = request[2].asInt();
                int ey = request[3].asInt();
                int ax = request[4].asInt();
                int ay = request[5].asInt();
                
                if (myColor == WHITE) {
                    // This is my previous move, skip
                } else {
                    // This is opponent's move
                    game.applyOpponentMove(sx, sy, ex, ey, ax, ay);
                }
            }
        }
    }
    
    // Find and output best move
    Move bestMove = game.findBestMove();
    
    Json::Value response;
    response.append(bestMove.sx);
    response.append(bestMove.sy);
    response.append(bestMove.ex);
    response.append(bestMove.ey);
    response.append(bestMove.ax);
    response.append(bestMove.ay);
    
    Json::FastWriter writer;
    cout << writer.write(response) << endl;
    
    return 0;
}
