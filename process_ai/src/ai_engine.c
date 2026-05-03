/**
 * ai_engine.c - AI 推理引擎
 *
 * Stage 1: YOLO11n-pose (KPU) → bbox + 17 关键点
 * Stage 2: 时序分类器 (CPU) → 动作类别 (0~5)
 *
 * PC 模拟: 模拟完整摔倒场景 + 时序分类
 */
#include "ai_engine.h"
#include "log.h"
#include <string.h>
#include <math.h>

#ifdef BSP_USING_K230
#include <k230_kpu.h>
#endif

#ifdef RT_USING_MOCK
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#endif

/* ========== 引擎上下文 ========== */

typedef struct {
    int initialized;
    int pose_loaded;
    int temporal_loaded;
    int inference_time_ms;

    /* 时序特征滑动窗口 */
    float feature_buf[FEATURE_BUF_SIZE];
    int feat_write_idx;
    int feat_count;

#ifdef BSP_USING_K230
    kpu_model_context_t pose_ctx;
    kpu_model_context_t temporal_ctx;
#endif

#ifdef RT_USING_MOCK
    FILE *py_stdin;
    FILE *py_stdout;
    pid_t py_pid;
    int   py_ready;
#endif
} ai_ctx_t;

static ai_ctx_t g_ai;

static const char *ACTION_NAMES[ACTION_COUNT] = {
    "Standing", "Walking", "Sitting", "Crouching", "FALLING", "Lying"
};

/* ========== Python 子进程管理 ========== */

#ifdef RT_USING_MOCK
static int spawn_python_subprocess(void)
{
    /* 清理旧的子进程 */
    if (g_ai.py_stdin) { fclose(g_ai.py_stdin); g_ai.py_stdin = NULL; }
    if (g_ai.py_stdout) { fclose(g_ai.py_stdout); g_ai.py_stdout = NULL; }
    if (g_ai.py_pid > 0) {
        kill(g_ai.py_pid, SIGTERM);
        waitpid(g_ai.py_pid, NULL, 0);
        g_ai.py_pid = 0;
    }
    g_ai.py_ready = 0;

    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        LOG_E(LOG_TAG_AI, "Failed to create pipes");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_E(LOG_TAG_AI, "Failed to fork");
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return -1;
    } else if (pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        execlp("python3", "python3", "pose_detect.py", (char *)NULL);
        _exit(1);
    } else {
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        g_ai.py_stdin = fdopen(stdin_pipe[1], "w");
        g_ai.py_stdout = fdopen(stdout_pipe[0], "r");
        g_ai.py_pid = pid;

        char ready[16] = {0};
        struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
        setsockopt(fileno(g_ai.py_stdout), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (fgets(ready, sizeof(ready), g_ai.py_stdout) &&
            strstr(ready, "READY")) {
            g_ai.py_ready = 1;
            LOG_I(LOG_TAG_AI, "Python subprocess ready (pid=%d)", pid);
            return 0;
        } else {
            LOG_E(LOG_TAG_AI, "Python subprocess failed to start");
            return -1;
        }
    }
}
#endif

/* ========== 公共 API ========== */

const char *ai_engine_action_name(action_class_t action)
{
    if (action >= 0 && action < ACTION_COUNT) return ACTION_NAMES[action];
    return "Unknown";
}

fall_err_t ai_engine_init(void)
{
    rt_memset(&g_ai, 0, sizeof(g_ai));

#ifdef BSP_USING_K230
    if (kpu_init() != 0) {
        LOG_E(LOG_TAG_AI, "KPU init failed");
        return FALL_ERR_IO;
    }
#endif

    g_ai.initialized = 1;

#ifdef RT_USING_MOCK
    /* 忽略 SIGPIPE, 写入已关闭的管道时不崩溃 */
    signal(SIGPIPE, SIG_IGN);
    spawn_python_subprocess();
#endif

#if defined(TEMPORAL_MODEL_LSTM)
    LOG_I(LOG_TAG_AI, "AI engine: YOLO11n-pose + LSTM classifier");
#elif defined(TEMPORAL_MODEL_1DCNN)
    LOG_I(LOG_TAG_AI, "AI engine: YOLO11n-pose + 1D-CNN classifier");
#elif defined(TEMPORAL_MODEL_TCN)
    LOG_I(LOG_TAG_AI, "AI engine: YOLO11n-pose + TCN classifier");
#endif
    return FALL_OK;
}

