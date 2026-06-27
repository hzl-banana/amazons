#pragma comment(lib, "Msimg32.lib")

#include "Graphics.h"
#include "../Utils/Constants.h"
#include "../Game/GameLogic.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

// Skin selection state
static int g_whiteSkin = 0;
static int g_blackSkin = 0;
static bool g_showSkinDialog = false;

// Skin selection functions
void DrawSkinButton()
{
    const int bx = 10;
    const int by = 10;
    const int bw = 60;
    const int bh = 24;

    setfillcolor(RGB(200, 200, 200));
    solidrectangle(bx, by, bx + bw, by + bh);
    setlinecolor(RGB(100, 100, 100));
    rectangle(bx, by, bx + bw, by + bh);
    
    settextcolor(BLACK);
    settextstyle(16, 0, _T("微软雅黑"));
    outtextxy(bx + 8, by + 4, _T("皮肤"));
}

void DrawSkinDialog()
{
    if (!g_showSkinDialog) return;

    const int dx = 200;
    const int dy = 150;
    const int dw = 600;
    const int dh = 300;

    setfillcolor(RGB(240, 240, 240));
    solidrectangle(dx, dy, dx + dw, dy + dh);
    setlinecolor(RGB(100, 100, 100));
    setlinestyle(PS_SOLID, 2);
    rectangle(dx, dy, dx + dw, dy + dh);

    settextcolor(BLACK);
    settextstyle(24, 0, _T("微软雅黑"));
    outtextxy(dx + dw / 2 - 48, dy + 20, _T("皮肤选择"));

    const int imgW = 120;
    const int imgH = 120;
    const int leftX = dx + 100;
    const int rightX = dx + dw - 100 - imgW;
    const int imgY = dy + 80;

    setfillcolor(RGB(220, 220, 220));
    solidrectangle(leftX, imgY, leftX + imgW, imgY + imgH);
    solidrectangle(rightX, imgY, rightX + imgW, rightX + imgH);

    IMAGE previewImg;
    if (std::filesystem::exists("images/white_skin1_8x8.bmp")) {
        loadimage(&previewImg, _T("images/white_skin1_8x8.bmp"));
        putimage(leftX + (imgW - previewImg.getwidth()) / 2, 
                 imgY + (imgH - previewImg.getheight()) / 2, &previewImg);
    }

    if (std::filesystem::exists("images/white_skin2_8x8.bmp")) {
        loadimage(&previewImg, _T("images/white_skin2_8x8.bmp"));
        putimage(rightX + (imgW - previewImg.getwidth()) / 2, 
                 imgY + (imgH - previewImg.getheight()) / 2, &previewImg);
    }

    settextstyle(20, 0, _T("微软雅黑"));
    settextcolor(BLACK);
    outtextxy(leftX + 30, imgY + imgH + 15, _T("皮肤一"));
    outtextxy(rightX + 30, imgY + imgH + 15, _T("皮肤二"));

    if (g_whiteSkin == 0) {
        setlinecolor(RGB(0, 120, 215));
        setlinestyle(PS_SOLID, 3);
        rectangle(leftX - 3, imgY - 3, leftX + imgW + 3, imgY + imgH + 3);
    } else {
        setlinecolor(RGB(0, 120, 215));
        setlinestyle(PS_SOLID, 3);
        rectangle(rightX - 3, imgY - 3, rightX + imgW + 3, imgY + imgH + 3);
    }
    
    setlinestyle(PS_SOLID, 1);
}

void SetWhiteSkin(int skinIndex) { g_whiteSkin = skinIndex; }
void SetBlackSkin(int skinIndex) { g_blackSkin = skinIndex; }
int GetWhiteSkin() { return g_whiteSkin; }
int GetBlackSkin() { return g_blackSkin; }

void ToggleSkinDialog() { g_showSkinDialog = !g_showSkinDialog; }
void ShowSkinDialog(bool show) { g_showSkinDialog = show; }
bool IsSkinDialogVisible() { return g_showSkinDialog; }

bool CheckSkinDialogClick(int x, int y) {
    if (!g_showSkinDialog) return false;
    
    const int dx = 200;
    const int dy = 150;
    const int dw = 600;
    const int imgW = 120;
    const int imgH = 120;
    const int leftX = dx + 100;
    const int rightX = dx + dw - 100 - imgW;
    const int imgY = dy + 80;
    
    if (x >= leftX && x <= leftX + imgW && y >= imgY && y <= imgY + imgH) {
        g_whiteSkin = 0;
        g_blackSkin = 0;
        return true;
    }
    
    if (x >= rightX && x <= rightX + imgW && y >= imgY && y <= imgY + imgH) {
        g_whiteSkin = 1;
        g_blackSkin = 1;
        return true;
    }
    
    return false;
}

Graphics::Graphics() : windowWidth(WINDOW_WIDTH), windowHeight(WINDOW_HEIGHT) {
}

