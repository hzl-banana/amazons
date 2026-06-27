#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>

#define GRIDSIZE 8
#define OBSTACLE 2
#define judge_black 0
#define judge_white 1
#define grid_black 1
#define grid_white -1

using namespace std;
using namespace chrono;

// 全局变量
int currBotColor;
int gridInfo[GRIDSIZE][GRIDSIZE] = {0};
int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};

// Zobrist哈希表
unsigned long long zobristTable[GRIDSIZE][GRIDSIZE][5];
mt19937_64 rng(time(0));

// 移动结构体
struct Move {
    int startX, startY;
    int endX, endY;
    int arrowX, arrowY;
    double score;
    unsigned long long hash;
    
    Move(int sx = 0, int sy = 0, int ex = 0, int ey = 0, int ax = 0, int ay = 0) 
        : startX(sx), startY(sy), endX(ex), endY(ey), arrowX(ax), arrowY(ay), score(0), hash(0) {}
    
    bool operator<(const Move& other) const {
        return score > other.score;
    }
    
    bool isValid() const {
        return startX != -1;
    }
};

// MCTS节点
struct MCTSNode {
    unsigned long long hash;
    int visitCount;
    double winScore;
    vector<Move> children;
    bool expanded;
    int color;
    int depth;
    MCTSNode* parent;
    
    MCTSNode(unsigned long long h = 0, int c = grid_black, MCTSNode* p = nullptr, int d = 0)
        : hash(h), visitCount(0), winScore(0), expanded(false), color(c), depth(d), parent(p) {}
};

// 初始化Zobrist哈希表
void initZobrist() {
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            for (int k = 0; k < 5; k++) {
                zobristTable[i][j][k] = rng();
            }
        }
    }
}

// 计算当前棋盘哈希值
unsigned long long calculateHash() {
    unsigned long long hash = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            int value = gridInfo[i][j];
            int index;
            if (value == 0) index = 0;
            else if (value == grid_black) index = 1;
            else if (value == grid_white) index = 2;
            else index = 3;
            hash ^= zobristTable[i][j][index];
        }
    }
    return hash;
}

// 增量更新哈希值
unsigned long long updateHash(unsigned long long oldHash, int x, int y, int oldVal, int newVal) {
    int oldIdx, newIdx;
    
    if (oldVal == 0) oldIdx = 0;
    else if (oldVal == grid_black) oldIdx = 1;
    else if (oldVal == grid_white) oldIdx = 2;
    else oldIdx = 3;
    
    if (newVal == 0) newIdx = 0;
    else if (newVal == grid_black) newIdx = 1;
    else if (newVal == grid_white) newIdx = 2;
    else newIdx = 3;
    
    return oldHash ^ zobristTable[x][y][oldIdx] ^ zobristTable[x][y][newIdx];
}

// 检查坐标是否在棋盘内
inline bool inMap(int x, int y) {
    return x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE;
}

// 执行移动（检查模式或实际执行）
bool ProcStep(int x0, int y0, int x1, int y1, int x2, int y2, int color, bool check_only = false) {
    if (!inMap(x0, y0) || !inMap(x1, y1) || !inMap(x2, y2))
        return false;
    if (gridInfo[x0][y0] != color || gridInfo[x1][y1] != 0)
        return false;
    if ((gridInfo[x2][y2] != 0) && !(x2 == x0 && y2 == y0))
        return false;
    
    if (!check_only) {
        gridInfo[x0][y0] = 0;
        gridInfo[x1][y1] = color;
        gridInfo[x2][y2] = OBSTACLE;
    }
    return true;
}

// 生成所有合法移动
vector<Move> generateAllMoves(int color) {
    vector<Move> moves;
    
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] != color) continue;
            
            // 8个方向移动
            for (int dir = 0; dir < 8; dir++) {
                int steps = 1;
                while (true) {
                    int x1 = i + dx[dir] * steps;
                    int y1 = j + dy[dir] * steps;
                    
                    if (!inMap(x1, y1) || gridInfo[x1][y1] != 0)
                        break;
                    
                    // 8个方向射箭
                    for (int arrowDir = 0; arrowDir < 8; arrowDir++) {
                        int arrowSteps = 1;
                        while (true) {
                            int x2 = x1 + dx[arrowDir] * arrowSteps;
                            int y2 = y1 + dy[arrowDir] * arrowSteps;
                            
                            if (!inMap(x2, y2)) break;
                            if (gridInfo[x2][y2] != 0 && !(i == x2 && j == y2)) break;
                            
                            moves.push_back(Move(i, j, x1, y1, x2, y2));
                            arrowSteps++;
                        }
                    }
                    steps++;
                }
            }
        }
    }
    
    return moves;
}

