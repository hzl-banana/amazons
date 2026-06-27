#include "GameLogic.h"
#include "Utils/Constants.h"
#include <iostream>
#include <fstream>
#include <ctime>
#include <queue>
#include <tuple>

GameLogic::GameLogic() : currentPlayer(1), gameOver(false), winner(0) {
    newGame();
}

void GameLogic::newGame() {
    board.initialize();
    currentPlayer = 1;
    moveHistory.clear();
    gameOver = false;
    winner = 0;
}

bool GameLogic::makeMove(const Move& move) {
    if (!isValidMove(move, currentPlayer)) {
        return false;
    }

    board.movePiece(move.fromX, move.fromY, move.toX, move.toY, currentPlayer);
    board.placeArrow(move.arrowX, move.arrowY);

    moveHistory.push_back(move);
    currentPlayer = -currentPlayer;

    checkGameOver();
    return true;
}

bool GameLogic::isGameOver() const {
    return gameOver;
}

int GameLogic::fullMobility(int x, int y, const BitBoard& b) const {
    int count = 0;
    for (int d = 0; d < 8; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        while (nx >= 0 && nx < 8 && ny >= 0 && ny < 8 && b.isEmpty(nx, ny)) {
            count++;
            nx += dx[d];
            ny += dy[d];
        }
    }
    return count;
}

std::vector<Move> GameLogic::generateLegalMoves(int player) const {
    std::vector<Move> moves;
    std::vector<std::pair<int, int>> pieces;
    board.getPiecePositions(player, pieces);

    for (const auto& piece : pieces) {
        int x = piece.first, y = piece.second;

        for (int d = 0; d < 8; d++) {
            int step = 1;
            while (true) {
                int nx = x + dx[d] * step;
                int ny = y + dy[d] * step;

                if (nx < 0 || nx >= 8 || ny < 0 || ny >= 8) break;
                if (!board.isEmpty(nx, ny)) break;

                for (int ad = 0; ad < 8; ad++) {
                    int astep = 1;
                    while (true) {
                        int ax = nx + dx[ad] * astep;
                        int ay = ny + dy[ad] * astep;

                        if (ax < 0 || ax >= 8 || ay < 0 || ay >= 8) break;
                        if (!board.isEmpty(ax, ay) && !(ax == x && ay == y)) break;

                        moves.push_back(Move(x, y, nx, ny, ax, ay));
                        astep++;
                    }
                }
                step++;
            }
        }
    }
    return moves;
}

bool GameLogic::isValidMove(const Move& move, int player) const {
    // 边界检查
    if (move.fromX < 0 || move.fromX >= 8 || move.fromY < 0 || move.fromY >= 8) return false;
    if (move.toX < 0 || move.toX >= 8 || move.toY < 0 || move.toY >= 8) return false;
    if (move.arrowX < 0 || move.arrowX >= 8 || move.arrowY < 0 || move.arrowY >= 8) return false;

    if (board.getPieceAt(move.fromX, move.fromY) != player) return false;
    if (!board.isEmpty(move.toX, move.toY)) return false;
    if (!board.isEmpty(move.arrowX, move.arrowY) &&
        !(move.arrowX == move.fromX && move.arrowY == move.fromY)) {
        return false;
    }

    // 检查移动路径
    int dx_dir = 0, dy_dir = 0;
    if (move.toX != move.fromX) dx_dir = (move.toX > move.fromX) ? 1 : -1;
    if (move.toY != move.fromY) dy_dir = (move.toY > move.fromY) ? 1 : -1;

    int x = move.fromX + dx_dir;
    int y = move.fromY + dy_dir;

    while (x != move.toX || y != move.toY) {
        if (!board.isEmpty(x, y)) return false;
        x += dx_dir;
        y += dy_dir;
    }

    // 检查箭路径
    dx_dir = 0; dy_dir = 0;
    if (move.arrowX != move.toX) dx_dir = (move.arrowX > move.toX) ? 1 : -1;
    if (move.arrowY != move.toY) dy_dir = (move.arrowY > move.toY) ? 1 : -1;

    x = move.toX + dx_dir;
    y = move.toY + dy_dir;

    while (x != move.arrowX || y != move.arrowY) {
        if (x == move.fromX && y == move.fromY) {
            x += dx_dir;
            y += dy_dir;
            continue;
        }
        
        if (!board.isEmpty(x, y)) return false;
        x += dx_dir;
        y += dy_dir;
    }

    return true;
}