Graphics::~Graphics() {
    closegraph();
}

bool Graphics::initialize() {
    initgraph(windowWidth, windowHeight);
    setbkmode(TRANSPARENT);
    loadImages();
    
    // 初始化音频系统
    if (soundManager.initialize("sound/")) {
        std::cout << "音频系统初始化成功" << std::endl;
        // 开始播放背景音乐
        soundManager.playBGM();
    } else {
        std::cerr << "音频系统初始化失败" << std::endl;
    }
    
    return true;
}

void Graphics::loadImages() {
    // 创建棋盘背景图
    imgChessboard = IMAGE(windowWidth, windowHeight);
    SetWorkingImage(&imgChessboard);
    setfillcolor(BOARD_COLOR);
    solidrectangle(0, 0, windowWidth, windowHeight);
    SetWorkingImage();

    // 从PNG文件加载图片（80x80像素）
    std::wcout << L"[图片] 开始加载PNG图片..." << std::endl;

    // 加载开始界面背景图 (1000x700)
    loadimage(&imgMenuBackground, L"images/start_background.png", windowWidth, windowHeight);
    std::wcout << L"[图片] 开始界面背景加载完成 (" << windowWidth << "x" << windowHeight << ")" << std::endl;

    
    // 加载黑棋图片（80x80）
    loadimage(&imgBlackPiece, L"images/black_piece.png", 80, 80);
    std::wcout << L"[图片] 黑棋图片加载完成 (80x80)" << std::endl;
    
    // 加载白棋图片（80x80）
    loadimage(&imgWhitePiece, L"images/white_piece.png", 80, 80);
    std::wcout << L"[图片] 白棋图片加载完成 (80x80)" << std::endl;
    
    // 加载箭图片（80x80）
    loadimage(&imgArrow, L"images/arrow.png", 80, 80);
    std::wcout << L"[图片] 箭图片加载完成 (80x80)" << std::endl;
    
    // 加载选中棋子图片（80x80）
    loadimage(&imgSelectedPiece, L"images/selected_piece.png", 80, 80);
    std::wcout << L"[图片] 选中棋子图片加载完成 (80x80)" << std::endl;
    
    // 创建高亮图片（用于可移动位置提示）
    imgHighlight = IMAGE(GRID_SIZE - 10, GRID_SIZE - 10);
    SetWorkingImage(&imgHighlight);
    setfillcolor(HIGHLIGHT_COLOR);
    solidrectangle(0, 0, GRID_SIZE - 10, GRID_SIZE - 10);
    SetWorkingImage();
    
    // 加载菜单背景图
    loadimage(&imgMenuBackground, L"images/menu_background.jpg");
    std::wcout << L"[图片] 菜单背景图加载完成" << std::endl;
    
    std::wcout << L"[图片] 所有图片加载完成" << std::endl;
}

void Graphics::drawMenu() {
    // 绘制背景图
    putimage(0, 0, &imgMenuBackground);
    
    // 标题使用半透明背景
    int titleBoxWidth = 400;
    int titleBoxHeight = 100;
    int titleBoxX = (WINDOW_WIDTH - titleBoxWidth) / 2;
    int titleBoxY = 40;
    
    // 半透明深色背景（使用深灰色模拟）
    setfillcolor(RGB(30, 30, 40));
    setlinecolor(RGB(100, 100, 120));
    setlinestyle(PS_SOLID, 2);
    solidroundrect(titleBoxX, titleBoxY, titleBoxX + titleBoxWidth, titleBoxY + titleBoxHeight, 15, 15);
    
    // 标题文字 - 亮黄色加粗
    settextcolor(RGB(255, 220, 0));
    settextstyle(48, 0, _T("微软雅黑"), 0, 0, FW_BOLD, false, false, false);
    drawTextCentered(WINDOW_WIDTH / 2, titleBoxY + titleBoxHeight / 2, _T("亚马逊棋"), 48, RGB(255, 220, 0));

    // 菜单按钮
    const TCHAR* menuItems[] = {
        _T("人机对战"),
        _T("双人对战"),
        _T("读取游戏"),
        _T("退出游戏")
    };

    for (int i = 0; i < 4; i++) {
        int y = MENU_START_Y + i * (MENU_BUTTON_HEIGHT + MENU_BUTTON_SPACING);
        drawRoundedRect(MENU_CENTER_X - MENU_BUTTON_WIDTH / 2, y, MENU_BUTTON_WIDTH, MENU_BUTTON_HEIGHT, 10, RGB(70, 130, 180));
        drawTextCentered(MENU_CENTER_X, y + MENU_BUTTON_HEIGHT / 2, menuItems[i], 24, WHITE);
    }

    // 版权信息 - 右下角
    settextstyle(14, 0, _T("宋体"));
    settextcolor(RGB(200, 200, 200));
    outtextxy(WINDOW_WIDTH - 280, WINDOW_HEIGHT - 30, _T("亚马逊棋 - 计算机博弈课程设计"));
}

