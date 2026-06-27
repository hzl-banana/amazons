#!/usr/bin/env python3
"""
Amazon Neural Network Trainer - Enhanced Version
优化网络结构和训练策略
"""

import numpy as np
import random
import time
import os
import multiprocessing as mp
from collections import deque
import pickle
import signal
import traceback
import queue
import threading
import re
import glob
from typing import List, Tuple, Dict, Optional
import json


# ==================== 信号处理 ====================

def init_worker():
    signal.signal(signal.SIGINT, signal.SIG_IGN)


# ==================== 基础类定义（保持不变） ====================

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


class Action:
    def __init__(self, from_pos=-1, to_pos=-1, arrow=-1):
        self.from_pos = from_pos
        self.to_pos = to_pos
        self.arrow = arrow

    def __eq__(self, other):
        return (self.from_pos == other.from_pos and
                self.to_pos == other.to_pos and
                self.arrow == other.arrow)

    def __hash__(self):
        return hash((self.from_pos, self.to_pos, self.arrow))


EMPTY_ACTION = Action()


# ==================== 改进的棋盘特征提取 ====================

class EnhancedBoard(Board):
    def __init__(self, copy_from=None):
        super().__init__(copy_from)

    def get_enhanced_tuples(self, player, rounds):
        """获取增强的特征向量"""
        features = [0] * 65

        if player == 0:
            my_pieces = self.one_board
            opp_pieces = self.two_board
        else:
            my_pieces = self.two_board
            opp_pieces = self.one_board

        # 1. 棋子位置特征（29维）
        for i in range(64):
            p = 1 << i
            if my_pieces & p:
                # 计算移动性
                mobility = min(15, bin(self._generate_reachable_squares(p)).count('1'))
                features[i] = 1 + mobility
            elif opp_pieces & p:
                mobility = min(15, bin(self._generate_reachable_squares(p)).count('1'))
                features[i] = 16 + mobility
            elif self.arrows & p:
                features[i] = 31
            else:
                # 空地特征
                features[i] = 32

        # 2. 回合桶特征
        if rounds < 10:
            features[64] = 0
        elif rounds < 20:
            features[64] = 1
        elif rounds < 30:
            features[64] = 2
        else:
            features[64] = 3

        return features

    def get_spatial_features(self):
        """获取空间特征（卷积网络用）"""
        # 简化的空间特征：将棋盘转换为4个通道
        # 通道0：己方棋子，通道1：对方棋子，通道2：箭，通道3：空格
        features = np.zeros((4, 8, 8), dtype=np.float32)

        for y in range(8):
            for x in range(8):
                idx = y * 8 + x
                bit = 1 << idx
                if self.one_board & bit:
                    features[0, y, x] = 1.0
                elif self.two_board & bit:
                    features[1, y, x] = 1.0
                elif self.arrows & bit:
                    features[2, y, x] = 1.0
                else:
                    features[3, y, x] = 1.0
        return features


# ==================== 改进的神经网络 ====================

