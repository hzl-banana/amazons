#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>
#include <set>

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

// 位置结构体
struct Position {
    int x, y;
    Position(int x = 0, int y = 0) : x(x), y(y) {}
};

// 区域类型枚举
enum AreaType {
    NEUTRAL,
    MY_TERRITORY,
    OP_TERRITORY,
    ACTIVE
};

// 区域结构体
struct Area {
    AreaType type;
    Position position;
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

// 路径验证函数 - 验证移动路径
bool validatePath(int x0, int y0, int x1, int y1) {
    if (!inMap(x0, y0) || !inMap(x1, y1)) return false;
    if (x0 == x1 && y0 == y1) return true;
    
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

// 执行移动（检查模式或实际执行）- 已修复路径验证
bool ProcStep(int x0, int y0, int x1, int y1, int x2, int y2, int color, bool check_only = false) {
    if (!inMap(x0, y0) || !inMap(x1, y1) || !inMap(x2, y2))
        return false;
    if (gridInfo[x0][y0] != color || gridInfo[x1][y1] != 0)
        return false;
    if ((gridInfo[x2][y2] != 0) && !(x2 == x0 && y2 == y0))
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

// 生成所有合法移动 - 已修复路径验证
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
                    
                    // 8个方向射箭
                    for (int arrowDir = 0; arrowDir < 8; arrowDir++) {
                        int arrowSteps = 1;
                        while (true) {
                            int x2 = x1 + dx[arrowDir] * arrowSteps;
                            int y2 = y1 + dy[arrowDir] * arrowSteps;
                            
                            if (!inMap(x2, y2)) break;
                            if (gridInfo[x2][y2] != 0 && !(i == x2 && j == y2)) break;
                            
                            // 验证箭头路径
                            if (validatePath(x1, y1, x2, y2)) {
                                moves.push_back(Move(i, j, x1, y1, x2, y2));
                            }
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

// 棋盘分区函数
void partitionBoard(int color, vector<Area>& areas) {
    vector<vector<int>> myDistances(GRIDSIZE, vector<int>(GRIDSIZE, (1 << 20)));
    vector<vector<int>> opDistances(GRIDSIZE, vector<int>(GRIDSIZE, (1 << 20)));
    
    calculateQueenMoveDistances(color, myDistances);
    calculateQueenMoveDistances(-color, opDistances);
    
    areas.clear();
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] != 0) continue;
            
            Area area;
            area.position = {i, j};
            
            if (myDistances[i][j] < opDistances[i][j]) {
                area.type = MY_TERRITORY;
            } else if (opDistances[i][j] < myDistances[i][j]) {
                area.type = OP_TERRITORY;
            } else {
                area.type = ACTIVE;
            }
            
            areas.push_back(area);
        }
    }
}

// 评估单个移动
double evaluateMove(const Move& move, int color, int turn) {
    double score = 0;
    int opponentColor = -color;
    
    // 备份棋盘状态
    int backupGrid[GRIDSIZE][GRIDSIZE];
    for (int i = 0; i < GRIDSIZE; i++)
        for (int j = 0; j < GRIDSIZE; j++)
            backupGrid[i][j] = gridInfo[i][j];
    
    // 临时执行移动
    gridInfo[move.startX][move.startY] = 0;
    gridInfo[move.endX][move.endY] = color;
    gridInfo[move.arrowX][move.arrowY] = OBSTACLE;
    
    // 1. 移动后位置的自由度
    int endLiberty = countLiberty(move.endX, move.endY);
    score += endLiberty * 3.0;
    
    // 2. 阻碍对手的程度
    int opponentMoves = (int)generateAllMoves(opponentColor).size();
    score += (30 - opponentMoves) * 2.0;
    
    // 计算对手棋子的总自由度变化
    int opLibertyTotal = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] == opponentColor) {
                opLibertyTotal += countLiberty(i, j);
            }
        }
    }
    
    // 恢复棋盘状态
    for (int i = 0; i < GRIDSIZE; i++)
        for (int j = 0; j < GRIDSIZE; j++)
            gridInfo[i][j] = backupGrid[i][j];
    
    // 计算对手棋子自由度变化
    int opLibertyBefore = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] == opponentColor) {
                opLibertyBefore += countLiberty(i, j);
            }
        }
    }
    
    score += (opLibertyBefore - opLibertyTotal) * 1.0;
    
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
    
    // 5. 障碍物有效性评估（终局阶段更重视）
    if (turn >= 30) {
        vector<Area> areas;
        partitionBoard(color, areas);
        
        // 计算障碍物对分割对手领地的影响
        int territoryReduction = 0;
        for (auto& area : areas) {
            if (area.type == OP_TERRITORY) {
                // 模拟放置障碍后检查该区域是否仍然可达对手
                if (gridInfo[area.position.x][area.position.y] == 0) {
                    // 简化计算：如果障碍物在对手领地附近，可能产生分割效果
                    int distToObstacle = abs(area.position.x - move.arrowX) + abs(area.position.y - move.arrowY);
                    if (distToObstacle <= 2) territoryReduction++;
                }
            }
        }
        score += territoryReduction * 1.5;
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
    vector<vector<int>> myDistances(GRIDSIZE, vector<int>(GRIDSIZE, (1 << 20)));
    vector<vector<int>> opDistances(GRIDSIZE, vector<int>(GRIDSIZE, (1 << 20)));
    
    calculateQueenMoveDistances(color, myDistances);
    calculateQueenMoveDistances(opponentColor, opDistances);
    
    int myTerritory = 0, opTerritory = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] != 0) continue;
            if (myDistances[i][j] < opDistances[i][j]) myTerritory++;
            else if (opDistances[i][j] < myDistances[i][j]) opTerritory++;
        }
    }
    
    score += (myTerritory - opTerritory) * 1.5; // 提高领地权重
    
    // 棋子灵活性
    int myMobility = (int)generateAllMoves(color).size();
    int opMobility = (int)generateAllMoves(opponentColor).size();
    score += (myMobility - opMobility) * 0.3;
    
    return score;
}

