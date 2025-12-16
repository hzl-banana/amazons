#include <bits/stdc++.h>

#if __has_include(<jsoncpp/json.h>)
#include <jsoncpp/json.h>
#define HAS_JSONCPP 1
#elif __has_include(<json/json.h>)
#include <json/json.h>
#define HAS_JSONCPP 1
#else
#define HAS_JSONCPP 0
#endif

using namespace std;

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

    Board() { memset(g, 0, sizeof(g)); }

    static bool inside(int x, int y) {
        return x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE;
    }

    void placeInitial() {
        memset(g, 0, sizeof(g));
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
                for (auto d : DIRS) {
                    int nx = x + d[0], ny = y + d[1];
                    while (inside(nx, ny) && g[nx][ny] == EMPTY) {
                        pair<int, int> origin{x, y};
                        pair<int, int> dest{nx, ny};
                        for (auto a : DIRS) {
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
                for (auto d : DIRS) {
                    int nx = x + d[0], ny = y + d[1];
                    while (inside(nx, ny) && g[nx][ny] == EMPTY) {
                        pair<int, int> origin{x, y};
                        pair<int, int> dest{nx, ny};
                        for (auto a : DIRS) {
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

static chrono::steady_clock::time_point gStart;
constexpr double TIME_LIMIT_MS = 950.0;  // stay under 1 second
bool gTimeout = false;

bool timeUp() {
    auto now = chrono::steady_clock::now();
    auto ms = chrono::duration_cast<chrono::milliseconds>(now - gStart).count();
    if (ms >= TIME_LIMIT_MS) gTimeout = true;
    return gTimeout;
}

int evaluate(const Board &b, int myColor) {
    int opp = (myColor == PLAYER_ONE) ? PLAYER_TWO : PLAYER_ONE;
    int myMob = b.countMoves(myColor);
    int oppMob = b.countMoves(opp);
    return (myMob - oppMob) * 10;
}

int alphabeta(const Board &state, int depth, int alpha, int beta, int player, int myColor, Move &bestMove) {
    if (depth == 0 || timeUp()) {
        return evaluate(state, myColor);
    }
    vector<Move> moves;
    state.generateMoves(player, moves);
    if (moves.empty()) {
        return (player == myColor) ? INT_MIN / 4 : INT_MAX / 4;
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
        sort(scored.begin(), scored.end(), [](auto &a, auto &b) { return a.first > b.first; });
    } else {
        sort(scored.begin(), scored.end(), [](auto &a, auto &b) { return a.first < b.first; });
    }

    if (player == myColor) {
        int value = INT_MIN;
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
        int value = INT_MAX;
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
    gStart = chrono::steady_clock::now();
    gTimeout = false;
    Move globalBest;
    vector<Move> rootMoves;
    state.generateMoves(myColor, rootMoves);
    if (rootMoves.empty()) return globalBest;
    globalBest = rootMoves.front();

    int depth = 1;
    while (!timeUp()) {
        Move bestThisDepth;
        alphabeta(state, depth, INT_MIN / 2, INT_MAX / 2, myColor, myColor, bestThisDepth);
        if (!gTimeout && bestThisDepth.valid) {
            globalBest = bestThisDepth;
        }
        ++depth;
    }
    return globalBest;
}

bool extractMoveFromJson(const
#if HAS_JSONCPP
    Json::Value &obj,
#else
    void * /*unused*/,
#endif
    Move &m) {
#if HAS_JSONCPP
    auto getCoord = [&](const char *key, bool &found) -> int {
        if (obj.isObject() && obj.isMember(key)) {
            found = true;
            return obj[key].asInt();
        }
        return -1;
    };

    if (obj.isNull()) return false;

    if (obj.isArray() && obj.size() >= 6) {
        m.sx = obj[0].asInt();
        m.sy = obj[1].asInt();
        m.tx = obj[2].asInt();
        m.ty = obj[3].asInt();
        m.ax = obj[4].asInt();
        m.ay = obj[5].asInt();
        m.valid = (m.sx >= 0);
        return m.valid;
    }

    bool found = false;
    int sx = getCoord("x0", found);
    int sy = getCoord("y0", found);
    int tx = getCoord("x1", found);
    int ty = getCoord("y1", found);
    int ax = getCoord("x2", found);
    int ay = getCoord("y2", found);
    if (found && sx >= 0 && sy >= 0 && tx >= 0 && ty >= 0 && ax >= 0 && ay >= 0) {
        m.sx = sx;
        m.sy = sy;
        m.tx = tx;
        m.ty = ty;
        m.ax = ax;
        m.ay = ay;
        m.valid = true;
        return true;
    }

    if (obj.isMember("from") && obj["from"].isArray() && obj["from"].size() == 2 &&
        obj.isMember("to") && obj["to"].isArray() && obj["to"].size() == 2 &&
        obj.isMember("arrow") && obj["arrow"].isArray() && obj["arrow"].size() == 2) {
        m.sx = obj["from"][0].asInt();
        m.sy = obj["from"][1].asInt();
        m.tx = obj["to"][0].asInt();
        m.ty = obj["to"][1].asInt();
        m.ax = obj["arrow"][0].asInt();
        m.ay = obj["arrow"][1].asInt();
        m.valid = true;
        return true;
    }
#endif
    (void)m;
    return false;
}

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
    int turn = reqs.size();
    res.requests.resize(turn);
    for (int i = 0; i < turn; ++i) {
        Move m;
        if (extractMoveFromJson(reqs[i], m)) res.requests[i] = m;
    }
    if (resps.isArray()) {
        int rsz = resps.size();
        res.responses.resize(rsz);
        for (int i = 0; i < rsz; ++i) {
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
    ios::sync_with_stdio(false);
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
