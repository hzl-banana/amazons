#!/usr/bin/env python3
"""
Amazon Neural Network Trainer - Python Parallel Version
修复工作进程数据生成问题
"""

import numpy as np
import random
import time
import os
import sys
import multiprocessing as mp
from collections import deque
import pickle
import signal
import traceback
import queue
import threading


# ==================== 信号处理 ====================

def init_worker():
    """初始化工作进程，忽略中断信号"""
    signal.signal(signal.SIGINT, signal.SIG_IGN)


# ==================== 基础类定义 ====================

class Random:
    def __init__(self, seed=None):
        if seed is None:
            seed = int(time.time() * 1000) ^ os.getpid()
        self.seed = seed & 0xFFFFFFFFFFFFFFFF
        if self.seed == 0:
            self.seed = 1

    def next_int(self, n=0):
        self.seed = (self.seed * 6364136223846793005 + 1) & 0xFFFFFFFFFFFFFFFF
        if n > 0:
            return (self.seed >> 32) % n
        return (self.seed >> 32) & 0x7FFFFFFF

    def next_float(self, mi=0.0, ma=1.0):
        return mi + (self.next_int() / 0x7FFFFFFF) * (ma - mi)

    def __getstate__(self):
        """支持pickle"""
        return {'seed': self.seed}

    def __setstate__(self, state):
        """支持unpickle"""
        self.seed = state['seed']


class Action:
    def __init__(self, from_pos=-1, to_pos=-1, arrow=-1):
        self.from_pos = from_pos  # 0-63
        self.to_pos = to_pos  # 0-63
        self.arrow = arrow  # 0-63

    def __eq__(self, other):
        return (self.from_pos == other.from_pos and
                self.to_pos == other.to_pos and
                self.arrow == other.arrow)

    def __str__(self):
        squares = [f"{chr(97 + (i % 8))}{8 - (i // 8)}" for i in range(64)]
        return f"{squares[self.from_pos]}{squares[self.to_pos]}{squares[self.arrow]}"

    def __hash__(self):
        return hash((self.from_pos, self.to_pos, self.arrow))

    def __getstate__(self):
        return {'from_pos': self.from_pos, 'to_pos': self.to_pos, 'arrow': self.arrow}

    def __setstate__(self, state):
        self.from_pos = state['from_pos']
        self.to_pos = state['to_pos']
        self.arrow = state['arrow']


EMPTY_ACTION = Action()


# ==================== 棋盘类 ====================