class EnhancedNeuralAgent:
    def __init__(self, input_dim=65, hidden_dim=256, output_dim=1):
        """
        改进的神经网络结构
        - 3层MLP
        - 批量归一化
        - 残差连接
        """
        self.input_dim = input_dim
        self.hidden_dim = hidden_dim

        # 第一层
        self.w1 = np.random.randn(input_dim, hidden_dim) * np.sqrt(2.0 / input_dim)
        self.b1 = np.zeros(hidden_dim)

        # 批量归一化参数
        self.bn1_gamma = np.ones(hidden_dim)
        self.bn1_beta = np.zeros(hidden_dim)
        self.bn1_mean = np.zeros(hidden_dim)
        self.bn1_var = np.ones(hidden_dim)

        # 第二层
        self.w2 = np.random.randn(hidden_dim, hidden_dim) * np.sqrt(2.0 / hidden_dim)
        self.b2 = np.zeros(hidden_dim)

        # 第三层（输出）
        self.w3 = np.random.randn(hidden_dim, output_dim) * np.sqrt(2.0 / hidden_dim)
        self.b3 = np.zeros(output_dim)

        # 动量优化器参数
        self.m_w1 = np.zeros_like(self.w1)
        self.m_b1 = np.zeros_like(self.b1)
        self.m_w2 = np.zeros_like(self.w2)
        self.m_b2 = np.zeros_like(self.b2)
        self.m_w3 = np.zeros_like(self.w3)
        self.m_b3 = np.zeros_like(self.b3)

        # 自适应学习率
        self.v_w1 = np.zeros_like(self.w1)
        self.v_b1 = np.zeros_like(self.b1)
        self.v_w2 = np.zeros_like(self.w2)
        self.v_b2 = np.zeros_like(self.b2)
        self.v_w3 = np.zeros_like(self.w3)
        self.v_b3 = np.zeros_like(self.b3)

        # Dropout
        self.dropout_rate = 0.1
        self.training = True

    def batch_norm_forward(self, x, gamma, beta, mean, var, eps=1e-5):
        """批量归一化前向传播"""
        if self.training:
            batch_mean = np.mean(x, axis=0)
            batch_var = np.var(x, axis=0)

            # 更新运行均值/方差
            momentum = 0.9
            self.bn1_mean = momentum * self.bn1_mean + (1 - momentum) * batch_mean
            self.bn1_var = momentum * self.bn1_var + (1 - momentum) * batch_var

            x_norm = (x - batch_mean) / np.sqrt(batch_var + eps)
        else:
            x_norm = (x - mean) / np.sqrt(var + eps)

        return gamma * x_norm + beta, x_norm, batch_mean, batch_var

    def relu(self, x):
        return np.maximum(0, x)

    def leaky_relu(self, x, alpha=0.01):
        return np.where(x > 0, x, alpha * x)

    def tanh(self, x):
        return np.tanh(x)

    def dropout(self, x, rate):
        if not self.training or rate == 0:
            return x
        mask = np.random.binomial(1, 1 - rate, size=x.shape) / (1 - rate)
        return x * mask

    def forward(self, x):
        """前向传播"""
        # 第一层
        z1 = x @ self.w1 + self.b1
        bn1, self.bn1_cache, self.bn1_batch_mean, self.bn1_batch_var = self.batch_norm_forward(
            z1, self.bn1_gamma, self.bn1_beta, self.bn1_mean, self.bn1_var
        )
        a1 = self.leaky_relu(bn1)
        a1 = self.dropout(a1, self.dropout_rate)

        # 第二层（带残差连接）
        z2 = a1 @ self.w2 + self.b2
        a2 = self.leaky_relu(z2)
        a2_res = a1 + a2 * 0.3  # 残差连接
        a2_res = self.dropout(a2_res, self.dropout_rate)

        # 输出层
        z3 = a2_res @ self.w3 + self.b3
        output = self.tanh(z3)

        return output, (z1, a1, z2, a2, a2_res, z3, output)

    def backward(self, x, y_pred, y_true, cache, learning_rate):
        """反向传播"""
        z1, a1, z2, a2, a2_res, z3, output = cache

        # 输出层梯度
        dloss = 2.0 * (y_pred - y_true) * (1.0 - y_pred ** 2)  # tanh导数

        # 第三层梯度
        d_w3 = a2_res.T @ dloss / len(x)
        d_b3 = np.mean(dloss, axis=0)

        # 第二层梯度（考虑残差）
        d_a2_res = dloss @ self.w3.T
        d_a2 = d_a2_res * 0.3 * (a2 > 0)  # Leaky ReLU导数
        d_z2 = d_a2
        d_w2 = a1.T @ d_z2 / len(x)
        d_b2 = np.mean(d_z2, axis=0)

        # 第一层梯度（考虑BN）
        d_a1 = d_z2 @ self.w2.T + d_a2_res * (a1 > 0)  # 来自残差连接
        d_bn1 = d_a1

        # 批量归一化反向传播
        d_gamma = np.sum(self.bn1_cache * d_bn1, axis=0)
        d_beta = np.sum(d_bn1, axis=0)

        # Adam优化器更新
        beta1, beta2 = 0.9, 0.999
        eps = 1e-8

        # 更新第三层
        self.m_w3 = beta1 * self.m_w3 + (1 - beta1) * d_w3
        self.v_w3 = beta2 * self.v_w3 + (1 - beta2) * (d_w3 ** 2)
        m_w3_hat = self.m_w3 / (1 - beta1)
        v_w3_hat = self.v_w3 / (1 - beta2)
        self.w3 -= learning_rate * m_w3_hat / (np.sqrt(v_w3_hat) + eps)

        self.m_b3 = beta1 * self.m_b3 + (1 - beta1) * d_b3
        self.v_b3 = beta2 * self.v_b3 + (1 - beta2) * (d_b3 ** 2)
        m_b3_hat = self.m_b3 / (1 - beta1)
        v_b3_hat = self.v_b3 / (1 - beta2)
        self.b3 -= learning_rate * m_b3_hat / (np.sqrt(v_b3_hat) + eps)

        # 更新第二层
        self.m_w2 = beta1 * self.m_w2 + (1 - beta1) * d_w2
        self.v_w2 = beta2 * self.v_w2 + (1 - beta2) * (d_w2 ** 2)
        m_w2_hat = self.m_w2 / (1 - beta1)
        v_w2_hat = self.v_w2 / (1 - beta2)
        self.w2 -= learning_rate * m_w2_hat / (np.sqrt(v_w2_hat) + eps)

        self.m_b2 = beta1 * self.m_b2 + (1 - beta1) * d_b2
        self.v_b2 = beta2 * self.v_b2 + (1 - beta2) * (d_b2 ** 2)
        m_b2_hat = self.m_b2 / (1 - beta1)
        v_b2_hat = self.v_b2 / (1 - beta2)
        self.b2 -= learning_rate * m_b2_hat / (np.sqrt(v_b2_hat) + eps)

        # 更新第一层
        d_z1 = d_bn1 * self.bn1_gamma / np.sqrt(self.bn1_batch_var + 1e-5)
        d_w1 = x.T @ d_z1 / len(x)
        d_b1 = np.mean(d_z1, axis=0)

        self.m_w1 = beta1 * self.m_w1 + (1 - beta1) * d_w1
        self.v_w1 = beta2 * self.v_w1 + (1 - beta2) * (d_w1 ** 2)
        m_w1_hat = self.m_w1 / (1 - beta1)
        v_w1_hat = self.v_w1 / (1 - beta2)
        self.w1 -= learning_rate * m_w1_hat / (np.sqrt(v_w1_hat) + eps)

        self.m_b1 = beta1 * self.m_b1 + (1 - beta1) * d_b1
        self.v_b1 = beta2 * self.v_b1 + (1 - beta2) * (d_b1 ** 2)
        m_b1_hat = self.m_b1 / (1 - beta1)
        v_b1_hat = self.v_b1 / (1 - beta2)
        self.b1 -= learning_rate * m_b1_hat / (np.sqrt(v_b1_hat) + eps)

        # 更新BN参数
        self.bn1_gamma -= learning_rate * d_gamma / len(x)
        self.bn1_beta -= learning_rate * d_beta / len(x)

    def get_score(self, features):
        """计算状态价值"""
        self.training = False
        x = np.array(features).reshape(1, -1)
        output, _ = self.forward(x)
        return output[0, 0]

    def train_batch(self, batch_data, learning_rate):
        """训练一个批次"""
        self.training = True
        states, targets = zip(*batch_data)
        states = np.array(states)
        targets = np.array(targets).reshape(-1, 1)

        # 前向传播
        predictions, cache = self.forward(states)

        # 计算损失
        loss = np.mean((predictions - targets) ** 2)

        # 反向传播
        self.backward(states, predictions, targets, cache, learning_rate)

        return loss

    def save(self, filename):
        """保存模型"""
        data = {
            'w1': self.w1,
            'b1': self.b1,
            'w2': self.w2,
            'b2': self.b2,
            'w3': self.w3,
            'b3': self.b3,
            'bn1_gamma': self.bn1_gamma,
            'bn1_beta': self.bn1_beta,
            'bn1_mean': self.bn1_mean,
            'bn1_var': self.bn1_var
        }
        with open(filename, 'wb') as f:
            pickle.dump(data, f, protocol=pickle.HIGHEST_PROTOCOL)

    def load(self, filename):
        """加载模型"""
        try:
            with open(filename, 'rb') as f:
                data = pickle.load(f)
                for key in data:
                    if hasattr(self, key):
                        setattr(self, key, data[key])
        except Exception as e:
            print(f"Warning: Could not load model: {e}")


