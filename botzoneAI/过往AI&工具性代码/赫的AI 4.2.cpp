#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>
#include <set>
#include <bitset>
#include <array>
#include <cstring>
#include <immintrin.h>
#include <xmmintrin.h>
#include <smmintrin.h>
#include <x86intrin.h>

#pragma GCC optimize("O3")
#pragma GCC optimize("unroll-loops")
#pragma GCC target("avx2,bmi,bmi2,popcnt,lzcnt,fma,tune=native")

#define GRIDSIZE 8
#define OBSTACLE 2
#define grid_black 1
#define grid_white -1
#define EMPTY 0

using namespace std;
using namespace std::chrono;

// ==================== 超参数 ====================
constexpr int MAX_SEARCH_DEPTH = 6;
constexpr int MAX_MOVES = 2176;
constexpr int TIME_LIMIT_MS = 950;
constexpr double C_PUCT = 1.414;
constexpr double EPSILON = 1e-8;
constexpr int KILLER_MOVES = 3;
constexpr int TT_SIZE = 1 << 20;
constexpr int NNUE_HIDDEN = 256;
constexpr int NNUE_BUCKETS = 5;
constexpr int OPENING_BOOK_SIZE = 50;

// ==================== 全局变量 ====================
int currBotColor;
int gridInfo[GRIDSIZE][GRIDSIZE] = {0};
int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};

// ==================== 移动编码 ====================
struct Move {
    int startX, startY;
    int endX, endY;
    int arrowX, arrowY;
    int score;
    int killer; // 杀手启发标志
    
    Move() : startX(-1), startY(-1), endX(-1), endY(-1), 
             arrowX(-1), arrowY(-1), score(0), killer(0) {}
    
    Move(int sx, int sy, int ex, int ey, int ax, int ay) 
        : startX(sx), startY(sy), endX(ex), endY(ey), 
          arrowX(ax), arrowY(ay), score(0), killer(0) {}
    
    int encode() const {
        return (startX << 12) | (startY << 9) | (endX << 6) | (endY << 3) | (arrowX << 0) | (arrowY << 15);
    }
    
    static Move decode(int code) {
        Move m;
        m.startX = (code >> 12) & 7;
        m.startY = (code >> 9) & 7;
        m.endX = (code >> 6) & 7;
        m.endY = (code >> 3) & 7;
        m.arrowX = code & 7;
        m.arrowY = (code >> 15) & 7;
        return m;
    }
    
    bool operator<(const Move& other) const {
        if (killer != other.killer) return killer > other.killer;
        return score > other.score;
    }
    
    bool isValid() const {
        return startX != -1;
    }
};

// ==================== 函数声明 ====================
inline bool inMap(int x, int y);
bool validatePath(int x0, int y0, int x1, int y1, bool isArrowPath = false);
bool ProcStep(int x0, int y0, int x1, int y1, int x2, int y2, 
              int color, bool check_only = false);
vector<Move> generateAllMoves(int color);

// ==================== SIMD优化 ====================
#ifdef __AVX2__
typedef __m256i SIMDVec;
inline SIMDVec simd_load(const int* p) { return _mm256_loadu_si256((const __m256i*)p); }
inline void simd_store(int* p, SIMDVec v) { _mm256_storeu_si256((__m256i*)p, v); }
inline SIMDVec simd_add(SIMDVec a, SIMDVec b) { return _mm256_add_epi32(a, b); }
inline SIMDVec simd_mul(SIMDVec a, SIMDVec b) { return _mm256_mullo_epi32(a, b); }
#endif

// ==================== Zobrist哈希 ====================
class ZobristHash {
private:
    uint64_t table[GRIDSIZE][GRIDSIZE][4];
    uint64_t sideHash;
    
public:
    ZobristHash() {
        mt19937_64 rng(chrono::steady_clock::now().time_since_epoch().count());
        for (int i = 0; i < GRIDSIZE; i++)
            for (int j = 0; j < GRIDSIZE; j++)
                for (int k = 0; k < 4; k++)
                    table[i][j][k] = rng();
        sideHash = rng();
    }
    