int Graphics::getMenuButtonClicked(int x, int y) const {
    int leftX = MENU_CENTER_X - MENU_BUTTON_WIDTH / 2;
    int rightX = MENU_CENTER_X + MENU_BUTTON_WIDTH / 2;

    for (int i = 0; i < 4; i++) {
        int topY = MENU_START_Y + i * (MENU_BUTTON_HEIGHT + MENU_BUTTON_SPACING);
        int bottomY = topY + MENU_BUTTON_HEIGHT;

        if (x >= leftX && x <= rightX && y >= topY && y <= bottomY) {
            return i;
        }
    }
    return -1;
}

void Graphics::drawChessboard(const GameLogic& game) {
    setfillcolor(BOARD_COLOR);
    solidrectangle(BOARD_OFFSET_X - 5, BOARD_OFFSET_Y - 5,
        BOARD_OFFSET_X + BOARD_SIZE * GRID_SIZE + 5,
        BOARD_OFFSET_Y + BOARD_SIZE * GRID_SIZE + 5);

    setlinecolor(LINE_COLOR);
    setlinestyle(PS_SOLID, 3);
    rectangle(BOARD_OFFSET_X - 5, BOARD_OFFSET_Y - 5,
        BOARD_OFFSET_X + BOARD_SIZE * GRID_SIZE + 5,
        BOARD_OFFSET_Y + BOARD_SIZE * GRID_SIZE + 5);

    setlinestyle(PS_SOLID, 1);
    for (int i = 0; i <= BOARD_SIZE; i++) {
        line(BOARD_OFFSET_X + i * GRID_SIZE, BOARD_OFFSET_Y,
            BOARD_OFFSET_X + i * GRID_SIZE, BOARD_OFFSET_Y + BOARD_SIZE * GRID_SIZE);
        line(BOARD_OFFSET_X, BOARD_OFFSET_Y + i * GRID_SIZE,
            BOARD_OFFSET_X + BOARD_SIZE * GRID_SIZE, BOARD_OFFSET_Y + i * GRID_SIZE);
    }

    drawCoordinateNumbers();
}

void Graphics::drawPieces(const GameLogic& game) {
    const BitBoard& board = game.getBoard();

    // Load images based on current skin selection
    IMAGE imgWhite, imgBlack;
    
    int whiteSkin = GetWhiteSkin();
    int blackSkin = GetBlackSkin();
    
    // Choose image path based on skin selection
    const wchar_t* whitePath = (whiteSkin == 0) ? L"images/white_piece.png" : L"images/white_piece.png";  // 默认都用同一张，您可以准备 white_piece_skin2.png
    const wchar_t* blackPath = (blackSkin == 0) ? L"images/black_piece.png" : L"images/black_piece_skin2.png";  // 默认黑棋用黑色皮肤2
    
    loadimage(&imgWhite, whitePath, 80, 80);
    loadimage(&imgBlack, blackPath, 80, 80);

    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            int piece = board.getPieceAt(x, y);
            if (piece == BLACK) {
                int screenX = boardToScreenX(x);
                int screenY = boardToScreenY(y);
                // 使用Alpha透明绘制，PNG透明部分不会显示黑色
                putimage_alpha(NULL, &imgBlack, screenX, screenY);
            }
            else if (piece == WHITE) {
                int screenX = boardToScreenX(x);
                int screenY = boardToScreenY(y);
                // 使用Alpha透明绘制
                putimage_alpha(NULL, &imgWhite, screenX, screenY);
            }
        }
    }
}

void Graphics::drawArrows(const GameLogic& game) {
    const BitBoard& board = game.getBoard();

    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (board.getPieceAt(x, y) == ARROW) {
                int screenX = boardToScreenX(x);
                int screenY = boardToScreenY(y);
                // 使用Alpha透明绘制箭，PNG透明部分不会显示黑色
                putimage_alpha(NULL, &imgArrow, screenX, screenY);
            }
        }
    }
}

