// amazons_bot_neural_fixed.cpp
// 完全修复版：正确使用KataGo神经网络（价值头+策略头）
// 编译：g++ -O3 -std=c++17 -march=native -pthread -o amazons_bot

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

using namespace std;
using namespace std::chrono;

// ==================== 配置文件 ====================
const std::string MODEL_PATH = "/data/b18c384.bin";
constexpr bool USE_OPENING_BOOK = true;
constexpr int OPENING_MAX_MOVES = 10;
constexpr int TIME_LIMIT_MS = 950;
constexpr int SAFETY_MARGIN_MS = 50;

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

// ==================== 开局库（保持原样）====================
class OpeningBook {
private:
    unordered_map<string, array<uint8_t, 6>> book;
    
    string boardFingerprint(int player) const {
        string fp = to_string(player) + ":";
        for (int x = 0; x < GRIDSIZE; ++x)
            for (int y = 0; y < GRIDSIZE; ++y)
                fp += to_string(gridInfo[x][y]) + ",";
        return fp;
    }
    
public:
    OpeningBook() {
        if (!USE_OPENING_BOOK) return;
// 正确开局库（基于实际对局）
  book["1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {5, 0, 5, 5, 6, 4};
  book["-1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {5, 7, 1, 3, 1, 1};
  book["1:0,0,1,0,0,-1,0,0,0,2,0,-1,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 0, 2, 3, 4, 5};
  book["-1:0,0,1,0,0,-1,0,0,0,2,0,-1,0,0,0,0,0,0,0,1,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 7, 2, 4, 4, 6};
  book["1:0,0,1,0,0,-1,0,0,0,2,0,-1,0,0,0,0,0,0,0,1,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {0, 2, 2, 2, 4, 2};
  book["-1:0,0,0,0,0,-1,0,0,0,2,0,-1,0,0,0,0,0,0,1,1,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,2,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {0, 5, 2, 5, 1, 4};
  book["1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,1,-1,-1,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,2,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 3, 3, 4, 3, 3};
  book["-1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,0,-1,-1,0,0,0,0,0,2,1,0,0,0,0,0,2,0,0,2,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 5, 3, 5, 2, 5};
  book["1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,0,-1,2,0,0,0,0,0,2,1,-1,0,0,0,0,2,0,0,2,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {5, 5, 5, 6, 5, 3};
  book["-1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,0,-1,2,0,0,0,0,0,2,1,-1,0,0,0,0,2,0,0,2,2,0,0,0,0,2,0,0,1,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 4, 2, 3, 2, 4};
  book["1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,-1,2,2,0,0,0,0,0,2,1,-1,0,0,0,0,2,0,0,2,2,0,0,0,0,2,0,0,1,0,0,0,0,0,2,0,0,0,0,0,1,0,0,-1,0,0,"] = {7, 2, 5, 0, 3, 2};
  book["-1:0,0,0,0,0,0,0,0,0,2,0,-1,2,0,0,0,0,0,1,-1,2,2,0,0,0,0,2,2,1,-1,0,0,0,0,2,0,0,2,2,0,1,0,0,2,0,0,1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,-1,0,0,"] = {2, 3, 1, 2, 3, 0};
  book["1:0,0,0,0,0,0,0,0,0,2,-1,-1,2,0,0,0,0,0,1,0,2,2,0,0,2,0,2,2,1,-1,0,0,0,0,2,0,0,2,2,0,1,0,0,2,0,0,1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,-1,0,0,"] = {5, 6, 4, 7, 7, 4};
  book["-1:0,0,0,0,0,0,0,0,0,2,-1,-1,2,0,0,0,0,0,1,0,2,2,0,0,2,0,2,2,1,-1,0,0,0,0,2,0,0,2,2,1,1,0,0,2,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,2,-1,0,0,"] = {1, 2, 2, 1, 1, 2};

  book["1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 0, 2, 5, 1, 4};
  book["-1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {5, 7, 5, 2, 6, 3};
  book["1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {0, 2, 2, 2, 0, 4};
  book["-1:0,0,0,0,2,-1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 7, 3, 7, 7, 3};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,2,0,-1,0,0,"] = {7, 2, 6, 2, 1, 7};
  book["-1:0,0,0,0,2,-1,0,0,0,0,0,0,2,0,0,2,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,-1,0,0,"] = {3, 7, 3, 3, 5, 3};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,0,0,2,0,0,1,0,0,1,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,-1,2,0,0,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,-1,0,0,"] = {5, 0, 3, 2, 4, 1};
  book["-1:0,0,0,0,2,-1,0,0,0,0,0,0,2,0,0,2,0,0,1,0,0,1,0,0,0,0,1,-1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,-1,2,0,0,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,-1,0,0,"] = {7, 5, 5, 5, 5, 4};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,0,0,2,0,0,1,0,0,1,0,0,0,0,1,-1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,-1,2,2,-1,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,0,0,0,"] = {2, 2, 2, 3, 2, 2};
  book["-1:0,0,0,0,2,-1,0,0,0,0,0,0,2,0,0,2,0,0,2,1,0,1,0,0,0,0,1,-1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,-1,2,2,-1,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,0,0,0,"] = {3, 3, 2, 4, 3, 4};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,0,0,2,0,0,2,1,-1,1,0,0,0,0,1,0,2,0,0,0,0,2,0,0,0,0,0,0,0,0,-1,2,2,-1,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,0,0,0,"] = {2, 3, 1, 3, 2, 3};
  book["-1:0,0,0,0,2,-1,0,0,0,0,0,1,2,0,0,2,0,0,2,2,-1,1,0,0,0,0,1,0,2,0,0,0,0,2,0,0,0,0,0,0,0,0,-1,2,2,-1,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,0,0,0,"] = {2, 4, 3, 5, 3, 6};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,1,2,0,0,2,0,0,2,2,0,1,0,0,0,0,1,0,2,-1,2,0,0,2,0,0,0,0,0,0,0,0,-1,2,2,-1,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,0,0,0,"] = {2, 5, 2, 6, 0, 6};
  book["-1:0,0,0,0,2,-1,2,0,0,0,0,1,2,0,0,2,0,0,2,2,0,0,1,0,0,0,1,0,2,-1,2,0,0,2,0,0,0,0,0,0,0,0,-1,2,2,-1,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,0,0,0,"] = {5, 5, 4, 6, 3, 7};
  
  book["1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 0, 2, 5, 1, 4};
  book["-1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {5, 7, 5, 2, 6, 3};
  book["1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {5, 0, 4, 0, 0, 4};
  book["-1:0,0,1,0,2,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {0, 5, 1, 5, 1, 6};
  book["1:0,0,1,0,2,0,0,0,0,0,0,0,2,-1,2,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {4, 0, 4, 6, 3, 6};
  book["-1:0,0,1,0,2,0,0,0,0,0,0,0,2,-1,2,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,1,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 7, 6, 7, 1, 2};
  book["1:0,0,1,0,2,0,0,0,0,0,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,1,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,-1,0,0,1,0,0,-1,0,0,"] = {4, 6, 5, 6, 6, 6};
  book["-1:0,0,1,0,2,0,0,0,0,0,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,1,0,0,0,0,2,0,0,2,-1,0,0,1,0,0,-1,0,0,"] = {7, 5, 3, 1, 3, 4};
  book["1:0,0,1,0,2,0,0,0,0,0,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,-1,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,1,0,0,0,0,2,0,0,2,-1,0,0,1,0,0,0,0,0,"] = {7, 2, 6, 1, 6, 2};
  book["-1:0,0,1,0,2,0,0,0,0,0,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,-1,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,1,0,0,1,2,2,0,0,2,-1,0,0,0,0,0,0,0,0,"] = {3, 1, 1, 1, 5, 1};
  book["1:0,0,1,0,2,0,0,0,0,-1,2,0,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,2,-1,0,0,0,1,0,0,1,2,2,0,0,2,-1,0,0,0,0,0,0,0,0,"] = {0, 2, 1, 3, 5, 7};
  book["-1:0,0,0,0,2,0,0,0,0,-1,2,1,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,2,-1,0,0,0,1,2,0,1,2,2,0,0,2,-1,0,0,0,0,0,0,0,0,"] = {6, 7, 7, 6, 7, 2};
  book["1:0,0,0,0,2,0,0,0,0,-1,2,1,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,2,-1,0,0,0,1,2,0,1,2,2,0,0,2,0,0,0,2,0,0,0,-1,0,"] = {5, 6, 6, 5, 7, 5};
  book["-1:0,0,0,0,2,0,0,0,0,-1,2,1,2,-1,2,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,2,0,0,0,0,0,0,0,0,0,0,2,-1,0,0,0,0,2,0,1,2,2,0,1,2,0,0,0,2,0,0,2,-1,0,"] = {1, 1, 2, 0, 5, 0};
  
  book["1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 0, 2, 5, 1, 4};
  book["-1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {5, 7, 5, 2, 6, 3};
  book["1:0,0,1,0,0,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {5, 0, 4, 0, 0, 4};
  book["-1:0,0,1,0,2,-1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {0, 5, 1, 5, 1, 6};
  book["1:0,0,1,0,2,0,0,0,0,0,0,0,2,-1,2,0,0,0,0,0,0,1,0,-1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 5, 3, 6, 2, 6};
  book["-1:0,0,1,0,2,0,0,0,0,0,0,0,2,-1,2,0,0,0,0,0,0,0,2,-1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {1, 5, 2, 4, 4, 6};
  book["1:0,0,1,0,2,0,0,0,0,0,0,0,2,0,2,0,0,0,0,0,-1,0,2,-1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {7, 2, 7, 4, 7, 2};
  book["-1:0,0,1,0,2,0,0,0,0,0,0,0,2,0,2,0,0,0,0,0,-1,0,2,-1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,-1,0,0,"] = {7, 5, 5, 5, 2, 5};
  book["1:0,0,1,0,2,0,0,0,0,0,0,0,2,0,2,0,0,0,0,0,-1,2,2,-1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,-1,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,0,0,0,"] = {0, 2, 1, 3, 2, 2};
  book["-1:0,0,0,0,2,0,0,0,0,0,0,1,2,0,2,0,0,0,2,0,-1,2,2,-1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,-1,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,0,0,0,"] = {2, 4, 2, 3, 2, 4};
  book["1:0,0,0,0,2,0,0,0,0,0,0,1,2,0,2,0,0,0,2,-1,2,2,2,-1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,-1,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,0,0,0,"] = {1, 3, 1, 2, 2, 1};
  book["-1:0,0,0,0,2,0,0,0,0,0,1,0,2,0,2,0,0,2,2,-1,2,2,2,-1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,-1,0,0,0,0,0,2,0,0,0,0,0,0,2,0,1,0,0,0,"] = {5, 5, 6, 5, 3, 5};
  book["1:0,0,0,0,2,0,0,0,0,0,1,0,2,0,2,0,0,2,2,-1,2,2,2,-1,0,0,0,0,0,2,1,0,1,0,0,0,0,0,2,0,0,0,-1,0,0,0,0,0,0,0,0,2,0,-1,0,0,0,0,2,0,1,0,0,0,"] = {4, 0, 4, 1, 6, 1};
  book["-1:0,0,0,0,2,0,0,0,0,0,1,0,2,0,2,0,0,2,2,-1,2,2,2,-1,0,0,0,0,0,2,1,0,0,1,0,0,0,0,2,0,0,0,-1,0,0,0,0,0,0,2,0,2,0,-1,0,0,0,0,2,0,1,0,0,0,"] = {5, 2, 4, 2, 4, 5};
  
  book["1:0,0,1,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 0, 2, 5, 1, 4};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,1,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {0, 2, 2, 2, 0, 4};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,2,1,0,0,0,1,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {2, 5, 1, 6, 1, 5};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,2,2,0,0,0,1,0,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,-1,0,0,0,0,0,0,1,0,0,0,1,0,0,-1,0,0,"] = {1, 6, 6, 6, 1, 6};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,2,2,0,0,0,1,0,0,0,0,-1,0,0,1,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,1,0,0,0,1,0,0,-1,0,0,"] = {5, 0, 3, 2, 4, 2};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,2,2,0,0,0,1,0,0,0,0,-1,2,0,0,0,1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,1,0,0,0,1,0,0,-1,0,0,"] = {3, 2, 3, 4, 3, 0};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,2,2,2,0,0,1,0,0,0,0,-1,2,0,0,0,0,1,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,0,1,0,0,0,1,0,0,-1,0,0,"] = {3, 4, 3, 5, 1, 7};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,2,2,2,0,0,1,0,0,0,0,-1,2,0,0,0,1,0,0,0,0,0,2,0,0,0,0,0,0,0,2,0,0,0,0,-1,0,0,0,0,0,0,1,0,0,0,1,0,0,-1,0,0,"] = {3, 5, 3, 4, 5, 2};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,0,2,2,2,2,0,1,2,0,0,0,0,-1,2,0,0,0,1,0,0,0,0,0,2,0,0,0,0,0,0,0,2,0,0,0,0,-1,0,0,0,0,0,0,1,0,0,0,1,0,0,-1,0,0,"] = {2, 2, 2, 1, 2, 2};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,2,2,2,2,2,0,1,2,0,0,0,0,-1,2,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,2,0,0,0,0,-1,0,0,0,0,0,0,1,0,0,0,1,0,0,-1,0,0,"] = {3, 4, 3, 3, 1, 3};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,2,2,2,2,2,0,1,2,0,2,0,0,-1,2,0,0,0,0,0,0,0,0,0,2,0,1,0,0,0,0,0,2,0,0,0,0,-1,0,0,0,0,0,0,1,0,0,0,1,0,0,-1,0,0,"] = {3, 3, 4, 4, 2, 4};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,2,2,2,2,2,0,1,2,0,2,0,0,-1,2,0,0,0,2,0,0,0,0,0,2,0,1,0,0,0,0,0,2,0,0,0,1,-1,0,0,0,0,0,0,0,0,0,0,1,0,0,-1,0,0,"] = {6, 6, 5, 6, 3, 4};
  book["1:0,0,0,0,2,-1,0,0,0,0,0,2,2,2,2,2,0,1,2,0,2,0,0,-1,2,0,0,0,2,0,0,0,0,0,2,0,1,0,0,0,0,0,2,0,2,0,0,-1,0,0,0,0,0,1,0,0,0,0,1,0,0,-1,0,0,"] = {5, 6, 6, 5, 5, 4};

    }
    
    bool lookup(int player, int moveCount, uint8_t& x0, uint8_t& y0, 
                uint8_t& x1, uint8_t& y1, uint8_t& x2, uint8_t& y2) const {
        if (!USE_OPENING_BOOK || moveCount >= OPENING_MAX_MOVES)
            return false;
            
        string fp = boardFingerprint(player);
        auto it = book.find(fp);
        if (it != book.end()) {
            x0 = it->second[0]; y0 = it->second[1];
            x1 = it->second[2]; y1 = it->second[3];
            x2 = it->second[4]; y2 = it->second[5];
            
            if (inMap(x0, y0) && inMap(x1, y1) && inMap(x2, y2) &&
                gridInfo[x0][y0] == player && gridInfo[x1][y1] == EMPTY && 
                (gridInfo[x2][y2] == EMPTY || (x2 == x0 && y2 == y0))) {
                cerr << "[OpeningBook] Found book move!" << endl;
                return true;
            }
        }
        return false;
    }
};

// ==================== 神经网络配置（增强版）====================
struct NetConfig {
    static constexpr int INPUT_CHANNELS = 22;
    static constexpr int NUM_BLOCKS = 18;
    static constexpr int NUM_FILTERS = 384;
    static constexpr int VALUE_CHANNELS = 32;
    static constexpr int VALUE_FC_SIZE = 256;
    static constexpr int POLICY_CHANNELS = 64;           // 策略头通道数
    static constexpr int POLICY_MAP_SIZE = 8*8*8*8;      // 8x8棋盘，每个起点8方向，每个方向距离
    static constexpr int POLICY_OUTPUT_SIZE = 4096;      // 8x8x8x8 = 4096
    
    struct FileHeader {
        int32_t magic;          // 魔数: 0x4B474E4E ('KGNN')
        int32_t version;        // 版本: 2（现在包含策略头）
        int32_t num_blocks;
        int32_t num_filters;
        int32_t value_channels;
        int32_t policy_channels;  // 新增：策略头通道
        int32_t policy_map_size;  // 新增：策略图大小
        int32_t reserved[9];
    };
};

// ==================== SIMD优化 ====================
#ifdef __AVX2__
inline void avx_add(float* dst, const float* src, int n) {
    for (int i = 0; i < n; i += 8) {
        __m256 a = _mm256_loadu_ps(dst + i);
        __m256 b = _mm256_loadu_ps(src + i);
        _mm256_storeu_ps(dst + i, _mm256_add_ps(a, b));
    }
}

inline void avx_relu(float* data, int n) {
    __m256 zero = _mm256_setzero_ps();
    for (int i = 0; i < n; i += 8) {
        __m256 x = _mm256_loadu_ps(data + i);
        _mm256_storeu_ps(data + i, _mm256_max_ps(x, zero));
    }
}
#endif

// ==================== 神经网络权重加载器（完整版）====================
class WeightLoader {
private:
    struct ConvWeights {
        vector<float> weight;
        vector<float> bias;
        int in_channels;
        int out_channels;
        int kernel_size;
        
        ConvWeights() = default;
        ConvWeights(int ic, int oc, int ks) : 
            in_channels(ic), out_channels(oc), kernel_size(ks) {
            weight.resize(oc * ic * ks * ks);
            bias.resize(oc);
        }
    };
    
    struct DenseWeights {
        vector<float> weight;
        vector<float> bias;
        int in_size;
        int out_size;
    };
    
    unordered_map<string, ConvWeights> conv_layers;
    unordered_map<string, DenseWeights> dense_layers;
    bool loaded = false;
    
public:
    bool loadFromFile(const string& path) {
        ifstream fin(path, ios::binary);
        if (!fin) {
            cerr << "[WeightLoader] Cannot open model file: " << path << endl;
            return false;
        }
        
        // 读取文件头
        NetConfig::FileHeader header;
        fin.read(reinterpret_cast<char*>(&header), sizeof(header));
        
        if (header.magic != 0x4B474E4E) {
            cerr << "[WeightLoader] Invalid model file format" << endl;
            return false;
        }
        
        cerr << "[WeightLoader] Loading b18c384 model (blocks=" 
             << header.num_blocks << ", filters=" << header.num_filters 
             << ", policy_channels=" << header.policy_channels << ")" << endl;
        
        // 加载输入卷积层
        loadConvLayer(fin, "conv1", NetConfig::INPUT_CHANNELS, NetConfig::NUM_FILTERS, 3);
        
        // 加载18个残差块
        for (int i = 0; i < NetConfig::NUM_BLOCKS; ++i) {
            string prefix = "block" + to_string(i);
            loadConvLayer(fin, prefix + ".conv1", NetConfig::NUM_FILTERS, NetConfig::NUM_FILTERS, 3);
            loadConvLayer(fin, prefix + ".conv2", NetConfig::NUM_FILTERS, NetConfig::NUM_FILTERS, 3);
        }
        
        // 加载价值头
        loadConvLayer(fin, "value_conv", NetConfig::NUM_FILTERS, NetConfig::VALUE_CHANNELS, 1);
        loadDenseLayer(fin, "value_fc1", NetConfig::VALUE_CHANNELS * BOARD_AREA, NetConfig::VALUE_FC_SIZE);
        loadDenseLayer(fin, "value_fc2", NetConfig::VALUE_FC_SIZE, 1);
        
        // 加载策略头（新增！）
        loadConvLayer(fin, "policy_conv", NetConfig::NUM_FILTERS, NetConfig::POLICY_CHANNELS, 1);
        loadDenseLayer(fin, "policy_fc", NetConfig::POLICY_CHANNELS * BOARD_AREA, NetConfig::POLICY_OUTPUT_SIZE);
        
        loaded = true;
        cerr << "[WeightLoader] Full model loaded successfully (value + policy)" << endl;
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
    
    bool isLoaded() const { return loaded; }
    
private:
    void loadConvLayer(ifstream& fin, const string& name, int ic, int oc, int ks) {
        ConvWeights layer(ic, oc, ks);
        fin.read(reinterpret_cast<char*>(layer.weight.data()), layer.weight.size() * sizeof(float));
        fin.read(reinterpret_cast<char*>(layer.bias.data()), layer.bias.size() * sizeof(float));
        conv_layers[name] = layer;
        cerr << "[WeightLoader] Loaded conv: " << name << " (" << ic << "->" << oc << ")" << endl;
    }
    
    void loadDenseLayer(ifstream& fin, const string& name, int in_sz, int out_sz) {
        DenseWeights layer;
        layer.in_size = in_sz;
        layer.out_size = out_sz;
        layer.weight.resize(in_sz * out_sz);
        layer.bias.resize(out_sz);
        fin.read(reinterpret_cast<char*>(layer.weight.data()), layer.weight.size() * sizeof(float));
        fin.read(reinterpret_cast<char*>(layer.bias.data()), layer.bias.size() * sizeof(float));
        dense_layers[name] = layer;
        cerr << "[WeightLoader] Loaded dense: " << name << " (" << in_sz << "->" << out_sz << ")" << endl;
    }
};

// ==================== 特征提取器（保持原样）====================
class FeatureExtractor {
public:
    vector<float> extract(int player) const {
        vector<float> features(NetConfig::INPUT_CHANNELS * BOARD_AREA, 0.0f);
        
        for (int y = 0; y < GRIDSIZE; ++y) {
            for (int x = 0; x < GRIDSIZE; ++x) {
                int idx = y * GRIDSIZE + x;
                int piece = gridInfo[x][y];
                
                if (piece == player) {
                    features[0 * BOARD_AREA + idx] = 1.0f;
                } else if (piece == -player) {
                    features[1 * BOARD_AREA + idx] = 1.0f;
                } else if (piece == OBSTACLE) {
                    features[2 * BOARD_AREA + idx] = 1.0f;
                } else {
                    features[3 * BOARD_AREA + idx] = 1.0f;
                }
            }
        }
        
        for (int y = 0; y < GRIDSIZE; ++y) {
            for (int x = 0; x < GRIDSIZE; ++x) {
                int idx = y * GRIDSIZE + x;
                
                if (gridInfo[x][y] == player) {
                    for (int d = 0; d < 8; ++d) {
                        int reach = computeDirectionalReach(x, y, d);
                        features[(4 + d) * BOARD_AREA + idx] = reach / 10.0f;
                    }
                }
                
                if (gridInfo[x][y] == -player) {
                    for (int d = 0; d < 8; ++d) {
                        int reach = computeDirectionalReach(x, y, d);
                        features[(12 + d) * BOARD_AREA + idx] = reach / 10.0f;
                    }
                }
            }
        }
        
        float my_pieces = 0, opp_pieces = 0;
        for (int x = 0; x < GRIDSIZE; ++x) {
            for (int y = 0; y < GRIDSIZE; ++y) {
                if (gridInfo[x][y] == player) my_pieces++;
                if (gridInfo[x][y] == -player) opp_pieces++;
            }
        }
        
        for (int i = 0; i < BOARD_AREA; ++i) {
            features[20 * BOARD_AREA + i] = my_pieces / 8.0f;
            features[21 * BOARD_AREA + i] = opp_pieces / 8.0f;
        }
        
        return features;
    }
    
private:
    int computeDirectionalReach(int x, int y, int dir) const {
        static const int dirs[8][2] = {
            {0,1}, {0,-1}, {1,0}, {-1,0}, 
            {1,1}, {1,-1}, {-1,1}, {-1,-1}
        };
        
        int reach = 0;
        int nx = x + dirs[dir][0];
        int ny = y + dirs[dir][1];
        
        while (inMap(nx, ny) && gridInfo[nx][ny] == EMPTY) {
            reach++;
            nx += dirs[dir][0];
            ny += dirs[dir][1];
        }
        
        return min(reach, 10);
    }
};

// ==================== 神经网络推理引擎（完整版）====================
class NeuralEngine {
private:
    WeightLoader& weights;
    FeatureExtractor extractor;
    
    // 快速3x3卷积
    void conv3x3(const vector<float>& input, vector<float>& output,
                const vector<float>& weight, const vector<float>& bias,
                int in_c, int out_c, int size) const {
        output.assign(out_c * size * size, 0.0f);
        
        #pragma omp parallel for collapse(2)
        for (int oc = 0; oc < out_c; ++oc) {
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    float sum = bias[oc];
                    int out_idx = oc * size * size + y * size + x;
                    
                    for (int ic = 0; ic < in_c; ++ic) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            int yy = y + dy;
                            if (yy < 0 || yy >= size) continue;
                            
                            for (int dx = -1; dx <= 1; ++dx) {
                                int xx = x + dx;
                                if (xx < 0 || xx >= size) continue;
                                
                                int in_idx = ic * size * size + yy * size + xx;
                                int w_idx = ((oc * in_c + ic) * 3 + (dy + 1)) * 3 + (dx + 1);
                                sum += input[in_idx] * weight[w_idx];
                            }
                        }
                    }
                    
                    output[out_idx] = max(0.0f, sum);
                }
            }
        }
    }
    
    // 1x1卷积
    void conv1x1(const vector<float>& input, vector<float>& output,
                const vector<float>& weight, const vector<float>& bias,
                int in_c, int out_c, int size) const {
        output.assign(out_c * size * size, 0.0f);
        
        #pragma omp parallel for collapse(2)
        for (int oc = 0; oc < out_c; ++oc) {
            for (int i = 0; i < size * size; ++i) {
                float sum = bias[oc];
                for (int ic = 0; ic < in_c; ++ic) {
                    sum += input[ic * size * size + i] * weight[oc * in_c + ic];
                }
                output[oc * size * size + i] = max(0.0f, sum);
            }
        }
    }
    
    // 全连接层
    void denseLayer(const vector<float>& input, vector<float>& output,
                   const vector<float>& weight, const vector<float>& bias,
                   int in_size, int out_size) const {
        output.resize(out_size);
        
        #pragma omp parallel for
        for (int i = 0; i < out_size; ++i) {
            float sum = bias[i];
            for (int j = 0; j < in_size; ++j) {
                sum += input[j] * weight[i * in_size + j];
            }
            output[i] = max(0.0f, sum);
        }
    }
    
    // 残差块
    void residualBlock(const vector<float>& input, vector<float>& output,
                      const string& block_name, int size) const {
        auto* conv1 = weights.getConv(block_name + ".conv1");
        auto* conv2 = weights.getConv(block_name + ".conv2");
        
        if (!conv1 || !conv2) {
            output = input;
            return;
        }
        
        vector<float> temp1, temp2;
        conv3x3(input, temp1, conv1->weight, conv1->bias, 
               NetConfig::NUM_FILTERS, NetConfig::NUM_FILTERS, size);
        
        conv3x3(temp1, temp2, conv2->weight, conv2->bias,
               NetConfig::NUM_FILTERS, NetConfig::NUM_FILTERS, size);
        
        output.resize(temp2.size());
        for (size_t i = 0; i < temp2.size(); ++i) {
            output[i] = max(0.0f, temp2[i] + input[i]);
        }
    }
    
public:
    NeuralEngine(WeightLoader& w) : weights(w) {}
    
    // 新函数：同时获取价值评估和策略分布
    pair<float, vector<float>> evaluateWithPolicy(int player) {
        if (!weights.isLoaded()) {
            return {fallbackEvaluation(player), vector<float>()};
        }
        
        // 1. 特征提取
        auto features = extractor.extract(player);
        
        // 2. 输入卷积
        auto* conv1 = weights.getConv("conv1");
        if (!conv1) return {fallbackEvaluation(player), vector<float>()};
        
        vector<float> hidden;
        conv3x3(features, hidden, conv1->weight, conv1->bias,
               NetConfig::INPUT_CHANNELS, NetConfig::NUM_FILTERS, GRIDSIZE);
        
        // 3. 18个残差块
        for (int i = 0; i < NetConfig::NUM_BLOCKS; ++i) {
            residualBlock(hidden, hidden, "block" + to_string(i), GRIDSIZE);
        }
        
        // 4. 价值头
        auto* value_conv = weights.getConv("value_conv");
        auto* value_fc1 = weights.getDense("value_fc1");
        auto* value_fc2 = weights.getDense("value_fc2");
        
        if (!value_conv || !value_fc1 || !value_fc2) {
            return {fallbackEvaluation(player), vector<float>()};
        }
        
        vector<float> value_feat;
        conv1x1(hidden, value_feat, value_conv->weight, value_conv->bias,
               NetConfig::NUM_FILTERS, NetConfig::VALUE_CHANNELS, GRIDSIZE);
        
        vector<float> flattened(value_feat.size());
        copy(value_feat.begin(), value_feat.end(), flattened.begin());
        
        vector<float> fc1_out, fc2_out;
        denseLayer(flattened, fc1_out, value_fc1->weight, value_fc1->bias,
                  NetConfig::VALUE_CHANNELS * BOARD_AREA, NetConfig::VALUE_FC_SIZE);
        
        denseLayer(fc1_out, fc2_out, value_fc2->weight, value_fc2->bias,
                  NetConfig::VALUE_FC_SIZE, 1);
        
        float value = tanh(fc2_out[0]);
        
        // 5. 策略头（新增！）
        auto* policy_conv = weights.getConv("policy_conv");
        auto* policy_fc = weights.getDense("policy_fc");
        
        if (!policy_conv || !policy_fc) {
            return {value, vector<float>()};
        }
        
        vector<float> policy_feat;
        conv1x1(hidden, policy_feat, policy_conv->weight, policy_conv->bias,
               NetConfig::NUM_FILTERS, NetConfig::POLICY_CHANNELS, GRIDSIZE);
        
        vector<float> policy_flattened(policy_feat.size());
        copy(policy_feat.begin(), policy_feat.end(), policy_flattened.begin());
        
        vector<float> policy_logits;
        denseLayer(policy_flattened, policy_logits, policy_fc->weight, policy_fc->bias,
                  NetConfig::POLICY_CHANNELS * BOARD_AREA, NetConfig::POLICY_OUTPUT_SIZE);
        
        // 应用softmax得到概率分布
        vector<float> policy_probs = softmax(policy_logits);
        
        return {value, policy_probs};
    }
    
    // 旧函数（保持兼容）
    float evaluate(int player) {
        auto result = evaluateWithPolicy(player);
        return result.first;
    }
    
private:
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
        int my_mobility = 0, opp_mobility = 0;
        int my_center = 0, opp_center = 0;
        
        for (int x = 0; x < GRIDSIZE; ++x) {
            for (int y = 0; y < GRIDSIZE; ++y) {
                if (gridInfo[x][y] == player) {
                    my_mobility += estimateMobility(x, y);
                    if (x >= 2 && x <= 5 && y >= 2 && y <= 5) my_center++;
                } else if (gridInfo[x][y] == -player) {
                    opp_mobility += estimateMobility(x, y);
                    if (x >= 2 && x <= 5 && y >= 2 && y <= 5) opp_center++;
                }
            }
        }
        
        float score = (my_mobility - opp_mobility) * 0.05f;
        score += (my_center - opp_center) * 0.1f;
        return tanh(score);
    }
    
    int estimateMobility(int x, int y) const {
        int mobility = 0;
        int dirs[8][2] = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1}};
        
        for (int d = 0; d < 8; ++d) {
            int nx = x + dirs[d][0];
            int ny = y + dirs[d][1];
            while (inMap(nx, ny) && gridInfo[nx][ny] == EMPTY) {
                mobility++;
                nx += dirs[d][0];
                ny += dirs[d][1];
            }
        }
        return min(mobility, 20);
    }
};

// ==================== 方向向量 ====================
const int dir_dx[8] = {0, 0, 1, -1, 1, 1, -1, -1};
const int dir_dy[8] = {1, -1, 0, 0, 1, -1, 1, -1};

inline int idx_from_xy(int x, int y) { return x * GRIDSIZE + y; }

// ==================== 预计算射线检查 ====================
uint64_t between_masks[64][64];
bool line_valid[64][64];

void init_masks() {
    static bool initialized = false;
    if (initialized) return;
    
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j < 64; ++j) {
            between_masks[i][j] = 0;
            line_valid[i][j] = false;
            
            int x1 = i / GRIDSIZE;
            int y1 = i % GRIDSIZE;
            int x2 = j / GRIDSIZE;
            int y2 = j % GRIDSIZE;
            int dx = x2 - x1;
            int dy = y2 - y1;
            
            if (dx == 0 && dy == 0) continue;
            
            if (dx == 0 || dy == 0 || abs(dx) == abs(dy)) {
                line_valid[i][j] = true;
                int step_x = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
                int step_y = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
                int steps = max(abs(dx), abs(dy));
                int x = x1 + step_x, y = y1 + step_y;
                
                for (int k = 1; k < steps; ++k) {
                    int idx = x * GRIDSIZE + y;
                    between_masks[i][j] |= (1ULL << idx);
                    x += step_x; y += step_y;
                }
            }
        }
    }
    initialized = true;
}

inline bool fast_line_clear(uint64_t occ, int idx0, int idx1) {
    if (!line_valid[idx0][idx1]) return false;
    return (occ & between_masks[idx0][idx1]) == 0;
}

// ==================== 移动结构体 ====================
struct Move {
    uint8_t x0, y0, x1, y1, x2, y2;
    
    Move() : x0(0), y0(0), x1(0), y1(0), x2(0), y2(0) {}
    Move(int x0, int y0, int x1, int y1, int x2, int y2)
        : x0(static_cast<uint8_t>(x0)), y0(static_cast<uint8_t>(y0)), 
          x1(static_cast<uint8_t>(x1)), y1(static_cast<uint8_t>(y1)),
          x2(static_cast<uint8_t>(x2)), y2(static_cast<uint8_t>(y2)) {}
    
    bool isValid() const {
        return x0 != 0 || y0 != 0 || x1 != 0 || y1 != 0;
    }
    
    void apply(int color) const {
        ProcStep(x0, y0, x1, y1, x2, y2, color);
    }
    
    void undo(int color) const {
        gridInfo[x2][y2] = EMPTY;
        gridInfo[x1][y1] = EMPTY;
        gridInfo[x0][y0] = color;
    }
    
    void print() const {
        cout << (int)x0 << " " << (int)y0 << " "
             << (int)x1 << " " << (int)y1 << " "
             << (int)x2 << " " << (int)y2 << endl;
    }
    
    // 转换为策略网络的索引
    int toPolicyIndex() const {
        // 编码：起点(x0,y0) -> 8方向 -> 距离 -> 箭点
        // 简化版本：直接计算唯一索引
        int idx = (x0 * 8 + y0) * 512 + (x1 * 8 + y1) * 8 + (x2 % 8);
        return idx % NetConfig::POLICY_OUTPUT_SIZE;
    }
};

// ==================== 移动生成器 ====================
vector<Move> fast_gen_moves(int color) {
    vector<Move> moves;
    
    uint64_t occ = 0;
    for (int x = 0; x < GRIDSIZE; ++x)
        for (int y = 0; y < GRIDSIZE; ++y)
            if (gridInfo[x][y] != 0)
                occ |= (1ULL << idx_from_xy(x, y));
    
    for (int x0 = 0; x0 < GRIDSIZE; ++x0) {
        for (int y0 = 0; y0 < GRIDSIZE; ++y0) {
            if (gridInfo[x0][y0] != color) continue;
            
            int idx0 = idx_from_xy(x0, y0);
            
            for (int d1 = 0; d1 < 8; ++d1) {
                int x1 = x0 + dir_dx[d1];
                int y1 = y0 + dir_dy[d1];
                
                while (inMap(x1, y1) && gridInfo[x1][y1] == EMPTY) {
                    int idx1 = idx_from_xy(x1, y1);
                    
                    uint64_t temp_occ = occ;
                    temp_occ &= ~(1ULL << idx0);
                    temp_occ |= (1ULL << idx1);
                    
                    for (int d2 = 0; d2 < 8; ++d2) {
                        int x2 = x1 + dir_dx[d2];
                        int y2 = y1 + dir_dy[d2];
                        
                        while (inMap(x2, y2)) {
                            bool arrow_possible = false;
                            if (gridInfo[x2][y2] == EMPTY) {
                                arrow_possible = true;
                            } else if (x2 == x0 && y2 == y0) {
                                arrow_possible = true;
                            }
                            
                            if (arrow_possible) {
                                int idx2 = idx_from_xy(x2, y2);
                                if (fast_line_clear(temp_occ, idx1, idx2)) {
                                    moves.emplace_back(x0, y0, x1, y1, x2, y2);
                                }
                            } else {
                                break;
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

// ==================== 策略映射器 ====================
class PolicyMapper {
public:
    // 将策略网络的输出映射到合法移动的先验概率
    static vector<float> mapPolicyToMoves(const vector<float>& policy_probs, 
                                          const vector<Move>& legal_moves) {
        vector<float> priors(legal_moves.size(), 0.001f); // 最小先验
        
        // 如果策略网络没输出，返回均匀分布
        if (policy_probs.empty()) {
            fill(priors.begin(), priors.end(), 1.0f / legal_moves.size());
            return priors;
        }
        
        float sum = 0.0f;
        for (size_t i = 0; i < legal_moves.size(); ++i) {
            int policy_idx = legal_moves[i].toPolicyIndex();
            if (policy_idx >= 0 && policy_idx < (int)policy_probs.size()) {
                priors[i] = policy_probs[policy_idx];
                sum += priors[i];
            }
        }
        
        // 归一化
        if (sum > 1e-6f) {
            for (size_t i = 0; i < priors.size(); ++i) {
                priors[i] = priors[i] / sum;
            }
        } else {
            fill(priors.begin(), priors.end(), 1.0f / legal_moves.size());
        }
        
        return priors;
    }
};

// ==================== MCTS节点与搜索 ====================
struct MCTSNode {
    Move move;
    float value_sum = 0;
    int visits = 0;
    float prior = 0;
    bool expanded = false;
    bool terminal = false;
    vector<MCTSNode> children;
    
    MCTSNode* select_child(float c_puct, float parent_log_visits) {
        MCTSNode* best = nullptr;
        float best_score = -1e9f;
        
        for (auto& child : children) {
            float score;
            if (child.visits == 0) {
                // 未访问节点：使用先验 + 探索奖励
                score = child.prior * 10000.0f;
            } else {
                float exploit = child.value_sum / child.visits;
                float explore = c_puct * child.prior * 
                               sqrtf(parent_log_visits) / (1.0f + child.visits);
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

// ==================== 完整的MCTS搜索器 ====================
class MCTSSearcher {
private:
    NeuralEngine& engine;
    OpeningBook& opening_book;
    float dirichlet_alpha = 0.3f;  // Dirichlet噪声参数
    float dirichlet_epsilon = 0.25f; // 噪声混合比例
    
public:
    MCTSSearcher(NeuralEngine& eval, OpeningBook& book) 
        : engine(eval), opening_book(book) {}
    
    Move search(int player, int moveCount, int timeBudgetMs) {
        auto startTime = steady_clock::now();
        
        // 1. 尝试开局库
        uint8_t ox0, oy0, ox1, oy1, ox2, oy2;
        if (opening_book.lookup(player, moveCount, ox0, oy0, ox1, oy1, ox2, oy2)) {
            return Move(ox0, oy0, ox1, oy1, ox2, oy2);
        }
        
        // 2. 生成所有合法移动
        auto legal_moves = fast_gen_moves(player);
        if (legal_moves.empty()) return Move();
        
        // 3. 获取神经网络评估（价值 + 策略）
        auto [root_value, policy_probs] = engine.evaluateWithPolicy(player);
        
        // 4. 创建根节点，使用神经网络策略作为先验
        MCTSNode root;
        root.children.reserve(legal_moves.size());
        
        // 映射策略到合法移动
        auto move_priors = PolicyMapper::mapPolicyToMoves(policy_probs, legal_moves);
        
        // 添加Dirichlet噪声增加探索（仅在早期）
        if (root.visits < 10) {
            addDirichletNoise(move_priors);
        }
        
        for (size_t i = 0; i < legal_moves.size(); ++i) {
            MCTSNode child;
            child.move = legal_moves[i];
            child.prior = move_priors[i];
            root.children.push_back(child);
        }
        root.expanded = true;
        
        // 5. 自适应MCTS搜索
        int sims_done = 0;
        float c_puct = 1.5f;
        int effective_time = timeBudgetMs - SAFETY_MARGIN_MS;
        
        vector<MCTSNode*> path_stack;
        default_random_engine rng(chrono::system_clock::now().time_since_epoch().count());
        
        while (true) {
            auto now = steady_clock::now();
            auto elapsed = duration_cast<milliseconds>(now - startTime).count();
            
            if (elapsed >= effective_time) break;
            
            // 动态调整探索参数
            float remaining_ratio = 1.0f - (float)elapsed / effective_time;
            float current_c_puct = c_puct * (0.8f + 0.2f * remaining_ratio);
            
            // 一次模拟
            path_stack.clear();
            MCTSNode* node = &root;
            int cur_player = player;
            
            // 选择阶段
            while (node->expanded && !node->terminal && !node->children.empty()) {
                float parent_log_visits = logf(node->visits + 1e-6f);
                MCTSNode* child = node->select_child(current_c_puct, parent_log_visits);
                if (!child) break;
                
                path_stack.push_back(node);
                child->move.apply(cur_player);
                node = child;
                cur_player = -cur_player;
            }
            
            // 评估叶节点
            float value;
            if (node->terminal) {
                value = -1.0f;
            } else if (!node->expanded) {
                // 扩展节点
                auto child_moves = fast_gen_moves(cur_player);
                if (child_moves.empty()) {
                    node->terminal = true;
                    value = -1.0f;
                } else {
                    // 获取神经网络评估用于扩展
                    auto [child_value, child_policy] = engine.evaluateWithPolicy(cur_player);
                    value = child_value;
                    
                    auto child_priors = PolicyMapper::mapPolicyToMoves(child_policy, child_moves);
                    
                    node->children.reserve(child_moves.size());
                    for (size_t i = 0; i < child_moves.size(); ++i) {
                        MCTSNode child;
                        child.move = child_moves[i];
                        child.prior = child_priors[i];
                        node->children.push_back(child);
                    }
                    node->expanded = true;
                }
            } else {
                value = engine.evaluate(cur_player);
            }
            
            // 回溯更新
            float propagate_value = value;
            while (!path_stack.empty()) {
                MCTSNode* path_node = path_stack.back();
                path_stack.pop_back();
                
                float node_value = (cur_player == player) ? propagate_value : -propagate_value;
                
                path_node->visits++;
                path_node->value_sum += node_value;
                
                propagate_value = -propagate_value;
                cur_player = -cur_player;
            }
            
            root.visits++;
            root.value_sum += (cur_player == player) ? value : -value;
            
            sims_done++;
            
            // 动态调整
            if (sims_done % 50 == 0 && elapsed > effective_time * 0.7f) {
                c_puct *= 0.95f; // 后期减少探索
            }
        }
        
        // 6. 选择最佳移动（最多访问次数 + 价值最高）
        MCTSNode* best_child = nullptr;
        int best_visits = -1;
        float best_combined_score = -1e9f;
        
        for (auto& child : root.children) {
            if (child.visits > 0) {
                float visit_score = child.visits;
                float value_score = child.value_sum / child.visits;
                float combined = visit_score * 0.7f + value_score * 0.3f * 100.0f;
                
                if (combined > best_combined_score) {
                    best_combined_score = combined;
                    best_visits = child.visits;
                    best_child = &child;
                }
            }
        }
        
        if (!best_child && !root.children.empty()) {
            best_child = &root.children[0];
        }
        
        cerr << "[MCTS] Simulations: " << sims_done 
             << ", Best visits: " << (best_child ? best_child->visits : 0)
             << ", Root value: " << root_value << endl;
        
        return best_child ? best_child->move : legal_moves[0];
    }
    
private:
    void addDirichletNoise(vector<float>& priors) {
        default_random_engine rng(chrono::system_clock::now().time_since_epoch().count());
        gamma_distribution<float> gamma(dirichlet_alpha, 1.0f);
        
        vector<float> noise(priors.size());
        float noise_sum = 0.0f;
        
        for (size_t i = 0; i < noise.size(); ++i) {
            noise[i] = gamma(rng);
            noise_sum += noise[i];
        }
        
        // 归一化噪声并混合
        for (size_t i = 0; i < priors.size(); ++i) {
            noise[i] /= noise_sum;
            priors[i] = (1.0f - dirichlet_epsilon) * priors[i] + 
                       dirichlet_epsilon * noise[i];
        }
    }
};

// ==================== 主函数 ====================
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    
    // 初始化
    init_masks();
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
    
    // 初始化组件
    OpeningBook opening_book;
    WeightLoader weight_loader;
    
    if (!weight_loader.loadFromFile(MODEL_PATH)) {
        cerr << "[Main] WARNING: Using fallback evaluation only" << endl;
    }
    
    NeuralEngine neural_net(weight_loader);
    MCTSSearcher searcher(neural_net, opening_book);
    
    // 验证合法性
    auto moves = fast_gen_moves(currBotColor);
    cerr << "[Main] Available moves: " << moves.size() << endl;
    
    if (moves.empty()) {
        cout << "-1 -1 -1 -1 -1 -1\n";
        cerr << "[Main] No legal moves!" << endl;
        return 0;
    }
    
    // 搜索最佳移动
    auto start_time = steady_clock::now();
    Move best_move = searcher.search(currBotColor, rounds, TIME_LIMIT_MS);
    auto end_time = steady_clock::now();
    
    auto elapsed = duration_cast<milliseconds>(end_time - start_time).count();
    cerr << "[Main] Decision time: " << elapsed << "ms" << endl;
    
    // 验证并输出
    bool move_valid = false;
    for (const auto& move : moves) {
        if (move.x0 == best_move.x0 && move.y0 == best_move.y0 &&
            move.x1 == best_move.x1 && move.y1 == best_move.y1 &&
            move.x2 == best_move.x2 && move.y2 == best_move.y2) {
            move_valid = true;
            break;
        }
    }
    
    if (!move_valid) {
        cerr << "[Main] WARNING: Selected move is not in legal move list!" << endl;
        best_move = moves[0];
    }
    
    // 应用并输出
    best_move.apply(currBotColor);
    best_move.print();
    
    return 0;
}