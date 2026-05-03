/**
 * video_capture.h - 视频采集接口
 */
#ifndef __VIDEO_CAPTURE_H__
#define __VIDEO_CAPTURE_H__

#include "fall_common.h"

/**
 * 初始化摄像头
 * @param width  分辨率宽
 * @param height 分辨率高
 * @param fps    帧率
 * @return FALL_OK 成功
 */
fall_err_t video_capture_init(uint16_t width, uint16_t height, uint8_t fps);

/**
 * 启动视频采集
 */
fall_err_t video_capture_start(void);

/**
 * 停止视频采集
 */
fall_err_t video_capture_stop(void);

/**
 * 获取一帧视频 (阻塞)
 * @param frame 输出帧缓冲
 * @return FALL_OK 成功
 */
fall_err_t video_capture_get_frame(video_frame_t *frame);

/**
 * 释放帧缓冲
 */
void video_capture_release_frame(video_frame_t *frame);

/**
 * 反初始化摄像头
 */
void video_capture_deinit(void);

#endif /* __VIDEO_CAPTURE_H__ */
