/**
 * fall_storage.c - 事件记录存储实现
 */
#include "fall_storage.h"
#include "log.h"
#include <string.h>

#ifdef RT_USING_MOCK
#define EVENT_DIR           "./data/events"
#define EVENT_INDEX_FILE    "./data/events/index.dat"
#else
#define EVENT_DIR           "/sdcard/events"
#define EVENT_INDEX_FILE    "/sdcard/events/index.dat"
#endif
#define MAX_STORED_EVENTS   1000

typedef struct {
    uint32_t count;
    fall_event_t events[MAX_STORED_EVENTS];
} event_store_t;

static event_store_t *g_store = RT_NULL;

static fall_err_t load_event_index(void)
{
    if (g_store == RT_NULL) return FALL_ERR_NOMEM;

    rt_file_t fp = rt_fopen(EVENT_INDEX_FILE, "rb");
    if (fp == RT_NULL) {
        g_store->count = 0;
        return FALL_OK;
    }

    rt_fread(&g_store->count, sizeof(uint32_t), 1, fp);

    if (g_store->count > MAX_STORED_EVENTS) {
        g_store->count = MAX_STORED_EVENTS;
    }

    rt_fread(g_store->events, sizeof(fall_event_t), g_store->count, fp);
    rt_fclose(fp);

    LOG_I(LOG_TAG_AI, "Loaded %d events from index", g_store->count);
    return FALL_OK;
}

static fall_err_t save_event_index(void)
{
    if (g_store == RT_NULL) return FALL_ERR_NOMEM;

    rt_file_t fp = rt_fopen(EVENT_INDEX_FILE, "wb");
    if (fp == RT_NULL) {
        LOG_E(LOG_TAG_AI, "Failed to save event index");
        return FALL_ERR_IO;
    }

    rt_fwrite(&g_store->count, sizeof(uint32_t), 1, fp);
    rt_fwrite(g_store->events, sizeof(fall_event_t), g_store->count, fp);
    rt_fclose(fp);

    return FALL_OK;
}

fall_err_t fall_storage_init(void)
{
    g_store = rt_malloc(sizeof(event_store_t));
    if (g_store == RT_NULL) {
        LOG_E(LOG_TAG_AI, "Failed to allocate event store");
        return FALL_ERR_NOMEM;
    }

    rt_memset(g_store, 0, sizeof(event_store_t));

    /* 确保目录存在 */
    rt_mkdir(EVENT_DIR);

    /* 加载已有索引 */
    load_event_index();

    LOG_I(LOG_TAG_AI, "Storage module initialized, %d existing events", g_store->count);
    return FALL_OK;
}

fall_err_t fall_storage_save_event(const fall_event_t *event)
{
    if (event == RT_NULL || g_store == RT_NULL) return FALL_ERR_INVALID;

    /* 检查是否已满 */
    if (g_store->count >= MAX_STORED_EVENTS) {
        /* 覆盖最旧的记录 */
        rt_memmove(&g_store->events[0], &g_store->events[1],
                   sizeof(fall_event_t) * (MAX_STORED_EVENTS - 1));
        g_store->count = MAX_STORED_EVENTS - 1;
    }

    /* 添加新事件 */
    rt_memcpy(&g_store->events[g_store->count], event, sizeof(fall_event_t));
    g_store->count++;

    /* 保存索引 */
    save_event_index();

    LOG_I(LOG_TAG_AI, "Event saved: %s (total: %d)", event->event_id, g_store->count);
    return FALL_OK;
}

uint32_t fall_storage_get_event_count(void)
{
    return g_store ? g_store->count : 0;
}

int fall_storage_get_recent(fall_event_t *events, int max_count)
{
    if (events == RT_NULL || g_store == RT_NULL) return 0;

    int count = FALL_MIN(max_count, (int)g_store->count);
    int start = g_store->count - count;

    rt_memcpy(events, &g_store->events[start], sizeof(fall_event_t) * count);
    return count;
}

void fall_storage_clear_all(void)
{
    if (g_store == RT_NULL) return;

    g_store->count = 0;
    save_event_index();

    /* 删除事件文件 */
    rt_file_t fp = rt_fopen(EVENT_INDEX_FILE, "wb");
    if (fp) rt_fclose(fp);

    LOG_I(LOG_TAG_AI, "All events cleared");
}

void fall_storage_deinit(void)
{
    if (g_store) {
        save_event_index();
        rt_free(g_store);
        g_store = RT_NULL;
    }
    LOG_I(LOG_TAG_AI, "Storage module deinitialized");
}
