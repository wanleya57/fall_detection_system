# 部署指南

## 1. 硬件准备

### 所需组件
- K230 RT-Smart AI 开发板
- DVP/MIPI 摄像头模组
- TFT LCD 显示屏 (SPI/RGB接口)
- SD 卡 (≥8GB, FAT32格式)
- 蜂鸣器模块
- WiFi 模块 (如板载ESP32-C3)
- 杜邦线若干

### 接线说明

| 组件 | 接口 | 说明 |
|------|------|------|
| 摄像头 | DVP/CSI | 根据开发板接口 |
| TFT LCD | SPI/RGB | 根据屏幕型号 |
| 蜂鸣器 | GPIO 8 | 可修改代码调整 |
| SD 卡 | SDIO | 板载卡槽 |
| 复位按键 | GPIO 0 | 板载按键 |

## 2. 软件环境搭建

### 安装 RT-Thread Smart SDK

```bash
# 克隆 RT-Thread Smart
git clone https://github.com/RT-Thread/rt-thread-smart.git
cd rt-thread-smart

# 安装 K230 BSP
git clone https://github.com/RT-Thread/rt-thread-k230.git bsp/k230
```

### 安装工具链

```bash
# RISC-V GCC 工具链
# 从 https://github.com/riscv-collab/riscv-gnu-toolchain 下载
# 或使用系统包管理器

# Ubuntu/Debian
sudo apt-get install gcc-riscv64-unknown-elf

# 设置环境变量
export PATH=/path/to/riscv/bin:$PATH
```

### 安装模型转换工具

```bash
pip install onnx onnxsim numpy
```

## 3. 编译项目

```bash
# 设置环境变量
export RTTHREAD_ROOT=/path/to/rt-thread-smart
export RTT_DIR=$RTTHREAD_ROOT

# 进入项目目录
cd Claude.worksapce

# 编译
make clean
make

# 编译单个进程 (调试用)
make video
make ai
make manager
```

## 4. 准备 SD 卡

### 目录结构

```
SD卡根目录/
├── fall_detection/
│   ├── fall_detection      (可执行文件)
│   ├── config.json         (配置文件)
│   └── model/
│       ├── yolov5n_k230.kmodel
│       └── pose_k230.kmodel
├── events/                 (事件视频存储)
└── logs/                   (事件日志)
```

### 准备步骤

```bash
# 1. 格式化 SD 卡为 FAT32
# Windows: 右键格式化 → FAT32
# Linux: mkfs.vfat -F 32 /dev/sdX

# 2. 创建目录
mkdir -p /mnt/sd/fall_detection/model
mkdir -p /mnt/sd/events
mkdir -p /mnt/sd/logs

# 3. 复制文件
cp fall_detection /mnt/sd/fall_detection/
cp common/config.json /mnt/sd/fall_detection/
cp model/*.kmodel /mnt/sd/fall_detection/model/

# 4. 安全弹出
umount /mnt/sd
```

## 5. 准备 AI 模型

### 方式一：使用预训练模型

```bash
# 下载 YOLOv5n
wget https://github.com/ultralytics/assets/releases/download/v0.0.0/yolov5n.pt

# 转换为 ONNX
python -c "
from ultralytics import YOLO
model = YOLO('yolov5n.pt')
model.export(format='onnx', imgsz=640)
"

# 转换为 K230 格式
python tools/model_convert/convert.py \
    --input yolov5n.onnx \
    --output model/yolov5n_k230.kmodel \
    --quantize int8
```

### 方式二：使用示例模型 (测试用)

```bash
# 生成简化测试模型
python tools/model_convert/convert.py --create-sample
```

**注意**: 示例模型仅用于测试流程，实际检测需要使用真实训练的模型。

## 6. 启动运行

1. 将 SD 卡插入 K230 开发板
2. 连接摄像头和 LCD
3. 连接蜂鸣器 (GPIO 8)
4. 上电启动

### 启动顺序

```
RT-Thread Smart 内核启动
  → sys_manager 进程启动
    → video_capture 进程启动 (摄像头+LCD)
    → ai_detect 进程启动 (KPU推理)
    → 系统就绪，开始检测
```

### 调试串口

通过串口连接查看日志输出：

```bash
# Linux
screen /dev/ttyUSB0 115200

# 或使用 minicom
minicom -D /dev/ttyUSB0 -b 115200
```

## 7. 常见问题

### Q: 摄像头无法初始化
- 检查摄像头模组型号是否匹配
- 检查接线是否正确
- 查看串口日志中的错误信息

### Q: AI 推理速度慢
- 检查模型是否正确加载到 KPU
- 降低输入分辨率
- 使用更轻量的模型 (如 MobileNet)

### Q: 跌倒检测误报率高
- 调整 `config.json` 中的 `confidence_threshold`
- 增加 `confirm_frames` 值
- 调整 `angle_threshold` 阈值

### Q: WiFi 连接失败
- 检查 WiFi SSID 和密码是否正确
- 确认 WiFi 模块已正确驱动
- 检查 MQTT 服务器是否可达

## 8. 性能调优

### 内存优化
- 减少环形缓冲区大小 (RING_BUF_COUNT)
- 使用更小的 AI 模型
- 关闭不需要的功能 (RTSP、录制等)

### 延迟优化
- 降低视频分辨率
- 使用更快的 AI 模型
- 优化 IPC 通信路径

### 功耗优化
- 降低帧率
- 使用间歇检测模式
- 关闭 LED 和不需要的外设