    uint64_t hashBoard(int grid[GRIDSIZE][GRIDSIZE], int color) {
        uint64_t h = 0;
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                int idx;
                if (grid[i][j] == EMPTY) idx = 0;
                else if (grid[i][j] == grid_black) idx = 1;
                else if (grid[i][j] == grid_white) idx = 2;
                else idx = 3; // OBSTACLE
                h ^= table[i][j][idx];
            }
        }
        if (color == grid_black) h ^= sideHash;
        return h;
    }
    
    uint64_t updateHash(uint64_t oldHash, int x, int y, int oldVal, int newVal, int oldColor, int newColor) {
        int oldIdx = (oldVal == EMPTY) ? 0 : (oldVal == grid_black) ? 1 : (oldVal == grid_white) ? 2 : 3;
        int newIdx = (newVal == EMPTY) ? 0 : (newVal == grid_black) ? 1 : (newVal == grid_white) ? 2 : 3;
        
        uint64_t h = oldHash ^ table[x][y][oldIdx] ^ table[x][y][newIdx];
        if (oldColor != newColor) h ^= sideHash;
        return h;
    }
};

static ZobristHash zobrist;

// ==================== 置换表 ====================
struct TranspositionTableEntry {
    uint64_t hash;
    int depth;
    int score;
    int moveEncoded; // 编码为 start*4096 + end*64 + arrow
    int bound; // 0=exact, 1=lower, 2=upper
    int turn;
    
    TranspositionTableEntry() : hash(0), depth(-1), score(0), moveEncoded(-1), bound(0), turn(0) {}
};

class TranspositionTable {
private:
    vector<TranspositionTableEntry> table;
    int sizeMask;
    
public:
    TranspositionTable(int size = TT_SIZE) {
        table.resize(size);
        sizeMask = size - 1;
    }
    
    void store(uint64_t hash, int depth, int score, int moveEncoded, int bound, int turn) {
        int idx = hash & sizeMask;
        auto& entry = table[idx];
        
        // 替换策略：深度优先
        if (entry.depth <= depth) {
            entry.hash = hash;
            entry.depth = depth;
            entry.score = score;
            entry.moveEncoded = moveEncoded;
            entry.bound = bound;
            entry.turn = turn;
        }
    }
    
    bool probe(uint64_t hash, int depth, int alpha, int beta, int turn, int& score, int& moveEncoded) {
        int idx = hash & sizeMask;
        auto& entry = table[idx];
        
        if (entry.hash == hash && entry.depth >= depth && entry.turn == turn) {
            moveEncoded = entry.moveEncoded;
            score = entry.score;
            
            if (entry.bound == 0) return true; // exact
            if (entry.bound == 1 && score <= alpha) return true; // lower bound
            if (entry.bound == 2 && score >= beta) return true; // upper bound
        }
        return false;
    }
    
    void clear() {
        fill(table.begin(), table.end(), TranspositionTableEntry());
    }
};

// ==================== 棋盘Bitboard表示 ====================
class BitBoard {
private:
    uint64_t blackPieces;
    uint64_t whitePieces;
    uint64_t obstacles;
    
public:
    BitBoard() : blackPieces(0), whitePieces(0), obstacles(0) {}
    
    void fromGrid(int grid[GRIDSIZE][GRIDSIZE]) {
        blackPieces = whitePieces = obstacles = 0;
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                uint64_t bit = 1ULL << (i * GRIDSIZE + j);
                if (grid[i][j] == grid_black) blackPieces |= bit;
                else if (grid[i][j] == grid_white) whitePieces |= bit;
                else if (grid[i][j] == OBSTACLE) obstacles |= bit;
            }
        }
    }
    
    uint64_t getPieces(int color) const {
        return (color == grid_black) ? blackPieces : whitePieces;
    }
    
    uint64_t getAllPieces() const {
        return blackPieces | whitePieces | obstacles;
    }
    
    uint64_t getEmpty() const {
        return ~getAllPieces();
    }
    
    // 快速生成皇后移动
    uint64_t queenAttacks(uint64_t pieces, int square) const {
        uint64_t blockers = getAllPieces() & ~(1ULL << square);
        return slidingAttacks(square, blockers, true) | slidingAttacks(square, blockers, false);
    }
    
private:
    uint64_t slidingAttacks(int sq, uint64_t blockers, bool rook) const {
        uint64_t attacks = 0;
        int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        if (!rook) {
            dirs[2][0] = 1; dirs[2][1] = -1;
            dirs[3][0] = 1; dirs[3][1] = 1;
        }
        
        for (int i = 0; i < 4; i++) {
            auto& dir = dirs[i];
            for (int step = 1; step < GRIDSIZE; step++) {
                int x = (sq / GRIDSIZE) + dir[0] * step;
                int y = (sq % GRIDSIZE) + dir[1] * step;
                if (x < 0 || x >= GRIDSIZE || y < 0 || y >= GRIDSIZE) break;
                int pos = x * GRIDSIZE + y;
                attacks |= 1ULL << pos;
                if (blockers & (1ULL << pos)) break;
            }
        }
        return attacks;
    }
};