void Graphics::drawSidebar(const GameLogic& game) {
    int sidebarX = SIDEBAR_X;
    int sidebarY = SIDEBAR_Y;
    int sidebarWidth = SIDEBAR_WIDTH;
    int sidebarHeight = BOARD_SIZE * GRID_SIZE;

    setfillcolor(RGB(240, 240, 240));
    solidrectangle(sidebarX, sidebarY, sidebarX + sidebarWidth, sidebarY + sidebarHeight);

    setlinecolor(RGB(180, 180, 180));
    rectangle(sidebarX, sidebarY, sidebarX + sidebarWidth, sidebarY + sidebarHeight);

    settextcolor(BLACK);
    settextstyle(20, 0, _T("Arial"));

    int y = sidebarY + 20;
    outtextxy(sidebarX + 20, y, _T("亚马逊棋"));

    y += 40;
    settextstyle(16, 0, _T("Arial"));

    const TCHAR* player = (game.getCurrentPlayer() == BLACK) ? _T("黑方回合") : _T("白方回合");
    outtextxy(sidebarX + 20, y, player);

    y += 30;
    TCHAR moveCount[50];
    _stprintf_s(moveCount, _T("步数: %d"), static_cast<int>(game.getMoveHistory().size()));
    outtextxy(sidebarX + 20, y, moveCount);

    // 预留倒计时区域空间 (drawTimer会在这里绘制)
    y += 150;  // 跳过倒计时显示区域

    // 绘制按钮
    const TCHAR* buttonLabels[] = {
        _T("新游戏"),
        _T("保存游戏"),
        _T("读取游戏"),
        _T("悔棋"),         // 新增悔棋按钮
        _T("暂停"),
        _T("返回菜单")
    };

    COLORREF buttonColors[] = {
        RGB(100, 150, 250),  // 新游戏 - 蓝色
        RGB(100, 200, 100),  // 保存游戏 - 绿色
        RGB(250, 150, 100),  // 读取游戏 - 橙色
        RGB(255, 200, 0),    // 悔棋 - 黄色
        RGB(200, 200, 100),  // 暂停 - 黄灰色
        RGB(200, 100, 100)   // 返回菜单 - 红色
    };

    for (int i = 0; i < 6; i++) {  // 改为6个按钮
        int btnY = SIDEBAR_BUTTON_START_Y + i * SIDEBAR_BUTTON_SPACING;
        drawRoundedRect(sidebarX + 20, btnY, SIDEBAR_BUTTON_WIDTH, SIDEBAR_BUTTON_HEIGHT, 5, buttonColors[i]);
        drawTextCentered(sidebarX + sidebarWidth / 2, btnY + SIDEBAR_BUTTON_HEIGHT / 2, buttonLabels[i], 16, WHITE);
    
    // 音量控制区域（在按钮下方）
    int volumeY = SIDEBAR_BUTTON_START_Y + 6 * SIDEBAR_BUTTON_SPACING + 20;
    
    // 音量标题
    settextcolor(RGB(80, 80, 80));
    settextstyle(16, 0, _T("微软雅黑"));
    outtextxy(sidebarX + 20, volumeY, _T("音量"));
    
    // BGM音量滑块
    drawVolumeSlider(sidebarX + 20, volumeY + 30, SIDEBAR_WIDTH - 80, L"BGM", soundManager.getBGMVolume());
    
    // 音效音量滑块
    drawVolumeSlider(sidebarX + 20, volumeY + 70, SIDEBAR_WIDTH - 80, L"音效", soundManager.getSoundVolume());
    }
}

int Graphics::getSidebarButtonClicked(int x, int y) const {
    int leftX = SIDEBAR_X + 20;
    int rightX = leftX + SIDEBAR_BUTTON_WIDTH;

    for (int i = 0; i < 6; i++) {  // 改为6个按钮
        int topY = SIDEBAR_BUTTON_START_Y + i * SIDEBAR_BUTTON_SPACING;
        int bottomY = topY + SIDEBAR_BUTTON_HEIGHT;

        if (x >= leftX && x <= rightX && y >= topY && y <= bottomY) {
            return i;
        }
    }
    return -1;
}

void Graphics::drawPauseMenu() {
    // 半透明遮罩
    setfillcolor(RGB(0, 0, 0));
    // 注意：EasyX 可能不支持真正的半透明，这里用深色背景
    solidrectangle(0, 0, windowWidth, windowHeight);

    int boxWidth = 400, boxHeight = 300;
    int boxX = (windowWidth - boxWidth) / 2, boxY = (windowHeight - boxHeight) / 2;

    setfillcolor(RGB(240, 240, 240));
    solidrectangle(boxX, boxY, boxX + boxWidth, boxY + boxHeight);

    setlinecolor(RGB(100, 100, 100));
    setlinestyle(PS_SOLID, 3);
    rectangle(boxX, boxY, boxX + boxWidth, boxY + boxHeight);

    settextcolor(BLACK);
    settextstyle(32, 0, _T("微软雅黑"));
    drawTextCentered(windowWidth / 2, boxY + 50, _T("游戏已暂停"), 32, BLACK);

    // 暂停菜单按钮
    const TCHAR* pauseButtons[] = {
        _T("继续游戏"),
        _T("保存游戏"),
        _T("返回菜单")
    };

    COLORREF pauseColors[] = {
        RGB(100, 200, 100),
        RGB(250, 150, 100),
        RGB(200, 100, 100)
    };

    for (int i = 0; i < 3; i++) {
        int btnY = boxY + 100 + i * 60;
        drawRoundedRect(boxX + 50, btnY, 300, 40, 5, pauseColors[i]);
        drawTextCentered(windowWidth / 2, btnY + 20, pauseButtons[i], 20, WHITE);
    }
}

