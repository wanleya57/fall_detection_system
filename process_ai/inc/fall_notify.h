/**
 * fall_notify.h - 网络推送接口
 */
#ifndef __FALL_NOTIFY_H__
#define __FALL_NOTIFY_H__

#include "fall_common.h"

/**
 * 初始化网络推送模块
 */
fall_err_t fall_notify_init(void);

/**
 * 推送跌倒事件
 * @param event 事件信息
 * @return FALL_OK 成功
 */
fall_err_t fall_notify_send(const fall_event_t *event);

/**
 * 发送心跳
 */
void fall_notify_heartbeat(void);

/**
 * 反初始化
 */
void fall_notify_deinit(void);

#endif /* __FALL_NOTIFY_H__ */