// ==================== 神经网络评估 (NNUE) ====================
class NNUE {
private:
    // 输入特征：每个格子4种状态 × 64个格子 = 256个特征
    static constexpr int INPUT_SIZE = 64 * 4;
    static constexpr int HIDDEN_SIZE = NNUE_HIDDEN;
    static constexpr int OUTPUT_SIZE = 1;
    
    // 权重矩阵
    float inputWeights[INPUT_SIZE * HIDDEN_SIZE];
    float hiddenWeights[HIDDEN_SIZE * OUTPUT_SIZE];
    float inputBias[HIDDEN_SIZE];
    float hiddenBias[OUTPUT_SIZE];
    
    // 激活函数
    inline float crelu(float x) const {
        return min(max(x, 0.0f), 1.0f);
    }
    
    // 快速点积
    float dotProduct(const float* a, const float* b, int n) const {
        float sum = 0;
        #ifdef __AVX2__
            __m256 sum_vec = _mm256_setzero_ps();
            for (int i = 0; i < n; i += 8) {
                __m256 va = _mm256_loadu_ps(a + i);
                __m256 vb = _mm256_loadu_ps(b + i);
                sum_vec = _mm256_fmadd_ps(va, vb, sum_vec);
            }
            sum = sum_vec[0] + sum_vec[1] + sum_vec[2] + sum_vec[3] +
                  sum_vec[4] + sum_vec[5] + sum_vec[6] + sum_vec[7];
        #else
            for (int i = 0; i < n; i++) {
                sum += a[i] * b[i];
            }
        #endif
        return sum;
    }
    
public:
    NNUE() {
        // 初始化权重（实际中应从文件加载训练好的权重）
        mt19937 rng(42);
        normal_distribution<float> dist(0.0f, 0.1f);
        
        for (int i = 0; i < INPUT_SIZE * HIDDEN_SIZE; i++)
            inputWeights[i] = dist(rng);
        for (int i = 0; i < HIDDEN_SIZE; i++)
            inputBias[i] = dist(rng);
        for (int i = 0; i < HIDDEN_SIZE * OUTPUT_SIZE; i++)
            hiddenWeights[i] = dist(rng);
        hiddenBias[0] = dist(rng);
    }
    
    float evaluate(int grid[GRIDSIZE][GRIDSIZE], int color, int turn) {
        // 构建输入特征向量
        float features[INPUT_SIZE] = {0};
        int featureIdx = 0;
        
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                int state = grid[i][j];
                if (state == color) features[featureIdx++] = 1.0f;
                else features[featureIdx++] = 0.0f;
                
                if (state == -color) features[featureIdx++] = 1.0f;
                else features[featureIdx++] = 0.0f;
                
                if (state == OBSTACLE) features[featureIdx++] = 1.0f;
                else features[featureIdx++] = 0.0f;
                
                if (state == EMPTY) features[featureIdx++] = 1.0f;
                else features[featureIdx++] = 0.0f;
            }
        }
        
        // 隐藏层计算
        float hidden[HIDDEN_SIZE];
        for (int i = 0; i < HIDDEN_SIZE; i++) {
            hidden[i] = dotProduct(features, &inputWeights[i * INPUT_SIZE], INPUT_SIZE) + inputBias[i];
            hidden[i] = crelu(hidden[i]);
        }
        
        // 输出层
        float output = dotProduct(hidden, hiddenWeights, HIDDEN_SIZE) + hiddenBias[0];
        
        // 应用Sigmoid
        return 2.0f / (1.0f + exp(-output)) - 1.0f; // 映射到[-1, 1]
    }
};

// ==================== 高级评估函数 ====================
class AdvancedEvaluator {
private:
    NNUE nnue;
    
