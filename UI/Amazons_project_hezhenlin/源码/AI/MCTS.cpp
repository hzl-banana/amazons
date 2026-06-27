#include "MCTS.h"
#include <cmath>
#include <algorithm>
#include <chrono>

MCTSNode::MCTSNode(const BitBoard& b, int p, MCTSNode* par, Move move)
    : board(b), player(p), visits(0), value(0), parent(par),
    lastMove(move), heuristic(0) {
}

MCTSNode::~MCTSNode() {
    for (auto child : children) {
        delete child;
    }
}

double MCTSNode::getUCB(double exploration) const {
    if (visits == 0) return 1e6;
    if (!parent) return value / visits;
    double exploit = value / visits;
    double explore = exploration * sqrt(log(parent->visits) / visits);
    return exploit + explore;
}

bool MCTSNode::isFullyExpanded() const {
    return untriedMoves.empty();
}

MCTS::MCTS() : rng(std::random_device{}()) {
}

Move MCTS::search(const BitBoard& board, int player, long timeLimitMs) {
    auto startTime = std::chrono::high_resolution_clock::now();

    MCTSNode* root = new MCTSNode(board, player);
    GameLogic game;
    game.setBoard(board);
    root->untriedMoves = game.generateLegalMoves(player);

    if (root->untriedMoves.empty()) {
        delete root;
        return Move();
    }

    int iterations = 0;
    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        if (elapsed >= timeLimitMs) break;

        MCTSNode* node = root;

        // 选择
        while (node->isFullyExpanded() && !node->children.empty()) {
            MCTSNode* bestChild = nullptr;
            double bestUCB = -1e9;

            for (auto child : node->children) {
                double ucb = child->getUCB();
                if (ucb > bestUCB) {
                    bestUCB = ucb;
                    bestChild = child;
                }
            }

            if (bestChild) {
                node = bestChild;
            }
            else {
                break;
            }
        }

        // 扩展
        if (!node->untriedMoves.empty()) {
            Move move = node->untriedMoves.back();
            node->untriedMoves.pop_back();

            BitBoard newBoard = node->board;
            GameLogic game;
            game.setBoard(newBoard);
            game.makeMove(move);

            MCTSNode* child = new MCTSNode(game.getBoard(), -node->player, node, move);
            child->untriedMoves = game.generateLegalMoves(-node->player);
            node->children.push_back(child);

            node = child;
        }

        // 模拟
        double result = simulate(node->board, node->player, player);

        // 回溯
        MCTSNode* back = node;
        while (back) {
            back->visits++;
            back->value += result;
            result = -result;
            back = back->parent;
        }

        iterations++;
    }

    // 选择最佳走法
    Move bestMove;
    int maxVisits = -1;

    for (auto child : root->children) {
        if (child->visits > maxVisits) {
            maxVisits = child->visits;
            bestMove = child->lastMove;
        }
    }

    if (bestMove.isNull() && !root->untriedMoves.empty()) {
        bestMove = root->untriedMoves[0];
    }

    delete root;
    return bestMove;
}

double MCTS::simulate(const BitBoard& board, int currentPlayer, int originalPlayer) {
    BitBoard simBoard = board;
    GameLogic game;
    game.setBoard(simBoard);

    int player = currentPlayer;
    int depth = 0;
    int maxDepth = 50;

    while (depth < maxDepth) {
        std::vector<Move> moves = game.generateLegalMoves(player);

        if (moves.empty()) {
            return (player == originalPlayer) ? -1.0 : 1.0;
        }

        int idx = rng() % moves.size();
        game.makeMove(moves[idx]);
        player = -player;
        depth++;
    }

    return evaluate(game.getBoard(), originalPlayer);
}

double MCTS::evaluate(const BitBoard& board, int player) {
    GameLogic game;
    game.setBoard(board);

    int myMobility = 0, oppMobility = 0;
    std::vector<std::pair<int, int>> myPieces, oppPieces;
    board.getPiecePositions(player, myPieces);
    board.getPiecePositions(-player, oppPieces);

    for (const auto& p : myPieces) {
        myMobility += game.fullMobility(p.first, p.second, board);
    }

    for (const auto& p : oppPieces) {
        oppMobility += game.fullMobility(p.first, p.second, board);
    }

    if (oppMobility == 0) return 1.0;
    if (myMobility == 0) return -1.0;

    double mobScore = (double)(myMobility - oppMobility) / (myMobility + oppMobility);

    int myTerritory = 0, oppTerritory = 0;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (!board.isEmpty(x, y)) continue;

            int myDist = 100, oppDist = 100;

            for (const auto& p : myPieces) {
                int dist = abs(p.first - x) + abs(p.second - y);
                if (dist < myDist) myDist = dist;
            }

            for (const auto& p : oppPieces) {
                int dist = abs(p.first - x) + abs(p.second - y);
                if (dist < oppDist) oppDist = dist;
            }

            if (myDist < oppDist) myTerritory++;
            else if (oppDist < myDist) oppTerritory++;
        }
    }

    double terrScore = (double)(myTerritory - oppTerritory) / (myTerritory + oppTerritory + 1);
    return 0.7 * mobScore + 0.3 * terrScore;
}