// 计算棋子自由度
int countLiberty(int x, int y) {
    int liberty = 0;
    for (int dir = 0; dir < 8; dir++) {
        int nx = x + dx[dir];
        int ny = y + dy[dir];
        if (inMap(nx, ny) && gridInfo[nx][ny] == 0) {
            liberty++;
        }
    }
    return liberty;
}

// 评估单个移动
double evaluateMove(const Move& move, int color) {
    double score = 0;
    int opponentColor = -color;
    
    // 1. 移动后位置的自由度
    int endLiberty = countLiberty(move.endX, move.endY);
    score += endLiberty * 3.0;
    
    // 2. 阻碍对手的程度
    // 备份棋盘状态
    int backupGrid[GRIDSIZE][GRIDSIZE];
    for (int i = 0; i < GRIDSIZE; i++)
        for (int j = 0; j < GRIDSIZE; j++)
            backupGrid[i][j] = gridInfo[i][j];
    
    // 临时执行移动
    gridInfo[move.startX][move.startY] = 0;
    gridInfo[move.endX][move.endY] = color;
    gridInfo[move.arrowX][move.arrowY] = OBSTACLE;
    
    // 计算对手的移动数量
    int opponentMoves = (int)generateAllMoves(opponentColor).size();
    score += (30 - opponentMoves) * 2.0;
    
    // 计算己方移动数量
    int myMoves = (int)generateAllMoves(color).size();
    score += myMoves * 1.0;
    
    // 恢复棋盘状态
    for (int i = 0; i < GRIDSIZE; i++)
        for (int j = 0; j < GRIDSIZE; j++)
            gridInfo[i][j] = backupGrid[i][j];
    
    // 3. 中心化程度
    double centerX = 3.5, centerY = 3.5;
    double startDist = abs(move.startX - centerX) + abs(move.startY - centerY);
    double endDist = abs(move.endX - centerX) + abs(move.endY - centerY);
    score += (startDist - endDist) * 1.5;
    
    // 4. 控制角区（重要战略位置）
    vector<pair<int, int>> corners = {{0,0}, {0,7}, {7,0}, {7,7}};
    for (auto& corner : corners) {
        double dist = abs(move.endX - corner.first) + abs(move.endY - corner.second);
        if (dist < 3) score += 2.0;
    }
    
    return score;
}

// 开局阶段评估函数
double evaluateEarlyGame(int color, int turn) {
    double score = 0;
    int opponentColor = -color;
    
    // 棋子灵活性
    vector<Move> myMoves = generateAllMoves(color);
    vector<Move> oppMoves = generateAllMoves(opponentColor);
    score += ((int)myMoves.size() - (int)oppMoves.size()) * 0.5;
    
    // 棋子自由度
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] == color) {
                int liberty = countLiberty(i, j);
                score += liberty * 0.3;
                if (liberty <= 2) score -= 2.0; // 棋子被困惩罚
            }
        }
    }
    
    // 控制中心区域
    int centerControl = 0;
    for (int i = 2; i <= 5; i++) {
        for (int j = 2; j <= 5; j++) {
            if (gridInfo[i][j] == color) centerControl++;
        }
    }
    score += centerControl * 0.5;
    
    return score;
}

// 中局阶段评估函数
double evaluateMidGame(int color, int turn) {
    double score = 0;
    int opponentColor = -color;
    
    // 使用BFS计算领地控制
    int myTerritory = 0, oppTerritory = 0;
    
    // 为每个空位计算距离双方棋子的最小QueenMove步数
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] != 0) continue;
            
            // 计算到己方棋子的最小距离
            int myDist = GRIDSIZE * GRIDSIZE;
            int oppDist = GRIDSIZE * GRIDSIZE;
            
            for (int x = 0; x < GRIDSIZE; x++) {
                for (int y = 0; y < GRIDSIZE; y++) {
                    if (gridInfo[x][y] == color) {
                        if (x == i || y == j || abs(x - i) == abs(y - j)) {
                            int dist = max(abs(x - i), abs(y - j));
                            if (dist < myDist) myDist = dist;
                        }
                    } else if (gridInfo[x][y] == opponentColor) {
                        if (x == i || y == j || abs(x - i) == abs(y - j)) {
                            int dist = max(abs(x - i), abs(y - j));
                            if (dist < oppDist) oppDist = dist;
                        }
                    }
                }
            }
            
            if (myDist < oppDist) myTerritory++;
            else if (oppDist < myDist) oppTerritory++;
        }
    }
    
    score += (myTerritory - oppTerritory) * 0.8;
    
    // 棋子灵活性
    int myMobility = (int)generateAllMoves(color).size();
    int oppMobility = (int)generateAllMoves(opponentColor).size();
    score += (myMobility - oppMobility) * 0.3;
    
    return score;
}