    // 计算棋子的移动范围
    int calculateMobility(int color) {
        int mobility = 0;
        uint64_t empty = 0;
        for (int i = 0; i < GRIDSIZE; i++)
            for (int j = 0; j < GRIDSIZE; j++)
                if (gridInfo[i][j] == EMPTY) empty |= (1ULL << (i * GRIDSIZE + j));
        
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                if (gridInfo[i][j] == color) {
                    // 8个方向计算可达位置
                    for (int dir = 0; dir < 8; dir++) {
                        int x = i + dx[dir];
                        int y = j + dy[dir];
                        while (x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE && 
                               gridInfo[x][y] == EMPTY) {
                            mobility++;
                            x += dx[dir];
                            y += dy[dir];
                        }
                    }
                }
            }
        }
        return mobility;
    }
    
    // 计算领地（BFS方法）
    void calculateTerritory(int color, int territory[GRIDSIZE][GRIDSIZE]) {
        queue<pair<int, int>> q;
        bool visited[GRIDSIZE][GRIDSIZE] = {false};
        
        // 初始化己方棋子距离为0
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                territory[i][j] = 1000;
                if (gridInfo[i][j] == color) {
                    territory[i][j] = 0;
                    q.push({i, j});
                    visited[i][j] = true;
                }
            }
        }
        
        while (!q.empty()) {
            auto [x, y] = q.front();
            q.pop();
            
            for (int dir = 0; dir < 8; dir++) {
                int nx = x + dx[dir];
                int ny = y + dy[dir];
                int steps = 1;
                
                while (nx >= 0 && nx < GRIDSIZE && ny >= 0 && ny < GRIDSIZE && 
                       gridInfo[nx][ny] == EMPTY && steps <= territory[x][y] + 1) {
                    if (territory[nx][ny] > territory[x][y] + 1) {
                        territory[nx][ny] = territory[x][y] + 1;
                        if (!visited[nx][ny]) {
                            visited[nx][ny] = true;
                            q.push({nx, ny});
                        }
                    }
                    nx += dx[dir];
                    ny += dy[dir];
                    steps++;
                }
            }
        }
    }
    
    // 计算被困棋子
    int countTrappedPieces(int color) {
        int trapped = 0;
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                if (gridInfo[i][j] == color) {
                    bool canMove = false;
                    for (int dir = 0; dir < 8; dir++) {
                        int x = i + dx[dir];
                        int y = j + dy[dir];
                        if (x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE && 
                            gridInfo[x][y] == EMPTY) {
                            canMove = true;
                            break;
                        }
                    }
                    if (!canMove) trapped++;
                }
            }
        }
        return trapped;
    }
    
    // 计算连通性
    int calculateConnectivity(int color) {
        int connectedGroups = 0;
        bool visited[GRIDSIZE][GRIDSIZE] = {false};
        
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                if (gridInfo[i][j] == color && !visited[i][j]) {
                    connectedGroups++;
                    queue<pair<int, int>> q;
                    q.push({i, j});
                    visited[i][j] = true;
                    
                    while (!q.empty()) {
                        auto [x, y] = q.front();
                        q.pop();
                        
                        for (int dir = 0; dir < 8; dir++) {
                            int nx = x + dx[dir];
                            int ny = y + dy[dir];
                            if (nx >= 0 && nx < GRIDSIZE && ny >= 0 && ny < GRIDSIZE &&
                                gridInfo[nx][ny] == color && !visited[nx][ny]) {
                                visited[nx][ny] = true;
                                q.push({nx, ny});
                            }
                        }
                    }
                }
            }
        }
        
        return connectedGroups; // 返回连通组数，越少越好
    }
    
