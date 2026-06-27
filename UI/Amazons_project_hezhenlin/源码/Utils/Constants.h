#pragma once

// 取消Windows头文件中可能存在的宏定义
#ifdef BLACK
#undef BLACK
#endif

#ifdef WHITE
#undef WHITE
#endif

#ifdef EMPTY
#undef EMPTY
#endif

#ifdef ARROW
#undef ARROW
#endif

constexpr int BOARD_SIZE = 8;
constexpr int GRID_SIZE = 80;
constexpr int WINDOW_WIDTH = 1000;  // 从800增加到1000以完整显示侧边栏
constexpr int WINDOW_HEIGHT = 700;
constexpr int BOARD_OFFSET_X = 50;
constexpr int BOARD_OFFSET_Y = 50;
constexpr int SIDEBAR_WIDTH = 200;

// 侧边栏按钮定义
constexpr int SIDEBAR_X = BOARD_OFFSET_X + BOARD_SIZE * GRID_SIZE + 20;
constexpr int SIDEBAR_Y = BOARD_OFFSET_Y;
constexpr int SIDEBAR_BUTTON_WIDTH = SIDEBAR_WIDTH - 40;
constexpr int SIDEBAR_BUTTON_HEIGHT = 35;
constexpr int SIDEBAR_BUTTON_SPACING = 45;
constexpr int SIDEBAR_BUTTON_START_Y = 300; // 按钮起始Y坐标

// 菜单按钮定义
constexpr int MENU_CENTER_X = WINDOW_WIDTH / 2;
constexpr int MENU_START_Y = 200;
constexpr int MENU_BUTTON_WIDTH = 300;
constexpr int MENU_BUTTON_HEIGHT = 50;
constexpr int MENU_BUTTON_SPACING = 20;

// 现在可以安全地定义我们的常量
constexpr int BLACK = 1;
constexpr int WHITE = -1;
constexpr int EMPTY = 0;
constexpr int ARROW = 2;

// 8 directions for piece movement
static constexpr int dx[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
static constexpr int dy[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };

// Game state
enum GameState {
    GS_MENU,
    GS_AI_DIFFICULTY_SETTINGS,  // AI难度和颜色选择
    GS_PLAYING,
    GS_PAUSED,
    GS_GAMEOVER,
    GS_REPLAY
};

// Game mode
enum GameMode {
    GM_HUMAN_VS_AI,
    GM_HUMAN_VS_HUMAN,
    GM_AI_VS_AI
};

// AI Difficulty
enum AIDifficulty {
    AI_EASY = 0,      // 简单模式 (原MCTS)
    AI_HARD = 1       // 地狱模式 (AI-hard)
};

// Player Color (for Human vs AI)
enum PlayerColor {
    PLAYER_BLACK = 0,  // 玩家执黑
    PLAYER_WHITE = 1   // 玩家执白
};

// Menu button indices
enum MenuButton {
    MB_HUMAN_VS_AI = 0,
    MB_HUMAN_VS_HUMAN = 1,
    MB_LOAD_GAME = 2,
    MB_EXIT = 3
};

// Sidebar button indices
enum SidebarButton {
    SB_NEW_GAME = 0,
    SB_SAVE_GAME = 1,
    SB_LOAD_GAME = 2,
    SB_UNDO = 3,           // 悔棋按钮
    SB_PAUSE = 4,
    SB_EXIT = 5
};
