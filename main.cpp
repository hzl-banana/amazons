#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;
using std::cin;
using std::cout;
using std::endl;
using std::max;
using std::min;
using std::pair;
using std::sort;
using std::string;
using std::vector;

struct Move {
    int sx = -1, sy = -1;
    int tx = -1, ty = -1;
    int ax = -1, ay = -1;
    bool valid = false;
};

constexpr int BOARD_SIZE = 8;
constexpr int EMPTY = 0;
constexpr int PLAYER_ONE = 1;
constexpr int PLAYER_TWO = 2;
constexpr int BLOCKED = 3;

static const int DIRS[8][2] = {
    {1, 0},  {0, 1},  {-1, 0}, {0, -1},
    {1, 1},  {1, -1}, {-1, 1}, {-1, -1},
};

struct Board {
    int g[BOARD_SIZE][BOARD_SIZE]{};

    Board() { clear(); }

    void clear() {
        for (auto &row : g) {
            std::fill(std::begin(row), std::end(row), EMPTY);
        }
    }

    static bool inside(int x, int y) {
        return x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE;
    }

    void placeInitial() {
        clear();
        // First player
        g[0][2] = g[0][5] = g[2][0] = g[2][7] = PLAYER_ONE;
        // Second player
        g[5][0] = g[5][7] = g[7][2] = g[7][5] = PLAYER_TWO;
    }

    void loadFromArray(const vector<vector<int>> &arr) {
        for (int i = 0; i < BOARD_SIZE; ++i)
            for (int j = 0; j < BOARD_SIZE; ++j)
                g[i][j] = arr[i][j];
    }

    void applyMove(const Move &m, int player) {
        if (!m.valid) return;
        g[m.sx][m.sy] = EMPTY;
        g[m.tx][m.ty] = player;
        g[m.ax][m.ay] = BLOCKED;
    }

    bool cellFree(int x, int y, const pair<int, int> &origin, const pair<int, int> &dest) const {
        if (!inside(x, y)) return false;
        if (x == dest.first && y == dest.second) return false;
        if (x == origin.first && y == origin.second) return true;
        return g[x][y] == EMPTY;
    }

    void generateMoves(int player, vector<Move> &moves) const {
        for (int x = 0; x < BOARD_SIZE; ++x) {
            for (int y = 0; y < BOARD_SIZE; ++y) {
                if (g[x][y] != player) continue;
                for (const auto &d : DIRS) {
                    int nx = x + d[0], ny = y + d[1];
                    while (inside(nx, ny) && g[nx][ny] == EMPTY) {
                        pair<int, int> origin{x, y};
                        pair<int, int> dest{nx, ny};
                        for (const auto &a : DIRS) {
                            int ax = nx + a[0], ay = ny + a[1];
                            while (cellFree(ax, ay, origin, dest)) {
                                Move mv;
                                mv.sx = x;
                                mv.sy = y;
                                mv.tx = nx;
                                mv.ty = ny;
                                mv.ax = ax;
                                mv.ay = ay;
                                mv.valid = true;
                                moves.push_back(mv);
                                ax += a[0];
                                ay += a[1];
                            }
                        }
                        nx += d[0];
                        ny += d[1];
                    }
                }
            }
        }
    }

    int countMoves(int player, int cutoff = INT_MAX) const {
        int cnt = 0;
        for (int x = 0; x < BOARD_SIZE; ++x) {
            for (int y = 0; y < BOARD_SIZE; ++y) {
                if (g[x][y] != player) continue;
                for (const auto &d : DIRS) {
                    int nx = x + d[0], ny = y + d[1];
                    while (inside(nx, ny) && g[nx][ny] == EMPTY) {
                        pair<int, int> origin{x, y};
                        pair<int, int> dest{nx, ny};
                        for (const auto &a : DIRS) {
                            int ax = nx + a[0], ay = ny + a[1];
                            while (cellFree(ax, ay, origin, dest)) {
                                ++cnt;
                                if (cnt >= cutoff) return cnt;
                                ax += a[0];
                                ay += a[1];
                            }
                        }
                        nx += d[0];
                        ny += d[1];
                    }
                }
            }
        }
        return cnt;
    }
};

struct ParsedInput {
    vector<Move> requests;
    bool iAmFirst = true;
};

static Clock::time_point gStart;
constexpr double TIME_LIMIT_MS = 950.0;  // stay under 1 second
constexpr int MOBILITY_WEIGHT = 10;
constexpr int LOSS_SCORE = INT_MIN / 4;
constexpr int WIN_SCORE = INT_MAX / 4;
constexpr int NEG_INF = INT_MIN / 2;
constexpr int POS_INF = INT_MAX / 2;
std::atomic<bool> gTimeout{false};

bool timeUp() {
    auto now = Clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - gStart).count();
    if (ms >= TIME_LIMIT_MS) gTimeout.store(true);
    return gTimeout.load();
}

