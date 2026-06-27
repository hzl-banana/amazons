#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>

#define GRIDSIZE 8
#define OBSTACLE 2
#define judge_black 0
#define judge_white 1
#define grid_black 1
#define grid_white -1

using namespace std;
using namespace chrono;

// ANSI颜色代码
#define ANSI_RESET "\033[0m"
#define ANSI_BLACK "\033[30m"
#define ANSI_WHITE "\033[37m"
#define ANSI_RED_BG "\033[41m"
#define ANSI_GREEN_BG "\033[42m"
#define ANSI_BLUE_BG "\033[44m"

int currBotColor;
int gridInfo[GRIDSIZE][GRIDSIZE] = { 0 };
int dx[] = { -1,-1,-1,0,0,1,1,1 };
int dy[] = { -1,0,1,-1,1,-1,0,1 };

// Zobrist哈希表
unsigned long long zobristTable[GRIDSIZE][GRIDSIZE][5];
const int EMPTY = 0;
const int BLACK = 1;
const int WHITE = -1;
const int OBST = 2;

// 游戏状态
struct GameState {
    int grid[GRIDSIZE][GRIDSIZE];
    int turn;
    int color;
    unsigned long long hash;
    
    GameState() : turn(0), color(grid_black), hash(0) {
        for(int i=0; i<GRIDSIZE; i++)
            for(int j=0; j<GRIDSIZE; j++)
                grid[i][j] = gridInfo[i][j];
    }
};

// 移动结构
struct Move {
    int startX, startY;
    int endX, endY;
    int arrowX, arrowY;
    double score;
    
    Move(int sx=0, int sy=0, int ex=0, int ey=0, int ax=0, int ay=0) 
        : startX(sx), startY(sy), endX(ex), endY(ey), arrowX(ax), arrowY(ay), score(0) {}
    
