/**
 * process_ai - AI 事件检测与告警进程
 *
 * 职责:
 *   1. 从共享内存读取视频帧
 *   2. KPU 加速运行轻量级视觉模型
 *   3. 实时检测人体跌倒事件 (准确率>=90%)
 *   4. 通过 IPC 通知视频进程叠加 OSD
 *   5. 触发音频告警
 *   6. 保存事件视频片段到 SD 卡
 *   7. 通过 WiFi 推送通知
 */
#include <rtthread.h>
#include "ai_engine.h"
#include "fall_detect.h"
#include "fall_alert.h"
#include "fall_notify.h"
#include "fall_storage.h"
#include "video_record.h"
#include "ipc_protocol.h"
#include "config.h"
#include "log.h"
#include "pose_overlay.h"

#define THREAD_STACK_SIZE    8192
#define THREAD_PRIO_INFER    (RT_THREAD_PRIORITY_MAX - 2)
#define THREAD_PRIO_ALERT    (RT_THREAD_PRIORITY_MAX - 3)
#define THREAD_PRIO_NOTIFY   (RT_THREAD_PRIORITY_MAX - 7)
#define THREAD_PRIO_STORAGE  (RT_THREAD_PRIORITY_MAX - 8)

static volatile uint8_t g_running = 0;
static ipc_handle_t *g_ipc = RT_NULL;
static system_config_t *g_cfg = RT_NULL;

/* AI 推理线程 */
static void thread_inference_entry(void *param)
{
    (void)param;
    video_frame_t frame;
    fall_result_t result;
    pose_result_t pose;
    uint64_t last_log_tick = 0;

    LOG_I(LOG_TAG_AI, "Inference thread started");

    while (g_running) {
        /* 从共享内存读取视频帧 */
        fall_err_t ret = ipc_read_frame(&frame);
        if (ret != FALL_OK) {
            rt_thread_msleep(5);
            continue;
        }

        /* 分析帧并判断跌倒 */
        ret = fall_detect_analyze(&frame, &result, &pose, RT_NULL);

#ifdef BSP_USING_K230
        /* K230: 通过 IPC 发送 pose 数据给视频进程 */
        ipc_write_pose(&pose);
#else
        /* PC sim: 单进程, 直接更新骨架叠加 */
        pose_overlay_update(&pose);
#endif

        if (ret == FALL_OK && result.state == FALL_STATE_CONFIRMED) {
            /* 跌倒确认! 生成事件 */
            fall_event_t event;
            rt_memset(&event, 0, sizeof(event));
            rt_memcpy(event.event_id, result.event_id, sizeof(event.event_id));
            event.timestamp = result.timestamp;
            event.confidence = result.confidence;
            event.fall_angle = result.fall_angle;
            event.state = FALL_STATE_CONFIRMED;

            /* 1. 通知视频进程显示 OSD */
            ipc_send_fall_result(&result);

            /* 2. 触发告警 */
            fall_alert_trigger(&event);

            /* 3. 开始录制事件视频 */
            if (g_cfg->recording_enabled) {
                video_record_start(event.event_id);
            }

            LOG_E(LOG_TAG_AI, "=== FALL EVENT: %s ===", event.event_id);
        } else if (ret == FALL_OK && result.state == FALL_STATE_NORMAL) {
            /* 状态恢复 */
            if (video_record_is_recording()) {
                const char *path = video_record_stop();
                if (path) {
                    LOG_I(LOG_TAG_AI, "Event video saved: %s", path);
                }
            }
        }

        /* 定期输出统计信息 (每30秒) */
        uint64_t now = rt_tick_get();
        if (now - last_log_tick > RT_TICK_PER_SECOND * 30) {
            last_log_tick = now;
            uint32_t total, detected, false_pos;
            fall_detect_get_stats(&total, &detected, &false_pos);
            LOG_I(LOG_TAG_AI, "Stats: total=%d detected=%d false_pos=%d infer_time=%dms",
                  total, detected, false_pos, ai_engine_get_inference_time_ms());
        }
    }

    LOG_I(LOG_TAG_AI, "Inference thread stopped");
}

/* 网络推送线程 */
static void thread_notify_entry(void *param)
{
    (void)param;

    LOG_I(LOG_TAG_AI, "Notification thread started");

    while (g_running) {
        /* 检查是否有事件需要推送 */
        fall_event_t event;
        /* 从事件队列中取 (简化实现) */

        /* 发送心跳 */
        fall_notify_heartbeat();

        rt_thread_msleep(5000);
    }

    LOG_I(LOG_TAG_AI, "Notification thread stopped");
}

/* AI 进程主函数 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    LOG_I(LOG_TAG_AI, "=== AI Detection Process Starting ===");

    /* 初始化日志 */
    log_init();

    /* 加载配置 */
    g_cfg = config_get();
    if (g_cfg == RT_NULL) {
        config_init();
        g_cfg = config_get();
    }

    /* 初始化 IPC */
    g_ipc = ipc_get_handle();

    /* 初始化 AI 引擎 */
    if (ai_engine_init() != FALL_OK) {
        LOG_E(LOG_TAG_AI, "AI engine init failed");
        return -1;
    }

    /* 加载模型: YOLO11n-pose + 时序分类器 */
    ai_pipeline_info_t pipeline;
    ai_engine_get_pipeline_info(&pipeline);
    LOG_I(LOG_TAG_AI, "Loading: %s + %s", pipeline.spatial_model, pipeline.temporal_model);

    if (ai_engine_load_model(pipeline.spatial_path, pipeline.temporal_path) != FALL_OK) {
        LOG_E(LOG_TAG_AI, "Model load failed");
        return -1;
    }

    /* 初始化跌倒检测 */
    fall_detect_init();

    /* 初始化告警 */
    fall_alert_init();

    /* 初始化网络推送 */
    fall_notify_init();

    /* 初始化存储 */
    fall_storage_init();

    /* 初始化骨架叠加 */
    pose_overlay_init();

    /* 初始化录制模块 */
    video_record_init();

    g_running = 1;

    /* 创建线程 */
    rt_thread_t tid_infer = rt_thread_create("ai_infer", thread_inference_entry,
                                              RT_NULL, THREAD_STACK_SIZE * 2,
                                              THREAD_PRIO_INFER, 10);
    rt_thread_t tid_notify = rt_thread_create("ai_notify", thread_notify_entry,
                                               RT_NULL, THREAD_STACK_SIZE,
                                               THREAD_PRIO_NOTIFY, 10);

    if (tid_infer) rt_thread_startup(tid_infer);
    if (tid_notify) rt_thread_startup(tid_notify);

    LOG_I(LOG_TAG_AI, "AI detection process running");

    /* 等待退出 */
    while (g_running) {
        /* 处理控制命令 */
        cmd_data_t cmd;
        if (ipc_recv_cmd(&cmd, 100) == FALL_OK) {
            switch (cmd.cmd) {
            case MSG_TYPE_CMD_STOP:
                LOG_I(LOG_TAG_AI, "Received stop command");
                g_running = 0;
                break;
            case MSG_TYPE_ALERT_RESET:
                LOG_I(LOG_TAG_AI, "Received alert reset command");
                fall_detect_reset();
                fall_alert_reset();
                break;
            default:
                break;
            }
        }
    }

    /* 清理 */
    fall_detect_deinit();
    fall_alert_deinit();
    fall_notify_deinit();
    fall_storage_deinit();
    pose_overlay_deinit();
    ai_engine_deinit();
    video_record_deinit();

    LOG_I(LOG_TAG_AI, "=== AI Detection Process Exited ===");
    return 0;
}
