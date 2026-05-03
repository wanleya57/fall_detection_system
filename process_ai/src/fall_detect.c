/**
 * fall_detect.c - 跌倒检测 (YOLO11n-pose + 时序分类器)
 *
 * 流程:
 *   1. YOLO11n-pose 输出 bbox + 17 关键点
 *   2. 提取 41 维特征向量
 *   3. 时序分类器 (LSTM/1D-CNN/TCN) 判定动作类别
 *   4. 动作 = FALLING → 状态机确认 → 告警
 *
 * 优势: 区分「真摔倒」和「蹲下/坐低」，大幅降低误报
 */
#include "fall_detect.h"
#include "ai_engine.h"
#include "log.h"
#include <string.h>
#include <math.h>

#define MAX_PERSONS     8

typedef struct {
    fall_state_t state;
    int confirm_count;
    uint64_t state_enter_tick;
    uint64_t last_detect_tick;

    uint32_t total_frames;
    uint32_t detected_frames;
    uint32_t false_positives;
} detect_ctx_t;

static detect_ctx_t g_detect;
static system_config_t *g_cfg = RT_NULL;

fall_err_t fall_detect_init(void)
{
    rt_memset(&g_detect, 0, sizeof(g_detect));
    g_detect.state = FALL_STATE_NORMAL;
    g_cfg = config_get();

    LOG_I(LOG_TAG_AI, "Fall detection initialized (pose + temporal classifier)");
    return FALL_OK;
}

fall_err_t fall_detect_analyze(video_frame_t *frame, fall_result_t *result,
                               pose_result_t *pose_out, int *ran_detect)
{
    if (!frame || !result) return FALL_ERR_INVALID;

    float detections[MAX_PERSONS][5];
    int det_count = 0;
    pose_result_t pose;

    rt_memset(result, 0, sizeof(fall_result_t));

    /* Stage 1: YOLO11n-pose → bbox + keypoints */
    int detect_ran = 0;
    ai_engine_detect_and_pose(frame, detections, MAX_PERSONS, &det_count, &pose, &detect_ran);
    g_detect.total_frames++;

    result->person_count = pose.person_count;

    /* DEBUG: 每 300 帧打印一次检测结果 */
    if (g_detect.total_frames % 300 == 0) {
        LOG_D(LOG_TAG_AI, "analyze #%lu: det=%d person_cnt=%d kp0=(%.0f,%.0f) sc=%.2f",
              (unsigned long)g_detect.total_frames, det_count, pose.person_count,
              pose.persons[0].keypoints[0].x, pose.persons[0].keypoints[0].y,
              pose.persons[0].keypoints[0].score);
    }

    /* 输出姿态数据给骨架叠加 */
    if (pose_out) rt_memcpy(pose_out, &pose, sizeof(pose_result_t));
    if (ran_detect) *ran_detect = detect_ran;

    if (det_count == 0 || pose.person_count == 0) {
        if (g_detect.state == FALL_STATE_FALLING) {
            g_detect.confirm_count = 0;
            g_detect.state = FALL_STATE_NORMAL;
        }
        return FALL_ERR_EMPTY;
    }

    /* Stage 2: 提取特征 → 时序分类 */
    float feat[FEATURE_PER_FRAME];
    ai_engine_extract_feature(&pose, detections, det_count, feat);

    float action_probs[ACTION_COUNT];
    action_class_t action = ai_engine_classify_action(feat, action_probs);

    result->action = (int)action;
    result->confidence = action_probs[action];

    /* Stage 3: 状态机 */
    int is_falling = (action == ACTION_FALLING);

    switch (g_detect.state) {
    case FALL_STATE_NORMAL:
        if (is_falling) {
            g_detect.confirm_count = 1;
            g_detect.state = FALL_STATE_FALLING;
            g_detect.state_enter_tick = rt_tick_get();
            LOG_D(LOG_TAG_AI, "Action=FALLING, confirming...");
        }
        break;

    case FALL_STATE_FALLING:
        if (is_falling) {
            g_detect.confirm_count++;
            /* 时序分类器已经看过多帧，确认 3 帧即可 */
            if (g_detect.confirm_count >= 3) {
                g_detect.state = FALL_STATE_CONFIRMED;
                g_detect.state_enter_tick = rt_tick_get();
                g_detect.detected_frames++;

                result->confidence = detections[0][4];
                result->fall_angle = feat[40] * 90.0f;
                result->frame_id = frame->frame_id;
                result->state = FALL_STATE_CONFIRMED;
                result->timestamp = rt_tick_get();

                rt_snprintf(result->event_id, sizeof(result->event_id),
                            "E%08X", (uint32_t)result->timestamp);

                LOG_I(LOG_TAG_AI, "Fall CONFIRMED! action=%s conf=%.2f angle=%.1f",
                      ai_engine_action_name(action), result->confidence, result->fall_angle);

                g_detect.last_detect_tick = rt_tick_get();
                return FALL_OK;
            }
        } else {
            g_detect.confirm_count = 0;
            g_detect.state = FALL_STATE_NORMAL;
            g_detect.false_positives++;
        }
        break;

    case FALL_STATE_CONFIRMED:
        {
            uint64_t elapsed = (rt_tick_get() - g_detect.state_enter_tick) * 1000 / RT_TICK_PER_SECOND;
            if (elapsed >= (uint64_t)g_cfg->cooldown_ms) {
                g_detect.state = FALL_STATE_COOLDOWN;
                g_detect.state_enter_tick = rt_tick_get();
                LOG_I(LOG_TAG_AI, "Fall cooldown started");
            }
        }
        break;

    case FALL_STATE_COOLDOWN:
        {
            uint64_t elapsed = (rt_tick_get() - g_detect.state_enter_tick) * 1000 / RT_TICK_PER_SECOND;
            if (elapsed >= (uint64_t)g_cfg->cooldown_ms) {
                g_detect.state = FALL_STATE_NORMAL;
                g_detect.confirm_count = 0;
                LOG_I(LOG_TAG_AI, "Fall alert cleared");
                result->state = FALL_STATE_NORMAL;
                return FALL_OK;
            }
        }
        break;

    case FALL_STATE_RESET:
        g_detect.state = FALL_STATE_NORMAL;
        g_detect.confirm_count = 0;
        break;
    }

    return FALL_ERR_EMPTY;
}

fall_state_t fall_detect_get_state(void)
{
    return g_detect.state;
}

void fall_detect_reset(void)
{
    g_detect.state = FALL_STATE_RESET;
    g_detect.confirm_count = 0;
    LOG_I(LOG_TAG_AI, "Fall detection reset");
}

void fall_detect_get_stats(uint32_t *total, uint32_t *detected, uint32_t *fp)
{
    if (total) *total = g_detect.total_frames;
    if (detected) *detected = g_detect.detected_frames;
    if (fp) *fp = g_detect.false_positives;
}

void fall_detect_deinit(void)
{
    rt_memset(&g_detect, 0, sizeof(g_detect));
    LOG_I(LOG_TAG_AI, "Fall detection deinitialized");
}