// 残局阶段评估函数
double evaluateEndGame(int color, int turn) {
    double score = 0;
    int opponentColor = -color;
    
    // 棋子是否被困
    int myTrapped = 0, opTrapped = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] == color) {
                int liberty = countLiberty(i, j);
                if (liberty <= 1) myTrapped++;
            } else if (gridInfo[i][j] == opponentColor) {
                int liberty = countLiberty(i, j);
                if (liberty <= 1) opTrapped++;
            }
        }
    }
    
    score -= myTrapped * 10.0; // 提高被困惩罚
    score += opTrapped * 10.0;
    
    // 领地控制（更精确）
    vector<vector<int>> myDistances(GRIDSIZE, vector<int>(GRIDSIZE, (1 << 20)));
    vector<vector<int>> opDistances(GRIDSIZE, vector<int>(GRIDSIZE, (1 << 20)));
    
    calculateQueenMoveDistances(color, myDistances);
    calculateQueenMoveDistances(opponentColor, opDistances);
    
    int myTerritory = 0, opTerritory = 0;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] != 0) continue;
            if (myDistances[i][j] < opDistances[i][j]) myTerritory++;
            else if (opDistances[i][j] < myDistances[i][j]) opTerritory++;
        }
    }
    
    score += (myTerritory - opTerritory) * 2.0; // 终局阶段提高领地权重
    
    return score;
}

// 综合评估函数
double evaluateBoard(int color, int turn) {
    if (turn < 15) {
        return evaluateEarlyGame(color, turn);
    } else if (turn < 30) { // 调整终局阶段阈值
        return evaluateMidGame(color, turn);
    } else {
        return evaluateEndGame(color, turn);
    }
}

// 神经网络缓存结构
struct NeuralNetworkCache {
    vector<vector<double>> cache;
    vector<vector<double>> weights;
    int buckets;
    
    NeuralNetworkCache(int bucketCount = 3) : buckets(bucketCount) {
        // 初始化缓存和权重
        cache.resize(buckets);
        weights.resize(buckets);
        for (int b = 0; b < buckets; b++) {
            cache[b].resize(65, 0.0); // 64棋盘位置 + 1阶段信息
            weights[b].resize(65, 0.0);
        }
    }
    
    // 生成缓存 - 预计算权重差值
    void generateCache() {
        for (int b = 0; b < buckets; b++) {
            // 简化：随机初始化权重（实际应用中应加载预训练权重）
            for (int i = 0; i < 64; i++) {
                weights[b][i] = (double)rng() / (double)rng.max();
            }
            weights[b][64] = (double)rng() / (double)rng.max(); // 阶段信息权重
        }
    }
    
