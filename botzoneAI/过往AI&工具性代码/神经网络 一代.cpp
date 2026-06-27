// amazons_infer_mcts_optimized.cpp
// 终极优化版：确保800ms内完成决策
// 编译: g++ -O3 -std=c++17 -march=native amazons_infer_mcts_optimized.cpp -o amazons_infer_mcts_optimized

#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <memory>
#include <fstream>
#include <cstdint>
#include <chrono>
#include <string>
#include <cstring>
#include <unordered_map>
#include <random>

using namespace std;
using namespace std::chrono;

const std::string HIDDEN_PATH = "/data/hidden_101.bin";
const std::string OUTPUT_PATH = "/data/output_101.bin";

// 全局变量
constexpr int GRIDSIZE = 8;
constexpr int OBSTACLE = 2;
constexpr int grid_black = 1;
constexpr int grid_white = -1;
constexpr int EMPTY = 0;

int gridInfo[GRIDSIZE][GRIDSIZE]{};

inline bool inMap(int x,int y){return x>=0&&x<GRIDSIZE&&y>=0&&y<GRIDSIZE;}

// 处理一步移动
void ProcStep(int x0,int y0,int x1,int y1,int x2,int y2,int color){
    gridInfo[x0][y0] = 0;
    gridInfo[x1][y1] = color;
    gridInfo[x2][y2] = OBSTACLE;
}

// NN dims
constexpr int UNITS=29, TUPLES=65, HIDDEN=256, BUCKETS=3;
vector<float> hidden_w, output_w;

// 位板优化
using U64 = uint64_t;

// 方向向量
const int dir_dx[8] = {0, 0, 1, -1, 1, 1, -1, -1};
const int dir_dy[8] = {1, -1, 0, 0, 1, -1, 1, -1};

// 坐标转换
inline int idx_from_xy(int x,int y){return 8*(7 - x) + (7 - y);}

// 预计算两点之间的格子（射线检查用）
U64 between_masks[64][64];
bool line_valid[64][64];  // 两点之间是否有直线路径

void init_masks() {
    static bool initialized = false;
    if (initialized) return;
    
    // 初始化 between_masks 和 line_valid
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j < 64; ++j) {
            between_masks[i][j] = 0;
            line_valid[i][j] = false;
            
            int x1 = 7 - (i / 8), y1 = 7 - (i % 8);
            int x2 = 7 - (j / 8), y2 = 7 - (j % 8);
            
            int dx = x2 - x1;
            int dy = y2 - y1;
            
            if (dx == 0 && dy == 0) continue;
            
            // 检查是否在同一行、列或对角线上
            if (dx == 0 || dy == 0 || abs(dx) == abs(dy)) {
                line_valid[i][j] = true;
                
                int step_x = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
                int step_y = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
                
                int steps = max(abs(dx), abs(dy));
                int x = x1 + step_x, y = y1 + step_y;
                
                // 记录两点之间的所有格子（不包含端点）
                for (int k = 1; k < steps; ++k) {
                    int idx = 8*(7 - x) + (7 - y);
                    between_masks[i][j] |= (1ULL << idx);
                    x += step_x;
                    y += step_y;
                }
            }
        }
    }
    initialized = true;
}

// 超级快速的射线检查
inline bool fast_line_clear(U64 occ, int idx0, int idx1) {
    if (!line_valid[idx0][idx1]) return false;
    return (occ & between_masks[idx0][idx1]) == 0;
}

// 优化版位棋盘
struct FastBoard {
    U64 white=0, black=0, arrow=0;
    
    void from_grid() {
        white = black = arrow = 0;
        // 展开循环以提高速度
        for (int i = 0; i < 64; ++i) {
            int x = 7 - (i / 8);
            int y = 7 - (i % 8);
            int v = gridInfo[x][y];
            U64 bit = 1ULL << i;
            if (v == grid_white) white |= bit;
            else if (v == grid_black) black |= bit;
            else if (v == OBSTACLE) arrow |= bit;
        }
    }
    
    U64 gen_reach_single(U64 src) const {
        U64 empty = ~(white | black | arrow);
        U64 reach = 0;
        
        int idx = __builtin_ctzll(src);
        int x0 = 7 - (idx / 8), y0 = 7 - (idx % 8);
        
        // 手动展开8个方向
        for (int d = 0; d < 8; ++d) {
            int x = x0 + dir_dx[d];
            int y = y0 + dir_dy[d];
            
            while (inMap(x, y)) {
                int cidx = 8*(7 - x) + (7 - y);
                if ((empty >> cidx) & 1) {
                    reach |= (1ULL << cidx);
                    x += dir_dx[d];
                    y += dir_dy[d];
                } else {
                    break;
                }
            }
        }
        return reach;
    }
    
