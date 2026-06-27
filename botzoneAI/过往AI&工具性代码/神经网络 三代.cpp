// amazons_bot_ultimate.cpp
// 此程序是用于botzone平台的amazons游戏的对战AI
// 终极版：完整KataGo神经网络 + 高级搜索 + 多种优化
// 编译：g++ -O3 -std=c++17 -march=native -pthread -o amazons_bot amazons_bot_ultimate.cpp -lz

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
#include <cassert>
#include <immintrin.h>
#include <queue>
#include <bitset>
#include <zlib.h>
#include <future>
#include <atomic>
#include <shared_mutex>
#include <mutex>
#include <optional>

using namespace std;
using namespace std::chrono;

// ==================== 配置文件 ====================
const std::string MODEL_PATH = "/data/amazons8x8.bin.gz";
constexpr bool USE_OPENING_BOOK = true;
constexpr int OPENING_MAX_MOVES = 12;
constexpr int TIME_LIMIT_MS = 950;
constexpr int SAFETY_MARGIN_MS = 50;
constexpr bool USE_PARALLEL_EVAL = true;
constexpr int PARALLEL_THREADS = 4;
constexpr bool USE_SYMMETRY = true;
constexpr bool USE_TABLEBASE = false;

// ==================== 棋盘常量 ====================
constexpr int GRIDSIZE = 8;
constexpr int BOARD_AREA = GRIDSIZE * GRIDSIZE;
constexpr int OBSTACLE = 2;
constexpr int grid_black = 1;
constexpr int grid_white = -1;
constexpr int EMPTY = 0;

int gridInfo[GRIDSIZE][GRIDSIZE] = {};

inline bool inMap(int x, int y) { 
    return x >= 0 && x < GRIDSIZE && y >= 0 && y < GRIDSIZE; 
}

// ==================== 核心处理函数 ====================
inline void ProcStep(int x0, int y0, int x1, int y1, int x2, int y2, int color) {
    gridInfo[x0][y0] = EMPTY;
    gridInfo[x1][y1] = color;
    gridInfo[x2][y2] = OBSTACLE;
}

inline void UndoStep(int x0, int y0, int x1, int y1, int x2, int y2, int color) {
    gridInfo[x2][y2] = EMPTY;
    gridInfo[x1][y1] = EMPTY;
    gridInfo[x0][y0] = color;
}

// ==================== SIMD优化 ====================
#ifdef __AVX2__
struct AVXFloat32 {
    static constexpr int ELEMENTS = 8;
    
    inline static void add(float* dst, const float* src, size_t n) {
        size_t i = 0;
        for (; i + ELEMENTS <= n; i += ELEMENTS) {
            __m256 a = _mm256_loadu_ps(dst + i);
            __m256 b = _mm256_loadu_ps(src + i);
            _mm256_storeu_ps(dst + i, _mm256_add_ps(a, b));
        }
        for (; i < n; ++i) dst[i] += src[i];
    }
    
    inline static void mul_add(float* dst, const float* src, float factor, size_t n) {
        __m256 factor_vec = _mm256_set1_ps(factor);
        size_t i = 0;
        for (; i + ELEMENTS <= n; i += ELEMENTS) {
            __m256 a = _mm256_loadu_ps(dst + i);
            __m256 b = _mm256_loadu_ps(src + i);
            __m256 c = _mm256_mul_ps(b, factor_vec);
            _mm256_storeu_ps(dst + i, _mm256_add_ps(a, c));
        }
        for (; i < n; ++i) dst[i] += src[i] * factor;
    }
    
    inline static void relu(float* data, size_t n) {
        __m256 zero = _mm256_setzero_ps();
        size_t i = 0;
        for (; i + ELEMENTS <= n; i += ELEMENTS) {
            __m256 x = _mm256_loadu_ps(data + i);
            _mm256_storeu_ps(data + i, _mm256_max_ps(x, zero));
        }
        for (; i < n; ++i) data[i] = max(data[i], 0.0f);
    }
    
    inline static float dot(const float* a, const float* b, size_t n) {
        __m256 sum = _mm256_setzero_ps();
        size_t i = 0;
        for (; i + ELEMENTS <= n; i += ELEMENTS) {
            __m256 va = _mm256_loadu_ps(a + i);
            __m256 vb = _mm256_loadu_ps(b + i);
            sum = _mm256_add_ps(sum, _mm256_mul_ps(va, vb));
        }
        alignas(32) float temp[ELEMENTS];
        _mm256_store_ps(temp, sum);
        float result = temp[0] + temp[1] + temp[2] + temp[3] + 
                      temp[4] + temp[5] + temp[6] + temp[7];
        for (; i < n; ++i) result += a[i] * b[i];
        return result;
    }
};
#endif

// ==================== 哈希函数 ====================
struct ZobristHash {
    uint64_t piece_hash[64][3];  // 位置×颜色
    uint64_t turn_hash;
    uint64_t move_hash[64][64];  // 移动历史哈希
    
    ZobristHash() {
        mt19937_64 rng(123456789);
        for (int i = 0; i < 64; ++i) {
            for (int j = 0; j < 3; ++j) {
                piece_hash[i][j] = rng();
            }
        }
        turn_hash = rng();
        for (int i = 0; i < 64; ++i) {
            for (int j = 0; j < 64; ++j) {
                move_hash[i][j] = rng();
            }
        }
    }
    
    uint64_t hashBoard(int player) const {
        uint64_t h = 0;
        for (int x = 0; x < GRIDSIZE; ++x) {
            for (int y = 0; y < GRIDSIZE; ++y) {
                int idx = x * GRIDSIZE + y;
                int piece = gridInfo[x][y];
                if (piece != EMPTY) {
                    int color_idx = (piece == OBSTACLE) ? 2 : (piece == grid_black ? 0 : 1);
                    h ^= piece_hash[idx][color_idx];
                }
            }
        }
        if (player == grid_black) h ^= turn_hash;
        return h;
    }
};

static ZobristHash zobrist;

// ==================== 神经网络配置 ====================
struct NetConfig {
    static constexpr int INPUT_CHANNELS = 22;
    static constexpr int NUM_BLOCKS = 18;
    static constexpr int NUM_FILTERS = 384;
    static constexpr int VALUE_CHANNELS = 32;
    static constexpr int VALUE_FC_SIZE = 256;
    static constexpr int POLICY_CHANNELS = 64;
    static constexpr int POLICY_MAP_SIZE = 4096;  // 8x8x8x8
    static constexpr int SE = 128;  // Squeeze-and-Excitation通道
    
    struct ResidualBlock {
        bool use_se;  // 是否使用SE模块
        bool gated;   // 是否使用门控激活
    };
    
    static constexpr ResidualBlock BLOCKS[NUM_BLOCKS] = {
        {false, false}, {false, false}, {false, false}, {false, false},
        {true, false}, {true, false}, {true, false}, {true, false},
        {true, true}, {true, true}, {true, true}, {true, true},
        {true, true}, {true, true}, {true, true}, {true, true},
        {true, true}, {true, true}
    };
    
    struct FileHeader {
        int32_t magic;
        int32_t version;
        int32_t num_blocks;
        int32_t num_filters;
        int32_t value_channels;
        int32_t policy_channels;
        int32_t se_channels;
        int32_t use_gate;
        int32_t reserved[8];
    };
};

// ==================== 高级权重加载器 ====================
class AdvancedWeightLoader {
private:
    struct ConvWeights {
        vector<float> weight;
        vector<float> bias;
        int in_channels;
        int out_channels;
        int kernel_size;
        bool use_bias;
        
        ConvWeights() : in_channels(0), out_channels(0), kernel_size(0), use_bias(true) {}  // 默认构造函数
        
        ConvWeights(int ic, int oc, int ks, bool ub = true) 
            : in_channels(ic), out_channels(oc), kernel_size(ks), use_bias(ub) {
            weight.resize(oc * ic * ks * ks);
            if (use_bias) bias.resize(oc);
        }
    };
    
    struct DenseWeights {
        vector<float> weight;
        vector<float> bias;
        int in_size;
        int out_size;
        
        DenseWeights() : in_size(0), out_size(0) {}  // 默认构造函数
    };
    
public:
    struct SEWeights {
        vector<float> fc1_w, fc1_b;
        vector<float> fc2_w, fc2_b;
        int channels;
        
        SEWeights() : channels(0) {}  // 默认构造函数
    };
    
private:
    unordered_map<string, ConvWeights> conv_layers;
    unordered_map<string, DenseWeights> dense_layers;
    unordered_map<string, SEWeights> se_layers;
    unordered_map<string, vector<float>> bn_layers;  // BatchNorm参数
    
