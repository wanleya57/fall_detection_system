/**
 * log.c - 日志实现
 */
#include "log.h"
#include "fall_common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int g_log_level = LOG_LVL_INFO;

static const char *level_str[] = {
    "[E]",
    "[W]",
    "[I]",
    "[D]",
};

static void log_output(int level, const char *tag, const char *fmt, va_list args)
{
    if (level > g_log_level) return;

    char buf[256];
    int offset = 0;

    /* 时间戳 */
    rt_tick_t tick = rt_tick_get();
    unsigned int sec = tick / RT_TICK_PER_SECOND;
    unsigned int ms = (tick % RT_TICK_PER_SECOND) * 1000 / RT_TICK_PER_SECOND;
    offset += rt_snprintf(buf + offset, sizeof(buf) - offset, "[%u.%03u] ", sec, ms);

    /* 级别 */
    offset += rt_snprintf(buf + offset, sizeof(buf) - offset, "%s", level_str[level]);

    /* 标签 */
    offset += rt_snprintf(buf + offset, sizeof(buf) - offset, "[%s] ", tag);

    /* 消息 */
    offset += rt_vsnprintf(buf + offset, sizeof(buf) - offset, fmt, args);

    /* 换行 */
    if (offset < (int)sizeof(buf) - 1) {
        buf[offset] = '\n';
        buf[offset + 1] = '\0';
    }

    rt_kprintf("%s", buf);
}

void log_set_level(int level)
{
    if (level >= LOG_LVL_ERROR && level <= LOG_LVL_DEBUG) {
        g_log_level = level;
    }
}

int log_get_level(void)
{
    return g_log_level;
}

void log_error(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_output(LOG_LVL_ERROR, tag, fmt, args);
    va_end(args);
}

void log_warn(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_output(LOG_LVL_WARN, tag, fmt, args);
    va_end(args);
}

void log_info(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_output(LOG_LVL_INFO, tag, fmt, args);
    va_end(args);
}

void log_debug(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_output(LOG_LVL_DEBUG, tag, fmt, args);
    va_end(args);
}

void log_init(void)
{
    log_set_level(LOG_LVL_DEBUG);
    LOG_I(LOG_TAG_SYSTEM, "Log system initialized, level=%d", g_log_level);
}
