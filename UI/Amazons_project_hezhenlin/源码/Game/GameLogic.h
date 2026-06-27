#pragma once
#include "BitBoard.h"
#include "Move.h"
#include <vector>
#include <ctime>

// 前向声明游戏模式枚举（从 Constants.h）
enum GameMode;

// 存档信息结构体
struct SaveFileInfo {
    bool exists;              // 存档是否存在
    time_t saveTime;          // 保存时间
    int moveCount;            // 步数
    int currentPlayer;        // 当前玩家
    int gameMode;             // 游戏模式
    bool isValid;             // 文件是否有效
    
    SaveFileInfo() : exists(false), saveTime(0), moveCount(0), 
                     currentPlayer(0), gameMode(0), isValid(false) {}
};

class GameLogic {
private:
    BitBoard board;
    int currentPlayer;
    std::vector<Move> moveHistory;
    bool gameOver;
    int winner;

public:
    GameLogic();

    // 游戏控制
    void newGame();
    bool makeMove(const Move& move);
    bool isGameOver() const;
    int getWinner() const { return winner; }

    // 游戏状态查询
    int getCurrentPlayer() const { return currentPlayer; }
    const BitBoard& getBoard() const { return board; }
    void setBoard(const BitBoard& b) { board = b; }

    // 走法生成
    std::vector<Move> generateLegalMoves(int player) const;
    std::vector<Move> generateLegalMoves() const { return generateLegalMoves(currentPlayer); }

    // 移动有效性检查
    bool isValidMove(const Move& move, int player) const;

    // 存档/读档 - 支持保存游戏模式
    bool saveGame(const char* filename, int gameMode) const;
    bool loadGame(const char* filename, int& outGameMode);
    SaveFileInfo getSaveFileInfo(const char* filename) const;

    // 历史记录
    const std::vector<Move>& getMoveHistory() const { return moveHistory; }
    void undoMove();

    // 移动性（用于评估）
    int fullMobility(int x, int y, const BitBoard& b) const;

private:
    void checkGameOver();
};