/**
 * video_record.c - 视频录制到 SD 卡
 */
#include "video_record.h"
#include "log.h"
#include <string.h>

#ifdef RT_USING_MOCK
#define RECORD_DIR          "./data/events"
#else
#define RECORD_DIR          "/sdcard/events"
#endif
#define RECORD_FILE_PREFIX  "event_"
#define RECORD_MAX_SECONDS  30
#define RECORD_FRAME_BUF_COUNT  10  /* 预录制缓冲 (~0.3s, 节省内存) */

typedef struct {
    rt_file_t fp;
    char file_path[128];
    char event_id[16];
    uint8_t recording;
    uint64_t start_tick;
    uint32_t frame_count;

    /* 预录制环形缓冲 */
    video_frame_t *pre_buf;
    uint32_t pre_write_idx;
    uint32_t pre_count;
} record_ctx_t;

static record_ctx_t g_record;

fall_err_t video_record_init(void)
{
    rt_memset(&g_record, 0, sizeof(g_record));

    /* 分配预录制缓冲 */
    g_record.pre_buf = rt_malloc(sizeof(video_frame_t) * RECORD_FRAME_BUF_COUNT);
    if (g_record.pre_buf == RT_NULL) {
        LOG_E(LOG_TAG_VIDEO, "Failed to allocate record pre-buffer");
        return FALL_ERR_NOMEM;
    }

    /* 确保目录存在 */
    rt_mkdir(RECORD_DIR);

    LOG_I(LOG_TAG_VIDEO, "Record module initialized");
    return FALL_OK;
}

fall_err_t video_record_start(const char *event_id)
{
    if (g_record.recording) return FALL_OK;
    if (event_id == RT_NULL) return FALL_ERR_INVALID;

    rt_strncpy(g_record.event_id, event_id, sizeof(g_record.event_id));

    /* 生成文件路径 */
    rt_snprintf(g_record.file_path, sizeof(g_record.file_path),
                "%s/%s%s.yuv", RECORD_DIR, RECORD_FILE_PREFIX, event_id);

    g_record.fp = rt_fopen(g_record.file_path, "wb");
    if (g_record.fp == RT_NULL) {
        LOG_E(LOG_TAG_VIDEO, "Failed to open record file: %s", g_record.file_path);
        return FALL_ERR_IO;
    }

    /* 写入预录制缓冲中的历史帧 */
    uint32_t start = (g_record.pre_write_idx + RECORD_FRAME_BUF_COUNT - g_record.pre_count)
                     % RECORD_FRAME_BUF_COUNT;
    for (uint32_t i = 0; i < g_record.pre_count; i++) {
        uint32_t idx = (start + i) % RECORD_FRAME_BUF_COUNT;
        rt_fwrite(g_record.pre_buf[idx].frame_buf, 1, FRAME_SIZE, g_record.fp);
        g_record.frame_count++;
    }

    g_record.recording = 1;
    g_record.start_tick = rt_tick_get();
    g_record.frame_count = g_record.pre_count;  /* 包含预录制帧 */

    LOG_I(LOG_TAG_VIDEO, "Recording started: %s (pre_buf=%d frames)",
          g_record.file_path, g_record.pre_count);
    return FALL_OK;
}

const char *video_record_stop(void)
{
    if (!g_record.recording) return RT_NULL;

    g_record.recording = 0;

    if (g_record.fp) {
        rt_fclose(g_record.fp);
        g_record.fp = RT_NULL;
    }

    LOG_I(LOG_TAG_VIDEO, "Recording stopped: %s (%d frames)",
          g_record.file_path, g_record.frame_count);

    return g_record.file_path;
}

void video_record_write_frame(video_frame_t *frame)
{
    if (frame == RT_NULL) return;

    /* 总是写入预录制缓冲 */
    if (g_record.pre_buf) {
        rt_memcpy(&g_record.pre_buf[g_record.pre_write_idx], frame, sizeof(video_frame_t));
        g_record.pre_write_idx = (g_record.pre_write_idx + 1) % RECORD_FRAME_BUF_COUNT;
        if (g_record.pre_count < RECORD_FRAME_BUF_COUNT) {
            g_record.pre_count++;
        }
    }

    /* 如果正在录制，写入文件 */
    if (g_record.recording && g_record.fp) {
        rt_fwrite(frame->frame_buf, 1, FRAME_SIZE, g_record.fp);
        g_record.frame_count++;

        /* 检查录制时长限制 */
        uint64_t elapsed = (rt_tick_get() - g_record.start_tick) * 1000 / RT_TICK_PER_SECOND;
        if (elapsed >= RECORD_MAX_SECONDS * 1000) {
            LOG_W(LOG_TAG_VIDEO, "Record max duration reached");
            video_record_stop();
        }
    }
}

int video_record_is_recording(void)
{
    return g_record.recording;
}

void video_record_deinit(void)
{
    video_record_stop();

    if (g_record.pre_buf) {
        rt_free(g_record.pre_buf);
        g_record.pre_buf = RT_NULL;
    }

    rt_memset(&g_record, 0, sizeof(g_record));
    LOG_I(LOG_TAG_VIDEO, "Record module deinitialized");
}
