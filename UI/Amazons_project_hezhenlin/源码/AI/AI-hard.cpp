#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <immintrin.h>
#include <assert.h>
#include <inttypes.h>

#define bool  int
#define true  1
#define false 0

#define MAX_COUNT    (2000 * 2000 * 3)  // 最大状态数量
#define OPENING_BOOK_SIZE 64  // 开局库大小

#define BLACK    0  // 黑方
#define WHITE    1  // 白方
#define BOARD_SIZE 8  // 棋盘大小 8x8

#pragma GCC target("popcnt")
#pragma GCC optimize("unroll-loops")
#pragma GCC target("sse,sse2,sse3,ssse3,sse4,popcnt,abm,mmx,avx,avx2")

#ifdef _WIN64 
    #define __inline __inline       // Windows平台内联
#else 
    #define __inline __always_inline // Linux平台强制内联
#endif

uint32_t MaxTime = 930000;  // 最大搜索时间（微秒）

// ====================================================
// 数据结构定义
// ====================================================

/**
 * 状态节点结构体
 * 用于MCTS搜索树中的节点
 */
typedef struct tagState {
    double          quality;     // 节点质量/价值（胜率估计）
    uint32_t        visit;       // 访问次数
    uint32_t        len;         // 子节点数量
    
    uint64_t        board;       // 棋盘状态（1表示有棋子/箭，0表示空位）
    uint64_t        coor[2];     // 黑白双方棋子位置 [0]=黑棋, [1]=白棋
    uint64_t        hash;        // Zobrist哈希值
    
    struct tagState* child;      // 子节点数组（动态分配）
    struct tagState* parent;     // 父节点
} State;

// 全局状态缓存（内存池）
uint32_t StateTop = 0;
State State_cache[MAX_COUNT];

/**
 * 从内存池分配一个新的状态节点
 * @return 分配的状态节点指针
 */
__inline State* _state_alloc() {
    return &State_cache[StateTop++];
}

// 全局变量
clock_t start_time = 0;  // 搜索开始时间
uint32_t me;             // 当前玩家（0=黑，1=白）

// ====================================================
// 开局库定义
// ====================================================

/**
 * 开局库条目
 */
typedef struct {
    uint64_t hash;              // 棋盘哈希值
    uint16_t move_count;        // 推荐的走法数量
    struct {
        uint8_t start_x : 3;    // 起始位置x坐标
        uint8_t start_y : 3;    // 起始位置y坐标
        uint8_t end_x   : 3;    // 目标位置x坐标
        uint8_t end_y   : 3;    // 目标位置y坐标
        uint8_t arrow_x : 3;    // 箭的位置x坐标
        uint8_t arrow_y : 3;    // 箭的位置y坐标
        uint16_t weight : 10;   // 走法权重（胜率*1000）
    } moves[10];                // 推荐的走法（最多10个）
} OpeningBookEntry;