    bool loaded = false;
    NetConfig::FileHeader header;
    
public:
    bool loadFromFile(const string& path) {
        ifstream fin(path, ios::binary);
        if (!fin) {
            cerr << "[WeightLoader] Cannot open model file: " << path << endl;
            return false;
        }
        
        // 读取文件头
        fin.read(reinterpret_cast<char*>(&header), sizeof(header));
        
        if (header.magic != 0x4B474E4E) {
            cerr << "[WeightLoader] Invalid model file format" << endl;
            return false;
        }
        
        cerr << "[WeightLoader] Loading b18c384 model v" << header.version 
             << " (blocks=" << header.num_blocks 
             << ", filters=" << header.num_filters 
             << ", se=" << header.se_channels 
             << ", gate=" << header.use_gate << ")" << endl;
        
        // 加载输入卷积
        loadConvLayer(fin, "input_conv", NetConfig::INPUT_CHANNELS, NetConfig::NUM_FILTERS, 3);
        
        // 加载残差块
        for (int i = 0; i < NetConfig::NUM_BLOCKS; ++i) {
            string prefix = "block" + to_string(i);
            
            // 第一个卷积
            loadConvLayer(fin, prefix + ".conv1", NetConfig::NUM_FILTERS, NetConfig::NUM_FILTERS, 3);
            
            // 第二个卷积
            loadConvLayer(fin, prefix + ".conv2", NetConfig::NUM_FILTERS, NetConfig::NUM_FILTERS, 3);
            
            // 如果有SE模块
            if (NetConfig::BLOCKS[i].use_se) {
                loadSELayer(fin, prefix + ".se", NetConfig::NUM_FILTERS, header.se_channels);
            }
            
            // 如果有门控激活
            if (NetConfig::BLOCKS[i].gated) {
                loadConvLayer(fin, prefix + ".gate", NetConfig::NUM_FILTERS, NetConfig::NUM_FILTERS, 1, false);
            }
            
            // BatchNorm参数
            loadBatchNorm(fin, prefix + ".bn1", NetConfig::NUM_FILTERS);
            loadBatchNorm(fin, prefix + ".bn2", NetConfig::NUM_FILTERS);
        }
        
        // 加载价值头
        loadConvLayer(fin, "value_conv", NetConfig::NUM_FILTERS, NetConfig::VALUE_CHANNELS, 1);
        loadDenseLayer(fin, "value_fc1", NetConfig::VALUE_CHANNELS * BOARD_AREA, NetConfig::VALUE_FC_SIZE);
        loadDenseLayer(fin, "value_fc2", NetConfig::VALUE_FC_SIZE, 3);  // win/loss/draw
        
        // 加载策略头
        loadConvLayer(fin, "policy_conv", NetConfig::NUM_FILTERS, NetConfig::POLICY_CHANNELS, 1);
        loadDenseLayer(fin, "policy_fc", NetConfig::POLICY_CHANNELS * BOARD_AREA, NetConfig::POLICY_MAP_SIZE);
        
        // 加载所有权头
        loadConvLayer(fin, "ownership_conv", NetConfig::NUM_FILTERS, 1, 1);
        
        loaded = true;
        cerr << "[WeightLoader] Full model loaded successfully" << endl;
        return true;
    }
    
    const ConvWeights* getConv(const string& name) const {
        auto it = conv_layers.find(name);
        return it != conv_layers.end() ? &it->second : nullptr;
    }
    
    const DenseWeights* getDense(const string& name) const {
        auto it = dense_layers.find(name);
        return it != dense_layers.end() ? &it->second : nullptr;
    }
    
    const SEWeights* getSE(const string& name) const {
        auto it = se_layers.find(name);
        return it != se_layers.end() ? &it->second : nullptr;
    }
    
    const vector<float>* getBN(const string& name) const {
        auto it = bn_layers.find(name);
        return it != bn_layers.end() ? &it->second : nullptr;
    }
    
    bool isLoaded() const { return loaded; }
    const NetConfig::FileHeader& getHeader() const { return header; }
    
private:
    void loadConvLayer(ifstream& fin, const string& name, int ic, int oc, int ks, bool use_bias = true) {
        ConvWeights layer;
        layer.in_channels = ic;
        layer.out_channels = oc;
        layer.kernel_size = ks;
        layer.use_bias = use_bias;
        layer.weight.resize(oc * ic * ks * ks);
        fin.read(reinterpret_cast<char*>(layer.weight.data()), layer.weight.size() * sizeof(float));
        if (use_bias) {
            layer.bias.resize(oc);
            fin.read(reinterpret_cast<char*>(layer.bias.data()), layer.bias.size() * sizeof(float));
        }
        conv_layers.emplace(name, std::move(layer));
    }
    
    void loadDenseLayer(ifstream& fin, const string& name, int in_sz, int out_sz) {
        DenseWeights layer;
        layer.in_size = in_sz;
        layer.out_size = out_sz;
        layer.weight.resize(in_sz * out_sz);
        layer.bias.resize(out_sz);
        fin.read(reinterpret_cast<char*>(layer.weight.data()), layer.weight.size() * sizeof(float));
        fin.read(reinterpret_cast<char*>(layer.bias.data()), layer.bias.size() * sizeof(float));
        dense_layers.emplace(name, std::move(layer));
    }
    
    void loadSELayer(ifstream& fin, const string& name, int channels, int se_channels) {
        SEWeights layer;
        layer.channels = channels;
        
        // FC1: channels -> se_channels
        layer.fc1_w.resize(channels * se_channels);
        layer.fc1_b.resize(se_channels);
        fin.read(reinterpret_cast<char*>(layer.fc1_w.data()), layer.fc1_w.size() * sizeof(float));
        fin.read(reinterpret_cast<char*>(layer.fc1_b.data()), layer.fc1_b.size() * sizeof(float));
        
        // FC2: se_channels -> channels
        layer.fc2_w.resize(se_channels * channels);
        layer.fc2_b.resize(channels);
        fin.read(reinterpret_cast<char*>(layer.fc2_w.data()), layer.fc2_w.size() * sizeof(float));
        fin.read(reinterpret_cast<char*>(layer.fc2_b.data()), layer.fc2_b.size() * sizeof(float));
        
        se_layers.emplace(name, std::move(layer));
    }
    
    void loadBatchNorm(ifstream& fin, const string& name, int channels) {
        vector<float> params(channels * 4);  // gamma, beta, mean, var
        fin.read(reinterpret_cast<char*>(params.data()), params.size() * sizeof(float));
        bn_layers.emplace(name, std::move(params));
    }
};

// ==================== 高级特征提取器 ====================
class AdvancedFeatureExtractor {
private:
    mutable vector<float> directional_reach[2][64];  // 缓存每个位置的方向可达性
    mutable uint64_t reach_hash[2] = {0, 0};
    
    void computeDirectionalReach(int player) const {
        int pidx = (player == grid_black) ? 0 : 1;
        uint64_t current_hash = zobrist.hashBoard(player);
        
        if (reach_hash[pidx] == current_hash) return;
        
        for (int idx = 0; idx < 64; ++idx) {
            directional_reach[pidx][idx].assign(8, 0);
        }
        
        int dirs[8][2] = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1}};
        
        for (int x = 0; x < GRIDSIZE; ++x) {
            for (int y = 0; y < GRIDSIZE; ++y) {
                if (gridInfo[x][y] == player) {
                    int idx = x * GRIDSIZE + y;
                    for (int d = 0; d < 8; ++d) {
                        int nx = x + dirs[d][0];
                        int ny = y + dirs[d][1];
                        int reach = 0;
                        while (inMap(nx, ny) && gridInfo[nx][ny] == EMPTY) {
                            reach++;
                            nx += dirs[d][0];
                            ny += dirs[d][1];
                        }
                        directional_reach[pidx][idx][d] = min(reach, 10) / 10.0f;
                    }
                }
            }
        }
        
        reach_hash[pidx] = current_hash;
    }
    
