# 轻量级智能跌倒检测监护系统

基于嘉楠 K230 RT-Smart AI 套件的智能跌倒检测系统。当前处于 **PC 仿真阶段**，使用手机 IP Webcam 作为摄像头，在 PC 上验证算法逻辑，后续移植到 K230 开发板。

## 系统特性

- 手机 IP Webcam 实时视频采集 720P@30fps
- YOLOv8n-pose 人体检测 + 17 关键点姿态估计
- 时序分类器 (LSTM / 1D-CNN / TCN 可选) 区分站立/行走/坐下/蹲下/摔倒/躺倒
- 骨架叠加渲染 (YUV 平面绘制，适合 K230 低开销)
- HTTP 视频流查看 (localhost:8554)
- 跌倒事件模拟与告警
- 多进程架构 (PC 仿真为单进程线程模拟)

## 项目结构

```
fall_detection_system/
├── common/                  公共模块
│   ├── inc/                 头文件 (类型定义/IPC/配置/日志/骨架叠加)
│   └── src/                 源文件
├── process_video/           视频采集与显示进程
├── process_ai/              AI 推理与跌倒检测进程
├── process_manager/         系统管理进程
├── pc_sim/                  PC 仿真环境 (WSL/Linux)
│   ├── main_pc.c            PC 仿真主程序
│   ├── pose_detect.py       YOLOv8n-pose Python 子进程
│   ├── rtthread_mock.*      RT-Thread API 模拟层
│   └── Makefile             PC 仿真编译脚本
├── tools/model_convert/     模型转换工具 (ONNX → K230 kmodel)
├── Makefile                 K230 构建脚本
└── ARCHITECTURE.md          架构设计文档
```

## 快速开始 (PC 仿真)

### 环境要求

- WSL (Ubuntu) 或 Linux
- GCC
- Python 3 + ultralytics (YOLOv8)
- 手机安装 IP Webcam (Android) 或同类 App

### 编译运行

```bash
cd pc_sim

# 安装 Python 依赖
pip install ultralytics

# 编译
make clean && make

# 运行
make run
```

### 查看视频流

浏览器打开 http://localhost:8554 ，即可看到带骨架叠加的实时视频流。

### 操作

- 终端按 **Enter** 模拟跌倒事件
- 按 **q** 退出

## 摄像头配置

手机安装 IP Webcam App，开启后在同一局域网内会分配一个 IP 地址（如 `192.168.x.x:8080`）。

系统会自动扫描局域网内的摄像头，首次连接成功后会缓存 IP 到 `camera_cache.txt`，后续启动直接使用缓存。

如果需要手动指定，编辑 `camera_cache.txt` 写入摄像头 URL：
```
http://192.168.x.x:8080/video
```

## AI 模型

### PC 仿真

使用 YOLOv8n-pose (PyTorch)，通过 Python 子进程调用 ultralytics 推理。

模型文件 `yolov8n-pose.pt` 需自行下载（约 6MB）。

### K230 部署 (未来)

需要将模型转换为 K230 KPU 支持的 `.kmodel` 格式：

```bash
cd tools/model_convert
python convert.py --input yolov8n_pose.onnx --output yolov8n_pose_k230.kmodel --target k230 --quantize int8
```

## 技术栈

| 模块 | 技术 |
|------|------|
| 目标芯片 | Kendryte K230 (RISC-V 双核, KPU AI 加速) |
| 实时系统 | RT-Thread Smart (微内核多进程) |
| 空间模型 | YOLOv8n-pose (17 关键点姿态估计) |
| 时序模型 | LSTM / 1D-CNN / TCN (动作分类, 6 类) |
| 特征维度 | 41 维/帧 (34 关键点坐标 + 4 bbox + 2 速度 + 1 倾斜角) |
| 视频编码 | YUV420 → JPEG (ffmpeg) → HTTP MJPEG 流 |
| 骨架渲染 | YUV Y 平面绘制 (整数运算, 无动态内存, 适配 K230) |
| 进程通信 | 共享内存环形缓冲 + 消息队列 + 信号量 |

## 跌倒检测流程

```
摄像头 → 视频帧 → [YOLOv8n-pose] → bbox + 17 关键点
                                       ↓
                                  提取 41 维特征
                                       ↓
                            [时序分类器] → 动作类别
                                       ↓
                            状态机确认 (连续 N 帧) → 跌倒告警
```

## 部署到 K230 (未来)

```bash
# 设置环境变量
export RTTHREAD_ROOT=/path/to/rt-thread-smart
export RTT_DIR=$RTTHREAD_ROOT

# 编译
make

# 部署到 SD 卡
make deploy
```

需要准备：
1. K230 开发板 + SD 卡
2. RT-Thread Smart SDK
3. RISC-V GCC 工具链
4. 转换后的 kmodel 模型文件
