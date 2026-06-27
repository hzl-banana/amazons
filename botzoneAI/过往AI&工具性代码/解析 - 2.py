#!/usr/bin/env python3
# direct_zip_parser.py
import zipfile
import struct
import json
import os
import sys


def parse_katago_zip(zip_path):
    """
    直接解析KataGo的ZIP格式模型文件
    """
    print(f"解析ZIP文件: {zip_path}")

    if not zipfile.is_zipfile(zip_path):
        print("错误: 不是有效的ZIP文件")
        return

    with zipfile.ZipFile(zip_path, 'r') as zipf:
        # 1. 列出所有文件
        file_list = zipf.namelist()
        print(f"ZIP包含 {len(file_list)} 个文件:")

        for file in file_list:
            info = zipf.getinfo(file)
            print(f"  {file} ({info.file_size:,} 字节)")

        # 2. 检查关键文件
        key_files = ['archive/data.pkl', 'data.pkl', 'model.pkl']
        pkl_file = None

        for f in key_files:
            if f in file_list:
                pkl_file = f
                break

        if pkl_file:
            print(f"\n找到主要数据文件: {pkl_file}")

            # 提取并分析pkl文件
            extract_and_analyze_pkl(zipf, pkl_file, zip_path)
        else:
            print("\n未找到标准pkl文件，尝试其他文件...")

            # 查找最大的文件（可能是权重）
            largest_file = max(file_list, key=lambda x: zipf.getinfo(x).file_size)
            print(f"最大的文件: {largest_file}")

            analyze_binary_file(zipf, largest_file)


def extract_and_analyze_pkl(zipf, pkl_path, zip_path):
    """
    提取并分析pkl文件
    """
    print(f"提取 {pkl_path}...")

    # 创建输出目录
    output_dir = "extracted_model"
    os.makedirs(output_dir, exist_ok=True)

    # 提取文件
    zipf.extract(pkl_path, output_dir)
    full_path = os.path.join(output_dir, pkl_path)

    print(f"✓ 提取到: {full_path}")

    # 分析文件大小
    file_size = os.path.getsize(full_path)
    print(f"文件大小: {file_size:,} 字节")

    # 检查是否是pickle格式
    with open(full_path, 'rb') as f:
        first_bytes = f.read(100)

        # pickle文件的常见特征
        if first_bytes.startswith(b'\x80\x02'):  # Python 2 pickle
            print("✓ Python 2 pickle格式")
            parse_pickle_file(full_path)
        elif first_bytes.startswith(b'\x80\x03'):  # Python 3 pickle
            print("✓ Python 3 pickle格式")
            parse_pickle_file(full_path)
        elif b'OrderedDict' in first_bytes or b'collections' in first_bytes:
            print("✓ 包含OrderedDict/collections，是pickle文件")
            parse_pickle_file(full_path)
        else:
            print("⚠️  不是标准pickle格式，尝试二进制分析")
            analyze_raw_binary(full_path)


def parse_pickle_file(pkl_path):
    """
    尝试解析pickle文件
    """
    print("\n=== 尝试解析pickle文件 ===")

    # 方法1：尝试用pickle加载
    try:
        import pickle
        with open(pkl_path, 'rb') as f:
            data = pickle.load(f)
            print(f"✓ 成功加载pickle文件")
            print(f"数据类型: {type(data)}")

            if isinstance(data, dict):
                print(f"字典键: {list(data.keys())}")
                analyze_pickle_dict(data)
            else:
                print(f"数据内容前100字符: {str(data)[:100]}...")

    except Exception as e:
        print(f"pickle加载失败: {e}")

        # 方法2：手动解析pickle结构
        print("\n尝试手动解析pickle结构...")
        analyze_pickle_structure(pkl_path)


