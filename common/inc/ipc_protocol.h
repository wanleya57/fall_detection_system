/**
 * ipc_protocol.h - IPC 通信协议定义
 */
#ifndef __IPC_PROTOCOL_H__
#define __IPC_PROTOCOL_H__

#include "fall_common.h"

/* IPC 对象名称 */
#define IPC_SHM_NAME                "fall_shm"
#define IPC_MSGQ_OSD_NAME           "fall_osd_mq"
#define IPC_MSGQ_CMD_NAME           "fall_cmd_mq"
#define IPC_MSGQ_EVENT_NAME         "fall_event_mq"
#define IPC_SEM_FRAME_NAME          "fall_frame_sem"
#define IPC_MUTEX_NAME              "fall_mutex"

/* 消息队列容量 */
#define OSD_MQ_SIZE                 16
#define CMD_MQ_SIZE                 8
#define EVENT_MQ_SIZE               16

/* 消息类型 */
typedef enum {
    MSG_TYPE_FRAME_READY    = 0x01,
    MSG_TYPE_FALL_DETECTED  = 0x02,
    MSG_TYPE_FALL_CLEARED   = 0x03,
    MSG_TYPE_OSD_UPDATE     = 0x04,
    MSG_TYPE_OSD_CLEAR      = 0x05,
    MSG_TYPE_ALERT_TRIGGER  = 0x06,
    MSG_TYPE_ALERT_RESET    = 0x07,
    MSG_TYPE_SYS_STATUS     = 0x08,
    MSG_TYPE_EVENT_LOG      = 0x09,
    MSG_TYPE_CMD_START      = 0x10,
    MSG_TYPE_CMD_STOP       = 0x11,
    MSG_TYPE_CMD_CONFIG     = 0x12,
    MSG_TYPE_CMD_RECORD     = 0x13,
    MSG_TYPE_CMD_SNAPSHOT   = 0x14,
    MSG_TYPE_CMD_REBOOT     = 0x1F,
} msg_type_t;

/* 消息头 */
typedef struct {
    msg_type_t type;
    uint32_t   seq;
    uint64_t   timestamp;
    uint16_t   data_len;
} ipc_msg_header_t;

/* OSD 叠加指令 */
typedef struct {
    int   x;
    int   y;
    int   width;
    int   height;
    uint8_t show_flag;
    char  text[64];
    uint32_t color;
    uint32_t duration_ms;
} osd_cmd_t;

/* 跌倒检测结果 */
typedef struct {
    float    confidence;
    float    fall_angle;
    int      frame_id;
    int      duration_ms;
    uint64_t timestamp;
    char     event_id[16];
    fall_state_t state;
    int      action;           /* action_class_t: 当前动作类别 */
    int      person_count;     /* 检测到的人数 */
} fall_result_t;

/* 控制命令 */
typedef struct {
    msg_type_t cmd;
    uint8_t    param_len;
    uint8_t    param[32];
} cmd_data_t;

/* 事件日志条目 */
typedef struct {
    char     event_id[16];
    uint64_t timestamp;
    fall_state_t state;
    float    confidence;
    char     description[64];
} event_log_entry_t;

/* IPC 管理句柄 */
typedef struct {
    /* 共享内存 */
    rt_lwp_t lwp;
    frame_ring_buf_t *ring_buf;
    pose_ring_buf_t *pose_buf;          /* pose 环形缓冲 (AI→视频) */

    /* 消息队列 */
    rt_mq_t osd_mq;
    rt_mq_t cmd_mq;
    rt_mq_t event_mq;

    /* 同步对象 */
    rt_sem_t frame_sem;
    rt_mutex_t mutex;
} ipc_handle_t;

/**
 * 初始化 IPC 通信
 * @return FALL_OK 成功, 其他失败
 */
fall_err_t ipc_init(void);

/**
 * 销毁 IPC 通信
 */
void ipc_deinit(void);

/**
 * 获取 IPC 句柄
 */
ipc_handle_t *ipc_get_handle(void);

/* ---- 发送函数 ---- */

/**
 * 发送 OSD 叠加指令
 */
fall_err_t ipc_send_osd_cmd(osd_cmd_t *cmd);

/**
 * 发送跌倒检测结果
 */
fall_err_t ipc_send_fall_result(fall_result_t *result);

/**
 * 发送控制命令
 */
fall_err_t ipc_send_cmd(msg_type_t cmd, uint8_t *param, uint8_t param_len);

/**
 * 发送事件日志
 */
fall_err_t ipc_send_event_log(event_log_entry_t *entry);

/* ---- 接收函数 ---- */

/**
 * 接收 OSD 指令 (非阻塞)
 * @return FALL_OK 有消息, FALL_ERR_EMPTY 无消息
 */
fall_err_t ipc_recv_osd_cmd(osd_cmd_t *cmd);

/**
 * 接收跌倒结果 (非阻塞)
 */
fall_err_t ipc_recv_fall_result(fall_result_t *result);

/**
 * 接收控制命令 (阻塞)
 * @param timeout_ms 超时时间, 0=永久等待
 */
fall_err_t ipc_recv_cmd(cmd_data_t *cmd, uint32_t timeout_ms);

/**
 * 接收事件日志 (非阻塞)
 */
fall_err_t ipc_recv_event_log(event_log_entry_t *entry);

/* ---- 共享内存操作 ---- */

/**
 * 写入视频帧到环形缓冲
 * @return FALL_OK 成功, FALL_ERR_FULL 缓冲已满
 */
fall_err_t ipc_write_frame(video_frame_t *frame);

/**
 * 从环形缓冲读取视频帧
 * @return FALL_OK 成功, FALL_ERR_EMPTY 无帧
 */
fall_err_t ipc_read_frame(video_frame_t *frame);

/**
 * 获取当前帧缓冲状态
 */
void ipc_get_frame_stats(uint32_t *written, uint32_t *read);

/* ---- Pose 数据操作 (AI进程→视频进程) ---- */

/**
 * 写入 pose 数据到环形缓冲 (AI 进程调用)
 * @return FALL_OK 成功, FALL_ERR_FULL 缓冲已满
 */
fall_err_t ipc_write_pose(pose_result_t *pose);

/**
 * 从环形缓冲读取最新 pose 数据 (视频进程调用, 非阻塞)
 * @return FALL_OK 有数据, FALL_ERR_EMPTY 无新数据
 */
fall_err_t ipc_read_pose(pose_result_t *pose);

#endif /* __IPC_PROTOCOL_H__ */
