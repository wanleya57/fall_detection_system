/**
 * pose_overlay.h - 骨架叠加渲染
 *
 * AI 线程写入 pose, 采集线程在 JPEG 编码前绘制骨架到 YUV 帧
 * 内存占用: ~2KB (适合 K230)
 */
#ifndef __POSE_OVERLAY_H__
#define __POSE_OVERLAY_H__

#include "fall_common.h"

void pose_overlay_init(void);
void pose_overlay_deinit(void);

/* AI 线程调用: 更新最新骨架数据 */
void pose_overlay_update(const pose_result_t *pose);

/* 采集线程调用: 在 YUV 帧上绘制骨架 (JPEG 编码前) */
void pose_overlay_draw(uint8_t *yuv_frame, int width, int height);

#endif /* __POSE_OVERLAY_H__ */
