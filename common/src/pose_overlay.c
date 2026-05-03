/**
 * pose_overlay.c - 骨架叠加渲染
 *
 * 在 YUV420 帧的 Y 平面上绘制:
 *   - 17 个关键点 (圆点)
 *   - 骨架连线 (线段)
 *   - 置信度标签
 *
 * K230 优化: 纯整数运算, 无动态内存
 */
#include "pose_overlay.h"
#include "log.h"
#include <string.h>

/* COCO 17 关键点骨架连接 */
static const struct { int a, b; } SKELETON[] = {
    {0,1},{1,2},{2,3},{3,4},       /* 头: 鼻→左眼→左耳, 鼻→右眼→右耳 */
    {1,5},{5,6},{6,7},             /* 左臂 */
    {1,8},{8,9},{9,10},            /* 右臂 */
    {5,11},{11,12},{12,13},        /* 左腿 */
    {8,14},{14,15},{15,16},        /* 右腿 */
    {11,14},                       /* 胯 */
};
#define SKELETON_COUNT (sizeof(SKELETON)/sizeof(SKELETON[0]))

/* 关键点轮廓亮度 (YUV Y 分量) - 黑色轮廓确保在任何背景上可见 */
#define KP_OUTLINE  0    /* 黑色轮廓 */
#define KP_FILL     255  /* 白色填充 */
#define SKEL_LINE   0    /* 黑色骨骼线 */
#define SKEL_WIDTH  3    /* 骨骼线半宽 (总宽 = 2*SKEL_WIDTH+1) */

static pose_result_t g_pose;
static int g_has_pose;

void pose_overlay_init(void)
{
    rt_memset(&g_pose, 0, sizeof(g_pose));
    g_has_pose = 0;
    LOG_I(LOG_TAG_SYSTEM, "Pose overlay initialized");
}

void pose_overlay_deinit(void)
{
    g_has_pose = 0;
}

void pose_overlay_update(const pose_result_t *pose)
{
    if (!pose) return;
    rt_memcpy(&g_pose, pose, sizeof(pose_result_t));
    g_has_pose = (pose->person_count > 0);
    if (g_has_pose) {
        LOG_D(LOG_TAG_SYSTEM, "pose_update: %d persons, kp0=(%.0f,%.0f) sc=%.2f",
              g_pose.person_count,
              g_pose.persons[0].keypoints[0].x,
              g_pose.persons[0].keypoints[0].y,
              g_pose.persons[0].keypoints[0].score);
    }
}

/* ---- YUV 绘图原语 ---- */

static inline void put_pixel(uint8_t *y_plane, int w, int h, int x, int y, uint8_t val)
{
    if (x >= 0 && x < w && y >= 0 && y < h)
        y_plane[y * w + x] = val;
}

/* 在 U/V 平面上画彩色点 (半分辨率) */
static void put_uv_pixel(uint8_t *u_plane, uint8_t *v_plane,
                         int w, int h, int x, int y, uint8_t u, uint8_t v)
{
    int ux = x / 2, uy = y / 2;
    int uv_w = w / 2, uv_h = h / 2;
    if (ux >= 0 && ux < uv_w && uy >= 0 && uy < uv_h) {
        u_plane[uy * uv_w + ux] = u;
        v_plane[uy * uv_w + ux] = v;
    }
}

/* 在 U/V 平面上画彩色圆 (半分辨率) */
static void draw_uv_dot(uint8_t *u_plane, uint8_t *v_plane,
                        int w, int h, int cx, int cy, int r, uint8_t u, uint8_t v)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r)
                put_uv_pixel(u_plane, v_plane, w, h, cx + dx, cy + dy, u, v);
        }
    }
}

/* Bresenham 画线 */
static void draw_line(uint8_t *y_plane, int w, int h,
                      int x0, int y0, int x1, int y1, uint8_t val)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        put_pixel(y_plane, w, h, x0, y0, val);
        put_pixel(y_plane, w, h, x0 + 1, y0, val);
        put_pixel(y_plane, w, h, x0, y0 + 1, val);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/* 画实心圆 (点半径 r) */
static void draw_dot(uint8_t *y_plane, int w, int h,
                     int cx, int cy, int r, uint8_t val)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r)
                put_pixel(y_plane, w, h, cx + dx, cy + dy, val);
        }
    }
}

/* ---- 主绘制函数 ---- */

