# 轻量级智能跌倒检测监护系统 - 整体架构设计

## 一、系统概述

基于嘉楠 K230 芯片 RT-Smart AI 套件，构建一个实时跌倒检测监护系统。利用 K230 双核 RISC-V 架构 + KPU 神经网络加速器 + RT-Thread Smart 微内核多进程机制，实现视频采集、AI 推理、事件告警的解耦运行。

---

## 二、硬件平台

| 组件 | 说明 |
|------|------|
| SoC | Kendryte K230 (RISC-V 双核, 12nm) |
| AI 加速 | 内置 KPU (支持 INT8/INT16 推理) |
| 摄像头 | DVP/MIPI 摄像头模组，支持 720P@30fps |
| 显示屏 | TFT LCD (SPI/RGB 接口) |
| 音频 | I2S 蜂鸣器/喇叭 |
| 存储 | SD 卡 (SPI/SDIO) |
| 网络 | Wi-Fi 模块 (ESP32-C3/板载) |
| 按键 | GPIO 按键 (手动复位) |

---

## 三、软件架构

### 3.1 总体分层

```
┌─────────────────────────────────────────────────────────────┐
│                    用户空间 (User Space)                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  进程1:       │  │  进程2:       │  │  进程3:       │      │
│  │  视频采集     │  │  AI跌倒检测   │  │  系统管理     │      │
│  │  与显示       │  │  与告警       │  │  与服务       │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                 │                 │               │
│         └────────┬────────┴────────┬────────┘               │
│                  │                 │                         │
│         ┌────────▼─────────────────▼────────┐              │
│         │        IPC 通信层                  │              │
│         │  (共享内存 + 消息队列 + 信号量)     │              │
│         └───────────────────────────────────┘              │
├─────────────────────────────────────────────────────────────┤
│                  RT-Thread Smart 微内核                      │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐            │
│  │ 调度 │ │ 内存 │ │ IPC  │ │ 文件 │ │ 设备 │            │
│  │ 器   │ │ 管理 │ │ 管理 │ │ 系统 │ │ 驱动 │            │
│  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘            │
├─────────────────────────────────────────────────────────────┤
│                      硬件抽象层 (HAL)                        │
│  KPU驱动 │ Camera驱动 │ LCD驱动 │ I2S驱动 │ SPI/SDIO │ WiFi │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 进程划分

#### 进程1: video_capture (视频采集与显示)

**职责：**
- 从摄像头实时采集 720P@30fps 视频帧
- 在 TFT LCD 上实时预览画面
- 接收 AI 进程的 OSD 叠加指令，绘制告警标识
- 可选：启动 RTSP/HTTP Server 提供远程视频流
- 可选：录制视频片段保存到 SD 卡

**关键数据结构：**
```c
// 视频帧共享缓冲区 (环形队列)
typedef struct {
    uint8_t  frame_buf[FRAME_BUF_SIZE];  // 帧数据
    uint32_t frame_id;                    // 帧序号
    uint64_t timestamp;                   // 时间戳 (ms)
    uint16_t width;
    uint16_t height;
    uint8_t  format;                      // YUV420/NV12
    volatile uint8_t is_valid;            // 有效标志
} video_frame_t;

// 环形帧缓冲
typedef struct {
    video_frame_t frames[RING_BUF_COUNT]; // 环形缓冲
    volatile uint32_t write_idx;           // 写索引
    volatile uint32_t read_idx;            // 读索引
    rt_sem_t empty_sem;                   // 空槽信号量
    rt_sem_t full_sem;                    // 满槽信号量
    rt_mutex_t lock;                      // 互斥锁
} frame_ring_buf_t;
```

**线程模型：**
```
video_capture 进程
├── thread_capture    : 摄像头采集线程 (高优先级, 实时)
├── thread_display    : LCD 显示刷新线程 (中优先级)
├── thread_osd        : OSD 叠加渲染线程 (中优先级)
├── thread_rtsp       : RTSP 服务线程 (低优先级, 可选)
└── thread_record     : 视频录制线程 (低优先级, 按需启动)
```

---

#### 进程2: ai_detect (AI事件检测与告警)

**职责：**
- 从共享内存读取视频帧
- 运行轻量级视觉模型 (姿态估计/跌倒分类)
- 检测跌倒事件 (准确率 ≥90%)
- 通过 IPC 通知视频进程叠加 OSD
- 触发音频告警 (蜂鸣器/语音)
- 保存事件视频片段到 SD 卡
- 通过 Wi-Fi 推送通知到手机/云端

**AI 模型方案：**
```
方案 A: 姿态估计 + 跌倒分类 (推荐)
┌─────────────┐    ┌──────────────┐    ┌─────────────┐
│ YOLOv5-nano │───▶│ 人体关键点   │───▶│  跌倒分类   │
│ 人体检测    │    │ 姿态估计     │    │  判定       │
│ (KPU加速)   │    │ (KPU加速)    │    │ (规则+MLP)  │
└─────────────┘    └──────────────┘    └─────────────┘

