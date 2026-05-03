/**
 * fall_alert.c - 告警触发实现 (蜂鸣器/语音)
 */
#include "fall_alert.h"
#include "log.h"
#include <string.h>

#ifdef BSP_USING_K230
#include <k230_gpio.h>
#include <k230_i2s.h>
#endif

/* 蜂鸣器 GPIO 定义 (需根据实际硬件调整) */
#define BUZZER_GPIO_PIN     8

typedef struct {
    uint8_t  active;
    uint8_t  buzzer_enabled;
    uint64_t trigger_tick;
    rt_thread_t thread;
} alert_ctx_t;

static alert_ctx_t g_alert;

/* 告警音调序列 */
static const int alert_tones[] = {
    1000, 0, 1000, 0, 1500, 0, 1500, 0, 2000, 0,
};
#define TONE_COUNT  (sizeof(alert_tones) / sizeof(alert_tones[0]))

static void alert_thread_entry(void *param)
{
    (void)param;

    while (g_alert.active) {
        /* 蜂鸣器报警 */
        for (unsigned int i = 0; i < TONE_COUNT && g_alert.active; i++) {
            if (alert_tones[i] > 0) {
#ifdef BSP_USING_K230
                k230_gpio_set(BUZZER_GPIO_PIN, 1);
                rt_thread_millisecond_sleep(200);
                k230_gpio_set(BUZZER_GPIO_PIN, 0);
#else
                LOG_D(LOG_TAG_ALERT, "Buzzer ON freq=%d", alert_tones[i]);
                rt_thread_millisecond_sleep(200);
                LOG_D(LOG_TAG_ALERT, "Buzzer OFF");
#endif
            } else {
                rt_thread_millisecond_sleep(100);
            }
        }

        /* 周期间隔 */
        rt_thread_millisecond_sleep(500);
    }
}

fall_err_t fall_alert_init(void)
{
    rt_memset(&g_alert, 0, sizeof(g_alert));

#ifdef BSP_USING_K230
    k230_gpio_set_mode(BUZZER_GPIO_PIN, K230_GPIO_MODE_OUTPUT);
    k230_gpio_set(BUZZER_GPIO_PIN, 0);
#endif

    g_alert.buzzer_enabled = 1;

    LOG_I(LOG_TAG_ALERT, "Alert module initialized");
    return FALL_OK;
}

void fall_alert_trigger(const fall_event_t *event)
{
    if (event == RT_NULL || g_alert.active) return;

    g_alert.active = 1;
    g_alert.trigger_tick = rt_tick_get();

    LOG_E(LOG_TAG_ALERT, "!!! FALL ALERT TRIGGERED !!! event_id=%s conf=%.1f%%",
          event->event_id, event->confidence * 100);

    /* 启动告警线程 */
    g_alert.thread = rt_thread_create("alert", alert_thread_entry,
                                       RT_NULL, 4096,
                                       RT_THREAD_PRIORITY_MAX - 3, 10);
    if (g_alert.thread) {
        rt_thread_startup(g_alert.thread);
    }
}

void fall_alert_stop(void)
{
    g_alert.active = 0;

#ifdef BSP_USING_K230
    k230_gpio_set(BUZZER_GPIO_PIN, 0);
#endif

    LOG_I(LOG_TAG_ALERT, "Alert stopped");
}

int fall_alert_is_cooldown(void)
{
    if (!g_alert.active) return 0;

    uint64_t elapsed = (uint64_t)(rt_tick_get() - g_alert.trigger_tick) * 1000 / RT_TICK_PER_SECOND;
    return (elapsed < 10000) ? 1 : 0;  /* 10秒冷却 */
}

void fall_alert_reset(void)
{
    fall_alert_stop();
    LOG_I(LOG_TAG_ALERT, "Alert manually reset");
}

void fall_alert_deinit(void)
{
    fall_alert_stop();
    rt_memset(&g_alert, 0, sizeof(g_alert));
    LOG_I(LOG_TAG_ALERT, "Alert module deinitialized");
}
