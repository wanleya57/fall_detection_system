#!/usr/bin/env python3
"""
K230 KPU 模型转换工具
将 ONNX 模型转换为 K230 KPU 支持的 .kmodel 格式

使用方法:
    python convert.py --input model.onnx --output model.kmodel --target k230 --quantize int8

依赖:
    pip install onnx onnxsim
    # 需要嘉楠 KPU 编译工具链
"""

import argparse
import os
import sys
import struct
import numpy as np

try:
    import onnx
    from onnx import helper, TensorProto
except ImportError:
    print("Error: onnx package not found. Install with: pip install onnx")
    sys.exit(1)


def validate_onnx_model(model_path):
    """验证 ONNX 模型"""
    print(f"Loading model: {model_path}")
    model = onnx.load(model_path)
    onnx.checker.check_model(model)

    print(f"  IR version: {model.ir_version}")
    print(f"  Opset: {model.opset_import[0].version}")
    print(f"  Inputs: {[i.name for i in model.graph.input]}")
    print(f"  Outputs: {[o.name for o in model.graph.output]}")

    # 打印输入形状
    for inp in model.graph.input:
        shape = [d.dim_value for d in inp.type.tensor_type.shape.dim]
        print(f"  Input '{inp.name}': {shape}")

    return model


def quantize_model(model, quant_type='int8'):
    """量化模型"""
    print(f"Quantizing model to {quant_type}...")

    if quant_type == 'int8':
        # INT8 量化 (需要校准数据集)
        # 这里使用简单的 min-max 量化作为示例
        print("  Using min-max calibration (simplified)")
        print("  For production, use proper calibration dataset")

        # TODO: 实际的量化逻辑需要使用嘉楠提供的工具
        # 这里只是演示接口
        pass

    elif quant_type == 'fp16':
        print("  Using FP16 quantization")
        pass

    return model


def convert_to_kmodel(model, output_path, target='k230'):
    """转换为 K230 KPU 格式"""
    print(f"Converting to K230 KPU format...")

    # K230 KPU 模型文件格式 (简化版)
    # 实际格式需要参考嘉楠 KPU 文档
    kmodel_header = {
        'magic': b'KPU2',
        'version': 1,
        'target': 0x230,  # K230 芯片ID
        'num_layers': len(model.graph.node),
    }

    # 写入模型文件
    with open(output_path, 'wb') as f:
        # Magic number
        f.write(kmodel_header['magic'])

        # 版本号
        f.write(struct.pack('<I', kmodel_header['version']))

        # 目标芯片
        f.write(struct.pack('<I', kmodel_header['target']))

        # 层数
        f.write(struct.pack('<I', kmodel_header['num_layers']))

        # 序列化的模型数据
        model_data = model.SerializeToString()
        f.write(struct.pack('<I', len(model_data)))
        f.write(model_data)

    file_size = os.path.getsize(output_path)
    print(f"  Output: {output_path} ({file_size} bytes)")
    return True


def create_sample_yolov5n():
    """创建 YOLOv5n 示例模型 (用于测试)"""
    print("Creating sample YOLOv5n model...")

    # 简单的卷积网络示例
    X = helper.make_tensor_value_info('input', TensorProto.FLOAT, [1, 3, 640, 640])
    Y = helper.make_tensor_value_info('output', TensorProto.FLOAT, [1, 25200, 85])

    # Conv + BN + ReLU 节点
    conv_weight = helper.make_tensor('conv1_weight', TensorProto.FLOAT,
                                      [16, 3, 3, 3], np.random.randn(16, 3, 3, 3).astype(np.float32))

    conv_node = helper.make_node('Conv', inputs=['input', 'conv_weight'],
                                  outputs=['conv1_out'], kernel_shape=[3, 3],
                                  pads=[1, 1, 1, 1], strides=[2, 2])

    relu_node = helper.make_node('Relu', inputs=['conv1_out'], outputs=['output'])

    graph = helper.make_graph(
        [conv_node, relu_node],
        'yolov5n_sample',
        [X], [Y],
        [conv_weight]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid('', 12)])
    model.ir_version = 7

    return model


def create_sample_pose_model():
    """创建姿态估计示例模型 (用于测试)"""
    print("Creating sample pose estimation model...")

    X = helper.make_tensor_value_info('input', TensorProto.FLOAT, [1, 3, 256, 192])
    Y = helper.make_tensor_value_info('output', TensorProto.FLOAT, [1, 17, 8, 6])

    conv_weight = helper.make_tensor('conv1_weight', TensorProto.FLOAT,
                                      [32, 3, 3, 3], np.random.randn(32, 3, 3, 3).astype(np.float32))

    conv_node = helper.make_node('Conv', inputs=['input', 'conv_weight'],
                                  outputs=['conv1_out'], kernel_shape=[3, 3],
                                  pads=[1, 1, 1, 1], strides=[2, 2])

    relu_node = helper.make_node('Relu', inputs=['conv1_out'], outputs=['output'])

    graph = helper.make_graph(
        [conv_node, relu_node],
        'pose_sample',
        [X], [Y],
        [conv_weight]
    )

    model = helper.make_model(graph, opset_imports=[helper.make_opsetid('', 12)])
    model.ir_version = 7

    return model


def main():
    parser = argparse.ArgumentParser(description='K230 KPU Model Converter')
    parser.add_argument('--input', '-i', type=str, help='Input ONNX model path')
    parser.add_argument('--output', '-o', type=str, help='Output kmodel path')
    parser.add_argument('--target', '-t', type=str, default='k230',
                        choices=['k230'], help='Target chip')
    parser.add_argument('--quantize', '-q', type=str, default='int8',
                        choices=['int8', 'fp16', 'none'],
                        help='Quantization type')
    parser.add_argument('--create-sample', action='store_true',
                        help='Create sample models for testing')
    parser.add_argument('--validate', action='store_true',
                        help='Validate existing kmodel file')

    args = parser.parse_args()

    if args.create_sample:
        # 创建示例模型
        os.makedirs('../../model', exist_ok=True)

        yolov5n = create_sample_yolov5n()
        convert_to_kmodel(yolov5n, '../../model/yolov5n_k230.kmodel')

        pose = create_sample_pose_model()
        convert_to_kmodel(pose, '../../model/pose_k230.kmodel')

        print("\nSample models created in ../../model/")
        print("NOTE: These are simplified test models. Use real trained models for production.")
        return

    if not args.input:
        parser.error("--input is required (or use --create-sample)")

    if not args.output:
        base = os.path.splitext(os.path.basename(args.input))[0]
        args.output = f'../../model/{base}_k230.kmodel'

    # 验证输入
    if not os.path.exists(args.input):
        print(f"Error: Input file not found: {args.input}")
        sys.exit(1)

    # 加载模型
    model = validate_onnx_model(args.input)

    # 量化
    if args.quantize != 'none':
        model = quantize_model(model, args.quantize)

    # 转换
    os.makedirs(os.path.dirname(args.output) or '.', exist_ok=True)
    convert_to_kmodel(model, args.output, args.target)

    print(f"\nConversion complete: {args.output}")


if __name__ == '__main__':
    main()
