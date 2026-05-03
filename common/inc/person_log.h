/**
 * person_log.h - 人物检测状态 + 日志环形缓冲
 *
 * 线程安全: AI 线程写, HTTP 线程读 (单写单读, 简单锁)
 * 内存占用: ~1.5KB (适合 K230)
 */
#ifndef __PERSON_LOG_H__
#define __PERSON_LOG_H__

#include "fall_common.h"
#include "ai_engine.h"

#define PERSON_LOG_SIZE     32

typedef struct {
    uint64_t timestamp;
    int      person_count;
    int      action;           /* action_class_t */
    float    confidence;
    uint32_t frame_id;
    char     action_name[16];
} person_log_entry_t;

typedef struct {
    /* 实时状态 */
    int      person_count;
    int      action;
    float    confidence;

    /* 日志环形缓冲 */
    person_log_entry_t entries[PERSON_LOG_SIZE];
    int write_idx;
    int count;
} person_log_t;

void person_log_init(void);
void person_log_deinit(void);

/* AI 线程调用: 记录一帧的人物状态 */
void person_log_record(int person_count, int action, float confidence,
                       uint32_t frame_id);

/* HTTP 线程调用: 获取当前状态和日志 */
const person_log_t *person_log_get(void);

#endif /* __PERSON_LOG_H__ */
