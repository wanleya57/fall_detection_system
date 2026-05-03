/**
 * ipc_protocol.c - IPC 通信协议实现
 */
#include "ipc_protocol.h"
#include "log.h"
#include <string.h>

/* 内存屏障兼容宏 */
#ifndef rt_hw_memory_barrier
#define rt_hw_memory_barrier()  __asm__ __volatile__ ("" ::: "memory")
#endif

static ipc_handle_t g_ipc_handle;

/* 环形缓冲区操作 (单生产者单消费者无锁) */
static inline uint32_t ring_next(uint32_t idx)
{
    return (idx + 1) % RING_BUF_COUNT;
}

fall_err_t ipc_init(void)
{
    rt_memset(&g_ipc_handle, 0, sizeof(g_ipc_handle));

    /* 创建消息队列 */
    g_ipc_handle.osd_mq = rt_mq_create(IPC_MSGQ_OSD_NAME,
                                         sizeof(osd_cmd_t),
                                         OSD_MQ_SIZE,
                                         RT_IPC_FLAG_FIFO);
    if (g_ipc_handle.osd_mq == RT_NULL) {
        LOG_E(LOG_TAG_IPC, "Failed to create OSD message queue");
        return FALL_ERR_NOMEM;
    }

    g_ipc_handle.cmd_mq = rt_mq_create(IPC_MSGQ_CMD_NAME,
                                         sizeof(cmd_data_t),
                                         CMD_MQ_SIZE,
                                         RT_IPC_FLAG_FIFO);
    if (g_ipc_handle.cmd_mq == RT_NULL) {
        LOG_E(LOG_TAG_IPC, "Failed to create CMD message queue");
        return FALL_ERR_NOMEM;
    }

    g_ipc_handle.event_mq = rt_mq_create(IPC_MSGQ_EVENT_NAME,
                                           sizeof(event_log_entry_t),
                                           EVENT_MQ_SIZE,
                                           RT_IPC_FLAG_FIFO);
    if (g_ipc_handle.event_mq == RT_NULL) {
        LOG_E(LOG_TAG_IPC, "Failed to create EVENT message queue");
        return FALL_ERR_NOMEM;
    }

    /* 创建信号量 */
    g_ipc_handle.frame_sem = rt_sem_create(IPC_SEM_FRAME_NAME, 0, RT_IPC_FLAG_FIFO);
    if (g_ipc_handle.frame_sem == RT_NULL) {
        LOG_E(LOG_TAG_IPC, "Failed to create frame semaphore");
        return FALL_ERR_NOMEM;
    }

    /* 创建互斥锁 */
    g_ipc_handle.mutex = rt_mutex_create(IPC_MUTEX_NAME, RT_IPC_FLAG_FIFO);
    if (g_ipc_handle.mutex == RT_NULL) {
        LOG_E(LOG_TAG_IPC, "Failed to create mutex");
        return FALL_ERR_NOMEM;
    }

    /* 分配并初始化帧环形缓冲区 */
    g_ipc_handle.ring_buf = (frame_ring_buf_t *)rt_malloc(sizeof(frame_ring_buf_t));
    if (g_ipc_handle.ring_buf == RT_NULL) {
        LOG_E(LOG_TAG_IPC, "Failed to allocate ring buffer");
        return FALL_ERR_NOMEM;
    }
    rt_memset(g_ipc_handle.ring_buf, 0, sizeof(frame_ring_buf_t));

    g_ipc_handle.ring_buf->empty_sem = rt_sem_create("ring_empty", RING_BUF_COUNT, RT_IPC_FLAG_FIFO);
    g_ipc_handle.ring_buf->full_sem = rt_sem_create("ring_full", 0, RT_IPC_FLAG_FIFO);
    g_ipc_handle.ring_buf->lock = rt_mutex_create("ring_lock", RT_IPC_FLAG_FIFO);

    if (g_ipc_handle.ring_buf->empty_sem == RT_NULL ||
        g_ipc_handle.ring_buf->full_sem == RT_NULL ||
        g_ipc_handle.ring_buf->lock == RT_NULL) {
        LOG_E(LOG_TAG_IPC, "Failed to create ring buffer sync objects");
        return FALL_ERR_NOMEM;
    }

    /* 分配并初始化 pose 环形缓冲区 */
    g_ipc_handle.pose_buf = (pose_ring_buf_t *)rt_malloc(sizeof(pose_ring_buf_t));
    if (g_ipc_handle.pose_buf == RT_NULL) {
        LOG_E(LOG_TAG_IPC, "Failed to allocate pose buffer");
        return FALL_ERR_NOMEM;
    }
    rt_memset(g_ipc_handle.pose_buf, 0, sizeof(pose_ring_buf_t));
    g_ipc_handle.pose_buf->lock = rt_mutex_create("pose_lock", RT_IPC_FLAG_FIFO);
    if (g_ipc_handle.pose_buf->lock == RT_NULL) {
        LOG_E(LOG_TAG_IPC, "Failed to create pose lock");
        return FALL_ERR_NOMEM;
    }

    LOG_I(LOG_TAG_IPC, "IPC initialized successfully");
    return FALL_OK;
}