# ==================== 增强的MCTS算法 ====================

class MCTSNode:
    """MCTS节点"""

    def __init__(self, game, parent=None, action=None):
        self.game = game
        self.parent = parent
        self.action = action
        self.children = []
        self.visits = 0
        self.value = 0.0
        self.untried_actions = game.get_available_moves()

    def is_fully_expanded(self):
        return len(self.untried_actions) == 0

    def is_terminal(self):
        return self.game.is_over()

    def best_child(self, c_puct=1.0):
        """选择最佳子节点"""
        if not self.children:
            return None

        # 使用UCB公式
        scores = []
        for child in self.children:
            if child.visits == 0:
                ucb = 1e6  # 未访问过的节点优先探索
            else:
                exploit = child.value / child.visits
                explore = c_puct * np.sqrt(np.log(self.visits) / child.visits)
                ucb = exploit + explore
            scores.append(ucb)

        return self.children[np.argmax(scores)]


def run_enhanced_mcts(game, agent, iterations=200, c_puct=1.0):
    """增强的MCTS搜索"""
    root = MCTSNode(game)

    for _ in range(iterations):
        node = root

        # 选择阶段
        while node.is_fully_expanded() and not node.is_terminal():
            node = node.best_child(c_puct)

        # 扩展阶段
        if not node.is_terminal() and not node.is_fully_expanded():
            action = node.untried_actions.pop()
            child_game = game.copy()
            child_game.make_move(action)
            child_node = MCTSNode(child_game, node, action)
            node.children.append(child_node)
            node = child_node

        # 模拟阶段
        sim_game = node.game.copy()
        while not sim_game.is_over():
            moves = sim_game.get_available_moves()
            if not moves:
                break

            # 使用神经网络指导模拟
            if len(moves) <= 10:
                # 对少数走法，使用神经网络评估
                best_move = None
                best_score = -float('inf')
                for move in moves:
                    sim_game.make_move(move)
                    score = agent.get_score(sim_game.get_tuples())
                    sim_game.undo_move()

                    if score > best_score:
                        best_score = score
                        best_move = move

                if best_move:
                    sim_game.make_move(best_move)
                else:
                    sim_game.make_move(random.choice(moves))
            else:
                sim_game.make_move(random.choice(moves))

        # 回溯阶段
        value = 1.0 if sim_game.get_winner() == node.game.current_player else -1.0

        while node is not None:
            node.visits += 1
            node.value += value
            value = -value  # 对手视角相反
            node = node.parent

    # 选择最佳动作
    if not root.children:
        return [], []

    # 根据访问次数计算概率
    visits = np.array([child.visits for child in root.children])
    probs = visits / np.sum(visits)

    moves = [child.action for child in root.children]
    return moves, probs