public:
    vector<float> extract(int player) const {
        vector<float> features(NetConfig::INPUT_CHANNELS * BOARD_AREA, 0.0f);
        
        computeDirectionalReach(player);
        computeDirectionalReach(-player);
        
        int pidx = (player == grid_black) ? 0 : 1;
        int oidx = 1 - pidx;
        
        // 通道0-7: 玩家棋子的8方向可达性
        // 通道8-15: 对手棋子的8方向可达性
        // 通道16-19: 棋子位置特征
        // 通道20: 移动次数特征
        // 通道21: 对称特征
        
        for (int y = 0; y < GRIDSIZE; ++y) {
            for (int x = 0; x < GRIDSIZE; ++x) {
                int idx = y * GRIDSIZE + x;
                int piece = gridInfo[x][y];
                int pos_idx = x * GRIDSIZE + y;
                
                // 通道0-7: 玩家方向可达性
                if (piece == player) {
                    for (int d = 0; d < 8; ++d) {
                        features[d * BOARD_AREA + idx] = directional_reach[pidx][pos_idx][d];
                    }
                }
                
                // 通道8-15: 对手方向可达性
                if (piece == -player) {
                    for (int d = 0; d < 8; ++d) {
                        features[(8 + d) * BOARD_AREA + idx] = directional_reach[oidx][pos_idx][d];
                    }
                }
                
                // 通道16: 玩家棋子
                if (piece == player) {
                    features[16 * BOARD_AREA + idx] = 1.0f;
                }
                
                // 通道17: 对手棋子
                if (piece == -player) {
                    features[17 * BOARD_AREA + idx] = 1.0f;
                }
                
                // 通道18: 障碍物
                if (piece == OBSTACLE) {
                    features[18 * BOARD_AREA + idx] = 1.0f;
                }
                
                // 通道19: 空位
                if (piece == EMPTY) {
                    features[19 * BOARD_AREA + idx] = 1.0f;
                }
                
                // 通道20: 中心区域（3x3中心）
                if (x >= 2 && x <= 5 && y >= 2 && y <= 5) {
                    features[20 * BOARD_AREA + idx] = 1.0f;
                }
                
                // 通道21: 边缘区域
                if (x == 0 || x == 7 || y == 0 || y == 7) {
                    features[21 * BOARD_AREA + idx] = 1.0f;
                }
            }
        }
        
        return features;
    }
    
    // 对称变换特征
    vector<float> extractWithSymmetry(int player, int symmetry) const {
        auto features = extract(player);
        if (symmetry == 0) return features;
        
        vector<float> sym_features(features.size());
        
        bool flip_x = (symmetry & 1) != 0;
        bool flip_y = (symmetry & 2) != 0;
        bool transpose = (symmetry & 4) != 0;
        
        for (int c = 0; c < NetConfig::INPUT_CHANNELS; ++c) {
            for (int y = 0; y < GRIDSIZE; ++y) {
                for (int x = 0; x < GRIDSIZE; ++x) {
                    int src_x = x, src_y = y;
                    
                    if (flip_x) src_x = GRIDSIZE - 1 - src_x;
                    if (flip_y) src_y = GRIDSIZE - 1 - src_y;
                    if (transpose) swap(src_x, src_y);
                    
                    int src_idx = src_y * GRIDSIZE + src_x;
                    int dst_idx = y * GRIDSIZE + x;
                    
                    sym_features[c * BOARD_AREA + dst_idx] = features[c * BOARD_AREA + src_idx];
                }
            }
        }
        
        return sym_features;
    }
};

// ==================== 并行神经网络引擎 ====================
class ParallelNeuralEngine {
private:
    AdvancedWeightLoader& weights;
    AdvancedFeatureExtractor extractor;
    mutable shared_mutex cache_mutex;
    unordered_map<uint64_t, tuple<float, vector<float>, vector<float>>> cache;
    
    // 快速卷积实现
    void conv3x3_avx(const float* input, float* output,
                    const vector<float>& weight, const vector<float>& bias,
                    int in_c, int out_c, int size) const {
        // 使用普通的并行循环，移除OpenMP指令
        for (int oc = 0; oc < out_c; ++oc) {
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    float sum = bias.empty() ? 0.0f : bias[oc];
                    
                    for (int ic = 0; ic < in_c; ++ic) {
                        const float* w_ptr = &weight[((oc * in_c + ic) * 3) * 3];
                        const float* i_ptr = &input[ic * size * size + max(0, y-1) * size];
                        
                        for (int dy = -1; dy <= 1; ++dy) {
                            int yy = y + dy;
                            if (yy < 0 || yy >= size) continue;
                            
                            for (int dx = -1; dx <= 1; ++dx) {
                                int xx = x + dx;
                                if (xx < 0 || xx >= size) continue;
                                
                                sum += i_ptr[yy * size + xx] * w_ptr[(dy+1)*3 + (dx+1)];
                            }
                        }
                    }
                    
                    output[oc * size * size + y * size + x] = max(0.0f, sum);
                }
            }
        }
    }
    
    void conv1x1_avx(const float* input, float* output,
                    const vector<float>& weight, const vector<float>& bias,
                    int in_c, int out_c, int size) const {
        int spatial = size * size;
        
        for (int oc = 0; oc < out_c; ++oc) {
            float b = bias.empty() ? 0.0f : bias[oc];
            const float* w_ptr = &weight[oc * in_c];
            
            #ifdef __AVX2__
            for (int i = 0; i < spatial; i += 8) {
                __m256 sum = _mm256_set1_ps(b);
                
                for (int ic = 0; ic < in_c; ++ic) {
                    __m256 w = _mm256_set1_ps(w_ptr[ic]);
                    __m256 in_val = _mm256_loadu_ps(&input[ic * spatial + i]);
                    sum = _mm256_add_ps(sum, _mm256_mul_ps(w, in_val));
                }
                
                _mm256_storeu_ps(&output[oc * spatial + i], _mm256_max_ps(sum, _mm256_setzero_ps()));
            }
            #else
            for (int i = 0; i < spatial; ++i) {
                float sum = b;
                for (int ic = 0; ic < in_c; ++ic) {
                    sum += input[ic * spatial + i] * w_ptr[ic];
                }
                output[oc * spatial + i] = max(0.0f, sum);
            }
            #endif
        }
    }
    
    void se_block(float* features, const AdvancedWeightLoader::SEWeights* se, int channels, int size) const {
        if (!se) return;
        
        int spatial = size * size;
        vector<float> squeeze(channels, 0.0f);
        
        // 全局平均池化
        float scale = 1.0f / spatial;
        for (int c = 0; c < channels; ++c) {
            float sum = 0.0f;
            for (int i = 0; i < spatial; ++i) {
                sum += features[c * spatial + i];
            }
            squeeze[c] = sum * scale;
        }
        
        // 第一个全连接层 + ReLU
        vector<float> excite(se->fc1_b.size(), 0.0f);
        for (size_t i = 0; i < excite.size(); ++i) {
            float sum = se->fc1_b[i];
            for (int c = 0; c < channels; ++c) {
                sum += squeeze[c] * se->fc1_w[i * channels + c];
            }
            excite[i] = max(0.0f, sum);
        }
        
        // 第二个全连接层 + Sigmoid
        vector<float> scale_vec(channels, 0.0f);
        for (int c = 0; c < channels; ++c) {
            float sum = se->fc2_b[c];
            for (size_t i = 0; i < excite.size(); ++i) {
                sum += excite[i] * se->fc2_w[c * se->fc1_b.size() + i];
            }
            scale_vec[c] = 1.0f / (1.0f + exp(-sum));  // Sigmoid
        }
        
        // 应用缩放
        for (int c = 0; c < channels; ++c) {
            float scale = scale_vec[c];
            for (int i = 0; i < spatial; ++i) {
                features[c * spatial + i] *= scale;
            }
        }
    }
    
    void gated_activation(float* features, const vector<float>* gate_weights, int channels, int size) const {
        if (!gate_weights) return;
        
        int spatial = size * size;
        vector<float> gate(channels * spatial);
        
        // 计算门控值（使用1x1卷积）
        const auto& weights = *gate_weights;
        for (int c = 0; c < channels; ++c) {
            for (int i = 0; i < spatial; ++i) {
                float sum = 0.0f;
                for (int ic = 0; ic < channels; ++ic) {
                    sum += features[ic * spatial + i] * weights[c * channels + ic];
                }
                gate[c * spatial + i] = 1.0f / (1.0f + exp(-sum));  // Sigmoid
            }
        }
        
        // 应用门控
        for (int i = 0; i < channels * spatial; ++i) {
            features[i] *= gate[i];
        }
    }
    
    void batch_norm(float* features, const vector<float>* bn_params, int channels, int size) const {
        if (!bn_params) return;
        
        int spatial = size * size;
        const float* gamma = bn_params->data();
        const float* beta = gamma + channels;
        const float* mean = beta + channels;
        const float* var = mean + channels;
        
        for (int c = 0; c < channels; ++c) {
            float g = gamma[c];
            float b = beta[c];
            float m = mean[c];
            float v = var[c];
            float scale = g / sqrt(v + 1e-5f);
            float shift = b - m * scale;
            
            for (int i = 0; i < spatial; ++i) {
                int idx = c * spatial + i;
                features[idx] = features[idx] * scale + shift;
            }
        }
    }
    