class Board:
    LEFT_MASK = 0x7f7f7f7f7f7f7f7f
    RIGHT_MASK = 0xfefefefefefefefe

    def __init__(self, copy_from=None):
        if copy_from is None:
            # 初始棋盘配置
            opening = "..b..b..........b......b................w......w..........w..w.."
            self.one_board = 0  # 白棋
            self.two_board = 0  # 黑棋
            self.arrows = 0  # 箭

            for i in range(8):
                line = opening[8 * i:8 * (i + 1)]
                for j in range(8):
                    c = line[j]
                    idx = 8 * (7 - i) + (7 - j)
                    if c == 'w':
                        self.one_board |= 1 << idx
                    elif c == 'b':
                        self.two_board |= 1 << idx
                    elif c == '-':
                        self.arrows |= 1 << idx
        else:
            self.one_board = copy_from.one_board
            self.two_board = copy_from.two_board
            self.arrows = copy_from.arrows

    def copy(self):
        return Board(copy_from=self)

    def get_available_actions(self, player):
        """获取所有合法动作"""
        moves = []
        empty = ~(self.one_board | self.two_board | self.arrows)
        board = self.one_board if player == 0 else self.two_board

        dirs = [1, -1, 8, -8, 9, 7, -9, -7]
        masks = [
            self.RIGHT_MASK, self.LEFT_MASK,
            0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
            self.RIGHT_MASK, self.LEFT_MASK,
            self.LEFT_MASK, self.RIGHT_MASK
        ]

        # 查找所有己方棋子
        pieces = board
        while pieces:
            i = (pieces & -pieces).bit_length() - 1
            pieces ^= 1 << i

            for d in range(8):
                j = i + dirs[d]
                while 0 <= j < 64:
                    bit = 1 << j
                    if not (empty & bit & masks[d]):
                        break

                    # 找到所有可能的箭位置
                    arrow_moves = self._get_arrow_moves(player, i, j)
                    for arrow in arrow_moves:
                        moves.append(Action(i, j, arrow))
                    j += dirs[d]

        return moves

    def _get_arrow_moves(self, player, from_pos, to_pos):
        """获取从to位置发射的所有可能的箭位置，增加步数上限以防极端长循环"""
        # 保存原始状态
        original_one = self.one_board
        original_two = self.two_board
        original_arrows = self.arrows

        # 临时移动棋子
        if player == 0:
            self.one_board ^= (1 << from_pos) | (1 << to_pos)
        else:
            self.two_board ^= (1 << from_pos) | (1 << to_pos)

        empty = ~(self.one_board | self.two_board | self.arrows)
        dirs = [1, -1, 8, -8, 9, 7, -9, -7]
        masks = [
            self.RIGHT_MASK, self.LEFT_MASK,
            0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
            self.RIGHT_MASK, self.LEFT_MASK,
            self.LEFT_MASK, self.RIGHT_MASK
        ]

        arrow_moves = []
        MAX_STEPS = 8  # 最多扫描8格，避免极端长循环
        for d in range(8):
            j = to_pos
            steps = 0
            while steps < MAX_STEPS:
                j += dirs[d]
                steps += 1
                if not (0 <= j < 64):
                    break
                bit = 1 << j
                if not (empty & bit & masks[d]):
                    break
                arrow_moves.append(j)

        # 恢复棋盘
        self.one_board = original_one
        self.two_board = original_two
        self.arrows = original_arrows

        return arrow_moves

    def make_move(self, player, action):
        """执行移动"""
        if player == 0:
            self.one_board ^= (1 << action.from_pos) | (1 << action.to_pos)
        else:
            self.two_board ^= (1 << action.from_pos) | (1 << action.to_pos)
        self.arrows ^= 1 << action.arrow

    def is_over(self, player):
        """检查游戏是否结束"""
        empty = ~(self.one_board | self.two_board | self.arrows)
        board = self.one_board if player == 0 else self.two_board

        for dir_idx in range(8):
            shifted = self._shift(board, dir_idx)
            if shifted & empty:
                return False
        return True

    def _shift(self, disks, dir_idx):
        """位棋盘移位"""
        dirs = [1, -1, 8, -8, 9, 7, -9, -7]
        masks = [
            self.RIGHT_MASK, self.LEFT_MASK,
            0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
            self.RIGHT_MASK, self.LEFT_MASK,
            self.LEFT_MASK, self.RIGHT_MASK
        ]

        dir_val = dirs[dir_idx]
        mask = masks[dir_idx]

        if dir_val > 0:
            return (disks << dir_val) & mask
        else:
            return (disks >> -dir_val) & mask

    def get_tuples(self, player, rounds):
        """获取状态特征向量"""
        tuples = [0] * 65

        if player == 0:
            one, two = self.one_board, self.two_board
        else:
            one, two = self.two_board, self.one_board

        # 生成可达位置
        a = self._generate_reachable_squares(one)
        a2 = self._generate_reachable_squares(a | one)
        b = self._generate_reachable_squares(two)
        b2 = self._generate_reachable_squares(b | two)

        for i in range(64):
            p = 1 << i
            if one & p:
                reachable = self._generate_reachable_squares(p)
                mobility = min(9, bin(reachable).count('1'))
                tuples[i] = 1 + mobility
            elif two & p:
                reachable = self._generate_reachable_squares(p)
                mobility = min(9, bin(reachable).count('1'))
                tuples[i] = 10 + mobility
            elif self.arrows & p:
                tuples[i] = 20
            else:
                A = (a & p) != 0
                B = (b & p) != 0
                A2 = (a2 & p) != 0
                B2 = (b2 & p) != 0

                if A and B:
                    tuples[i] = 21
                elif A:
                    tuples[i] = 22 if B2 else 23
                elif B:
                    tuples[i] = 24 if A2 else 25
                elif A2 and B2:
                    tuples[i] = 26
                elif A2:
                    tuples[i] = 27
                elif B2:
                    tuples[i] = 28

        # 回合桶
        if rounds < 14:
            tuples[64] = 0
        elif rounds < 28:
            tuples[64] = 1
        else:
            tuples[64] = 2

        return tuples

    def _generate_reachable_squares(self, squares):
        """生成给定棋子的可达位置"""
        legal_moves = 0
        empty = ~(self.one_board | self.two_board | self.arrows)

        for dir_idx in range(8):
            x = self._shift(squares, dir_idx) & empty
            for _ in range(6):
                x |= self._shift(x, dir_idx) & empty
            legal_moves |= x

        return legal_moves