void Graphics::drawSaveFileInfo(const SaveFileInfo& info) {
    // 绘制存档信息对话框
    int boxWidth = 450, boxHeight = 280;  // 增加高度以显示游戏模式
    int boxX = (windowWidth - boxWidth) / 2;
    int boxY = (windowHeight - boxHeight) / 2;

    // 背景
    setfillcolor(RGB(240, 240, 240));
    solidrectangle(boxX, boxY, boxX + boxWidth, boxY + boxHeight);

    // 边框
    setlinecolor(RGB(100, 100, 100));
    setlinestyle(PS_SOLID, 3);
    rectangle(boxX, boxY, boxX + boxWidth, boxY + boxHeight);

    settextcolor(BLACK);
    
    if (!info.exists) {
        // 存档不存在
        settextstyle(24, 0, _T("微软雅黑"));
        drawTextCentered(windowWidth / 2, boxY + 60, _T("未找到存档文件"), 24, BLACK);
        
        settextstyle(16, 0, _T("微软雅黑"));
        drawTextCentered(windowWidth / 2, boxY + 110, _T("文件: savegame.dat"), 16, RGB(100, 100, 100));
        
    } else if (!info.isValid) {
        // 存档文件损坏
        settextstyle(24, 0, _T("微软雅黑"));
        drawTextCentered(windowWidth / 2, boxY + 60, _T("存档文件已损坏"), 24, RGB(200, 0, 0));
        
        settextstyle(16, 0, _T("微软雅黑"));
        drawTextCentered(windowWidth / 2, boxY + 110, _T("无法读取存档信息"), 16, RGB(100, 100, 100));
        
    } else {
        // 显示存档信息
        settextstyle(24, 0, _T("微软雅黑"));
        drawTextCentered(windowWidth / 2, boxY + 30, _T("存档信息"), 24, BLACK);
        
        // 转换时间
        struct tm timeinfo;
        localtime_s(&timeinfo, &info.saveTime);
        wchar_t timeStr[128];
        wcsftime(timeStr, sizeof(timeStr) / sizeof(wchar_t), L"保存时间: %Y年%m月%d日 %H:%M:%S", &timeinfo);
        
        settextstyle(16, 0, _T("微软雅黑"));
        int y = boxY + 70;
        
        outtextxy(boxX + 50, y, timeStr);
        y += 30;
        
        // 显示游戏模式
        const wchar_t* modeNames[] = {L"人机对战", L"双人对战", L"AI对战"};
        wchar_t modeStr[64];
        if (info.gameMode >= 0 && info.gameMode <= 2) {
            swprintf_s(modeStr, L"游戏模式: %s", modeNames[info.gameMode]);
        } else {
            swprintf_s(modeStr, L"游戏模式: 未知");
        }
        outtextxy(boxX + 50, y, modeStr);
        y += 30;
        
        wchar_t moveCountStr[64];
        swprintf_s(moveCountStr, L"游戏步数: %d", info.moveCount);
        outtextxy(boxX + 50, y, moveCountStr);
        y += 30;
        
        const wchar_t* playerStr = (info.currentPlayer == BLACK) ? L"当前回合: 黑方" : L"当前回合: 白方";
        outtextxy(boxX + 50, y, playerStr);
    }
    
    // 确定按钮
    int btnY = boxY + boxHeight - 60;
    drawRoundedRect(boxX + 125, btnY, 200, 40, 5, RGB(100, 150, 250));
    drawTextCentered(windowWidth / 2, btnY + 20, _T("确定"), 20, WHITE);
}

int Graphics::getGameOverButtonClicked(int x, int y) const {
    int boxWidth = 400, boxHeight = 200;
    int boxX = (windowWidth - boxWidth) / 2, boxY = (windowHeight - boxHeight) / 2;

    // 新游戏按钮
    if (x >= boxX + 50 && x <= boxX + 170 && y >= boxY + 120 && y <= boxY + 160) {
        return 0;
    }
    // 退出按钮
    if (x >= boxX + 230 && x <= boxX + 350 && y >= boxY + 120 && y <= boxY + 160) {
        return 1;
    }
    return -1;
}

