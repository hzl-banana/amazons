#pragma once
#include <cstdint>
#include <vector>
#include <utility>
#include <string>

class BitBoard {
private:
    uint64_t black;
    uint64_t white;
    uint64_t arrows;

public:
    BitBoard();

    // 初始化棋盘
    void initialize();

    // 基础查询
    bool isEmpty(int x, int y) const;
    int getPieceAt(int x, int y) const;
    uint64_t getOccupied() const;

    // 棋子操作
    void movePiece(int fromX, int fromY, int toX, int toY, int color);
    void placeArrow(int x, int y);
    void removeArrow(int x, int y);

    // 获取所有棋子位置
    void getPiecePositions(int color, std::vector<std::pair<int, int>>& positions) const;

    // 统计空位
    int countEmptySquares() const;

    // 获取棋盘状态（用于保存/加载）
    uint64_t getBlackBits() const { return black; }
    uint64_t getWhiteBits() const { return white; }
    uint64_t getArrowBits() const { return arrows; }
    void setBoard(uint64_t b, uint64_t w, uint64_t a);

    // 调试功能
    void printBoard() const;
    std::string toString() const;
};