def analyze_pickle_structure(pkl_path):
    """
    手动分析pickle文件结构
    """
    with open(pkl_path, 'rb') as f:
        data = f.read()

    # 查找常见的PyTorch模型标记
    markers = [
        (b'torch._utils._rebuild_tensor_v2', 'PyTorch张量'),
        (b'FloatStorage', '浮点存储'),
        (b'LongStorage', '长整型存储'),
        (b'conv.weight', '卷积权重'),
        (b'conv.bias', '卷积偏置'),
        (b'blockstack', '残差块'),
        (b'normactconv', '归一化激活卷积'),
        (b'policy', '策略头'),
        (b'value', '价值头')
    ]

    print("在pickle文件中找到的标记:")
    for marker, description in markers:
        if marker in data:
            count = data.count(marker)
            print(f"  {description}: 出现 {count} 次")

    # 提取所有可能的变量名
    print("\n提取可能的变量名:")

    # 在pickle中查找字符串
    strings = extract_strings_from_binary(data)

    # 过滤出看起来像层名的字符串
    layer_keywords = ['conv', 'weight', 'bias', 'block', 'norm', 'policy', 'value', 'trunk']
    layer_names = []

    for s in strings:
        if any(keyword in s.lower() for keyword in layer_keywords) and len(s) > 10:
            if s not in layer_names:
                layer_names.append(s)

    for name in layer_names[:30]:  # 只显示前30个
        print(f"  {name}")

    # 保存分析结果
    result = {
        'file_size': len(data),
        'markers_found': [(desc, data.count(marker)) for marker, desc in markers if marker in data],
        'layer_names': layer_names[:50],
        'all_strings': strings[:100]
    }

    with open('pickle_analysis.json', 'w') as f:
        json.dump(result, f, indent=2)

    print(f"\n✓ 分析结果保存到: pickle_analysis.json")


def extract_strings_from_binary(data, min_length=4):
    """
    从二进制数据中提取字符串
    """
    strings = []
    current_string = []

    for byte in data:
        if 32 <= byte < 127:  # 可打印ASCII字符
            current_string.append(chr(byte))
        else:
            if len(current_string) >= min_length:
                strings.append(''.join(current_string))
            current_string = []

    # 处理最后一个字符串
    if len(current_string) >= min_length:
        strings.append(''.join(current_string))

    return strings


def analyze_pickle_dict(data):
    """
    分析pickle字典内容
    """
    print("\n=== 分析字典内容 ===")

    if 'model' in data:
        model_data = data['model']
        print(f"'model'键的类型: {type(model_data)}")

        if hasattr(model_data, 'items'):
            print(f"模型包含 {len(model_data)} 个参数")

            # 显示前20个参数
            print("\n前20个参数:")
            for i, (key, value) in enumerate(list(model_data.items())[:20]):
                if hasattr(value, 'shape'):
                    print(f"  [{i:2d}] {key:60s} shape: {value.shape}")
                else:
                    print(f"  [{i:2d}] {key:60s} type: {type(value)}")

    # 查找网络配置
    config_keys = ['config', 'args', 'params', 'training_config']
    for key in config_keys:
        if key in data:
            print(f"\n找到配置键 '{key}':")
            config = data[key]
            if isinstance(config, dict):
                for k, v in config.items():
                    if not isinstance(v, (dict, list)) and len(str(v)) < 100:
                        print(f"  {k}: {v}")

    # 提取权重信息
    print("\n=== 权重统计 ===")
    extract_weight_info(data)


def extract_weight_info(data):
    """
    提取权重信息
    """
    # 查找所有张量
    tensors = []

    def find_tensors(obj, path=""):
        if hasattr(obj, 'shape'):  # 可能是张量
            tensors.append((path, obj))
        elif isinstance(obj, dict):
            for k, v in obj.items():
                find_tensors(v, f"{path}.{k}" if path else k)
        elif isinstance(obj, (list, tuple)):
            for i, v in enumerate(obj):
                find_tensors(v, f"{path}[{i}]")

    find_tensors(data)

    print(f"找到 {len(tensors)} 个张量")

    if tensors:
        # 统计信息
        total_params = 0
        layer_types = {}

        for path, tensor in tensors[:30]:  # 前30个
            if hasattr(tensor, 'shape'):
                params = 1
                for dim in tensor.shape:
                    params *= dim
                total_params += params

                # 分类
                for layer_type in ['conv', 'bn', 'linear', 'bias']:
                    if layer_type in path.lower():
                        layer_types[layer_type] = layer_types.get(layer_type, 0) + 1
                        break

        print(f"总参数: {total_params:,}")
        print(f"层类型分布: {layer_types}")

        # 保存权重映射
        weight_map = []
        for path, tensor in tensors[:50]:
            if hasattr(tensor, 'shape'):
                weight_map.append({
                    'name': path,
                    'shape': list(tensor.shape),
                    'size': int(np.prod(tensor.shape)) if hasattr(tensor, 'shape') else 0
                })

        with open('weight_mapping.json', 'w') as f:
            json.dump(weight_map, f, indent=2)

        print(f"✓ 权重映射保存到: weight_mapping.json")


