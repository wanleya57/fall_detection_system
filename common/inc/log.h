/**
 * log.h - 日志接口
 */
#ifndef __LOG_H__
#define __LOG_H__

#ifdef RT_USING_MOCK
#include "rtthread_mock.h"
#else
#include <rtthread.h>
#endif

/* 日志级别 */
#define LOG_LVL_ERROR               0
#define LOG_LVL_WARN                1
#define LOG_LVL_INFO                2
#define LOG_LVL_DEBUG               3

/* 设置日志级别 */
void log_set_level(int level);

/* 获取日志级别 */
int log_get_level(void);

/* 基础日志函数 */
void log_error(const char *tag, const char *fmt, ...);
void log_warn(const char *tag, const char *fmt, ...);
void log_info(const char *tag, const char *fmt, ...);
void log_debug(const char *tag, const char *fmt, ...);

/* 便捷宏 */
#define LOG_E(tag, fmt, ...) log_error(tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) log_warn(tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) log_info(tag, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) log_debug(tag, fmt, ##__VA_ARGS__)

/* 初始化日志模块 */
void log_init(void);

#endif /* __LOG_H__ */