public:
    ParallelNeuralEngine(AdvancedWeightLoader& w) : weights(w) {}
    
    tuple<float, vector<float>, vector<float>> evaluate(int player, int symmetry = 0) {
        uint64_t hash = zobrist.hashBoard(player) ^ (uint64_t(symmetry) << 56);
        
        // 检查缓存
        {
            shared_lock<shared_mutex> lock(cache_mutex);
            auto it = cache.find(hash);
            if (it != cache.end()) {
                return it->second;
            }
        }
        
        // 提取特征
        auto features = extractor.extractWithSymmetry(player, symmetry);
        
        // 输入卷积
        auto* input_conv = weights.getConv("input_conv");
        if (!input_conv) {
            return {fallbackEvaluation(player), {}, {}};
        }
        
        vector<float> hidden(NetConfig::NUM_FILTERS * BOARD_AREA);
        conv3x3_avx(features.data(), hidden.data(), 
                   input_conv->weight, input_conv->bias,
                   NetConfig::INPUT_CHANNELS, NetConfig::NUM_FILTERS, GRIDSIZE);
        
        // 残差块
        for (int i = 0; i < NetConfig::NUM_BLOCKS; ++i) {
            string prefix = "block" + to_string(i);
            
            // 第一个卷积 + BN
            auto* conv1 = weights.getConv(prefix + ".conv1");
            auto* bn1 = weights.getBN(prefix + ".bn1");
            
            vector<float> temp1(NetConfig::NUM_FILTERS * BOARD_AREA);
            conv3x3_avx(hidden.data(), temp1.data(),
                       conv1->weight, conv1->bias,
                       NetConfig::NUM_FILTERS, NetConfig::NUM_FILTERS, GRIDSIZE);
            
            if (bn1) batch_norm(temp1.data(), bn1, NetConfig::NUM_FILTERS, GRIDSIZE);
            AVXFloat32::relu(temp1.data(), temp1.size());
            
            // 第二个卷积 + BN
            auto* conv2 = weights.getConv(prefix + ".conv2");
            auto* bn2 = weights.getBN(prefix + ".bn2");
            
            vector<float> temp2(NetConfig::NUM_FILTERS * BOARD_AREA);
            conv3x3_avx(temp1.data(), temp2.data(),
                       conv2->weight, conv2->bias,
                       NetConfig::NUM_FILTERS, NetConfig::NUM_FILTERS, GRIDSIZE);
            
            if (bn2) batch_norm(temp2.data(), bn2, NetConfig::NUM_FILTERS, GRIDSIZE);
            
            // SE模块
            if (NetConfig::BLOCKS[i].use_se) {
                auto* se = weights.getSE(prefix + ".se");
                se_block(temp2.data(), se, NetConfig::NUM_FILTERS, GRIDSIZE);
            }
            
            // 门控激活
            if (NetConfig::BLOCKS[i].gated) {
                auto* gate = weights.getConv(prefix + ".gate");
                if (gate) {
                    gated_activation(temp2.data(), &gate->weight, NetConfig::NUM_FILTERS, GRIDSIZE);
                }
            }
            
            // 残差连接 + ReLU
            for (size_t j = 0; j < temp2.size(); ++j) {
                temp2[j] = max(0.0f, temp2[j] + hidden[j]);
            }
            
            hidden.swap(temp2);
        }
        
        // 价值头
        auto* value_conv = weights.getConv("value_conv");
        auto* value_fc1 = weights.getDense("value_fc1");
        auto* value_fc2 = weights.getDense("value_fc2");
        
        vector<float> value_feat(NetConfig::VALUE_CHANNELS * BOARD_AREA);
        conv1x1_avx(hidden.data(), value_feat.data(),
                   value_conv->weight, value_conv->bias,
                   NetConfig::NUM_FILTERS, NetConfig::VALUE_CHANNELS, GRIDSIZE);
        AVXFloat32::relu(value_feat.data(), value_feat.size());
        
        // 展平
        vector<float> flattened(value_feat.begin(), value_feat.end());
        
        vector<float> fc1_out(value_fc1->out_size);
        dense_layer(flattened.data(), fc1_out.data(),
                   value_fc1->weight, value_fc1->bias,
                   value_fc1->in_size, value_fc1->out_size);
        AVXFloat32::relu(fc1_out.data(), fc1_out.size());
        
        vector<float> value_out(value_fc2->out_size);
        dense_layer(fc1_out.data(), value_out.data(),
                   value_fc2->weight, value_fc2->bias,
                   value_fc2->in_size, value_fc2->out_size);
        
        // Softmax得到价值
        vector<float> value_probs = softmax(value_out);
        float win_prob = value_probs[0];
        float loss_prob = value_probs[1];
        float draw_prob = value_probs[2];
        float value = win_prob - loss_prob + draw_prob * 0.1f;  // 平局有小的正价值
        
        // 策略头
        auto* policy_conv = weights.getConv("policy_conv");
        auto* policy_fc = weights.getDense("policy_fc");
        
        vector<float> policy_feat(NetConfig::POLICY_CHANNELS * BOARD_AREA);
        conv1x1_avx(hidden.data(), policy_feat.data(),
                   policy_conv->weight, policy_conv->bias,
                   NetConfig::NUM_FILTERS, NetConfig::POLICY_CHANNELS, GRIDSIZE);
        AVXFloat32::relu(policy_feat.data(), policy_feat.size());
        
        vector<float> policy_flat(policy_feat.begin(), policy_feat.end());
        vector<float> policy_logits(policy_fc->out_size);
        dense_layer(policy_flat.data(), policy_logits.data(),
                   policy_fc->weight, policy_fc->bias,
                   policy_fc->in_size, policy_fc->out_size);
        
        vector<float> policy_probs = softmax(policy_logits);
        
        // 所有权头
        auto* ownership_conv = weights.getConv("ownership_conv");
        vector<float> ownership(BOARD_AREA);
        conv1x1_avx(hidden.data(), ownership.data(),
                   ownership_conv->weight, ownership_conv->bias,
                   NetConfig::NUM_FILTERS, 1, GRIDSIZE);
        
        // 缓存结果
        auto result = make_tuple(value, policy_probs, ownership);
        {
            unique_lock<shared_mutex> lock(cache_mutex);
            cache[hash] = result;
            if (cache.size() > 10000) cache.clear();  // 防止内存占用过多
        }
        
        return result;
    }
    
    // 批量评估多个对称性
    vector<tuple<float, vector<float>, vector<float>>> evaluateAllSymmetries(int player) {
        vector<tuple<float, vector<float>, vector<float>>> results;
        vector<future<tuple<float, vector<float>, vector<float>>>> futures;
        
        int symmetries = USE_SYMMETRY ? 8 : 1;
        
        for (int s = 0; s < symmetries; ++s) {
            if (USE_PARALLEL_EVAL) {
                futures.push_back(async(launch::async, [this, player, s]() {
                    return evaluate(player, s);
                }));
            } else {
                results.push_back(evaluate(player, s));
            }
        }
        
        if (USE_PARALLEL_EVAL) {
            for (auto& f : futures) {
                results.push_back(f.get());
            }
        }
        
        return results;
    }
    