def analyze_binary_file(zipf, file_path):
    """
    分析二进制文件
    """
    print(f"\n分析二进制文件: {file_path}")

    with zipf.open(file_path) as f:
        data = f.read(1000)  # 读前1000字节

        print("前100字节 (十六进制):")
        for i in range(0, min(100, len(data)), 16):
            line = data[i:i + 16]
            hex_str = ' '.join(f'{b:02x}' for b in line)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in line)
            print(f"{i:04x}: {hex_str:<48} {ascii_str}")

        # 检查是否是PyTorch存储格式
        if b'FloatStorage' in data or b'LongStorage' in data:
            print("✓ 包含PyTorch存储标记")

        # 检查是否有权重数据
        if len(data) > 100:
            # 尝试解释为float32
            try:
                floats = struct.unpack('<{}f'.format(len(data) // 4), data[:len(data) // 4 * 4])
                print(f"可以作为 {len(floats)} 个float32读取")

                # 统计值范围
                floats_array = np.array(floats[:100])
                print(f"前100个值的范围: [{floats_array.min():.3f}, {floats_array.max():.3f}]")
                print(f"平均值: {floats_array.mean():.3f}, 标准差: {floats_array.std():.3f}")
            except:
                print("不能作为float32数组解析")


def analyze_raw_binary(file_path):
    """
    分析原始二进制文件
    """
    print(f"\n分析原始二进制文件: {file_path}")

    with open(file_path, 'rb') as f:
        # 读取整个文件（如果不太大）
        file_size = os.path.getsize(file_path)
        print(f"文件大小: {file_size:,} 字节")

        if file_size < 10000000:  # 小于10MB
            data = f.read()

            # 查找PyTorch特定的标记
            pt_markers = [
                (b'torch._utils._rebuild_tensor', 'PyTorch张量重建'),
                (b'storage', '存储对象'),
                (b'collections.OrderedDict', '有序字典'),
                (b'conv', '卷积层'),
                (b'weight', '权重'),
                (b'bias', '偏置')
            ]

            print("找到的标记:")
            for marker, desc in pt_markers:
                if marker in data:
                    count = data.count(marker)
                    print(f"  {desc}: {count} 次")

            # 提取可能的层名
            print("\n可能的层名:")
            # 查找格式为 "xxx.weight" 或 "xxx.bias" 的字符串
            import re

            # 简单的正则匹配
            pattern = rb'([a-zA-Z0-9_\.]+\.(?:weight|bias|running_mean|running_var))'
            matches = re.findall(pattern, data)

            for match in matches[:30]:
                try:
                    name = match.decode('utf-8', errors='ignore')
                    print(f"  {name}")
                except:
                    pass


# 如果pickle不可用，定义一个简单的numpy替代
try:
    import numpy as np
except ImportError:
    class SimpleNP:
        @staticmethod
        def array(arr):
            return arr

        @staticmethod
        def prod(arr):
            result = 1
            for x in arr:
                result *= x
            return result


    np = SimpleNP()

if __name__ == "__main__":
    # 直接指定文件路径
    zip_path = "/Users/hezhenlin/Desktop/model.ckpt"

    if not os.path.exists(zip_path):
        print(f"错误: 文件不存在 {zip_path}")
        sys.exit(1)

    print("=" * 70)
    print("KataGo模型直接解析器 (无依赖版本)")
    print("=" * 70)

    parse_katago_zip(zip_path)

    print("\n" + "=" * 70)
    print("✅ 解析完成！")
    print("\n基于分析结果，建议的Botzone方案:")
    print("1. 使用提取的权重信息编写C++神经网络")
    print("2. 或者，直接在Botzone上使用libtorch加载原始.pt文件")
    print("\n需要我:")
    print("A. 编写基于提取权重的纯C++神经网络代码")
    print("B. 编写libtorch C++加载代码")
    print("\n请告诉我你的选择 (A 或 B)")