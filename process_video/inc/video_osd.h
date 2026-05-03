/**
 * video_osd.h - OSD 叠加渲染接口
 */
#ifndef __VIDEO_OSD_H__
#define __VIDEO_OSD_H__

#include "fall_common.h"
#include "ipc_protocol.h"

/**
 * 初始化 OSD 模块
 */
fall_err_t video_osd_init(void);

/**
 * 处理 OSD 指令
 * @param cmd OSD 指令
 */
void video_osd_process_cmd(osd_cmd_t *cmd);

/**
 * 渲染 OSD 到帧缓冲
 * @param frame 目标帧 (RGB888)
 */
void video_osd_render(uint8_t *frame);

/**
 * 清除所有 OSD
 */
void video_osd_clear_all(void);

/**
 * 反初始化 OSD
 */
void video_osd_deinit(void);

#endif /* __VIDEO_OSD_H__ */
