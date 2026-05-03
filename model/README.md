# AI 模型文件

## 所需模型

本项目需要两个 KPU 加速模型：

### 1. 人体检测模型
- **文件**: `yolov5n_k230.kmodel`
- **用途**: 检测画面中的人体
- **输入**: 640x640 RGB 图像
- **输出**: 检测框 + 置信度
- **推荐**: YOLOv5n nano 版本，K230 INT8 量化

### 2. 姿态估计模型
- **文件**: `pose_k230.kmodel`
- **用途**: 提取人体 17 个关键点
- **输入**: 裁剪后的人体区域
- **输出**: 17 个关键点坐标 + 置信度
- **推荐**: MobileNet-SSD 姿态估计，K230 INT8 量化

## 模型转换

使用嘉楠提供的模型转换工具：

```bash
# 转换 YOLOv5n
python tools/model_convert/convert.py \
    --input yolov5n.onnx \
    --output yolov5n_k230.kmodel \
    --target k230 \
    --quantize int8

# 转换姿态估计模型
python tools/model_convert/convert.py \
    --input pose_model.onnx \
    --output pose_k230.kmodel \
    --target k230 \
    --quantize int8
```

## 模型下载

可从以下地址获取预训练模型：

- YOLOv5n: https://github.com/ultralytics/ultralytics
- 姿态估计: https://github.com/NVIDIA/DeepLearningExamples

## 注意事项

1. 模型需要转换为 K230 KPU 支持的格式 (.kmodel)
2. 推荐使用 INT8 量化以获得最佳性能
3. 将转换后的模型放到 SD 卡的 `/sdcard/model/` 目录
