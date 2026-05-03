/**
 * fall_common.h - 系统公共类型定义
 */
#ifndef __FALL_COMMON_H__
#define __FALL_COMMON_H__

#include <stdint.h>

#ifdef RT_USING_MOCK
#include "rtthread_mock.h"
#else
#include <rtthread.h>
#endif

/* 系统版本 */
#define FALL_DETECTION_VERSION      "1.0.0"
#define FALL_DETECTION_AUTHOR       "Fall Detection Team"

/* 视频参数 */
#define VIDEO_WIDTH                 720
#define VIDEO_HEIGHT                480
#define VIDEO_FPS                   30
#define VIDEO_FORMAT_YUV420         0
#define FRAME_SIZE                  (VIDEO_WIDTH * VIDEO_HEIGHT * 3 / 2)

/* 环形缓冲区 */
#define RING_BUF_COUNT              3

/* 告警参数 */
#define ALERT_COOLDOWN_MS           10000
#define FALL_CONFIRM_FRAMES         5
#define FALL_ANGLE_THRESHOLD        45.0f
#define FALL_VELOCITY_THRESHOLD     0.3f

/* 进程 PID */
#define PID_SYS_MANAGER             100
#define PID_VIDEO_CAPTURE           200
#define PID_AI_DETECT               300

/* 进程名称 */
#define PROC_NAME_SYS_MANAGER       "sys_manager"
#define PROC_NAME_VIDEO_CAPTURE     "video_capture"
#define PROC_NAME_AI_DETECT         "ai_detect"

/* 日志标签 */
#define LOG_TAG_MAIN                "MAIN"
#define LOG_TAG_VIDEO               "VIDEO"
#define LOG_TAG_AI                  "AI"
#define LOG_TAG_ALERT               "ALERT"
#define LOG_TAG_IPC                 "IPC"
#define LOG_TAG_SYSTEM              "SYS"

/* 跌倒检测状态 */
typedef enum {
    FALL_STATE_NORMAL = 0,
    FALL_STATE_FALLING,
    FALL_STATE_CONFIRMED,
    FALL_STATE_COOLDOWN,
    FALL_STATE_RESET,
} fall_state_t;

/* 错误码 */
typedef enum {
    FALL_OK = 0,
    FALL_ERR_NOMEM = -1,
    FALL_ERR_TIMEOUT = -2,
    FALL_ERR_INVALID = -3,
    FALL_ERR_IO = -4,
    FALL_ERR_MODEL = -5,
    FALL_ERR_IPC = -6,
    FALL_ERR_FULL = -7,
    FALL_ERR_EMPTY = -8,
} fall_err_t;

/* 视频帧结构 (共享内存) */
typedef struct {
    uint8_t  frame_buf[FRAME_SIZE];
    uint32_t frame_id;
    uint64_t timestamp;
    uint16_t width;
    uint16_t height;
    uint8_t  format;
    volatile uint8_t is_valid;
} video_frame_t;

/* 环形帧缓冲 */
typedef struct {
    video_frame_t frames[RING_BUF_COUNT];
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
    rt_sem_t empty_sem;
    rt_sem_t full_sem;
    rt_mutex_t lock;
} frame_ring_buf_t;

/* 人体关键点 (17个 COCO 格式) */
typedef struct {
    float x;
    float y;
    float score;
} keypoint_t;

#define KEYPOINT_COUNT              17

/* 姿态数据 */
typedef struct {
    int person_count;
    struct {
        keypoint_t keypoints[KEYPOINT_COUNT];
        float bbox[4];           // x, y, w, h
        float confidence;
    } persons[8];                // 最多8个人
} pose_result_t;

/* Pose 环形缓冲区 (AI进程写, 视频进程读) */
#define POSE_BUF_COUNT  3

typedef struct {
    pose_result_t poses[POSE_BUF_COUNT];
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
    rt_mutex_t lock;
} pose_ring_buf_t;

/* 跌倒特征 */
typedef struct {
    float head_y;
    float hip_y;
    float ankle_y;
    float body_angle;
    float velocity;
    int   frame_count;
} fall_feature_t;

/* 事件信息 */
typedef struct {
    char     event_id[16];
    uint64_t timestamp;
    float    confidence;
    float    fall_angle;
    int      frame_id;
    fall_state_t state;
    char     video_path[128];
} fall_event_t;

/* 系统状态 */
typedef struct {
    uint8_t  cpu_usage;
    uint32_t mem_free;
    uint32_t mem_total;
    int      temperature;
    fall_state_t fall_state;
    uint32_t total_events;
    uint64_t uptime_ms;
} sys_status_t;

/* 配置参数 */
typedef struct {
    /* 视频 */
    uint16_t video_width;
    uint16_t video_height;
    uint8_t  video_fps;

    /* AI */
    float    confidence_threshold;
    int      confirm_frames;
    float    angle_threshold;
    float    velocity_threshold;
    int      cooldown_ms;

    /* 告警 */
    uint8_t  alert_enabled;
    uint8_t  buzzer_enabled;
    uint8_t  notification_enabled;
    uint8_t  recording_enabled;

    /* 网络 */
    char     wifi_ssid[32];
    char     wifi_password[64];
    char     mqtt_broker[64];
    uint16_t mqtt_port;
    char     mqtt_topic[64];
} system_config_t;

/* 工具宏 */
#define FALL_MS_TO_TICK(ms)         rt_tick_from_millisecond(ms)
#define FALL_TICK_TO_MS(tick)       ((tick) * 1000 / RT_TICK_PER_SECOND)
#define FALL_GET_TIMESTAMP()        rt_tick_get()
#define FALL_MIN(a, b)              ((a) < (b) ? (a) : (b))
#define FALL_MAX(a, b)              ((a) > (b) ? (a) : (b))

#endif /* __FALL_COMMON_H__ */
