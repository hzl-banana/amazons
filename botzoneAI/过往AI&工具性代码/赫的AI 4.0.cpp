#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>
#include <set>
#include <sstream>
#include <fstream>
#include <thread>
#include <mutex>

using namespace std;
using namespace chrono;

// 全局变量定义
#define GRIDSIZE 8
#define OBSTACLE 2
#define grid_black 1
#define grid_white -1

mt19937_64 rng(chrono::steady_clock::now().time_since_epoch().count());

// 方向数组
int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};

// 棋盘状态
int gridInfo[GRIDSIZE][GRIDSIZE];

// 战略要地权重矩阵
double cellWeight[GRIDSIZE][GRIDSIZE] = {
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
    {1.0, 1.0, 2.0, 1.5, 1.5, 2.0, 1.0, 1.0},
    {1.0, 1.0, 1.5, 1.5, 1.5, 1.0, 1.0, 1.0},
    {1.0, 1.0, 1.5, 1.5, 1.5, 1.0, 1.0, 1.0},
    {1.0, 1.0, 2.0, 1.0, 1.0, 2.0, 1.0, 1.0},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
    {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0}
};

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
        return startX != -1 && startY != -1 && endX != -1 && endY != -1 && arrowX != -1 && arrowY != -1;
    }
};

// 位置结构体
struct Position {
    int x, y;
    Position(int x = 0, int y = 0) : x(x), y(y) {}
};

// 初始化Zobrist哈希表
unsigned long long zobristTable[GRIDSIZE][GRIDSIZE][5]; // 8x8棋盘，5种状态（空、黑、白、障碍、其他）
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
            else if (value == OBSTACLE) index = 3;
            else index = 4;
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
    else if (oldVal == OBSTACLE) oldIdx = 3;
    else oldIdx = 4;
    if (newVal == 0) newIdx = 0;
    else if (newVal == grid_black) newIdx = 1;
    else if (newVal == grid_white) newIdx = 2;
    else if (newVal == OBSTACLE) newIdx = 3;
    else newIdx = 4;
    return oldHash ^ zobristTable[x][y][oldIdx] ^ zobristTable[x][y][newIdx];
}

// 检查坐标是否在棋盘内
inline bool inMap(int x, int y) {
    return x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE;
}

// 路径验证函数 - 验证移动路径
bool validatePath(int x0, int y0, int x1, int y1) {
    if (!inMap(x0, y0) || !inMap(x1, y1)) return false;
    if (x0 == x1 && y0 == y1) return true;
    // 计算方向向量
    int dx_path = x1 - x0;
    int dy_path = y1 - y0;
    // 检查是否在皇后移动方向上
    if (dx_path != 0 && dy_path != 0 && abs(dx_path) != abs(dy_path)) return false;
    if (dx_path == 0 && dy_path == 0) return false;
    // 计算步长
    int step_x = (dx_path == 0) ? 0 : (dx_path > 0 ? 1 : -1);
    int step_y = (dy_path == 0) ? 0 : (dy_path > 0 ? 1 : -1);
    // 验证路径上的每个格子
    int x = x0 + step_x;
    int y = y0 + step_y;
    while (x != x1 || y != y1) {
        if (!inMap(x, y) || gridInfo[x][y] != 0) {
            return false;
        }
        x += step_x;
        y += step_y;
    }
    return true;
}

// 检查是否动的是自己的棋
bool isOwnPiece(int x, int y, int color) {
    return inMap(x, y) && gridInfo[x][y] == color;
}

// 检查箭是否射在自己的棋子上
bool arrowHitsOwnPiece(int arrowX, int arrowY, int color) {
    if (!inMap(arrowX, arrowY)) return false;
    return gridInfo[arrowX][arrowY] == color;
}