    bool operator<(const Move& other) const {
        return score > other.score; // 降序排列
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
    int turn;
    MCTSNode* parent;
    
    MCTSNode(unsigned long long h=0, int c=grid_black, MCTSNode* p=nullptr) 
        : hash(h), visitCount(0), winScore(0), expanded(false), color(c), turn(0), parent(p) {}
};

// 初始化Zobrist哈希表
void initZobrist() {
    srand(time(0));
    for(int i=0; i<GRIDSIZE; i++) {
        for(int j=0; j<GRIDSIZE; j++) {
            for(int k=0; k<5; k++) {
                zobristTable[i][j][k] = 
                    ((unsigned long long)rand() << 32) | rand();
            }
        }
    }
}

// 计算哈希值
unsigned long long calculateHash(int grid[GRIDSIZE][GRIDSIZE]) {
    unsigned long long hash = 0;
    for(int i=0; i<GRIDSIZE; i++) {
        for(int j=0; j<GRIDSIZE; j++) {
            int index;
            if(grid[i][j] == 0) index = 0;
            else if(grid[i][j] == grid_black) index = 1;
            else if(grid[i][j] == grid_white) index = 2;
            else if(grid[i][j] == OBSTACLE) index = 3;
            else index = 4;
            hash ^= zobristTable[i][j][index];
        }
    }
    return hash;
}

// 更新哈希值（增量更新）
unsigned long long updateHash(unsigned long long oldHash, int x, int y, int oldVal, int newVal) {
    int oldIdx, newIdx;
    if(oldVal == 0) oldIdx = 0;
    else if(oldVal == grid_black) oldIdx = 1;
    else if(oldVal == grid_white) oldIdx = 2;
    else if(oldVal == OBSTACLE) oldIdx = 3;
    else oldIdx = 4;
    
    if(newVal == 0) newIdx = 0;
    else if(newVal == grid_black) newIdx = 1;
    else if(newVal == grid_white) newIdx = 2;
    else if(newVal == OBSTACLE) newIdx = 3;
    else newIdx = 4;
    
    return oldHash ^ zobristTable[x][y][oldIdx] ^ zobristTable[x][y][newIdx];
}

inline bool inMap(int x, int y) {
    return x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE;
}

bool ProcStep(int x0, int y0, int x1, int y1, int x2, int y2, int color, bool check_only) {
    if ((!inMap(x0, y0)) || (!inMap(x1, y1)) || (!inMap(x2, y2)))
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
    
    for (int i = 0; i < GRIDSIZE; ++i) {
        for (int j = 0; j < GRIDSIZE; ++j) {
            if (gridInfo[i][j] != color) continue;
            
            // 棋子的八个方向
            for (int k = 0; k < 8; ++k) {
                for (int delta1 = 1; delta1 < GRIDSIZE; delta1++) {
                    int xx = i + dx[k] * delta1;
                    int yy = j + dy[k] * delta1;
                    if (!inMap(xx, yy) || gridInfo[xx][yy] != 0)
                        break;
                    
                    // 射箭的八个方向
                    for (int l = 0; l < 8; ++l) {
                        for (int delta2 = 1; delta2 < GRIDSIZE; delta2++) {
                            int xxx = xx + dx[l] * delta2;
                            int yyy = yy + dy[l] * delta2;
                            if (!inMap(xxx, yyy))
                                break;
                            if (gridInfo[xxx][yyy] != 0 && !(i == xxx && j == yyy))
                                break;
                            
                            moves.push_back(Move(i, j, xx, yy, xxx, yyy));
                        }
                    }
                }
            }
        }
    }
    
    return moves;
}

// 棋子自由度计算
int countLiberty(int x, int y, int grid[GRIDSIZE][GRIDSIZE]) {
    int liberty = 0;
    for(int k=0; k<8; k++) {
        int xx = x + dx[k];
        int yy = y + dy[k];
        if(inMap(xx, yy) && grid[xx][yy] == 0) {
            liberty++;
        }
    }
    return liberty;
}

// 评估函数 - 开局阶段
double evaluateEarlyGame(int color) {
    double score = 0;
    int opponentColor = -color;
    
    // 棋子灵活性评估
    int myMoves = generateAllMoves(color).size();
    int oppMoves = generateAllMoves(opponentColor).size();
    
    // 计算被困棋子
    int myTrapped = 0, oppTrapped = 0;
    for(int i=0; i<GRIDSIZE; i++) {
        for(int j=0; j<GRIDSIZE; j++) {
            if(gridInfo[i][j] == color) {
                int liberty = countLiberty(i, j, gridInfo);
                if(liberty <= 2) myTrapped++;
            } else if(gridInfo[i][j] == opponentColor) {
                int liberty = countLiberty(i, j, gridInfo);
                if(liberty <= 2) oppTrapped++;
            }
        }
    }
    
    score = 2.0 * (myMoves - oppMoves) - 5.0 * (myTrapped - oppTrapped);
    
    return score;
}

// 计算QueenMove距离
vector<vector<vector<int>>> calculateQueenMoveDistances() {
    vector<vector<vector<int>>> distances(GRIDSIZE, 
        vector<vector<int>>(GRIDSIZE, vector<int>(3, GRIDSIZE * GRIDSIZE)));
    
    // 为黑棋和白棋分别计算距离
    for(int colorIdx = 0; colorIdx < 2; colorIdx++) {
        int color = (colorIdx == 0) ? grid_black : grid_white;
        queue<pair<int, int>> q;
        
        // 初始化所有棋子位置距离为0
        for(int i=0; i<GRIDSIZE; i++) {
            for(int j=0; j<GRIDSIZE; j++) {
                if(gridInfo[i][j] == color) {
                    distances[i][j][colorIdx] = 0;
                    q.push({i, j});
                }
            }
        }
        
        // BFS计算距离
        while(!q.empty()) {
            auto [x, y] = q.front();
            q.pop();
            
            for(int k=0; k<8; k++) {
                int steps = 1;
                while(true) {
                    int xx = x + dx[k] * steps;
                    int yy = y + dy[k] * steps;
                    
                    if(!inMap(xx, yy) || gridInfo[xx][yy] != 0) break;
                    
                    if(distances[xx][yy][colorIdx] > distances[x][y][colorIdx] + 1) {
                        distances[xx][yy][colorIdx] = distances[x][y][colorIdx] + 1;
                        q.push({xx, yy});
                    }
                    steps++;
                }
            }
        }
    }
    
    return distances;
}

// 评估函数 - 中期阶段
double evaluateMidGame(int color) {
    double score = 0;
    int opponentColor = -color;
    
    auto distances = calculateQueenMoveDistances();
    int myIdx = (color == grid_black) ? 0 : 1;
    int oppIdx = (opponentColor == grid_black) ? 0 : 1;
    
    // 领地控制评估
    int myTerritory = 0, oppTerritory = 0;
    for(int i=0; i<GRIDSIZE; i++) {
        for(int j=0; j<GRIDSIZE; j++) {
            if(gridInfo[i][j] == 0) {
                if(distances[i][j][myIdx] < distances[i][j][oppIdx]) {
                    myTerritory++;
                } else if(distances[i][j][oppIdx] < distances[i][j][myIdx]) {
                    oppTerritory++;
                }
            }
        }
    }
    
    // 角区控制评估（四个3x3角区）
    double cornerControl = 0;
    vector<pair<int, int>> corners = {{0,0}, {0,5}, {5,0}, {5,5}};
    for(auto [cx, cy] : corners) {
        int myControl = 0, oppControl = 0;
        for(int i=cx; i<min(cx+3, GRIDSIZE); i++) {
            for(int j=cy; j<min(cy+3, GRIDSIZE); j++) {
                if(gridInfo[i][j] == color) myControl++;
                else if(gridInfo[i][j] == opponentColor) oppControl++;
                else {
                    if(distances[i][j][myIdx] < distances[i][j][oppIdx]) myControl++;
                    else if(distances[i][j][oppIdx] < distances[i][j][myIdx]) oppControl++;
                }
            }
        }
        cornerControl += (myControl - oppControl) / 9.0;
    }
    
    score = 1.5 * (myTerritory - oppTerritory) + 2.0 * cornerControl;
    
    return score;
}

// 评估函数 - 残局阶段
double evaluateEndGame(int color) {
    double score = 0;
    int opponentColor = -color;
    
    auto distances = calculateQueenMoveDistances();
    int myIdx = (color == grid_black) ? 0 : 1;
    int oppIdx = (opponentColor == grid_black) ? 0 : 1;
    
    // 领地控制评估（更精确）
    double myTerritory = 0, oppTerritory = 0;
    for(int i=0; i<GRIDSIZE; i++) {
        for(int j=0; j<GRIDSIZE; j++) {
            if(gridInfo[i][j] == 0) {
                int delta = distances[i][j][myIdx] - distances[i][j][oppIdx];
                double weight = 1.0 / (1.0 + exp(-0.5 * delta));
                if(delta < 0) myTerritory += weight;
                else if(delta > 0) oppTerritory += weight;
            }
        }
    }
    
    // 棋子被困评估
    int myTrapped = 0, oppTrapped = 0;
    for(int i=0; i<GRIDSIZE; i++) {
        for(int j=0; j<GRIDSIZE; j++) {
            if(gridInfo[i][j] == color) {
                int liberty = countLiberty(i, j, gridInfo);
                if(liberty <= 1) myTrapped++;
            } else if(gridInfo[i][j] == opponentColor) {
                int liberty = countLiberty(i, j, gridInfo);
                if(liberty <= 1) oppTrapped++;
            }
        }
    }
    
    score = 3.0 * (myTerritory - oppTerritory) - 10.0 * (myTrapped - oppTrapped);
    
    return score;
}

// 综合评估函数
double evaluateBoard(int color, int turn) {
    if(turn < 20) {
        return evaluateEarlyGame(color);
    } else if(turn < 50) {
        return evaluateMidGame(color);
    } else {
        return evaluateEndGame(color);
    }
}

// 改进的蒙特卡洛树搜索
class MCTS {
private:
    unordered_map<unsigned long long, MCTSNode*> transpositionTable;
    steady_clock::time_point startTime;
    const int TIME_LIMIT = 900; // 900毫秒
    int myColor;
    int opponentColor;
    
public:
    MCTS(int color) : myColor(color), opponentColor(-color) {
        initZobrist();
    }
    
