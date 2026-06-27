#pragma once

#include <iostream>

// 走法结构体，表示亚马逊棋的一步棋
struct Move {
    int fromX;    // 起点X坐标
    int fromY;    // 起点Y坐标
    int toX;      // 终点X坐标
    int toY;      // 终点Y坐标
    int arrowX;   // 箭的X坐标
    int arrowY;   // 箭的Y坐标

    // 默认构造函数
    Move() : fromX(-1), fromY(-1), toX(-1), toY(-1), arrowX(-1), arrowY(-1) {}

    // 带参数构造函数
    Move(int fx, int fy, int tx, int ty, int ax, int ay)
        : fromX(fx), fromY(fy), toX(tx), toY(ty), arrowX(ax), arrowY(ay) {
    }

    // 检查走法是否有效（简化版，具体验证在GameLogic中）
    bool isValid() const {
        // 基本坐标范围检查
        if (fromX < 0 || fromX >= 8 || fromY < 0 || fromY >= 8) return false;
        if (toX < 0 || toX >= 8 || toY < 0 || toY >= 8) return false;
        if (arrowX < 0 || arrowX >= 8 || arrowY < 0 || arrowY >= 8) return false;

        // 起点和终点不能相同
        if (fromX == toX && fromY == toY) return false;

        // 箭的位置不能是移动前的起点（但可以是移动后的位置）
        // 注意：在亚马逊棋中，箭可以放在移动前的位置（即fromX, fromY）
        // 所以我们不在这里检查这个

        return true;
    }

    // 检查是否是无效的走法
    bool isNull() const {
        return fromX == -1 && fromY == -1 &&
            toX == -1 && toY == -1 &&
            arrowX == -1 && arrowY == -1;
    }

    // 设置无效走法
    void setNull() {
        fromX = fromY = toX = toY = arrowX = arrowY = -1;
    }

    // 打印走法（调试用）
    void print() const {
        std::cout << "Move: (" << fromX << "," << fromY << ") -> ("
            << toX << "," << toY << ") Arrow: ("
            << arrowX << "," << arrowY << ")" << std::endl;
    }

    // 转换为字符串表示
    std::string toString() const {
        char buffer[100];
        sprintf_s(buffer, "(%d,%d)->(%d,%d) arrow:(%d,%d)",
            fromX, fromY, toX, toY, arrowX, arrowY);
        return std::string(buffer);
    }

    // 比较运算符（用于排序和查找）
    bool operator==(const Move& other) const {
        return fromX == other.fromX && fromY == other.fromY &&
            toX == other.toX && toY == other.toY &&
            arrowX == other.arrowX && arrowY == other.arrowY;
    }

    bool operator!=(const Move& other) const {
        return !(*this == other);
    }

    // 用于在容器中排序
    bool operator<(const Move& other) const {
        if (fromX != other.fromX) return fromX < other.fromX;
        if (fromY != other.fromY) return fromY < other.fromY;
        if (toX != other.toX) return toX < other.toX;
        if (toY != other.toY) return toY < other.toY;
        if (arrowX != other.arrowX) return arrowX < other.arrowX;
        return arrowY < other.arrowY;
    }
};

// 用于哈希表等容器
namespace std {
    template<>
    struct hash<Move> {
        size_t operator()(const Move& m) const {
            // 简单的哈希函数，将6个int组合成一个哈希值
            return ((hash<int>()(m.fromX) ^
                (hash<int>()(m.fromY) << 1)) >> 1) ^
                (hash<int>()(m.toX) << 1) ^
                (hash<int>()(m.toY) << 2) ^
                (hash<int>()(m.arrowX) << 3) ^
                (hash<int>()(m.arrowY) << 4);
        }
    };
}