方案 B: 端到端跌倒检测网络 (简化)
┌─────────────────────────┐    ┌─────────────┐
│ 轻量级跌倒检测CNN       │───▶│  跌倒判定   │
│ (MobileNet-SSD / 简化版) │    │  + 后处理   │
│ (KPU加速)               │    └─────────────┘
└─────────────────────────┘
```

**跌倒判定逻辑：**
```c
// 基于关键点的跌倒判定
typedef struct {
    float head_y;       // 头部关键点 y 坐标
    float hip_y;        // 髋部关键点 y 坐标
    float ankle_y;      // 踝部关键点 y 坐标
    float body_angle;   // 身体倾斜角度
    float velocity;     // 垂直方向速度
    int   frame_count;  // 连续帧计数
} fall_feature_t;

// 判定规则
#define FALL_ANGLE_THRESHOLD   45.0f  // 倾斜角度阈值 (度)
#define FALL_VELOCITY_THRESHOLD 0.3f  // 下落速度阈值 (归一化)
#define FALL_CONFIRM_FRAMES    5      // 连续确认帧数
#define FALL_COOLDOWN_MS      10000   // 告警冷却时间 (ms)
```

**线程模型：**
```
ai_detect 进程
├── thread_inference   : AI 推理线程 (高优先级, 实时)
├── thread_alert       : 告警处理线程 (高优先级)
├── thread_notification: 网络推送线程 (低优先级)
├── thread_storage     : 视频存储线程 (低优先级)
└── thread_log         : 事件日志线程 (低优先级)
```

---

#### 进程3: sys_manager (系统管理与服务)

**职责：**
- 系统初始化与进程生命周期管理
- 按键检测与手动复位
- 本地事件日志管理
- 可选：HTTP API 服务 (远程查询/配置)
- 系统状态监控 (CPU/内存/温度)

**线程模型：**
```
sys_manager 进程
├── thread按键检测    : GPIO 按键扫描线程 (中优先级)
├── thread_log        : 日志管理线程 (低优先级)
├── thread_http       : HTTP API 服务线程 (低优先级, 可选)
└── thread_watchdog   : 系统健康监控线程 (中优先级)
```

---

### 3.3 IPC 通信设计

RT-Thread Smart 支持三种主要 IPC 机制，本系统混合使用：

| IPC 机制 | 用途 | 方向 |
|----------|------|------|
| **共享内存** | 视频帧传输 | video_capture → ai_detect |
| **消息队列** | 事件通知/控制命令 | 双向 |
| **信号量** | 同步与流控 | 双向 |

**IPC 数据流：**

```
video_capture                          ai_detect
    │                                      │
    │  ┌─── 共享内存: 视频帧 ────────────┐ │
    │  │  frame_ring_buf (环形队列)       │ │
    │  │  - 写入: video_capture           │ │
    │  │  - 读取: ai_detect               │ │
    │  └─────────────────────────────────┘ │
    │                                      │
    │  ┌─── 消息队列: AI结果 → OSD ──────┐ │
    │  │  osd_notify_mq                   │ │
    │  │  - 发送: ai_detect               │ │
    │  │  - 接收: video_capture           │ │
    │  └─────────────────────────────────┘ │
    │                                      │
    │  ┌─── 消息队列: 控制命令 ──────────┐ │
    │  │  cmd_mq                          │ │
    │  │  - 双向通信                      │ │
    │  └─────────────────────────────────┘ │
    │                                      │
    │  ┌─── 信号量: 帧同步 ─────────────┐ │
    │  │  frame_ready_sem                 │ │
    │  │  - V post (video_capture)        │ │
    │  │  - P wait (ai_detect)            │ │
    │  └─────────────────────────────────┘ │
