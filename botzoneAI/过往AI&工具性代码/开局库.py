#!/usr/bin/env python3
# 最终修正版：自动推断起始玩家，处理完整对局

GRIDSIZE = 8
OBSTACLE = 2
GRID_BLACK = 1
GRID_WHITE = -1
EMPTY = 0


def init_board_correct():
    """正确的棋盘初始化"""
    board = [[EMPTY for _ in range(GRIDSIZE)] for _ in range(GRIDSIZE)]
    pos = (GRIDSIZE - 1) // 3  # pos = 2

    # 黑方（1）位置
    board[0][pos] = GRID_BLACK  # (0,2)
    board[pos][0] = GRID_BLACK  # (2,0)
    board[GRIDSIZE - 1 - pos][0] = GRID_BLACK  # (5,0)
    board[GRIDSIZE - 1][pos] = GRID_BLACK  # (7,2)

    # 白方（-1）位置
    board[0][GRIDSIZE - 1 - pos] = GRID_WHITE  # (0,5)
    board[pos][GRIDSIZE - 1] = GRID_WHITE  # (2,7)
    board[GRIDSIZE - 1 - pos][GRIDSIZE - 1] = GRID_WHITE  # (5,7)
    board[GRIDSIZE - 1][GRIDSIZE - 1 - pos] = GRID_WHITE  # (7,5)

    return board


def board_to_fingerprint(board, player):
    """生成与C++代码一致的指纹"""
    fingerprint = f"{player}:"
    for x in range(GRIDSIZE):
        for y in range(GRIDSIZE):
            fingerprint += f"{board[x][y]},"
    return fingerprint


def proc_step_safe(board, x0, y0, x1, y1, x2, y2, color, verbose=False):
    """安全执行移动"""
    if not (0 <= x0 < GRIDSIZE and 0 <= y0 < GRIDSIZE and
            0 <= x1 < GRIDSIZE and 0 <= y1 < GRIDSIZE and
            0 <= x2 < GRIDSIZE and 0 <= y2 < GRIDSIZE):
        if verbose:
            print(f"  错误：坐标越界 ({x0},{y0})→({x1},{y1})箭({x2},{y2})")
        return False

    if board[x0][y0] != color:
        if verbose:
            print(f"  错误：位置({x0},{y0})是{board[x0][y0]}，不是{color}的棋子")
        return False

    if board[x1][y1] != EMPTY:
        if verbose:
            print(f"  错误：目标位置({x1},{y1})不是空的")
        return False

    # 箭位置可以是空的，或者就是起点（起点刚被腾空）
    if board[x2][y2] != EMPTY and not (x2 == x0 and y2 == y0):
        if verbose:
            print(f"  错误：箭位置({x2},{y2})不是空的也不是起点")
        return False

    # 执行移动
    board[x0][y0] = EMPTY
    board[x1][y1] = color
    board[x2][y2] = OBSTACLE
    return True


def infer_starting_player_from_move(move):
    """
    从第一个移动推断起始玩家

    关键：亚马逊棋白方先手，所以第一个移动应该是白方
    但如果第一个移动是黑棋，说明数据可能是"黑方视角"的记录
    """
    x0, y0, _, _, _, _ = move
    board = init_board_correct()

    piece_color = board[x0][y0]

    if piece_color == GRID_WHITE:
        print(f"第一个移动是白棋({x0},{y0}) → 推断：白方先手（正常规则）")
        return GRID_WHITE
    elif piece_color == GRID_BLACK:
        print(f"第一个移动是黑棋({x0},{y0}) → 推断：黑方先手（特殊对局或视角）")
        return GRID_BLACK
    else:
        print("无法推断起始玩家，默认白方先手")
        return GRID_WHITE