    U64 gen_reach_all(U64 pieces) const {
        U64 empty = ~(white | black | arrow);
        U64 all_reach = 0;
        
        while (pieces) {
            int idx = __builtin_ctzll(pieces);
            int x0 = 7 - (idx / 8), y0 = 7 - (idx % 8);
            
            for (int d = 0; d < 8; ++d) {
                int x = x0 + dir_dx[d];
                int y = y0 + dir_dy[d];
                
                while (inMap(x, y)) {
                    int cidx = 8*(7 - x) + (7 - y);
                    if ((empty >> cidx) & 1) {
                        all_reach |= (1ULL << cidx);
                        x += dir_dx[d];
                        y += dir_dy[d];
                    } else {
                        break;
                    }
                }
            }
            
            pieces &= pieces - 1;
        }
        return all_reach;
    }
    
    void to_tuples(int cur_player, int rounds, array<int,TUPLES>& t) const {
        U64 mine = (cur_player==0) ? white : black;
        U64 opp  = (cur_player==0) ? black : white;
        U64 occ = white | black | arrow;
        
        // 预计算可达区域
        U64 mine_reach = gen_reach_all(mine);
        U64 opp_reach = gen_reach_all(opp);
        U64 mine_reach2 = gen_reach_all(mine | mine_reach);
        U64 opp_reach2 = gen_reach_all(opp | opp_reach);
        
        // 使用快速遍历
        for (int i = 0; i < 64; ++i) {
            U64 pos = 1ULL << i;
            
            if (mine & pos) {
                U64 r = gen_reach_single(pos);
                int mob = min(9, __builtin_popcountll(r));
                t[i] = 1 + mob;
            } else if (opp & pos) {
                U64 r = gen_reach_single(pos);
                int mob = min(9, __builtin_popcountll(r));
                t[i] = 10 + mob;
            } else if (arrow & pos) {
                t[i] = 20;
            } else {
                bool a1 = (mine_reach & pos) != 0;
                bool b1 = (opp_reach & pos) != 0;
                bool a2 = (mine_reach2 & pos) != 0;
                bool b2 = (opp_reach2 & pos) != 0;
                
                if (a1 && b1) t[i] = 21;
                else if (a1) t[i] = b2 ? 22 : 23;
                else if (b1) t[i] = a2 ? 24 : 25;
                else if (a2 && b2) t[i] = 26;
                else if (a2) t[i] = 27;
                else if (b2) t[i] = 28;
                else t[i] = 0;
            }
        }
        
        // 回合特征
        if (rounds < 14) t[64] = 0;
        else if (rounds < 28) t[64] = 1;
        else t[64] = 2;
    }
};

// 快速神经网络评估
float fast_eval_state(const array<int,TUPLES>& ts) {
    alignas(32) float h[HIDDEN];
    memset(h, 0, sizeof(h));
    
    // 使用循环展开
    for (int j = 0; j < TUPLES; ++j) {
        int idx = ts[j];
        if (idx < 0 || idx >= UNITS) continue;
        
        size_t base = static_cast<size_t>(idx) * TUPLES * HIDDEN + j * HIDDEN;
        
        // 每次处理8个元素
        int k = 0;
        for (; k + 7 < HIDDEN; k += 8) {
            h[k]   += hidden_w[base + k];
            h[k+1] += hidden_w[base + k + 1];
            h[k+2] += hidden_w[base + k + 2];
            h[k+3] += hidden_w[base + k + 3];
            h[k+4] += hidden_w[base + k + 4];
            h[k+5] += hidden_w[base + k + 5];
            h[k+6] += hidden_w[base + k + 6];
            h[k+7] += hidden_w[base + k + 7];
        }
        for (; k < HIDDEN; ++k) {
            h[k] += hidden_w[base + k];
        }
    }
    
    // 激活函数 (relu2)
    for (int k = 0; k < HIDDEN; ++k) {
        float x = h[k];
        if (x < 0) h[k] = 0;
        else if (x < 1) h[k] = x * x;
    }
    
    // 输出层
    int bucket = ts[64];
    if (bucket < 0 || bucket >= BUCKETS) bucket = 0;
    
    size_t out_base = static_cast<size_t>(bucket) * HIDDEN;
    float out = 0.0f;
    
    for (int k = 0; k < HIDDEN; ++k) {
        out += output_w[out_base + k] * h[k];
    }
    
    // tanh近似
    float x = out;
    if (x < -3.0f) return -0.995f;
    if (x > 3.0f) return 0.995f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);  // Pade近似
}

