/**
 * video_rtsp.h - RTSP/HTTP 视频流服务接口
 */
#ifndef __VIDEO_RTSP_H__
#define __VIDEO_RTSP_H__

#include "fall_common.h"

/**
 * 启动 HTTP 视频服务
 * @param port 监听端口 (默认 8554)
 * @return FALL_OK 成功
 */
fall_err_t video_rtsp_start(uint16_t port);

/**
 * 停止 HTTP 视频服务
 */
void video_rtsp_stop(void);

/**
 * 推送一帧到 HTTP 流
 * @param frame 视频帧
 */
void video_rtsp_push_frame(video_frame_t *frame);

/**
 * 推送一帧到 HTTP 流 (带骨架叠加)
 * AI 线程调用: 先画骨架再编码
 * @param frame 视频帧 (会被修改: 骨架叠加写入 YUV 数据)
 */
void video_rtsp_push_frame_with_overlay(video_frame_t *frame);

/**
 * 推送跌倒事件到 Web 仪表盘
 * @param event 跌倒事件信息
 */
void video_rtsp_push_event(const fall_event_t *event);

/**
 * 获取当前 HTTP 连接数
 */
int video_rtsp_get_clients(void);

#endif /* __VIDEO_RTSP_H__ */
