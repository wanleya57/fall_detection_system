/**
 * fall_detect.h - 跌倒检测算法接口
 */
#ifndef __FALL_DETECT_H__
#define __FALL_DETECT_H__

#include "fall_common.h"
#include "ipc_protocol.h"
#include "config.h"

/**
 * 初始化跌倒检测模块
 * @return FALL_OK 成功
 */
fall_err_t fall_detect_init(void);

/**
 * 分析一帧并判断跌倒
 * @param frame 视频帧
 * @param result 输出跌倒检测结果
 * @param pose_out 输出姿态数据 (可选, NULL=不输出)
 * @param ran_detect 输出: 1=本次实际执行了检测, 0=跳过 (可选, NULL=不输出)
 * @return FALL_OK 有结果, FALL_ERR_EMPTY 无结果
 */
fall_err_t fall_detect_analyze(video_frame_t *frame, fall_result_t *result,
                               pose_result_t *pose_out, int *ran_detect);

/**
 * 获取当前跌倒状态
 */
fall_state_t fall_detect_get_state(void);

/**
 * 手动复位跌倒状态
 */
void fall_detect_reset(void);

/**
 * 获取检测统计信息
 */
void fall_detect_get_stats(uint32_t *total_frames, uint32_t *detected_frames,
                            uint32_t *false_positives);

/**
 * 反初始化
 */
void fall_detect_deinit(void);

#endif /* __FALL_DETECT_H__ */
