/**
 * fall_storage.h - 事件存储接口
 */
#ifndef __FALL_STORAGE_H__
#define __FALL_STORAGE_H__

#include "fall_common.h"

/**
 * 初始化存储模块
 */
fall_err_t fall_storage_init(void);

/**
 * 保存跌倒事件
 * @param event 事件信息
 * @return FALL_OK 成功
 */
fall_err_t fall_storage_save_event(const fall_event_t *event);

/**
 * 获取事件总数
 */
uint32_t fall_storage_get_event_count(void);

/**
 * 读取最近的事件
 * @param events 输出事件数组
 * @param max_count 最大数量
 * @return 实际读取数量
 */
int fall_storage_get_recent(fall_event_t *events, int max_count);

/**
 * 清除所有事件记录
 */
void fall_storage_clear_all(void);

/**
 * 反初始化
 */
void fall_storage_deinit(void);

#endif /* __FALL_STORAGE_H__ */