void Graphics::drawGameOver(int winner) {
    setfillcolor(RGB(150, 255, 150));
    solidrectangle(0, 0, windowWidth, windowHeight);

    int boxWidth = 400, boxHeight = 200;
    int boxX = (windowWidth - boxWidth) / 2, boxY = (windowHeight - boxHeight) / 2;

    setfillcolor(RGB(255, 255, 255));
    solidrectangle(boxX, boxY, boxX + boxWidth, boxY + boxHeight);

    setlinecolor(RGB(100, 100, 100));
    setlinestyle(PS_SOLID, 3);
    rectangle(boxX, boxY, boxX + boxWidth, boxY + boxHeight);

    settextcolor(BLACK);
    settextstyle(32, 0, _T("微软雅黑"));

    if (winner == BLACK) {
        drawTextCentered(windowWidth / 2, boxY + 50, L"黑方胜利!", 32, BLACK);
    }
    else if (winner == WHITE) {
        drawTextCentered(windowWidth / 2, boxY + 50, L"白方胜利!", 32, BLACK);
    }
    else {
        drawTextCentered(windowWidth / 2, boxY + 50, L"游戏结束!", 32, BLACK);
    }

    drawRoundedRect(boxX + 50, boxY + 120, 120, 40, 5, RGB(100, 200, 100));
    drawTextCentered(boxX + 110, boxY + 135, L"新游戏", 20, WHITE);;

    drawRoundedRect(boxX + 230, boxY + 120, 120, 40, 5, RGB(200, 100, 100));
    drawTextCentered(boxX + 290, boxY + 135, L"退出", 20, WHITE);
}

void Graphics::drawCoordinateNumbers() {
    settextcolor(BLACK);
    settextstyle(16, 0, _T("Arial"));

    for (int i = 0; i < BOARD_SIZE; i++) {
        wchar_t coord[2] = { (wchar_t)('A' + i), L'\0' };
        int x = BOARD_OFFSET_X + i * GRID_SIZE + GRID_SIZE / 2 - 5;
        outtextxy(x, BOARD_OFFSET_Y - 25, coord);
        outtextxy(x, BOARD_OFFSET_Y + BOARD_SIZE * GRID_SIZE + 5, coord);
    }

    for (int i = 0; i < BOARD_SIZE; i++) {
        wchar_t coord[2] = { (wchar_t)('1' + i), L'\0' };
        int y = BOARD_OFFSET_Y + i * GRID_SIZE + GRID_SIZE / 2 - 8;
        outtextxy(BOARD_OFFSET_X - 25, y, coord);
        outtextxy(BOARD_OFFSET_X + BOARD_SIZE * GRID_SIZE + 10, y, coord);
    }
}

int Graphics::screenToBoardX(int screenX) const {
    return (screenX - BOARD_OFFSET_X) / GRID_SIZE;
}

int Graphics::screenToBoardY(int screenY) const {
    return (screenY - BOARD_OFFSET_Y) / GRID_SIZE;
}

int Graphics::boardToScreenX(int boardX) const {
    return BOARD_OFFSET_X + boardX * GRID_SIZE;
}

int Graphics::boardToScreenY(int boardY) const {
    return BOARD_OFFSET_Y + boardY * GRID_SIZE;
}

void Graphics::drawRoundedRect(int x, int y, int width, int height, int radius, COLORREF color) {
    setfillcolor(color);
    setlinecolor(color);
    solidroundrect(x, y, x + width, y + height, radius, radius);
}

void Graphics::drawTextCentered(int x, int y, const wchar_t* text, int fontSize, COLORREF color) {
    settextcolor(color);
    settextstyle(fontSize, 0, _T("宋体"));
    int textWidth = textwidth(text);
    int textHeight = textheight(text);
    outtextxy(x - textWidth / 2, y - textHeight / 2, text);
}

// Alpha透明绘制函数 - 修复PNG黑边问题
void Graphics::putimage_alpha(IMAGE* dstimg, IMAGE* srcimg, int x, int y) {
    HDC dstDC = GetImageHDC(dstimg);
    HDC srcDC = GetImageHDC(srcimg);
    int width = srcimg->getwidth();
    int height = srcimg->getheight();
    
    BLENDFUNCTION blend;
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    
    AlphaBlend(dstDC, x, y, width, height, srcDC, 0, 0, width, height, blend);
}

void Graphics::drawMoveIndicator(int fromX, int fromY, const std::vector<Move>& moves) {
    if (fromX < 0 || fromY < 0) return;

    // 在选中的棋子位置绘制高亮图片
    int screenX = boardToScreenX(fromX);
    int screenY = boardToScreenY(fromY);
    
    // 绘制选中棋子的高亮效果（使用Alpha透明PNG图片）
    putimage_alpha(NULL, &imgSelectedPiece, screenX, screenY);

    // 收集所有可移动到的位置（去重）
    std::set<std::pair<int, int>> movePositions;
    for (const auto& move : moves) {
        if (move.fromX == fromX && move.fromY == fromY) {
            movePositions.insert({move.toX, move.toY});
        }
    }

    // 绘制可移动到的位置（淡黄色半透明效果）
    for (const auto& pos : movePositions) {
        int toScreenX = boardToScreenX(pos.first);
        int toScreenY = boardToScreenY(pos.second);
        
        setfillcolor(RGB(255, 255, 100));
        setlinecolor(RGB(255, 200, 0));
        setlinestyle(PS_SOLID, 2);
        solidrectangle(toScreenX + 8, toScreenY + 8, 
                     toScreenX + GRID_SIZE - 8, toScreenY + GRID_SIZE - 8);
        
        // 绘制一个小圆点作为中心标记
        setfillcolor(RGB(255, 150, 0));
        solidcircle(toScreenX + GRID_SIZE / 2, toScreenY + GRID_SIZE / 2, 6);
    }

    // 恢复默认线条样式
    setlinestyle(PS_SOLID, 1);
}