# ==================== 训练配置 ====================

class EnhancedTrainingConfig:
    def __init__(self):
        # 训练参数
        self.num_episodes = 2000
        self.mcts_iterations = 200
        self.replay_buffer_size = 50000
        self.batch_size = 256
        self.save_interval = 100

        # MCTS参数
        self.c_puct = 1.5
        self.dirichlet_alpha = 0.3
        self.dirichlet_epsilon = 0.25

        # 学习率
        self.initial_learning_rate = 0.001
        self.learning_rate_decay = 0.9995

        # 网络参数
        self.hidden_dim = 512

        # 路径
        self.model_dir = "./enhanced_models"
        self.log_file = "./training_log_enhanced.csv"
        self.config_file = "./training_config.json"

        # 探索策略
        self.exploration_factor = 0.8
        self.temperature = 1.0
        self.temperature_decay = 0.995

    def save(self):
        """保存配置"""
        os.makedirs(os.path.dirname(self.config_file), exist_ok=True)
        with open(self.config_file, 'w') as f:
            json.dump(self.__dict__, f, indent=2)

    def load(self):
        """加载配置"""
        try:
            with open(self.config_file, 'r') as f:
                data = json.load(f)
                for key, value in data.items():
                    if hasattr(self, key):
                        setattr(self, key, value)
        except:
            pass