fall_err_t ai_engine_load_model(const char *pose_path, const char *temporal_path)
{
    if (!g_ai.initialized) return FALL_ERR_INVALID;

#ifdef BSP_USING_K230
    /* 加载 YOLO11n-pose 到 KPU */
    if (pose_path) {
        rt_file_t fp = rt_fopen(pose_path, "rb");
        if (!fp) {
            LOG_E(LOG_TAG_AI, "Cannot open pose model: %s", pose_path);
            return FALL_ERR_IO;
        }
        rt_fseek(fp, 0, SEEK_END);
        long sz = rt_ftell(fp);
        rt_fseek(fp, 0, SEEK_SET);
        void *data = rt_malloc(sz);
        if (!data) { rt_fclose(fp); return FALL_ERR_NOMEM; }
        rt_fread(data, 1, sz, fp);
        rt_fclose(fp);

        if (kpu_load(&g_ai.pose_ctx, data) != 0) {
            rt_free(data);
            return FALL_ERR_MODEL;
        }
        rt_free(data);
        g_ai.pose_loaded = 1;
        LOG_I(LOG_TAG_AI, "YOLO11n-pose loaded: %ld bytes", sz);
    }

    /* 加载时序分类器 (可选，CPU 推理) */
    if (temporal_path) {
        rt_file_t fp = rt_fopen(temporal_path, "rb");
        if (!fp) {
            LOG_W(LOG_TAG_AI, "Temporal model not found, using rule-based fallback");
        } else {
            rt_fseek(fp, 0, SEEK_END);
            long sz = rt_ftell(fp);
            rt_fseek(fp, 0, SEEK_SET);
            void *data = rt_malloc(sz);
            if (!data) { rt_fclose(fp); return FALL_ERR_NOMEM; }
            rt_fread(data, 1, sz, fp);
            rt_fclose(fp);

            if (kpu_load(&g_ai.temporal_ctx, data) != 0) {
                rt_free(data);
                LOG_W(LOG_TAG_AI, "Temporal model load failed, using rule-based");
            } else {
                rt_free(data);
                g_ai.temporal_loaded = 1;
                LOG_I(LOG_TAG_AI, "Temporal classifier loaded: %ld bytes", sz);
            }
        }
    }

#else
    (void)pose_path;
    (void)temporal_path;
    LOG_I(LOG_TAG_AI, "Models loaded (mock)");
    g_ai.pose_loaded = 1;
    g_ai.temporal_loaded = 1;
#endif

    return FALL_OK;
}

void ai_engine_get_pipeline_info(ai_pipeline_info_t *info)
{
    if (!info) return;
    rt_memset(info, 0, sizeof(ai_pipeline_info_t));

    info->spatial_model = "YOLO11n-pose";
    info->spatial_path = MODEL_PATH_YOLO11N_POSE;
    info->spatial_ms = 8;

#if defined(TEMPORAL_MODEL_LSTM)
    info->temporal_model = "LSTM";
    info->temporal_path = MODEL_PATH_TEMPORAL;
    info->temporal_ms = 1;
#elif defined(TEMPORAL_MODEL_1DCNN)
    info->temporal_model = "1D-CNN";
    info->temporal_path = MODEL_PATH_TEMPORAL;
    info->temporal_ms = 1;
#elif defined(TEMPORAL_MODEL_TCN)
    info->temporal_model = "TCN";
    info->temporal_path = MODEL_PATH_TEMPORAL;
    info->temporal_ms = 2;
#endif
}

/* ========== 特征提取 ========== */