void Graphics::drawArrowIndicator(int fromX, int fromY, int toX, int toY, const std::vector<Move>& moves) {
    // 高亮移动后的位置
    int toScreenX = boardToScreenX(toX);
    int toScreenY = boardToScreenY(toY);
    
    setlinecolor(RGB(0, 150, 255));
    setlinestyle(PS_SOLID, 4);
    rectangle(toScreenX + 3, toScreenY + 3, toScreenX + GRID_SIZE - 3, toScreenY + GRID_SIZE - 3);

    // 收集所有可放置箭的位置（去重）
    std::set<std::pair<int, int>> arrowPositions;
    for (const auto& move : moves) {
        if (move.fromX == fromX && move.fromY == fromY &&
            move.toX == toX && move.toY == toY) {
            arrowPositions.insert({move.arrowX, move.arrowY});
        }
    }

    // 绘制可放置箭的位置（淡绿色）
    for (const auto& pos : arrowPositions) {
        int arrowScreenX = boardToScreenX(pos.first);
        int arrowScreenY = boardToScreenY(pos.second);
        
        setfillcolor(RGB(100, 255, 100));
        setlinecolor(RGB(0, 200, 0));
        setlinestyle(PS_SOLID, 2);
        solidrectangle(arrowScreenX + 8, arrowScreenY + 8, 
                     arrowScreenX + GRID_SIZE - 8, arrowScreenY + GRID_SIZE - 8);
        
        // 绘制箭头标记
        setfillcolor(RGB(0, 180, 0));
        solidcircle(arrowScreenX + GRID_SIZE / 2, arrowScreenY + GRID_SIZE / 2, 5);
    }

    // 恢复默认线条样式
    setlinestyle(PS_SOLID, 1);
}

void Graphics::drawTimer(int blackTime, int whiteTime, int currentPlayer, int blackTimeLimit, int whiteTimeLimit) {
    int timerX = SIDEBAR_X + 20;
    int timerY = SIDEBAR_Y + 100;
    int timerWidth = SIDEBAR_WIDTH - 40;
    
    // 绘制标题
    settextcolor(RGB(80, 80, 80));
    settextstyle(18, 0, _T("微软雅黑"));
    outtextxy(timerX, timerY, _T("倒计时"));
    
    // 黑方计时器
    bool blackActive = (currentPlayer == BLACK);
    wchar_t blackStr[50];
    swprintf_s(blackStr, L"黑方: %d秒", blackTime);
    settextcolor(blackActive ? RGB(255, 0, 0) : RGB(100, 100, 100));
    settextstyle(16, 0, _T("微软雅黑"));
    outtextxy(timerX, timerY + 30, blackStr);
    
    // 黑方进度条
    int blackProgress = (blackTimeLimit > 0) ? (blackTime * 100) / blackTimeLimit : 0;
    if (blackProgress > 100) blackProgress = 100;
    if (blackProgress < 0) blackProgress = 0;
    COLORREF blackColor = (blackTime <= 5) ? RGB(255, 0, 0) : 
                          (blackTime <= 10) ? RGB(255, 165, 0) : RGB(100, 200, 100);
    drawProgressBar(timerX, timerY + 52, timerWidth, 8, blackProgress, blackColor);
    
    // 白方计时器
    bool whiteActive = (currentPlayer == WHITE);
    wchar_t whiteStr[50];
    swprintf_s(whiteStr, L"白方: %d秒", whiteTime);
    settextcolor(whiteActive ? RGB(255, 0, 0) : RGB(100, 100, 100));
    outtextxy(timerX, timerY + 75, whiteStr);
    
    // 白方进度条
    int whiteProgress = (whiteTimeLimit > 0) ? (whiteTime * 100) / whiteTimeLimit : 0;
    if (whiteProgress > 100) whiteProgress = 100;
    if (whiteProgress < 0) whiteProgress = 0;
    COLORREF whiteColor = (whiteTime <= 5) ? RGB(255, 0, 0) : 
                          (whiteTime <= 10) ? RGB(255, 165, 0) : RGB(100, 200, 100);
    drawProgressBar(timerX, timerY + 97, timerWidth, 8, whiteProgress, whiteColor);
}