# ==================== 改进的自对弈工作进程 ====================

def enhanced_self_play_worker(worker_id, agent_state, config_dict,
                              shared_queue, stop_event, debug=False):
    """增强的自对弈工作进程"""
    try:
        # 创建本地代理
        agent = EnhancedNeuralAgent()
        # 简单的状态加载（实际需要适配）

        config = TrainingConfig()
        for key, value in config_dict.items():
            setattr(config, key, value)

        games_played = 0

        while not stop_event.is_set():
            game = Game()
            game_examples = []
            move_count = 0
            max_moves = 60

            temperature = config.temperature

            while not game.is_over() and move_count < max_moves and not stop_event.is_set():
                # 运行MCTS
                moves, probs = run_enhanced_mcts(
                    game, agent,
                    iterations=config.mcts_iterations,
                    c_puct=config.c_puct
                )

                if not moves:
                    break

                # 添加Dirichlet噪声
                if move_count < 10:
                    noise = np.random.dirichlet([config.dirichlet_alpha] * len(probs))
                    probs = [(1 - config.dirichlet_epsilon) * p +
                             config.dirichlet_epsilon * n
                             for p, n in zip(probs, noise)]

                # 温度采样
                if temperature > 0.1:
                    log_probs = np.log(probs + 1e-10) / temperature
                    exp_probs = np.exp(log_probs - np.max(log_probs))
                    probs = exp_probs / np.sum(exp_probs)

                # 选择动作
                selected_idx = np.random.choice(len(probs), p=probs)
                selected_action = moves[selected_idx]

                # 记录训练样本
                game_examples.append((
                    game.get_tuples(),  # 状态特征
                    selected_action,  # 选择的动作
                    probs,  # MCTS概率分布
                    game.current_player  # 当前玩家
                ))

                # 执行动作
                game.make_move(selected_action)
                move_count += 1

                # 温度衰减
                temperature *= config.temperature_decay

            # 分配奖励
            if game.is_over():
                winner = game.get_winner()
                final_reward = 1.0 if winner == 0 else -1.0
            else:
                # 使用神经网络评估最终局面
                final_reward = agent.get_score(game.get_tuples())

            # 准备训练数据
            training_data = []
            for i, (state, action, probs, player) in enumerate(game_examples):
                # 折扣奖励
                discount = 0.99
                reward = final_reward * (discount ** (len(game_examples) - i - 1))

                # 对于动作选择，使用MCTS的概率作为监督信号
                training_data.append((state, reward, probs))

            # 发送数据
            try:
                shared_queue.put_nowait((worker_id, training_data))
                games_played += 1

                if debug and games_played % 10 == 0:
                    print(f"Worker {worker_id}: Played {games_played} games")
            except queue.Full:
                if debug and games_played % 100 == 0:
                    print(f"Worker {worker_id}: Queue full")

            time.sleep(0.001)

    except Exception as e:
        print(f"Worker {worker_id} error: {e}")
        traceback.print_exc()


# ==================== 改进的训练器 ====================