    // 选择节点
    MCTSNode* selectNode(MCTSNode* node) {
        while(node->expanded && !node->children.empty()) {
            double maxUCB = -1e9;
            MCTSNode* bestChild = nullptr;
            
            for(auto& move : node->children) {
                // 查找子节点
                unsigned long long childHash = simulateMoveHash(node->hash, move);
                if(transpositionTable.find(childHash) == transpositionTable.end()) {
                    MCTSNode* child = new MCTSNode(childHash, -node->color, node);
                    transpositionTable[childHash] = child;
                    return child;
                }
                
                MCTSNode* child = transpositionTable[childHash];
                if(child->visitCount == 0) {
                    return child;
                }
                
                // 计算UCB值
                double ucb = (child->winScore / child->visitCount) +
                           sqrt(2 * log(node->visitCount) / child->visitCount);
                
                if(ucb > maxUCB) {
                    maxUCB = ucb;
                    bestChild = child;
                }
            }
            
            if(bestChild) node = bestChild;
            else break;
        }
        return node;
    }
    
    // 扩展节点
    void expandNode(MCTSNode* node) {
        if(node->expanded) return;
        
        // 备份当前棋盘状态
        int backupGrid[GRIDSIZE][GRIDSIZE];
        for(int i=0; i<GRIDSIZE; i++)
            for(int j=0; j<GRIDSIZE; j++)
                backupGrid[i][j] = gridInfo[i][j];
        
        // 生成所有合法移动
        vector<Move> moves = generateAllMoves(node->color);
        
        // 为每个移动评分并排序
        for(auto& move : moves) {
            move.score = evaluateMove(move, node->color);
        }
        sort(moves.begin(), moves.end());
        
        // 只保留前20个最佳移动（剪枝）
        if(moves.size() > 20) {
            moves.resize(20);
        }
        
        node->children = moves;
        node->expanded = true;
        
        // 恢复棋盘状态
        for(int i=0; i<GRIDSIZE; i++)
            for(int j=0; j<GRIDSIZE; j++)
                gridInfo[i][j] = backupGrid[i][j];
    }
    