```

**消息格式定义：**
```c
// 消息类型枚举
typedef enum {
    MSG_TYPE_FRAME_READY    = 0x01,  // 新帧就绪
    MSG_TYPE_FALL_DETECTED  = 0x02,  // 检测到跌倒
    MSG_TYPE_FALL_CLEARED   = 0x03,  // 跌倒解除/复位
    MSG_TYPE_OSD_UPDATE     = 0x04,  // OSD 更新指令
    MSG_TYPE_ALERT_TRIGGER  = 0x05,  // 触发告警
    MSG_TYPE_ALERT_RESET    = 0x06,  // 手动复位告警
    MSG_TYPE_SYS_STATUS     = 0x07,  // 系统状态上报
    MSG_TYPE_CMD_START      = 0x10,  // 开始检测
    MSG_TYPE_CMD_STOP       = 0x11,  // 停止检测
    MSG_TYPE_CMD_CONFIG     = 0x12,  // 配置参数
} msg_type_t;

// 消息头
typedef struct {
    msg_type_t type;
    uint32_t   seq;          // 序列号
    uint64_t   timestamp;    // 时间戳
    uint16_t   data_len;     // 数据长度
    uint8_t    data[0];      // 变长数据
} ipc_msg_t;

// 跌倒检测结果消息
typedef struct {
    float confidence;        // 置信度 0.0~1.0
    float fall_angle;        // 身体角度
    int   frame_id;          // 触发帧ID
    int   duration_ms;       // 检测耗时
    char  event_id[16];      // 事件唯一ID
} fall_result_t;

// OSD 叠加指令
typedef struct {
    int   x, y;              // 叠加位置
    int   width, height;     // 叠加区域
    uint8_t show_flag;       // 显示/隐藏
    char  text[64];          // 显示文字
    uint32_t color;          // 颜色 (ARGB8888)
    uint32_t duration_ms;    // 显示时长 (0=持续)
} osd_cmd_t;
```

---

## 四、关键流程

### 4.1 系统启动流程

```
┌──────────────────────────────────────────────────────┐
│                    系统上电                           │
└──────────────────┬───────────────────────────────────┘
                   ▼
┌──────────────────────────────────────────────────────┐
│  RT-Thread Smart 内核初始化                          │
│  - HAL 初始化 (时钟/中断/GPIO)                       │
│  - 设备驱动加载 (Camera/LCD/KPU/SPI/WiFi)            │
│  - 文件系统挂载 (FAT32 - SD卡)                       │
│  - 共享内存区域创建                                  │
│  - IPC 对象创建 (消息队列/信号量)                     │
└──────────────────┬───────────────────────────────────┘
                   ▼
┌──────────────────────────────────────────────────────┐
│  启动 sys_manager 进程 (PID=100)                     │
│  - 加载配置文件 (SD卡/config.json)                    │
│  - 初始化按键检测                                    │
│  - 启动事件日志服务                                  │
└──────────────────┬───────────────────────────────────┘
                   ▼
┌──────────────────────────────────────────────────────┐
│  启动 video_capture 进程 (PID=200)                   │
│  - 初始化摄像头 (720P@30fps YUV420)                  │
│  - 初始化 TFT LCD                                    │
│  - 创建环形帧缓冲                                    │
│  - 启动采集/显示线程                                 │
│  - (可选) 启动 RTSP Server                           │
└──────────────────┬───────────────────────────────────┘
                   ▼
┌──────────────────────────────────────────────────────┐
│  启动 ai_detect 进程 (PID=300)                       │
│  - 加载 AI 模型到 KPU 内存                           │
│  - 初始化 KPU 推理上下文                             │
│  - 绑定共享内存读取端                                │
│  - 启动推理/告警/推送/存储线程                        │
└──────────────────┬───────────────────────────────────┘
                   ▼
┌──────────────────────────────────────────────────────┐
│  系统就绪，开始实时检测                              │
└──────────────────────────────────────────────────────┘
```

### 4.2 实时检测数据流

```
摄像头 → 采集帧 → 写入共享内存 → 通知AI进程
                                      │
                                      ▼
                                KPU 推理
                                      │
                              ┌───────┴───────┐
                              │               │
                         正常状态          跌倒检测
                              │               │
                              ▼               ▼
                          继续采集     ┌──────────────┐
                                       │ 通知视频进程 │→ OSD "Fall Detected!"
                                       │ 触发蜂鸣器   │→ 声音告警
                                       │ 保存视频片段 │→ SD 卡
                                       │ 推送通知     │→ WiFi/云端
                                       │ 记录日志     │→ SD 卡
                                       └──────────────┘
                                              │
                                              ▼
                                    等待手动复位 / 自动冷却
