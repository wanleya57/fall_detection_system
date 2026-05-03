/**
 * video_display.c - TFT LCD 显示实现
 */
#include "video_display.h"
#include "log.h"
#include <string.h>

#ifdef BSP_USING_K230
#include <k230_lcd.h>
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  initialized;
    uint8_t *framebuffer;
} display_ctx_t;

static display_ctx_t g_display;

fall_err_t video_display_init(void)
{
    rt_memset(&g_display, 0, sizeof(g_display));

#ifdef BSP_USING_K230
    /* 初始化 TFT LCD */
    k230_lcd_config_t lcd_cfg = {
        .width = VIDEO_WIDTH,
        .height = VIDEO_HEIGHT,
        .format = K230_LCD_FMT_RGB565,
    };
    if (k230_lcd_init(&lcd_cfg) != 0) {
        LOG_E(LOG_TAG_VIDEO, "LCD init failed");
        return FALL_ERR_IO;
    }

    g_display.width = VIDEO_WIDTH;
    g_display.height = VIDEO_HEIGHT;
#else
    g_display.width = VIDEO_WIDTH;
    g_display.height = VIDEO_HEIGHT;
    LOG_I(LOG_TAG_VIDEO, "LCD init (mock mode)");
#endif

    /* 分配显示缓冲 */
    g_display.framebuffer = rt_malloc(VIDEO_WIDTH * VIDEO_HEIGHT * 2);
    if (g_display.framebuffer == RT_NULL) {
        LOG_E(LOG_TAG_VIDEO, "Failed to allocate display buffer");
        return FALL_ERR_NOMEM;
    }

    g_display.initialized = 1;
    LOG_I(LOG_TAG_VIDEO, "Display initialized: %dx%d", g_display.width, g_display.height);
    return FALL_OK;
}

fall_err_t video_display_frame(video_frame_t *frame)
{
    if (frame == RT_NULL || !g_display.initialized) return FALL_ERR_INVALID;

#ifdef BSP_USING_K230
    /* YUV420 → RGB565 转换并送到 LCD */
    k230_lcd_draw_yuv420(frame->frame_buf,
                          frame->width, frame->height,
                          0, 0);
#else
    /* 模拟显示 (开发调试) */
    (void)frame;
#endif

    return FALL_OK;
}

void video_display_clear(void)
{
    if (!g_display.initialized) return;

#ifdef BSP_USING_K230
    k230_lcd_clear(0x0000);
#endif
}

void video_display_deinit(void)
{
    if (g_display.framebuffer) {
        rt_free(g_display.framebuffer);
        g_display.framebuffer = RT_NULL;
    }

#ifdef BSP_USING_K230
    k230_lcd_deinit();
#endif

    g_display.initialized = 0;
    LOG_I(LOG_TAG_VIDEO, "Display deinitialized");
}