// 残局阶段评估函数
double evaluateEndGame(int color, int turn) {
    double score = 0;
    int opponentColor = -color;
    
    // 棋子是否被困
    int myTrapped = 0, oppTrapped = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] == color) {
                int liberty = countLiberty(i, j);
                if (liberty <= 1) myTrapped++;
            } else if (gridInfo[i][j] == opponentColor) {
                int liberty = countLiberty(i, j);
                if (liberty <= 1) oppTrapped++;
            }
        }
    }
    
    score -= myTrapped * 5.0;
    score += oppTrapped * 5.0;
    
    // 领地控制（更精确）
    int myReachable = 0, oppReachable = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] != 0) continue;
            
            bool myCanReach = false, oppCanReach = false;
            
            // 检查己方是否能到达
            for (int x = 0; x < GRIDSIZE && !myCanReach; x++) {
                for (int y = 0; y < GRIDSIZE && !myCanReach; y++) {
                    if (gridInfo[x][y] == color) {
                        if (x == i || y == j || abs(x - i) == abs(y - j)) {
                            // 检查路径是否畅通
                            bool blocked = false;
                            int stepX = (i > x) ? 1 : (i < x) ? -1 : 0;
                            int stepY = (j > y) ? 1 : (j < y) ? -1 : 0;
                            
                            int curX = x + stepX;
                            int curY = y + stepY;
                            while (curX != i || curY != j) {
                                if (gridInfo[curX][curY] != 0) {
                                    blocked = true;
                                    break;
                                }
                                curX += stepX;
                                curY += stepY;
                            }
                            
                            if (!blocked) {
                                myCanReach = true;
                            }
                        }
                    }
                }
            }
            
            // 检查对手是否能到达
            for (int x = 0; x < GRIDSIZE && !oppCanReach; x++) {
                for (int y = 0; y < GRIDSIZE && !oppCanReach; y++) {
                    if (gridInfo[x][y] == opponentColor) {
                        if (x == i || y == j || abs(x - i) == abs(y - j)) {
                            bool blocked = false;
                            int stepX = (i > x) ? 1 : (i < x) ? -1 : 0;
                            int stepY = (j > y) ? 1 : (j < y) ? -1 : 0;
                            
                            int curX = x + stepX;
                            int curY = y + stepY;
                            while (curX != i || curY != j) {
                                if (gridInfo[curX][curY] != 0) {
                                    blocked = true;
                                    break;
                                }
                                curX += stepX;
                                curY += stepY;
                            }
                            
                            if (!blocked) {
                                oppCanReach = true;
                            }
                        }
                    }
                }
            }
            
            if (myCanReach && !oppCanReach) myReachable++;
            else if (!myCanReach && oppCanReach) oppReachable++;
        }
    }
    
    score += (myReachable - oppReachable) * 1.5;
    
    return score;
}

// 综合评估函数
double evaluateBoard(int color, int turn) {
    if (turn < 15) {
        return evaluateEarlyGame(color, turn);
    } else if (turn < 40) {
        return evaluateMidGame(color, turn);
    } else {
        return evaluateEndGame(color, turn);
    }
}

// MCTS算法实现
class MCTS {
private:
    unordered_map<unsigned long long, MCTSNode*> transpositionTable;
    steady_clock::time_point startTime;
    const int TIME_LIMIT = 880; // 留20ms余量
    int myColor;
    int opponentColor;
    int currentTurn;
    
public:
    MCTS(int color, int turn) : myColor(color), opponentColor(-color), currentTurn(turn) {
        initZobrist();
    }
    
    ~MCTS() {
        for (auto& pair : transpositionTable) {
            delete pair.second;
        }
    }
    
