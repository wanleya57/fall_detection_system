/**
 * video_capture.c - 视频采集实现
 *
 * 支持三种模式 (按优先级):
 *   1. K230 摄像头 HAL (BSP_USING_K230)
 *   2. MJPEG/RTSP 流 (通过 ffmpeg 子进程)
 *   3. Mock 灰色画面 (默认)
 *
 * 摄像头连接方式:
 *   - 设置环境变量: CAMERA_URL=http://ip:port/video
 *   - 不设置则自动扫描局域网内的 IP Webcam
 *   - 扫描结果缓存到 camera_cache.txt，下次直接使用
 */
#include "video_capture.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

#ifdef RT_USING_MOCK
#include <stdio.h>
#include <unistd.h>
#endif

#ifdef BSP_USING_K230
#include <k230_camera.h>
#endif

/* 自动扫描相关 */
#ifdef RT_USING_MOCK
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#define SCAN_TIMEOUT_MS     200
#define CACHE_FILE          "camera_cache.txt"
#define IP_WEBCAM_PORT      8080
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  fps;
    uint8_t  running;
    rt_sem_t frame_sem;

#ifdef RT_USING_MOCK
    FILE *ffmpeg_pipe;
    pid_t  ffmpeg_pid;    /* 子进程 PID, 用于清理 */
    uint8_t use_camera;
    uint32_t mock_frame_id;
    uint8_t  prebuf[1024];
    size_t   prebuf_len;
    size_t   prebuf_pos;
    char     camera_url[256];
#endif
} capture_ctx_t;

static capture_ctx_t g_capture;

#ifdef RT_USING_MOCK

/**
 * 尝试连接指定 IP:port，返回是否可达
 */
static int try_connect(const char *ip, int port, int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    /* 非阻塞模式 */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno == EINPROGRESS) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ret = select(fd + 1, NULL, &fds, NULL, &tv);
    }

    close(fd);
    return (ret > 0);
}

/**
 * 从缓存文件读取上次发现的摄像头 URL
 */