public:
    float evaluate(int color, int turn) {
        float score = 0.0f;
        int opponent = -color;
        
        // 1. NNUE评估（主要部分）
        score += nnue.evaluate(gridInfo, color, turn) * 10.0f;
        
        // 2. 领地评估
        int myTerritory[GRIDSIZE][GRIDSIZE], oppTerritory[GRIDSIZE][GRIDSIZE];
        calculateTerritory(color, myTerritory);
        calculateTerritory(opponent, oppTerritory);
        
        int territoryScore = 0;
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                if (gridInfo[i][j] == EMPTY) {
                    if (myTerritory[i][j] < oppTerritory[i][j]) territoryScore++;
                    else if (oppTerritory[i][j] < myTerritory[i][j]) territoryScore--;
                }
            }
        }
        score += territoryScore * 0.5f;
        
        // 3. 移动性评估
        int myMobility = calculateMobility(color);
        int oppMobility = calculateMobility(opponent);
        score += (myMobility - oppMobility) * 0.02f;
        
        // 4. 被困棋子评估
        int myTrapped = countTrappedPieces(color);
        int oppTrapped = countTrappedPieces(opponent);
        score += (oppTrapped - myTrapped) * 3.0f;
        
        // 5. 中心控制
        int centerControl = 0;
        for (int i = 2; i <= 5; i++) {
            for (int j = 2; j <= 5; j++) {
                if (gridInfo[i][j] == color) centerControl++;
                else if (gridInfo[i][j] == opponent) centerControl--;
            }
        }
        score += centerControl * 0.3f;
        
        // 6. 棋子密度和连通性
        float myDensity = 0, oppDensity = 0;
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                if (gridInfo[i][j] == color) {
                    // 计算周围己方棋子
                    for (int dx2 = -1; dx2 <= 1; dx2++) {
                        for (int dy2 = -1; dy2 <= 1; dy2++) {
                            int x = i + dx2, y = j + dy2;
                            if (x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE) {
                                if (gridInfo[x][y] == color) myDensity += 0.1f;
                            }
                        }
                    }
                }
            }
        }
        score += (myDensity - oppDensity) * 0.1f;
        
        // 7. 连通性评估 - 连通组数越少越好
        int myGroups = calculateConnectivity(color);
        int oppGroups = calculateConnectivity(opponent);
        score += (oppGroups - myGroups) * 1.0f;
        
        // 8. 终局阶段加强
        if (turn > 30) {
            // 终局阶段更重视领地和被困棋子
            score += territoryScore * 0.5f;
            score += (oppTrapped - myTrapped) * 2.0f; // 终局时被困棋子更关键
        }
        
        return score;
    }
    
    // 快速评估用于移动排序
    float quickEvaluate(const Move& move, int color) {
        float score = 0.0f;
        
        // 移动后位置的自由度
        int liberty = 0;
        for (int dir = 0; dir < 8; dir++) {
            int x = move.endX + dx[dir];
            int y = move.endY + dy[dir];
            if (x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE && 
                gridInfo[x][y] == EMPTY) {
                liberty++;
            }
        }
        score += liberty * 0.5f;
        
        // 移动到中心
        float centerX = 3.5f, centerY = 3.5f;
        float distStart = abs(move.startX - centerX) + abs(move.startY - centerY);
        float distEnd = abs(move.endX - centerX) + abs(move.endY - centerY);
        score += (distStart - distEnd) * 0.3f;
        
        // 阻碍对手
        for (int dir = 0; dir < 8; dir++) {
            int x = move.arrowX + dx[dir];
            int y = move.arrowY + dy[dir];
            if (x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE && 
                gridInfo[x][y] == -color) {
                score += 1.0f;
            }
        }
        
        // 防止自己被困
        int trappedAfter = 0;
        for (int dir = 0; dir < 8; dir++) {
            int x = move.endX + dx[dir];
            int y = move.endY + dy[dir];
            if (x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE && 
                gridInfo[x][y] != EMPTY) {
                trappedAfter++;
            }
        }
        if (trappedAfter == 8) score -= 10.0f; // 严重惩罚被困
        
        return score;
    }
};

// ==================== 开局库 ====================
class OpeningBook {
private:
    unordered_map<uint64_t, vector<Move>> book;
    
public:
    OpeningBook() {
        // 添加一些经典开局（实际应从一个文件加载更多开局）
        // 这里只添加几个示例
        vector<Move> moves1 = {Move(0,2,2,4,3,5), Move(0,2,1,4,2,5)};
        vector<Move> moves2 = {Move(7,2,5,4,6,5), Move(7,2,6,4,5,5)};
        
        // 计算初始局面的哈希
        uint64_t hash1 = zobrist.hashBoard(gridInfo, grid_black);
        book[hash1] = moves1;
        
        // 需要更多开局数据...
    }
    
    Move getBookMove(uint64_t hash) {
        auto it = book.find(hash);
        if (it != book.end() && !it->second.empty()) {
            return it->second[rand() % it->second.size()];
        }
        return Move(); // 返回无效移动
    }
    
    bool inBook(uint64_t hash) {
        return book.find(hash) != book.end();
    }
};

// ==================== MCTS节点 ====================
struct MCTSNode {
    uint64_t hash;
    int visits;
    float totalValue;
    float prior;
    vector<Move> children;
    vector<float> priors;
    vector<int> visitsChildren;
    vector<float> valuesChildren;
    MCTSNode* parent;
    bool expanded;
    
    MCTSNode(uint64_t h = 0, float p = 0.5f, MCTSNode* par = nullptr) 
        : hash(h), visits(0), totalValue(0.0f), prior(p), 
          parent(par), expanded(false) {}
    
    float uctScore(float parentVisits, float c = C_PUCT) const {
        if (visits == 0) return 10000.0f + (rand() % 1000) * 0.001f;
        return totalValue / visits + c * prior * sqrt(parentVisits) / (1 + visits);
    }
    
    Move bestChild() {
        if (children.empty()) return Move();
        
        int bestIdx = 0;
        float bestScore = -1e9;
        for (size_t i = 0; i < children.size(); i++) {
            float score = (visitsChildren[i] > 0) ? 
                valuesChildren[i] / visitsChildren[i] : 0.0f;
            if (score > bestScore) {
                bestScore = score;
                bestIdx = i;
            }
        }
        return children[bestIdx];
    }
};