void ipc_deinit(void)
{
    if (g_ipc_handle.osd_mq)
        rt_mq_delete(g_ipc_handle.osd_mq);
    if (g_ipc_handle.cmd_mq)
        rt_mq_delete(g_ipc_handle.cmd_mq);
    if (g_ipc_handle.event_mq)
        rt_mq_delete(g_ipc_handle.event_mq);
    if (g_ipc_handle.frame_sem)
        rt_sem_delete(g_ipc_handle.frame_sem);
    if (g_ipc_handle.mutex)
        rt_mutex_delete(g_ipc_handle.mutex);

    if (g_ipc_handle.ring_buf) {
        if (g_ipc_handle.ring_buf->empty_sem)
            rt_sem_delete(g_ipc_handle.ring_buf->empty_sem);
        if (g_ipc_handle.ring_buf->full_sem)
            rt_sem_delete(g_ipc_handle.ring_buf->full_sem);
        if (g_ipc_handle.ring_buf->lock)
            rt_mutex_delete(g_ipc_handle.ring_buf->lock);
        rt_free(g_ipc_handle.ring_buf);
    }

    if (g_ipc_handle.pose_buf) {
        if (g_ipc_handle.pose_buf->lock)
            rt_mutex_delete(g_ipc_handle.pose_buf->lock);
        rt_free(g_ipc_handle.pose_buf);
    }

    rt_memset(&g_ipc_handle, 0, sizeof(g_ipc_handle));
    LOG_I(LOG_TAG_IPC, "IPC deinitialized");
}

ipc_handle_t *ipc_get_handle(void)
{
    return &g_ipc_handle;
}

/* ---- 发送函数 ---- */

fall_err_t ipc_send_osd_cmd(osd_cmd_t *cmd)
{
    if (cmd == RT_NULL) return FALL_ERR_INVALID;

    rt_err_t ret = rt_mq_send(g_ipc_handle.osd_mq, cmd, sizeof(osd_cmd_t));
    if (ret != RT_EOK) {
        LOG_W(LOG_TAG_IPC, "OSD mq send failed: %d", ret);
        return FALL_ERR_FULL;
    }
    return FALL_OK;
}

fall_err_t ipc_send_fall_result(fall_result_t *result)
{
    if (result == RT_NULL) return FALL_ERR_INVALID;

    /* 跌倒结果通过事件消息队列发送 */
    event_log_entry_t entry;
    rt_memset(&entry, 0, sizeof(entry));
    rt_memcpy(entry.event_id, result->event_id, sizeof(entry.event_id));
    entry.timestamp = result->timestamp ? result->timestamp : rt_tick_get();
    entry.confidence = result->confidence;
    entry.state = result->state;

    if (result->state == FALL_STATE_CONFIRMED) {
        rt_snprintf(entry.description, sizeof(entry.description),
                     "Fall detected (conf=%.1f%%, angle=%.1f)",
                     result->confidence * 100, result->fall_angle);
    } else if (result->state == FALL_STATE_NORMAL) {
        rt_snprintf(entry.description, sizeof(entry.description),
                     "Fall cleared");
    }

    rt_err_t ret = rt_mq_send(g_ipc_handle.event_mq, &entry, sizeof(entry));
    if (ret != RT_EOK) {
        LOG_W(LOG_TAG_IPC, "Event mq send failed: %d", ret);
        return FALL_ERR_FULL;
    }

    /* 同时发送 OSD 指令 */
    osd_cmd_t osd;
    rt_memset(&osd, 0, sizeof(osd));

    if (result->state == FALL_STATE_CONFIRMED) {
        osd.show_flag = 1;
        osd.color = 0xFFFF0000;  /* 红色 */
        osd.duration_ms = 5000;
        rt_snprintf(osd.text, sizeof(osd.text), "Fall Detected!");
    } else {
        osd.show_flag = 0;
    }

    ipc_send_osd_cmd(&osd);

    return FALL_OK;
}