static int load_camera_cache(char *url, size_t url_size)
{
    FILE *fp = fopen(CACHE_FILE, "r");
    if (!fp) return 0;
    if (fgets(url, url_size, fp)) {
        /* 去掉换行符 */
        char *nl = strchr(url, '\n');
        if (nl) *nl = '\0';
        nl = strchr(url, '\r');
        if (nl) *nl = '\0';
        if (url[0] != '\0') {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

/**
 * 保存摄像头 URL 到缓存文件
 */
static void save_camera_cache(const char *url)
{
    FILE *fp = fopen(CACHE_FILE, "w");
    if (fp) {
        fprintf(fp, "%s\n", url);
        fclose(fp);
    }
}

/**
 * 获取本机 IP 地址和子网
 */
static int get_local_ip(char *ip_out, size_t ip_size)
{
    /* 通过连接外部地址获取本机 IP */
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = inet_addr("8.8.8.8");

    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) { close(fd); return 0; }

    socklen_t len = sizeof(addr);
    ret = getsockname(fd, (struct sockaddr *)&addr, &len);
    close(fd);

    if (ret < 0) return 0;
    const char *ip = inet_ntoa(addr.sin_addr);
    strncpy(ip_out, ip, ip_size);
    return 1;
}

/**
 * 扫描局域网寻找 IP Webcam
 * 使用 curl 快速检测，找到后返回 URL
 */
static int scan_for_camera(char *url_out, size_t url_size)
{
    char local_ip[64];
    if (!get_local_ip(local_ip, sizeof(local_ip))) {
        LOG_W(LOG_TAG_VIDEO, "Cannot determine local IP for scanning");
        return 0;
    }

    LOG_I(LOG_TAG_VIDEO, "Scanning LAN from %s for IP Webcam...", local_ip);

    /* 提取子网前缀 (假设 /24) */
    char prefix[32];
    strncpy(prefix, local_ip, sizeof(prefix) - 1);
    char *last_dot = strrchr(prefix, '.');
    if (!last_dot) return 0;
    *last_dot = '\0';

    /* 并行扫描: 32 个并发, ~8 秒完成全网段 */
    char scan_cmd[1024];
    char last_octet[8];
    strncpy(last_octet, strrchr(local_ip, '.') + 1, sizeof(last_octet) - 1);

    snprintf(scan_cmd, sizeof(scan_cmd),
        "for i in $(seq 1 254); do "
        "[ \"$i\" = \"%s\" ] && continue; "
        "echo $i; "
        "done | xargs -P32 -I{} sh -c "
        "'curl -s --connect-timeout 0.5 --max-time 1 "
        "\"http://%s.{}:%d/\" > /dev/null 2>&1 "
        "&& echo \"%s.{}\"' 2>/dev/null",
        last_octet,
        prefix, IP_WEBCAM_PORT,
        prefix);

    LOG_I(LOG_TAG_VIDEO, "Scanning %s.1 ~ %s.254 (parallel)...", prefix, prefix);

    FILE *fp = popen(scan_cmd, "r");
    if (!fp) {
        LOG_W(LOG_TAG_VIDEO, "Scan failed");
        return 0;
    }

    char found_ip[64] = {0};
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] != '\0') {
            strncpy(found_ip, line, sizeof(found_ip) - 1);
            break;
        }
    }
    pclose(fp);

    if (found_ip[0] == '\0') {
        LOG_W(LOG_TAG_VIDEO, "No IP Webcam found on LAN");
        return 0;
    }

    LOG_I(LOG_TAG_VIDEO, "Found device at %s:%d, verifying...", found_ip, IP_WEBCAM_PORT);

    /* 验证：检查页面是否包含 IP Webcam 特征 */
    char verify_cmd[512];
    snprintf(verify_cmd, sizeof(verify_cmd),
        "curl -s --connect-timeout 3 --max-time 5 'http://%s:%d/' 2>/dev/null",
        found_ip, IP_WEBCAM_PORT);

    fp = popen(verify_cmd, "r");
    if (fp) {
        char buf[2048];
        size_t total = 0;
        char line[256];
        while (fgets(line, sizeof(line), fp) && total < sizeof(buf) - 1) {
            size_t len = strlen(line);
            if (total + len < sizeof(buf)) {
                memcpy(buf + total, line, len);
                total += len;
            }
        }
        buf[total] = '\0';
        pclose(fp);

        /* IP Webcam 页面通常包含这些特征 */
        if (strstr(buf, "IP Webcam") || strstr(buf, "ipwebcam") ||
            strstr(buf, "IP摄像头") || strstr(buf, "videofeed")) {
            LOG_I(LOG_TAG_VIDEO, "IP Webcam confirmed at %s", found_ip);
            snprintf(url_out, url_size, "http://%s:%d/video", found_ip, IP_WEBCAM_PORT);
            return 1;
        }
    }

    LOG_W(LOG_TAG_VIDEO, "Device at %s is not an IP Webcam", found_ip);
    return 0;
}

/**
 * 尝试通过 ffmpeg 连接摄像头流
 * 成功返回 1，失败返回 0
 */
static int try_open_camera(const char *url, uint16_t width, uint16_t height, uint8_t fps)
{
    LOG_I(LOG_TAG_VIDEO, "Connecting to camera: %s", url);

    /* 用 fork+exec 代替 popen, 以便跟踪子进程 PID */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        LOG_E(LOG_TAG_VIDEO, "pipe failed");
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        return 0;
    }

    if (pid == 0) {
        /* 子进程: ffmpeg */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        char w_str[16], h_str[16], f_str[8], vf_str[32];
        snprintf(w_str, sizeof(w_str), "%d", width);
        snprintf(h_str, sizeof(h_str), "%d", height);
        snprintf(f_str, sizeof(f_str), "%d", fps);
        snprintf(vf_str, sizeof(vf_str), "scale=%d:%d", width, height);

        execlp("ffmpeg", "ffmpeg",
               "-loglevel", "error",
               "-reconnect", "1", "-reconnect_streamed", "1",
               "-i", url,
               "-vf", vf_str,
               "-pix_fmt", "yuv420p",
               "-f", "rawvideo", "-r", f_str,
               "-", (char *)NULL);
        _exit(1);
    }

    /* 父进程 */
    close(pipefd[1]);
    g_capture.ffmpeg_pid = pid;
    g_capture.ffmpeg_pipe = fdopen(pipefd[0], "r");
    if (!g_capture.ffmpeg_pipe) {
        close(pipefd[0]);
        kill(pid, SIGTERM);
        return 0;
    }

    /* 测试连接 */
    size_t test_n = fread(g_capture.prebuf, 1, sizeof(g_capture.prebuf), g_capture.ffmpeg_pipe);
    if (test_n > 0) {
        g_capture.use_camera = 1;
        g_capture.prebuf_len = test_n;
        g_capture.prebuf_pos = 0;
        g_capture.mock_frame_id = 0;
        LOG_I(LOG_TAG_VIDEO, "Camera stream connected (%zu bytes test read)", test_n);
        return 1;
    }

    LOG_W(LOG_TAG_VIDEO, "Camera stream failed - no data received");
    pclose(g_capture.ffmpeg_pipe);
    g_capture.ffmpeg_pipe = NULL;
    return 0;
}

