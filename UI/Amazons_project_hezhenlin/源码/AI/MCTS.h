#pragma once
#include "../Game/GameLogic.h"
#include <vector>
#include <chrono>
#include <random>

struct MCTSNode {
    BitBoard board;
    int player;
    int visits;
    double value;
    MCTSNode* parent;
    std::vector<MCTSNode*> children;
    std::vector<Move> untriedMoves;
    Move lastMove;
    double heuristic;

    MCTSNode(const BitBoard& b, int p, MCTSNode* par = nullptr, Move move = Move());
    ~MCTSNode();

    double getUCB(double exploration = 1.2) const;
    bool isFullyExpanded() const;
};

class MCTS {
private:
    std::mt19937 rng;

    double simulate(const BitBoard& board, int currentPlayer, int originalPlayer);
    double evaluate(const BitBoard& board, int player);

public:
    MCTS();
    Move search(const BitBoard& board, int player, long timeLimitMs = 1000);
};