void Graphics::drawProgressBar(int x, int y, int width, int height, int percentage, COLORREF color) {
    // 背景
    setfillcolor(RGB(220, 220, 220));
    solidrectangle(x, y, x + width, y + height);
    
    // 前景（进度）
    int filledWidth = (width * percentage) / 100;
    if (filledWidth > 0) {
        setfillcolor(color);
        solidrectangle(x, y, x + filledWidth, y + height);
    }
    
    // 边框
    setlinecolor(RGB(150, 150, 150));
    setlinestyle(PS_SOLID, 1);
    rectangle(x, y, x + width, y + height);
}

// ========== 皮肤系统全局函数实现 ==========
// 全局皮肤状态变量
static int g_whiteSkin = 0;
static int g_blackSkin = 0;
static bool g_showSkinDialog = false;

void DrawSkinButton()
{
    // 在左下角绘制皮肤按钮
    const int bx = 10;
    const int by = WINDOW_HEIGHT - 34;  // 底部位置
    const int bw = 60;
    const int bh = 24;

    // 按钮背景
    setfillcolor(RGB(200, 200, 200));
    solidrectangle(bx, by, bx + bw, by + bh);
    setlinecolor(RGB(100, 100, 100));
    rectangle(bx, by, bx + bw, by + bh);
    
    // 按钮文字
    settextcolor(BLACK);
    settextstyle(16, 0, _T("微软雅黑"));
    outtextxy(bx + 8, by + 4, _T("皮肤"));
}

void DrawSkinDialog()
{
    if (!g_showSkinDialog) return;

    const int dx = 200;
    const int dy = 150;
    const int dw = 600;
    const int dh = 300;

    // 对话框背景
    setfillcolor(RGB(240, 240, 240));
    solidrectangle(dx, dy, dx + dw, dy + dh);
    setlinecolor(RGB(100, 100, 100));
    setlinestyle(PS_SOLID, 2);
    rectangle(dx, dy, dx + dw, dy + dh);

    // 标题
    settextcolor(BLACK);
    settextstyle(24, 0, _T("微软雅黑"));
    outtextxy(dx + dw / 2 - 48, dy + 20, _T("皮肤选择"));

    // 左右两个皮肤预览区域
    const int imgW = 120;
    const int imgH = 120;
    const int leftX = dx + 100;
    const int rightX = dx + dw - 100 - imgW;
    const int imgY = dy + 80;

    // 绘制预览框
    setfillcolor(RGB(220, 220, 220));
    solidrectangle(leftX, imgY, leftX + imgW, imgY + imgH);
    solidrectangle(rightX, imgY, rightX + imgW, rightX + imgH);

    // 标签
    settextstyle(20, 0, _T("微软雅黑"));
    outtextxy(leftX + 30, imgY + imgH + 15, _T("皮肤一"));
    outtextxy(rightX + 30, imgY + imgH + 15, _T("皮肤二"));

    // 高亮当前选中的皮肤
    if (g_whiteSkin == 0) {
        setlinecolor(RGB(0, 120, 215));
        setlinestyle(PS_SOLID, 3);
        rectangle(leftX - 3, imgY - 3, leftX + imgW + 3, imgY + imgH + 3);
    } else {
        setlinecolor(RGB(0, 120, 215));
        setlinestyle(PS_SOLID, 3);
        rectangle(rightX - 3, imgY - 3, rightX + imgW + 3, imgY + imgH + 3);
    }
}

void SetWhiteSkin(int skinIndex) { g_whiteSkin = skinIndex; }
void SetBlackSkin(int skinIndex) { g_blackSkin = skinIndex; }
int GetWhiteSkin() { return g_whiteSkin; }
int GetBlackSkin() { return g_blackSkin; }

void ToggleSkinDialog() { g_showSkinDialog = !g_showSkinDialog; }
void ShowSkinDialog(bool show) { g_showSkinDialog = show; }
bool IsSkinDialogVisible() { return g_showSkinDialog; }

// 检查鼠标点击是否在皮肤选择对话框内
bool CheckSkinDialogClick(int x, int y) {
    if (!g_showSkinDialog) return false;

    const int dx = 200;
    const int dy = 150;
    const int dw = 600;
    const int dh = 300;
    const int imgW = 120;
    const int imgH = 120;
    const int leftX = dx + 100;
    const int rightX = dx + dw - 100 - imgW;
    const int imgY = dy + 80;

    // 点击左侧皮肤（皮肤一）
    if (x >= leftX && x <= leftX + imgW && y >= imgY && y <= imgY + imgH) {
        g_whiteSkin = 0;
        g_blackSkin = 0;
        return true;
    }
    // 点击右侧皮肤（皮肤二）
    else if (x >= rightX && x <= rightX + imgW && y >= imgY && y <= imgY + imgH) {
        g_whiteSkin = 1;
        g_blackSkin = 1;
        return true;
    }
    
    return false;
}













