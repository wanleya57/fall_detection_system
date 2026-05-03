/**
 * main_pc.c - PC 端模拟主程序 (Linux/WSL)
 *
 * 将三个进程作为线程运行在同一进程中，验证代码逻辑
 * 编译: gcc -o fall_detection_pc *.c -I../common/inc -I../pc_sim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

/* 引入 RT-Thread 模拟层 (RT_USING_MOCK 已通过 -D 编译选项定义) */
#include "rtthread_mock.h"

/* 引入公共模块 */
#include "fall_common.h"
#include "ipc_protocol.h"
#include "config.h"
#include "log.h"

/* 引入各进程接口 */
#include "video_capture.h"
#include "video_display.h"
#include "video_osd.h"
#include "video_rtsp.h"
#include "video_record.h"
#include "ai_engine.h"
#include "fall_detect.h"
#include "fall_alert.h"
#include "fall_notify.h"
#include "fall_storage.h"
#include "person_log.h"
#include "pose_overlay.h"
#include "sys_init.h"
#include "sys_monitor.h"
#include "key_handler.h"
#include "event_log.h"

/* ---- 全局状态 ---- */
static volatile int g_running = 1;
static ipc_handle_t *g_ipc = RT_NULL;
static system_config_t *g_cfg = RT_NULL;

/* ---- 非阻塞键盘读取 (Linux) ---- */
static struct termios g_old_termios;
static int g_terminal_setup = 0;

static void terminal_setup(void)
{
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &g_old_termios);
    new_termios = g_old_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    g_terminal_setup = 1;
}

static void terminal_restore(void)
{
    if (g_terminal_setup) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_old_termios);
        g_terminal_setup = 0;
    }
}

/* ---- SIGINT/SIGTERM 信号处理: 清理子进程 ---- */
#include <signal.h>

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
    kill(0, SIGTERM);
    terminal_restore();
    _exit(0);
}

static int kb_hit(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        return c;
    }
    return 0;
}

/* ---- 视频采集线程 ---- */
static void pc_thread_capture(void *param)
{
    (void)param;
    video_frame_t frame;
    int frame_count = 0;

    printf("[PC] Capture thread started\n");

    while (g_running) {
        fall_err_t ret = video_capture_get_frame(&frame);
        if (ret != FALL_OK) {
            usleep(10000);
            continue;
        }

        ipc_write_frame(&frame);

        /* RTSP 推送由 AI 线程处理 (画骨架叠加后再编码) */

        video_capture_release_frame(&frame);
        frame_count++;

        if (frame_count % 100 == 0) {
            printf("[PC] Captured %d frames\n", frame_count);
        }
    }

    printf("[PC] Capture thread stopped\n");
}

/* ---- AI 检测线程 ---- */
static void pc_thread_ai_detect(void *param)
{
    (void)param;
    video_frame_t frame;
    fall_result_t result;
    pose_result_t pose;
    int ran_detect = 0;

    printf("[PC] AI detect thread started\n");

    while (g_running) {
        fall_err_t ret = ipc_read_frame(&frame);
        if (ret != FALL_OK) {
            usleep(5000);
            continue;
        }

        ret = fall_detect_analyze(&frame, &result, &pose, &ran_detect);

        /* 每帧都更新 (不管是否检测到跌倒) */
        person_log_record(result.person_count, result.action,
                          result.confidence, result.frame_id);
        /* 只在实际执行了检测时更新骨架 (跳帧时保持上次的骨架) */
        if (ran_detect) {
            pose_overlay_update(&pose);
        }

        /* AI 处理完后, 画骨架叠加并推送到视频流 */
        video_rtsp_push_frame_with_overlay(&frame);

        if (ret == FALL_OK && result.state == FALL_STATE_CONFIRMED) {
            printf("[PC] !!! FALL DETECTED !!! id=%s conf=%.1f%% angle=%.1f\n",
                   result.event_id, result.confidence * 100, result.fall_angle);

            ipc_send_fall_result(&result);

            fall_event_t event;
            memset(&event, 0, sizeof(event));
            strncpy(event.event_id, result.event_id, sizeof(event.event_id));
            event.confidence = result.confidence;
            event.fall_angle = result.fall_angle;
            fall_alert_trigger(&event);
            video_rtsp_push_event(&event);

            if (g_cfg->recording_enabled) {
                video_record_start(event.event_id);
            }
        }
    }

    printf("[PC] AI detect thread stopped\n");
}

/* ---- 显示线程 ---- */
static void pc_thread_display(void *param)
{
    (void)param;
    osd_cmd_t osd_cmd;

    printf("[PC] Display thread started\n");

    /* 注意: 帧已由 capture 线程直接推送到 RTSP，不再从 ring buffer 读取 */
    /* 此线程仅处理 OSD 命令 */
    while (g_running) {
        while (ipc_recv_osd_cmd(&osd_cmd) == FALL_OK) {
            if (osd_cmd.show_flag) {
                printf("[PC] OSD: %s (color=0x%08X)\n", osd_cmd.text, osd_cmd.color);
            }
        }
        usleep(33000);
    }

    printf("[PC] Display thread stopped\n");
}