// 文件加载
bool load_bin(const string& path, vector<float>& buf, size_t expect) {
    ifstream f(path, ios::binary);
    if (!f) return false;
    
    buf.resize(expect);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<streamsize>(expect * sizeof(float)));
    
    return f.good();
}

// 移动结构
struct Move {
    uint8_t x0, y0, x1, y1, x2, y2;
    
    Move() = default;
    Move(int x0, int y0, int x1, int y1, int x2, int y2) :
        x0(static_cast<uint8_t>(x0)), y0(static_cast<uint8_t>(y0)),
        x1(static_cast<uint8_t>(x1)), y1(static_cast<uint8_t>(y1)),
        x2(static_cast<uint8_t>(x2)), y2(static_cast<uint8_t>(y2)) {}
    
    void apply(int color) const {
        gridInfo[x0][y0] = 0;
        gridInfo[x1][y1] = color;
        gridInfo[x2][y2] = OBSTACLE;
    }
    
    void undo(int color) const {
        gridInfo[x2][y2] = 0;
        gridInfo[x1][y1] = 0;
        gridInfo[x0][y0] = color;
    }
};

// Zobrist哈希表（用于缓存）
U64 zobrist_table[64][4];  // 64个格子，4种状态(0:空,1:白,2:黑,3:箭)

void init_zobrist() {
    static std::mt19937_64 rng(123456);
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j < 4; ++j) {
            zobrist_table[i][j] = rng();
        }
    }
}

U64 compute_board_hash() {
    U64 hash = 0;
    for (int i = 0; i < 64; ++i) {
        int x = 7 - (i / 8);
        int y = 7 - (i % 8);
        int piece = gridInfo[x][y];
        int state = 0;
        if (piece == grid_white) state = 1;
        else if (piece == grid_black) state = 2;
        else if (piece == OBSTACLE) state = 3;
        hash ^= zobrist_table[i][state];
    }
    return hash;
}

// 高效移动生成（不使用缓存，直接生成）
vector<Move> fast_gen_moves(int color) {
    vector<Move> moves;
    
    // 计算障碍位板
    U64 occ = 0;
    for (int x = 0; x < GRIDSIZE; ++x) {
        for (int y = 0; y < GRIDSIZE; ++y) {
            if (gridInfo[x][y] != 0) {
                occ |= (1ULL << idx_from_xy(x, y));
            }
        }
    }
    
    // 查找所有己方棋子
    for (int x0 = 0; x0 < GRIDSIZE; ++x0) {
        for (int y0 = 0; y0 < GRIDSIZE; ++y0) {
            if (gridInfo[x0][y0] != color) continue;
            
            int idx0 = idx_from_xy(x0, y0);
            
            // 生成所有可能的落点（8个方向）
            for (int d1 = 0; d1 < 8; ++d1) {
                int x1 = x0 + dir_dx[d1];
                int y1 = y0 + dir_dy[d1];
                
                while (inMap(x1, y1) && gridInfo[x1][y1] == 0) {
                    int idx1 = idx_from_xy(x1, y1);
                    
                    // 生成所有可能的箭点（8个方向）
                    for (int d2 = 0; d2 < 8; ++d2) {
                        int x2 = x1 + dir_dx[d2];
                        int y2 = y1 + dir_dy[d2];
                        
                        while (inMap(x2, y2)) {
                            // 箭点可以是空地或起点（起点已腾空）
                            if (gridInfo[x2][y2] != 0 && !(x2 == x0 && y2 == y0)) break;
                            
                            int idx2 = idx_from_xy(x2, y2);
                            
                            // 快速射线检查（落点->箭点）
                            if (fast_line_clear(occ | (1ULL << idx1), idx1, idx2)) {
                                moves.emplace_back(x0, y0, x1, y1, x2, y2);
                            }
                            
                            x2 += dir_dx[d2];
                            y2 += dir_dy[d2];
                        }
                    }
                    
                    x1 += dir_dx[d1];
                    y1 += dir_dy[d1];
                }
            }
        }
    }
    
    return moves;
}

// 简化MCTS节点
struct SimpleNode {
    Move move;
    float value_sum = 0;
    int visits = 0;
    float prior = 0;
    bool expanded = false;
    bool terminal = false;
    vector<SimpleNode> children;
    