void ai_engine_extract_feature(pose_result_t *pose, float (*detections)[5],
                                int det_count, float *feat_out)
{
    rt_memset(feat_out, 0, sizeof(float) * FEATURE_PER_FRAME);

    if (det_count == 0 || pose->person_count == 0) return;

    keypoint_t *kp = pose->persons[0].keypoints;

    /* 17 关键点 x,y → 34 维 */
    for (int i = 0; i < KEYPOINT_COUNT; i++) {
        feat_out[i * 2]     = kp[i].x / 720.0f;     /* 归一化到 [0,1] */
        feat_out[i * 2 + 1] = kp[i].y / 480.0f;
    }

    /* bbox (x,y,w,h) → 4 维 */
    float bx = detections[0][0], by = detections[0][1];
    float bw = detections[0][2], bh = detections[0][3];
    feat_out[34] = bx / 720.0f;
    feat_out[35] = by / 480.0f;
    feat_out[36] = bw / 720.0f;
    feat_out[37] = bh / 480.0f;

    /* 中心速度 (与上一帧比较) */
    float cx = (bx + bw / 2.0f) / 720.0f;
    float cy = (by + bh / 2.0f) / 480.0f;
    if (g_ai.feat_count > 0) {
        int prev = ((g_ai.feat_write_idx - 1) + FEATURE_BUF_SIZE) % FEATURE_BUF_SIZE;
        float prev_cx = g_ai.feature_buf[prev + 34] + g_ai.feature_buf[prev + 36] / 2.0f;
        float prev_cy = g_ai.feature_buf[prev + 35] + g_ai.feature_buf[prev + 37] / 2.0f;
        feat_out[38] = (cx - prev_cx) * 30.0f;  /* 速度 = 位移 × fps */
        feat_out[39] = (cy - prev_cy) * 30.0f;
    }

    /* 身体倾斜角 */
    float shoulder_cx = (kp[6].x + kp[7].x) / 2.0f;
    float shoulder_cy = (kp[6].y + kp[7].y) / 2.0f;
    float hip_cx = (kp[12].x + kp[13].x) / 2.0f;
    float hip_cy = (kp[12].y + kp[13].y) / 2.0f;
    float dx = hip_cx - shoulder_cx;
    float dy = hip_cy - shoulder_cy;
    float angle = atan2f(fabsf(dx), fabsf(dy)) * 180.0f / 3.14159265f;
    feat_out[40] = angle / 90.0f;  /* 归一化到 [0,2] 左右 */
}

/* ========== 时序分类 ========== */

/* 将特征加入滑动窗口缓冲 */
static void push_feature(const float *feat)
{
    int offset = (g_ai.feat_write_idx % TEMPORAL_WINDOW) * FEATURE_PER_FRAME;
    rt_memcpy(&g_ai.feature_buf[offset], feat, sizeof(float) * FEATURE_PER_FRAME);
    g_ai.feat_write_idx++;
    if (g_ai.feat_count < TEMPORAL_WINDOW) g_ai.feat_count++;
}

/* 规则引擎后备方案 (无时序模型时使用) */
static action_class_t rule_based_classify(const float *feat)
{
    float angle = feat[40] * 90.0f;
    float vy = feat[39];  /* 垂直速度 */

    /* 关键点: kp[0]=头, kp[12]=左髋, kp[13]=右髋 */
    float head_y = feat[1];   /* kp[0].y 归一化 */
    float hip_y  = (feat[25] + feat[27]) / 2.0f;  /* (kp[12].y + kp[13].y) / 2 */

    /* 头低于髋 + 角度大 → 摔倒 */
    if (head_y > hip_y && angle > 40.0f)
        return ACTION_FALLING;

    /* 角度大 + 快速下落 → 摔倒 */
    if (angle > 45.0f && vy > 0.05f)
        return ACTION_FALLING;

    /* 角度中等 → 蹲下 */
    if (angle > 25.0f)
        return ACTION_CROUCHING;

    /* 头和髋接近地面 → 躺倒 */
    if (head_y > 0.7f && hip_y > 0.7f)
        return ACTION_LYING;

    return ACTION_STANDING;
}