# ==================== 游戏类 ====================

class Game:
    ONE = 0
    TWO = 1

    def __init__(self, copy_from=None):
        if copy_from is None:
            self.board = Board()
            self.current_player = self.ONE
            self.rounds = 0
            self.history = []
        else:
            self.board = copy_from.board.copy()
            self.current_player = copy_from.current_player
            self.rounds = copy_from.rounds
            self.history = copy_from.history.copy()

    def copy(self):
        return Game(copy_from=self)

    def get_available_moves(self):
        moves = self.board.get_available_actions(self.current_player)
        return moves

    def make_move(self, action):
        self.history.append(action)
        self.board.make_move(self.current_player, action)
        self.current_player ^= 1
        self.rounds += 1

    def undo_move(self):
        if not self.history:
            return
        action = self.history.pop()
        self.current_player ^= 1
        self.rounds -= 1
        self.board.make_move(self.current_player, action)

    def is_over(self):
        return self.board.is_over(self.current_player)

    def get_winner(self):
        return self.current_player ^ 1

    def get_tuples(self):
        return self.board.get_tuples(self.current_player, self.rounds)


# ==================== 神经网络代理 ====================

class NeuralAgent:
    def __init__(self, tuples=65, units=29, hidden=256, buckets=3):
        self.tuples = tuples
        self.units = units
        self.hidden = hidden
        self.buckets = buckets

        # 权重初始化
        self.hidden_weights = np.random.randn(units, tuples, hidden) * np.sqrt(2.0 / (tuples + hidden))
        self.output_weights = np.random.randn(buckets, hidden) * np.sqrt(2.0 / (hidden + 1))

        # 自适应学习率参数
        self.a_hidden_weights = np.zeros_like(self.hidden_weights)
        self.n_hidden_weights = np.zeros_like(self.hidden_weights)
        self.a_output_weights = np.zeros_like(self.output_weights)
        self.n_output_weights = np.zeros_like(self.output_weights)

    @staticmethod
    def relu2(x):
        return np.where(x < 0, 0, np.where(x >= 1, x, x * x))

    @staticmethod
    def drelu2(x):
        return np.where(x < 0, 0, np.where(x >= 1, 1.0, 2.0 * np.sqrt(x)))

    @staticmethod
    def fast_tanh(x):
        return np.tanh(x)

    @staticmethod
    def dtanh(x):
        return 1.0 - x * x

    def get_score_deep(self, ts):
        """前向传播计算得分"""
        scores = np.zeros(self.hidden)

        # 累加隐藏层输入
        for j in range(self.tuples):
            scores += self.hidden_weights[ts[j], j, :]

        # ReLU激活
        scores = self.relu2(scores)

        # 输出层
        bucket = ts[64]
        output = np.dot(self.output_weights[bucket], scores)

        return self.fast_tanh(output)

    def learn_deep_rl(self, ts, target, learning_rate=0.001):
        """强化学习更新"""
        # 前向传播
        scores = np.zeros(self.hidden)
        for j in range(self.tuples):
            scores += self.hidden_weights[ts[j], j, :]
        scores = self.relu2(scores)

        bucket = ts[64]
        op = np.dot(self.output_weights[bucket], scores)
        op = self.fast_tanh(op)

        # 误差计算
        e = target - op
        dop = e * self.dtanh(op)

        # 隐藏层梯度
        dh = dop * self.output_weights[bucket] * self.drelu2(scores)

        # 自适应学习率更新输出层
        for i in range(self.hidden):
            idx = (bucket, i)
            if self.a_output_weights[idx] == 0:
                a = 1.0
            else:
                a = abs(self.n_output_weights[idx]) / self.a_output_weights[idx]

            delta = learning_rate * 0.2 * a * scores[i] * dop
            self.output_weights[idx] += delta
            self.a_output_weights[idx] += abs(scores[i] * dop)
            self.n_output_weights[idx] += scores[i] * dop

        # 自适应学习率更新隐藏层
        for j in range(self.tuples):
            idx = ts[j]
            for i in range(self.hidden):
                h_idx = (idx, j, i)
                if self.a_hidden_weights[h_idx] == 0:
                    a = 1.0
                else:
                    a = abs(self.n_hidden_weights[h_idx]) / self.a_hidden_weights[h_idx]

                delta = learning_rate * 0.2 * a * dh[i]
                self.hidden_weights[h_idx] += delta
                self.a_hidden_weights[h_idx] += abs(dh[i])
                self.n_hidden_weights[h_idx] += dh[i]

    def save(self, filename):
        """保存模型"""
        data = {
            'hidden_weights': self.hidden_weights,
            'output_weights': self.output_weights,
            'a_hidden_weights': self.a_hidden_weights,
            'n_hidden_weights': self.n_hidden_weights,
            'a_output_weights': self.a_output_weights,
            'n_output_weights': self.n_output_weights
        }

        with open(filename, 'wb') as f:
            pickle.dump(data, f, protocol=pickle.HIGHEST_PROTOCOL)

    def load(self, filename):
        """加载模型"""
        try:
            with open(filename, 'rb') as f:
                data = pickle.load(f)
                self.hidden_weights = data['hidden_weights']
                self.output_weights = data['output_weights']
                self.a_hidden_weights = data.get('a_hidden_weights', np.zeros_like(self.hidden_weights))
                self.n_hidden_weights = data.get('n_hidden_weights', np.zeros_like(self.hidden_weights))
                self.a_output_weights = data.get('a_output_weights', np.zeros_like(self.output_weights))
                self.n_output_weights = data.get('n_output_weights', np.zeros_like(self.output_weights))
        except Exception as e:
            print(f"Warning: Could not load model from {filename}: {e}")
            print("Using random weights")

    def get_state_dict(self):
        """获取模型状态字典"""
        return {
            'hidden_weights': self.hidden_weights.copy(),
            'output_weights': self.output_weights.copy(),
            'a_hidden_weights': self.a_hidden_weights.copy(),
            'n_hidden_weights': self.n_hidden_weights.copy(),
            'a_output_weights': self.a_output_weights.copy(),
            'n_output_weights': self.n_output_weights.copy()
        }

    def load_state_dict(self, state_dict):
        """加载模型状态字典"""
        self.hidden_weights = state_dict['hidden_weights']
        self.output_weights = state_dict['output_weights']
        self.a_hidden_weights = state_dict['a_hidden_weights']
        self.n_hidden_weights = state_dict['n_hidden_weights']
        self.a_output_weights = state_dict['a_output_weights']
        self.n_output_weights = state_dict['n_output_weights']


