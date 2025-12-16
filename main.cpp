#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if __has_include(<jsoncpp/json.h>)
#include <jsoncpp/json.h>
#define HAS_JSONCPP 1
#elif __has_include(<json/json.h>)
#include <json/json.h>
#define HAS_JSONCPP 1
#else
#define HAS_JSONCPP 0
#endif

using Clock = std::chrono::steady_clock;
using std::cin;
using std::cout;
using std::endl;
using std::istreambuf_iterator;
using std::max;
using std::min;
using std::pair;
using std::sort;
using std::string;
using std::unique_ptr;
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
    vector<Move> responses;
    bool hasBoard = false;
    vector<vector<int>> board;
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

#if HAS_JSONCPP
bool extractMoveFromJson(const Json::Value &obj, Move &m) {
    auto inRange = [](int v) { return v >= 0 && v < BOARD_SIZE; };

    if (obj.isNull()) return false;

    if (obj.isArray() && obj.size() >= 6) {
        m.sx = obj[0].asInt();
        m.sy = obj[1].asInt();
        m.tx = obj[2].asInt();
        m.ty = obj[3].asInt();
        m.ax = obj[4].asInt();
        m.ay = obj[5].asInt();
        m.valid = inRange(m.sx) && inRange(m.sy) && inRange(m.tx) && inRange(m.ty) && inRange(m.ax) && inRange(m.ay);
        return m.valid;
    }

    bool hasX0 = obj.isObject() && obj.isMember("x0");
    bool hasY0 = obj.isObject() && obj.isMember("y0");
    bool hasX1 = obj.isObject() && obj.isMember("x1");
    bool hasY1 = obj.isObject() && obj.isMember("y1");
    bool hasX2 = obj.isObject() && obj.isMember("x2");
    bool hasY2 = obj.isObject() && obj.isMember("y2");
    if (hasX0 && hasY0 && hasX1 && hasY1 && hasX2 && hasY2) {
        int sx = obj["x0"].asInt();
        int sy = obj["y0"].asInt();
        int tx = obj["x1"].asInt();
        int ty = obj["y1"].asInt();
        int ax = obj["x2"].asInt();
        int ay = obj["y2"].asInt();
        if (inRange(sx) && inRange(sy) && inRange(tx) && inRange(ty) && inRange(ax) && inRange(ay)) {
            m.sx = sx;
            m.sy = sy;
            m.tx = tx;
            m.ty = ty;
            m.ax = ax;
            m.ay = ay;
            m.valid = true;
            return true;
        }
    }

    if (obj.isMember("from") && obj["from"].isArray() && obj["from"].size() == 2 &&
        obj.isMember("to") && obj["to"].isArray() && obj["to"].size() == 2 &&
        obj.isMember("arrow") && obj["arrow"].isArray() && obj["arrow"].size() == 2) {
        int sx = obj["from"][0].asInt();
        int sy = obj["from"][1].asInt();
        int tx = obj["to"][0].asInt();
        int ty = obj["to"][1].asInt();
        int ax = obj["arrow"][0].asInt();
        int ay = obj["arrow"][1].asInt();
        if (inRange(sx) && inRange(sy) && inRange(tx) && inRange(ty) && inRange(ax) && inRange(ay)) {
            m.sx = sx;
            m.sy = sy;
            m.tx = tx;
            m.ty = ty;
            m.ax = ax;
            m.ay = ay;
            m.valid = true;
            return true;
        }
    }
    return false;
}
#else
bool extractMoveFromJson(const void *, Move &) { return false; }
#endif

ParsedInput parseInput(const string &raw) {
    ParsedInput res;
    res.iAmFirst = true;
#if HAS_JSONCPP
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    string errs;
    if (!reader->parse(raw.data(), raw.data() + raw.size(), &root, &errs)) {
        return res;
    }
    if (!root.isMember("requests")) return res;

    const auto &reqs = root["requests"];
    const auto &resps = root["responses"];
    Json::ArrayIndex turn = reqs.size();
    res.requests.resize(static_cast<size_t>(turn));
    for (Json::ArrayIndex i = 0; i < turn; ++i) {
        Move m;
        if (extractMoveFromJson(reqs[i], m)) res.requests[i] = m;
    }
    if (resps.isArray()) {
        Json::ArrayIndex rsz = resps.size();
        res.responses.resize(static_cast<size_t>(rsz));
        for (Json::ArrayIndex i = 0; i < rsz; ++i) {
            Move m;
            if (extractMoveFromJson(resps[i], m)) res.responses[i] = m;
        }
    }

    if (turn > 0) {
        Move first;
        res.iAmFirst = !extractMoveFromJson(reqs[0], first) || !first.valid;
        const auto &lastReq = reqs[turn - 1];
        const char *keys[] = {"chessboard", "board"};
        for (auto key : keys) {
            if (lastReq.isMember(key) && lastReq[key].isArray() && lastReq[key].size() == BOARD_SIZE) {
                res.hasBoard = true;
                res.board.assign(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
                for (int i = 0; i < BOARD_SIZE; ++i) {
                    for (int j = 0; j < BOARD_SIZE; ++j) res.board[i][j] = lastReq[key][i][j].asInt();
                }
                break;
            }
        }
    }
#endif
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

    string rawInput((istreambuf_iterator<char>(cin)), istreambuf_iterator<char>());
    ParsedInput input = parseInput(rawInput);

    int myColor = input.iAmFirst ? PLAYER_ONE : PLAYER_TWO;
    int oppColor = (myColor == PLAYER_ONE) ? PLAYER_TWO : PLAYER_ONE;

    Board board;
    if (input.hasBoard) {
        board.loadFromArray(input.board);
    } else {
        board.placeInitial();
        int turn = static_cast<int>(input.requests.size());
        for (int i = 0; i < turn; ++i) {
            if (i > 0 && i - 1 < static_cast<int>(input.responses.size())) {
                board.applyMove(input.responses[i - 1], myColor);
            }
            board.applyMove(input.requests[i], oppColor);
        }
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