```

### 4.3 跌倒检测算法流程

```
┌─────────────────┐
│  读取视频帧      │
│  (从共享内存)    │
└────────┬────────┘
         ▼
┌─────────────────┐
│  预处理          │
│  - 缩放至模型输入│
│  - 归一化        │
└────────┬────────┘
         ▼
┌─────────────────┐
│  KPU 推理        │
│  方案A:          │
│   1. YOLOv5-nano │→ 人体检测框
│   2. 姿态估计    │→ 17个关键点
│  方案B:          │
│   1. 跌倒分类CNN │→ 跌倒概率
└────────┬────────┘
         ▼
┌─────────────────┐
│  后处理          │
│  - 关键点角度计算│
│  - 速度估计      │
│  - 跌倒判定规则  │
└────────┬────────┘
         ▼
┌─────────────────┐
│  状态机判定      │
│  状态:          │
│   NORMAL        │ ← 正常
│   FALLING       │ ← 疑似跌倒 (连续N帧)
│   CONFIRMED     │ ← 确认跌倒
│   COOLDOWN      │ ← 告警冷却
│   MANUAL_RESET  │ ← 手动复位
└─────────────────┘
```

---

## 五、目录结构

```
fall_detection/
├── ARCHITECTURE.md          # 本文件
├── README.md                # 项目说明
├── Makefile                 # 顶层构建脚本
├── Kconfig                  # RT-Thread 配置
├── SConscript               # SCons 构建脚本
│
├── common/                  # 公共模块
│   ├── inc/
│   │   ├── fall_common.h    # 公共类型定义
│   │   ├── ipc_protocol.h   # IPC 协议定义
│   │   ├── config.h         # 系统配置常量
│   │   └── log.h            # 日志接口
│   └── src/
│       ├── ipc_protocol.c   # IPC 协议实现
│       ├── config.c         # 配置管理
│       └── log.c            # 日志实现
│
├── process_video/           # 进程1: 视频采集与显示
│   ├── main.c               # 进程入口
│   ├── inc/
│   │   ├── video_capture.h  # 采集接口
│   │   ├── video_display.h  # 显示接口
│   │   ├── video_osd.h      # OSD 叠加接口
│   │   ├── video_rtsp.h     # RTSP 服务接口
│   │   └── video_record.h   # 录制接口
│   └── src/
│       ├── video_capture.c  # 摄像头采集
│       ├── video_display.c  # TFT 显示刷新
│       ├── video_osd.c      # OSD 叠加渲染
│       ├── video_rtsp.c     # RTSP/HTTP 服务
│       └── video_record.c   # 视频录制到SD卡
│
├── process_ai/              # 进程2: AI事件检测与告警
│   ├── main.c               # 进程入口
│   ├── inc/
│   │   ├── ai_engine.h      # AI 推理引擎接口
│   │   ├── fall_detect.h    # 跌倒检测算法接口
│   │   ├── fall_alert.h     # 告警触发接口
│   │   ├── fall_notify.h    # 网络推送接口
│   │   └── fall_storage.h   # 视频存储接口
│   └── src/
│       ├── ai_engine.c      # KPU 推理引擎
│       ├── fall_detect.c    # 跌倒检测算法 + 状态机
│       ├── fall_alert.c     # 蜂鸣器/语音告警
│       ├── fall_notify.c    # WiFi 推送 (MQTT/HTTP)
│       └── fall_storage.c   # 视频片段保存
│
├── process_manager/         # 进程3: 系统管理
│   ├── main.c               # 进程入口
│   ├── inc/
│   │   ├── sys_init.h       # 系统初始化接口
│   │   ├── key_handler.h    # 按键处理接口
│   │   ├── event_log.h      # 事件日志接口
│   │   └── sys_monitor.h    # 系统监控接口
│   └── src/
│       ├── sys_init.c       # 系统初始化 + 进程启动
│       ├── key_handler.c    # 按键检测 + 手动复位
│       ├── event_log.c      # 事件日志记录/查询
│       └── sys_monitor.c    # CPU/内存/温度监控
│
├── model/                   # AI 模型文件
│   ├── yolov5n_k230.kmodel  # 人体检测模型 (KPU量化)
│   ├── pose_k230.kmodel     # 姿态估计模型 (KPU量化)
│   └── README.md            # 模型说明
│
├── drivers/                 # 板级驱动适配
│   ├── camera/              # 摄像头驱动适配
│   ├── lcd/                 # LCD 驱动适配
│   ├── audio/               # 音频驱动适配
│   └── wifi/                # WiFi 驱动适配
│
├── tools/                   # 工具脚本
│   ├── model_convert/       # 模型转换工具
│   ├── ota/                 # OTA 升级工具
│   └── test/                # 测试脚本
│
└── docs/                    # 文档
    ├── user_guide.md        # 用户手册
    ├── api_reference.md     # API 参考
    └── deployment.md        # 部署指南
