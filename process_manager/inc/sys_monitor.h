/**
 * sys_monitor.h - 系统健康监控接口
 */
#ifndef __SYS_MONITOR_H__
#define __SYS_MONITOR_H__

#include "fall_common.h"

/**
 * 初始化系统监控
 */
fall_err_t sys_monitor_init(void);

/**
 * 获取系统状态
 * @param status 输出系统状态
 */
void sys_monitor_get_status(sys_status_t *status);

/**
 * 获取 CPU 使用率 (%)
 */
int sys_monitor_get_cpu_usage(void);

/**
 * 获取内存使用信息
 */
void sys_monitor_get_memory(uint32_t *free, uint32_t *total);

/**
 * 获取芯片温度 (摄氏度)
 */
int sys_monitor_get_temperature(void);

/**
 * 反初始化
 */
void sys_monitor_deinit(void);

#endif /* __SYS_MONITOR_H__ */