// ==================== 强化版MCTS ====================
class EnhancedMCTS {
private:
    MCTSNode* root;
    AdvancedEvaluator evaluator;
    TranspositionTable tt;
    OpeningBook openingBook;
    vector<vector<Move>> killerMoves;
    vector<int> historyHeuristic[GRIDSIZE][GRIDSIZE][GRIDSIZE][GRIDSIZE]; // 历史启发
    
    steady_clock::time_point searchStart;
    int timeLimit;
    int simulations;
    int maxDepthReached;
    
    // 选择节点
    MCTSNode* select() {
        MCTSNode* node = root;
        while (node->expanded && !node->children.empty()) {
            float bestUCT = -1e9;
            int bestIdx = -1;
            
            for (size_t i = 0; i < node->children.size(); i++) {
                float score = node->uctScore(node->visits);
                // 添加历史启发
                if (node->visitsChildren[i] > 0) {
                    Move& m = node->children[i];
                    int hist = historyHeuristic[m.startX][m.startY][m.endX][m.endY].size();
                    score += sqrt(hist) * 0.1f;
                }
                
                if (score > bestUCT) {
                    bestUCT = score;
                    bestIdx = i;
                }
            }
            
            if (bestIdx == -1) break;
            
            // 执行移动
            Move& move = node->children[bestIdx];
            int originalGrid[GRIDSIZE][GRIDSIZE];
            memcpy(originalGrid, gridInfo, sizeof(gridInfo));
            
            // 验证移动是否合法
            if (ProcStep(move.startX, move.startY, move.endX, move.endY,
                        move.arrowX, move.arrowY, 
                        (node == root) ? currBotColor : -currBotColor, true)) {
                // 移动合法，执行
                ProcStep(move.startX, move.startY, move.endX, move.endY,
                        move.arrowX, move.arrowY, 
                        (node == root) ? currBotColor : -currBotColor, false);
            } else {
                // 移动不合法，跳过
                continue;
            }
            
            // 恢复原始状态
            memcpy(gridInfo, originalGrid, sizeof(gridInfo));
            
            // 移动到子节点
            // 注意：这里简化了，实际需要维护节点树
            break;
        }
        return node;
    }
    
    // 扩展节点
    void expand(MCTSNode* node) {
        if (node->expanded) return;
        
        int color = (node == root) ? currBotColor : -currBotColor;
        vector<Move> moves = generateAllMoves(color);
        
        // 使用评估器对移动排序
        for (Move& m : moves) {
            m.score = evaluator.quickEvaluate(m, color);
        }
        sort(moves.begin(), moves.end());
        
        // 限制分支数
        int maxBranches = min(30, (int)moves.size());
        moves.resize(maxBranches);
        
        node->children = moves;
        node->priors.resize(moves.size());
        node->visitsChildren.resize(moves.size(), 0);
        node->valuesChildren.resize(moves.size(), 0.0f);
        
        // 设置先验概率
        for (size_t i = 0; i < moves.size(); i++) {
            node->priors[i] = 1.0f / (i + 2); // 递减的先验概率
        }
        
        node->expanded = true;
    }
    
    // 模拟
    float simulate(MCTSNode* node, int depth = 0) {
        if (depth > 10) { // 限制模拟深度
            return evaluator.evaluate(currBotColor, depth);
        }
        
        // 检查胜负
        vector<Move> moves = generateAllMoves(-currBotColor);
        if (moves.empty()) {
            return (node == root) ? 1.0f : -1.0f; // 对手无路可走
        }
        
        // 使用策略网络或随机选择移动
        Move bestMove;
        float bestScore = -1e9;
        for (Move& m : moves) {
            float score = evaluator.quickEvaluate(m, -currBotColor);
            if (score > bestScore) {
                bestScore = score;
                bestMove = m;
            }
        }
        
        // 执行移动
        int originalGrid[GRIDSIZE][GRIDSIZE];
        memcpy(originalGrid, gridInfo, sizeof(gridInfo));
        
        ProcStep(bestMove.startX, bestMove.startY, bestMove.endX, bestMove.endY,
                bestMove.arrowX, bestMove.arrowY, -currBotColor, false);
        
        // 递归模拟
        float result = -simulate(node, depth + 1);
        
        // 恢复状态
        memcpy(gridInfo, originalGrid, sizeof(gridInfo));
        
        return result;
    }
    