    // 模拟移动
    double simulate(MCTSNode* node) {
        // 备份当前棋盘状态
        int backupGrid[GRIDSIZE][GRIDSIZE];
        for(int i=0; i<GRIDSIZE; i++)
            for(int j=0; j<GRIDSIZE; j++)
                backupGrid[i][j] = gridInfo[i][j];
        
        // 设置棋盘状态
        setBoardFromHash(node->hash);
        
        int currentColor = node->color;
        int depth = 0;
        const int MAX_DEPTH = 10;
        
        while(depth < MAX_DEPTH) {
            vector<Move> moves = generateAllMoves(currentColor);
            if(moves.empty()) {
                // 当前玩家无路可走，对手获胜
                setBoardFromHash(node->hash);
                for(int i=0; i<GRIDSIZE; i++)
                    for(int j=0; j<GRIDSIZE; j++)
                        gridInfo[i][j] = backupGrid[i][j];
                return (currentColor == myColor) ? 0.0 : 1.0;
            }
            
            // 选择最佳移动（基于简单评估）
            Move bestMove = moves[0];
            double bestScore = -1e9;
            for(auto& move : moves) {
                double score = evaluateMove(move, currentColor);
                if(score > bestScore) {
                    bestScore = score;
                    bestMove = move;
                }
            }
            
            // 执行移动
            ProcStep(bestMove.startX, bestMove.startY,
                    bestMove.endX, bestMove.endY,
                    bestMove.arrowX, bestMove.arrowY,
                    currentColor, false);
            
            currentColor = -currentColor;
            depth++;
        }
        
        // 评估最终局面
        double result = evaluateBoard(myColor, node->turn + depth);
        result = 1.0 / (1.0 + exp(-result / 10.0)); // 归一化到[0,1]
        
        // 恢复棋盘状态
        setBoardFromHash(node->hash);
        for(int i=0; i<GRIDSIZE; i++)
            for(int j=0; j<GRIDSIZE; j++)
                gridInfo[i][j] = backupGrid[i][j];
        
        return result;
    }
    
