/**
 * video_display.h - TFT 显示接口
 */
#ifndef __VIDEO_DISPLAY_H__
#define __VIDEO_DISPLAY_H__

#include "fall_common.h"

/**
 * 初始化 TFT LCD
 * @return FALL_OK 成功
 */
fall_err_t video_display_init(void);

/**
 * 显示一帧视频
 * @param frame 视频帧数据
 * @return FALL_OK 成功
 */
fall_err_t video_display_frame(video_frame_t *frame);

/**
 * 清除屏幕
 */
void video_display_clear(void);

/**
 * 反初始化显示
 */
void video_display_deinit(void);

#endif /* __VIDEO_DISPLAY_H__ */