    // 选择节点（使用UCT算法）
    MCTSNode* selectNode(MCTSNode* node) {
        while (node->expanded && !node->children.empty()) {
            MCTSNode* bestChild = nullptr;
            double bestUCT = -1e9;
            
            for (auto& move : node->children) {
                if (transpositionTable.find(move.hash) == transpositionTable.end()) {
                    // 创建新节点
                    MCTSNode* child = new MCTSNode(move.hash, -node->color, node, node->depth + 1);
                    transpositionTable[move.hash] = child;
                    return child;
                }
                
                MCTSNode* child = transpositionTable[move.hash];
                if (child->visitCount == 0) {
                    return child;
                }
                
                // UCT公式
                double uct = (child->winScore / child->visitCount) +
                           sqrt(2.0 * log(node->visitCount) / child->visitCount);
                
                // 根据游戏阶段调整探索参数
                double explorationBias = 1.4;
                if (currentTurn + node->depth > 40) {
                    explorationBias = 0.8; // 残局减少探索
                }
                
                uct *= explorationBias;
                
                if (uct > bestUCT) {
                    bestUCT = uct;
                    bestChild = child;
                }
            }
            
            if (bestChild) {
                node = bestChild;
            } else {
                break;
            }
        }
        return node;
    }
    
    // 扩展节点
    void expandNode(MCTSNode* node) {
        if (node->expanded) return;
        
        // 生成所有合法移动
        vector<Move> moves = generateAllMoves(node->color);
        
        if (moves.empty()) {
            node->expanded = true;
            return;
        }
        
        // 为每个移动评分
        for (auto& move : moves) {
            move.score = evaluateMove(move, node->color);
            
            // 计算移动后的哈希值
            unsigned long long oldHash = node->hash;
            move.hash = updateHash(oldHash, move.startX, move.startY, node->color, 0);
            move.hash = updateHash(move.hash, move.endX, move.endY, 0, node->color);
            move.hash = updateHash(move.hash, move.arrowX, move.arrowY, 0, OBSTACLE);
        }
        
        // 排序并选择最好的移动进行扩展
        sort(moves.begin(), moves.end());
        
        // 根据游戏阶段限制分支因子
        size_t maxBranches;
        if (currentTurn + node->depth < 15) maxBranches = 15; // 开局
        else if (currentTurn + node->depth < 40) maxBranches = 10; // 中局
        else maxBranches = 8; // 残局
        
        if (moves.size() > maxBranches) {
            moves.resize(maxBranches);
        }
        
        node->children = moves;
        node->expanded = true;
    }
    