// 内置开局库数据
static OpeningBookEntry builtin_opening_book[] = {
    {
        .hash = 0x1B89874978E6CB5EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 4, .end_y = 1, .arrow_x = 5, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x2E2F0586961525EDULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 5, .end_x = 6, .end_y = 5, .arrow_x = 3, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x2EA6C31983EE1FC1ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 4, .end_x = 2, .end_y = 5, .arrow_x = 0, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x45D7BCF4F7EE46FCULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 7, .end_x = 5, .end_y = 2, .arrow_x = 2, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x499E7A243283F342ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 7, .end_x = 1, .end_y = 7, .arrow_x = 2, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x4E02F747EE5CAA58ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 1, .end_x = 5, .end_y = 0, .arrow_x = 4, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x5797962AD7E000BBULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 6, .end_y = 2, .arrow_x = 4, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x57C998C237840A1AULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 2, .end_x = 5, .end_y = 1, .arrow_x = 1, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x6D864BB6ADA8EA90ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 7, .end_x = 6, .end_y = 6, .arrow_x = 6, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x6E476CE768EC7BD7ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 7, .end_x = 2, .end_y = 7, .arrow_x = 0, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x7113575A96DB2153ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 5, .end_x = 2, .end_y = 3, .arrow_x = 0, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x8AA64B50D1DB2452ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 2, .end_x = 5, .end_y = 1, .arrow_x = 5, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x900D113F95C30A9EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 3, .end_x = 3, .end_y = 4, .arrow_x = 3, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x919A21A3F23E0BE6ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 0, .end_x = 4, .end_y = 1, .arrow_x = 5, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x945631877D01A90FULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 6, .end_y = 1, .arrow_x = 6, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xA75BA8A0EBFFD663ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 4, .end_x = 1, .end_y = 3, .arrow_x = 0, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xAA24FBC8E2A35EBCULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 5, .end_x = 0, .end_y = 4, .arrow_x = 1, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xAEA319669C869047ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 3, .end_x = 0, .end_y = 2, .arrow_x = 1, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xC3D86796DB481187ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 1, .end_x = 5, .end_y = 0, .arrow_x = 4, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x094429040727074EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 6, .end_y = 1, .arrow_x = 5, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x13EB4D360911BAF7ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 6, .end_x = 7, .end_y = 7, .arrow_x = 7, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x1EC8FD5E8536B5A5ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 1, .end_x = 4, .end_y = 0, .arrow_x = 6, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x23EB5D2879CDB80DULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 0, .end_x = 6, .end_y = 0, .arrow_x = 5, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x34983FF321DF26C1ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 2, .end_x = 3, .end_y = 5, .arrow_x = 3, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x37624BB6AC935A6EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 2, .end_x = 6, .end_y = 4, .arrow_x = 5, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x3A28A3393AF415E2ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 0, .end_x = 5, .end_y = 1, .arrow_x = 6, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x3A3EB204DF2416FBULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 0, .end_x = 5, .end_y = 1, .arrow_x = 4, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x400CF27BD9B254D8ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 0, .end_x = 1, .end_y = 0, .arrow_x = 2, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x463C372D13C41D4FULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 0, .end_x = 3, .end_y = 1, .arrow_x = 3, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x4D7A8C39E23CFAAAULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 6, .end_x = 7, .end_y = 4, .arrow_x = 5, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x55BC77E8CFEA3D66ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 0, .end_x = 2, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x5748009514B0B0D8ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 2, .end_x = 6, .end_y = 3, .arrow_x = 4, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x75DAB52463D2EA20ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 4, .end_x = 7, .end_y = 6, .arrow_x = 7, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x90B4BD43F8B9435CULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 0, .end_x = 2, .end_y = 2, .arrow_x = 4, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x989FF7BAA25983AAULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 1, .end_x = 4, .end_y = 0, .arrow_x = 3, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x991A52FCC2E16771ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 0, .end_x = 5, .end_y = 6, .arrow_x = 2, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xA47C2D5739EF9957ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 3, .end_x = 6, .end_y = 4, .arrow_x = 7, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xAA903ED920D09040ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 4, .end_x = 7, .end_y = 3, .arrow_x = 6, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xB69C422873491858ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 0, .end_x = 1, .end_y = 1, .arrow_x = 1, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xBE97627931813C93ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 3, .end_x = 6, .end_y = 2, .arrow_x = 7, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xCDA79C9ED5DFC9BDULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 4, .end_x = 7, .end_y = 5, .arrow_x = 6, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xD2DB7FA2C9DE7E04ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 2, .end_x = 5, .end_y = 2, .arrow_x = 4, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xD564EDAD6AA2A829ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 6, .end_y = 0, .arrow_x = 7, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xDEE8BA893175D75AULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 2, .end_x = 0, .end_y = 0, .arrow_x = 6, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xEEC0DC1C5AE3F8F9ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 5, .end_x = 3, .end_y = 1, .arrow_x = 2, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xC545230B09BCEE1DULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 6, .end_x = 6, .end_y = 7, .arrow_x = 7, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xC5F869ABA8FC3217ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 5, .end_x = 6, .end_y = 6, .arrow_x = 6, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xC71F894383699E55ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 1, .end_x = 5, .end_y = 1, .arrow_x = 6, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xCEC6368EB5F9A23EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 6, .end_x = 7, .end_y = 7, .arrow_x = 6, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xE6FBC4C15110BD01ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 7, .end_x = 1, .end_y = 7, .arrow_x = 0, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xF74A328C2A66848DULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 5, .end_x = 1, .end_y = 5, .arrow_x = 2, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x089C08484E61D50BULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 1, .end_x = 5, .end_y = 0, .arrow_x = 4, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x1AC2E496C6F74488ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 5, .end_x = 2, .end_y = 2, .arrow_x = 7, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x22AA068B990F9167ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 1, .end_x = 7, .end_y = 3, .arrow_x = 7, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x2E8D6A1B48DE9D3FULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 0, .end_x = 7, .end_y = 1, .arrow_x = 6, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x2EAB1D5A3EF1F5ECULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 1, .end_x = 2, .end_y = 0, .arrow_x = 4, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x362295D18792A5F4ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 2, .end_x = 1, .end_y = 2, .arrow_x = 0, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x37926A0BFA4A9033ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 2, .end_x = 5, .end_y = 5, .arrow_x = 1, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x3E75D62E29B07DDDULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 2, .end_x = 4, .end_y = 1, .arrow_x = 2, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x4944DA8916C16D6DULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 3, .end_x = 7, .end_y = 1, .arrow_x = 7, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x4A3AB01A74861978ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 7, .end_x = 4, .end_y = 7, .arrow_x = 2, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x624715DBA07D462BULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 5, .end_x = 3, .end_y = 3, .arrow_x = 1, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x7896102D2FE2830FULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 0, .end_x = 3, .end_y = 0, .arrow_x = 2, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x7CA79650D294D1DCULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 2, .end_x = 3, .end_y = 6, .arrow_x = 1, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x7D5CE4FB4661F7A7ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 1, .end_x = 7, .end_y = 2, .arrow_x = 6, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x81EC872BAB3E2DEAULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 0, .end_x = 6, .end_y = 0, .arrow_x = 5, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x8F14C0103EB758D3ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 5, .end_x = 7, .end_y = 6, .arrow_x = 6, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x991A52FCC2E16771ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 0, .end_x = 2, .end_y = 5, .arrow_x = 1, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x9F7E499F26D9409EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 0, .end_x = 4, .end_y = 1, .arrow_x = 4, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xA97ACC0EEFD160E6ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 1, .end_x = 4, .end_y = 2, .arrow_x = 4, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xB32789C6370BDFD0ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 2, .end_x = 0, .end_y = 2, .arrow_x = 1, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xB528944F2B1D4D5AULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 3, .end_x = 3, .end_y = 1, .arrow_x = 3, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xC9765C3EA5C4C8DAULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 2, .end_x = 2, .end_y = 2, .arrow_x = 4, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xCBC6D12BF6541171ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 7, .end_x = 6, .end_y = 5, .arrow_x = 7, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xD7542243F71EA5F0ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 2, .end_x = 1, .end_y = 2, .arrow_x = 1, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xEB0CF70A75CB972AULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 6, .end_x = 2, .end_y = 7, .arrow_x = 6, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xEC5A51BEAED8A642ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 2, .end_x = 7, .end_y = 1, .arrow_x = 7, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x1919E382873F2DEFULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 1, .end_x = 7, .end_y = 2, .arrow_x = 5, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x1D53907F21128E15ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 2, .end_x = 6, .end_y = 1, .arrow_x = 4, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x1D99B677AFBCDD74ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 4, .end_x = 3, .end_y = 3, .arrow_x = 3, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x1E8B8B80AED9C345ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 0, .end_x = 3, .end_y = 6, .arrow_x = 5, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x23776B32C282AD20ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 3, .end_x = 2, .end_y = 3, .arrow_x = 4, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x3323FC3EDF85C65EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 2, .end_x = 6, .end_y = 1, .arrow_x = 7, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x3821147CA4D794FAULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 4, .end_x = 0, .end_y = 4, .arrow_x = 1, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x38FA5E6296F68589ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 3, .end_x = 0, .end_y = 2, .arrow_x = 0, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x455D6938D8A6E851ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 2, .end_x = 0, .end_y = 0, .arrow_x = 4, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x4728394F9D492CABULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 3, .end_x = 3, .end_y = 3, .arrow_x = 4, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x4FDD7B023460C3A0ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 3, .end_x = 2, .end_y = 3, .arrow_x = 3, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x732C5C8516EEF8E8ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 0, .end_x = 3, .end_y = 0, .arrow_x = 7, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x7444E4AD7CDC1F45ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 3, .end_x = 4, .end_y = 2, .arrow_x = 4, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x74D22CBD172BB47DULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 3, .end_x = 3, .end_y = 4, .arrow_x = 3, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x79AFD3DD8AC9AC4CULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 4, .end_x = 3, .end_y = 3, .arrow_x = 3, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x7A5CF79B5667FA6DULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 6, .end_x = 2, .end_y = 5, .arrow_x = 5, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x8B6C3D991DAE4491ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 1, .end_x = 6, .end_y = 0, .arrow_x = 6, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x991A52FCC2E16771ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 0, .end_x = 5, .end_y = 5, .arrow_x = 6, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x9A03918B5062B01BULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 3, .end_x = 1, .end_y = 4, .arrow_x = 2, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x9E19A0E544FAD7E2ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 2, .end_x = 6, .end_y = 2, .arrow_x = 3, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xA8C665B6064BBF7AULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 3, .end_x = 2, .end_y = 3, .arrow_x = 0, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xB4160A055132E522ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 5, .end_x = 3, .end_y = 3, .arrow_x = 5, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xD542975255F523E6ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 4, .end_x = 0, .end_y = 3, .arrow_x = 0, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xDD7EE26522723C84ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 0, .end_x = 7, .end_y = 0, .arrow_x = 6, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xDDFEC022D858BD93ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 6, .end_x = 2, .end_y = 7, .arrow_x = 1, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xDE0CCB543E9D7A98ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 2, .end_x = 2, .end_y = 4, .arrow_x = 4, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xE99BEA0CEC1F12CFULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 5, .end_x = 0, .end_y = 7, .arrow_x = 5, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xEB902245CE6921D5ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 7, .end_x = 1, .end_y = 6, .arrow_x = 1, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x167158431A198C09ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 1, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x1ABDCAB2B0B6C105ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 3, .end_x = 0, .end_y = 4, .arrow_x = 1, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x2977F11CAA7E8D79ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 2, .end_x = 5, .end_y = 1, .arrow_x = 6, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x353716522A93C19BULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 4, .end_x = 1, .end_y = 4, .arrow_x = 2, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x35B96692B26B0A31ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 4, .end_x = 2, .end_y = 4, .arrow_x = 0, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x49EB256C8EF6656CULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x4AF4607767D975B5ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 2, .end_x = 5, .end_y = 1, .arrow_x = 7, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x57F6BB043AE55DE8ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 6, .end_y = 2, .arrow_x = 4, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x59EB53F46CFE9868ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 6, .end_x = 5, .end_y = 6, .arrow_x = 6, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x62F5C114EC3238BFULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 6, .end_y = 1, .arrow_x = 5, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x69A6E92B380CC0C1ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 0, .end_x = 2, .end_y = 1, .arrow_x = 2, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x7628E8DF7B4C5F68ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 1, .arrow_x = 1, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x841634F91BF63667ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 6, .end_x = 4, .end_y = 7, .arrow_x = 0, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x88C050F105335954ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 1, .end_x = 7, .end_y = 0, .arrow_x = 6, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x8AC57205A890EB7FULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 1, .end_x = 1, .end_y = 0, .arrow_x = 2, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x991A52FCC2E16771ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 0, .end_x = 5, .end_y = 6, .arrow_x = 2, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x994BD3E896F7DFA6ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 5, .end_x = 1, .end_y = 3, .arrow_x = 0, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xA4606B807FB93542ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 7, .end_x = 4, .end_y = 6, .arrow_x = 7, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xAC7B28642803C964ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 2, .end_x = 3, .end_y = 5, .arrow_x = 4, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xB5BD52CC85A97A28ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 6, .end_x = 6, .end_y = 5, .arrow_x = 6, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xC7607F087DBC8439ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 5, .end_y = 2, .arrow_x = 4, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xDB666D6EA7C7DF5CULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 5, .end_x = 7, .end_y = 6, .arrow_x = 7, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xDC4BF3A97ECB587CULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 2, .end_x = 5, .end_y = 1, .arrow_x = 5, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xDDEC6A207DB0A687ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 6, .end_x = 7, .end_y = 7, .arrow_x = 7, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xE69138883EA35BEBULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 5, .end_x = 7, .end_y = 5, .arrow_x = 6, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xFDF12F52D788610AULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 2, .end_x = 5, .end_y = 2, .arrow_x = 3, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x02DBDAE75C36CA28ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 4, .end_x = 5, .end_y = 4, .arrow_x = 5, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x03FF7D46A740E667ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 7, .end_x = 5, .end_y = 1, .arrow_x = 4, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x11BA74212CCD1E20ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 2, .end_x = 0, .end_y = 3, .arrow_x = 0, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x28D46AE8B576F1D9ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 4, .end_x = 7, .end_y = 4, .arrow_x = 7, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x35CDC71F984EE38FULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 1, .end_x = 1, .end_y = 0, .arrow_x = 1, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x37104341CB9EDAACULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 5, .end_x = 3, .end_y = 3, .arrow_x = 3, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x3763F580CFA584B0ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 5, .end_x = 7, .end_y = 3, .arrow_x = 7, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x391E567A27D21AB3ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 6, .end_x = 1, .end_y = 7, .arrow_x = 0, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x441D17D5DDEB149EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 3, .end_x = 2, .end_y = 3, .arrow_x = 3, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x5C06CD65483FD435ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 6, .end_x = 5, .end_y = 7, .arrow_x = 4, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x5D42C0D8C6BB647AULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 3, .end_x = 6, .end_y = 4, .arrow_x = 6, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x5E81EB88F8BBFAA1ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 3, .end_x = 2, .end_y = 3, .arrow_x = 1, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x60DC9DC35F21A0E3ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 7, .end_x = 0, .end_y = 6, .arrow_x = 0, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x6C001D6BA0F5014AULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 1, .end_x = 0, .end_y = 2, .arrow_x = 0, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x791F72457A26F6AFULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 6, .end_x = 6, .end_y = 6, .arrow_x = 6, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x7D5A73EBF3C6461DULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 7, .end_x = 6, .end_y = 6, .arrow_x = 5, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x87734BA8B77ABA5AULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 6, .end_x = 1, .end_y = 7, .arrow_x = 2, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x89CA8172C633121DULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 6, .end_x = 0, .end_y = 7, .arrow_x = 2, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x8C7B1DCD284948AFULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 1, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x8CED80C5B2AB6403ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 7, .end_x = 0, .end_y = 6, .arrow_x = 0, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x9A8951E12E3AC635ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 3, .end_x = 2, .end_y = 2, .arrow_x = 1, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xAFAC28F14FC052DEULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 7, .end_x = 2, .end_y = 6, .arrow_x = 1, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xB13120E56BF1ABDBULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 2, .end_x = 3, .end_y = 3, .arrow_x = 2, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xD0581CEDAB099860ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 1, .end_y = 1, .arrow_x = 6, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xD80606F2D0AC927BULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 5, .end_x = 1, .end_y = 5, .arrow_x = 1, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xDD32EC8AA311C2E4ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 4, .end_x = 7, .end_y = 6, .arrow_x = 7, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xFEB3B71F25815610ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 1, .arrow_x = 0, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x03FF7D46A740E667ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 7, .end_x = 5, .end_y = 2, .arrow_x = 6, .arrow_y = 3, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x10971B5D1B19A143ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 4, .end_x = 1, .end_y = 3, .arrow_x = 2, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x150D4E7E93064C5EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 5, .end_x = 6, .end_y = 4, .arrow_x = 3, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x1642FE9C18A0B3B2ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 1, .end_x = 6, .end_y = 0, .arrow_x = 6, .arrow_y = 1, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x22E0176EBCA727FBULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 2, .end_x = 5, .end_y = 1, .arrow_x = 6, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x27C8327A0F079426ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 4, .end_x = 7, .end_y = 4, .arrow_x = 7, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x3EB1B9DAF2DBA4A2ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 5, .end_x = 2, .end_y = 4, .arrow_x = 5, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x41FEE1302A43568DULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 7, .end_x = 6, .end_y = 7, .arrow_x = 1, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x4D19F57DF0A18858ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 4, .end_x = 5, .end_y = 4, .arrow_x = 4, .arrow_y = 4, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x4EA280D58BF02CC0ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 1, .end_x = 6, .end_y = 1, .arrow_x = 7, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x5DB53ECF1AB12506ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 1, .start_y = 5, .end_x = 2, .end_y = 4, .arrow_x = 2, .arrow_y = 0, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x7EB4DB92B4B0CF28ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 2, .start_y = 4, .end_x = 3, .end_y = 5, .arrow_x = 0, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x8D5501570E3A1E45ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 7, .start_y = 4, .end_x = 4, .end_y = 4, .arrow_x = 4, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0x9E12062E3669A516ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 5, .start_y = 2, .end_x = 6, .end_y = 2, .arrow_x = 2, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xAB10E3EA1AF2D23EULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 4, .end_x = 5, .end_y = 4, .arrow_x = 3, .arrow_y = 2, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xAFD9CD0D6C0A3E1CULL,
        .move_count = 1,
        .moves = {
            { .start_x = 3, .start_y = 7, .end_x = 1, .end_y = 7, .arrow_x = 0, .arrow_y = 7, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xCBB64852BEE60418ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 6, .start_y = 7, .end_x = 3, .end_y = 7, .arrow_x = 2, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xE4EABD528BF372E5ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 0, .start_y = 5, .end_x = 1, .end_y = 5, .arrow_x = 1, .arrow_y = 6, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    },
    {
        .hash = 0xFD4ADBC263053586ULL,
        .move_count = 1,
        .moves = {
            { .start_x = 4, .start_y = 4, .end_x = 6, .end_y = 4, .arrow_x = 7, .arrow_y = 5, .weight = 500 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
            { .start_x = 0, .start_y = 0, .end_x = 0, .end_y = 0, .arrow_x = 0, .arrow_y = 0, .weight = 0 },
        }
    }
};

static const int builtin_opening_book_size = sizeof(builtin_opening_book) / sizeof(OpeningBookEntry);
bool opening_book_loaded = true;  // 内置开局库始终可用

// ====================================================
// Zobrist哈希
// ====================================================

// Zobrist哈希表
static uint64_t zobrist_table[64][3];  // [位置][类型:0=空,1=黑棋,2=白棋]
static uint64_t zobrist_side;          // 轮到哪一方

/**
 * 初始化Zobrist哈希表
 */
void init_zobrist() {
    static bool initialized = false;
    if (initialized) return;
    
    // 使用固定的随机种子以确保可重复性
    srand(0xABCD1234);
    
    // 初始化棋盘位置的随机数
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 3; j++) {
            zobrist_table[i][j] = 0;
            for (int k = 0; k < 4; k++) {
                zobrist_table[i][j] = (zobrist_table[i][j] << 16) | (rand() & 0xFFFF);
            }
        }
    }
    
    // 初始化先手随机数
    zobrist_side = 0;
    for (int k = 0; k < 4; k++) {
        zobrist_side = (zobrist_side << 16) | (rand() & 0xFFFF);
    }
    
    initialized = true;
}

/**
 * 计算棋盘状态的哈希值
 * @param state 棋盘状态
 * @param turn 当前玩家
 * @return 哈希值
 */
uint64_t compute_hash(State* state, int turn) {
    uint64_t hash = 0;
    
    // 处理黑棋
    uint64_t black_pieces = state->coor[BLACK];
    while (black_pieces) {
        int pos = __builtin_ctzll(black_pieces);
        hash ^= zobrist_table[pos][1];  // 黑棋
        black_pieces &= black_pieces - 1;
    }
    
    // 处理白棋
    uint64_t white_pieces = state->coor[WHITE];
    while (white_pieces) {
        int pos = __builtin_ctzll(white_pieces);
        hash ^= zobrist_table[pos][2];  // 白棋
        white_pieces &= white_pieces - 1;
    }
    
    // 处理箭（棋盘上有但既不是黑棋也不是白棋的位置）
    uint64_t arrows = state->board & ~(state->coor[BLACK] | state->coor[WHITE]);
    while (arrows) {
        int pos = __builtin_ctzll(arrows);
        hash ^= zobrist_table[pos][0];  // 箭
        arrows &= arrows - 1;
    }
    
    // 添加当前玩家信息
    if (turn == BLACK) {
        hash ^= zobrist_side;
    }
    
    return hash;
}

/**
 * 更新哈希值（移动棋子）
 * @param old_hash 旧的哈希值
 * @param old_pos 旧位置
 * @param new_pos 新位置
 * @param arrow_pos 箭的位置
 * @param piece_type 棋子类型 (1=黑棋, 2=白棋)
 * @param turn 当前玩家
 * @return 新的哈希值
 */
uint64_t update_hash(uint64_t old_hash, int old_pos, int new_pos, int arrow_pos, 
                     int piece_type, int old_turn, int new_turn) {
    // 移除旧位置的棋子
    old_hash ^= zobrist_table[old_pos][piece_type];
    
    // 添加新位置的棋子
    old_hash ^= zobrist_table[new_pos][piece_type];
    
    // 添加箭
    old_hash ^= zobrist_table[arrow_pos][0];
    
    // 切换玩家（移除旧玩家，添加新玩家）
    if (old_turn != new_turn) {
        old_hash ^= zobrist_side;
    }
    
    return old_hash;
}

// ====================================================
// 开局库查询
// ====================================================

/**
 * 查询开局库
 * @param hash 棋盘哈希值
 * @param entry 输出：找到的开局库条目
 * @return 是否找到匹配项
 */
bool query_opening_book(uint64_t hash, OpeningBookEntry** entry) {
    if (!opening_book_loaded) return false;
    
    // 在内置开局库中线性查找
    for (int i = 0; i < builtin_opening_book_size; i++) {
        if (builtin_opening_book[i].hash == hash && builtin_opening_book[i].move_count > 0) {
            *entry = &builtin_opening_book[i];
            return true;
        }
    }
    
    return false;
}

/**
 * 根据权重随机选择一个开局库走法
 * @param entry 开局库条目
 * @param x0,y0 输出：起始位置
 * @param x1,y1 输出：目标位置
 * @param x2,y2 输出：箭的位置
 * @return 是否成功选择走法
 */
bool select_opening_move(OpeningBookEntry* entry, int* x0, int* y0, 
                         int* x1, int* y1, int* x2, int* y2) {
    if (entry->move_count == 0) return false;
    
    // 计算总权重
    uint32_t total_weight = 0;
    for (int i = 0; i < entry->move_count; i++) {
        total_weight += entry->moves[i].weight;
    }
    
    if (total_weight == 0) return false;
    
    // 随机选择一个走法（基于权重）
    uint32_t random_value = rand() % total_weight;
    uint32_t cumulative_weight = 0;
    
    for (int i = 0; i < entry->move_count; i++) {
        cumulative_weight += entry->moves[i].weight;
        if (random_value < cumulative_weight) {
            *x0 = entry->moves[i].start_x;
            *y0 = entry->moves[i].start_y;
            *x1 = entry->moves[i].end_x;
            *y1 = entry->moves[i].end_y;
            *x2 = entry->moves[i].arrow_x;
            *y2 = entry->moves[i].arrow_y;
            return true;
        }
    }
    
    // 如果上面没有选择到（理论上不会发生），选择第一个
    *x0 = entry->moves[0].start_x;
    *y0 = entry->moves[0].start_y;
    *x1 = entry->moves[0].end_x;
    *y1 = entry->moves[0].end_y;
    *x2 = entry->moves[0].arrow_x;
    *y2 = entry->moves[0].arrow_y;
    return true;
}

// ====================================================
// 调试函数
// ====================================================

/**
 * 打印当前棋盘状态
 * @param state 状态节点
 */
void printBoard(State * state) {
    for (int i = 0; i < 64; i++) {
        // 每行开始前换行
        if (!(i & 0x7)) {
            fprintf(stderr, "\n");
        }
        
        if (state->board & (1ull << i)) {
            // 有棋子的位置
            if (state->coor[me] & (1ull << i)) {
                fprintf(stderr, "A ");  // 己方棋子
            } else if (state->coor[1 - me] & (1ull << i)) {
                fprintf(stderr, "B ");  // 对方棋子
            } else {
                fprintf(stderr, "X ");  // 箭
            }
        } else {
            fprintf(stderr, "_ ");      // 空位
        }
    }
    fprintf(stderr, "\n");
}

/**
 * 简单打印棋盘位图
 * @param board 棋盘位图
 */
void showBoard(uint64_t board) {
    for (int i = 0; i < 64; i++) {
        if (!(i & 0x7)) {
            putchar('\n');
        }
        
        if (board & (1ull << i)) {  
            printf("X ");  // 有障碍
        } else {
            printf("_ ");  // 空位
        }
    }
    puts("");
}

// ====================================================
// 评估引擎
// ====================================================

#define min(x, y) ((x) < (y) ? (x) : (y))

// 距离表的缓存（AVX2对齐）
__attribute__((aligned(32))) uint64_t king_move_me[64];    // 己方国王距离表
__attribute__((aligned(32))) uint64_t king_move_you[64];   // 对方国王距离表
__attribute__((aligned(32))) uint64_t queen_move_me[64];   // 己方皇后距离表
__attribute__((aligned(32))) uint64_t queen_move_you[64];  // 对方皇后距离表

// 最大距离
uint8_t king_move_me_max_dist;
uint8_t king_move_you_max_dist;
uint8_t queen_move_me_max_dist;
uint8_t queen_move_you_max_dist;

// 调试计数器
uint32_t _debug_evaluate_count = 0;
uint32_t _dup_count = 0;

/**
 * 评估函数参数表
 * 不同回合（棋子数量）使用不同的权重参数
 * 每行参数: [领地权重1, 领地权重2, 位置权重1, 位置权重2, 机动性权重]
 */
double network[28][5] = {
    { 0.07747249543793637 ,0.05755603330699520 ,0.64627749023334498 ,0.70431267004292740 ,0.02438131097879579 },
    { 0.05093047840251742 ,0.06276538622537013 ,0.69898059004821581 ,0.66192728970497727 ,0.02362598306372760 },
    { 0.06036622274224539 ,0.06253298199478051 ,0.60094570235521628 ,0.67719126081076242 ,0.01873142786640421 },
    { 0.07597341130849308 ,0.06952095866594065 ,0.69061184234845333 ,0.67989394578528273 ,0.02098781856298665 },
    { 0.08083391263897154 ,0.08815144960484271 ,0.58981849824874917 ,0.54664183543259470 ,0.02318479501373763 },
    { 0.09155731347030857 ,0.08397548702353251 ,0.56392480085083986 ,0.54319242129550227 ,0.02317401477849946 },
    { 0.10653095458609237 ,0.10479793630859575 ,0.54840938009286515 ,0.53023658889860381 ,0.02084758939889652 },
    { 0.11534143744086589 ,0.11515706838023705 ,0.53325566869906469 ,0.52423368303553451 ,0.02237127451593010 },
    { 0.12943854523554690 ,0.12673742164114844 ,0.50841519367287034 ,0.52208373964502879 ,0.02490545306630711 },
    { 0.12882484162931859 ,0.13946973532382280 ,0.49621839819987758 ,0.51776460089353364 ,0.03045473763611049 },
    { 0.13701233819832731 ,0.15338865590616042 ,0.47601466399954588 ,0.51500429509193190 ,0.03249896738636078 },
    { 0.14530543898518938 ,0.15565237403332051 ,0.45365475320199057 ,0.50934623406618500 ,0.03830491784046246 },
    { 0.14521045986025419 ,0.16388365022083374 ,0.44531995327608060 ,0.50517597255948953 ,0.04864124027084386 },
    { 0.13750613208150655 ,0.16326621164859418 ,0.43619350878439399 ,0.50328876650721398 ,0.05912794240603884 },
    { 0.13565263325548560 ,0.15529175902376631 ,0.42382223063419649 ,0.50288212924827379 ,0.07437679521343679 },
    { 0.12382760525087406 ,0.10361944098637088 ,0.50487335391408680 ,0.55808747967333505 ,0.02791980213792046 },
    { 0.11809487853625075 ,0.14632850080535232 ,0.40738388113193924 ,0.41782129616811122 ,0.10308050317730764 },
    { 0.10805473551960752 ,0.15043981450391137 ,0.40520488356004784 ,0.43073574707030956 ,0.10967613304465569 },
    { 0.09668240983912251 ,0.15666221434557865 ,0.40215634987047013 ,0.44165716517577754 ,0.10906426061069142 },
    { 0.10585263971502025 ,0.16319090506614549 ,0.38220029690800922 ,0.45465487463858675 ,0.10062997439277618 },
    { 0.11123671989551248 ,0.15516074827095279 ,0.36904588744714037 ,0.46534418781939937 ,0.09118229977179015 },
    { 0.12535649823409767 ,0.10492555251930048 ,0.35567115915540981 ,0.48043579160677637 ,0.08337580273275977 },
    { 0.28657326967317970 ,0.16655279311197080 ,0.38060545469477008 ,0.42472577515072628 ,0.10316994796202342 },
    { 0.07143084940040888 ,0.16655279311197080 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 },
    { 0.07143084940040888 ,0.16655279311197080 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 },
    { 0.07143084940040888 ,0.16655279311197080 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 },
    { 0.07143084940040888 ,0.16655279311197080 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 },
    { 0.07143084940040888 ,0.14627749023334498 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 }
};

// ====================================================
// 距离计算函数（BFS算法）
// ====================================================

/**
 * 使用AVX2指令计算国王（单步移动）距离表
 * @param state 状态节点
 * @param board 棋盘位图
 * @param coor 起始位置
 * @param can_moves 输出：各距离可到达的位置
 * @param max_dist 输出：最大距离
 */
__inline void king_move_bfs_avx2(State * state, uint64_t board, uint64_t coor, 
                                 uint64_t* can_moves, uint8_t* max_dist) {
    uint8_t  dist = 0;
    uint64_t last_can = coor;
    uint64_t can;
    
    board = ~board;  // 反转，1表示可走位置
    can_moves[dist++] = coor;
    
    // AVX2掩码：处理棋盘边界
    __m256i d0_mask = _mm256_set_epi64x(
        0xfefefefefefefefe,  // 左边界掩码
        0x7f7f7f7f7f7f7f7f,  // 右边界掩码  
        0xffffffffffffffff,  // 垂直掩码
        0xfefefefefefefefe   // 对角线掩码
    );
    
    __m256i d1_mask = _mm256_set_epi64x(
        0x7f7f7f7f7f7f7f7f,
        0xfefefefefefefefe,
        0xffffffffffffffff,
        0x7f7f7f7f7f7f7f7f
    );
    
    __m256i count = _mm256_set_epi64x(1, 7, 8, 9);  // 8个方向的移动步长
    
    // BFS循环
    for (;;) {
        __m256i _pos = _mm256_set1_epi64x(can_moves[dist - 1]);
        
        // 计算8个方向移动后的位置
        __m256i _can = _mm256_or_si256(
            _mm256_or_si256(
                _mm256_srlv_epi64(_mm256_and_si256(_pos, d0_mask), count),
                _mm256_sllv_epi64(_mm256_and_si256(_pos, d1_mask), count)
            ),
            _pos
        );
        
        // 提取结果并与可走位置取交
        can = (
            _mm256_extract_epi64(_can, 0) |
            _mm256_extract_epi64(_can, 1) |
            _mm256_extract_epi64(_can, 2) |
            _mm256_extract_epi64(_can, 3)
        ) & board;
        
        // 如果没有新位置可达，结束BFS
        if (can == last_can) {
            can_moves[dist] = can;
            break;
        }
        
        last_can = can;
        can_moves[dist++] = can;
    }
    
    // 转换为增量形式：can_moves[i] = 距离i可达的位置
    for (int i = dist - 1; i > 1; i--) {
        can_moves[i] ^= can_moves[i - 1];
    }
    
    *max_dist = dist;
}

/**
 * 使用AVX2指令计算皇后（多步移动）距离表
 * @param board 棋盘位图
 * @param coor 起始位置
 * @param can_moves 输出：各距离可到达的位置
 * @param max_dist 输出：最大距离
 */
__inline void queen_move_bfs_avx2(uint64_t board, uint64_t coor, 
                                  uint64_t* can_moves, uint8_t* max_dist) {
    uint8_t  dist = 0;
    uint64_t can;
    uint64_t last_can = coor;
    
    board = ~board;
    can_moves[dist++] = coor;
    
    // AVX2掩码和步长
    __m256i d0_mask = _mm256_set_epi64x(
        0xfefefefefefefefe,
        0x7f7f7f7f7f7f7f7f,
        0xffffffffffffffff,
        0xfefefefefefefefe
    );
    
    __m256i d1_mask = _mm256_set_epi64x(
        0x7f7f7f7f7f7f7f7f,
        0xfefefefefefefefe,
        0xffffffffffffffff,
        0x7f7f7f7f7f7f7f7f
    );
    
    __m256i count = _mm256_set_epi64x(1, 7, 8, 9);
    __m256i and_mask = _mm256_set1_epi64x(board);
    
    // BFS循环
    for (;;) {
        __m256i _d0_pos[8], _d1_pos[8], _can;
        
        can = 0;
        _d0_pos[0] = _mm256_set1_epi64x(can_moves[dist - 1]);
        _d1_pos[0] = _mm256_set1_epi64x(can_moves[dist - 1]);
        
        // 计算8个方向上最多7步的移动
        for (int i = 1; i <= 7; i++) {
            _d0_pos[i] = _mm256_and_si256(
                _mm256_srlv_epi64(_mm256_and_si256(_d0_pos[i - 1], d0_mask), count),
                and_mask
            );
            _d1_pos[i] = _mm256_and_si256(
                _mm256_sllv_epi64(_mm256_and_si256(_d1_pos[i - 1], d1_mask), count),
                and_mask
            );
        }
        
        // 合并所有方向所有步数的结果
        _can = _mm256_or_si256(
            _mm256_or_si256(
                _mm256_or_si256(
                    _mm256_or_si256(_d0_pos[0], _d0_pos[1]),
                    _mm256_or_si256(_d0_pos[2], _d0_pos[3])
                ),
                _mm256_or_si256(
                    _mm256_or_si256(_d0_pos[4], _d0_pos[5]),
                    _mm256_or_si256(_d0_pos[6], _d0_pos[7])
                )
            ),
            _mm256_or_si256(
                _mm256_or_si256(
                    _mm256_or_si256(_d1_pos[0], _d1_pos[1]),
                    _mm256_or_si256(_d1_pos[2], _d1_pos[3])
                ),
                _mm256_or_si256(
                    _mm256_or_si256(_d1_pos[4], _d1_pos[5]),
                    _mm256_or_si256(_d1_pos[6], _d1_pos[7])
                )
            )
        );
   
        // 提取结果
        can = (
            _mm256_extract_epi64(_can, 0) |
            _mm256_extract_epi64(_can, 1) |
            _mm256_extract_epi64(_can, 2) |
            _mm256_extract_epi64(_can, 3)
        ) & board;
        
        // 收敛检查
        if (can == last_can) {
            can_moves[dist] = can;
            break;
        }
        
        last_can = can;
        can_moves[dist++] = can;
    }
    
    // 转换为增量形式
    for (int i = dist - 1; i > 1; i--) {
        can_moves[i] ^= can_moves[i - 1];
    }
    
    *max_dist = dist;
}

// ====================================================
// 特征计算函数
// ====================================================

/**
 * 计算皇后移动的机动性（自由度）
 * @param board 棋盘位图
 * @param coor 皇后位置
 * @return 机动性分数
 */
__inline double calc_mobility_avx2(uint64_t board, uint64_t coor) {
    board = ~board;  // 可走位置
    double m = 0;
    
    // AVX2掩码和步长
    __m256i d0_mask = _mm256_set_epi64x(
        0xfefefefefefefefe,
        0x7f7f7f7f7f7f7f7f,
        0xffffffffffffffff,
        0xfefefefefefefefe
    );
    
    __m256i d1_mask = _mm256_set_epi64x(
        0x7f7f7f7f7f7f7f7f,
        0xfefefefefefefefe,
        0xffffffffffffffff,
        0x7f7f7f7f7f7f7f7f
    );
    
    __m256i count = _mm256_set_epi64x(1, 7, 8, 9);
    __m256i and_mask = _mm256_set1_epi64x(board);
    
    __m256i _d0_pos, _d1_pos;
    _d0_pos = _d1_pos = _mm256_set1_epi64x(coor);
    
    // 计算1-7步的移动能力
    for (int i = 1; i <= 7; i++) {
        // 向8个方向移动i步
        _d0_pos = _mm256_and_si256(
            _mm256_srlv_epi64(_mm256_and_si256(_d0_pos, d0_mask), count),
            and_mask
        );
        _d1_pos = _mm256_and_si256(
            _mm256_sllv_epi64(_mm256_and_si256(_d1_pos, d1_mask), count),
            and_mask
        );
        
        // 合并结果
        __m256i _can = _mm256_or_si256(_d0_pos, _d1_pos);
        
        uint64_t u64_can = 
            _mm256_extract_epi64(_can, 0) |
            _mm256_extract_epi64(_can, 1) |
            _mm256_extract_epi64(_can, 2) |
            _mm256_extract_epi64(_can, 3);
        
        // 计算下一步的可选位置
        __m256i space = _mm256_or_si256(
            _mm256_and_si256(
                _mm256_srlv_epi64(_mm256_and_si256(_mm256_set1_epi64x(u64_can), d0_mask), count),
                and_mask
            ),
            _mm256_and_si256(
                _mm256_sllv_epi64(_mm256_and_si256(_mm256_set1_epi64x(u64_can), d1_mask), count),
                and_mask
            )
        );
        
        uint64_t u64Space = (
            _mm256_extract_epi64(space, 0) |
            _mm256_extract_epi64(space, 1) |
            _mm256_extract_epi64(space, 2) |
            _mm256_extract_epi64(space, 3)
        );
        
        // 距离越远，权重越小
        m += __builtin_popcountll(u64Space) / (i * 1.0);
    }
    
    return m;
}

/**
 * 计算皇后位置优势
 * 公式: 2 * Σ(2^(-距离)) * 棋子数
 * @param can_moves_arr_me 己方距离表
 * @param me_len 己方最大距离
 * @param can_moves_arr_you 对方距离表
 * @param you_len 对方最大距离
 * @return 位置优势分数
 */
__inline double calc_queen_position(uint64_t* can_moves_arr_me, uint8_t me_len,
                                   uint64_t* can_moves_arr_you, uint8_t you_len) {
    double ret = 0, f_pow;
    uint64_t can, off, _pow;
    
    union {
        uint64_t u;
        double d;
    } converter;

    // 计算己方位置优势
    for (int i = 1; i < me_len; i++) {
        can = can_moves_arr_me[i];
        off = i;
        // 快速计算2^(-off): 通过浮点数位操作
        _pow = 0x3ff0000000000000ull - (off << 52);
        converter.u = _pow;
        f_pow = converter.d;
        ret += __builtin_popcountll(can) * f_pow;
    }
    
    // 减去对方位置优势
    for (int i = 1; i < you_len; i++) {
        can = can_moves_arr_you[i];
        off = i;
        _pow = 0x3ff0000000000000ull - (off << 52);
        converter.u = _pow;
        f_pow = converter.d;
        ret -= __builtin_popcountll(can) * f_pow;
    }
    
    return 2 * ret;
}

/**
 * 计算国王位置优势
 * @param can_moves_arr_me 己方国王距离表
 * @param me_len 己方最大距离
 * @param can_moves_arr_you 对方国王距离表
 * @param you_len 对方最大距离
 * @return 国王位置优势分数
 */
__inline double calc_king_position(uint64_t* can_moves_arr_me, uint8_t me_len,
                                  uint64_t* can_moves_arr_you, uint8_t you_len) {
    double p = 0;
    
    // 计算共同可达区域和独占区域
    uint64_t _common = (can_moves_arr_me[me_len] & can_moves_arr_you[you_len]);
    uint64_t _me_can  = (can_moves_arr_me[me_len] & (~_common));
    uint64_t _you_can = (can_moves_arr_you[you_len] & (~_common));
    
    // 在共同区域中，距离越近优势越大
    for (int i = 1; i < me_len; i++) {
        p -= __builtin_popcountll(can_moves_arr_me[i] & _common) * i / 6.0;
    }
    
    for (int i = 1; i < you_len; i++) {
        p += __builtin_popcountll(can_moves_arr_you[i] & _common) * i / 6.0;
    }
    
    // 独占区域直接算作优势
    p += __builtin_popcountll(_me_can) - __builtin_popcountll(_you_can);
    
    return p;
}

/**
 * 快速计算领地控制
 * BFS比较双方谁能更快到达每个位置
 * @param can_moves_arr_me 己方距离表
 * @param me_len 己方最大距离
 * @param can_moves_arr_you 对方距离表
 * @param you_len 对方最大距离
 * @return 领地控制分数（己方占优为正）
 */
__inline int32_t calc_territory_fast(uint64_t* can_moves_arr_me, uint8_t me_len,
                                    uint64_t* can_moves_arr_you, uint8_t you_len) {
    int32_t t = 0;
    uint64_t visited = 0;  // 已分配的区域
    
    // 比较相同距离的到达能力
    for (int i = 1; i < me_len && i < you_len; i++) {
        // 己方能先到达的位置
        t += __builtin_popcountll(can_moves_arr_me[i] & (~visited));
        // 对方能先到达的位置
        t -= __builtin_popcountll(can_moves_arr_you[i] & (~visited));
        // 标记已分配
        visited |= can_moves_arr_me[i] | can_moves_arr_you[i];
    }
    
    // 己方有更远距离（能到达对方不能到达的区域）
    for (int i = me_len; i < you_len; i++) {
        t -= __builtin_popcountll(can_moves_arr_you[i] & (~visited));
        visited |= can_moves_arr_you[i];
    }
    
    // 对方有更远距离
    for (int i = you_len; i < me_len; i++) {
        t += __builtin_popcountll(can_moves_arr_me[i] & (~visited));
        visited |= can_moves_arr_me[i];
    }
    
    return t;
}

/**
 * 超时检查
 * @return true如果超时，否则false
 */
__inline bool timeout() {
    return (((clock() - start_time)) > MaxTime);
}

// ====================================================
// 主评估函数
// ====================================================

/**
 * 综合评估函数
 * 计算当前局面对己方的胜率估计（0-1之间）
 * @param state 状态节点
 * @return 胜率估计（0=必输，1=必赢，0.5=均势）
 */
__inline double evaluate(State* state) {
    uint64_t board = state->board;
    ++_debug_evaluate_count;
    
    double score = 0.0, t1 = 0, t2 = 0, p1 = 0, p2 = 0, m0 = 0;
    
    // 1. 计算国王和皇后的距离表
    king_move_bfs_avx2(state, board, state->coor[me], 
                       king_move_me, &king_move_me_max_dist);
    king_move_bfs_avx2(state, board, state->coor[1 - me], 
                       king_move_you, &king_move_you_max_dist);
    
    queen_move_bfs_avx2(board, state->coor[me], 
                        queen_move_me, &queen_move_me_max_dist);
    queen_move_bfs_avx2(board, state->coor[1 - me], 
                        queen_move_you, &queen_move_you_max_dist);
    
    // 2. 计算各种特征
    t1 = calc_territory_fast(queen_move_me, queen_move_me_max_dist,
                            queen_move_you, queen_move_you_max_dist);
    t2 = calc_territory_fast(king_move_me, king_move_me_max_dist,
                            king_move_you, king_move_you_max_dist);
    
    p1 = calc_queen_position(queen_move_me, queen_move_me_max_dist,
                            queen_move_you, queen_move_you_max_dist);
    p2 = calc_king_position(king_move_me, king_move_me_max_dist,
                           king_move_you, king_move_you_max_dist);
    
    m0 = calc_mobility_avx2(board, state->coor[me]) - 
         calc_mobility_avx2(board, state->coor[1 - me]);
    
    // 3. 根据回合数选择权重参数
    uint32_t evaluate_turn = (__builtin_popcountll(state->board) - 8) >> 1;
    
    // 4. 加权求和得到原始分数
    score = (
        network[evaluate_turn][0] * t1 + 
        network[evaluate_turn][1] * t2 +
        network[evaluate_turn][2] * p1 + 
        network[evaluate_turn][3] * p2 +
        network[evaluate_turn][4] * m0
    ) * 0.20;  // 全局缩放因子
    
    // 5. 通过sigmoid函数转换为胜率
    return 1 / (1 + exp(-score));
}

// ====================================================
// 走法生成（整合哈希更新）
// ====================================================

/**
 * 计算皇后可以移动到的所有位置（一步之内）
 * @param board 棋盘位图
 * @param coor 皇后当前位置
 * @return 可移动位置的位图
 */
__inline uint64_t get_queen_can_moves_avx2(uint64_t board, uint64_t coor) {
    __m256i _d0_pos, _d1_pos, _can = {0};
    uint64_t can;
    
    // AVX2掩码和步长
    __m256i d0_mask = _mm256_set_epi64x(
        0xfefefefefefefefe,
        0x7f7f7f7f7f7f7f7f,
        0xffffffffffffffff,
        0xfefefefefefefefe
    );
    
    __m256i d1_mask = _mm256_set_epi64x(
        0x7f7f7f7f7f7f7f7f,
        0xfefefefefefefefe,
        0xffffffffffffffff,
        0x7f7f7f7f7f7f7f7f
    );
    
    __m256i count = _mm256_set_epi64x(1, 7, 8, 9);
    __m256i and_mask = _mm256_set1_epi64x(board);
    
    _d0_pos = _d1_pos = _mm256_set1_epi64x(coor);
    
    // 向8个方向最多走7步
    for (int i = 1; i <= 7; i++) {
        _d0_pos = _mm256_and_si256(
            _mm256_srlv_epi64(_mm256_and_si256(_d0_pos, d0_mask), count),
            and_mask
        );
        _d1_pos = _mm256_and_si256(
            _mm256_sllv_epi64(_mm256_and_si256(_d1_pos, d1_mask), count),
            and_mask
        );
        
        _can = _mm256_or_si256(
            _mm256_or_si256(_d0_pos, _d1_pos),
            _can
        );
    }
    
    can = (
        _mm256_extract_epi64(_can, 0) |
        _mm256_extract_epi64(_can, 1) |
        _mm256_extract_epi64(_can, 2) |
        _mm256_extract_epi64(_can, 3)
    ) & board;
    
    return can;
}

/**
 * 生成当前玩家的所有合法走法（整合哈希）
 * @param state 当前状态
 * @param turn 当前玩家（0=黑，1=白）
 */
__inline void generate_moves_with_hash(State* state, int turn) {
    uint64_t b0 = state->board;
    uint64_t chess;
    State* l = NULL;
    uint64_t gen_count = 0;
    
    chess = state->coor[turn];
    
    // 遍历所有己方棋子
    for (; chess;) {
        uint64_t chess_coor = chess & (-chess);  // 提取最低位的棋子
        uint64_t can_moves = 0;
        chess ^= chess_coor;  // 移除当前棋子
        
        // 计算当前棋子可以移动的位置
        can_moves = get_queen_can_moves_avx2(~b0, chess_coor);
        
        // 枚举所有可能的移动位置
        for (; can_moves;) {
            uint64_t new_pos = can_moves & (-can_moves);  // 提取一个目标位置
            can_moves ^= new_pos;  // 移除已处理的位置
            
            // 移动棋子到新位置
            b0 ^= (new_pos ^ chess_coor);
            
            // 计算可以放箭的位置
            uint64_t arrow_can = get_queen_can_moves_avx2(~b0, new_pos);
            
            // 枚举所有放箭位置
            for (; arrow_can;) {
                uint64_t arrow_pos = arrow_can & (-arrow_can);
                arrow_can -= arrow_pos;
                
                // 放置箭
                b0 ^= arrow_pos;
                
                // 创建新状态节点
                State* new_state = _state_alloc();
                ++gen_count;
                
                // 初始化子节点链表
                if (__glibc_unlikely(l == NULL)) {
                    l = new_state;
                }
                
                // 设置新状态
                new_state->board = b0;
                new_state->coor[0] = state->coor[0];
                new_state->coor[1] = state->coor[1];
                new_state->coor[turn] ^= (new_pos ^ chess_coor);
                new_state->parent = state;
                
                // 计算哈希值
                int start_pos = __builtin_ctzll(chess_coor);
                int end_pos = __builtin_ctzll(new_pos);
                int arrow_pos_idx = __builtin_ctzll(arrow_pos);
                int piece_type = (turn == BLACK) ? 1 : 2;
                
                // 如果有父节点的哈希值，就更新，否则计算新的
                if (state->hash != 0) {
                    new_state->hash = update_hash(state->hash, start_pos, end_pos, 
                                                 arrow_pos_idx, piece_type, turn, 1 - turn);
                } else {
                    // 如果没有父节点哈希，计算完整哈希
                    new_state->hash = compute_hash(new_state, 1 - turn);
                }
                
                // 评估新状态（己方视角的胜率）
                new_state->quality = (me == (uint32_t)turn) ? 
                    evaluate(new_state) : 1 - evaluate(new_state);
                
                // 撤销放箭
                b0 ^= arrow_pos;
            }
            
            // 撤销棋子移动
            b0 ^= (new_pos ^ chess_coor);
        }
    }
    
    // 更新状态信息
    state->len = gen_count;
    state->child = l;
}

// ====================================================
// MCTS核心算法（整合开局库）
// ====================================================

double C;  // UCT探索常数
uint64_t debug_uct_count = 0;

/**
 * 计算UCT（Upper Confidence Bound for Trees）值
 * 用于在MCTS中选择最有潜力的节点
 * 公式: Q/(1+N) + C*sqrt(ln(Np)/(1+N))
 * @param state 子节点状态
 * @return UCT值
 */
__inline double UCT(State* state) {
    ++debug_uct_count;
    return state->quality / (1 + state->visit) +
           C * __builtin_sqrt(__builtin_log(state->parent->visit) / (1 + state->visit));
}

// 快速对数计算的辅助宏和函数
#define LOG_POLY_DEGREE 6

#define POLY0(x, c0) _mm_set1_ps(c0)
#define POLY1(x, c0, c1) _mm_add_ps(_mm_mul_ps(POLY0(x, c1), x), _mm_set1_ps(c0))
#define POLY2(x, c0, c1, c2) _mm_add_ps(_mm_mul_ps(POLY1(x, c1, c2), x), _mm_set1_ps(c0))
#define POLY3(x, c0, c1, c2, c3) _mm_add_ps(_mm_mul_ps(POLY2(x, c1, c2, c3), x), _mm_set1_ps(c0))
#define POLY4(x, c0, c1, c2, c3, c4) _mm_add_ps(_mm_mul_ps(POLY3(x, c1, c2, c3, c4), x), _mm_set1_ps(c0))
#define POLY5(x, c0, c1, c2, c3, c4, c5) _mm_add_ps(_mm_mul_ps(POLY4(x, c1, c2, c3, c4, c5), x), _mm_set1_ps(c0))

/**
 * 快速log2计算（SIMD版本）
 * 使用多项式近似
 */
__m128 log2f4(__m128 x) {
    __m128i exp = _mm_set1_epi32(0x7F800000);
    __m128i mant = _mm_set1_epi32(0x007FFFFF);
    __m128 one = _mm_set1_ps(1.0f);
    __m128i i = _mm_castps_si128(x);
    
    // 提取指数部分
    __m128 e = _mm_cvtepi32_ps(
        _mm_sub_epi32(
            _mm_srli_epi32(_mm_and_si128(i, exp), 23),
            _mm_set1_epi32(127)
        )
    );
    
    // 提取尾数部分并归一化到[1,2)
    __m128 m = _mm_or_ps(_mm_castsi128_ps(_mm_and_si128(i, mant)), one);
    __m128 p;
    
    // 多项式近似（不同阶数选择）
#if LOG_POLY_DEGREE == 6
    p = POLY5(m, 3.1157899f, -3.3241990f, 2.5988452f, -1.2315303f, 3.1821337e-1f, -3.4436006e-2f);
#elif LOG_POLY_DEGREE == 5
    p = POLY4(m, 2.8882704548164776201f, -2.52074962577807006663f, 
              1.48116647521213171641f, -0.465725644288844778798f, 0.0596515482674574969533f);
#elif LOG_POLY_DEGREE == 4
    p = POLY3(m, 2.61761038894603480148f, -1.75647175389045657003f, 
              0.688243882994381274313f, -0.107254423828329604454f);
#elif LOG_POLY_DEGREE == 3
    p = POLY2(m, 2.28330284476918490682f, -1.04913055217340124191f, 0.204446009836232697516f);
#else
    #error "未定义的LOG_POLY_DEGREE"
#endif
    
    // 调整多项式确保log2(1)=0
    p = _mm_mul_ps(p, _mm_sub_ps(m, one));
    return _mm_add_ps(p, e);
}

/**
 * 快速自然对数计算（SIMD版本）
 * 将4个整数转换为自然对数
 */
__m256d ln(uint32_t v0, uint32_t v1, uint32_t v2, uint32_t v3) {
    return _mm256_cvtps_pd(
        _mm_div_ps(
            log2f4(_mm_cvtepi32_ps(_mm_set_epi32(v0, v1, v2, v3))),
            log2f4(_mm_set1_ps(2.718281828459045))
        )
    );
}

/**
 * 使用SIMD加速选择UCT值最大的子节点
 * @param c 子节点数组起始指针
 * @param len 子节点数量
 * @return 具有最大UCT值的子节点指针
 */
__inline State* get_uct_max_state(State* c, int64_t len) {
    __m256d max_uct = {0};
    __m256d max_addr = {0};
    double _max_uct = 0;
    State* _max_state = NULL;
    double ucts[4];
    
    // 使用AVX2处理4个节点一组
    if (len >= 4) {
        for (; len >= 4; len -= 4) {
            // 计算分母: 1 + visit_count
            __m256d den = _mm256_cvtepi32_pd(_mm_add_epi32(
                _mm_set_epi32(c[0].visit, c[1].visit, c[2].visit, c[3].visit),
                _mm_set1_epi32(1)
            ));
            
            // 计算UCT值: Q/(1+N) + C*sqrt(ln(Np)/(1+N))
            __m256d cur_value = _mm256_add_pd(
                _mm256_div_pd(
                    _mm256_set_pd(c[0].quality, c[1].quality, c[2].quality, c[3].quality),
                    den
                ),
                _mm256_mul_pd(
                    _mm256_set1_pd(C),
                    _mm256_sqrt_pd(
                        _mm256_div_pd(
                            ln(c[0].parent->visit, c[1].parent->visit, 
                               c[2].parent->visit, c[3].parent->visit),
                            den
                        )
                    )
                )
            );
            
            // 保存节点地址
            __m256d cur_addr = _mm256_castsi256_pd(
                _mm256_set_epi64x((uint64_t)c, (uint64_t)(c + 1),
                                 (uint64_t)(c + 2), (uint64_t)(c + 3))
            );
            
            // 比较并选择最大值
            __m256d mask = _mm256_cmp_pd(cur_value, max_uct, _CMP_GT_OQ);
            max_uct = _mm256_blendv_pd(max_uct, cur_value, mask);
            max_addr = _mm256_blendv_pd(max_addr, cur_addr, mask);
            
            c += 4;
        }
        
        // 提取SIMD寄存器中的结果
        _mm256_storeu_pd(ucts, max_uct);
        
        // 找出4个值中的最大值
        if (ucts[0] > _max_uct) {
            _max_uct = ucts[0];
            _max_state = (State*)_mm256_extract_epi64(_mm256_castpd_si256(max_addr), 0);
        }
        if (ucts[1] > _max_uct) {
            _max_uct = ucts[1];
            _max_state = (State*)_mm256_extract_epi64(_mm256_castpd_si256(max_addr), 1);
        }
        if (ucts[2] > _max_uct) {
            _max_uct = ucts[2];
            _max_state = (State*)_mm256_extract_epi64(_mm256_castpd_si256(max_addr), 2);
        }
        if (ucts[3] > _max_uct) {
            _max_uct = ucts[3];
            _max_state = (State*)_mm256_extract_epi64(_mm256_castpd_si256(max_addr), 3);
        }
    }
    
    // 处理剩余节点
    while (len > 0) {
        double _uct = UCT(c);
        if (_uct >= _max_uct) {
            _max_uct = _uct;
            _max_state = c;
        }
        c++;
        --len;
    }
    
    return _max_state;
}

/**
 * 扩展节点：为叶子节点生成所有子节点（整合哈希）
 * @param state 要扩展的节点
 * @param next_turn 下一个行动的玩家
 */
__inline void expand_with_hash(State* state, int next_turn) {
    // 检查是否是终局状态
    if (state->coor[0] != 0xffffffffffffffff) {
        generate_moves_with_hash(state, next_turn);
        
        // 如果无棋可走，创建终局节点
        if (state->child == NULL) {
            state->child = _state_alloc();
            state->len = 1;
            
            state->child->coor[0] = 0xffffffffffffffff;  // 终局标记
            state->child->parent = state;
            state->child->quality = 0;  // 终局价值为0（输）
            state->child->hash = 0;     // 终局不需要哈希
        }
    }
}

/**
 * 选择阶段：从根节点到叶子节点选择路径
 * @param leaf 起始节点（通常是根节点）
 * @param next_turn 输出：到达叶子节点时的玩家
 * @return 选择的叶子节点
 */
__inline State* select_with_hash(State* leaf, int* next_turn) {
    while (leaf->child) {
        leaf = get_uct_max_state(leaf->child, leaf->len);
        *next_turn = 1 - *next_turn;  // 切换玩家
    }
    return leaf;
}

/**
 * 回传阶段：将模拟结果沿路径回传更新统计信息
 * @param state 叶子节点（模拟开始的节点）
 * @param next_turn 模拟时的玩家（未使用但保留接口）
 */
__inline void backup(State* state, int* next_turn) {
    double value = state->quality;
    
    // 更新叶子节点
    state->visit++;
    state = state->parent;
    value = 1 - value;  // 切换视角（对手的价值是1-己方价值）
    
    // 沿路径向上更新所有祖先节点
    while (state) {
        state->visit++;
        state->quality += value;
        
        state = state->parent;
        value = 1 - value;  // 每层切换视角
    }
}

/**
 * 主MCTS搜索函数（整合开局库）
 * @param root 根节点（当前局面）
 * @param current_hash 当前棋盘的哈希值
 */
__inline void MCTS_with_opening(State* root, uint64_t current_hash) {
    // 首先检查开局库
    OpeningBookEntry* book_entry = NULL;
    if (query_opening_book(current_hash, &book_entry)) {
        int x0, y0, x1, y1, x2, y2;
        if (select_opening_move(book_entry, &x0, &y0, &x1, &y1, &x2, &y2)) {
            // 输出开局库走法
            printf("%d %d %d %d %d %d\n", x0, y0, x1, y1, x2, y2);
            printf("elapse: 0.000000, ");  // 开局库立即返回
            printf("iterator: 0, ");       // 没有进行MCTS搜索
            printf("evaluate: 0, ");       // 没有进行评估
            printf("call_uct_count: 0 \n"); // 没有调用UCT
            printf("info: opening book move\n"); // 额外信息
            return;
        }
    }
    
    // 如果没有在开局库中找到，执行原来的MCTS搜索
    State* leaf, * action = NULL;
    uint64_t diff;
    uint8_t start, result, arrow;
    uint32_t max_visit = 0;
    int next_turn, iteration = 0;
    
    start_time = clock();  // 开始计时
    
    // 确保根节点有哈希值
    if (root->hash == 0) {
        root->hash = current_hash;
    }
    
    // 主搜索循环
    for (; !timeout();) {
        next_turn = me;
        
        // 1. 选择阶段
        leaf = select_with_hash(root, &next_turn);
        
        // 2. 扩展阶段（如果节点已被访问过）
        if (leaf->visit) {
            expand_with_hash(leaf, next_turn);
            leaf = select_with_hash(leaf, &next_turn);
        }
        
        // 3. 回传阶段
        backup(leaf, &next_turn);
        iteration++;
    }
    
    // 4. 决策阶段：选择访问次数最多的子节点
    State* c = root->child;
    for (uint32_t i = 0; i < root->len; i++, c++) {
        if (action == NULL || c->visit > max_visit) {
            max_visit = c->visit;
            action = c;
        }
    }
    
    // 解码走法信息
    diff = root->coor[me] ^ action->coor[me];
    start = __builtin_ctzll(root->coor[me] & diff);      // 起始位置
    result = __builtin_ctzll(action->coor[me] & diff);   // 目标位置
    arrow = __builtin_ctzll(root->board ^ diff ^ action->board);  // 箭的位置
    
    // 输出走法（棋盘坐标）
    printf("%d %d %d %d %d %d\n",
           start & 0x7,        // x1
           start >> 3,         // y1
           result & 0x7,       // x2
           result >> 3,        // y2
           arrow & 0x7,        // x3
           arrow >> 3          // y3
    );
    
    // 输出调试信息
    printf("elapse: %lf, ", (clock() - start_time) * 1.0 / CLOCKS_PER_SEC);
    printf("iterator: %d, ", iteration);
    printf("evaluate: %d, ", _debug_evaluate_count);
    printf("call_uct_count: %" PRIu64 " \n", debug_uct_count);
    printf("info: mcts move\n"); // 额外信息
}

// ====================================================
// 棋盘操作和主函数
// ====================================================

/**
 * 执行一步走法（增加哈希更新）
 * @param state 当前状态
 * @param x0,y0 起始位置
 * @param x1,y1 目标位置
 * @param x2,y2 箭的位置
 * @param who 执行走法的玩家
 * @param current_turn 当前玩家
 * @param old_hash 旧的哈希值
 * @return 新的哈希值
 */
uint64_t move_with_hash(State* state, int x0, int y0, int x1, int y1, int x2, int y2, 
                        int who, int current_turn, uint64_t old_hash) {
    assert(0 == ((~0x7) & (x0 | x1 | x2 | y0 | y1 | y2)));  // 坐标必须在0-7范围内
    
    int old_pos = (y0 << 3) | x0;
    int new_pos = (y1 << 3) | x1;
    int arrow_pos = (y2 << 3) | x2;
    int piece_type = (who == BLACK) ? 1 : 2;
    
    // 更新棋盘位图：移除起始位置，添加目标位置和箭
    state->board ^= (1ull << old_pos) ^ 
                    (1ull << new_pos) ^ 
                    (1ull << arrow_pos);
    
    // 更新棋子位置：移动棋子
    state->coor[who] ^= (1ull << old_pos) ^ 
                        (1ull << new_pos);
    
    // 更新哈希值
    return update_hash(old_hash, old_pos, new_pos, arrow_pos, 
                      piece_type, current_turn, 1 - current_turn);
}

/**
 * 主函数
 * 读取输入，初始化棋盘，执行MCTS搜索
 */
int main(int argc, char* argv[]) {
    State input_state = { 0 };
    int turnID;
    
    // 初始化Zobrist哈希
    init_zobrist();
    
    // 内置开局库始终可用
    opening_book_loaded = true;
    
    // 初始化棋盘（标准起始位置）
    // 黑棋位置: (2,0), (5,0), (0,2), (7,2)
    input_state.board |= (1ull << 2) | (1ull << 5) | (1ull << 16) | (1ull << 23);
    input_state.coor[BLACK] = input_state.board;
    
    // 白棋位置: (0,5), (7,5), (2,7), (5,7)
    input_state.board |= (1ull << 40) | (1ull << 47) | (1ull << 58) | (1ull << 61);
    input_state.coor[WHITE] = input_state.board ^ input_state.coor[BLACK];
    
    // 当前哈希值
    uint64_t current_hash = 0;
    
    // 读取回合信息
    if (scanf("%d", &turnID) != 1) {
        fprintf(stderr, "Error reading turnID\n");
        return 1;
    }
    me = WHITE;  // 默认白方开始
    
    // 第一回合时间加倍
    if (turnID == 1) {
        MaxTime <<= 1;
    }
    
    // 回放历史走法
    for (int i = 0; i < turnID; i++) {
        int x0, y0, x1, y1, x2, y2;
        if (scanf("%d%d%d%d%d%d", &x0, &y0, &x1, &y1, &x2, &y2) != 6) {
            fprintf(stderr, "Error reading move coordinates at turn %d\n", i);
            return 1;
        }
        
        if (x0 == -1) {
            me = BLACK;  // 第一回合收到(-1,-1)，说明我是黑方
            // 计算初始哈希值（白方先手）
            current_hash = compute_hash(&input_state, WHITE);
        } else {
            // 更新哈希并移动棋子
            int opponent = 1 - me;
            current_hash = move_with_hash(&input_state, x0, y0, x1, y1, x2, y2, opponent, opponent, current_hash);
        }
        
        if (i < turnID - 1) {
            if (scanf("%d%d%d%d%d%d", &x0, &y0, &x1, &y1, &x2, &y2) != 6) {
                fprintf(stderr, "Error reading my move coordinates at turn %d\n", i);
                return 1;
            }
            if (x0 >= 0) {
                // 更新哈希并移动棋子
                current_hash = move_with_hash(&input_state, x0, y0, x1, y1, x2, y2, me, me, current_hash);
            }
        }
    }
    
    // 如果是第一回合且我是黑方，需要计算初始哈希值
    if (turnID == 1 && me == BLACK && current_hash == 0) {
        current_hash = compute_hash(&input_state, me);
    }
    
    // 设置UCT探索常数（随回合数衰减）
    C = 0.176999999999 * exp(-0.008 * (turnID - 1.41));
    
    // 执行带开局库的MCTS搜索
    input_state.hash = current_hash;
    MCTS_with_opening(&input_state, current_hash);
    
    return 0;
}