# ==================== 训练数据类 ====================

class TrainingExample:
    def __init__(self, state_tuples, action_probs, selected_action, value_target, player):
        self.state_tuples = state_tuples
        self.action_probs = action_probs
        self.selected_action = selected_action
        self.value_target = value_target
        self.player = player

    def __getstate__(self):
        return {
            'state_tuples': self.state_tuples,
            'action_probs': self.action_probs,
            'selected_action': self.selected_action,
            'value_target': self.value_target,
            'player': self.player
        }

    def __setstate__(self, state):
        self.state_tuples = state['state_tuples']
        self.action_probs = state['action_probs']
        self.selected_action = state['selected_action']
        self.value_target = state['value_target']
        self.player = state['player']


# ==================== MCTS算法（简化版本） ====================

def run_mcts_simple(game, agent, iterations=50):
    """简化的MCTS搜索，确保能快速生成数据"""
    # 获取所有合法移动
    moves = game.get_available_moves()

    if not moves:
        # 没有合法移动，游戏结束
        return None, None

    # 如果移动很少，直接返回均匀分布
    if len(moves) <= 5 or iterations <= 10:
        # 使用神经网络评估每个移动
        scores = []
        for move in moves:
            # 执行移动
            game.make_move(move)
            if game.is_over():
                # 如果移动后游戏结束，这是一个好移动
                scores.append(1.0)
            else:
                # 使用神经网络评估
                score = agent.get_score_deep(game.get_tuples())
                scores.append(score)
            game.undo_move()

        # 转换为概率
        if scores:
            # 归一化
            min_score = min(scores)
            max_score = max(scores)
            if max_score > min_score:
                normalized = [(s - min_score) / (max_score - min_score) for s in scores]
            else:
                normalized = [1.0 / len(scores)] * len(scores)

            # softmax
            exp_scores = np.exp(normalized)
            probs = exp_scores / np.sum(exp_scores)
        else:
            probs = [1.0 / len(moves)] * len(moves)

        return moves, probs

    # 简单的随机模拟
    visits = np.ones(len(moves))
    values = np.zeros(len(moves))
    SIM_STEPS = 3  # 缩短随机模拟步数，减少计算量

    for _ in range(min(iterations, 100)):
        # 随机选择一个移动
        idx = random.randint(0, len(moves) - 1)
        move = moves[idx]

        # 执行移动
        game.make_move(move)

        # 评估局面
        if game.is_over():
            winner = game.get_winner()
            value = 1.0 if winner == game.current_player else -1.0
        else:
            # 随机模拟几步
            sim_game = game.copy()
            for _ in range(SIM_STEPS):  # 只模拟少量步数
                sim_moves = sim_game.get_available_moves()
                if not sim_moves:
                    break
                sim_move = random.choice(sim_moves)
                sim_game.make_move(sim_move)
                if sim_game.is_over():
                    break

            # 评估终局
            if sim_game.is_over():
                winner = sim_game.get_winner()
                value = 1.0 if winner == game.current_player else -1.0
            else:
                value = agent.get_score_deep(sim_game.get_tuples())

        # 更新统计
        visits[idx] += 1
        values[idx] += value

        # 撤销移动
        game.undo_move()

    # 计算概率（基于访问次数）
    probs = visits / np.sum(visits)

    return moves, probs