action_class_t ai_engine_classify_action(float *feat, float *output)
{
    /* 加入滑动窗口 */
    push_feature(feat);

    /* 清零输出概率 */
    if (output) {
        rt_memset(output, 0, sizeof(float) * ACTION_COUNT);
    }

    /* 需要至少 10 帧才能判断 */
    if (g_ai.feat_count < 10) {
        if (output) output[ACTION_STANDING] = 1.0f;
        return ACTION_STANDING;
    }

#ifdef BSP_USING_K230
    if (g_ai.temporal_loaded) {
        /* KPU/CPU 推理时序模型 */
        /* 输入: feature_buf (30×41 = 1230 维) */
        /* 输出: 6 类概率 */
        /* TODO: 实际模型推理 */
        /*
        kpu_run(&g_ai.temporal_ctx, g_ai.feature_buf);
        float *out = kpu_get_output(&g_ai.temporal_ctx);
        for (int i = 0; i < ACTION_COUNT; i++) output[i] = out[i];
        */
    }
#endif

    /* 规则引擎后备 */
    action_class_t result = rule_based_classify(feat);
    if (output) output[result] = 1.0f;
    return result;
}

/* ========== Mock 摔倒模拟 (PC) ========== */

#ifdef RT_USING_MOCK

#define MOCK_FPS            30
#define MOCK_STAND_FRAMES   (MOCK_FPS * 20)
#define MOCK_CROUCH_FRAMES  (MOCK_FPS * 2)
#define MOCK_FALL_FRAMES    (MOCK_FPS * 1)
#define MOCK_LYING_FRAMES   (MOCK_FPS * 5)
#define MOCK_RECOVER_FRAMES (MOCK_FPS * 2)
#define MOCK_CYCLE          (MOCK_STAND_FRAMES + MOCK_CROUCH_FRAMES + \
                             MOCK_FALL_FRAMES + MOCK_LYING_FRAMES + MOCK_RECOVER_FRAMES)

static uint32_t g_mock_frame = 0;
static float jitter(int s) { return (float)(s % 7 - 3); }

static void fill_standing(keypoint_t *kp, float bx, float by, float bw, float bh)
{
    float cx = bx + bw / 2.0f;
    kp[0]  = (keypoint_t){cx, by + bh * 0.05f, 0.92f};
    kp[1]  = (keypoint_t){cx, by + bh * 0.12f, 0.95f};
    kp[2]  = (keypoint_t){cx - 8, by + bh * 0.10f, 0.88f};
    kp[3]  = (keypoint_t){cx + 8, by + bh * 0.10f, 0.87f};
    kp[4]  = (keypoint_t){cx - 15, by + bh * 0.12f, 0.80f};
    kp[5]  = (keypoint_t){cx + 15, by + bh * 0.12f, 0.79f};
    float slx = cx - bw * 0.35f, srx = cx + bw * 0.35f;
    float shy = by + bh * 0.18f;
    kp[6]  = (keypoint_t){slx, shy, 0.91f};
    kp[7]  = (keypoint_t){srx, shy, 0.90f};
    kp[8]  = (keypoint_t){slx - 10, shy + bh * 0.18f, 0.85f};
    kp[9]  = (keypoint_t){srx + 10, shy + bh * 0.18f, 0.84f};
    kp[10] = (keypoint_t){slx - 15, shy + bh * 0.28f, 0.78f};
    kp[11] = (keypoint_t){srx + 15, shy + bh * 0.28f, 0.77f};
    float hlx = cx - bw * 0.20f, hrx = cx + bw * 0.20f;
    float hy = by + bh * 0.50f;
    kp[12] = (keypoint_t){hlx, hy, 0.90f};
    kp[13] = (keypoint_t){hrx, hy, 0.89f};
    kp[14] = (keypoint_t){hlx, by + bh * 0.72f, 0.87f};
    kp[15] = (keypoint_t){hrx, by + bh * 0.72f, 0.86f};
    kp[16] = (keypoint_t){hlx, by + bh * 0.95f, 0.82f};
}

