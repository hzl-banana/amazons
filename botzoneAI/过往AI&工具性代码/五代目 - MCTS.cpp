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

#define bool  int
#define true  1
#define false 0

#define MAX_COUNT    (2000 * 2000 * 3)  // 最大状态数量

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

uint32_t MaxTime = 980999;  // 最大搜索时间（微秒）

// ====================================================
// 开局库定义
// ====================================================
typedef struct {
    uint64_t fingerprint;  // 棋盘指纹
    int x0, y0, x1, y1, x2, y2;  // 走法
} OpeningEntry;

// 开局库数据 - 包含常见开局走法
OpeningEntry opening_book[] = {
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
    //待填写
};

#define OPENING_BOOK_SIZE (sizeof(opening_book) / sizeof(OpeningEntry))

// 棋盘指纹计算函数
uint64_t calculate_fingerprint(State* state) {
    // 简单的指纹计算，实际应用中可以使用更复杂的哈希函数
    uint64_t fp = state->board;
    fp ^= state->coor[0];
    fp ^= state->coor[1];
    
    // 添加位置信息的异或
    for (int i = 0; i < 64; i++) {
        if (state->board & (1ULL << i)) {
            fp ^= (i * 0x5DEECE66DLL + 0xB);
        }
    }
    
    return fp;
}

// 查找开局库中的走法
bool lookup_opening(State* state, int* x0, int* y0, int* x1, int* y1, int* x2, int* y2) {
    uint64_t fingerprint = calculate_fingerprint(state);
    
    for (int i = 0; i < OPENING_BOOK_SIZE; i++) {
        if (opening_book[i].fingerprint == fingerprint) {
            *x0 = opening_book[i].x0;
            *y0 = opening_book[i].y0;
            *x1 = opening_book[i].x1;
            *y1 = opening_book[i].y1;
            *x2 = opening_book[i].x2;
            *y2 = opening_book[i].y2;
            return true;
        }
    }
    
    return false;
}

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
 * 每行参数: [领地权重1, 领地权重2, 位置权重1, 位置权重2, 机动性权重, 未使用]
 */
double args[28][6] = {
		{ 0.07747249543793637 ,0.05755603330699520 ,0.64627749023334498 ,0.70431267004292740 ,0.02438131097879579 , 0.00 },
		{ 0.05093047840251742 ,0.06276538622537013 ,0.69898059004821581 ,0.66192728970497727 ,0.02362598306372760 , 0.00 },
		{ 0.06036622274224539 ,0.06253298199478051 ,0.60094570235521628 ,0.67719126081076242 ,0.01873142786640421 , 0.00 },
		{ 0.07597341130849308 ,0.06952095866594065 ,0.69061184234845333 ,0.67989394578528273 ,0.02098781856298665 , 0.00 },
		{ 0.08083391263897154 ,0.08815144960484271 ,0.58981849824874917 ,0.54664183543259470 ,0.02318479501373763 , 0.00 },
		{ 0.09155731347030857 ,0.08397548702353251 ,0.56392480085083986 ,0.54319242129550227 ,0.02317401477849946 , 0.00 },
		{ 0.10653095458609237 ,0.10479793630859575 ,0.54840938009286515 ,0.53023658889860381 ,0.02084758939889652 , 0.00 },
		{ 0.11534143744086589 ,0.11515706838023705 ,0.53325566869906469 ,0.52423368303553451 ,0.02237127451593010 , 0.00 },
		{ 0.12943854523554690 ,0.12673742164114844 ,0.50841519367287034 ,0.52208373964502879 ,0.02490545306630711 , 0.00 },
		{ 0.12882484162931859 ,0.13946973532382280 ,0.49621839819987758 ,0.51776460089353364 ,0.03045473763611049 , 0.00 },
		{ 0.13701233819832731 ,0.15338865590616042 ,0.47601466399954588 ,0.51500429509193190 ,0.03249896738636078 , 0.00 },
		{ 0.14530543898518938 ,0.15565237403332051 ,0.45365475320199057 ,0.50934623406618500 ,0.03830491784046246 , 0.00 },
		{ 0.14521045986025419 ,0.16388365022083374 ,0.44531995327608060 ,0.50517597255948953 ,0.04864124027084386 , 0.00 },
		{ 0.13750613208150655 ,0.16326621164859418 ,0.43619350878439399 ,0.50328876650721398 ,0.05912794240603884 , 0.00 },
		{ 0.13565263325548560 ,0.15529175902376631 ,0.42382223063419649 ,0.50288212924827379 ,0.07437679521343679 , 0.00 },
		{ 0.12382760525087406 ,0.10361944098637088 ,0.50487335391408680 ,0.55808747967333505 ,0.02791980213792046 , 0.00 },
		{ 0.11809487853625075 ,0.14632850080535232 ,0.40738388113193924 ,0.41782129616811122 ,0.10308050317730764 , 0.00 },
		{ 0.10805473551960752 ,0.15043981450391137 ,0.40520488356004784 ,0.43073574707030956 ,0.10967613304465569 , 0.00 },
		{ 0.09668240983912251 ,0.15666221434557865 ,0.40215634987047013 ,0.44165716517577754 ,0.10906426061069142 , 0.00 },
		{ 0.10585263971502025 ,0.16319090506614549 ,0.38220029690800922 ,0.45465487463858675 ,0.10062997439277618 , 0.00 },
		{ 0.11123671989551248 ,0.15516074827095279 ,0.36904588744714037 ,0.46534418781939937 ,0.09118229977179015 , 0.00 },
		{ 0.12535649823409767 ,0.10492555251930048 ,0.35567115915540981 ,0.48043579160677637 ,0.08337580273275977 , 0.00 },
		{ 0.28657326967317970 ,0.16655279311197080 ,0.38060545469477008 ,0.42472577515072628 ,0.10316994796202342 , 0.00 },
		{ 0.07143084940040888 ,0.16655279311197080 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 , 0.00 },
		{ 0.07143084940040888 ,0.16655279311197080 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 , 0.00 },
		{ 0.07143084940040888 ,0.16655279311197080 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 , 0.00 },
		{ 0.07143084940040888 ,0.16655279311197080 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 , 0.00 },
		{ 0.07143084940040888 ,0.14627749023334498 ,0.36658063304313299 ,0.39520049916162908 ,0.02194263694320541 , 0.00 }
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
    
    // 计算己方位置优势
    for (int i = 1; i < me_len; i++) {
        can = can_moves_arr_me[i];
        off = i;
        // 快速计算2^(-off): 通过浮点数位操作
        _pow = 0x3ff0000000000000ull - (off << 52);
        f_pow = *(double*)&_pow;
        ret += __builtin_popcountll(can) * f_pow;
    }
    
    // 减去对方位置优势
    for (int i = 1; i < you_len; i++) {
        can = can_moves_arr_you[i];
        off = i;
        _pow = 0x3ff0000000000000ull - (off << 52);
        f_pow = *(double*)&_pow;
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
        args[evaluate_turn][0] * t1 + 
        args[evaluate_turn][1] * t2 +
        args[evaluate_turn][2] * p1 + 
        args[evaluate_turn][3] * p2 +
        args[evaluate_turn][4] * m0
    ) * 0.20;  // 全局缩放因子
    
    // 5. 通过sigmoid函数转换为胜率
    return 1 / (1 + exp(-score));
}

