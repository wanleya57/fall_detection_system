/**
 * fall_alert.h - 告警触发接口
 */
#ifndef __FALL_ALERT_H__
#define __FALL_ALERT_H__

#include "fall_common.h"

/**
 * 初始化告警模块
 */
fall_err_t fall_alert_init(void);

/**
 * 触发告警
 * @param event 事件信息
 */
void fall_alert_trigger(const fall_event_t *event);

/**
 * 停止告警
 */
void fall_alert_stop(void);

/**
 * 检查是否在冷却期
 */
int fall_alert_is_cooldown(void);

/**
 * 手动复位告警
 */
void fall_alert_reset(void);

/**
 * 反初始化
 */
void fall_alert_deinit(void);

#endif /* __FALL_ALERT_H__ */