    // 根据棋盘状态更新缓存
    double cacheScoresDeep(const vector<vector<int>>& board, int turn) {
        int bucket = min(turn / 15, buckets - 1); // 根据回合数选择桶
        double score = 0.0;
        
        // 将棋盘状态转换为特征向量
        vector<int> features(65, 0);
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                int idx = i * GRIDSIZE + j;
                if (board[i][j] == grid_black) features[idx] = 1;
                else if (board[i][j] == grid_white) features[idx] = -1;
                else features[idx] = 0;
            }
        }
        features[64] = min(turn / 10, 6); // 阶段信息，归一化到0-6
        
        // 使用缓存计算得分
        for (int i = 0; i < 65; i++) {
            score += weights[bucket][i] * features[i];
        }
        
        return score;
    }
    
    // 重载函数，接受二维数组
    double cacheScoresDeep(int board[GRIDSIZE][GRIDSIZE], int turn) {
        vector<vector<int>> boardVec(GRIDSIZE, vector<int>(GRIDSIZE));
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                boardVec[i][j] = board[i][j];
            }
        }
        return cacheScoresDeep(boardVec, turn);
    }
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
    NeuralNetworkCache nnCache; // 神经网络缓存
    
public:
    MCTS(int color, int turn, int remainingTime = 900) : 
        myColor(color), opponentColor(-color), currentTurn(turn), totalRemainingTime(remainingTime), nnCache(3) {
        // 动态计算时间限制
        if (currentTurn < 15) { // 开局阶段
            TIME_LIMIT = min(900, (int)(totalRemainingTime * 0.2)); // 分配20%剩余时间
        } else if (currentTurn < 30) { // 中盘阶段
            TIME_LIMIT = min(900, (int)(totalRemainingTime * 0.6)); // 分配60%剩余时间
        } else { // 终局阶段
            TIME_LIMIT = min(900, (int)(totalRemainingTime * 0.8)); // 分配80%剩余时间
        }
        TIME_LIMIT = min(TIME_LIMIT, 880); // 留20ms余量
        
        initZobrist();
        nnCache.generateCache(); // 初始化神经网络缓存
    }
    
    ~MCTS() {
        for (auto& pair : transpositionTable) {
            delete pair.second;
        }
    }
    
    // 使用神经网络评估棋步
    double evaluateWithNN(const Move& move, int color, int turn) {
        // 备份棋盘状态
        int backupGrid[GRIDSIZE][GRIDSIZE];
        for (int i = 0; i < GRIDSIZE; i++)
            for (int j = 0; j < GRIDSIZE; j++)
                backupGrid[i][j] = gridInfo[i][j];
        
        // 临时执行移动
        gridInfo[move.startX][move.startY] = 0;
        gridInfo[move.endX][move.endY] = color;
        gridInfo[move.arrowX][move.arrowY] = OBSTACLE;
        
        // 使用神经网络评估
        double nnScore = nnCache.cacheScoresDeep(gridInfo, turn);
        
        // 恢复棋盘状态
        for (int i = 0; i < GRIDSIZE; i++)
            for (int j = 0; j < GRIDSIZE; j++)
                gridInfo[i][j] = backupGrid[i][j];
        
        return nnScore;
    }
    
    // 选择节点（使用UCT算法，结合神经网络评估）
    MCTSNode* selectNode(MCTSNode* node) {
        while (node->expanded && !node->children.empty()) {
            MCTSNode* bestChild = nullptr;
            double bestUCT = -1e9;
            
            // 根据阶段动态调整探索参数C
            double C = 1.4;
            if (currentTurn + node->depth >= 30) { // 终局阶段
                C = 0.5; // 减少探索
            } else if (currentTurn + node->depth < 15) { // 开局阶段
                C = 1.8; // 增加探索
            }
            
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
                
                // 使用神经网络评估作为启发式值
                double nnHeuristic = evaluateWithNN(move, node->color, currentTurn + node->depth);
                
                // UCT公式，结合神经网络启发式
                double uct = (child->winScore / child->visitCount) +
                           nnHeuristic * 0.1 + // 神经网络启发式
                           sqrt(2.0 * log(node->visitCount) / child->visitCount) * C;
                
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
        
        // 为每个移动评分（结合神经网络评估）
        for (auto& move : moves) {
            // 额外验证移动的合法性
            if (!validatePath(move.startX, move.startY, move.endX, move.endY) || 
                !validatePath(move.endX, move.endY, move.arrowX, move.arrowY)) {
                continue; // 跳过非法移动
            }
            
            move.score = evaluateMove(move, node->color, currentTurn + node->depth);
            // 增强神经网络评估
            move.score += evaluateWithNN(move, node->color, currentTurn + node->depth) * 0.2;
            
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
        const int MAX_SIM_DEPTH = 10;
        
        // 终局阶段使用启发式模拟
        if (simTurn >= 30) {
            for (int depth = 0; depth < MAX_SIM_DEPTH; depth++) {
                vector<Move> moves = generateAllMoves(simColor);
                if (moves.empty()) break;
                
                // 选择能阻碍对手的移动
                Move bestMove = moves[0];
                double bestScore = -1e9;
                for (auto& move : moves) {
                    // 额外验证移动的合法性
                    if (!validatePath(move.startX, move.startY, move.endX, move.endY) || 
                        !validatePath(move.endX, move.endY, move.arrowX, move.arrowY)) {
                        continue; // 跳过非法移动
                    }
                    
                    double score = evaluateMove(move, simColor, simTurn + depth);
                    // 在终局阶段更重视封堵对手
                    int opLibertyBefore = 0;
                    for (int i = 0; i < GRIDSIZE; i++) {
                        for (int j = 0; j < GRIDSIZE; j++) {
                            if (gridInfo[i][j] == -simColor) {
                                opLibertyBefore += countLiberty(i, j);
                            }
                        }
                    }
                    
                    // 临时执行移动
                    int tempBackup[GRIDSIZE][GRIDSIZE];
                    for (int i = 0; i < GRIDSIZE; i++)
                        for (int j = 0; j < GRIDSIZE; j++)
                            tempBackup[i][j] = gridInfo[i][j];
                    
                    ProcStep(move.startX, move.startY, move.endX, move.endY, move.arrowX, move.arrowY, simColor, false);
                    
                    int opLibertyAfter = 0;
                    for (int i = 0; i < GRIDSIZE; i++) {
                        for (int j = 0; j < GRIDSIZE; j++) {
                            if (gridInfo[i][j] == -simColor) {
                                opLibertyAfter += countLiberty(i, j);
                            }
                        }
                    }
                    
                    // 恢复棋盘
                    for (int i = 0; i < GRIDSIZE; i++)
                        for (int j = 0; j < GRIDSIZE; j++)
                            gridInfo[i][j] = tempBackup[i][j];
                    
                    score += (opLibertyBefore - opLibertyAfter) * 1.5; // 终局阶段重视封堵
                    if (score > bestScore) {
                        bestScore = score;
                        bestMove = move;
                    }
                }
                
                // 执行移动（验证合法性）
                if (ProcStep(bestMove.startX, bestMove.startY,
                        bestMove.endX, bestMove.endY,
                        bestMove.arrowX, bestMove.arrowY,
                        simColor, false)) {
                    // 执行成功
                } else {
                    // 如果执行失败，尝试其他移动
                    for (auto& move : moves) {
                        if (ProcStep(move.startX, move.startY,
                                move.endX, move.endY,
                                move.arrowX, move.arrowY,
                                simColor, false)) {
                            bestMove = move;
                            break;
                        }
                    }
                }
                
                simColor = -simColor;
                simTurn++;
            }
        } else { // 其他阶段使用随机模拟
            for (int depth = 0; depth < MAX_SIM_DEPTH; depth++) {
                vector<Move> moves = generateAllMoves(simColor);
                if (moves.empty()) break;
                
                // 选择移动（基于简单启发式和神经网络评估）
                Move bestMove = moves[0];
                double bestScore = -1e9;
                
                for (auto& move : moves) {
                    // 额外验证移动的合法性
                    if (!validatePath(move.startX, move.startY, move.endX, move.endY) || 
                        !validatePath(move.endX, move.endY, move.arrowX, move.arrowY)) {
                        continue; // 跳过非法移动
                    }
                    
                    double score = evaluateMove(move, simColor, simTurn + depth);
                    // 结合神经网络评估
                    score += evaluateWithNN(move, simColor, simTurn + depth) * 0.1;
                    if (score > bestScore) {
                        bestScore = score;
                        bestMove = move;
                    }
                }
                
                // 执行移动（验证合法性）
                if (ProcStep(bestMove.startX, bestMove.startY,
                        bestMove.endX, bestMove.endY,
                        bestMove.arrowX, bestMove.arrowY,
                        simColor, false)) {
                    // 执行成功
                } else {
                    // 如果执行失败，尝试其他移动
                    for (auto& move : moves) {
                        if (ProcStep(move.startX, move.startY,
                                move.endX, move.endY,
                                move.arrowX, move.arrowY,
                                simColor, false)) {
                            bestMove = move;
                            break;
                        }
                    }
                }
                
                simColor = -simColor;
                simTurn++;
            }
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
        } else if (currentTurn == 2 && myColor == grid_black) {
            return Move(2, 0, 4, 2, 5, 3); // 黑方第二步
        } else if (currentTurn == 3 && myColor == grid_black) {
            return Move(7, 0, 5, 2, 6, 3); // 黑方第三步
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
        
        // 为每个移动评分（结合神经网络评估）
        for (auto& move : allMoves) {
            // 额外验证移动的合法性
            if (!validatePath(move.startX, move.startY, move.endX, move.endY) || 
                !validatePath(move.endX, move.endY, move.arrowX, move.arrowY)) {
                continue; // 跳过非法移动
            }
            
            move.score = evaluateMove(move, myColor, currentTurn);
            // 增强神经网络评估
            move.score += evaluateWithNN(move, myColor, currentTurn) * 0.2;
        }
        
        // 选择几个最有希望的移动作为初始扩展
        sort(allMoves.begin(), allMoves.end());
        size_t initialMoves = min((size_t)10, allMoves.size());
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
        Move bestMove;
        int maxVisits = -1;
        
        for (auto& move : root->children) {
            // 额外验证移动的合法性
            if (!validatePath(move.startX, move.startY, move.endX, move.endY) || 
                !validatePath(move.endX, move.endY, move.arrowX, move.arrowY)) {
                continue; // 跳过非法移动
            }
            
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
            // 额外验证移动的合法性
            for (auto& move : allMoves) {
                if (validatePath(move.startX, move.startY, move.endX, move.endY) && 
                    validatePath(move.endX, move.endY, move.arrowX, move.arrowY)) {
                    bestMove = move;
                    break;
                }
            }
        }
        
        // 最终验证最佳移动
        if (bestMove.isValid()) {
            if (!validatePath(bestMove.startX, bestMove.startY, bestMove.endX, bestMove.endY) || 
                !validatePath(bestMove.endX, bestMove.endY, bestMove.arrowX, bestMove.arrowY)) {
                // 如果最佳移动非法，尝试其他合法移动
                for (auto& move : allMoves) {
                    if (validatePath(move.startX, move.startY, move.endX, move.endY) && 
                        validatePath(move.endX, move.endY, move.arrowX, move.arrowY)) {
                        bestMove = move;
                        break;
                    }
                }
            }
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
            if (!ProcStep(x0, y0, x1, y1, x2, y2, -currBotColor, false)) {
                // 如果对手移动非法，输出错误信息
                cout << "ERROR: Opponent made illegal move: " << x0 << ' ' << y0 << ' ' << x1 << ' ' << y1 << ' ' << x2 << ' ' << y2 << endl;
                return -1;
            }
        }
        
        if (i < turnID - 1) {
            cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
            if (x0 >= 0) {
                if (!ProcStep(x0, y0, x1, y1, x2, y2, currBotColor, false)) {
                    // 如果对手移动非法，输出错误信息
                    cout << "ERROR: Opponent made illegal move: " << x0 << ' ' << y0 << ' ' << x1 << ' ' << y1 << ' ' << x2 << ' ' << y2 << endl;
                    return -1;
                }
            }
        }
    }
    
    // 使用MCTS算法计算最佳移动
    MCTS mcts(currBotColor, turnID);
    Move bestMove = mcts.findBestMove();
    
    // 最终验证移动的合法性
    if (bestMove.isValid()) {
        if (!validatePath(bestMove.startX, bestMove.startY, bestMove.endX, bestMove.endY) || 
            !validatePath(bestMove.endX, bestMove.endY, bestMove.arrowX, bestMove.arrowY)) {
            // 如果移动非法，输出错误信息
            cout << "ERROR: Generated illegal move: " << bestMove.startX << ' ' << bestMove.startY << ' ' << bestMove.endX << ' ' << bestMove.endY << ' ' << bestMove.arrowX << ' ' << bestMove.arrowY << endl;
            return -1;
        }
    }
    
    // 输出结果
    cout << bestMove.startX << ' ' << bestMove.startY << ' '
         << bestMove.endX << ' ' << bestMove.endY << ' '
         << bestMove.arrowX << ' ' << bestMove.arrowY << endl;
    
    return 0;
}