fall_err_t ipc_send_cmd(msg_type_t cmd, uint8_t *param, uint8_t param_len)
{
    cmd_data_t data;
    rt_memset(&data, 0, sizeof(data));
    data.cmd = cmd;
    data.param_len = param_len;
    if (param && param_len > 0) {
        rt_memcpy(data.param, param, FALL_MIN(param_len, sizeof(data.param)));
    }

    rt_err_t ret = rt_mq_send(g_ipc_handle.cmd_mq, &data, sizeof(data));
    if (ret != RT_EOK) {
        LOG_W(LOG_TAG_IPC, "CMD mq send failed: %d", ret);
        return FALL_ERR_FULL;
    }
    return FALL_OK;
}

fall_err_t ipc_send_event_log(event_log_entry_t *entry)
{
    if (entry == RT_NULL) return FALL_ERR_INVALID;

    rt_err_t ret = rt_mq_send(g_ipc_handle.event_mq, entry, sizeof(*entry));
    if (ret != RT_EOK) {
        LOG_W(LOG_TAG_IPC, "Event log mq send failed: %d", ret);
        return FALL_ERR_FULL;
    }
    return FALL_OK;
}

/* ---- 接收函数 ---- */

fall_err_t ipc_recv_osd_cmd(osd_cmd_t *cmd)
{
    if (cmd == RT_NULL) return FALL_ERR_INVALID;

    rt_err_t ret = rt_mq_recv(g_ipc_handle.osd_mq, cmd, sizeof(*cmd), 0);
    if (ret != RT_EOK) {
        return FALL_ERR_EMPTY;
    }
    return FALL_OK;
}

fall_err_t ipc_recv_fall_result(fall_result_t *result)
{
    if (result == RT_NULL) return FALL_ERR_INVALID;

    event_log_entry_t entry;
    rt_err_t ret = rt_mq_recv(g_ipc_handle.event_mq, &entry, sizeof(entry), 0);
    if (ret != RT_EOK) {
        return FALL_ERR_EMPTY;
    }

    rt_memcpy(result->event_id, entry.event_id, sizeof(result->event_id));
    result->timestamp = entry.timestamp;
    result->confidence = entry.confidence;
    result->state = entry.state;
    result->fall_angle = 0;

    return FALL_OK;
}

fall_err_t ipc_recv_cmd(cmd_data_t *cmd, uint32_t timeout_ms)
{
    if (cmd == RT_NULL) return FALL_ERR_INVALID;

    rt_int32_t timeout = (timeout_ms == 0) ? RT_WAITING_FOREVER
                                            : rt_tick_from_millisecond(timeout_ms);
    rt_err_t ret = rt_mq_recv(g_ipc_handle.cmd_mq, cmd, sizeof(*cmd), timeout);
    if (ret != RT_EOK) {
        return FALL_ERR_TIMEOUT;
    }
    return FALL_OK;
}

fall_err_t ipc_recv_event_log(event_log_entry_t *entry)
{
    if (entry == RT_NULL) return FALL_ERR_INVALID;

    rt_err_t ret = rt_mq_recv(g_ipc_handle.event_mq, entry, sizeof(*entry), 0);
    if (ret != RT_EOK) {
        return FALL_ERR_EMPTY;
    }
    return FALL_OK;
}

/* ---- 共享内存操作 ---- */