// 执行移动（检查模式或实际执行）
bool ProcStep(int x0, int y0, int x1, int y1, int x2, int y2, int color, bool check_only = false) {
    if (!inMap(x0, y0) || !inMap(x1, y1) || !inMap(x2, y2))
        return false;
    
    // 检查是否动的是自己的棋
    if (!isOwnPiece(x0, y0, color))
        return false;
    
    if (gridInfo[x1][y1] != 0)
        return false;
    if ((gridInfo[x2][y2] != 0) && !(x2 == x0 && y2 == y0))
        return false;
    
    // 检查箭是否射在自己的棋子上
    if (arrowHitsOwnPiece(x2, y2, color))
        return false;
    
    // 添加路径验证
    if (!validatePath(x0, y0, x1, y1)) return false;
    if (!validatePath(x1, y1, x2, y2)) return false;
    
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
                    // 验证移动路径
                    if (!validatePath(i, j, x1, y1)) break;
                    // 生成射箭点
                    for (int arrow_dir = 0; arrow_dir < 8; arrow_dir++) {
                        int arrow_steps = 1;
                        while (true) {
                            int x2 = x1 + dx[arrow_dir] * arrow_steps;
                            int y2 = y1 + dy[arrow_dir] * arrow_steps;
                            if (!inMap(x2, y2)) break;
                            if (gridInfo[x2][y2] != 0 && !(x2 == x1 && y2 == y1)) break;
                            // 检查箭是否射在自己的棋子上
                            if (arrowHitsOwnPiece(x2, y2, color)) break;
                            // 验证箭头路径
                            if (validatePath(x1, y1, x2, y2)) {
                                Move move(i, j, x1, y1, x2, y2);
                                moves.push_back(move);
                            }
                            arrow_steps++;
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
int countLiberty(int x, int y, int color) {
    int liberty = 0;
    for (int dir = 0; dir < 8; dir++) {
        int nx = x + dx[dir];
        int ny = y + dy[dir];
        if (inMap(nx, ny) && gridInfo[nx][ny] == 0) {
            // 验证路径是否完全空旷
            if (validatePath(x, y, nx, ny)) {
                liberty++;
            }
        }
    }
    return liberty;
}

// 检查棋子是否被困
bool isTrapped(int x, int y, int color) {
    // 检查所有方向是否有可行路径
    for (int dir = 0; dir < 8; dir++) {
        for (int steps = 1; steps <= 8; steps++) {
            int nx = x + dx[dir] * steps;
            int ny = y + dy[dir] * steps;
            if (!inMap(nx, ny)) continue;
            // 验证路径是否完全空旷
            if (validatePath(x, y, nx, ny)) {
                return false; // 存在可行路径
            }
        }
    }
    return true;
}

// 计算QueenMove距离的BFS函数
void calculateQueenMoveDistances(int color, vector<vector<int>>& distances) {
    queue<Position> q;
    // 初始化距离数组
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            distances[i][j] = (1 << 20); // 大数表示无穷
        }
    }
    // 将所有己方棋子位置的距离设为0并加入队列
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] == color) {
                distances[i][j] = 0;
                q.push({i, j});
            }
        }
    }
    // BFS计算QueenMove最小步数
    while (!q.empty()) {
        Position pos = q.front();
        q.pop();
        for (int dir = 0; dir < 8; dir++) {
            int x = pos.x + dx[dir];
            int y = pos.y + dy[dir];
            while (inMap(x, y) && gridInfo[x][y] == 0) {
                if (distances[x][y] > distances[pos.x][pos.y] + 1) {
                    distances[x][y] = distances[pos.x][pos.y] + 1;
                    q.push({x, y});
                }
                x += dx[dir];
                y += dy[dir];
            }
        }
    }
}

