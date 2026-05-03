/**
 * process_manager - 系统管理与服务进程
 *
 * 职责:
 *   1. 系统初始化与进程生命周期管理
 *   2. 按键检测与手动复位
 *   3. 本地事件日志管理
 *   4. 系统状态监控
 */
#include <rtthread.h>
#include "sys_init.h"
#include "sys_monitor.h"
#include "key_handler.h"
#include "event_log.h"
#include "ipc_protocol.h"
#include "config.h"
#include "log.h"

#define THREAD_STACK_SIZE    4096
#define THREAD_PRIO_KEY      (RT_THREAD_PRIORITY_MAX - 5)
#define THREAD_PRIO_MONITOR  (RT_THREAD_PRIORITY_MAX - 7)

static volatile uint8_t g_running = 0;
static ipc_handle_t *g_ipc = RT_NULL;

/* 按键处理线程 */
static void thread_key_entry(void *param)
{
    (void)param;

    LOG_I(LOG_TAG_SYSTEM, "Key handler thread started");

    while (g_running) {
        key_event_t event = key_handler_poll();

        switch (event) {
        case KEY_EVENT_SHORT_PRESS:
            LOG_I(LOG_TAG_SYSTEM, "Key: short press -> reset alert");
            /* 手动复位告警 */
            ipc_send_cmd(MSG_TYPE_ALERT_RESET, RT_NULL, 0);
            break;

        case KEY_EVENT_LONG_PRESS:
            LOG_I(LOG_TAG_SYSTEM, "Key: long press -> toggle recording");
            /* 切换录制状态 */
            {
                uint8_t toggle = 0xFF;
                ipc_send_cmd(MSG_TYPE_CMD_RECORD, &toggle, 1);
            }
            break;

        case KEY_EVENT_DOUBLE_CLICK:
            LOG_I(LOG_TAG_SYSTEM, "Key: double click -> view logs");
            /* 显示最近日志到 OSD */
            {
                event_log_entry_t entries[5];
                int count = event_log_read_recent(entries, 5, 0);
                LOG_I(LOG_TAG_SYSTEM, "Recent events: %d", count);
                for (int i = 0; i < count; i++) {
                    LOG_I(LOG_TAG_SYSTEM, "  [%s] %s",
                          entries[i].event_id, entries[i].description);
                }
            }
            break;

        default:
            break;
        }

        rt_thread_msleep(20);
    }

    LOG_I(LOG_TAG_SYSTEM, "Key handler thread stopped");
}

/* 系统监控线程 */
static void thread_monitor_entry(void *param)
{
    (void)param;

    LOG_I(LOG_TAG_SYSTEM, "Monitor thread started");

    while (g_running) {
        sys_status_t status;
        sys_monitor_get_status(&status);

        /* 每30秒输出系统状态 */
        static int log_counter = 0;
        if (log_counter++ >= 6) {
            log_counter = 0;
            LOG_I(LOG_TAG_SYSTEM, "SYS: cpu=%d%% mem=%d/%dKB temp=%dC uptime=%lu",
                  status.cpu_usage,
                  status.mem_free / 1024,
                  status.mem_total / 1024,
                  status.temperature,
                  (unsigned long)(status.uptime_ms / 1000));
        }

        rt_thread_msleep(5000);
    }

    LOG_I(LOG_TAG_SYSTEM, "Monitor thread stopped");
}

/* 系统管理进程主函数 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* 系统初始化 */
    if (sys_init() != FALL_OK) {
        LOG_E(LOG_TAG_SYSTEM, "System init failed!");
        return -1;
    }

    LOG_I(LOG_TAG_SYSTEM, "=== System Manager Starting ===");

    /* 启动子进程 */
    sys_start_processes();

    /* 初始化按键 */
    key_handler_init();

    /* 初始化事件日志 */
    event_log_init();

    g_running = 1;
    g_ipc = ipc_get_handle();

    /* 创建管理线程 */
    rt_thread_t tid_key = rt_thread_create("mgr_key", thread_key_entry,
                                            RT_NULL, THREAD_STACK_SIZE,
                                            THREAD_PRIO_KEY, 10);
    rt_thread_t tid_mon = rt_thread_create("mgr_mon", thread_monitor_entry,
                                            RT_NULL, THREAD_STACK_SIZE,
                                            THREAD_PRIO_MONITOR, 10);

    if (tid_key) rt_thread_startup(tid_key);
    if (tid_mon) rt_thread_startup(tid_mon);

    LOG_I(LOG_TAG_SYSTEM, "System manager running, waiting for commands...");

    /* 主循环: 处理事件日志 */
    while (g_running) {
        event_log_entry_t event;
        if (ipc_recv_event_log(&event) == FALL_OK) {
            event_log_write(&event);

            /* 控制台输出 */
            LOG_I(LOG_TAG_SYSTEM, "Event: [%s] %s",
                  event.event_id, event.description);
        }

        rt_thread_msleep(100);
    }

    /* 清理 */
    sys_stop_processes();
    key_handler_deinit();
    event_log_deinit();
    sys_monitor_deinit();

    LOG_I(LOG_TAG_SYSTEM, "=== System Manager Exited ===");
    return 0;
}
