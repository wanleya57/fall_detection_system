# API 参考手册

## 公共模块 (common/)

### fall_common.h

系统公共类型定义和常量。

#### 数据类型

```c
typedef enum {
    FALL_OK = 0,
    FALL_ERR_NOMEM = -1,
    FALL_ERR_TIMEOUT = -2,
    FALL_ERR_INVALID = -3,
    FALL_ERR_IO = -4,
    FALL_ERR_MODEL = -5,
    FALL_ERR_IPC = -6,
} fall_err_t;

typedef struct {
    uint8_t frame_buf[FRAME_SIZE];
    uint32_t frame_id;
    uint64_t timestamp;
    uint16_t width;
    uint16_t height;
    uint8_t format;
    volatile uint8_t is_valid;
} video_frame_t;

typedef struct {
    float x, y;
    float score;
} keypoint_t;
```

### ipc_protocol.h

IPC 通信协议。

#### 函数

```c
// 初始化 IPC 通信
fall_err_t ipc_init(void);

// 销毁 IPC 通信
void ipc_deinit(void);

// 获取 IPC 句柄
ipc_handle_t *ipc_get_handle(void);

// 发送 OSD 叠加指令
fall_err_t ipc_send_osd_cmd(osd_cmd_t *cmd);

// 发送跌倒检测结果
fall_err_t ipc_send_fall_result(fall_result_t *result);

// 发送控制命令
fall_err_t ipc_send_cmd(msg_type_t cmd, uint8_t *param, uint8_t param_len);

// 接收控制命令 (阻塞)
fall_err_t ipc_recv_cmd(cmd_data_t *cmd, uint32_t timeout_ms);

// 写入视频帧到共享内存
fall_err_t ipc_write_frame(video_frame_t *frame);

// 从共享内存读取视频帧
fall_err_t ipc_read_frame(video_frame_t *frame);
```

### config.h

配置管理。

#### 函数

```c
// 加载配置文件
fall_err_t config_load(system_config_t *config);

// 保存配置文件
fall_err_t config_save(const system_config_t *config);

// 加载默认配置
void config_load_default(system_config_t *config);

// 获取当前配置 (单例)
system_config_t *config_get(void);

// 初始化配置管理
fall_err_t config_init(void);
```

---

## 视频进程 (process_video/)

### video_capture.h

摄像头采集接口。

#### 函数

```c
// 初始化摄像头
fall_err_t video_capture_init(uint16_t width, uint16_t height, uint8_t fps);

// 启动视频采集
fall_err_t video_capture_start(void);

// 停止视频采集
fall_err_t video_capture_stop(void);

// 获取一帧视频 (阻塞)
fall_err_t video_capture_get_frame(video_frame_t *frame);

// 释放帧缓冲
void video_capture_release_frame(video_frame_t *frame);
```

### video_display.h

TFT LCD 显示接口。

#### 函数

```c
// 初始化 TFT LCD
fall_err_t video_display_init(void);

// 显示一帧视频
fall_err_t video_display_frame(video_frame_t *frame);

// 清除屏幕
void video_display_clear(void);
```

### video_osd.h

OSD 叠加渲染接口。

#### 函数

```c
// 初始化 OSD 模块
fall_err_t video_osd_init(void);

// 处理 OSD 指令
void video_osd_process_cmd(osd_cmd_t *cmd);

// 渲染 OSD 到帧缓冲
void video_osd_render(uint8_t *frame);

// 清除所有 OSD
void video_osd_clear_all(void);
```

### video_rtsp.h

RTSP 视频流服务接口。

#### 函数

```c
// 启动 RTSP 服务
fall_err_t video_rtsp_start(uint16_t port);

// 停止 RTSP 服务
void video_rtsp_stop(void);

// 推送一帧到所有客户端
void video_rtsp_push_frame(video_frame_t *frame);

// 获取当前连接数
int video_rtsp_get_clients(void);
```

### video_record.h

视频录制接口。

#### 函数

```c
// 初始化录制模块
fall_err_t video_record_init(void);

// 开始录制
fall_err_t video_record_start(const char *event_id);

// 停止录制
const char *video_record_stop(void);

// 写入一帧到录制缓冲
void video_record_write_frame(video_frame_t *frame);

// 获取录制状态
int video_record_is_recording(void);
```

---

## AI 进程 (process_ai/)

### ai_engine.h

KPU AI 推理引擎接口。

#### 函数

```c
// 初始化 AI 引擎
fall_err_t ai_engine_init(void);

// 加载人体检测模型
fall_err_t ai_engine_load_detect_model(const char *model_path);

// 加载姿态估计模型
fall_err_t ai_engine_load_pose_model(const char *model_path);

// 人体检测推理
fall_err_t ai_engine_detect_persons(video_frame_t *frame,
                                     float (*detections)[5],
                                     int max_det, int *count);

// 姿态估计推理
fall_err_t ai_engine_estimate_pose(video_frame_t *frame,
                                    float *person_box,
                                    pose_result_t *result);

// 获取推理耗时 (ms)
int ai_engine_get_inference_time_ms(void);
```

