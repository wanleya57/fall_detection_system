/**
 * event_log.c - 事件日志实现
 */
#include "event_log.h"
#include "log.h"
#include <string.h>

#define MAX_LOG_ENTRIES     2000
#define LOG_FLUSH_INTERVAL  5000  /* 5秒刷盘 */

typedef struct {
    uint32_t count;
    uint32_t flush_count;
    event_log_entry_t entries[MAX_LOG_ENTRIES];
    uint64_t last_flush_tick;
    uint8_t dirty;
} event_log_ctx_t;

static event_log_ctx_t g_log;

static fall_err_t load_log_file(void)
{
    rt_file_t fp = rt_fopen(EVENT_LOG_FILE, "rb");
    if (fp == RT_NULL) {
        g_log.count = 0;
        return FALL_OK;
    }

    rt_fread(&g_log.count, sizeof(uint32_t), 1, fp);
    if (g_log.count > MAX_LOG_ENTRIES) {
        g_log.count = MAX_LOG_ENTRIES;
    }

    rt_fread(g_log.entries, sizeof(event_log_entry_t), g_log.count, fp);
    rt_fclose(fp);

    LOG_I(LOG_TAG_SYSTEM, "Loaded %d log entries", g_log.count);
    return FALL_OK;
}

static fall_err_t flush_log_file(void)
{
    if (!g_log.dirty) return FALL_OK;

    rt_file_t fp = rt_fopen(EVENT_LOG_FILE, "wb");
    if (fp == RT_NULL) {
        LOG_E(LOG_TAG_SYSTEM, "Failed to flush log file");
        return FALL_ERR_IO;
    }

    rt_fwrite(&g_log.count, sizeof(uint32_t), 1, fp);

    uint32_t write_count = FALL_MIN(g_log.count, MAX_LOG_ENTRIES);
    rt_fwrite(g_log.entries, sizeof(event_log_entry_t), write_count, fp);
    rt_fclose(fp);

    g_log.dirty = 0;
    g_log.flush_count++;
    return FALL_OK;
}

fall_err_t event_log_init(void)
{
    rt_memset(&g_log, 0, sizeof(g_log));

    rt_mkdir(EVENT_LOG_DIR);
    load_log_file();

    g_log.last_flush_tick = rt_tick_get();
    LOG_I(LOG_TAG_SYSTEM, "Event log initialized (%d entries)", g_log.count);
    return FALL_OK;
}

fall_err_t event_log_write(event_log_entry_t *entry)
{
    if (entry == RT_NULL) return FALL_ERR_INVALID;

    /* 环形覆盖 */
    if (g_log.count >= MAX_LOG_ENTRIES) {
        rt_memmove(&g_log.entries[0], &g_log.entries[1],
                   sizeof(event_log_entry_t) * (MAX_LOG_ENTRIES - 1));
        g_log.count = MAX_LOG_ENTRIES - 1;
    }

    rt_memcpy(&g_log.entries[g_log.count], entry, sizeof(event_log_entry_t));
    g_log.count++;
    g_log.dirty = 1;

    /* 定期刷盘 */
    uint64_t now = rt_tick_get();
    if (now - g_log.last_flush_tick > RT_TICK_FROM_MILLISEC(LOG_FLUSH_INTERVAL)) {
        flush_log_file();
        g_log.last_flush_tick = now;
    }

    return FALL_OK;
}

int event_log_read_recent(event_log_entry_t *entries, int max_count, int offset)
{
    if (entries == RT_NULL || g_log.count == 0) return 0;

    int start = (int)g_log.count - offset - max_count;
    if (start < 0) start = 0;
    int count = FALL_MIN(max_count, (int)g_log.count - start);
    if (count <= 0) return 0;

    rt_memcpy(entries, &g_log.entries[start], sizeof(event_log_entry_t) * count);
    return count;
}

uint32_t event_log_get_count(void)
{
    return g_log.count;
}

void event_log_clear(void)
{
    g_log.count = 0;
    g_log.dirty = 1;
    flush_log_file();
    LOG_I(LOG_TAG_SYSTEM, "Event log cleared");
}

void event_log_deinit(void)
{
    flush_log_file();
    rt_memset(&g_log, 0, sizeof(g_log));
    LOG_I(LOG_TAG_SYSTEM, "Event log deinitialized");
}