static void fill_falling(keypoint_t *kp, float bx, float by, float bw, float bh, float t)
{
    float cx = bx + bw / 2.0f;
    float tilt = bw * t * 0.3f;
    float head_y = by + bh * (0.3f + t * 0.5f);
    float hip_y = by + bh * (0.5f + t * 0.15f);
    float ankle_y = by + bh * (0.85f + t * 0.1f);

    kp[0]  = (keypoint_t){cx + tilt, head_y, 0.85f};
    kp[1]  = (keypoint_t){cx + tilt, head_y + 10, 0.88f};
    kp[2]  = (keypoint_t){cx + tilt - 6, head_y + 5, 0.80f};
    kp[3]  = (keypoint_t){cx + tilt + 6, head_y + 5, 0.79f};
    kp[4]  = (keypoint_t){cx + tilt - 12, head_y + 8, 0.72f};
    kp[5]  = (keypoint_t){cx + tilt + 12, head_y + 8, 0.71f};
    kp[6]  = (keypoint_t){cx + tilt - bw * 0.3f, head_y + bh * 0.15f, 0.86f};
    kp[7]  = (keypoint_t){cx + tilt + bw * 0.3f, head_y + bh * 0.15f, 0.85f};
    kp[8]  = (keypoint_t){cx + tilt - bw * 0.4f, head_y + bh * 0.25f, 0.78f};
    kp[9]  = (keypoint_t){cx + tilt + bw * 0.4f, head_y + bh * 0.25f, 0.77f};
    kp[10] = (keypoint_t){cx + tilt - bw * 0.45f, head_y + bh * 0.35f, 0.70f};
    kp[11] = (keypoint_t){cx + tilt + bw * 0.45f, head_y + bh * 0.35f, 0.69f};
    kp[12] = (keypoint_t){cx - bw * 0.15f, hip_y, 0.88f};
    kp[13] = (keypoint_t){cx + bw * 0.15f, hip_y, 0.87f};
    kp[14] = (keypoint_t){cx - bw * 0.15f, (hip_y + ankle_y) / 2, 0.82f};
    kp[15] = (keypoint_t){cx + bw * 0.15f, (hip_y + ankle_y) / 2, 0.81f};
    kp[16] = (keypoint_t){cx - bw * 0.20f, ankle_y, 0.78f};
}

static void fill_lying(keypoint_t *kp, float bx, float by, float bw, float bh)
{
    float body_y = by + bh * 0.4f;
    float lx = bx + bw * 0.1f;
    float rx = bx + bw * 0.9f;
    float sx = lx + bw * 0.2f;
    float hx = lx + bw * 0.55f;

    kp[0]  = (keypoint_t){lx, body_y - 5, 0.80f};
    kp[1]  = (keypoint_t){lx + 15, body_y, 0.83f};
    kp[2]  = (keypoint_t){lx + 8, body_y - 8, 0.75f};
    kp[3]  = (keypoint_t){lx + 8, body_y + 3, 0.74f};
    kp[4]  = (keypoint_t){lx + 3, body_y - 10, 0.68f};
    kp[5]  = (keypoint_t){lx + 3, body_y + 8, 0.67f};
    kp[6]  = (keypoint_t){sx, body_y - 15, 0.82f};
    kp[7]  = (keypoint_t){sx, body_y + 15, 0.81f};
    kp[8]  = (keypoint_t){sx + 20, body_y - 20, 0.72f};
    kp[9]  = (keypoint_t){sx + 20, body_y + 20, 0.71f};
    kp[10] = (keypoint_t){sx + 35, body_y - 25, 0.65f};
    kp[11] = (keypoint_t){sx + 35, body_y + 25, 0.64f};
    kp[12] = (keypoint_t){hx, body_y - 12, 0.85f};
    kp[13] = (keypoint_t){hx, body_y + 12, 0.84f};
    kp[14] = (keypoint_t){rx - 40, body_y - 10, 0.80f};
    kp[15] = (keypoint_t){rx - 40, body_y + 10, 0.79f};
    kp[16] = (keypoint_t){rx, body_y - 8, 0.75f};
}

