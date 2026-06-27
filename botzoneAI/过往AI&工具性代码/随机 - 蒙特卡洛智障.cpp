#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <cmath>
#include <algorithm>

#define GRIDSIZE 8
#define OBSTACLE 2
#define judge_black 0
#define judge_white 1
#define grid_black 1
#define grid_white -1

using namespace std;

int currBotColor;
int gridInfo[GRIDSIZE][GRIDSIZE] = { 0 };
int dx[] = { -1,-1,-1,0,0,1,1,1 };
int dy[] = { -1,0,1,-1,1,-1,0,1 };

// 移动结构体
struct Move {
    int startX, startY;
    int endX, endY;
    int obstacleX, obstacleY;
    
    Move(int sx = -1, int sy = -1, int ex = -1, int ey = -1, int ox = -1, int oy = -1) 
        : startX(sx), startY(sy), endX(ex), endY(ey), obstacleX(ox), obstacleY(oy) {}
    
    bool isValid() const {
        return startX != -1;
    }
};

inline bool inMap(int x, int y)
{
    if (x < 0 || x >= GRIDSIZE || y < 0 || y >= GRIDSIZE)
        return false;
    return true;
}

bool ProcStep(int x0, int y0, int x1, int y1, int x2, int y2, int color, bool check_only)
{
    if ((!inMap(x0, y0)) || (!inMap(x1, y1)) || (!inMap(x2, y2)))
        return false;
    if (gridInfo[x0][y0] != color || gridInfo[x1][y1] != 0)
        return false;
    if ((gridInfo[x2][y2] != 0) && !(x2 == x0 && y2 == y0))
        return false;
    if (!check_only)
    {
        gridInfo[x0][y0] = 0;
        gridInfo[x1][y1] = color;
        gridInfo[x2][y2] = OBSTACLE;
    }
    return true;
}