```

---

## 六、关键技术点

### 6.1 KPU 内存管理

K230 的 KPU 可直接访问 DDR，模型加载需要注意内存对齐和分区：

```c
// KPU 模型加载
#define KPU_MODEL_MEM_SIZE  (2 * 1024 * 1024)  // 2MB 预留

// 模型加载到 KPU 可访问区域
void *model_mem = rt_malloc_align(KPU_MODEL_MEM_SIZE, 64);
kpu_model_load(model_file, model_mem);
```

### 6.2 实时性保障

- 采集线程使用 `RT_SCHED_PRIORITYHighest` 实时优先级
- KPU 推理使用 DMA 传输，不占用 CPU
- 共享内存使用无锁环形队列（单生产者单消费者）
- 告警线程使用高优先级，确保 ≤500ms 延迟

### 6.3 资源限制与稳定性

```c
// 资源配置
#define MAX_FRAME_BUF_SIZE     (720 * 480 * 3 / 2)  // YUV420
#define RING_BUF_COUNT         3                      // 三重缓冲
#define MAX_EVENT_LOG_ENTRIES   1000                   // 日志条目上限
#define WATCHDOG_TIMEOUT_MS    30000                   // 看门狗 30s
#define ALERT_COOLDOWN_MS      10000                   // 告警冷却 10s
```

### 6.4 跌倒检测状态机

```
         ┌──────────┐
    ┌───▶│  NORMAL  │◀──────────────────────┐
    │    └────┬─────┘                        │
    │         │ 检测到疑似跌倒               │
    │         ▼                              │
    │    ┌──────────┐                        │
    │    │ FALLING  │─── 超时未确认 ─────────┤
    │    └────┬─────┘                        │
    │         │ 连续N帧确认                  │
    │         ▼                              │
    │    ┌──────────┐                        │
    │    │CONFIRMED │                        │
    │    └────┬─────┘                        │
    │         │ 触发告警                     │
    │         ▼                              │
    │    ┌──────────┐                        │
    │    │ COOLDOWN │─── 冷却时间结束 ───────┘
    │    └────┬─────┘
    │         │ 手动复位按钮
    │         ▼
    │    ┌──────────┐
    └────│  RESET   │
         └──────────┘
```

---

## 七、性能指标

| 指标 | 目标值 | 说明 |
|------|--------|------|
| 视频帧率 | 30 fps | 720P 实时采集 |
| AI 推理速度 | ≥15 fps | 每帧推理时间 ≤66ms |
| 跌倒检测准确率 | ≥90% | 标准室内光照条件 |
| 端到端延迟 | ≤500ms | 跌倒→告警触发 (加分项) |
| 内存占用 | ≤80MB | 总系统内存 |
| CPU 占用 | ≤70% | 双核平均 |
| 误报率 | ≤5% | 标准场景 |
| 漏报率 | ≤10% | 标准场景 |

---

## 八、开发计划

| 阶段 | 内容 | 周期 |
|------|------|------|
| Phase 1 | 项目搭建 + IPC 通信验证 | 3天 |
| Phase 2 | 视频采集与显示进程 | 4天 |
| Phase 3 | AI 模型训练/转换 + 推理引擎 | 5天 |
| Phase 4 | 跌倒检测算法 + 状态机 | 3天 |
| Phase 5 | 告警/存储/推送模块 | 3天 |
| Phase 6 | 系统集成 + 调优 | 4天 |
| Phase 7 | 测试 + 文档 + 比赛材料 | 3天 |
| **总计** | | **约25天** |