/* 关键点颜色 (U, V) - 用于彩色标记 */
typedef struct { uint8_t u, v; } uv_color_t;
static const uv_color_t KP_COLORS[17] = {
    {128, 128},  /* 0: 鼻 - 灰 (无色) */
    {128, 64},   /* 1: 左眼 - 偏红 */
    {128, 64},   /* 2: 左耳 - 偏红 */
    {64, 128},   /* 3: 右眼 - 偏蓝 */
    {64, 128},   /* 4: 右耳 - 偏蓝 */
    {128, 32},   /* 5: 左肩 - 红 */
    {128, 32},   /* 6: 左肘 - 红 */
    {128, 32},   /* 7: 左腕 - 红 */
    {32, 128},   /* 8: 右肩 - 蓝 */
    {32, 128},   /* 9: 右肘 - 蓝 */
    {32, 128},   /* 10: 右腕 - 蓝 */
    {128, 32},   /* 11: 左髋 - 红 */
    {128, 32},   /* 12: 左膝 - 红 */
    {128, 32},   /* 13: 左踝 - 红 */
    {32, 128},   /* 14: 右髋 - 蓝 */
    {32, 128},   /* 15: 右膝 - 蓝 */
    {32, 128},   /* 16: 右踝 - 蓝 */
};

void pose_overlay_draw(uint8_t *yuv_frame, int width, int height)
{
    if (!g_has_pose || !yuv_frame) {
        static int cnt = 0;
        if (++cnt % 300 == 1)
            LOG_I(LOG_TAG_SYSTEM, "pose_draw: SKIP (has_pose=%d)", g_has_pose);
        return;
    }

    uint8_t *y_plane = yuv_frame;
    int y_size = width * height;
    int uv_w = width / 2, uv_h = height / 2;
    int uv_size = uv_w * uv_h;
    uint8_t *u_plane = yuv_frame + y_size;
    uint8_t *v_plane = yuv_frame + y_size + uv_size;

    LOG_I(LOG_TAG_SYSTEM, "pose_draw: %d persons, w=%d h=%d",
          g_pose.person_count, width, height);

    for (int p = 0; p < g_pose.person_count && p < 8; p++) {
        const keypoint_t *kp = g_pose.persons[p].keypoints;

        LOG_I(LOG_TAG_SYSTEM, "pose_draw person %d: kp0=(%.0f,%.0f) sc=%.2f kp1=(%.0f,%.0f) sc=%.2f",
              p, kp[0].x, kp[0].y, kp[0].score, kp[1].x, kp[1].y, kp[1].score);

        /* 画骨架连线 (黑色, 5像素粗线) */
        for (int s = 0; s < SKELETON_COUNT; s++) {
            int a = SKELETON[s].a, b = SKELETON[s].b;
            if (kp[a].score < 0.3f || kp[b].score < 0.3f) continue;
            for (int off = -SKEL_WIDTH; off <= SKEL_WIDTH; off++) {
                draw_line(y_plane, width, height,
                          (int)kp[a].x + off, (int)kp[a].y,
                          (int)kp[b].x + off, (int)kp[b].y, KP_OUTLINE);
                draw_line(y_plane, width, height,
                          (int)kp[a].x, (int)kp[a].y + off,
                          (int)kp[b].x, (int)kp[b].y + off, KP_OUTLINE);
            }
        }

        /* 画彩色关键点 (Y=白色 + U/V=颜色, 超大圆点) */
        for (int k = 0; k < KEYPOINT_COUNT; k++) {
            if (kp[k].score < 0.3f) continue;
            int kx = (int)kp[k].x, ky = (int)kp[k].y;
            int r = (k == 0) ? 6 : 4;

            /* Y 平面: 黑色轮廓 + 白色填充 */
            draw_dot(y_plane, width, height, kx, ky, r + 2, KP_OUTLINE);
            draw_dot(y_plane, width, height, kx, ky, r, KP_FILL);

            /* U/V 平面: 彩色填充 */
            draw_uv_dot(u_plane, v_plane, width, height,
                        kx, ky, r, KP_COLORS[k].u, KP_COLORS[k].v);
        }

        /* 画边界框 (黑色矩形, 3像素粗) */
        {
            const float *bbox = g_pose.persons[p].bbox;
            int bx0 = (int)bbox[0], by0 = (int)bbox[1];
            int bx1 = (int)bbox[2], by1 = (int)bbox[3];
            for (int off = -1; off <= 1; off++) {
                draw_line(y_plane, width, height, bx0, by0+off, bx1, by0+off, KP_OUTLINE);
                draw_line(y_plane, width, height, bx0, by1+off, bx1, by1+off, KP_OUTLINE);
            }
            for (int off = -1; off <= 1; off++) {
                draw_line(y_plane, width, height, bx0+off, by0, bx0+off, by1, KP_OUTLINE);
                draw_line(y_plane, width, height, bx1+off, by0, bx1+off, by1, KP_OUTLINE);
            }
        }
    }
}
