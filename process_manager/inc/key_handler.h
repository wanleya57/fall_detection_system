/**
 * key_handler.h - 按键处理接口
 */
#ifndef __KEY_HANDLER_H__
#define __KEY_HANDLER_H__

#include "fall_common.h"

/* 按键定义 */
#define KEY_RESET_PIN       0   /* 复位按键 GPIO */
#define KEY_FUNCTION_PIN    1   /* 功能按键 GPIO */

/* 按键事件 */
typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SHORT_PRESS,      /* 短按 (<500ms) */
    KEY_EVENT_LONG_PRESS,       /* 长按 (>2s) */
    KEY_EVENT_DOUBLE_CLICK,     /* 双击 */
} key_event_t;

/**
 * 初始化按键模块
 */
fall_err_t key_handler_init(void);

/**
 * 获取按键事件 (非阻塞)
 * @return 按键事件
 */
key_event_t key_handler_poll(void);

/**
 * 反初始化
 */
void key_handler_deinit(void);

#endif /* __KEY_HANDLER_H__ */
