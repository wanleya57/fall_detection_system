/**
 * sys_init.h - 系统初始化接口
 */
#ifndef __SYS_INIT_H__
#define __SYS_INIT_H__

#include "fall_common.h"

/**
 * 系统初始化
 * - HAL 初始化
 * - 设备驱动加载
 * - 文件系统挂载
 * - IPC 对象创建
 * - 启动子进程
 * @return FALL_OK 成功
 */
fall_err_t sys_init(void);

/**
 * 启动子进程
 */
fall_err_t sys_start_processes(void);

/**
 * 停止所有子进程
 */
void sys_stop_processes(void);

/**
 * 获取系统运行时间 (ms)
 */
uint64_t sys_get_uptime_ms(void);

#endif /* __SYS_INIT_H__ */
