/**
 * sys_init.c - 系统初始化实现
 */
#include "sys_init.h"
#include "sys_monitor.h"
#include "ipc_protocol.h"
#include "config.h"
#include "log.h"

#ifdef BSP_USING_K230
#include <k230_system.h>
#endif

/* RT-Thread 用户进程启动 */
#ifdef RT_USING_LWP
#include <lwp.h>
#endif

typedef struct {
    rt_lwp_t video_lwp;
    rt_lwp_t ai_lwp;
    uint8_t video_running;
    uint8_t ai_running;
} sys_ctx_t;

static sys_ctx_t g_sys;
static uint64_t g_boot_tick = 0;

/* 系统硬件初始化 */
static fall_err_t hal_init(void)
{
    LOG_I(LOG_TAG_SYSTEM, "Initializing HAL...");

#ifdef BSP_USING_K230
    /* K230 系统时钟初始化 */
    k230_system_init();

    /* GPIO 初始化 */
    k230_gpio_init();

    /* I2C 初始化 (传感器) */
    k230_i2c_init(0);

    /* SPI 初始化 (SD卡) */
    k230_spi_init(0);
#endif

    LOG_I(LOG_TAG_SYSTEM, "HAL initialized");
    return FALL_OK;
}

/* 挂载文件系统 */
static fall_err_t fs_init(void)
{
    LOG_I(LOG_TAG_SYSTEM, "Mounting filesystem...");

#ifdef BSP_USING_K230
    /* 挂载 SD 卡 */
    if (rt_device_find("sd0") == RT_NULL) {
        LOG_W(LOG_TAG_SYSTEM, "SD card not found");
        return FALL_ERR_IO;
    }

    if (dfs_mount("sd0", "/", "elm", 0, 0) != 0) {
        LOG_E(LOG_TAG_SYSTEM, "Failed to mount SD card");
        return FALL_ERR_IO;
    }
#else
    /* 开发环境使用本地目录模拟 */
    LOG_I(LOG_TAG_SYSTEM, "FS init (mock mode)");
#endif

    /* 创建必要目录 */
#ifdef RT_USING_MOCK
    rt_mkdir("./data");
    rt_mkdir("./data/events");
    rt_mkdir("./data/logs");
    rt_mkdir("./data/model");
#else
    rt_mkdir("/sdcard/events");
    rt_mkdir("/sdcard/logs");
    rt_mkdir("/sdcard/model");
#endif

    LOG_I(LOG_TAG_SYSTEM, "Filesystem mounted");
    return FALL_OK;
}

/* 启动子进程 */
static int start_user_process(const char *name, const char *path,
                               int priority, int stack_size)
{
#ifdef RT_USING_LWP
    rt_lwp_t lwp = lwp_create(path);
    if (lwp == RT_NULL) {
        LOG_E(LOG_TAG_SYSTEM, "Failed to create process: %s", name);
        return -1;
    }

    lwp_startup(lwp);
    LOG_I(LOG_TAG_SYSTEM, "Process started: %s (pid=%d)", name, lwp->pid);
    return lwp->pid;
#else
    /* 非 LWP 模式，使用线程模拟 */
    LOG_I(LOG_TAG_SYSTEM, "Process stub: %s (non-LWP mode)", name);
    (void)name; (void)path; (void)priority; (void)stack_size;
    return 0;
#endif
}

fall_err_t sys_init(void)
{
    rt_memset(&g_sys, 0, sizeof(g_sys));
    g_boot_tick = rt_tick_get();

    LOG_I(LOG_TAG_SYSTEM, "=== System Init Start ===");

    /* 硬件初始化 */
    if (hal_init() != FALL_OK) {
        LOG_E(LOG_TAG_SYSTEM, "HAL init failed");
        return FALL_ERR_IO;
    }

    /* 文件系统 */
    if (fs_init() != FALL_OK) {
        LOG_W(LOG_TAG_SYSTEM, "FS init failed, some features disabled");
    }

    /* 加载配置 */
    if (config_init() != FALL_OK) {
        LOG_E(LOG_TAG_SYSTEM, "Config init failed");
        return FALL_ERR_IO;
    }

    /* 初始化 IPC */
    if (ipc_init() != FALL_OK) {
        LOG_E(LOG_TAG_SYSTEM, "IPC init failed");
        return FALL_ERR_IPC;
    }

    /* 初始化系统监控 */
    sys_monitor_init();

    LOG_I(LOG_TAG_SYSTEM, "=== System Init Complete ===");
    return FALL_OK;
}

fall_err_t sys_start_processes(void)
{
    LOG_I(LOG_TAG_SYSTEM, "Starting child processes...");

    /* 启动视频采集进程 */
    g_sys.video_lwp = (rt_lwp_t)(uintptr_t)start_user_process(
        PROC_NAME_VIDEO_CAPTURE,
        "video_capture",
        RT_THREAD_PRIORITY_MAX - 5,
        16384);

    /* 启动 AI 检测进程 */
    g_sys.ai_lwp = (rt_lwp_t)(uintptr_t)start_user_process(
        PROC_NAME_AI_DETECT,
        "ai_detect",
        RT_THREAD_PRIORITY_MAX - 5,
        16384);

    g_sys.video_running = 1;
    g_sys.ai_running = 1;

    LOG_I(LOG_TAG_SYSTEM, "All processes started");
    return FALL_OK;
}

void sys_stop_processes(void)
{
    LOG_I(LOG_TAG_SYSTEM, "Stopping child processes...");

    /* 发送停止命令 */
    ipc_send_cmd(MSG_TYPE_CMD_STOP, RT_NULL, 0);

    g_sys.video_running = 0;
    g_sys.ai_running = 0;

#ifdef RT_USING_LWP
    if (g_sys.video_lwp) {
        lwp_free(g_sys.video_lwp);
        g_sys.video_lwp = RT_NULL;
    }
    if (g_sys.ai_lwp) {
        lwp_free(g_sys.ai_lwp);
        g_sys.ai_lwp = RT_NULL;
    }
#endif

    LOG_I(LOG_TAG_SYSTEM, "All processes stopped");
}

uint64_t sys_get_uptime_ms(void)
{
    return (rt_tick_get() - g_boot_tick) * 1000 / RT_TICK_PER_SECOND;
}
