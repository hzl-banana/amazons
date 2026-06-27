#!/usr/bin/env python3
# 神经网络解析.py
import struct
import os
import json
import sys


def analyze_ckpt_directly(file_path):
    """直接分析.ckpt二进制文件"""
    print(f"分析文件: {file_path}")
    file_size = os.path.getsize(file_path)
    print(f"文件大小: {file_size:,} 字节 ({file_size / 1024 / 1024:.2f} MB)")

    with open(file_path, 'rb') as f:
        # 1. 读取文件头
        header = f.read(1024)

        print("=== 文件头分析 ===")

        # 检查常见的机器学习格式标记
        if b'TensorFlow' in header:
            print("✓ 包含'TensorFlow'标记")
        if b'checkpoint' in header.lower():
            print("✓ 包含'checkpoint'标记")
        if b'PK\x03\x04' in header[:4]:
            print("✓ ZIP文件格式 (可能是PyTorch .pt文件)")
        if b'\x80\x02' in header[:2]:
            print("✓ Python pickle格式")
        if b'KATG' in header[:4]:
            print("✓ KataGo专用格式")

        # 显示前256字节
        print("\n前256字节 (十六进制):")
        for i in range(0, min(256, len(header)), 16):
            line = header[i:i + 16]
            hex_part = ' '.join(f'{b:02x}' for b in line)
            ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in line)
            print(f"{i:04x}: {hex_part:<48} {ascii_part}")

        # 2. 查找可能的变量名
        print("\n=== 查找网络层名称 ===")

        f.seek(0)
        all_data = f.read(min(file_size, 10000000))  # 最多读10MB

        # KataGo可能的层名关键词
        katago_keywords = [
            b'conv', b'Conv', b'CONV',
            b'input_conv', b'input',
            b'residual', b'res',
            b'block', b'trunk',
            b'policy', b'value',
            b'dense', b'fc',
            b'kernel', b'bias',
            b'weight', b'gamma', b'beta',
            b'mean', b'variance',
            b'moving_mean', b'moving_variance'
        ]

        found_strings = []
        for keyword in katago_keywords:
            pos = 0
            while True:
                pos = all_data.find(keyword, pos)
                if pos == -1:
                    break

                # 提取周围的文本
                start = max(0, pos - 30)
                end = min(len(all_data), pos + 50)

                try:
                    # 找到完整的字符串
                    # 向前找开始
                    str_start = pos
                    while str_start > start and all_data[str_start - 1] > 31 and all_data[str_start - 1] < 127:
                        str_start -= 1

                    # 向后找结束
                    str_end = pos + len(keyword)
                    while str_end < end and all_data[str_end] > 31 and all_data[str_end] < 127:
                        str_end += 1

                    text = all_data[str_start:str_end].decode('utf-8', errors='ignore')
                    if text not in found_strings and len(text) > 3:
                        found_strings.append(text)

                except:
                    pass

                pos += len(keyword)

        # 显示找到的字符串
        if found_strings:
            print("找到的文本字符串:")
            for text in sorted(set(found_strings))[:30]:  # 只显示前30个
                print(f"  '{text}'")
        else:
            print("未找到明显的文本字符串")

        # 3. 分析是否为TensorFlow checkpoint
        print("\n=== 检查TensorFlow checkpoint特征 ===")

        # TensorFlow checkpoint通常有这些特征
        tf_markers = [
            b'.data-', b'.index', b'.meta',
            b'tensor_name', b'checkpoint',
            b'DT_FLOAT', b'DT_HALF'
        ]

        for marker in tf_markers:
            if marker in all_data:
                print(f"✓ 包含TF标记: {marker}")

        # 4. 分析数据格式
        print("\n=== 数据格式分析 ===")

        if file_size % 4 == 0:
            print(f"文件大小是4的倍数，可能是float32数组")
            num_floats = file_size // 4
            print(f"  可能包含 {num_floats:,} 个float32值")

            # 检查前几个值
            f.seek(0)
            sample_values = []
            for i in range(min(10, num_floats)):
                try:
                    val = struct.unpack('<f', f.read(4))[0]
                    sample_values.append(val)
                except:
                    break

            if sample_values:
                print(f"  前{len(sample_values)}个float32值:")
                for i, val in enumerate(sample_values):
                    print(f"    [{i}] {val:12.6f} (0x{struct.pack('<f', val).hex()})")

        # 5. 检查是否为KataGo的旧格式
        print("\n=== 检查KataGo特定格式 ===")

        # KataGo早期版本可能使用自定义二进制格式
        # 检查是否有明显的网络结构参数
        if file_size > 100:
            # 读取前几个可能的结构参数
            f.seek(0)
            possible_ints = []
            for i in range(min(20, file_size // 4)):
                try:
                    val = struct.unpack('<I', f.read(4))[0]
                    possible_ints.append(val)
                except:
                    break

            # 可能的网络参数值
            common_params = [8, 18, 22, 256, 384, 512]
            found_params = [val for val in possible_ints if val in common_params]

            if found_params:
                print(f"发现可能的网络参数值: {found_params}")

        # 6. 保存分析结果
        result = {
            'file_path': file_path,
            'file_size': file_size,
            'file_size_mb': file_size / 1024 / 1024,
            'possible_strings': sorted(set(found_strings)),
            'is_4byte_aligned': file_size % 4 == 0,
            'sample_floats': sample_values if 'sample_values' in locals() else []
        }

        output_file = 'ckpt_analysis_result.json'
        with open(output_file, 'w', encoding='utf-8') as f_out:
            json.dump(result, f_out, indent=2, ensure_ascii=False)

        print(f"\n✓ 详细分析已保存到: {output_file}")

        return result


def guess_file_type(file_path, file_size):
    """根据文件特征猜测类型"""
    print("\n=== 文件类型猜测 ===")

    with open(file_path, 'rb') as f:
        first_1k = f.read(1024)

    # 常见格式判断
    if first_1k.startswith(b'PK\x03\x04'):
        return "ZIP压缩文件 (可能是PyTorch .pt或TensorFlow SavedModel)"
    elif first_1k.startswith(b'\x80\x02'):
        return "Python pickle文件"
    elif first_1k.startswith(b'KATG'):
        return "KataGo专用二进制格式"
    elif b'tensorflow' in first_1k.lower():
        return "TensorFlow checkpoint或SavedModel"
    elif file_size < 1000000:  # 小于1MB
        return "可能是索引文件或配置文件"
    elif file_size % 4 == 0:
        # 检查值范围判断是否为权重
        with open(file_path, 'rb') as f:
            try:
                # 采样检查
                sample_vals = []
                for i in range(10):
                    f.seek(i * 1000 * 4)
                    val = struct.unpack('<f', f.read(4))[0]
                    sample_vals.append(val)

                avg_abs = sum(abs(v) for v in sample_vals) / len(sample_vals)
                if avg_abs < 1.0:
                    return "可能是神经网络权重文件 (float32)"
                else:
                    return "二进制数据文件"
            except:
                return "二进制数据文件"
    else:
        return "未知二进制格式"


if __name__ == "__main__":
    # 直接指定文件路径
    file_path = "/Users/hezhenlin/Desktop/model.ckpt"

    if not os.path.exists(file_path):
        print(f"错误: 文件不存在 {file_path}")
        # 尝试查找其他可能的位置
        print("在桌面搜索相关文件...")
        desktop_path = "/Users/hezhenlin/Desktop"
        for f in os.listdir(desktop_path):
            if 'model' in f.lower() or 'ckpt' in f.lower() or 'bin' in f.lower():
                print(f"  找到: {f}")
        sys.exit(1)

    print("=" * 70)
    print("KataGo神经网络文件分析器")
    print("=" * 70)

    # 分析文件
    result = analyze_ckpt_directly(file_path)

    # 猜测文件类型
    file_type = guess_file_type(file_path, result['file_size'])
    print(f"\n推测的文件类型: {file_type}")

    # 给出建议
    print("\n" + "=" * 70)
    print("下一步建议:")

    if result['file_size'] > 100000000:  # 大于100MB
        print("1. 这是大型模型文件，很可能是完整的神经网络权重")
        print("2. 需要进一步解析网络结构")
    elif 'possible_strings' in result and result['possible_strings']:
        print("1. 文件中包含网络层名称文本")
        print("2. 可能包含模型结构信息")

    print("\n请将以下信息提供给我:")
    print(f"1. 文件大小: {result['file_size']:,} 字节")
    print(f"2. 是否找到文本: {'是' if result['possible_strings'] else '否'}")
    if result['possible_strings']:
        print(f"3. 找到的文本示例: {result['possible_strings'][:3]}")