void GameLogic::checkGameOver() {
    std::vector<Move> moves = generateLegalMoves(currentPlayer);
    if (moves.empty()) {
        gameOver = true;
        winner = -currentPlayer;
    }
}

bool GameLogic::saveGame(const char* filename, int gameMode) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "无法创建存档文件: " << filename << std::endl;
        return false;
    }

    try {
        // 1. 写入文件头（魔数 + 版本号）
        const char magic[4] = {'A', 'M', 'Z', 'N'};
        file.write(magic, 4);
        
        int version = 2;  // 版本升级到2，支持游戏模式
        file.write(reinterpret_cast<const char*>(&version), sizeof(int));
        
        // 2. 写入保存时间
        time_t saveTime = time(nullptr);
        file.write(reinterpret_cast<const char*>(&saveTime), sizeof(time_t));
        
        // 3. 写入游戏模式（新增）
        file.write(reinterpret_cast<const char*>(&gameMode), sizeof(int));
        
        // 4. 写入棋盘状态（3个64位整数）
        uint64_t blackBits = board.getBlackBits();
        uint64_t whiteBits = board.getWhiteBits();
        uint64_t arrowBits = board.getArrowBits();
        
        file.write(reinterpret_cast<const char*>(&blackBits), sizeof(uint64_t));
        file.write(reinterpret_cast<const char*>(&whiteBits), sizeof(uint64_t));
        file.write(reinterpret_cast<const char*>(&arrowBits), sizeof(uint64_t));
        
        // 5. 写入游戏状态
        file.write(reinterpret_cast<const char*>(&currentPlayer), sizeof(int));
        file.write(reinterpret_cast<const char*>(&gameOver), sizeof(bool));
        file.write(reinterpret_cast<const char*>(&winner), sizeof(int));
        
        // 6. 写入移动历史
        int moveCount = static_cast<int>(moveHistory.size());
        file.write(reinterpret_cast<const char*>(&moveCount), sizeof(int));
        
        for (const Move& move : moveHistory) {
            file.write(reinterpret_cast<const char*>(&move.fromX), sizeof(int));
            file.write(reinterpret_cast<const char*>(&move.fromY), sizeof(int));
            file.write(reinterpret_cast<const char*>(&move.toX), sizeof(int));
            file.write(reinterpret_cast<const char*>(&move.toY), sizeof(int));
            file.write(reinterpret_cast<const char*>(&move.arrowX), sizeof(int));
            file.write(reinterpret_cast<const char*>(&move.arrowY), sizeof(int));
        }
        
        file.close();
        
        const char* modeNames[] = {"人机对战", "双人对战", "AI对战"};
        std::cout << "游戏已保存到: " << filename << std::endl;
        std::cout << "模式: " << modeNames[gameMode] 
                  << ", 步数: " << moveCount 
                  << ", 当前玩家: " << (currentPlayer == BLACK ? "黑方" : "白方") << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "保存游戏时出错: " << e.what() << std::endl;
        file.close();
        return false;
    }
}