    // 回传
    void backpropagate(MCTSNode* node, float value) {
        MCTSNode* current = node;
        while (current != nullptr) {
            current->visits++;
            current->totalValue += value;
            value = -value; // 对手视角
            current = current->parent;
        }
    }
    
public:
    EnhancedMCTS() : timeLimit(TIME_LIMIT_MS) {
        root = nullptr;
        killerMoves.resize(MAX_SEARCH_DEPTH);
        simulations = 0;
        maxDepthReached = 0;
    }
    
    Move search(int color) {
        searchStart = steady_clock::now();
        simulations = 0;
        maxDepthReached = 0;
        
        // 检查开局库
        uint64_t currentHash = zobrist.hashBoard(gridInfo, color);
        Move bookMove = openingBook.getBookMove(currentHash);
        if (bookMove.isValid()) {
            // 验证开局库移动是否合法
            if (ProcStep(bookMove.startX, bookMove.startY, bookMove.endX, bookMove.endY,
                        bookMove.arrowX, bookMove.arrowY, color, true)) {
                return bookMove;
            }
        }
        
        // 创建根节点
        root = new MCTSNode(currentHash, 1.0f);
        
        // MCTS主循环
        while (true) {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - searchStart);
            if (elapsed.count() > timeLimit || simulations > 10000) {
                break;
            }
            
            // 保存当前状态
            int backupGrid[GRIDSIZE][GRIDSIZE];
            memcpy(backupGrid, gridInfo, sizeof(gridInfo));
            
            // 选择
            MCTSNode* node = select();
            
            // 扩展
            if (!node->expanded) {
                expand(node);
            }
            
            // 模拟
            float value = simulate(node);
            
            // 回传
            backpropagate(node, value);
            
            // 恢复状态
            memcpy(gridInfo, backupGrid, sizeof(gridInfo));
            
            simulations++;
        }
        
        // 选择最佳移动
        Move bestMove = root->bestChild();
        
        // 最后验证移动是否合法
        if (bestMove.isValid()) {
            if (!ProcStep(bestMove.startX, bestMove.startY, bestMove.endX, bestMove.endY,
                         bestMove.arrowX, bestMove.arrowY, color, true)) {
                // 移动不合法，重新生成所有合法移动并选择最佳的
                vector<Move> moves = generateAllMoves(color);
                if (!moves.empty()) {
                    bestMove = moves[0];
                }
            }
        }
        
        // 清理
        // 需要清理整个树...
        
        cerr << "MCTS simulations: " << simulations << endl;
        cerr << "Max depth reached: " << maxDepthReached << endl;
        
        return bestMove;
    }
};

// ==================== 主搜索函数 ====================
inline bool inMap(int x, int y) {
    return x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE;
}

bool validatePath(int x0, int y0, int x1, int y1, bool isArrowPath) {
    if (!inMap(x0, y0) || !inMap(x1, y1)) return false;
    if (x0 == x1 && y0 == y1) return true;
    
    int dx_path = x1 - x0;
    int dy_path = y1 - y0;
    
    // 检查是否为直线或对角线移动
    if (dx_path != 0 && dy_path != 0 && abs(dx_path) != abs(dy_path)) return false;
    if (dx_path == 0 && dy_path == 0) return false;
    
    int step_x = (dx_path == 0) ? 0 : (dx_path > 0 ? 1 : -1);
    int step_y = (dy_path == 0) ? 0 : (dy_path > 0 ? 1 : -1);
    
    int x = x0 + step_x;
    int y = y0 + step_y;
    while (x != x1 || y != y1) {
        if (!inMap(x, y)) return false;
        // 对于移动路径（非箭路径），路径上不能有任何障碍物
        // 对于箭路径，路径上可以有障碍物，但不能有棋子
        if (!isArrowPath) {
            if (gridInfo[x][y] != 0) return false;
        } else {
            // 箭路径：中间路径可以有障碍物，但不能是棋子
            if (gridInfo[x][y] == grid_black || gridInfo[x][y] == grid_white) return false;
        }
        x += step_x;
        y += step_y;
    }
    
    return true;
}