# ==================== 自对弈工作进程（简化版本） ====================

def self_play_worker_simple(worker_id, agent_state, config_dict, shared_queue, stop_event, debug=False):
    """简化的自对弈工作进程"""
    try:
        if debug:
            print(f"Worker {worker_id} started (PID: {os.getpid()})")

        # 创建本地代理
        agent = NeuralAgent()
        agent.load_state_dict(agent_state)

        # 配置参数
        mcts_iterations = config_dict.get('mcts_iterations', 20)  # 下发20
        dirichlet_alpha = config_dict.get('dirichlet_alpha', 0.3)
        dirichlet_epsilon = config_dict.get('dirichlet_epsilon', 0.25)

        games_played = 0
        consecutive_full = 0

        while not stop_event.is_set():
            # 开始新游戏
            game = Game()
            game_examples = []
            move_count = 0
            max_moves = 40  # 减少最大步数

            while not game.is_over() and move_count < max_moves and not stop_event.is_set():
                # 运行简化的MCTS
                moves, probs = run_mcts_simple(game, agent, mcts_iterations)

                # 检查是否有合法移动
                if moves is None or not moves:
                    break

                # 添加Dirichlet噪声（前5步）
                if move_count < 5:
                    noise = np.random.dirichlet([dirichlet_alpha] * len(probs))
                    probs = [(1 - dirichlet_epsilon) * p +
                             dirichlet_epsilon * n
                             for p, n in zip(probs, noise)]

                # 选择动作
                selected_idx = np.random.choice(len(probs), p=probs)
                selected_action = moves[selected_idx]

                # 记录训练样本
                game_examples.append(TrainingExample(
                    game.get_tuples(), probs, selected_action, 0.0, game.current_player
                ))

                # 执行动作
                game.make_move(selected_action)
                move_count += 1

            # 确定赢家
            if game.is_over():
                winner = game.get_winner()
            else:
                # 使用网络评估终局
                final_score = agent.get_score_deep(game.get_tuples())
                winner = game.current_player ^ 1 if final_score > 0 else game.current_player

            # 为样本分配值
            for example in game_examples:
                example.value_target = 1.0 if winner == example.player else -1.0

            # 发送训练样本到主进程
            if game_examples:
                try:
                    shared_queue.put_nowait((worker_id, game_examples, len(game_examples)))
                    games_played += 1
                    consecutive_full = 0
                    if debug and games_played % 10 == 0:
                        print(f"Worker {worker_id}: Played {games_played} games")
                except queue.Full:
                    consecutive_full += 1
                    if debug and consecutive_full % 10 == 0:
                        print(f"Worker {worker_id}: queue full (x{consecutive_full}), dropping batch")

            # 根据队列压力调整休眠，避免过快产出
            if consecutive_full > 20:
                time.sleep(0.05)
            else:
                time.sleep(0.002 if consecutive_full == 0 else 0.01)

    except (KeyboardInterrupt, SystemExit):
        if debug:
            print(f"Worker {worker_id} received stop signal, exiting.")
    except Exception as e:
        print(f"Worker {worker_id} error: {e}")
        traceback.print_exc()