#endif /* RT_USING_MOCK */

/* ========== 空间推理 (YOLO11n-pose) ========== */

fall_err_t ai_engine_detect_and_pose(video_frame_t *frame,
                                      float (*detections)[5],
                                      int max_det, int *count,
                                      pose_result_t *result,
                                      int *ran_detect)
{
    if (!frame || !detections || !count || !result) return FALL_ERR_INVALID;
#ifndef RT_USING_MOCK
    if (!g_ai.pose_loaded) return FALL_ERR_INVALID;
#endif

    uint64_t start = rt_tick_get();
    *count = 0;
    if (ran_detect) *ran_detect = 0;
    rt_memset(result, 0, sizeof(pose_result_t));

#ifdef BSP_USING_K230
    (void)max_det;
    /* TODO: KPU 推理 YOLO11n-pose */
    /* kpu_run(&g_ai.pose_ctx, input); */
    /* parse_output(output, detections, count, result); */

#elif defined(RT_USING_MOCK)
    (void)max_det;

    /* 优先使用 YOLOv8n-pose Python 子进程 */
    if (g_ai.py_ready && g_ai.py_stdin && g_ai.py_stdout) {
        static int frame_seq = 0;
        static int last_ok_seq = -1;
        frame_seq++;

        /* 每 10 帧发送一次给 Python (CPU 推理慢, 降低频率) */
        if ((frame_seq - last_ok_seq) < 10 && last_ok_seq >= 0) {
            return FALL_OK;
        }

        /* 发送帧: [4B width][4B height] + YUV420 data (纯二进制) */
        uint32_t hdr[2] = { (uint32_t)frame->width, (uint32_t)frame->height };
        fwrite(hdr, 4, 2, g_ai.py_stdin);
        int frame_bytes = frame->width * frame->height * 3 / 2;
        fwrite(frame->frame_buf, 1, frame_bytes, g_ai.py_stdin);

        if (ferror(g_ai.py_stdin)) {
            LOG_E(LOG_TAG_AI, "Frame #%d: Python stdin broken, restarting...", frame_seq);
            clearerr(g_ai.py_stdin);
            spawn_python_subprocess();
            return FALL_OK;
        }
        fflush(g_ai.py_stdin);

        LOG_I(LOG_TAG_AI, "Frame #%d sent to Python: %dx%d (%d bytes)",
              frame_seq, frame->width, frame->height, frame_bytes);

        /* 用 select() 等待 Python 回复, 超时 5 秒 */
        int fd = fileno(g_ai.py_stdout);
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        int ready = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ready <= 0) {
            LOG_E(LOG_TAG_AI, "Frame #%d: Python reply timeout (5s), skipping", frame_seq);
            last_ok_seq = frame_seq;
            return FALL_OK;
        }

        /* 读取 JSON 结果 */
        char line[8192];
        if (fgets(line, sizeof(line), g_ai.py_stdout)) {
            LOG_I(LOG_TAG_AI, "Frame #%d Python reply: %.200s...", frame_seq, line);
            last_ok_seq = frame_seq;
            /* 简易 JSON 解析: 查找 persons 数组 */
            char *p = strstr(line, "\"persons\"");
            if (p) {
                int person_idx = 0;
                /* 遍历每个 person */
                char *scan = p;
                while (person_idx < 8) {
                    char *bbox_start = strstr(scan, "\"bbox\"");
                    if (!bbox_start) break;

                    float bx = 0, by = 0, bw = 0, bh = 0, conf = 0;
                    sscanf(bbox_start, "\"bbox\":[%f,%f,%f,%f]", &bx, &by, &bw, &bh);

                    char *conf_start = strstr(bbox_start, "\"conf\"");
                    if (conf_start) sscanf(conf_start, "\"conf\":%f", &conf);

                    detections[person_idx][0] = bx;
                    detections[person_idx][1] = by;
                    detections[person_idx][2] = bw - bx;
                    detections[person_idx][3] = bh - by;
                    detections[person_idx][4] = conf;

                    result->persons[person_idx].confidence = conf;
                    result->persons[person_idx].bbox[0] = bx;
                    result->persons[person_idx].bbox[1] = by;
                    result->persons[person_idx].bbox[2] = bw - bx;
                    result->persons[person_idx].bbox[3] = bh - by;

                    /* 解析 17 个关键点 */
                    char *kps_start = strstr(bbox_start, "\"keypoints\"");
                    if (kps_start) {
                        char *kps_scan = kps_start;
                        for (int k = 0; k < 17; k++) {
                            char *kx = strstr(kps_scan, "\"x\":");
                            if (!kx) break;
                            char *ky = strstr(kx + 4, "\"y\":");
                            char *ks = strstr(ky ? ky : kx + 4, "\"score\":");
                            if (kx && ky && ks) {
                                float x = 0, y = 0, s = 0;
                                sscanf(kx, "\"x\":%f", &x);
                                sscanf(ky, "\"y\":%f", &y);
                                sscanf(ks, "\"score\":%f", &s);
                                result->persons[person_idx].keypoints[k].x = x;
                                result->persons[person_idx].keypoints[k].y = y;
                                result->persons[person_idx].keypoints[k].score = s;
                            }
                            /* 移到下一个 keypoint: 从 "score": 之后搜索下一个 "x": */
                            char *next = strstr(ks + 8, "\"x\":");
                            if (next) kps_scan = next;
                            else break;
                        }
                    }

                    person_idx++;
                    scan = bbox_start + 10;
                }
                *count = person_idx;
                result->person_count = person_idx;
                if (ran_detect) *ran_detect = 1;

                LOG_I(LOG_TAG_AI, "Frame #%d parsed: %d persons, kp0=(%.1f,%.1f) sc=%.2f",
                      frame_seq, person_idx,
                      result->persons[0].keypoints[0].x,
                      result->persons[0].keypoints[0].y,
                      result->persons[0].keypoints[0].score);
            } else {
                LOG_W(LOG_TAG_AI, "Frame #%d: no 'persons' in reply", frame_seq);
            }
        } else {
            LOG_E(LOG_TAG_AI, "Frame #%d: Python subprocess crashed, restarting...", frame_seq);
            spawn_python_subprocess();
        }
    } else {
        /* 回退: 基础 mock (无人) */
        *count = 0;
        result->person_count = 0;
    }