#endif /* RT_USING_MOCK */

fall_err_t video_capture_init(uint16_t width, uint16_t height, uint8_t fps)
{
    rt_memset(&g_capture, 0, sizeof(g_capture));
    g_capture.width = width;
    g_capture.height = height;
    g_capture.fps = fps;

    g_capture.frame_sem = rt_sem_create("cap_sem", 0, RT_IPC_FLAG_FIFO);
    if (g_capture.frame_sem == RT_NULL) {
        LOG_E(LOG_TAG_VIDEO, "Failed to create capture semaphore");
        return FALL_ERR_NOMEM;
    }

#ifdef BSP_USING_K230
    k230_camera_config_t cam_cfg = {
        .width = width, .height = height, .fps = fps,
        .format = K230_CAMERA_FMT_YUV420,
    };
    if (k230_camera_init(&cam_cfg) != 0) {
        LOG_E(LOG_TAG_VIDEO, "Camera init failed");
        return FALL_ERR_IO;
    }
#elif defined(RT_USING_MOCK)
    const char *env_url = getenv("CAMERA_URL");

    /* 优先使用环境变量 */
    if (env_url && env_url[0] != '\0') {
        strncpy(g_capture.camera_url, env_url, sizeof(g_capture.camera_url) - 1);
        if (try_open_camera(g_capture.camera_url, width, height, fps)) {
            save_camera_cache(g_capture.camera_url);
        } else {
            LOG_E(LOG_TAG_VIDEO, "Failed to connect to CAMERA_URL: %s", env_url);
            g_capture.use_camera = 0;
        }
    }
    /* 其次尝试缓存 */
    else if (load_camera_cache(g_capture.camera_url, sizeof(g_capture.camera_url))) {
        LOG_I(LOG_TAG_VIDEO, "Trying cached camera: %s", g_capture.camera_url);
        if (!try_open_camera(g_capture.camera_url, width, height, fps)) {
            LOG_W(LOG_TAG_VIDEO, "Cached camera failed, scanning LAN...");
            if (scan_for_camera(g_capture.camera_url, sizeof(g_capture.camera_url))) {
                try_open_camera(g_capture.camera_url, width, height, fps);
                save_camera_cache(g_capture.camera_url);
            }
        }
    }
    /* 最后自动扫描局域网 */
    else {
        LOG_I(LOG_TAG_VIDEO, "No CAMERA_URL set, auto-scanning LAN...");
        if (scan_for_camera(g_capture.camera_url, sizeof(g_capture.camera_url))) {
            try_open_camera(g_capture.camera_url, width, height, fps);
            save_camera_cache(g_capture.camera_url);
        } else {
            LOG_I(LOG_TAG_VIDEO, "No camera found, using mock frames");
            LOG_I(LOG_TAG_VIDEO, "Tip: CAMERA_URL=http://phone_ip:8080/video");
            g_capture.use_camera = 0;
        }
    }
#endif

    LOG_I(LOG_TAG_VIDEO, "Camera initialized: %dx%d@%dfps", width, height, fps);
    return FALL_OK;
}