bool GameLogic::loadGame(const char* filename, int& outGameMode) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "无法打开存档文件: " << filename << std::endl;
        return false;
    }

    try {
        // 1. 读取并验证文件头
        char magic[4];
        file.read(magic, 4);
        if (magic[0] != 'A' || magic[1] != 'M' || 
            magic[2] != 'Z' || magic[3] != 'N') {
            std::cerr << "无效的存档文件格式" << std::endl;
            file.close();
            return false;
        }
        
        int version;
        file.read(reinterpret_cast<char*>(&version), sizeof(int));
        
        // 2. 读取保存时间
        time_t saveTime;
        file.read(reinterpret_cast<char*>(&saveTime), sizeof(time_t));
        
        // 3. 读取游戏模式（版本2新增）
        if (version >= 2) {
            file.read(reinterpret_cast<char*>(&outGameMode), sizeof(int));
        } else {
            // 版本1的存档，默认为人机对战
            outGameMode = 0; // GM_HUMAN_VS_AI
        }
        
        // 4. 读取棋盘状态
        uint64_t blackBits, whiteBits, arrowBits;
        file.read(reinterpret_cast<char*>(&blackBits), sizeof(uint64_t));
        file.read(reinterpret_cast<char*>(&whiteBits), sizeof(uint64_t));
        file.read(reinterpret_cast<char*>(&arrowBits), sizeof(uint64_t));
        
        board.setBoard(blackBits, whiteBits, arrowBits);
        
        // 5. 读取游戏状态
        file.read(reinterpret_cast<char*>(&currentPlayer), sizeof(int));
        file.read(reinterpret_cast<char*>(&gameOver), sizeof(bool));
        file.read(reinterpret_cast<char*>(&winner), sizeof(int));
        
        // 6. 读取移动历史
        int moveCount;
        file.read(reinterpret_cast<char*>(&moveCount), sizeof(int));
        
        moveHistory.clear();
        moveHistory.reserve(moveCount);
        
        for (int i = 0; i < moveCount; i++) {
            Move move;
            file.read(reinterpret_cast<char*>(&move.fromX), sizeof(int));
            file.read(reinterpret_cast<char*>(&move.fromY), sizeof(int));
            file.read(reinterpret_cast<char*>(&move.toX), sizeof(int));
            file.read(reinterpret_cast<char*>(&move.toY), sizeof(int));
            file.read(reinterpret_cast<char*>(&move.arrowX), sizeof(int));
            file.read(reinterpret_cast<char*>(&move.arrowY), sizeof(int));
            moveHistory.push_back(move);
        }
        
        file.close();
        
        // 转换时间为可读格式
        struct tm timeinfo;
        localtime_s(&timeinfo, &saveTime);
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        
        const char* modeNames[] = {"人机对战", "双人对战", "AI对战"};
        std::cout << "游戏已加载: " << filename << std::endl;
        std::cout << "保存时间: " << timeStr << std::endl;
        std::cout << "模式: " << modeNames[outGameMode]
                  << ", 步数: " << moveCount 
                  << ", 当前玩家: " << (currentPlayer == BLACK ? "黑方" : "白方") << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "加载游戏时出错: " << e.what() << std::endl;
        file.close();
        return false;
    }
}

void GameLogic::undoMove() {
    if (moveHistory.empty()) return;

    // 获取最后一步移动
    Move lastMove = moveHistory.back();
    moveHistory.pop_back();
    
    // 移除箭
    board.removeArrow(lastMove.arrowX, lastMove.arrowY);
    
    // 移回棋子
    board.movePiece(lastMove.toX, lastMove.toY, lastMove.fromX, lastMove.fromY, -currentPlayer);
    
    // 切换玩家
    currentPlayer = -currentPlayer;
    
    // 重新检查游戏状态
    gameOver = false;
    winner = 0;
}

SaveFileInfo GameLogic::getSaveFileInfo(const char* filename) const {
    SaveFileInfo info;
    
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        info.exists = false;
        return info;
    }
    
    info.exists = true;
    
    try {
        // 读取并验证文件头
        char magic[4];
        file.read(magic, 4);
        if (magic[0] != 'A' || magic[1] != 'M' || 
            magic[2] != 'Z' || magic[3] != 'N') {
            info.isValid = false;
            file.close();
            return info;
        }
        
        int version;
        file.read(reinterpret_cast<char*>(&version), sizeof(int));
        if (version < 1 || version > 2) {
            info.isValid = false;
            file.close();
            return info;
        }
        
        // 读取保存时间
        file.read(reinterpret_cast<char*>(&info.saveTime), sizeof(time_t));
        
        // 读取游戏模式（版本2新增）
        if (version >= 2) {
            file.read(reinterpret_cast<char*>(&info.gameMode), sizeof(int));
        } else {
            info.gameMode = 0; // 默认人机对战
        }
        
        // 跳过棋盘状态（3 * 8 bytes）
        file.seekg(3 * sizeof(uint64_t), std::ios::cur);
        
        // 读取当前玩家
        file.read(reinterpret_cast<char*>(&info.currentPlayer), sizeof(int));
        
        // 跳过 gameOver 和 winner
        file.seekg(sizeof(bool) + sizeof(int), std::ios::cur);
        
        // 读取步数
        file.read(reinterpret_cast<char*>(&info.moveCount), sizeof(int));
        
        info.isValid = true;
        file.close();
        
    } catch (const std::exception& e) {
        info.isValid = false;
        file.close();
    }
    
    return info;
}