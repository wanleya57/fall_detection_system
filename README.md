# 轻量级智能跌倒检测监护系统

基于嘉楠 K230 RT-Smart AI 套件的智能跌倒检测监护系统，利用 KPU 神经网络加速器实时检测人体跌倒事件。

## 系统特性

- 实时视频采集 720P@30fps，TFT LCD 本地预览
- KPU 加速 AI 推理，跌倒检测准确率 ≥90%
- 多进程架构：视频采集、AI 检测、系统管理独立运行
- 跌倒告警：OSD 叠加 + 蜂鸣器 + WiFi MQTT 推送
- 视频录制：事件前后视频片段自动保存到 SD 卡
- 手动复位：按键手动复位告警
- 事件日志：本地事件记录与查看
- RTSP 远程视频查看（可选）

## 项目结构

```
fall_detection/
├── common/              公共模块 (类型定义/IPC/配置/日志)
├── process_video/       视频采集与显示进程
├── process_ai/          AI事件检测与告警进程
├── process_manager/     系统管理与服务进程
├── model/               AI模型文件 (需自行准备)
├── Makefile             构建脚本
├── Kconfig              RT-Thread 配置
└── ARCHITECTURE.md      架构设计文档
```

## 编译部署

### 环境要求

- RT-Thread Smart SDK
- RISC-V GCC 工具链 (riscv64-unknown-elf-gcc)
- K230 开发板

### 编译

```bash
# 设置环境变量
export RTTHREAD_ROOT=/path/to/rt-thread-smart
export RTT_DIR=$RTTHREAD_ROOT

# 编译
make

# 单独编译各进程
make video
make ai
make manager
```

### 部署

1. 将编译产物复制到 SD 卡
2. 准备 AI 模型文件 (详见 model/README.md)
3. 复制配置文件 config.json 到 SD 卡
4. 插入 SD 卡，上电启动

## 配置说明

编辑 `common/config.json`：

```json
{
  "video_width": 720,
  "video_height": 480,
  "video_fps": 30,
  "confidence_threshold": 0.6,
  "confirm_frames": 5,
  "cooldown_ms": 10000,
  "wifi_ssid": "YourWiFi",
  "wifi_password": "YourPassword",
  "mqtt_broker": "192.168.1.100",
  "mqtt_port": 1883,
  "mqtt_topic": "fall_detection/events"
}
```

## 按键操作

- **短按**: 手动复位告警
- **长按**: 切换录制状态
- **双击**: 查看最近事件日志

## 技术栈

- **芯片**: Kendryte K230 (RISC-V 双核, KPU AI加速)
- **系统**: RT-Thread Smart (微内核多进程)
- **AI**: YOLOv5n 人体检测 + 姿态估计 + 规则判定
- **通信**: 共享内存 + 消息队列 + 信号量 + MQTT