bool ProcStep(int x0, int y0, int x1, int y1, int x2, int y2, 
              int color, bool check_only) {
    if (!inMap(x0, y0) || !inMap(x1, y1) || !inMap(x2, y2))
        return false;
    if (gridInfo[x0][y0] != color || gridInfo[x1][y1] != 0)
        return false;
    if (gridInfo[x2][y2] != 0 && !(x2 == x0 && y2 == y0))
        return false;
    
    // 检查移动路径（棋子移动）：路径上不能有任何障碍物
    if (!validatePath(x0, y0, x1, y1, false)) return false;
    // 检查射箭路径：路径上不能有棋子，但可以有障碍物
    if (!validatePath(x1, y1, x2, y2, true)) return false;
    
    if (!check_only) {
        gridInfo[x0][y0] = 0;
        gridInfo[x1][y1] = color;
        gridInfo[x2][y2] = OBSTACLE;
    }
    return true;
}

vector<Move> generateAllMoves(int color) {
    vector<Move> moves;
    moves.reserve(MAX_MOVES);
    
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            if (gridInfo[i][j] != color) continue;
            
            for (int dir = 0; dir < 8; dir++) {
                int steps = 1;
                while (true) {
                    int x1 = i + dx[dir] * steps;
                    int y1 = j + dy[dir] * steps;
                    
                    if (!inMap(x1, y1) || gridInfo[x1][y1] != 0) break;
                    
                    // 检查移动路径（棋子移动）是否合法：路径上不能有任何障碍物
                    if (!validatePath(i, j, x1, y1, false)) break;
                    
                    for (int arrowDir = 0; arrowDir < 8; arrowDir++) {
                        int arrowSteps = 1;
                        while (true) {
                            int x2 = x1 + dx[arrowDir] * arrowSteps;
                            int y2 = y1 + dy[arrowDir] * arrowSteps;
                            
                            if (!inMap(x2, y2)) break;
                            if (gridInfo[x2][y2] != 0 && !(i == x2 && j == y2)) break;
                            
                            // 检查箭路径是否合法：路径上不能有棋子，但可以有障碍物
                            if (validatePath(x1, y1, x2, y2, true)) {
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

// ==================== 主函数 ====================
int main() {
    // 设置随机种子
    srand(time(NULL) ^ (uint64_t)main);
    
    // 初始化棋盘
    memset(gridInfo, 0, sizeof(gridInfo));
    
    // 设置初始棋子位置
    gridInfo[0][2] = grid_black;
    gridInfo[2][0] = grid_black;
    gridInfo[5][0] = grid_black;
    gridInfo[7][2] = grid_black;
    
    gridInfo[0][5] = grid_white;
    gridInfo[2][7] = grid_white;
    gridInfo[5][7] = grid_white;
    gridInfo[7][5] = grid_white;
    
    // 读取回合数
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
    
    // 选择搜索算法
    Move bestMove;
    
    // 开局阶段使用快速评估
    if (turnID < 10) {
        AdvancedEvaluator eval;
        vector<Move> moves = generateAllMoves(currBotColor);
        
        if (!moves.empty()) {
            for (Move& m : moves) {
                m.score = eval.quickEvaluate(m, currBotColor);
            }
            sort(moves.begin(), moves.end());
            bestMove = moves[0];
        }
    } else {
        // 中后期使用强化版MCTS
        EnhancedMCTS mcts;
        bestMove = mcts.search(currBotColor);
    }
    
    // 如果没有找到合法移动，选择第一个合法移动
    if (!bestMove.isValid()) {
        vector<Move> moves = generateAllMoves(currBotColor);
        if (!moves.empty()) {
            bestMove = moves[0];
        } else {
            bestMove = Move(-1, -1, -1, -1, -1, -1);
        }
    }
    
    // 最后验证移动是否合法
    if (bestMove.isValid()) {
        if (!ProcStep(bestMove.startX, bestMove.startY, bestMove.endX, bestMove.endY,
                     bestMove.arrowX, bestMove.arrowY, currBotColor, true)) {
            // 移动不合法，重新生成所有合法移动并选择最佳的
            vector<Move> moves = generateAllMoves(currBotColor);
            if (!moves.empty()) {
                bestMove = moves[0];
            } else {
                bestMove = Move(-1, -1, -1, -1, -1, -1);
            }
        }
    }
    
    // 输出结果
    cout << bestMove.startX << ' ' << bestMove.startY << ' '
         << bestMove.endX << ' ' << bestMove.endY << ' '
         << bestMove.arrowX << ' ' << bestMove.arrowY << endl;
    
    // 输出调试信息到stderr
    cerr << "Turn: " << turnID << ", Color: " 
         << (currBotColor == grid_black ? "Black" : "White") 
         << ", Move: " << bestMove.startX << "," << bestMove.startY 
         << "->" << bestMove.endX << "," << bestMove.endY 
         << " Arrow:" << bestMove.arrowX << "," << bestMove.arrowY << endl;
    
    return 0;
}