private:
    void dense_layer(const float* input, float* output,
                    const vector<float>& weight, const vector<float>& bias,
                    int in_size, int out_size) const {
        for (int i = 0; i < out_size; ++i) {
            float sum = bias[i];
            #ifdef __AVX2__
            int j = 0;
            __m256 sum_vec = _mm256_setzero_ps();
            for (; j + 8 <= in_size; j += 8) {
                __m256 w = _mm256_set1_ps(weight[i * in_size + j]);
                __m256 in_val = _mm256_loadu_ps(&input[j]);
                sum_vec = _mm256_add_ps(sum_vec, _mm256_mul_ps(w, in_val));
            }
            alignas(32) float temp[8];
            _mm256_store_ps(temp, sum_vec);
            sum += temp[0] + temp[1] + temp[2] + temp[3] + 
                  temp[4] + temp[5] + temp[6] + temp[7];
            for (; j < in_size; ++j) {
                sum += weight[i * in_size + j] * input[j];
            }
            #else
            for (int j = 0; j < in_size; ++j) {
                sum += weight[i * in_size + j] * input[j];
            }
            #endif
            output[i] = sum;
        }
    }
    
    vector<float> softmax(const vector<float>& logits) const {
        vector<float> probs(logits.size());
        float max_val = *max_element(logits.begin(), logits.end());
        float sum = 0.0f;
        
        for (size_t i = 0; i < logits.size(); ++i) {
            probs[i] = exp(logits[i] - max_val);
            sum += probs[i];
        }
        
        if (sum > 0) {
            for (size_t i = 0; i < probs.size(); ++i) {
                probs[i] /= sum;
            }
        }
        
        return probs;
    }
    
    float fallbackEvaluation(int player) const {
        int my_moves = 0, opp_moves = 0;
        int my_territory = 0, opp_territory = 0;
        int my_center = 0, opp_center = 0;
        
        // 预计算移动性
        int dirs[8][2] = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1}};
        
        for (int x = 0; x < GRIDSIZE; ++x) {
            for (int y = 0; y < GRIDSIZE; ++y) {
                if (gridInfo[x][y] == player) {
                    // 移动性
                    for (int d = 0; d < 8; ++d) {
                        int nx = x + dirs[d][0];
                        int ny = y + dirs[d][1];
                        while (inMap(nx, ny) && gridInfo[nx][ny] == EMPTY) {
                            my_moves++;
                            nx += dirs[d][0];
                            ny += dirs[d][1];
                        }
                    }
                    
                    // 中心控制
                    int dist_to_center = abs(x - 3.5) + abs(y - 3.5);
                    my_center += (14 - dist_to_center);
                    
                    // 领地估算
                    my_territory += estimateTerritory(x, y, player);
                } else if (gridInfo[x][y] == -player) {
                    for (int d = 0; d < 8; ++d) {
                        int nx = x + dirs[d][0];
                        int ny = y + dirs[d][1];
                        while (inMap(nx, ny) && gridInfo[nx][ny] == EMPTY) {
                            opp_moves++;
                            nx += dirs[d][0];
                            ny += dirs[d][1];
                        }
                    }
                    
                    int dist_to_center = abs(x - 3.5) + abs(y - 3.5);
                    opp_center += (14 - dist_to_center);
                    opp_territory += estimateTerritory(x, y, -player);
                }
            }
        }
        
        float score = 0.0f;
        score += (my_moves - opp_moves) * 0.02f;
        score += (my_center - opp_center) * 0.01f;
        score += (my_territory - opp_territory) * 0.05f;
        
        return tanh(score);
    }
    
    int estimateTerritory(int x, int y, int player) const {
        int territory = 0;
        bool visited[GRIDSIZE][GRIDSIZE] = {false};
        queue<pair<int, int>> q;
        q.push({x, y});
        visited[x][y] = true;
        
        int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        
        while (!q.empty()) {
            auto [cx, cy] = q.front();
            q.pop();
            territory++;
            
            for (int d = 0; d < 4; ++d) {
                int nx = cx + dirs[d][0];
                int ny = cy + dirs[d][1];
                if (inMap(nx, ny) && !visited[nx][ny] && gridInfo[nx][ny] == EMPTY) {
                    bool reachable = true;
                    // 检查是否可到达（不被障碍物阻挡）
                    int dx = nx - x, dy = ny - y;
                    if (dx != 0 && dy != 0 && abs(dx) != abs(dy)) continue;
                    
                    int step_x = dx == 0 ? 0 : (dx > 0 ? 1 : -1);
                    int step_y = dy == 0 ? 0 : (dy > 0 ? 1 : -1);
                    int steps = max(abs(dx), abs(dy));
                    int sx = x + step_x, sy = y + step_y;
                    
                    for (int i = 1; i < steps; ++i) {
                        if (gridInfo[sx][sy] == OBSTACLE) {
                            reachable = false;
                            break;
                        }
                        sx += step_x;
                        sy += step_y;
                    }
                    
                    if (reachable) {
                        visited[nx][ny] = true;
                        q.push({nx, ny});
                    }
                }
            }
        }
        
        return territory;
    }
};

// ==================== 移动生成和验证 ====================
struct Move {
    uint8_t x0, y0, x1, y1, x2, y2;
    float prior;
    float value;
    int visits;
    
    Move() : x0(0), y0(0), x1(0), y1(0), x2(0), y2(0), prior(0), value(0), visits(0) {}
    Move(int x0, int y0, int x1, int y1, int x2, int y2)
        : x0(x0), y0(y0), x1(x1), y1(y1), x2(x2), y2(y2), prior(0), value(0), visits(0) {}
    
    bool operator==(const Move& other) const {
        return x0 == other.x0 && y0 == other.y0 &&
               x1 == other.x1 && y1 == other.y1 &&
               x2 == other.x2 && y2 == other.y2;
    }
    
    void apply(int color) const { ProcStep(x0, y0, x1, y1, x2, y2, color); }
    void undo(int color) const { UndoStep(x0, y0, x1, y1, x2, y2, color); }
    
    void print() const {
        cout << (int)x0 << " " << (int)y0 << " "
             << (int)x1 << " " << (int)y1 << " "
             << (int)x2 << " " << (int)y2 << endl;
    }
    
    // 转换为策略索引
    int toPolicyIndex() const {
        // 编码：起点(6位) + 移动方向(3位) + 距离(3位) + 箭方向(3位) + 箭距离(3位)
        int dx1 = x1 - x0, dy1 = y1 - y0;
        int dx2 = x2 - x1, dy2 = y2 - y1;
        
        int dir1 = directionIndex(dx1, dy1);
        int dir2 = directionIndex(dx2, dy2);
        int dist1 = max(abs(dx1), abs(dy1));
        int dist2 = max(abs(dx2), abs(dy2));
        
        int idx = (x0 * 8 + y0) << 12;
        idx |= (dir1 << 9) | (dist1 << 6) | (dir2 << 3) | dist2;
        return idx % NetConfig::POLICY_MAP_SIZE;
    }
    
private:
    int directionIndex(int dx, int dy) const {
        if (dx == 0 && dy > 0) return 0;
        if (dx == 0 && dy < 0) return 1;
        if (dx > 0 && dy == 0) return 2;
        if (dx < 0 && dy == 0) return 3;
        if (dx > 0 && dy > 0) return 4;
        if (dx > 0 && dy < 0) return 5;
        if (dx < 0 && dy > 0) return 6;
        if (dx < 0 && dy < 0) return 7;
        return 0;
    }
};

class MoveGenerator {
private:
    static constexpr int MAX_MOVES = 2000;
    uint64_t between_masks[64][64];
    bool line_valid[64][64];
    bool initialized = false;
    
public:
    MoveGenerator() {
        if (!initialized) {
            initMasks();
            initialized = true;
        }
    }
    