// 评估棋盘状态
double evaluateBoard(int color, int turn) {
    // 分阶段权重调整
    int emptyCount = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] == 0) emptyCount++;
        }
    }
    double territoryWeight, mobilityWeight, trapWeight;
    if (emptyCount > 50) { // 开局
        territoryWeight = 0.3;
        mobilityWeight = 0.5;
        trapWeight = 0.0;
    } else if (emptyCount < 20) { // 残局
        territoryWeight = 1.5;
        mobilityWeight = 0.2;
        trapWeight = 10.0;
    } else { // 中局
        territoryWeight = 1.0;
        mobilityWeight = 0.3;
        trapWeight = 2.0;
    }

    // 领地评估
    vector<vector<int>> myQueenDist(GRIDSIZE, vector<int>(GRIDSIZE, (1 << 20)));
    vector<vector<int>> opQueenDist(GRIDSIZE, vector<int>(GRIDSIZE, (1 << 20)));
    calculateQueenMoveDistances(color, myQueenDist);
    calculateQueenMoveDistances(-color, opQueenDist);
    int myTerritory = 0, opTerritory = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] != 0) continue;
            if (myQueenDist[i][j] < opQueenDist[i][j]) myTerritory++;
            else if (opQueenDist[i][j] < myQueenDist[i][j]) opTerritory++;
        }
    }
    double territoryScore = (myTerritory - opTerritory) * territoryWeight;

    // 棋子灵活性评估
    vector<Move> myMoves = generateAllMoves(color);
    vector<Move> opMoves = generateAllMoves(-color);
    double mobilityScore = ((int)myMoves.size() - (int)opMoves.size()) * mobilityWeight;

    // 棋子被困检测
    int myTrapped = 0, opTrapped = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] == color) {
                if (isTrapped(i, j, color)) myTrapped++;
            } else if (gridInfo[i][j] == -color) {
                if (isTrapped(i, j, -color)) opTrapped++;
            }
        }
    }
    double trapScore = (opTrapped - myTrapped) * trapWeight;

    // 战略要地权重
    double strategyScore = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] == color) strategyScore += cellWeight[i][j];
        }
    }

    // 综合评估得分
    return territoryScore + mobilityScore + trapScore + strategyScore;
}

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
    bool isMoveNode; // 标记节点类型
    MCTSNode(unsigned long long h = 0, int c = grid_black, MCTSNode* p = nullptr, int d = 0, bool m = true)
        : hash(h), visitCount(0), winScore(0), expanded(false), color(c), depth(d), parent(p), isMoveNode(m) {}
};

// MCTS算法实现
class MCTS {
private:
    unordered_map<unsigned long long, MCTSNode*> transpositionTable;
    steady_clock::time_point startTime;
    int TIME_LIMIT; // 动态时间限制
    int myColor;
    int opponentColor;
    int currentTurn;
    int totalRemainingTime; // 总剩余时间
    