### fall_detect.h

跌倒检测算法接口。

#### 函数

```c
// 初始化跌倒检测模块
fall_err_t fall_detect_init(void);

// 分析一帧并判断跌倒
fall_err_t fall_detect_analyze(video_frame_t *frame, fall_result_t *result);

// 获取当前跌倒状态
fall_state_t fall_detect_get_state(void);

// 手动复位跌倒状态
void fall_detect_reset(void);

// 获取检测统计信息
void fall_detect_get_stats(uint32_t *total_frames, uint32_t *detected_frames,
                            uint32_t *false_positives);
```

### fall_alert.h

告警触发接口。

#### 函数

```c
// 初始化告警模块
fall_err_t fall_alert_init(void);

// 触发告警
void fall_alert_trigger(const fall_event_t *event);

// 停止告警
void fall_alert_stop(void);

// 手动复位告警
void fall_alert_reset(void);
```

### fall_notify.h

网络推送接口。

#### 函数

```c
// 初始化网络推送模块
fall_err_t fall_notify_init(void);

// 推送跌倒事件
fall_err_t fall_notify_send(const fall_event_t *event);

// 发送心跳
void fall_notify_heartbeat(void);
```

### fall_storage.h

事件存储接口。

#### 函数

```c
// 初始化存储模块
fall_err_t fall_storage_init(void);

// 保存跌倒事件
fall_err_t fall_storage_save_event(const fall_event_t *event);

// 获取事件总数
uint32_t fall_storage_get_event_count(void);

// 读取最近的事件
int fall_storage_get_recent(fall_event_t *events, int max_count);
```

---

## 系统管理进程 (process_manager/)

### sys_init.h

系统初始化接口。

#### 函数

```c
// 系统初始化
fall_err_t sys_init(void);

// 启动子进程
fall_err_t sys_start_processes(void);

// 停止所有子进程
void sys_stop_processes(void);

// 获取系统运行时间 (ms)
uint64_t sys_get_uptime_ms(void);
```

### key_handler.h

按键处理接口。

#### 函数

```c
// 初始化按键模块
fall_err_t key_handler_init(void);

// 获取按键事件 (非阻塞)
key_event_t key_handler_poll(void);
```

### event_log.h

事件日志接口。

#### 函数

```c
// 初始化事件日志模块
fall_err_t event_log_init(void);

// 记录事件日志
fall_err_t event_log_write(event_log_entry_t *entry);

// 读取最近的日志
int event_log_read_recent(event_log_entry_t *entries, int max_count, int offset);

// 获取日志总数
uint32_t event_log_get_count(void);

// 清除所有日志
void event_log_clear(void);
```

### sys_monitor.h

系统健康监控接口。

#### 函数

```c
// 初始化系统监控
fall_err_t sys_monitor_init(void);

// 获取系统状态
void sys_monitor_get_status(sys_status_t *status);

// 获取 CPU 使用率 (%)
int sys_monitor_get_cpu_usage(void);

// 获取内存使用信息
void sys_monitor_get_memory(uint32_t *free, uint32_t *total);

// 获取芯片温度 (摄氏度)
int sys_monitor_get_temperature(void);
```

---

## 状态机

### fall_state_t

```c
typedef enum {
    FALL_STATE_NORMAL = 0,    // 正常状态
    FALL_STATE_FALLING,       // 疑似跌倒 (连续N帧)
    FALL_STATE_CONFIRMED,     // 确认跌倒
    FALL_STATE_COOLDOWN,      // 告警冷却
    FALL_STATE_RESET,         // 手动复位
} fall_state_t;
```

### 状态转换

```
NORMAL → FALLING (检测到疑似跌倒)
FALLING → CONFIRMED (连续N帧确认)
FALLING → NORMAL (超时未确认)
CONFIRMED → COOLDOWN (冷却时间结束)
COOLDOWN → NORMAL (冷却结束)
任意状态 → RESET (手动复位)
RESET → NORMAL
```

---

## 错误码

| 错误码 | 值 | 说明 |
|--------|-----|------|
| FALL_OK | 0 | 成功 |
| FALL_ERR_NOMEM | -1 | 内存不足 |
| FALL_ERR_TIMEOUT | -2 | 超时 |
| FALL_ERR_INVALID | -3 | 参数无效 |
| FALL_ERR_IO | -4 | IO错误 |
| FALL_ERR_MODEL | -5 | 模型加载失败 |
| FALL_ERR_IPC | -6 | IPC通信失败 |
| FALL_ERR_FULL | -7 | 缓冲已满 |
| FALL_ERR_EMPTY | -8 | 缓冲为空 |