int evaluate(const Board &b, int myColor) {
    int opp = (myColor == PLAYER_ONE) ? PLAYER_TWO : PLAYER_ONE;
    int myMob = b.countMoves(myColor);
    int oppMob = b.countMoves(opp);
    return (myMob - oppMob) * MOBILITY_WEIGHT;
}

int alphabeta(const Board &state, int depth, int alpha, int beta, int player, int myColor, Move &bestMove) {
    if (depth == 0 || timeUp()) {
        return evaluate(state, myColor);
    }
    vector<Move> moves;
    state.generateMoves(player, moves);
    if (moves.empty()) {
        return (player == myColor) ? LOSS_SCORE : WIN_SCORE;
    }

    // Move ordering: prefer moves that change mobility more.
    vector<pair<int, Move>> scored;
    scored.reserve(moves.size());
    for (const auto &m : moves) {
        Board nxt = state;
        nxt.applyMove(m, player);
        int sc = evaluate(nxt, myColor);
        scored.push_back({sc, m});
    }
    if (player == myColor) {
        sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    } else {
        sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    }

    if (player == myColor) {
        int value = NEG_INF;
        for (auto &sm : scored) {
            if (timeUp()) break;
            Board nxt = state;
            nxt.applyMove(sm.second, player);
            Move dummy;
            int child = alphabeta(nxt, depth - 1, alpha, beta, (player == PLAYER_ONE) ? PLAYER_TWO : PLAYER_ONE,
                                  myColor, dummy);
            if (child > value) {
                value = child;
                bestMove = sm.second;
            }
            alpha = max(alpha, value);
            if (alpha >= beta) break;
        }
        return value;
    } else {
        int value = POS_INF;
        for (auto &sm : scored) {
            if (timeUp()) break;
            Board nxt = state;
            nxt.applyMove(sm.second, player);
            Move dummy;
            int child = alphabeta(nxt, depth - 1, alpha, beta, (player == PLAYER_ONE) ? PLAYER_TWO : PLAYER_ONE,
                                  myColor, dummy);
            if (child < value) {
                value = child;
                bestMove = sm.second;
            }
            beta = min(beta, value);
            if (alpha >= beta) break;
        }
        return value;
    }
}

Move searchBest(const Board &state, int myColor) {
    gStart = Clock::now();
    gTimeout.store(false);
    Move globalBest;
    vector<Move> rootMoves;
    state.generateMoves(myColor, rootMoves);
    if (rootMoves.empty()) return globalBest;
    globalBest = rootMoves.front();

    int depth = 1;
    while (!timeUp()) {
        Move bestThisDepth;
        alphabeta(state, depth, NEG_INF, POS_INF, myColor, myColor, bestThisDepth);
        if (!gTimeout.load() && bestThisDepth.valid) {
            globalBest = bestThisDepth;
        }
        ++depth;
    }
    return globalBest;
}

ParsedInput parseInputStream() {
    ParsedInput res;
    res.iAmFirst = true;
    vector<int> nums;
    int v = 0;
    while (cin >> v) nums.push_back(v);
    if (nums.empty()) return res;

    size_t moves = nums.size() / 6;
    res.requests.resize(moves);
    for (size_t i = 0; i < moves; ++i) {
        Move m;
        m.sx = nums[i * 6 + 0];
        m.sy = nums[i * 6 + 1];
        m.tx = nums[i * 6 + 2];
        m.ty = nums[i * 6 + 3];
        m.ax = nums[i * 6 + 4];
        m.ay = nums[i * 6 + 5];
        m.valid = Board::inside(m.sx, m.sy) && Board::inside(m.tx, m.ty) && Board::inside(m.ax, m.ay);
        res.requests[i] = m;
    }
    res.iAmFirst = false;
    return res;
}
Move fallbackMove(const Board &b, int myColor) {
    vector<Move> moves;
    b.generateMoves(myColor, moves);
    if (!moves.empty()) return moves.front();
    return Move{};
}

int main() {
    std::ios::sync_with_stdio(false);
    cin.tie(nullptr);

    ParsedInput input = parseInputStream();

    int myColor = input.iAmFirst ? PLAYER_ONE : PLAYER_TWO;
    int oppColor = (myColor == PLAYER_ONE) ? PLAYER_TWO : PLAYER_ONE;

    Board board;
    board.placeInitial();
    int turn = static_cast<int>(input.requests.size());
    for (int i = 0; i < turn; ++i) {
        board.applyMove(input.requests[i], oppColor);
    }

    Move best = searchBest(board, myColor);
    if (!best.valid) best = fallbackMove(board, myColor);

    if (!best.valid) {
        cout << "0 0 0 0 0 0" << endl;
    } else {
        cout << best.sx << ' ' << best.sy << ' ' << best.tx << ' ' << best.ty << ' ' << best.ax << ' ' << best.ay
             << endl;
    }
    return 0;
}