    vector<Move> generate(int color) const {
        vector<Move> moves;
        moves.reserve(MAX_MOVES);
        
        uint64_t occ = 0;
        for (int x = 0; x < GRIDSIZE; ++x)
            for (int y = 0; y < GRIDSIZE; ++y)
                if (gridInfo[x][y] != 0)
                    occ |= (1ULL << (x * 8 + y));
        
        for (int x0 = 0; x0 < GRIDSIZE; ++x0) {
            for (int y0 = 0; y0 < GRIDSIZE; ++y0) {
                if (gridInfo[x0][y0] != color) continue;
                
                int idx0 = x0 * 8 + y0;
                
                for (int d1 = 0; d1 < 8; ++d1) {
                    int x1 = x0, y1 = y0;
                    
                    while (true) {
                        x1 += (d1 == 2 || d1 == 4 || d1 == 5) ? 1 : (d1 == 3 || d1 == 6 || d1 == 7) ? -1 : 0;
                        y1 += (d1 == 0 || d1 == 4 || d1 == 6) ? 1 : (d1 == 1 || d1 == 5 || d1 == 7) ? -1 : 0;
                        
                        if (!inMap(x1, y1) || gridInfo[x1][y1] != EMPTY) break;
                        
                        int idx1 = x1 * 8 + y1;
                        uint64_t temp_occ = occ;
                        temp_occ &= ~(1ULL << idx0);
                        temp_occ |= (1ULL << idx1);
                        
                        for (int d2 = 0; d2 < 8; ++d2) {
                            int x2 = x1, y2 = y1;
                            
                            while (true) {
                                x2 += (d2 == 2 || d2 == 4 || d2 == 5) ? 1 : (d2 == 3 || d2 == 6 || d2 == 7) ? -1 : 0;
                                y2 += (d2 == 0 || d2 == 4 || d2 == 6) ? 1 : (d2 == 1 || d2 == 5 || d2 == 7) ? -1 : 0;
                                
                                if (!inMap(x2, y2)) break;
                                
                                if (gridInfo[x2][y2] == EMPTY || (x2 == x0 && y2 == y0)) {
                                    int idx2 = x2 * 8 + y2;
                                    if (lineClear(temp_occ, idx1, idx2)) {
                                        moves.emplace_back(x0, y0, x1, y1, x2, y2);
                                    }
                                } else {
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        return moves;
    }
    
    bool isLegal(const Move& move, int color) const {
        if (!inMap(move.x0, move.y0) || gridInfo[move.x0][move.y0] != color) return false;
        if (!inMap(move.x1, move.y1) || gridInfo[move.x1][move.y1] != EMPTY) return false;
        if (!inMap(move.x2, move.y2)) return false;
        if (gridInfo[move.x2][move.y2] != EMPTY && !(move.x2 == move.x0 && move.y2 == move.y0)) return false;
        
        // 检查移动路径
        if (!pathClear(move.x0, move.y0, move.x1, move.y1)) return false;
        
        // 检查箭路径
        uint64_t occ = 0;
        for (int x = 0; x < GRIDSIZE; ++x)
            for (int y = 0; y < GRIDSIZE; ++y)
                if (gridInfo[x][y] != 0)
                    occ |= (1ULL << (x * 8 + y));
        
        occ &= ~(1ULL << (move.x0 * 8 + move.y0));
        occ |= (1ULL << (move.x1 * 8 + move.y1));
        
        return lineClear(occ, move.x1 * 8 + move.y1, move.x2 * 8 + move.y2);
    }
    
private:
    void initMasks() {
        for (int i = 0; i < 64; ++i) {
            for (int j = 0; j < 64; ++j) {
                between_masks[i][j] = 0;
                line_valid[i][j] = false;
                
                int x1 = i / 8, y1 = i % 8;
                int x2 = j / 8, y2 = j % 8;
                int dx = x2 - x1, dy = y2 - y1;
                
                if (dx == 0 && dy == 0) continue;
                
                if (dx == 0 || dy == 0 || abs(dx) == abs(dy)) {
                    line_valid[i][j] = true;
                    int step_x = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
                    int step_y = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
                    int steps = max(abs(dx), abs(dy));
                    int x = x1 + step_x, y = y1 + step_y;
                    
                    for (int k = 1; k < steps; ++k) {
                        between_masks[i][j] |= (1ULL << (x * 8 + y));
                        x += step_x;
                        y += step_y;
                    }
                }
            }
        }
    }
    
    bool lineClear(uint64_t occ, int idx1, int idx2) const {
        if (!line_valid[idx1][idx2]) return false;
        return (occ & between_masks[idx1][idx2]) == 0;
    }
    
    bool pathClear(int x0, int y0, int x1, int y1) const {
        int dx = x1 - x0, dy = y1 - y0;
        if (dx == 0 && dy == 0) return true;
        
        int step_x = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
        int step_y = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
        int steps = max(abs(dx), abs(dy));
        
        int x = x0 + step_x, y = y0 + step_y;
        for (int i = 1; i < steps; ++i) {
            if (!inMap(x, y) || gridInfo[x][y] != EMPTY) return false;
            x += step_x;
            y += step_y;
        }
        return true;
    }
};

// ==================== 高级MCTS节点 ====================
struct AdvancedMCTSNode {
    Move move;
    float value_sum = 0;
    float value_sq_sum = 0;
    int visits = 0;
    float prior = 0;
    float virtual_loss = 0;
    bool expanded = false;
    bool terminal = false;
    vector<AdvancedMCTSNode*> children;
    AdvancedMCTSNode* parent = nullptr;
    
    ~AdvancedMCTSNode() {
        for (auto child : children) delete child;
    }
    
    float getValue() const {
        return visits > 0 ? value_sum / visits : 0;
    }
    
    float getVariance() const {
        if (visits <= 1) return 1.0f;
        float mean = getValue();
        return value_sq_sum / visits - mean * mean;
    }
    
    float getUCB(float c_puct, float parent_log_visits) const {
        if (visits == 0) return 1e9f + getVariance();  // 未访问节点优先
        float exploit = getValue() - virtual_loss;
        float explore = c_puct * prior * sqrtf(parent_log_visits) / (1.0f + visits);
        float variance_bonus = 0.1f * sqrtf(getVariance());
        return exploit + explore + variance_bonus;
    }
    
    AdvancedMCTSNode* selectBestChild(float c_puct) {
        float best_score = -1e9f;
        AdvancedMCTSNode* best_child = nullptr;
        float parent_log = logf(visits + 1e-6f);
        
        for (auto child : children) {
            float score = child->getUCB(c_puct, parent_log);
            if (score > best_score) {
                best_score = score;
                best_child = child;
            }
        }
        return best_child;
    }
    
    void update(float value) {
        visits++;
        value_sum += value;
        value_sq_sum += value * value;
        virtual_loss = 0;
        
        // 向上传播
        AdvancedMCTSNode* node = parent;
        while (node) {
            node->visits++;
            node->value_sum -= value;  // 对手视角
            node->value_sq_sum += value * value;
            value = -value;
            node = node->parent;
        }
    }
    
    void addVirtualLoss() {
        virtual_loss += 1.0f;
        AdvancedMCTSNode* node = parent;
        while (node) {
            node->virtual_loss += 1.0f;
            node = node->parent;
        }
    }
    
    void revertVirtualLoss() {
        virtual_loss -= 1.0f;
        AdvancedMCTSNode* node = parent;
        while (node) {
            node->virtual_loss -= 1.0f;
            node = node->parent;
        }
    }
};

// ==================== 并行MCTS搜索器 ====================
class ParallelMCTSSearcher {
private:
    ParallelNeuralEngine& engine;
    MoveGenerator move_gen;
    vector<AdvancedMCTSNode*> node_pool;
    atomic<int> simulation_counter{0};
    
    // 搜索参数
    float c_puct_base = 1.5f;
    float c_puct_init = 2.5f;
    float dirichlet_alpha = 0.3f;
    float dirichlet_epsilon = 0.25f;
    int virtual_loss = 3;
    float fpu_reduction = 0.2f;
    
public:
    ParallelMCTSSearcher(ParallelNeuralEngine& eng) 
        : engine(eng) {}
    
    Move search(int player, int moveCount, int timeBudgetMs) {
        auto start_time = steady_clock::now();
        
        // 1. 生成合法移动
        auto legal_moves = move_gen.generate(player);
        if (legal_moves.empty()) return Move();
        
        // 2. 神经网络评估所有对称性
        auto eval_results = engine.evaluateAllSymmetries(player);
        
        // 3. 融合对称性结果
        float avg_value = 0;
        vector<float> avg_policy(NetConfig::POLICY_MAP_SIZE, 0);
        vector<float> avg_ownership(BOARD_AREA, 0);
        
        for (const auto& eval_result : eval_results) {
            float value = get<0>(eval_result);
            const auto& policy = get<1>(eval_result);
            const auto& ownership = get<2>(eval_result);
            
            avg_value += value;
            for (size_t i = 0; i < policy.size(); ++i) avg_policy[i] += policy[i];
            for (size_t i = 0; i < ownership.size(); ++i) avg_ownership[i] += ownership[i];
        }
        
        float scale = 1.0f / eval_results.size();
        avg_value *= scale;
        for (auto& v : avg_policy) v *= scale;
        for (auto& v : avg_ownership) v *= scale;
        
        // 4. 创建根节点
        AdvancedMCTSNode* root = new AdvancedMCTSNode();
        root->visits = 1;
        
        // 5. 创建子节点
        unordered_map<int, vector<Move>> policy_groups;
        for (const auto& move : legal_moves) {
            int idx = move.toPolicyIndex();
            policy_groups[idx].push_back(move);
        }
        
        for (const auto& move : legal_moves) {
            AdvancedMCTSNode* child = new AdvancedMCTSNode();
            child->move = move;
            child->parent = root;
            
            // 计算先验概率
            int idx = move.toPolicyIndex();
            float group_size = policy_groups[idx].size();
            child->prior = (avg_policy[idx] / group_size) * (1.0f - dirichlet_epsilon);
            
            root->children.push_back(child);
        }
        
        // 添加Dirichlet噪声
        addDirichletNoise(root);
        
        // 6. 并行搜索
        int check_interval = 50;
        
        vector<thread> workers;
        atomic<bool> stop_flag(false);
        atomic<int> active_workers(0);
        
        auto worker_func = [&](int thread_id) {
            active_workers++;
            mt19937 rng(thread_id + time(nullptr));
            
            while (!stop_flag) {
                // 选择
                vector<AdvancedMCTSNode*> path;
                AdvancedMCTSNode* node = root;
                int cur_player = player;
                
                path.push_back(node);
                node->addVirtualLoss();
                
                while (node->expanded && !node->terminal && !node->children.empty()) {
                    AdvancedMCTSNode* child = node->selectBestChild(
                        c_puct_base + (c_puct_init - c_puct_base) * 
                        exp(-node->visits / 1000.0f)
                    );
                    
                    if (!child) break;
                    
                    child->addVirtualLoss();
                    child->move.apply(cur_player);
                    path.push_back(child);
                    
                    node = child;
                    cur_player = -cur_player;
                }
                
                // 扩展和评估
                float value;
                if (node->terminal) {
                    value = -1.0f;
                } else if (!node->expanded) {
                    // 检查游戏是否结束
                    auto next_moves = move_gen.generate(cur_player);
                    if (next_moves.empty()) {
                        node->terminal = true;
                        value = -1.0f;  // 无棋可走，输
                    } else {
                        // 神经网络评估
                        auto eval_result = engine.evaluate(cur_player);
                        value = get<0>(eval_result);
                        
                        // 创建子节点
                        unordered_map<int, vector<Move>> next_groups;
                        for (const auto& move : next_moves) {
                            int idx = move.toPolicyIndex();
                            next_groups[idx].push_back(move);
                        }
                        
                        for (const auto& move : next_moves) {
                            AdvancedMCTSNode* child = new AdvancedMCTSNode();
                            child->move = move;
                            child->parent = node;
                            
                            int idx = move.toPolicyIndex();
                            float group_size = next_groups[idx].size();
                            child->prior = get<1>(eval_result)[idx] / group_size;
                            
                            node->children.push_back(child);
                        }
                        
                        node->expanded = true;
                    }
                } else {
                    // 快速滚到底
                    value = fastRollout(cur_player, 10, rng);
                }
                
                // 回溯
                for (int i = path.size() - 1; i >= 0; --i) {
                    AdvancedMCTSNode* path_node = path[i];
                    float node_value = (i % 2 == 0) ? value : -value;
                    path_node->update(node_value);
                    path_node->revertVirtualLoss();
                }
                
                simulation_counter++;
                
                if (simulation_counter % check_interval == 0) {
                    auto elapsed = duration_cast<milliseconds>(
                        steady_clock::now() - start_time).count();
                    if (elapsed >= timeBudgetMs - SAFETY_MARGIN_MS) {
                        stop_flag = true;
                    }
                }
            }
            
            active_workers--;
        };
        
        // 启动工作线程
        int num_workers = USE_PARALLEL_EVAL ? PARALLEL_THREADS : 1;
        for (int i = 0; i < num_workers; ++i) {
            workers.emplace_back(worker_func, i);
        }
        
        // 主线程监控
        while (active_workers > 0) {
            this_thread::sleep_for(milliseconds(10));
            auto elapsed = duration_cast<milliseconds>(
                steady_clock::now() - start_time).count();
            if (elapsed >= timeBudgetMs - SAFETY_MARGIN_MS) {
                stop_flag = true;
                break;
            }
        }
        
        // 等待所有线程结束
        for (auto& worker : workers) {
            worker.join();
        }
        
        // 7. 选择最佳移动
        AdvancedMCTSNode* best_child = nullptr;
        float best_score = -1e9f;
        
        for (auto child : root->children) {
            if (child->visits > 0) {
                float child_value = child->getValue();
                float child_visits = child->visits;
                float score = child_value + sqrtf(logf(root->visits) / (child_visits + 1e-6f));
                
                if (score > best_score) {
                    best_score = score;
                    best_child = child;
                }
            }
        }
        
        Move best_move = best_child ? best_child->move : legal_moves[0];
        
        // 8. 清理
        delete root;
        
        cerr << "[MCTS] Simulations: " << simulation_counter.load()
             << ", Best visits: " << (best_child ? best_child->visits : 0)
             << ", Root value: " << avg_value << endl;
        
        simulation_counter = 0;
        return best_move;
    }
    
private:
    void addDirichletNoise(AdvancedMCTSNode* root) {
        if (root->children.empty()) return;
        
        mt19937 rng(time(nullptr));
        gamma_distribution<float> gamma(dirichlet_alpha, 1.0f);
        
        vector<float> noise(root->children.size());
        float noise_sum = 0;
        
        for (size_t i = 0; i < noise.size(); ++i) {
            noise[i] = gamma(rng);
            noise_sum += noise[i];
        }
        
        for (size_t i = 0; i < noise.size(); ++i) {
            noise[i] /= noise_sum;
            root->children[i]->prior = 
                (1.0f - dirichlet_epsilon) * root->children[i]->prior +
                dirichlet_epsilon * noise[i];
        }
    }
    
    float fastRollout(int player, int depth, mt19937& rng) {
        if (depth <= 0) {
            return get<0>(engine.evaluate(player));
        }
        
        auto moves = move_gen.generate(player);
        if (moves.empty()) return -1.0f;
        
        // 随机选择移动
        uniform_int_distribution<int> dist(0, moves.size() - 1);
        Move move = moves[dist(rng)];
        move.apply(player);
        
        float value = -fastRollout(-player, depth - 1, rng);
        
        move.undo(player);
        return value;
    }
};

// ==================== 开局库 ====================
class AdvancedOpeningBook {
private:
    struct BookEntry {
        array<uint8_t, 6> move;
        float weight;
        int depth;
    };
    
    unordered_map<string, vector<BookEntry>> book;
    
    string boardKey(int player) const {
        string key = to_string(player) + ":";
        for (int x = 0; x < GRIDSIZE; ++x) {
            for (int y = 0; y < GRIDSIZE; ++y) {
                key += to_string(gridInfo[x][y]) + ",";
            }
        }
        return key;
    }
    
public:
    AdvancedOpeningBook() {
        if (!USE_OPENING_BOOK) return;
        
        // 初始化开局库
        initializeBookFromProvidedData();
        
        cerr << "[OpeningBook] Loaded " << book.size() << " positions" << endl;
    }
    
    bool lookup(int player, int moveCount, Move& result) {
        if (!USE_OPENING_BOOK || moveCount >= OPENING_MAX_MOVES) {
            return false;
        }
        
        string key = boardKey(player);
        auto it = book.find(key);
        if (it != book.end()) {
            const auto& entries = it->second;
            
            // 选择权重最高且深度合适的开局
            float best_weight = -1;
            const BookEntry* best_entry = nullptr;
            
            for (const auto& entry : entries) {
                if (entry.depth <= moveCount && entry.weight > best_weight) {
                    best_weight = entry.weight;
                    best_entry = &entry;
                }
            }
            
            if (best_entry) {
                result = Move(best_entry->move[0], best_entry->move[1],
                            best_entry->move[2], best_entry->move[3],
                            best_entry->move[4], best_entry->move[5]);
                
                // 验证移动合法性
                MoveGenerator gen;
                if (gen.isLegal(result, player)) {
                    cerr << "[OpeningBook] Using book move" << endl;
                    return true;
                }
            }
        }
        
        return false;
    }
    
private:
    void addBookMove(const string& board_state, array<uint8_t, 6> move, float weight, int depth) {
        book[board_state].push_back({move, weight, depth});
    }
    
    void initializeBookFromProvidedData() {
        // 第一组开局 (高质量)
        addBookMove("1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {5, 0, 5, 5, 6, 4}, 1.0, 0);
        addBookMove("-1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {5, 7, 1, 3, 1, 1}, 1.0, 1);
        addBookMove("1:0,0,1,0,0,-1,0,0,0,2,0,-1,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {2, 0, 2, 3, 4, 5}, 1.0, 2);
        addBookMove("-1:0,0,1,0,0,-1,0,0,0,2,0,-1,0,0,0,0,0,0,0,1,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {2, 7, 2, 4, 4, 6}, 1.0, 3);
        addBookMove("1:0,0,1,0,0,-1,0,0,0,2,0,-1,0,0,0,0,0,0,0,1,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {0, 2, 2, 2, 4, 2}, 1.0, 4);
        addBookMove("-1:0,0,0,0,0,-1,0,0,0,2,0,-1,0,0,0,0,0,0,1,1,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,2,2,0,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {0, 5, 2, 5, 1, 4}, 1.0, 5);
        addBookMove("1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,1,-1,-1,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,2,2,0,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {2, 3, 3, 4, 3, 3}, 1.0, 6);
        addBookMove("-1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,0,-1,-1,0,0,0,0,0,2,1,0,0,0,0,0,2,0,0,0,2,2,0,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {2, 5, 3, 5, 2, 5}, 1.0, 7);
        addBookMove("1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,0,-1,2,0,0,0,0,0,2,1,-1,0,0,0,0,2,0,0,0,2,2,0,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {5, 5, 5, 6, 5, 3}, 1.0, 8);
        addBookMove("-1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,0,-1,2,0,0,0,0,0,2,1,-1,0,0,0,0,2,0,0,0,2,2,0,0,0,0,2,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {2, 4, 2, 3, 2, 4}, 1.0, 9);
        addBookMove("1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,-1,2,2,0,0,0,0,0,2,1,-1,0,0,0,0,2,0,0,0,2,2,0,0,0,0,2,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {7, 2, 5, 0, 3, 2}, 1.0, 10);
        addBookMove("-1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,-1,2,2,0,0,0,0,2,2,1,-1,0,0,0,0,2,0,0,0,2,2,0,1,0,0,2,0,0,0,1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,-1,0,0,", 
                   {2, 3, 1, 2, 3, 0}, 1.0, 11);
        addBookMove("1:0,0,0,0,0,0,0,0,0,2,-1,-1,2,0,0,0,0,0,1,0,2,2,0,0,2,0,2,2,1,-1,0,0,0,0,2,0,0,0,2,2,0,1,0,0,2,0,0,0,1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,-1,0,0,", 
                   {5, 6, 4, 7, 7, 4}, 1.0, 12);
        addBookMove("-1:0,0,0,0,0,0,0,0,0,2,-1,-1,2,0,0,0,0,0,1,0,2,2,0,0,2,0,2,2,1,-1,0,0,0,0,2,0,0,0,2,2,1,1,0,0,2,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,2,-1,0,0,", 
                   {1, 2, 2, 1, 1, 2}, 1.0, 13);

        // 第二组开局 (高质量)
        addBookMove("1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {2, 0, 2, 5, 1, 4}, 0.95, 0);
        addBookMove("-1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {5, 7, 5, 2, 6, 3}, 0.95, 1);
        addBookMove("1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {5, 0, 4, 0, 0, 4}, 0.95, 2);
        addBookMove("-1:0,0,1,0,2,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {0, 5, 1, 5, 1, 6}, 0.95, 3);
        addBookMove("1:0,0,1,0,2,0,0,0,0,0,0,0,2,-1,2,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {4, 0, 4, 6, 3, 6}, 0.95, 4);
        addBookMove("-1:0,0,1,0,2,0,0,0,0,0,0,0,2,-1,2,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {2, 7, 6, 7, 1, 2}, 0.95, 5);
        addBookMove("1:0,0,1,0,2,0,0,0,0,0,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,2,0,0,0,-1,0,0,1,0,0,-1,0,0,", 
                   {4, 6, 5, 6, 6, 6}, 0.95, 6);
        addBookMove("-1:0,0,1,0,2,0,0,0,0,0,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,2,0,0,2,-1,0,0,1,0,0,-1,0,0,", 
                   {7, 5, 3, 1, 3, 4}, 0.95, 7);
        addBookMove("1:0,0,1,0,2,0,0,0,0,0,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,-1,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,1,0,0,0,0,2,0,0,2,-1,0,0,1,0,0,0,0,0,", 
                   {7, 2, 6, 1, 6, 2}, 0.95, 8);
        addBookMove("-1:0,0,1,0,2,0,0,0,0,0,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,-1,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,2,2,0,0,2,-1,0,0,0,0,0,0,0,0,", 
                   {3, 1, 1, 1, 5, 1}, 0.95, 9);
        addBookMove("1:0,0,1,0,2,0,0,0,0,-1,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,2,-1,0,0,0,1,0,0,1,2,2,0,0,2,-1,0,0,0,0,0,0,0,0,0,0,0,", 
                   {0, 2, 1, 3, 5, 7}, 0.95, 10);
        addBookMove("-1:0,0,0,0,2,0,0,0,0,-1,2,1,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,2,-1,0,0,0,1,2,0,1,2,2,0,0,2,-1,0,0,0,0,0,0,0,0,0,0,0,", 
                   {6, 7, 7, 6, 7, 2}, 0.95, 11);
        addBookMove("1:0,0,0,0,2,0,0,0,0,-1,2,1,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,2,-1,0,0,0,1,2,0,1,2,2,0,0,2,0,0,0,2,0,0,0,-1,0,", 
                   {5, 6, 6, 5, 7, 5}, 0.95, 12);
        addBookMove("-1:0,0,0,0,2,0,0,0,0,-1,2,1,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,2,-1,0,0,0,0,2,0,1,2,2,0,1,2,0,0,0,2,0,0,2,-1,0,", 
                   {1, 1, 2, 0, 5, 0}, 0.95, 13);

        // 第三组开局 (优秀)
        addBookMove("1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {2, 0, 2, 5, 1, 4}, 0.9, 0);
        addBookMove("-1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {5, 7, 5, 2, 6, 3}, 0.9, 1);
        addBookMove("1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {5, 0, 4, 0, 0, 4}, 0.9, 2);
        addBookMove("-1:0,0,1,0,2,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {0, 5, 1, 5, 1, 6}, 0.9, 3);
        addBookMove("1:0,0,1,0,2,0,0,0,0,0,0,0,2,-1,2,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {2, 5, 3, 6, 2, 6}, 0.9, 4);
        addBookMove("-1:0,0,1,0,2,0,0,0,0,0,0,0,2,-1,2,0,0,0,0,0,0,0,2,-1,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {1, 5, 2, 4, 4, 6}, 0.9, 5);
        addBookMove("1:0,0,1,0,2,0,0,0,0,0,0,0,2,0,2,0,0,0,0,0,-1,0,2,-1,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,", 
                   {7, 2, 7, 4, 7, 2}, 0.9, 6);
        addBookMove("-1:0,0,1,0,2,0,0,0,0,0,0,0,2,0,2,0,0,0,0,0,-1,0,2,-1,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,-1,0,0,", 
                   {7, 5, 5, 5, 2, 5}, 0.9, 7);
        addBookMove("1:0,0,1,0,2,0,0,0,0,0,0,0,2,0,2,0,0,0,0,0,-1,2,2,-1,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,0,0,0,", 
                   {0, 2, 1, 3, 2, 2}, 0.9, 8);
        addBookMove("-1:0,0,0,0,2,0,0,0,0,0,0,1,2,0,2,0,0,0,2,0,-1,2,2,-1,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,0,0,0,", 
                   {2, 4, 2, 3, 2, 4}, 0.9, 9);
        addBookMove("1:0,0,0,0,2,0,0,0,0,0,0,1,2,0,2,0,0,0,2,-1,2,2,2,-1,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,0,0,0,", 
                   {1, 3, 1, 2, 2, 1}, 0.9, 10);
        addBookMove("-1:0,0,0,0,2,0,0,0,0,0,1,0,2,0,2,0,0,2,2,-1,2,2,2,-1,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,0,0,0,", 
                   {5, 5, 6, 5, 3, 5}, 0.9, 11);
        addBookMove("1:0,0,0,0,2,0,0,0,0,0,1,0,2,0,2,0,0,2,2,-1,2,2,2,-1,0,0,0,0,0,2,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,-1,0,0,0,0,2,0,1,0,0,0,", 
                   {4, 0, 4, 1, 6, 1}, 0.9, 12);
        addBookMove("-1:0,0,0,0,2,0,0,0,0,0,1,0,2,0,2,0,0,2,2,-1,2,2,2,-1,0,0,0,0,0,2,1,0,0,1,0,0,0,0,2,0,0,0,-1,0,0,0,0,0,0,2,0,2,0,-1,0,0,0,0,2,0,1,0,0,0,", 
                   {5, 2, 4, 2, 4, 5}, 0.9, 13);
    }
};

// ==================== 主函数 ====================
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    
    // 初始化棋盘
    memset(gridInfo, 0, sizeof(gridInfo));
    int pos = (GRIDSIZE - 1) / 3;
    
    // 初始布局
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
    
    cerr << "[Main] Turn " << turnID << ", Color " 
         << (currBotColor == grid_black ? "Black" : "White") 
         << ", Round " << rounds << endl;
    
    // 初始化组件
    AdvancedOpeningBook opening_book;
    AdvancedWeightLoader weight_loader;
    
    // 加载模型
    if (!weight_loader.loadFromFile(MODEL_PATH)) {
        cerr << "[Main] WARNING: Using fallback evaluation only" << endl;
    }
    
    // 初始化引擎
    ParallelNeuralEngine neural_net(weight_loader);
    ParallelMCTSSearcher searcher(neural_net);
    MoveGenerator move_gen;
    
    // 验证合法性
    auto moves = move_gen.generate(currBotColor);
    cerr << "[Main] Available moves: " << moves.size() << endl;
    
    if (moves.empty()) {
        cout << "-1 -1 -1 -1 -1 -1\n";
        cerr << "[Main] No legal moves!" << endl;
        return 0;
    }
    
    // 尝试开局库
    Move book_move;
    if (opening_book.lookup(currBotColor, rounds, book_move)) {
        book_move.apply(currBotColor);
        book_move.print();
        return 0;
    }
    
    // 自适应时间分配
    int time_for_move = TIME_LIMIT_MS;
    if (rounds < 10) {
        time_for_move = min(2000, TIME_LIMIT_MS * 2);  // 开局多思考
    } else if (moves.size() < 20) {
        time_for_move = TIME_LIMIT_MS * 3 / 2;  // 中残局多思考
    }
    
    // 搜索最佳移动
    auto start_time = steady_clock::now();
    Move best_move = searcher.search(currBotColor, rounds, time_for_move);
    auto end_time = steady_clock::now();
    
    auto elapsed = duration_cast<milliseconds>(end_time - start_time).count();
    cerr << "[Main] Decision time: " << elapsed << "ms" << endl;
    
    // 验证并输出
    bool move_valid = move_gen.isLegal(best_move, currBotColor);
    if (!move_valid) {
        cerr << "[Main] WARNING: Selected move is illegal, using first legal move" << endl;
        best_move = moves[0];
    }
    
    // 应用并输出
    best_move.apply(currBotColor);
    best_move.print();
    
    // 输出调试信息
    cerr << "[Main] Selected move: ";
    best_move.print();
    cerr << endl;
    
    return 0;
}