    // 模拟对局
    double simulate(MCTSNode* node) {
        // 备份当前棋盘
        int backupGrid[GRIDSIZE][GRIDSIZE];
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                backupGrid[i][j] = gridInfo[i][j];
            }
        }
        
        // 设置棋盘状态（通过执行移动序列）
        // 这里简化处理，直接从当前状态开始模拟
        
        int simColor = node->color;
        int simTurn = currentTurn + node->depth;
        const int MAX_SIM_DEPTH = 10;
        
        for (int depth = 0; depth < MAX_SIM_DEPTH; depth++) {
            vector<Move> moves = generateAllMoves(simColor);
            if (moves.empty()) {
                // 当前玩家无路可走，对手获胜
                for (int i = 0; i < GRIDSIZE; i++) {
                    for (int j = 0; j < GRIDSIZE; j++) {
                        gridInfo[i][j] = backupGrid[i][j];
                    }
                }
                return (simColor == myColor) ? 0.0 : 1.0;
            }
            
            // 选择移动（基于简单启发式）
            Move bestMove = moves[0];
            double bestScore = -1e9;
            
            for (auto& move : moves) {
                double score = evaluateMove(move, simColor);
                if (score > bestScore) {
                    bestScore = score;
                    bestMove = move;
                }
            }
            
            // 执行移动
            ProcStep(bestMove.startX, bestMove.startY,
                    bestMove.endX, bestMove.endY,
                    bestMove.arrowX, bestMove.arrowY,
                    simColor, false);
            
            simColor = -simColor;
            simTurn++;
        }
        
        // 评估最终局面
        double result = evaluateBoard(myColor, simTurn);
        result = 1.0 / (1.0 + exp(-result / 10.0)); // Sigmoid归一化
        
        // 恢复棋盘
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                gridInfo[i][j] = backupGrid[i][j];
            }
        }
        
        return result;
    }
    
    // 回溯更新
    void backpropagate(MCTSNode* node, double result) {
        while (node != nullptr) {
            node->visitCount++;
            node->winScore += result;
            
            // 切换视角
            result = 1.0 - result;
            node = node->parent;
        }
    }
    
    // 查找最佳移动
    Move findBestMove() {
        startTime = steady_clock::now();
        
        // 如果是第一回合且是黑棋，使用固定开局
        if (currentTurn == 1 && myColor == grid_black) {
            return Move(0, 2, 2, 4, 3, 5); // 黑方固定开局
        }
        
        // 创建根节点
        unsigned long long rootHash = calculateHash();
        MCTSNode* root = new MCTSNode(rootHash, myColor, nullptr, 0);
        transpositionTable[rootHash] = root;
        
        // 首先生成并评估根节点的所有子节点
        vector<Move> allMoves = generateAllMoves(myColor);
        if (allMoves.empty()) {
            return Move(-1, -1, -1, -1, -1, -1); // 无路可走
        }
        
        // 为每个移动评分
        for (auto& move : allMoves) {
            move.score = evaluateMove(move, myColor);
        }
        
        // 选择几个最有希望的移动作为初始扩展
        sort(allMoves.begin(), allMoves.end());
        size_t initialMoves = min((size_t)5, allMoves.size());
        for (size_t i = 0; i < initialMoves; i++) {
            Move move = allMoves[i];
            move.hash = updateHash(rootHash, move.startX, move.startY, myColor, 0);
            move.hash = updateHash(move.hash, move.endX, move.endY, 0, myColor);
            move.hash = updateHash(move.hash, move.arrowX, move.arrowY, 0, OBSTACLE);
            root->children.push_back(move);
        }
        root->expanded = true;
        
        // MCTS主循环
        int iterations = 0;
        while (true) {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - startTime);
            if (elapsed.count() >= TIME_LIMIT || iterations > 5000) {
                break;
            }
            
            // 选择
            MCTSNode* node = selectNode(root);
            
            // 扩展
            if (!node->expanded) {
                expandNode(node);
            }
            
            // 模拟
            double result = simulate(node);
            
            // 回溯
            backpropagate(node, result);
            
            iterations++;
        }
        
        // 选择最佳移动（基于访问次数）
        Move bestMove;
        int maxVisits = -1;
        
        for (auto& move : root->children) {
            if (transpositionTable.find(move.hash) != transpositionTable.end()) {
                MCTSNode* child = transpositionTable[move.hash];
                if (child->visitCount > maxVisits) {
                    maxVisits = child->visitCount;
                    bestMove = move;
                }
            }
        }
        
        // 如果没有找到，选择评分最高的移动
        if (!bestMove.isValid() && !allMoves.empty()) {
            bestMove = allMoves[0];
        }
        
        return bestMove;
    }
};

int main() {
    // 初始化棋盘
    gridInfo[0][(GRIDSIZE - 1) / 3] = grid_black;
    gridInfo[(GRIDSIZE - 1) / 3][0] = grid_black;
    gridInfo[GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)][0] = grid_black;
    gridInfo[GRIDSIZE - 1][(GRIDSIZE - 1) / 3] = grid_black;
    
    gridInfo[0][GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)] = grid_white;
    gridInfo[(GRIDSIZE - 1) / 3][GRIDSIZE - 1] = grid_white;
    gridInfo[GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)][GRIDSIZE - 1] = grid_white;
    gridInfo[GRIDSIZE - 1][GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)] = grid_white;
    
    // 读取当前回合数
    int turnID;
    cin >> turnID;
    
    // 读取历史移动
    currBotColor = grid_white;
    for (int i = 0; i < turnID; i++) {
        int x0, y0, x1, y1, x2, y2;
        cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
        
        if (x0 == -1) {
            currBotColor = grid_black;
        } else {
            ProcStep(x0, y0, x1, y1, x2, y2, -currBotColor, false);
        }
        
        if (i < turnID - 1) {
            cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
            if (x0 >= 0) {
                ProcStep(x0, y0, x1, y1, x2, y2, currBotColor, false);
            }
        }
    }
    
    // 使用MCTS算法计算最佳移动
    MCTS mcts(currBotColor, turnID);
    Move bestMove = mcts.findBestMove();
    
    // 输出结果
    cout << bestMove.startX << ' ' << bestMove.startY << ' '
         << bestMove.endX << ' ' << bestMove.endY << ' '
         << bestMove.arrowX << ' ' << bestMove.arrowY << endl;
    
    return 0;
}