#endif

    g_ai.inference_time_ms = (rt_tick_get() - start) * 1000 / RT_TICK_PER_SECOND;
    return FALL_OK;
}

/* ========== 生命周期 ========== */

void ai_engine_deinit(void)
{
#ifdef BSP_USING_K230
    if (g_ai.pose_loaded) kpu_unload(&g_ai.pose_ctx);
    if (g_ai.temporal_loaded) kpu_unload(&g_ai.temporal_ctx);
    kpu_deinit();
#endif

#ifdef RT_USING_MOCK
    if (g_ai.py_stdin) {
        fclose(g_ai.py_stdin);
        g_ai.py_stdin = NULL;
    }
    if (g_ai.py_stdout) {
        fclose(g_ai.py_stdout);
        g_ai.py_stdout = NULL;
    }
    if (g_ai.py_pid > 0) {
        kill(g_ai.py_pid, SIGTERM);
        waitpid(g_ai.py_pid, NULL, 0);
        g_ai.py_pid = 0;
    }
    g_ai.py_ready = 0;
#endif

    rt_memset(&g_ai, 0, sizeof(g_ai));
    LOG_I(LOG_TAG_AI, "AI engine deinitialized");
}

int ai_engine_get_inference_time_ms(void)
{
    return g_ai.inference_time_ms;
}