    // 回溯更新
    void backpropagate(MCTSNode* node, double result) {
        while(node != nullptr) {
            node->visitCount++;
            node->winScore += result;
            result = 1.0 - result; // 对手的视角
            node = node->parent;
        }
    }
    
    // 查找最佳移动
    Move findBestMove(int turn) {
        startTime = steady_clock::now();
        
        // 创建根节点
        unsigned long long rootHash = calculateHash(gridInfo);
        MCTSNode* root = new MCTSNode(rootHash, myColor);
        transpositionTable[rootHash] = root;
        
        int iterations = 0;
        while(true) {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - startTime);
            if(elapsed.count() >= TIME_LIMIT || iterations > 10000) break;
            
            // 选择
            MCTSNode* node = selectNode(root);
            
            // 扩展
            if(node->visitCount == 0) {
                expandNode(node);
            }
            
            // 模拟
            double result = simulate(node);
            
            // 回溯
            backpropagate(node, result);
            
            iterations++;
        }
        
        // 选择最佳移动
        Move bestMove;
        double bestValue = -1e9;
        
        for(auto& move : root->children) {
            unsigned long long childHash = simulateMoveHash(rootHash, move);
            if(transpositionTable.find(childHash) != transpositionTable.end()) {
                MCTSNode* child = transpositionTable[childHash];
                double value = child->visitCount;
                if(value > bestValue) {
                    bestValue = value;
                    bestMove = move;
                }
            }
        }
        
        // 如果没有找到最佳移动，返回第一个合法移动
        if(bestValue == -1e9 && !root->children.empty()) {
            bestMove = root->children[0];
        }
        
        // 清理内存（简化版本，实际应该更完善）
        for(auto& [hash, node] : transpositionTable) {
            delete node;
        }
        transpositionTable.clear();
        
        return bestMove;
    }
    
private:
    // 评估单个移动
    double evaluateMove(const Move& move, int color) {
        double score = 0;
        
        // 移动后的位置评估
        int endLiberty = countLiberty(move.endX, move.endY, gridInfo);
        score += endLiberty * 2.0;
        
        // 阻碍对手评估
        int opponentColor = -color;
        int opponentMovesBefore = generateAllMoves(opponentColor).size();
        
        // 模拟执行移动
        int backup1 = gridInfo[move.startX][move.startY];
        int backup2 = gridInfo[move.endX][move.endY];
        int backup3 = gridInfo[move.arrowX][move.arrowY];
        
        gridInfo[move.startX][move.startY] = 0;
        gridInfo[move.endX][move.endY] = color;
        gridInfo[move.arrowX][move.arrowY] = OBSTACLE;
        
        int opponentMovesAfter = generateAllMoves(opponentColor).size();
        
        // 恢复棋盘
        gridInfo[move.startX][move.startY] = backup1;
        gridInfo[move.endX][move.endY] = backup2;
        gridInfo[move.arrowX][move.arrowY] = backup3;
        
        score += (opponentMovesBefore - opponentMovesAfter) * 1.5;
        
        // 中心化程度
        double centerX = (GRIDSIZE - 1) / 2.0;
        double centerY = (GRIDSIZE - 1) / 2.0;
        double startDist = sqrt(pow(move.startX - centerX, 2) + pow(move.startY - centerY, 2));
        double endDist = sqrt(pow(move.endX - centerX, 2) + pow(move.endY - centerY, 2));
        score += (startDist - endDist) * 1.0; // 向中心移动
        
        return score;
    }
    
    // 模拟移动并计算哈希
    unsigned long long simulateMoveHash(unsigned long long oldHash, const Move& move) {
        int startVal = gridInfo[move.startX][move.startY];
        int endVal = gridInfo[move.endX][move.endY];
        int arrowVal = gridInfo[move.arrowX][move.arrowY];
        
        unsigned long long newHash = oldHash;
        newHash = updateHash(newHash, move.startX, move.startY, startVal, 0);
        newHash = updateHash(newHash, move.endX, move.endY, endVal, startVal);
        newHash = updateHash(newHash, move.arrowX, move.arrowY, arrowVal, OBSTACLE);
        
        return newHash;
    }
    