fall_err_t ipc_write_frame(video_frame_t *frame)
{
    if (frame == RT_NULL || g_ipc_handle.ring_buf == RT_NULL) return FALL_ERR_INVALID;

    uint32_t next = ring_next(g_ipc_handle.ring_buf->write_idx);

    /* 检查是否已满 (写索引追上读索引) */
    if (next == g_ipc_handle.ring_buf->read_idx) {
        return FALL_ERR_FULL;
    }

    /* 写入帧数据 */
    rt_memcpy(&g_ipc_handle.ring_buf->frames[g_ipc_handle.ring_buf->write_idx],
              frame, sizeof(video_frame_t));
    g_ipc_handle.ring_buf->frames[g_ipc_handle.ring_buf->write_idx].is_valid = 1;

    /* 更新写索引 (内存屏障保证可见性) */
    rt_hw_memory_barrier();
    g_ipc_handle.ring_buf->write_idx = next;

    /* 通知消费者 */
    rt_sem_release(g_ipc_handle.frame_sem);

    return FALL_OK;
}

fall_err_t ipc_read_frame(video_frame_t *frame)
{
    if (frame == RT_NULL || g_ipc_handle.ring_buf == RT_NULL) return FALL_ERR_INVALID;

    /* 等待新帧 */
    rt_err_t ret = rt_sem_take(g_ipc_handle.frame_sem,
                                rt_tick_from_millisecond(100));
    if (ret != RT_EOK) {
        return FALL_ERR_EMPTY;
    }

    /* 读取帧数据 */
    rt_memcpy(frame, &g_ipc_handle.ring_buf->frames[g_ipc_handle.ring_buf->read_idx],
              sizeof(video_frame_t));

    /* 标记帧已消费 */
    g_ipc_handle.ring_buf->frames[g_ipc_handle.ring_buf->read_idx].is_valid = 0;

    /* 更新读索引 */
    rt_hw_memory_barrier();
    g_ipc_handle.ring_buf->read_idx = ring_next(g_ipc_handle.ring_buf->read_idx);

    return FALL_OK;
}

void ipc_get_frame_stats(uint32_t *written, uint32_t *read)
{
    if (!g_ipc_handle.ring_buf) {
        if (written) *written = 0;
        if (read) *read = 0;
        return;
    }
    if (written) *written = g_ipc_handle.ring_buf->write_idx;
    if (read) *read = g_ipc_handle.ring_buf->read_idx;
}

/* ---- Pose 数据操作 ---- */

fall_err_t ipc_write_pose(pose_result_t *pose)
{
    if (!pose || !g_ipc_handle.pose_buf) return FALL_ERR_INVALID;

    pose_ring_buf_t *pb = g_ipc_handle.pose_buf;
    rt_mutex_take(pb->lock, RT_WAITING_FOREVER);

    uint32_t next = (pb->write_idx + 1) % POSE_BUF_COUNT;
    if (next == pb->read_idx) {
        /* 缓冲满, 覆盖最旧数据 */
        pb->read_idx = (pb->read_idx + 1) % POSE_BUF_COUNT;
    }

    rt_memcpy(&pb->poses[pb->write_idx], pose, sizeof(pose_result_t));
    rt_hw_memory_barrier();
    pb->write_idx = next;

    rt_mutex_release(pb->lock);
    return FALL_OK;
}

fall_err_t ipc_read_pose(pose_result_t *pose)
{
    if (!pose || !g_ipc_handle.pose_buf) return FALL_ERR_INVALID;

    pose_ring_buf_t *pb = g_ipc_handle.pose_buf;
    rt_mutex_take(pb->lock, RT_WAITING_FOREVER);

    if (pb->read_idx == pb->write_idx) {
        rt_mutex_release(pb->lock);
        return FALL_ERR_EMPTY;
    }

    /* 跳到最新数据 (丢弃中间帧) */
    while (((pb->read_idx + 1) % POSE_BUF_COUNT) != pb->write_idx) {
        pb->read_idx = (pb->read_idx + 1) % POSE_BUF_COUNT;
    }

    rt_memcpy(pose, &pb->poses[pb->read_idx], sizeof(pose_result_t));
    pb->read_idx = (pb->read_idx + 1) % POSE_BUF_COUNT;

    rt_mutex_release(pb->lock);
    return FALL_OK;
}
