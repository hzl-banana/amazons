#pragma once
#include <graphics.h>
#include "../Game/GameLogic.h"
#include "../Utils/SoundManager.h"
#include <set>

// Forward declarations for skin functions (implemented in Graphics.cpp)
void DrawSkinButton();
void DrawSkinDialog();
void SetWhiteSkin(int skinIndex);
void SetBlackSkin(int skinIndex);
int GetWhiteSkin();
int GetBlackSkin();
void ToggleSkinDialog();
void ShowSkinDialog(bool show);
bool IsSkinDialogVisible();
bool CheckSkinDialogClick(int x, int y);

class Graphics {
private:
    int windowWidth, windowHeight;
    IMAGE imgChessboard;      // 棋盘背景图
    IMAGE imgStartBackground; // 开始界面背景图
    IMAGE imgBlackPiece;      // 黑棋图片
    IMAGE imgWhitePiece;      // 白棋子图片
    IMAGE imgArrow;           // 箭头图片
    IMAGE imgHighlight;       // 高亮图片
    IMAGE imgSelectedPiece;   // 选中棋子图片
    IMAGE imgMenuBg;          // 菜单背景
    IMAGE imgMenuBackground;  // 开始界面背景图
    IMAGE imgSkinDialogBg;    // 皮肤选择对话框背景图
    IMAGE imgSkinButton;      // 皮肤选择按钮图

    // 颜色常量
    COLORREF BOARD_COLOR = RGB(253, 193, 200);    // 棋盘底色 #FDC1C8
    COLORREF LINE_COLOR = RGB(101, 67, 33);       // 线条颜色
    COLORREF HIGHLIGHT_COLOR = RGB(255, 100, 100); // 高亮颜色

    // 音频管理器
    SoundManager soundManager;

    // 皮肤选择相关
    bool skinDialogVisible;          // 皮肤选择对话框是否可见
    int currentWhiteSkin;           // 当前白棋皮肤索引
    int currentBlackSkin;           // 当前黑棋皮肤索引

public:
    Graphics();
    ~Graphics();

    // 初始化
    bool initialize();

    // 绘制函数
    void drawMenu();                              // 绘制菜单
    void drawChessboard(const GameLogic& game);   // 绘制棋盘
    void drawPieces(const GameLogic& game);       // 绘制棋子
    void drawArrows(const GameLogic& game);       // 绘制箭
    void drawSidebar(const GameLogic& game);      // 绘制侧边栏信息
    void drawMoveIndicator(int fromX, int fromY, const std::vector<Move>& moves); // 绘制可移动位置
    void drawArrowIndicator(int fromX, int fromY, int toX, int toY, const std::vector<Move>& moves); // 绘制可放置箭的位置
    void drawGameOver(int winner);                // 绘制游戏结束画面
    void drawPauseMenu();                         // 绘制暂停菜单
    void drawSaveFileInfo(const SaveFileInfo& info);  // 显示存档信息
    void drawTimer(int blackTime, int whiteTime, int currentPlayer, int blackTimeLimit, int whiteTimeLimit); // 绘制倒计时
    void drawProgressBar(int x, int y, int width, int height, int percentage, COLORREF color); // 绘制进度条
    void drawTimeSelector(int selectedIndex);
    void drawVolumeSlider(int x, int y, int width, const wchar_t* label, int volume);  // 音量滑块
    void drawAIDifficultyDialog(int selectedDifficulty, int selectedColor);  // AI难度选择对话框

    // 皮肤相关函数
    void ToggleSkinDialog();                       // 切换皮肤选择对话框的显示状态
    void ShowSkinDialog(bool show);                // 显示或隐藏皮肤选择对话框
    bool IsSkinDialogVisible();                     // 检查皮肤选择对话框是否可见
    bool CheckSkinDialogClick(int x, int y);      // 检查皮肤选择对话框点击

    // 获取当前皮肤
    void SetWhiteSkin(int skinIndex);              // 设置白棋皮肤
    void SetBlackSkin(int skinIndex);              // 设置黑棋皮肤
    int GetWhiteSkin();                            // 获取白棋皮肤
    int GetBlackSkin();                            // 获取黑棋皮肤

    // 点击检测
    int getMenuButtonClicked(int x, int y) const;      // 返回菜单按钮索引，-1表示未点击任何按钮
    int getSidebarButtonClicked(int x, int y) const;   // 返回侧边栏按钮索引，-1表示未点击任何按钮
    int getGameOverButtonClicked(int x, int y) const;  // 返回游戏结束按钮索引
    
    // 音量滑块检测 - 返回: 0=无点击, 1=BGM滑块, 2=音效滑块
    int getVolumeSliderClicked(int x, int y) const;
    // 根据鼠标位置计算音量值 (0-1000)
    int calculateVolumeFromPosition(int mouseX, int sliderX, int sliderWidth) const;

    // 坐标转换
    int screenToBoardX(int screenX) const;
    int screenToBoardY(int screenY) const;
    int boardToScreenX(int boardX) const;
    int boardToScreenY(int boardY) const;

    // 工具函数
    void drawRoundedRect(int x, int y, int width, int height, int radius, COLORREF color);
    void drawTextCentered(int x, int y, const wchar_t* text, int fontSize = 20, COLORREF color = BLACK);
    
    // Alpha透明绘制（修复PNG黑边问题）
    void putimage_alpha(IMAGE* dstimg, IMAGE* srcimg, int x, int y);

    // 获取音频管理器（供外部使用）
    SoundManager& getSoundManager() { return soundManager; }

private:
    void loadImages();
    void drawCoordinateNumbers();
};