    // 从哈希值设置棋盘状态（需要全局状态，这里简化处理）
    void setBoardFromHash(unsigned long long hash) {
        // 注意：这是一个简化版本，实际上需要维护哈希值与棋盘的映射
        // 这里我们使用当前棋盘状态，因为我们在模拟过程中会修改它
    }
};

// 绘制精美棋盘
void drawBoard() {
    cout << endl;
    cout << ANSI_GREEN_BG "    A   B   C   D   E   F   G   H    " ANSI_RESET << endl;
    
    for(int i=0; i<GRIDSIZE; i++) {
        cout << ANSI_GREEN_BG " " << (i+1) << " " ANSI_RESET;
        for(int j=0; j<GRIDSIZE; j++) {
            cout << " ";
            if(gridInfo[i][j] == grid_black) {
                cout << ANSI_BLACK "♛" ANSI_RESET;
            } else if(gridInfo[i][j] == grid_white) {
                cout << ANSI_WHITE "♛" ANSI_RESET;
            } else if(gridInfo[i][j] == OBSTACLE) {
                cout << ANSI_RED_BG "💥" ANSI_RESET;
            } else {
                // 交替显示棋盘格子颜色
                if((i+j) % 2 == 0) {
                    cout << "□";
                } else {
                    cout << "■";
                }
            }
            cout << " ";
        }
        cout << ANSI_GREEN_BG " " << (i+1) << " " ANSI_RESET << endl;
    }
    
    cout << ANSI_GREEN_BG "    A   B   C   D   E   F   G   H    " ANSI_RESET << endl;
    cout << endl;
}

// 显示菜单
void showMenu() {
    cout << ANSI_BLUE_BG "🎮 亚马逊棋 AI 对战系统 🎮" ANSI_RESET << endl;
    cout << "1. 新游戏 🎯" << endl;
    cout << "2. AI 对战 🤖" << endl;
    cout << "3. 退出游戏 🚪" << endl;
    cout << "请选择操作 (1-3): ";
}

int main() {
    // 初始化
    int x0, y0, x1, y1, x2, y2;
    
    // 设置初始棋盘
    gridInfo[0][(GRIDSIZE - 1) / 3] = grid_black;
    gridInfo[(GRIDSIZE - 1) / 3][0] = grid_black;
    gridInfo[GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)][0] = grid_black;
    gridInfo[GRIDSIZE - 1][(GRIDSIZE - 1) / 3] = grid_black;
    
    gridInfo[0][GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)] = grid_white;
    gridInfo[(GRIDSIZE - 1) / 3][GRIDSIZE - 1] = grid_white;
    gridInfo[GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)][GRIDSIZE - 1] = grid_white;
    gridInfo[GRIDSIZE - 1][GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)] = grid_white;
    
    // 显示初始棋盘
    drawBoard();
    
    // 读取当前回合
    int turnID;
    cin >> turnID;
    
    currBotColor = grid_white;
    for (int i = 0; i < turnID; i++) {
        cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
        if (x0 == -1) {
            currBotColor = grid_black;
        }
        else
            ProcStep(x0, y0, x1, y1, x2, y2, -currBotColor, false);
        if (i < turnID - 1) {
            cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
            if (x0 >= 0)
                ProcStep(x0, y0, x1, y1, x2, y2, currBotColor, false);
        }
    }
    
    // 如果是第一回合且是黑棋，使用固定开局
    if (turnID == 1 && currBotColor == grid_black) {
        // 黑方固定开局：移动到(2,4)，放障碍到(3,5)
        cout << "0 2 2 4 3 5" << endl;
        return 0;
    }
    
    // 使用MCTS AI计算最佳移动
    MCTS mcts(currBotColor);
    Move bestMove = mcts.findBestMove(turnID);
    
    // 输出最佳移动
    cout << bestMove.startX << ' ' << bestMove.startY << ' ' 
         << bestMove.endX << ' ' << bestMove.endY << ' ' 
         << bestMove.arrowX << ' ' << bestMove.arrowY << endl;
    
    return 0;
}