/**
 * sys_monitor.c - 系统健康监控实现
 */
#include "sys_monitor.h"
#include "log.h"

#ifdef BSP_USING_K230
#include <k230_system.h>
#include <k230_temp.h>
#endif

typedef struct {
    int cpu_usage;
    uint32_t mem_free;
    uint32_t mem_total;
    int temperature;
    uint8_t initialized;
} monitor_ctx_t;

static monitor_ctx_t g_monitor;

fall_err_t sys_monitor_init(void)
{
    rt_memset(&g_monitor, 0, sizeof(g_monitor));

    /* 获取内存信息 */
    sys_monitor_get_memory(&g_monitor.mem_free, &g_monitor.mem_total);

    g_monitor.initialized = 1;
    LOG_I(LOG_TAG_SYSTEM, "System monitor initialized");
    return FALL_OK;
}

int sys_monitor_get_cpu_usage(void)
{
#ifdef BSP_USING_K230
    g_monitor.cpu_usage = k230_get_cpu_usage();
#else
    /* 模拟 CPU 使用率 */
    g_monitor.cpu_usage = 45 + (rt_tick_get() % 20);
#endif
    return g_monitor.cpu_usage;
}

void sys_monitor_get_memory(uint32_t *free, uint32_t *total)
{
    if (free) *free = rt_memory_remaining();
    if (total) *total = 128 * 1024 * 1024;  /* 假设 128MB DDR */
}

int sys_monitor_get_temperature(void)
{
#ifdef BSP_USING_K230
    g_monitor.temperature = k230_temp_get();
#else
    /* 模拟温度 */
    g_monitor.temperature = 45 + (rt_tick_get() % 15);
#endif
    return g_monitor.temperature;
}

void sys_monitor_get_status(sys_status_t *status)
{
    if (status == RT_NULL) return;

    status->cpu_usage = sys_monitor_get_cpu_usage();
    sys_monitor_get_memory(&status->mem_free, &status->mem_total);
    status->temperature = sys_monitor_get_temperature();
    status->uptime_ms = (uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND;

    /* 获取跌倒状态 (需要外部模块支持) */
    status->fall_state = FALL_STATE_NORMAL;
    status->total_events = 0;
}

void sys_monitor_deinit(void)
{
    rt_memset(&g_monitor, 0, sizeof(g_monitor));
    LOG_I(LOG_TAG_SYSTEM, "System monitor deinitialized");
}