    SimpleNode* select_child(float c_puct, int parent_visits) {
        SimpleNode* best = nullptr;
        float best_score = -1e9f;
        
        for (auto& child : children) {
            float score;
            if (child.visits == 0) {
                score = 10000.0f + child.prior;  // 鼓励探索未访问节点
            } else {
                float exploit = child.value_sum / child.visits;
                float explore = c_puct * child.prior * 
                               sqrtf(logf(parent_visits + 1e-6f)) / (1.0f + child.visits);
                score = exploit + explore;
            }
            
            if (score > best_score) {
                best_score = score;
                best = &child;
            }
        }
        
        return best;
    }
};

// 评估缓存
unordered_map<U64, float> eval_cache;
constexpr size_t MAX_CACHE_SIZE = 10000;

// 快速评估（带缓存）
float quick_evaluate(int color, int rounds) {
    // 计算棋盘哈希
    static U64 last_hash = 0;
    static float last_value = 0;
    static int last_color = -1;
    static int last_rounds = -1;
    
    U64 hash = compute_board_hash();
    hash ^= (color == grid_white) ? 0x123456789ABCDEF0ULL : 0x0FEDCBA987654321ULL;
    hash ^= static_cast<U64>(rounds) << 32;
    
    if (hash == last_hash && color == last_color && rounds == last_rounds) {
        return last_value;
    }
    
    // 检查缓存
    auto it = eval_cache.find(hash);
    if (it != eval_cache.end()) {
        last_hash = hash;
        last_color = color;
        last_rounds = rounds;
        last_value = it->second;
        return last_value;
    }
    
    // 计算评估值
    FastBoard board;
    board.from_grid();
    array<int, TUPLES> features;
    board.to_tuples(color == grid_white ? 0 : 1, rounds, features);
    float value = fast_eval_state(features);
    
    // 更新缓存
    if (eval_cache.size() < MAX_CACHE_SIZE) {
        eval_cache[hash] = value;
    }
    
    last_hash = hash;
    last_color = color;
    last_rounds = rounds;
    last_value = value;
    
    return value;
}