fall_err_t video_capture_start(void)
{
    if (g_capture.running) return FALL_OK;

#ifdef BSP_USING_K230
    k230_camera_start();
#endif

    g_capture.running = 1;
    LOG_I(LOG_TAG_VIDEO, "Camera capture started");
    return FALL_OK;
}

fall_err_t video_capture_stop(void)
{
    if (!g_capture.running) return FALL_OK;

#ifdef BSP_USING_K230
    k230_camera_stop();
#endif

    g_capture.running = 0;
    LOG_I(LOG_TAG_VIDEO, "Camera capture stopped");
    return FALL_OK;
}

fall_err_t video_capture_get_frame(video_frame_t *frame)
{
    if (frame == RT_NULL) return FALL_ERR_INVALID;
    if (!g_capture.running) return FALL_ERR_IO;

#ifdef BSP_USING_K230
    void *raw_buf = RT_NULL;
    if (k230_camera_get_frame(&raw_buf, 100) != 0) {
        return FALL_ERR_TIMEOUT;
    }
    rt_memcpy(frame->frame_buf, raw_buf, FRAME_SIZE);
    k230_camera_release_frame(raw_buf);

#elif defined(RT_USING_MOCK)
    if (g_capture.use_camera && g_capture.ffmpeg_pipe) {
        size_t need = FRAME_SIZE;
        size_t total = 0;

        /* 先消耗预读缓冲 */
        if (g_capture.prebuf_pos < g_capture.prebuf_len) {
            size_t avail = g_capture.prebuf_len - g_capture.prebuf_pos;
            size_t copy = (avail < need) ? avail : need;
            rt_memcpy(frame->frame_buf, g_capture.prebuf + g_capture.prebuf_pos, copy);
            total = copy;
            g_capture.prebuf_pos += copy;
        }

        while (total < need) {
            size_t n = fread(frame->frame_buf + total, 1, need - total,
                            g_capture.ffmpeg_pipe);
            if (n == 0) {
                LOG_W(LOG_TAG_VIDEO, "Camera stream disconnected, reconnecting...");
                pclose(g_capture.ffmpeg_pipe);
                g_capture.ffmpeg_pipe = NULL;

                if (g_capture.camera_url[0] != '\0') {
                    try_open_camera(g_capture.camera_url,
                                    g_capture.width, g_capture.height, g_capture.fps);
                }
                if (!g_capture.use_camera) {
                    LOG_E(LOG_TAG_VIDEO, "Reconnect failed, switching to mock");
                }
                return FALL_ERR_IO;
            }
            total += n;
        }

        frame->frame_id = g_capture.mock_frame_id++;
        frame->timestamp = rt_tick_get();
    } else {
        rt_memset(frame->frame_buf, 0x80, FRAME_SIZE);
        frame->frame_id = g_capture.mock_frame_id++;
        frame->timestamp = rt_tick_get();
    }
#endif

    frame->width = g_capture.width;
    frame->height = g_capture.height;
    frame->format = VIDEO_FORMAT_YUV420;
    frame->is_valid = 1;

    return FALL_OK;
}

void video_capture_release_frame(video_frame_t *frame)
{
    if (frame) frame->is_valid = 0;
}

void video_capture_deinit(void)
{
    video_capture_stop();

#ifdef RT_USING_MOCK
    if (g_capture.ffmpeg_pid > 0) {
        kill(g_capture.ffmpeg_pid, SIGTERM);
        waitpid(g_capture.ffmpeg_pid, NULL, 0);
        g_capture.ffmpeg_pid = 0;
    }
    if (g_capture.ffmpeg_pipe) {
        fclose(g_capture.ffmpeg_pipe);
        g_capture.ffmpeg_pipe = NULL;
    }
#endif

    if (g_capture.frame_sem) {
        rt_sem_delete(g_capture.frame_sem);
        g_capture.frame_sem = RT_NULL;
    }

#ifdef BSP_USING_K230
    k230_camera_deinit();
#endif

    rt_memset(&g_capture, 0, sizeof(g_capture));
    LOG_I(LOG_TAG_VIDEO, "Camera deinitialized");
}