    // 计算动态探索参数C
    double calculateDynamicC(int emptyCount) {
        double baseC = 1.4;
        double emptyRatio = emptyCount / 48.0; // 初始空地48个
        double c = baseC * sqrt(1 - emptyRatio);
        // 根据游戏阶段进一步调整
        if (emptyCount > 50) { // 开局阶段
            c *= 1.8;
        } else if (emptyCount < 20) { // 终局阶段
            c *= 0.5;
        }
        return c;
    }

public:
    MCTS(int color, int turn, int remainingTime = 900) : 
        myColor(color), opponentColor(-color), currentTurn(turn), totalRemainingTime(remainingTime) {
        // 动态计算时间限制
        initZobrist();
        TIME_LIMIT = min(900, (int)(totalRemainingTime * 0.8)); // 分配80%剩余时间
        TIME_LIMIT = min(TIME_LIMIT, 880); // 留20ms余量
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
            // 根据阶段动态调整探索参数C
            int emptyCount = 0;
            for (int i = 0; i < GRIDSIZE; i++) {
                for (int j = 0; j < GRIDSIZE; j++) {
                    if (gridInfo[i][j] == 0) emptyCount++;
                }
            }
            double C = calculateDynamicC(emptyCount);
            for (auto& move : node->children) {
                if (transpositionTable.find(move.hash) == transpositionTable.end()) {
                    // 创建新节点
                    MCTSNode* child = new MCTSNode(move.hash, node->color, node, node->depth + 1, !node->isMoveNode);
                    transpositionTable[move.hash] = child;
                    return child;
                }
                MCTSNode* child = transpositionTable[move.hash];
                if (child->visitCount == 0) {
                    return child;
                }
                // UCT公式
                double uct = (child->winScore / child->visitCount) +
                           C * sqrt(2.0 * log(node->visitCount) / child->visitCount);
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
        // 评估移动动作
        for (auto& move : moves) {
            // 计算移动后的哈希值
            unsigned long long oldHash = node->hash;
            unsigned long long tempHash = updateHash(oldHash, move.startX, move.startY, node->color, 0);
            move.hash = updateHash(tempHash, move.endX, move.endY, 0, node->color);
            
            // 验证移动路径
            if (!validatePath(move.startX, move.startY, move.endX, move.endY)) continue;
            if (!validatePath(move.endX, move.endY, move.arrowX, move.arrowY)) continue;
            
            // 检查箭是否射在自己的棋子上
            if (arrowHitsOwnPiece(move.arrowX, move.arrowY, node->color)) continue;
            
            // 评估移动动作
            move.score = evaluateBoard(node->color, currentTurn + node->depth);
        }
        // 选择最佳移动
        sort(moves.begin(), moves.end());
        // 根据游戏阶段限制分支因子
        size_t maxBranches;
        if (currentTurn + node->depth < 15) maxBranches = 15; // 开局
        else if (currentTurn + node->depth < 30) maxBranches = 10; // 中局
        else maxBranches = 5; // 终局（减少分支）
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
        int simColor = node->color;
        int simTurn = currentTurn + node->depth;
        
        // 随机模拟直到游戏结束或达到最大回合数
        int maxSimTurns = 100; // 限制模拟回合数
        while (simTurn < maxSimTurns) {
            vector<Move> possibleMoves = generateAllMoves(simColor);
            if (possibleMoves.empty()) {
                break; // 无法移动，游戏结束
            }
            // 随机选择一个移动
            Move randomMove = possibleMoves[rng() % possibleMoves.size()];
            // 执行移动
            ProcStep(randomMove.startX, randomMove.startY,
                     randomMove.endX, randomMove.endY,
                     randomMove.arrowX, randomMove.arrowY,
                     simColor, false);
            simColor = -simColor; // 切换颜色
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
        if (currentTurn == 0 && myColor == grid_black) {
            Move initialMove(0, 2, 2, 4, 3, 5); // 黑方固定开局
            if (ProcStep(initialMove.startX, initialMove.startY,
                         initialMove.endX, initialMove.endY,
                         initialMove.arrowX, initialMove.arrowY,
                         myColor, true)) {
                return initialMove;
            } else {
                // 如果固定开局不合法，生成所有合法移动并选择一个
                vector<Move> allMoves = generateAllMoves(myColor);
                if (!allMoves.empty()) {
                    return allMoves[0];
                }
            }
        }
        
        // 创建根节点
        unsigned long long rootHash = calculateHash();
        MCTSNode* root = new MCTSNode(rootHash, myColor, nullptr, 0, true);
        transpositionTable[rootHash] = root;
        
        // 首先生成并评估根节点的所有子节点
        vector<Move> allMoves = generateAllMoves(myColor);
        if (allMoves.empty()) {
            return Move(-1, -1, -1, -1, -1, -1); // 无路可走
        }
        
        // MCTS主循环
        int iterations = 0;
        while (true) {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - startTime);
            if (elapsed.count() >= TIME_LIMIT) {
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
        Move bestMove(-1, -1, -1, -1, -1, -1);
        int maxVisits = -1;
        for (auto& move : root->children) {
            // 验证移动路径
            if (!validatePath(move.startX, move.startY, move.endX, move.endY)) continue;
            if (!validatePath(move.endX, move.endY, move.arrowX, move.arrowY)) continue;
            // 检查箭是否射在自己的棋子上
            if (arrowHitsOwnPiece(move.arrowX, move.arrowY, myColor)) continue;
            // 选择访问次数最多的移动
            if (transpositionTable.find(move.hash) != transpositionTable.end()) {
                MCTSNode* child = transpositionTable[move.hash];
                if (child->visitCount > maxVisits) {
                    maxVisits = child->visitCount;
                    bestMove = move;
                }
            }
        }
        
        // 如果没有找到，选择评分最高的移动
        if (!bestMove.isValid() && !root->children.empty()) {
            bestMove = root->children[0];
        }
        
        // 最终验证最佳移动
        if (!ProcStep(bestMove.startX, bestMove.startY,
                bestMove.endX, bestMove.endY,
                bestMove.arrowX, bestMove.arrowY,
                myColor, true)) {
            // 如果移动非法，尝试其他合法移动
            for (auto& move : root->children) {
                if (ProcStep(move.startX, move.startY,
                        move.endX, move.endY,
                        move.arrowX, move.arrowY,
                        myColor, true)) {
                    bestMove = move;
                    break;
                }
            }
        }
        
        // 确保返回的移动是合法的，否则返回默认移动
        if (!bestMove.isValid() || 
            !ProcStep(bestMove.startX, bestMove.startY,
                      bestMove.endX, bestMove.endY,
                      bestMove.arrowX, bestMove.arrowY,
                      myColor, true)) {
            // 寻找一个合法的移动
            for (int i = 0; i < GRIDSIZE; i++) {
                for (int j = 0; j < GRIDSIZE; j++) {
                    if (gridInfo[i][j] == myColor) {
                        // 尝试在8个方向上移动一格
                        for (int dir = 0; dir < 8; dir++) {
                            int nx = i + dx[dir];
                            int ny = j + dy[dir];
                            if (inMap(nx, ny) && gridInfo[nx][ny] == 0) {
                                // 找到一个空位，尝试射箭
                                for (int arrow_dir = 0; arrow_dir < 8; arrow_dir++) {
                                    int arrow_x = nx + dx[arrow_dir];
                                    int arrow_y = ny + dy[arrow_dir];
                                    if (inMap(arrow_x, arrow_y) && 
                                        (gridInfo[arrow_x][arrow_y] == 0 || 
                                         (arrow_x == i && arrow_y == j)) &&
                                        !arrowHitsOwnPiece(arrow_x, arrow_y, myColor)) {
                                        Move tempMove(i, j, nx, ny, arrow_x, arrow_y);
                                        if (ProcStep(tempMove.startX, tempMove.startY,
                                                     tempMove.endX, tempMove.endY,
                                                     tempMove.arrowX, tempMove.arrowY,
                                                     myColor, true)) {
                                            bestMove = tempMove;
                                            goto found_valid_move;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            found_valid_move:;
        }
        
        // 如果仍然找不到合法移动，返回无效移动
        if (!bestMove.isValid() || 
            !ProcStep(bestMove.startX, bestMove.startY,
                      bestMove.endX, bestMove.endY,
                      bestMove.arrowX, bestMove.arrowY,
                      myColor, true)) {
            return Move(-1, -1, -1, -1, -1, -1);
        }
        
        // 清理内存
        for (auto& pair : transpositionTable) {
            delete pair.second;
        }
        transpositionTable.clear();
        
        return bestMove;
    }
};

int main() {
    // 读取输入
    string color;
    cin >> color;
    int timeLeft;
    cin >> timeLeft;
    
    int playerColor = (color == "black") ? grid_black : grid_white;
    
    // 初始化棋盘
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            gridInfo[i][j] = 0;
        }
    }
    
    // 设置初始棋子位置
    gridInfo[0][2] = grid_black;
    gridInfo[0][5] = grid_black;
    gridInfo[0][7] = grid_black;
    gridInfo[7][2] = grid_white;
    gridInfo[7][5] = grid_white;
    gridInfo[7][7] = grid_white;
    
    // 读取历史移动
    int historyCount;
    cin >> historyCount;
    for (int i = 0; i < historyCount; i++) {
        int x0, y0, x1, y1, x2, y2;
        cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
        ProcStep(x0, y0, x1, y1, x2, y2, (i % 2 == 0) ? grid_black : grid_white, false);
    }
    
    // 计算当前回合数
    int currentTurn = historyCount;
    
    // 创建MCTS实例并寻找最佳移动
    MCTS mcts(playerColor, currentTurn, timeLeft);
    Move bestMove = mcts.findBestMove();
    
    // 输出结果
    cout << bestMove.startX << " " << bestMove.startY << " " 
         << bestMove.endX << " " << bestMove.endY << " " 
         << bestMove.arrowX << " " << bestMove.arrowY << endl;
    
    return 0;
}