# ==================== 并行训练器（改进版本） ====================

class ParallelTrainer:
    def __init__(self, config):
        self.config = config
        self.agent = NeuralAgent()

        # 进程间通信
        self.shared_queue = mp.Queue(maxsize=20000)  # 增大队列容量
        self.stop_event = mp.Event()

        # 训练数据
        self.replay_buffer = deque(maxlen=config.replay_buffer_size)
        self.buffer_lock = threading.Lock()

        # 统计
        self.total_games = 0
        self.total_moves = 0
        self.episode = 0

        # 工作进程
        self.processes = []
        self.num_workers = min(9, mp.cpu_count())  # 降低并发 worker 数

        # 调试标志
        self.debug = True

        # 创建目录
        os.makedirs(config.model_dir, exist_ok=True)

        # 日志
        self.log_file = open(config.log_file, 'w')
        self.log_file.write("episode,games_played,moves_played,loss_value,buffer_size,learning_rate\n")

        # 加载现有模型
        latest_model = self._find_latest_model()
        if latest_model:
            print(f"Loading model: {latest_model}")
            self.agent.load(latest_model)
        else:
            print("Starting with random weights")

        config.print_config()
        print(f"Using {self.num_workers} workers")

    def _find_latest_model(self):
        """查找最新的模型文件"""
        import glob
        model_files = glob.glob(os.path.join(self.config.model_dir, "model_*.pkl"))
        if not model_files:
            return None

        import re

        def extract_number(f):
            match = re.search(r'model_(\d+).pkl', f)
            return int(match.group(1)) if match else -1

        return max(model_files, key=extract_number)

    def start_workers(self):
        """启动工作进程"""
        print(f"Starting {self.num_workers} parallel workers...")

        # 准备配置
        config_dict = {
            'mcts_iterations': 300,  # 进一步减少迭代次数
            'dirichlet_alpha': self.config.dirichlet_alpha,
            'dirichlet_epsilon': self.config.dirichlet_epsilon
        }

        agent_state = self.agent.get_state_dict()

        for i in range(self.num_workers):
            process = mp.Process(
                target=self_play_worker_simple,
                args=(i, agent_state, config_dict, self.shared_queue, self.stop_event, self.debug),
                daemon=True
            )
            process.start()
            self.processes.append(process)
            time.sleep(0.1)  # 稍微延迟启动，避免竞争

        print(f"All {self.num_workers} workers started.")

        # 等待工作进程启动
        time.sleep(1.0)

    def stop_workers(self):
        """停止工作进程"""
        print("Stopping workers...")
        self.stop_event.set()

        for process in self.processes:
            process.join(timeout=2.0)
            if process.is_alive():
                process.terminate()

        self.processes.clear()
        self.stop_event.clear()
        print("All workers stopped.")

    def collect_data(self, timeout=0.5):
        """收集来自工作进程的数据；避免长时间阻塞"""
        collected = 0
        start_time = time.time()

        while time.time() - start_time < timeout:
            try:
                worker_id, examples, move_count = self.shared_queue.get_nowait()
            except queue.Empty:
                break

            with self.buffer_lock:
                for ex in examples:
                    self.replay_buffer.append(ex)
                    if len(self.replay_buffer) > self.config.replay_buffer_size:
                        self.replay_buffer.popleft()

            self.total_games += 1
            self.total_moves += move_count
            collected += len(examples)

            if self.debug and self.total_games % 10 == 0:
                print(f"Collected data from worker {worker_id}: {len(examples)} examples")

        return collected

    def train(self):
        """主训练循环"""
        print("Starting parallel training...")
        start_time = time.time()

        try:
            # 启动工作进程
            self.start_workers()

            # 主训练循环
            last_save = 0
            last_sync = 0

            # 等待初始数据收集
            print("Waiting for initial data collection...")
            initial_wait_start = time.time()
            while (len(self.replay_buffer) < self.config.batch_size * 2 and
                   time.time() - initial_wait_start < 30.0):  # 最多等待30秒
                self.collect_data(timeout=0.5)
                elapsed = time.time() - start_time
                print(f"\rWaiting for data... Buffer: {len(self.replay_buffer)}/{self.config.batch_size * 2} "
                      f"Time: {elapsed:.1f}s", end="", flush=True)
                time.sleep(0.1)

            print(f"\nInitial data collected: {len(self.replay_buffer)} samples")

            for self.episode in range(self.config.num_episodes):
                # 先收一次数据
                self.collect_data(timeout=0.3)

                # 如果有足够数据，进行训练
                if len(self.replay_buffer) >= self.config.batch_size:
                    loss = self._training_step()
                    elapsed = time.time() - start_time

                    # 训练后再收一次，尽快清空队列
                    self.collect_data(timeout=0.3)

                    # 记录进度
                    if self.episode % 5 == 0:
                        self._log_progress(self.episode, loss, elapsed)

                    # 学习率衰减
                    self.config.learning_rate *= self.config.learning_rate_decay
                else:
                    # 数据不足，跳过训练
                    if self.episode % 10 == 0:
                        print(
                            f"\n[Episode {self.episode}] Not enough data: {len(self.replay_buffer)}/{self.config.batch_size}")

                # 定期同步工作进程的模型（改为每100个episode）
                if self.episode - last_sync >= 100 and self.episode > 0:
                    print(f"\n[Episode {self.episode}] Synchronizing worker models...")
                    self.stop_workers()
                    time.sleep(1.0)

                    # 重新启动工作进程
                    self.start_workers()
                    last_sync = self.episode

                # 定期保存（改为每50个episode）
                if self.episode - last_save >= 50 and self.episode > 0:
                    self._save_model(self.episode)
                    last_save = self.episode

                # 显示进度
                if self.episode % 2 == 0:
                    self._display_progress(self.episode, start_time)

                # 短暂休息
                time.sleep(0.01)

        except KeyboardInterrupt:
            print("\n\nTraining interrupted by user")
        except Exception as e:
            print(f"\n\nTraining error: {e}")
            traceback.print_exc()
        finally:
            # 清理
            self.stop_workers()
            self._save_model(self.episode)
            self.log_file.close()
            print("\nTraining completed.")

    def _training_step(self):
        """执行一步训练"""
        with self.buffer_lock:
            if len(self.replay_buffer) < self.config.batch_size:
                return 0.0

            # 采样批次
            batch_size = min(self.config.batch_size, len(self.replay_buffer))
            batch_indices = np.random.choice(len(self.replay_buffer), batch_size, replace=False)
            batch = [self.replay_buffer[i] for i in batch_indices]

        total_loss = 0.0
        for example in batch:
            # 前向传播
            predicted = self.agent.get_score_deep(example.state_tuples)

            # 计算损失
            loss = (predicted - example.value_target) ** 2
            total_loss += loss

            # 更新网络
            self.agent.learn_deep_rl(example.state_tuples, example.value_target,
                                     self.config.learning_rate)

        return total_loss / len(batch)

    def _log_progress(self, episode, loss, elapsed):
        """记录训练进度"""
        games_per_sec = self.total_games / max(1.0, elapsed)
        moves_per_sec = self.total_moves / max(1.0, elapsed)

        print(f"\n[Episode {episode}] Games: {self.total_games} "
              f"Moves: {self.total_moves} Loss: {loss:.4f} "
              f"Buffer: {len(self.replay_buffer)} "
              f"GPS: {games_per_sec:.1f} MPS: {moves_per_sec:.1f} "
              f"LR: {self.config.learning_rate:.6f}")

        self.log_file.write(f"{episode},{self.total_games},{self.total_moves},"
                            f"{loss:.6f},{len(self.replay_buffer)},"
                            f"{self.config.learning_rate:.6f}\n")
        self.log_file.flush()

    def _display_progress(self, episode, start_time):
        """显示训练进度"""
        elapsed = time.time() - start_time
        progress = (episode + 1) / self.config.num_episodes * 100
        games_per_sec = self.total_games / max(1.0, elapsed)

        print(f"\rProgress: {episode + 1}/{self.config.num_episodes} ({progress:.1f}%) | "
              f"Games: {self.total_games} | GPS: {games_per_sec:.1f} | "
              f"Buffer: {len(self.replay_buffer)} | "
              f"Workers: {len(self.processes)}", end="", flush=True)

    def _save_model(self, episode):
        """保存模型"""
        filename = os.path.join(self.config.model_dir, f"model_{episode}.pkl")
        self.agent.save(filename)

        # 保存C++格式
        self._save_cpp_format(episode)

        print(f"\nModel saved: {filename}")

    def _save_cpp_format(self, episode):
        """保存为C++格式"""
        try:
            # 隐藏层权重
            hidden_file = os.path.join(self.config.model_dir, f"hidden_{episode}.bin")
            with open(hidden_file, 'wb') as f:
                data = self.agent.hidden_weights.astype(np.float32)
                f.write(data.tobytes())

            # 输出层权重
            output_file = os.path.join(self.config.model_dir, f"output_{episode}.bin")
            with open(output_file, 'wb') as f:
                data = self.agent.output_weights.astype(np.float32)
                f.write(data.tobytes())

            print(f"C++ weights saved: {hidden_file}, {output_file}")
        except Exception as e:
            print(f"Warning: Could not save C++ format: {e}")


