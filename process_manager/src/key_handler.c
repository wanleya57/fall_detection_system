/**
 * key_handler.c - 按键检测实现
 */
#include "key_handler.h"
#include "log.h"

#ifdef BSP_USING_K230
#include <k230_gpio.h>
#endif

#define KEY_DEBOUNCE_MS     50
#define KEY_LONG_PRESS_MS   2000
#define KEY_DOUBLE_CLICK_MS 300

typedef struct {
    uint8_t  initialized;
    uint8_t  last_state;
    uint8_t  waiting_release;   /* 正在等待释放 */
    uint8_t  long_press_fired;  /* 长按已触发，防止释放时重复 */
    uint64_t press_tick;
    uint8_t  press_count;
    uint64_t first_release_tick;
} key_ctx_t;

static key_ctx_t g_key;

static uint8_t read_key_pin(int pin)
{
#ifdef BSP_USING_K230
    return k230_gpio_get(pin);
#else
    (void)pin;
    return 0;  /* 模拟: 无按键按下 */
#endif
}

fall_err_t key_handler_init(void)
{
    rt_memset(&g_key, 0, sizeof(g_key));

#ifdef BSP_USING_K230
    k230_gpio_set_mode(KEY_RESET_PIN, K230_GPIO_MODE_INPUT);
    k230_gpio_set_mode(KEY_FUNCTION_PIN, K230_GPIO_MODE_INPUT);
#endif

    g_key.last_state = 0;
    g_key.initialized = 1;

    LOG_I(LOG_TAG_SYSTEM, "Key handler initialized");
    return FALL_OK;
}

key_event_t key_handler_poll(void)
{
    if (!g_key.initialized) return KEY_EVENT_NONE;

    uint8_t state = read_key_pin(KEY_RESET_PIN);
    uint64_t now = rt_tick_get();

    /* 检测按下边沿 */
    if (state && !g_key.last_state) {
        g_key.last_state = 1;
        g_key.press_tick = now;
        g_key.waiting_release = 1;
        g_key.long_press_fired = 0;
        return KEY_EVENT_NONE;
    }

    /* 检测释放边沿 */
    if (!state && g_key.last_state) {
        g_key.last_state = 0;
        g_key.waiting_release = 0;

        uint64_t press_duration = (now - g_key.press_tick) * 1000 / RT_TICK_PER_SECOND;

        /* 长按已在按住时触发过则跳过 */
        if (press_duration >= KEY_LONG_PRESS_MS && !g_key.long_press_fired) {
            g_key.long_press_fired = 0;
            return KEY_EVENT_LONG_PRESS;
        }
        g_key.long_press_fired = 0;

        /* 短按释放 */
        g_key.press_count++;

        if (g_key.press_count == 1) {
            g_key.first_release_tick = now;
            /* 等待可能的双击，暂不返回短按 */
        } else if (g_key.press_count == 2) {
            uint64_t interval = (now - g_key.first_release_tick) * 1000 / RT_TICK_PER_SECOND;
            if (interval < KEY_DOUBLE_CLICK_MS) {
                /* 确认双击 */
                g_key.press_count = 0;
                return KEY_EVENT_DOUBLE_CLICK;
            }
            /* 间隔过长，上一次算短按，这次重新计数 */
            g_key.press_count = 1;
            g_key.first_release_tick = now;
        }

        return KEY_EVENT_NONE;
    }

    /* 按住状态下检测长按（持续按下不释放） */
    if (state && g_key.waiting_release) {
        uint64_t duration = (now - g_key.press_tick) * 1000 / RT_TICK_PER_SECOND;
        if (duration >= KEY_LONG_PRESS_MS && !g_key.long_press_fired) {
            g_key.long_press_fired = 1;
            g_key.waiting_release = 0;  /* 防止重复触发 */
            return KEY_EVENT_LONG_PRESS;
        }
    }

    /* 短按双击等待超时: 超过窗口期仍未按第二次，确认为单击 */
    if (g_key.press_count == 1) {
        uint64_t elapsed = (now - g_key.first_release_tick) * 1000 / RT_TICK_PER_SECOND;
        if (elapsed > RT_TICK_PER_SECOND / 2) {
            g_key.press_count = 0;
            return KEY_EVENT_SHORT_PRESS;
        }
    }

    return KEY_EVENT_NONE;
}

void key_handler_deinit(void)
{
    rt_memset(&g_key, 0, sizeof(g_key));
    LOG_I(LOG_TAG_SYSTEM, "Key handler deinitialized");
}