def parse_complete_game(game_data):
    """
    解析完整对局，自动处理起始玩家问题
    """
    lines = [line.strip() for line in game_data.strip().split('\n') if line.strip()]

    if not lines:
        return [], 0, GRID_WHITE

    # 第一行是turnID
    try:
        turnID = int(lines[0])
    except:
        turnID = 0

    # 解析所有移动
    moves = []
    for i in range(1, len(lines)):
        parts = list(map(int, lines[i].split()))
        if len(parts) == 6:
            moves.append(tuple(parts))

    # 检查是否有-1标记
    if moves and moves[0] == (-1, -1, -1, -1, -1, -1):
        print("检测到-1标记：白方先手但未移动")
        # 移除-1标记
        moves = moves[1:]
        # 下一个移动应该是黑方
        starting_player = GRID_BLACK
    else:
        # 从第一个移动推断
        if moves:
            starting_player = infer_starting_player_from_move(moves[0])
        else:
            starting_player = GRID_WHITE

    return moves, turnID, starting_player


def simulate_complete_game_with_flexible_start(moves, starting_player):
    """
    模拟完整对局，支持灵活的起始玩家
    """
    board = init_board_correct()
    all_decisions = []

    current_player = starting_player
    move_index = 0
    step_count = 0

    print(f"\n模拟对局：起始玩家={'白方' if starting_player == GRID_WHITE else '黑方'}")

    # 逐个执行移动
    for move in moves:
        x0, y0, x1, y1, x2, y2 = move

        # 记录决策前的状态
        fingerprint = board_to_fingerprint(board, current_player)

        # 尝试执行移动
        if proc_step_safe(board, x0, y0, x1, y1, x2, y2, current_player, verbose=True):
            print(
                f"第{step_count + 1}步: {'白方' if current_player == GRID_WHITE else '黑方'}移动 ({x0},{y0})→({x1},{y1})箭({x2},{y2})")

            # 记录这个决策
            decision = {
                'fingerprint': fingerprint,
                'move': move,
                'player': current_player,
                'step': step_count + 1,
                'turn': (step_count // 2) + 1
            }
            all_decisions.append(decision)

            step_count += 1
            move_index += 1
            current_player = -current_player  # 切换玩家
        else:
            print(f"移动失败！当前玩家: {'白方' if current_player == GRID_WHITE else '黑方'}")
            print(f"棋盘位置({x0},{y0}): {board[x0][y0]}")
            # 尝试另一种可能性：也许当前玩家判断错了？
            print("尝试另一种可能性...")

            # 尝试用另一种颜色执行
            other_player = -current_player
            if proc_step_safe(board, x0, y0, x1, y1, x2, y2, other_player, verbose=False):
                print(f"成功！应该是{other_player}玩家下棋")
                # 修正当前玩家
                current_player = other_player

                # 记录修正后的决策
                fingerprint = board_to_fingerprint(board, current_player)
                decision = {
                    'fingerprint': fingerprint,
                    'move': move,
                    'player': current_player,
                    'step': step_count + 1,
                    'turn': (step_count // 2) + 1
                }
                all_decisions.append(decision)

                step_count += 1
                move_index += 1
                current_player = -current_player  # 切换玩家
            else:
                print("两种可能性都失败，退出模拟")
                break

    print(f"\n模拟完成：成功执行{step_count}步，提取{len(all_decisions)}个决策")
    return all_decisions


def analyze_and_generate_opening_book(all_decisions):
    """分析和生成开局库"""
    if not all_decisions:
        print("没有提取到任何决策，无法生成开局库")
        return ""

    # 按玩家分类
    white_decisions = [d for d in all_decisions if d['player'] == GRID_WHITE]
    black_decisions = [d for d in all_decisions if d['player'] == GRID_BLACK]

    print(f"\n决策分析:")
    print(f"  白方决策: {len(white_decisions)}个 (步骤: {[d['step'] for d in white_decisions]})")
    print(f"  黑方决策: {len(black_decisions)}个 (步骤: {[d['step'] for d in black_decisions]})")

    # 只取前期的决策（开局阶段）
    early_phase = [d for d in all_decisions if d['step'] <= 20]

    print(f"\n开局阶段（前20步）:")
    print(f"  白方: {sum(1 for d in early_phase if d['player'] == GRID_WHITE)}个决策")
    print(f"  黑方: {sum(1 for d in early_phase if d['player'] == GRID_BLACK)}个决策")

    # 生成C++开局库代码
    code_lines = []
    code_lines.append("// 从高质量对局提取的开局库")
    code_lines.append("// 自动推断起始玩家和处理完整对局")
    code_lines.append("")

    for decision in early_phase:
        fp = decision['fingerprint']
        move = decision['move']
        player = decision['player']
        step = decision['step']

        x0, y0, x1, y1, x2, y2 = move

        # 生成代码行
        code_line = f'book["{fp}"] = {{{x0}, {y0}, {x1}, {y1}, {x2}, {y2}}};'
        code_lines.append(code_line)
        code_lines.append("")  # 空行

    # 统计去重后的局面数
    fingerprints = set([d['fingerprint'] for d in early_phase])
    print(f"\n生成开局库:")
    print(f"  原始决策数: {len(early_phase)}")
    print(f"  去重后局面数: {len(fingerprints)}")
    print(f"  代码行数: {len(code_lines)}")

    return "\n".join(code_lines)


def main():
    # 你的对局数据
    game_data = """


1
2 0 2 5 1 4


0 2 2 2 0 4
2 5 1 6 1 5
1 6 6 6 1 6
5 0 3 2 4 2
3 2 3 4 3 0
3 4 3 5 1 7
3 5 3 4 5 2
2 2 2 1 2 2
3 4 3 3 1 3
3 3 4 4 2 4
6 6 5 6 3 4
5 6 6 5 5 4
6 5 6 6 7 5
4 4 5 3 4 4
6 6 5 6 6 5
5 6 6 6 5 6
7 2 7 1 7 2
5 3 3 3 5 3
7 1 6 0 7 1
2 1 2 0 2 1
6 6 7 6 6 6
7 6 7 7 7 6
3 3 4 3 2 3





"""

    print("高质量对局分析及开局库提取")
    print("=" * 60)

    # 1. 解析对局，自动推断起始玩家
    moves, turnID, starting_player = parse_complete_game(game_data)
    print(f"对局ID: {turnID}, 移动数: {len(moves)}")
    print(f"推断起始玩家: {'白方' if starting_player == GRID_WHITE else '黑方'}")

    # 2. 模拟完整对局
    all_decisions = simulate_complete_game_with_flexible_start(moves, starting_player)

    if not all_decisions:
        print("\n错误：无法模拟对局，可能数据格式有问题")

        # 尝试另一种方法：逐个尝试起始玩家
        print("\n尝试两种起始玩家可能性:")

        # 尝试白方先手
        print("\n1. 假设白方先手:")
        board = init_board_correct()
        current_player = GRID_WHITE
        for i, move in enumerate(moves[:3]):
            x0, y0, x1, y1, x2, y2 = move
            piece_color = board[x0][y0]
            print(f"  第{i + 1}步: 移动({x0},{y0})，棋子颜色={piece_color}，当前玩家={current_player}")
            if piece_color == current_player:
                print(f"    ✓ 匹配！可以执行")
            else:
                print(f"    ✗ 不匹配！棋子是{piece_color}但玩家是{current_player}")

        # 尝试黑方先手
        print("\n2. 假设黑方先手:")
        board = init_board_correct()
        current_player = GRID_BLACK
        for i, move in enumerate(moves[:3]):
            x0, y0, x1, y1, x2, y2 = move
            piece_color = board[x0][y0]
            print(f"  第{i + 1}步: 移动({x0},{y0})，棋子颜色={piece_color}，当前玩家={current_player}")
            if piece_color == current_player:
                print(f"    ✓ 匹配！可以执行")
            else:
                print(f"    ✗ 不匹配！棋子是{piece_color}但玩家是{current_player}")

        return

    # 3. 生成开局库
    print("\n" + "=" * 60)
    print("生成开局库代码:")

    code = analyze_and_generate_opening_book(all_decisions)

    if code:
        # 保存到文件
        output_file = "robust_opening_book.cpp"
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(code)

        print(f"\n开局库已保存到: {output_file}")

        # 显示前几个条目
        print("\n前5个开局库条目:")
        lines = code.split('\n')
        for i, line in enumerate(lines[:30]):
            if line.strip():
                print(f"  {line}")

    else:
        print("无法生成开局库")


if __name__ == "__main__":
    main()