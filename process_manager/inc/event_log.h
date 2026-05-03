/**
 * event_log.h - 事件日志接口
 */
#ifndef __EVENT_LOG_H__
#define __EVENT_LOG_H__

#include "fall_common.h"
#include "ipc_protocol.h"

/* 日志文件路径 */
#ifdef RT_USING_MOCK
#define EVENT_LOG_DIR       "./data/logs"
#define EVENT_LOG_FILE      "./data/logs/events.log"
#else
#define EVENT_LOG_DIR       "/sdcard/logs"
#define EVENT_LOG_FILE      "/sdcard/logs/events.log"
#endif

/**
 * 初始化事件日志模块
 */
fall_err_t event_log_init(void);

/**
 * 记录事件日志
 * @param entry 日志条目
 * @return FALL_OK 成功
 */
fall_err_t event_log_write(event_log_entry_t *entry);

/**
 * 读取最近的日志
 * @param entries 输出数组
 * @param max_count 最大数量
 * @param offset 偏移量 (从最新开始)
 * @return 实际读取数量
 */
int event_log_read_recent(event_log_entry_t *entries, int max_count, int offset);

/**
 * 获取日志总数
 */
uint32_t event_log_get_count(void);

/**
 * 清除所有日志
 */
void event_log_clear(void);

/**
 * 反初始化
 */
void event_log_deinit(void);

#endif /* __EVENT_LOG_H__ */
