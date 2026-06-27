#include "BitBoard.h"
#include <bitset>
#include <iostream>
#include <sstream>

BitBoard::BitBoard() : black(0), white(0), arrows(0) {
    initialize();
}

void BitBoard::initialize() {
    // 基于single版本的初始布局
    black = (1ULL << (2 * 8 + 0)) | (1ULL << (0 * 8 + 2)) |
        (1ULL << (0 * 8 + 5)) | (1ULL << (2 * 8 + 7));
    white = (1ULL << (5 * 8 + 0)) | (1ULL << (7 * 8 + 2)) |
        (1ULL << (7 * 8 + 5)) | (1ULL << (5 * 8 + 7));
    arrows = 0;
}

bool BitBoard::isEmpty(int x, int y) const {
    if (x < 0 || x >= 8 || y < 0 || y >= 8) return false;
    uint64_t pos = 1ULL << (y * 8 + x);
    return !(getOccupied() & pos);
}

int BitBoard::getPieceAt(int x, int y) const {
    if (x < 0 || x >= 8 || y < 0 || y >= 8) return 0;
    uint64_t pos = 1ULL << (y * 8 + x);
    if (black & pos) return 1;
    if (white & pos) return -1;
    if (arrows & pos) return 2;
    return 0;
}

uint64_t BitBoard::getOccupied() const {
    return black | white | arrows;
}

void BitBoard::movePiece(int fromX, int fromY, int toX, int toY, int color) {
    uint64_t from = 1ULL << (fromY * 8 + fromX);
    uint64_t to = 1ULL << (toY * 8 + toX);

    if (color == 1) {
        black &= ~from;
        black |= to;
    }
    else {
        white &= ~from;
        white |= to;
    }
}

void BitBoard::placeArrow(int x, int y) {
    uint64_t pos = 1ULL << (y * 8 + x);
    arrows |= pos;
}

void BitBoard::removeArrow(int x, int y) {
    uint64_t pos = 1ULL << (y * 8 + x);
    arrows &= ~pos;
}

void BitBoard::getPiecePositions(int color, std::vector<std::pair<int, int>>& positions) const {
    positions.clear();
    uint64_t pieces = (color == 1) ? black : white;

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            uint64_t pos = 1ULL << (y * 8 + x);
            if (pieces & pos) {
                positions.push_back({ x, y });
            }
        }
    }
}

int BitBoard::countEmptySquares() const {
    int count = 0;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (isEmpty(x, y)) count++;
        }
    }
    return count;
}

void BitBoard::setBoard(uint64_t b, uint64_t w, uint64_t a) {
    black = b;
    white = w;
    arrows = a;
}

void BitBoard::printBoard() const {
    std::cout << "  A B C D E F G H\n";
    for (int y = 0; y < 8; y++) {
        std::cout << y + 1 << " ";
        for (int x = 0; x < 8; x++) {
            int piece = getPieceAt(x, y);
            if (piece == 1) std::cout << "B ";
            else if (piece == -1) std::cout << "W ";
            else if (piece == 2) std::cout << "X ";
            else std::cout << ". ";
        }
        std::cout << "\n";
    }
}

std::string BitBoard::toString() const {
    std::stringstream ss;
    ss << "  A B C D E F G H\n";
    for (int y = 0; y < 8; y++) {
        ss << y + 1 << " ";
        for (int x = 0; x < 8; x++) {
            int piece = getPieceAt(x, y);
            if (piece == 1) ss << "B ";
            else if (piece == -1) ss << "W ";
            else if (piece == 2) ss << "X ";
            else ss << ". ";
        }
        ss << "\n";
    }
    return ss.str();
}