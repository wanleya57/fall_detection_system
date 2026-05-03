/**
 * person_log.c - 人物检测状态 + 日志环形缓冲
 */
#include "person_log.h"
#include "log.h"
#include <string.h>

static person_log_t g_plog;

void person_log_init(void)
{
    rt_memset(&g_plog, 0, sizeof(g_plog));
    LOG_I(LOG_TAG_SYSTEM, "Person log initialized (entries=%d)", PERSON_LOG_SIZE);
}

void person_log_deinit(void)
{
    rt_memset(&g_plog, 0, sizeof(g_plog));
}

void person_log_record(int person_count, int action, float confidence,
                       uint32_t frame_id)
{
    g_plog.person_count = person_count;
    g_plog.action = action;
    g_plog.confidence = confidence;

    person_log_entry_t *e = &g_plog.entries[g_plog.write_idx % PERSON_LOG_SIZE];
    e->timestamp = rt_tick_get();
    e->person_count = person_count;
    e->action = action;
    e->confidence = confidence;
    e->frame_id = frame_id;

    const char *names[] = {"Stand", "Walk", "Sit", "Crouch", "FALL", "Lying"};
    if (action >= 0 && action < 6) {
        rt_strncpy(e->action_name, names[action], sizeof(e->action_name) - 1);
    } else {
        rt_strncpy(e->action_name, "N/A", sizeof(e->action_name) - 1);
    }

    g_plog.write_idx++;
    if (g_plog.count < PERSON_LOG_SIZE) g_plog.count++;
}

const person_log_t *person_log_get(void)
{
    return &g_plog;
}