class EnhancedParallelTrainer:
    def __init__(self, config):
        self.config = config
        self.agent = EnhancedNeuralAgent()

        # 进程管理
        self.shared_queue = mp.Queue(maxsize=50000)
        self.stop_event = mp.Event()
        self.processes = []

        # 训练数据
        self.replay_buffer = deque(maxlen=config.replay_buffer_size)
        self.buffer_lock = threading.Lock()

        # 统计
        self.total_games = 0
        self.total_moves = 0
        self.episode = 0
        self.best_loss = float('inf')

        # 优化
        self.learning_rate = config.initial_learning_rate

        # 日志
        os.makedirs(config.model_dir, exist_ok=True)
        self.log_file = open(config.log_file, 'w')
        self.log_file.write("episode,games,moves,loss,value_loss,policy_loss,buffer_size,lr\n")

        # 加载现有模型
        latest_model = self._find_latest_model()
        if latest_model:
            print(f"Loading model: {latest_model}")
            self.agent.load(latest_model)

        print(f"Enhanced trainer initialized with {config.hidden_dim} hidden units")

    def start_workers(self, num_workers=8):
        """启动工作进程"""
        print(f"Starting {num_workers} workers...")

        config_dict = {
            'mcts_iterations': self.config.mcts_iterations,
            'c_puct': self.config.c_puct,
            'dirichlet_alpha': self.config.dirichlet_alpha,
            'dirichlet_epsilon': self.config.dirichlet_epsilon,
            'temperature': self.config.temperature,
            'temperature_decay': self.config.temperature_decay
        }

        # 注意：这里需要传递agent状态，简化处理
        dummy_state = {}

        for i in range(num_workers):
            process = mp.Process(
                target=enhanced_self_play_worker,
                args=(i, dummy_state, config_dict, self.shared_queue, self.stop_event, True),
                daemon=True
            )
            process.start()
            self.processes.append(process)
            time.sleep(0.1)

    def collect_training_data(self, timeout=1.0):
        """收集训练数据"""
        collected = 0
        start_time = time.time()

        while time.time() - start_time < timeout:
            try:
                worker_id, data = self.shared_queue.get_nowait()
            except queue.Empty:
                break

            with self.buffer_lock:
                for state, reward, probs in data:
                    self.replay_buffer.append((state, reward))
                    collected += 1

            self.total_games += 1
            self.total_moves += len(data)

        return collected

    def train_step(self):
        """执行一步训练"""
        with self.buffer_lock:
            if len(self.replay_buffer) < self.config.batch_size * 2:
                return 0.0, 0.0

            # 采样批次
            batch_size = min(self.config.batch_size, len(self.replay_buffer))
            indices = np.random.choice(len(self.replay_buffer), batch_size, replace=False)
            batch = [self.replay_buffer[i] for i in indices]

        # 准备数据
        states, targets = zip(*batch)
        states = np.array(states)
        targets = np.array(targets).reshape(-1, 1)

        # 训练
        loss = self.agent.train_batch(list(zip(states, targets)), self.learning_rate)

        return loss, len(batch)

    def train(self):
        """主训练循环"""
        print("Starting enhanced training...")
        start_time = time.time()

        # 启动工作进程
        self.start_workers(min(8, mp.cpu_count() // 2))

        try:
            # 等待初始数据
            print("Collecting initial data...")
            while len(self.replay_buffer) < self.config.batch_size * 3:
                self.collect_training_data(timeout=0.5)
                print(f"\rBuffer: {len(self.replay_buffer)}/{self.config.batch_size * 3}", end="")
                time.sleep(0.1)
            print()

            # 主训练循环
            for episode in range(self.config.num_episodes):
                self.episode = episode

                # 收集数据
                collected = self.collect_training_data(timeout=0.2)

                # 训练
                if len(self.replay_buffer) >= self.config.batch_size:
                    loss, batch_size = self.train_step()

                    # 学习率衰减
                    self.learning_rate *= self.config.learning_rate_decay

                    # 记录日志
                    if episode % 10 == 0:
                        elapsed = time.time() - start_time
                        games_per_sec = self.total_games / max(1, elapsed)

                        print(f"[{episode:4d}] "
                              f"Games: {self.total_games:5d} "
                              f"Loss: {loss:.6f} "
                              f"Buffer: {len(self.replay_buffer):5d} "
                              f"GPS: {games_per_sec:.1f} "
                              f"LR: {self.learning_rate:.6f}")

                        self.log_file.write(
                            f"{episode},{self.total_games},{self.total_moves},"
                            f"{loss:.6f},{loss:.6f},0.0,"
                            f"{len(self.replay_buffer)},{self.learning_rate:.6f}\n"
                        )
                        self.log_file.flush()

                    # 保存模型
                    if episode % self.config.save_interval == 0 and episode > 0:
                        self.save_model(episode)
                        if loss < self.best_loss:
                            self.best_loss = loss
                            self.save_model(episode, best=True)

                # 显示进度
                if episode % 2 == 0:
                    self.display_progress(episode, start_time)

                time.sleep(0.01)

        except KeyboardInterrupt:
            print("\nTraining interrupted")
        finally:
            self.stop_workers()
            self.save_model(self.episode, final=True)
            self.log_file.close()
            print("Training completed")

    def display_progress(self, episode, start_time):
        """显示训练进度"""
        elapsed = time.time() - start_time
        progress = (episode + 1) / self.config.num_episodes * 100
        eta = elapsed / (episode + 1) * (self.config.num_episodes - episode - 1)

        print(f"\rProgress: {progress:5.1f}% | "
              f"ETA: {eta // 60:02.0f}:{eta % 60:02.0f} | "
              f"Games: {self.total_games} | "
              f"Buffer: {len(self.replay_buffer)}", end="")

    def save_model(self, episode, best=False, final=False):
        """保存模型"""
        if best:
            filename = os.path.join(self.config.model_dir, "best_model.pkl")
        elif final:
            filename = os.path.join(self.config.model_dir, "final_model.pkl")
        else:
            filename = os.path.join(self.config.model_dir, f"model_{episode:04d}.pkl")

        self.agent.save(filename)
        print(f"Model saved: {filename}")

    def stop_workers(self):
        """停止工作进程"""
        self.stop_event.set()
        for process in self.processes:
            process.join(timeout=1.0)
            if process.is_alive():
                process.terminate()
        self.processes.clear()


# ==================== 评估函数 ====================

def evaluate_agent(agent, num_games=100):
    """评估智能体性能"""
    print("Evaluating agent...")
    wins = 0
    losses = 0

    for game_idx in range(num_games):
        game = Game()
        players = [0, 1]
        random.shuffle(players)

        current_player = 0
        move_count = 0

        while not game.is_over() and move_count < 100:
            moves = game.get_available_moves()
            if not moves:
                break

            if game.current_player == players[0]:
                # 智能体走棋
                best_move = None
                best_score = -float('inf')

                for move in moves[:20]:  # 限制搜索数量
                    game_copy = game.copy()
                    game_copy.make_move(move)
                    score = agent.get_score(game_copy.get_tuples())

                    if score > best_score:
                        best_score = score
                        best_move = move

                if best_move:
                    game.make_move(best_move)
                else:
                    game.make_move(random.choice(moves))
            else:
                # 随机走棋
                game.make_move(random.choice(moves))

            move_count += 1

        if game.is_over():
            winner = game.get_winner()
            if winner == players[0]:
                wins += 1
            else:
                losses += 1

    win_rate = wins / num_games
    print(f"Evaluation: {wins} wins, {losses} losses, win rate: {win_rate:.2%}")
    return win_rate


# ==================== 主程序 ====================

def main():
    print("=== Enhanced Amazons Neural Network Trainer ===")
    print(f"CPU Cores: {mp.cpu_count()}")

    try:
        mp.set_start_method('fork')
    except:
        pass

    # 加载配置
    config = EnhancedTrainingConfig()
    config.load()

    # 创建训练器
    trainer = EnhancedParallelTrainer(config)

    # 询问确认
    response = input("Start training? (y/n): ")
    if response.lower() != 'y':
        print("Exiting...")
        return

    # 保存配置
    config.save()

    # 开始训练
    trainer.train()

    # 最终评估
    print("\nPerforming final evaluation...")
    evaluate_agent(trainer.agent)


if __name__ == "__main__":
    main()