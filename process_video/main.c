/**
 * process_video - 视频采集与显示进程
 *
 * 职责:
 *   1. 实时捕获摄像头数据 (720P@30fps)
 *   2. TFT 屏幕预览
 *   3. 接收 AI 进程的 OSD 叠加指令
 *   4. 可选 RTSP 远程视频查看
 *   5. 可选视频录制到 SD 卡
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "video_capture.h"
#include "video_display.h"
#include "video_osd.h"
#include "video_rtsp.h"
#include "video_record.h"
#include "ipc_protocol.h"
#include "config.h"
#include "log.h"
#include "pose_overlay.h"

/* 线程栈大小 */
#define THREAD_STACK_SIZE    8192
/* 线程优先级 */
#define THREAD_PRIO_CAPTURE  (RT_THREAD_PRIORITY_MAX - 2)
#define THREAD_PRIO_DISPLAY  (RT_THREAD_PRIORITY_MAX - 4)
#define THREAD_PRIO_OSD      (RT_THREAD_PRIORITY_MAX - 4)
#define THREAD_PRIO_RTSP     (RT_THREAD_PRIORITY_MAX - 6)
#define THREAD_PRIO_RECORD   (RT_THREAD_PRIORITY_MAX - 7)

/* 全局状态 */
static volatile uint8_t g_running = 0;
static ipc_handle_t *g_ipc = RT_NULL;
static system_config_t *g_cfg = RT_NULL;

/* 采集线程 */
static void thread_capture_entry(void *param)
{
    (void)param;
    video_frame_t frame;
    fall_err_t ret;

    LOG_I(LOG_TAG_VIDEO, "Capture thread started");

    while (g_running) {
        ret = video_capture_get_frame(&frame);
        if (ret != FALL_OK) {
            rt_thread_msleep(10);
            continue;
        }

        /* 写入共享内存供 AI 进程读取 */
        ret = ipc_write_frame(&frame);
        if (ret == FALL_ERR_FULL) {
            /* 缓冲满，跳过本帧 */
            LOG_D(LOG_TAG_VIDEO, "Frame buffer full, dropping frame");
        }

        /* 同时推送到 RTSP 客户端 */
        if (video_rtsp_get_clients() > 0) {
            video_rtsp_push_frame(&frame);
        }

        /* 写入录制缓冲 */
        if (g_cfg->recording_enabled) {
            video_record_write_frame(&frame);
        }

        video_capture_release_frame(&frame);
    }

    LOG_I(LOG_TAG_VIDEO, "Capture thread stopped");
}

/* 显示线程 */
static void thread_display_entry(void *param)
{
    (void)param;
    video_frame_t frame;
    pose_result_t pose;
    osd_cmd_t osd_cmd;

    LOG_I(LOG_TAG_VIDEO, "Display thread started");

    while (g_running) {
        /* 从共享内存读取最新帧用于显示 */
        fall_err_t ret = ipc_read_frame(&frame);
        if (ret == FALL_OK) {
            /* 读取 AI 进程的 pose 数据 */
            if (ipc_read_pose(&pose) == FALL_OK) {
                pose_overlay_update(&pose);
            }

            /* 在 LCD 显示前绘制骨架叠加 */
            pose_overlay_draw(frame.frame_buf, frame.width, frame.height);

            /* 渲染 OSD */
            video_osd_render(frame.frame_buf);

            /* 显示到 LCD */
            video_display_frame(&frame);
        }

        /* 处理 OSD 指令 */
        while (ipc_recv_osd_cmd(&osd_cmd) == FALL_OK) {
            video_osd_process_cmd(&osd_cmd);
        }

        rt_thread_msleep(33);  /* ~30fps */
    }

    LOG_I(LOG_TAG_VIDEO, "Display thread stopped");
}

/* RTSP 服务线程 */
static void thread_rtsp_entry(void *param)
{
    (void)param;

    LOG_I(LOG_TAG_VIDEO, "RTSP service thread started");

    /* 启动 RTSP 服务 */
    video_rtsp_start(8554);

    while (g_running) {
        rt_thread_msleep(1000);
    }

    video_rtsp_stop();
    LOG_I(LOG_TAG_VIDEO, "RTSP service thread stopped");
}

/* 视频进程主函数 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    LOG_I(LOG_TAG_VIDEO, "=== Video Capture Process Starting ===");

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

    /* 初始化摄像头 */
    if (video_capture_init(g_cfg->video_width, g_cfg->video_height,
                           g_cfg->video_fps) != FALL_OK) {
        LOG_E(LOG_TAG_VIDEO, "Camera init failed, exiting");
        return -1;
    }

    /* 初始化显示 */
    if (video_display_init() != FALL_OK) {
        LOG_E(LOG_TAG_VIDEO, "Display init failed, exiting");
        video_capture_deinit();
        return -1;
    }

    /* 初始化 OSD */
    video_osd_init();

    /* 初始化骨架叠加 */
    pose_overlay_init();

    /* 初始化录制 */
    if (g_cfg->recording_enabled) {
        video_record_init();
    }

    /* 启动摄像头采集 */
    video_capture_start();

    /* 显示启动画面 */
    video_display_clear();

    g_running = 1;

    /* 创建线程 */
    rt_thread_t tid_capture = rt_thread_create("vid_cap", thread_capture_entry,
                                                RT_NULL, THREAD_STACK_SIZE,
                                                THREAD_PRIO_CAPTURE, 10);
    rt_thread_t tid_display = rt_thread_create("vid_dis", thread_display_entry,
                                                RT_NULL, THREAD_STACK_SIZE,
                                                THREAD_PRIO_DISPLAY, 10);
    rt_thread_t tid_rtsp = RT_NULL;
    if (g_cfg->notification_enabled) {
        tid_rtsp = rt_thread_create("vid_rtsp", thread_rtsp_entry,
                                     RT_NULL, THREAD_STACK_SIZE,
                                     THREAD_PRIO_RTSP, 10);
    }

    if (tid_capture) rt_thread_startup(tid_capture);
    if (tid_display) rt_thread_startup(tid_display);
    if (tid_rtsp) rt_thread_startup(tid_rtsp);

    LOG_I(LOG_TAG_VIDEO, "Video process running");

    /* 等待退出信号 */
    while (g_running) {
        rt_thread_msleep(100);
    }

    /* 清理 */
    video_capture_stop();
    video_capture_deinit();
    video_display_deinit();
    video_osd_deinit();
    pose_overlay_deinit();
    video_record_deinit();

    LOG_I(LOG_TAG_VIDEO, "=== Video Capture Process Exited ===");
    return 0;
}
