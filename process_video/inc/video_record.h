/**
 * video_record.h - 视频录制接口
 */
#ifndef __VIDEO_RECORD_H__
#define __VIDEO_RECORD_H__

#include "fall_common.h"

/**
 * 初始化录制模块
 */
fall_err_t video_record_init(void);

/**
 * 开始录制
 * @param event_id 事件ID (用作文件名)
 * @return FALL_OK 成功
 */
fall_err_t video_record_start(const char *event_id);

/**
 * 停止录制
 * @return 文件路径
 */
const char *video_record_stop(void);

/**
 * 写入一帧到录制缓冲
 * @param frame 视频帧
 */
void video_record_write_frame(video_frame_t *frame);

/**
 * 获取录制状态
 * @return 1=录制中, 0=未录制
 */
int video_record_is_recording(void);

/**
 * 反初始化录制模块
 */
void video_record_deinit(void);

#endif /* __VIDEO_RECORD_H__ */