// 生成所有合法移动
vector<Move> generateAllMoves(int color) {
    vector<Move> moves;
    
    for (int i = 0; i < GRIDSIZE; ++i) {
        for (int j = 0; j < GRIDSIZE; ++j) {
            if (gridInfo[i][j] == color) {
                for (int k = 0; k < 8; ++k) {
                    for (int delta1 = 1; delta1 < GRIDSIZE; delta1++) {
                        int xx = i + dx[k] * delta1;
                        int yy = j + dy[k] * delta1;
                        if (gridInfo[xx][yy] != 0 || !inMap(xx, yy))
                            break;
                        for (int l = 0; l < 8; ++l) {
                            for (int delta2 = 1; delta2 < GRIDSIZE; delta2++) {
                                int xxx = xx + dx[l] * delta2;
                                int yyy = yy + dy[l] * delta2;
                                if (!inMap(xxx, yyy))
                                    break;
                                if (gridInfo[xxx][yyy] != 0 && !(i == xxx && j == yyy))
                                    break;
                                if (ProcStep(i, j, xx, yy, xxx, yyy, color, true)) {
                                    moves.push_back(Move(i, j, xx, yy, xxx, yyy));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return moves;
}

// 蒙特卡洛树节点
class MCTSNode {
public:
    MCTSNode* parent;
    vector<MCTSNode*> children;
    Move move;
    int visits;
    double wins;
    int player;
    vector<Move> untriedMoves;
    
    MCTSNode(Move m = Move(), MCTSNode* p = nullptr, int pl = 0) 
        : parent(p), move(m), visits(0), wins(0.0), player(pl) {}
    
    ~MCTSNode() {
        for (MCTSNode* child : children) {
            delete child;
        }
    }
    
    MCTSNode* selectChild() {
        MCTSNode* bestChild = nullptr;
        double bestScore = -1.0;
        
        for (MCTSNode* child : children) {
            if (child->visits == 0) {
                return child;
            }
            double uctValue = (child->wins / child->visits) + 
                             sqrt(2 * log(visits) / child->visits);
            
            if (uctValue > bestScore) {
                bestScore = uctValue;
                bestChild = child;
            }
        }
        return bestChild;
    }
    
    MCTSNode* addChild(Move m, int player) {
        MCTSNode* child = new MCTSNode(m, this, player);
        children.push_back(child);
        
        // 从untriedMoves中移除这个移动
        for (auto it = untriedMoves.begin(); it != untriedMoves.end(); ++it) {
            if (it->startX == m.startX && it->startY == m.startY &&
                it->endX == m.endX && it->endY == m.endY &&
                it->obstacleX == m.obstacleX && it->obstacleY == m.obstacleY) {
                untriedMoves.erase(it);
                break;
            }
        }
        return child;
    }
    
    void update(double result) {
        visits++;
        wins += result;
    }
};

// 游戏状态类（简化版，用于模拟）
class GameState {
private:
    int tempGrid[GRIDSIZE][GRIDSIZE];
    
public:
    int currentPlayer;
    
    GameState() : currentPlayer(currBotColor) {
        // 复制当前棋盘状态
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                tempGrid[i][j] = gridInfo[i][j];
            }
        }
    }
    
    // 检查移动是否合法
    bool isValidMove(const Move& move, int color) const {
        if (!inMap(move.startX, move.startY) || 
            !inMap(move.endX, move.endY) || 
            !inMap(move.obstacleX, move.obstacleY))
            return false;
        
        if (tempGrid[move.startX][move.startY] != color || 
            tempGrid[move.endX][move.endY] != 0)
            return false;
        
        if (tempGrid[move.obstacleX][move.obstacleY] != 0 && 
            !(move.obstacleX == move.startX && move.obstacleY == move.startY))
            return false;
        
        return true;
    }
    
    // 执行移动
    void makeMove(const Move& move, int color) {
        if (isValidMove(move, color)) {
            tempGrid[move.startX][move.startY] = 0;
            tempGrid[move.endX][move.endY] = color;
            tempGrid[move.obstacleX][move.obstacleY] = OBSTACLE;
            currentPlayer = -color;
        }
    }
    
    // 检查游戏是否结束
    bool isTerminal() const {
        return generateAllMovesForState(currentPlayer).empty() && 
               generateAllMovesForState(-currentPlayer).empty();
    }
    
    // 获取获胜者
    int getWinner() const {
        int blackCount = 0, whiteCount = 0;
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                if (tempGrid[i][j] == grid_black) blackCount++;
                else if (tempGrid[i][j] == grid_white) whiteCount++;
            }
        }
        if (blackCount > whiteCount) return grid_black;
        else if (whiteCount > blackCount) return grid_white;
        else return 0; // 平局
    }
    
private:
    // 为当前状态生成所有移动
    vector<Move> generateAllMovesForState(int color) const {
        vector<Move> moves;
        
        for (int i = 0; i < GRIDSIZE; ++i) {
            for (int j = 0; j < GRIDSIZE; ++j) {
                if (tempGrid[i][j] == color) {
                    for (int k = 0; k < 8; ++k) {
                        for (int delta1 = 1; delta1 < GRIDSIZE; delta1++) {
                            int xx = i + dx[k] * delta1;
                            int yy = j + dy[k] * delta1;
                            if (tempGrid[xx][yy] != 0 || !inMap(xx, yy))
                                break;
                            for (int l = 0; l < 8; ++l) {
                                for (int delta2 = 1; delta2 < GRIDSIZE; delta2++) {
                                    int xxx = xx + dx[l] * delta2;
                                    int yyy = yy + dy[l] * delta2;
                                    if (!inMap(xxx, yyy))
                                        break;
                                    if (tempGrid[xxx][yyy] != 0 && !(i == xxx && j == yyy))
                                        break;
                                    
                                    Move move(i, j, xx, yy, xxx, yyy);
                                    if (isValidMove(move, color)) {
                                        moves.push_back(move);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return moves;
    }
};

// 蒙特卡洛树搜索
Move monteCarloTreeSearch(int maxIterations = 500) {
    vector<Move> allMoves = generateAllMoves(currBotColor);
    
    if (allMoves.empty()) {
        return Move(-1, -1, -1, -1, -1, -1);
    }
    
    // 如果移动很少，直接返回第一个
    if (allMoves.size() == 1) {
        return allMoves[0];
    }
    
    MCTSNode* root = new MCTSNode(Move(), nullptr, currBotColor);
    root->untriedMoves = allMoves;
    
    for (int i = 0; i < maxIterations; i++) {
        MCTSNode* node = root;
        GameState state;
        
        // 1. 选择
        while (!node->untriedMoves.empty() && node->children.empty()) {
            if (node->children.empty()) {
                break;
            } else {
                node = node->selectChild();
                state.makeMove(node->move, node->parent ? node->parent->player : currBotColor);
            }
        }
        
        // 2. 扩展
        if (!node->untriedMoves.empty()) {
            int randomIndex = rand() % node->untriedMoves.size();
            Move move = node->untriedMoves[randomIndex];
            node = node->addChild(move, -state.currentPlayer);
            state.makeMove(move, node->parent->player);
        }
        
        // 3. 模拟
        double result = 0.5; // 默认平局
        
        // 简单模拟：随机走几步然后评估
        GameState simulateState = state;
        for (int step = 0; step < 10; step++) {
            vector<Move> currentMoves = generateAllMoves(simulateState.currentPlayer);
            if (currentMoves.empty()) {
                // 当前玩家无法移动，检查游戏是否结束
                if (generateAllMoves(-simulateState.currentPlayer).empty()) {
                    // 游戏结束
                    int winner = simulateState.getWinner();
                    if (winner == currBotColor) result = 1.0;
                    else if (winner == -currBotColor) result = 0.0;
                    else result = 0.5;
                    break;
                } else {
                    // 只是当前玩家无法移动，切换玩家继续
                    simulateState.currentPlayer = -simulateState.currentPlayer;
                    continue;
                }
            }
            
            // 随机选择一个移动
            Move randomMove = currentMoves[rand() % currentMoves.size()];
            simulateState.makeMove(randomMove, simulateState.currentPlayer);
        }
        
        // 如果模拟没有结束，使用简单评估
        if (result == 0.5) {
            // 计算棋子数量差
            int myPieces = 0, opponentPieces = 0;
            for (int i = 0; i < GRIDSIZE; i++) {
                for (int j = 0; j < GRIDSIZE; j++) {
                    if (simulateState.currentPlayer == currBotColor) {
                        if (gridInfo[i][j] == currBotColor) myPieces++;
                        else if (gridInfo[i][j] == -currBotColor) opponentPieces++;
                    }
                }
            }
            result = (myPieces > opponentPieces) ? 0.7 : 
                    (myPieces < opponentPieces) ? 0.3 : 0.5;
        }
        
        // 4. 回溯
        while (node != nullptr) {
            node->update(result);
            node = node->parent;
            result = 1.0 - result; // 从对手视角反转结果
        }
    }
    
    // 选择访问次数最多的移动
    MCTSNode* bestChild = nullptr;
    int mostVisits = -1;
    
    for (MCTSNode* child : root->children) {
        if (child->visits > mostVisits) {
            mostVisits = child->visits;
            bestChild = child;
        }
    }
    
    Move bestMove = (bestChild != nullptr) ? bestChild->move : allMoves[0];
    delete root;
    
    return bestMove;
}

int main()
{
    int x0, y0, x1, y1, x2, y2;

    // 初始化棋盘
    gridInfo[0][(GRIDSIZE - 1) / 3] = grid_black;
    gridInfo[(GRIDSIZE - 1) / 3][0] = grid_black;
    gridInfo[GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)][0] = grid_black;
    gridInfo[GRIDSIZE - 1][(GRIDSIZE - 1) / 3] = grid_black;

    gridInfo[0][GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)] = grid_white;
    gridInfo[(GRIDSIZE - 1) / 3][GRIDSIZE - 1] = grid_white;
    gridInfo[GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)][GRIDSIZE - 1] = grid_white;
    gridInfo[GRIDSIZE - 1][GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)] = grid_white;

    int turnID;
    cin >> turnID;

    currBotColor = grid_white;
    for (int i = 0; i < turnID; i++)
    {
        cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
        if (x0 == -1)
        {
            currBotColor = grid_black;
        }
        else
            ProcStep(x0, y0, x1, y1, x2, y2, -currBotColor, false);
        if (i < turnID - 1)
        {
            cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
            if (x0 >= 0)
                ProcStep(x0, y0, x1, y1, x2, y2, currBotColor, false);
        }
    }

    // 使用蒙特卡洛树搜索选择最佳移动
    Move bestMove = monteCarloTreeSearch(300); // 300次模拟，可以根据时间调整

    int startX, startY, resultX, resultY, obstacleX, obstacleY;
    if (bestMove.isValid()) {
        startX = bestMove.startX;
        startY = bestMove.startY;
        resultX = bestMove.endX;
        resultY = bestMove.endY;
        obstacleX = bestMove.obstacleX;
        obstacleY = bestMove.obstacleY;
    } else {
        startX = -1;
        startY = -1;
        resultX = -1;
        resultY = -1;
        obstacleX = -1;
        obstacleY = -1;
    }

    cout << startX << ' ' << startY << ' ' << resultX << ' ' << resultY << ' ' << obstacleX << ' ' << obstacleY << endl;
    return 0;
}