# ==================== 配置类 ====================

class TrainingConfig:
    def __init__(self):
        # 训练参数
        self.num_episodes = 1000  # 减少总训练轮数
        self.mcts_iterations = 80
        self.replay_buffer_size = 10000  # 减小缓冲区
        self.batch_size = 64  # 减小批次大小
        self.save_interval = 50

        # MCTS参数
        self.dirichlet_alpha = 0.3
        self.dirichlet_epsilon = 0.25

        # 学习率
        self.learning_rate = 0.001
        self.learning_rate_decay = 0.9999

        # 路径
        self.model_dir = "./trained_models"
        self.log_file = "./training_log.csv"

    def print_config(self):
        """打印配置信息"""
        print("=== Parallel Training Configuration ===")
        print(f"Episodes: {self.num_episodes}")
        print(f"MCTS Iterations: {self.mcts_iterations}")
        print(f"Batch Size: {self.batch_size}")
        print(f"Learning Rate: {self.learning_rate}")
        print(f"Save Interval: {self.save_interval}")
        print(f"Model Directory: {self.model_dir}")
        print(f"Replay Buffer Size: {self.replay_buffer_size}")
        print("======================================")


# ==================== 主程序 ====================

def main():
    print("=== Amazons Neural Network Trainer (Parallel Python) ===")
    print(f"CPU Cores Available: {mp.cpu_count()}")

    # 设置进程启动方法
    try:
        mp.set_start_method('spawn')
    except RuntimeError:
        pass

    # 配置训练
    config = TrainingConfig()

    # 创建并运行训练器
    trainer = ParallelTrainer(config)
    trainer.train()


if __name__ == "__main__":
    main()