/* ---- 系统监控线程 ---- */
static void pc_thread_monitor(void *param)
{
    (void)param;

    printf("[PC] Monitor thread started\n");

    while (g_running) {
        usleep(5000000);

        sys_status_t status;
        sys_monitor_get_status(&status);

        printf("[PC] SYS: cpu=%d%% mem=%d/%dKB temp=%dC uptime=%lus\n",
               status.cpu_usage,
               status.mem_free / 1024,
               status.mem_total / 1024,
               status.temperature,
               (unsigned long)(status.uptime_ms / 1000));
    }

    printf("[PC] Monitor thread stopped\n");
}

/* ---- 模拟跌倒事件 ---- */
static void pc_simulate_fall(void)
{
    printf("\n[PC] ===== SIMULATING FALL EVENT =====\n");

    fall_result_t result;
    memset(&result, 0, sizeof(result));
    result.confidence = 0.92f;
    result.fall_angle = 65.3f;
    result.frame_id = 1234;
    result.state = FALL_STATE_CONFIRMED;
    result.timestamp = rt_tick_get();
    snprintf(result.event_id, sizeof(result.event_id), "E%08X", (uint32_t)result.timestamp);

    ipc_send_fall_result(&result);

    fall_event_t event;
    memset(&event, 0, sizeof(event));
    strncpy(event.event_id, result.event_id, sizeof(event.event_id));
    event.confidence = result.confidence;
    event.fall_angle = result.fall_angle;
    event.state = FALL_STATE_CONFIRMED;
    fall_alert_trigger(&event);

    fall_storage_save_event(&event);
    video_rtsp_push_event(&event);

    event_log_entry_t log_entry;
    memset(&log_entry, 0, sizeof(log_entry));
    strncpy(log_entry.event_id, event.event_id, sizeof(log_entry.event_id));
    log_entry.timestamp = event.timestamp;
    log_entry.confidence = event.confidence;
    log_entry.state = event.state;
    snprintf(log_entry.description, sizeof(log_entry.description),
             "Simulated fall (conf=%.1f%%)", event.confidence * 100);
    event_log_write(&log_entry);

    printf("[PC] Fall event simulated: %s\n", result.event_id);
    printf("[PC] ====================================\n\n");
}

/* ---- 主程序 ---- */
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("============================================\n");
    printf("  Fall Detection System - PC Simulation\n");
    printf("  Version: 1.0.0\n");
    printf("============================================\n\n");

    /* 设置终端为非阻塞模式 */
    terminal_setup();

    /* 注册信号处理: Ctrl+C 时清理子进程 */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* 初始化日志 */
    log_init();
    log_set_level(LOG_LVL_DEBUG);

    /* 加载配置 */
    config_init();
    g_cfg = config_get();
    printf("[PC] Config loaded: %dx%d@%dfps\n",
           g_cfg->video_width, g_cfg->video_height, g_cfg->video_fps);

    /* 初始化 IPC */
    ipc_init();
    g_ipc = ipc_get_handle();

    /* 初始化各模块 */
    video_capture_init(g_cfg->video_width, g_cfg->video_height, g_cfg->video_fps);
    video_capture_start();
    video_display_init();
    video_osd_init();
    video_record_init();
    video_rtsp_start(8554);  /* HTTP 视频流服务 */

    ai_engine_init();
    fall_detect_init();
    person_log_init();
    pose_overlay_init();
    fall_alert_init();
    fall_notify_init();
    fall_storage_init();

    sys_monitor_init();
    event_log_init();
    key_handler_init();

    printf("[PC] All modules initialized\n\n");

    g_running = 1;

    /* 创建线程 */
    rt_thread_create("capture", pc_thread_capture, NULL, 8192, 28, 10);
    rt_thread_create("ai_detect", pc_thread_ai_detect, NULL, 8192, 28, 10);
    rt_thread_create("display", pc_thread_display, NULL, 8192, 26, 10);
    rt_thread_create("monitor", pc_thread_monitor, NULL, 4096, 20, 10);

    printf("[PC] All threads started\n");
    printf("[PC] Press ENTER to simulate fall event, 'q' to quit\n\n");

    /* 主循环 */
    while (g_running) {
        int ch = kb_hit();
        if (ch == '\n' || ch == '\r') {
            pc_simulate_fall();
            usleep(500000);
        }
        if (ch == 'q' || ch == 'Q') {
            printf("\n[PC] Quitting...\n");
            g_running = 0;
            break;
        }
        usleep(100000);
    }

    /* 恢复终端 */
    terminal_restore();

    /* 等待线程结束 */
    printf("[PC] Waiting for threads to stop...\n");
    usleep(2000000);

    /* 清理 */
    video_rtsp_stop();
    video_capture_stop();
    video_capture_deinit();
    video_display_deinit();
    video_osd_deinit();
    video_record_deinit();

    fall_detect_deinit();
    person_log_deinit();
    pose_overlay_deinit();
    fall_alert_deinit();
    fall_notify_deinit();
    fall_storage_deinit();
    ai_engine_deinit();

    sys_monitor_deinit();
    event_log_deinit();
    key_handler_deinit();

    ipc_deinit();

    printf("\n[PC] System shut down cleanly.\n");
    return 0;
}