// 快速MCTS模拟（单次）
float fast_simulate(SimpleNode& node, int to_move, int rounds, float c_puct) {
    SimpleNode* cur_node = &node;
    int cur_player = to_move;
    int cur_rounds = rounds;
    
    // 存储路径用于回溯
    vector<SimpleNode*> path;
    
    // 选择阶段
    while (cur_node->expanded && !cur_node->terminal) {
        SimpleNode* child = cur_node->select_child(c_puct, cur_node->visits);
        if (!child) break;
        
        child->move.apply(cur_player);
        path.push_back(child);
        cur_node = child;
        cur_player = -cur_player;
        cur_rounds++;
    }
    
    // 评估叶节点
    float value;
    if (cur_node->terminal) {
        value = -1.0f;  // 当前玩家输
    } else if (!cur_node->expanded) {
        // 扩展节点
        auto moves = fast_gen_moves(cur_player);
        
        if (moves.empty()) {
            cur_node->terminal = true;
            value = -1.0f;
        } else {
            // 创建子节点
            cur_node->children.reserve(moves.size());
            float prior = 1.0f / moves.size();
            
            for (auto& move : moves) {
                SimpleNode child;
                child.move = move;
                child.prior = prior;
                cur_node->children.push_back(child);
            }
            
            cur_node->expanded = true;
            
            // 评估当前局面
            value = quick_evaluate(cur_player, cur_rounds);
        }
    } else {
        value = quick_evaluate(cur_player, cur_rounds);
    }
    
    // 回溯更新
    float propagate_value = value;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        SimpleNode* path_node = *it;
        path_node->visits++;
        path_node->value_sum += propagate_value;
        
        // 撤销移动
        path_node->move.undo(propagate_value > 0 ? cur_player : -cur_player);
        
        propagate_value = -propagate_value;  // 对手视角翻转
    }
    
    // 更新根节点
    node.visits++;
    node.value_sum += (to_move == cur_player) ? value : -value;
    
    return value;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    
    // 初始化各种预计算表
    init_masks();
    init_zobrist();
    
    // 加载神经网络权重
    const size_t HW_SZ = static_cast<size_t>(UNITS) * TUPLES * HIDDEN;
    const size_t OW_SZ = static_cast<size_t>(BUCKETS) * HIDDEN;
    
    if (!load_bin(HIDDEN_PATH, hidden_w, HW_SZ)) {
        cerr << "Failed to load hidden weights" << endl;
        return 1;
    }
    
    if (!load_bin(OUTPUT_PATH, output_w, OW_SZ)) {
        cerr << "Failed to load output weights" << endl;
        return 1;
    }
    
    // 初始化棋盘
    memset(gridInfo, 0, sizeof(gridInfo));
    
    // 初始布局
    int pos = (GRIDSIZE - 1) / 3;
    gridInfo[0][pos] = grid_black;
    gridInfo[pos][0] = grid_black;
    gridInfo[GRIDSIZE-1-pos][0] = grid_black;
    gridInfo[GRIDSIZE-1][pos] = grid_black;
    
    gridInfo[0][GRIDSIZE-1-pos] = grid_white;
    gridInfo[pos][GRIDSIZE-1] = grid_white;
    gridInfo[GRIDSIZE-1-pos][GRIDSIZE-1] = grid_white;
    gridInfo[GRIDSIZE-1][GRIDSIZE-1-pos] = grid_white;
    
    // 读取输入
    int turnID;
    cin >> turnID;
    
    int currBotColor = grid_white;
    int rounds = 0;
    
    // 处理历史移动
    for (int i = 0; i < turnID; ++i) {
        int x0, y0, x1, y1, x2, y2;
        cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
        
        if (x0 == -1) {
            currBotColor = grid_black;
        } else {
            ProcStep(x0, y0, x1, y1, x2, y2, -currBotColor);
            rounds++;
        }
        
        if (i < turnID - 1) {
            cin >> x0 >> y0 >> x1 >> y1 >> x2 >> y2;
            if (x0 >= 0) {
                ProcStep(x0, y0, x1, y1, x2, y2, currBotColor);
                rounds++;
            }
        }
    }
    
    // 检查是否有合法移动
    auto moves = fast_gen_moves(currBotColor);
    if (moves.empty()) {
        cout << "-1 -1 -1 -1 -1 -1\n";
        return 0;
    }
    
    // 创建根节点
    SimpleNode root;
    root.children.reserve(moves.size());
    
    float prior = 1.0f / moves.size();
    for (auto& move : moves) {
        SimpleNode child;
        child.move = move;
        child.prior = prior;
        root.children.push_back(child);
    }
    root.expanded = true;
    
    // MCTS搜索 - 确保在800ms内完成
    const int TIME_LIMIT_MS = 700;  // 留20ms余量
    auto start_time = steady_clock::now();
    
    int sims_done = 0;
    float c_puct = 1.5f;  // 探索参数
    
    // 自适应搜索：根据剩余时间调整
    while (true) {
        auto now = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(now - start_time).count();
        
        if (elapsed >= TIME_LIMIT_MS) break;
        
        // 动态调整探索参数
        float remaining_ratio = 1.0f - (float)elapsed / TIME_LIMIT_MS;
        float current_c_puct = c_puct * (0.5f + 0.5f * remaining_ratio);
        
        // 执行一次模拟
        fast_simulate(root, currBotColor, rounds, current_c_puct);
        sims_done++;
        
        // 每100次模拟检查一次时间
        if (sims_done % 100 == 0) {
            auto checkpoint = steady_clock::now();
            auto checkpoint_elapsed = duration_cast<milliseconds>(checkpoint - start_time).count();
            if (checkpoint_elapsed >= TIME_LIMIT_MS * 0.9f) {
                // 最后10%时间，加快速度
                c_puct *= 0.8f;
            }
        }
    }
    
    // 选择最佳移动（最多访问次数）
    SimpleNode* best_child = nullptr;
    int best_visits = -1;
    float best_value = -1e9f;
    
    for (auto& child : root.children) {
        if (child.visits > 0) {
            float child_value = child.value_sum / child.visits;
            if (child.visits > best_visits || 
                (child.visits == best_visits && child_value > best_value)) {
                best_visits = child.visits;
                best_value = child_value;
                best_child = &child;
            }
        }
    }
    
    // 输出结果
    if (best_child && best_visits > 0) {
        Move best = best_child->move;
        cout << static_cast<int>(best.x0) << ' ' << static_cast<int>(best.y0) << ' '
             << static_cast<int>(best.x1) << ' ' << static_cast<int>(best.y1) << ' '
             << static_cast<int>(best.x2) << ' ' << static_cast<int>(best.y2) << endl;
    } else if (!moves.empty()) {
        // 兜底：选择第一个移动
        Move best = moves[0];
        cout << static_cast<int>(best.x0) << ' ' << static_cast<int>(best.y0) << ' '
             << static_cast<int>(best.x1) << ' ' << static_cast<int>(best.y1) << ' '
             << static_cast<int>(best.x2) << ' ' << static_cast<int>(best.y2) << endl;
    } else {
        cout << "-1 -1 -1 -1 -1 -1\n";
    }
    
    // 调试信息（可以注释掉）
    cerr << "Simulations: " << sims_done 
         << ", Best visits: " << best_visits 
         << ", Best value: " << best_value
         << ", Moves: " << moves.size() << endl;
    
    return 0;
}