// ====================================================
// 走法生成
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
    register __m256i d0_mask = _mm256_set_epi64x(
        0xfefefefefefefefe,
        0x7f7f7f7f7f7f7f7f,
        0xffffffffffffffff,
        0xfefefefefefefefe
    );
    
    register __m256i d1_mask = _mm256_set_epi64x(
        0x7f7f7f7f7f7f7f7f,
        0xfefefefefefefefe,
        0xffffffffffffffff,
        0x7f7f7f7f7f7f7f7f
    );
    
    register __m256i count = _mm256_set_epi64x(1, 7, 8, 9);
    register __m256i and_mask = _mm256_set1_epi64x(board);
    
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
 * 生成当前玩家的所有合法走法
 * @param state 当前状态
 * @param turn 当前玩家（0=黑，1=白）
 */
__inline void generate_moves(State* state, int turn) {
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
                
                // 评估新状态（己方视角的胜率）
                new_state->quality = (me == turn) ? 
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
// MCTS核心算法
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
 * 扩展节点：为叶子节点生成所有子节点
 * @param state 要扩展的节点
 * @param next_turn 下一个行动的玩家
 */
__inline void expand(State* state, int next_turn) {
    // 检查是否是终局状态
    if (state->coor[0] != 0xffffffffffffffff) {
        generate_moves(state, next_turn);
        
        // 如果无棋可走，创建终局节点
        if (state->child == NULL) {
            state->child = _state_alloc();
            state->len = 1;
            
            state->child->coor[0] = 0xffffffffffffffff;  // 终局标记
            state->child->parent = state;
            state->child->quality = 0;  // 终局价值为0（输）
        }
    }
}

/**
 * 选择阶段：从根节点到叶子节点选择路径
 * @param leaf 起始节点（通常是根节点）
 * @param next_turn 输出：到达叶子节点时的玩家
 * @return 选择的叶子节点
 */
__inline State* select(State* leaf, int* next_turn) {
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
    State* temp = state;
    
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
 * 打印搜索树（调试用）
 * @param root 根节点
 * @param who 当前玩家
 */
void printTree(State* root, int who) {
    fprintf(stderr, "[State: %p, visit: %d, who: %d, quality: %lf]\n",
            root, root->visit, who, root->quality);
    fprintf(stderr, "==========================\n");
    
    int l = root->len;
    State* c = root->child;
    
    for (int i = 0; i < l; i++) {
        printTree(c, 1 - who);
        c++;
    }
}

// ====================================================
// 主MCTS函数
// ====================================================

/**
 * 主MCTS搜索函数
 * @param root 根节点（当前局面）
 */
__inline void MCTS(State* root) {
    State* leaf, * action = NULL;
    uint64_t diff;
    uint8_t start, result, arrow;
    uint32_t max_visit = 0;
    int next_turn, iteration = 0;
    
    start_time = clock();  // 开始计时
    
    // 主搜索循环
    for (; !timeout();) {
        next_turn = me;
        
        // 1. 选择阶段
        leaf = select(root, &next_turn);
        
        // 2. 扩展阶段（如果节点已被访问过）
        if (leaf->visit) {
            expand(leaf, next_turn);
            leaf = select(leaf, &next_turn);
        }
        
        // 3. 回传阶段
        backup(leaf, &next_turn);
        iteration++;
    }
    
    // 4. 决策阶段：选择访问次数最多的子节点
    State* c = root->child;
    for (int i = 0; i < root->len; i++, c++) {
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
    printf("call_uct_count: %llu \n", debug_uct_count);
}

// ====================================================
// 棋盘操作和主函数
// ====================================================

/**
 * 执行一步走法
 * @param state 当前状态
 * @param x0,y0 起始位置
 * @param x1,y1 目标位置
 * @param x2,y2 箭的位置
 * @param who 执行走法的玩家
 */
void move(State* state, int x0, int y0, int x1, int y1, int x2, int y2, int who) {
    assert(0 == ((~0x7) & (x0 | x1 | x2 | y0 | y1 | y2)));  // 坐标必须在0-7范围内
    
    // 更新棋盘位图：移除起始位置，添加目标位置和箭
    state->board ^= (1ull << ((y0 << 3) | x0)) ^ 
                    (1ull << ((y1 << 3) | x1)) ^ 
                    (1ull << ((y2 << 3) | x2));
    
    // 更新棋子位置：移动棋子
    state->coor[who] ^= (1ull << ((y0 << 3) | x0)) ^ 
                        (1ull << ((y1 << 3) | x1));
}

/**
 * 主函数
 * 读取输入，初始化棋盘，执行MCTS搜索
 */
int main(int argc, char* argv[]) {
    State input_state = { 0 };
    int turnID;
    
    // 初始化棋盘（标准起始位置）
    // 黑棋位置: (2,0), (5,0), (0,2), (7,2)
    input_state.board |= (1ull << 2) | (1ull << 5) | (1ull << 16) | (1ull << 23);
    input_state.coor[BLACK] = input_state.board;
    
    // 白棋位置: (0,5), (7,5), (2,7), (5,7)
    input_state.board |= (1ull << 40) | (1ull << 47) | (1ull << 58) | (1ull << 61);
    input_state.coor[WHITE] = input_state.board ^ input_state.coor[BLACK];
    
    // 读取回合信息
    scanf("%d", &turnID);
    me = WHITE;  // 默认白方开始
    
    // 第一回合时间加倍
    if (turnID == 1) {
        MaxTime <<= 1;
    }
    
    // 回放历史走法
    for (int i = 0; i < turnID; i++) {
        int x0, y0, x1, y1, x2, y2;
        scanf("%d%d%d%d%d%d", &x0, &y0, &x1, &y1, &x2, &y2);
        
        if (x0 == -1) {
            me = BLACK;  // 第一回合收到(-1,-1)，说明我是黑方
        } else {
            move(&input_state, x0, y0, x1, y1, x2, y2, 1 - me);  // 对方走法
        }
        
        if (i < turnID - 1) {
            scanf("%d%d%d%d%d%d", &x0, &y0, &x1, &y1, &x2, &y2);
            if (x0 >= 0) {
                move(&input_state, x0, y0, x1, y1, x2, y2, me);  // 己方走法
            }
        }
    }
    
    // 检查开局库
    int x0, y0, x1, y1, x2, y2;
    if (lookup_opening(&input_state, &x0, &y0, &x1, &y1, &x2, &y2)) {
        // 使用开局库中的走法
        printf("%d %d %d %d %d %d\n", x0, y0, x1, y1, x2, y2);
        printf("elapse: 0.000000, ");
        printf("iterator: 0, ");
        printf("evaluate: 0, ");
        printf("call_uct_count: 0 \n");
    } else {
        // 设置UCT探索常数（随回合数衰减）
        C = 0.176999999999 * exp(-0.008 * (turnID - 1.41));
        
        // 执行MCTS搜索并输出最佳走法
        MCTS(&input_state);
    }
    
    return 0;
}