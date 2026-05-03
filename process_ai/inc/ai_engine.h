/**
 * ai_engine.h - AI 推理引擎 (YOLO11n-pose + 时序分类器)
 *
 * 两阶段架构:
 *   Stage 1 (KPU):  YOLO11n-pose → 每帧 bbox + 17 关键点
 *   Stage 2 (CPU):  LSTM/1D-CNN/TCN → 连续帧动作分类
 *
 * 动作类别:
 *   0=站立  1=行走  2=坐下  3=蹲下  4=摔倒  5=躺倒
 *
 * K230:  YOLO11n-pose 跑 KPU (INT8 ~8ms)，分类器跑 CPU (~1ms)
 * PC:    模拟完整摔倒场景
 */
#ifndef __AI_ENGINE_H__
#define __AI_ENGINE_H__

#include "fall_common.h"

/* ========== 动作类别 ========== */
typedef enum {
    ACTION_STANDING = 0,
    ACTION_WALKING,
    ACTION_SITTING,
    ACTION_CROUCHING,
    ACTION_FALLING,     /* ← 触发跌倒告警 */
    ACTION_LYING,
    ACTION_COUNT,
} action_class_t;

/* ========== 模型路径 (K230 SD 卡) ========== */
#define MODEL_PATH_YOLO11N_POSE  "/sdcard/model/yolo11n_pose_k230.kmodel"
#define MODEL_PATH_TEMPORAL      "/sdcard/model/temporal_classifier.kmodel"

/* ========== 时序模型类型选择 ========== */
/* 取消注释一个，或通过 Makefile -D 传入 */
/* #define TEMPORAL_MODEL_LSTM     */  /* LSTM: 最经典，~50K 参数 */
/* #define TEMPORAL_MODEL_1DCNN    */  /* 1D-CNN: 最快，~30K 参数 */
/* #define TEMPORAL_MODEL_TCN      */  /* TCN: 感受野大，~80K 参数 */

/* 默认: LSTM */
#if !defined(TEMPORAL_MODEL_LSTM) && !defined(TEMPORAL_MODEL_1DCNN) && \
    !defined(TEMPORAL_MODEL_TCN)
#define TEMPORAL_MODEL_LSTM
#endif

/* ========== 时序特征参数 ========== */
#define FEATURE_PER_FRAME   41      /* 每帧特征维度 */
#define TEMPORAL_WINDOW     30      /* 滑动窗口大小 (1秒@30fps) */
#define FEATURE_BUF_SIZE    (FEATURE_PER_FRAME * TEMPORAL_WINDOW)

/* 每帧提取的特征:
 *   17 关键点 × 2 (x,y)    = 34
 *   bbox (x,y,w,h)          =  4
 *   中心速度 (dx,dy)        =  2
 *   身体倾斜角              =  1
 *   总计                    = 41
 */

/* ========== 模型方案信息 ========== */
typedef struct {
    const char *spatial_model;      /* 空间模型名 */
    const char *temporal_model;     /* 时序模型名 */
    const char *spatial_path;       /* kmodel 路径 */
    const char *temporal_path;      /* kmodel 路径 */
    int spatial_ms;                 /* 空间推理耗时 */
    int temporal_ms;                /* 时序推理耗时 */
} ai_pipeline_info_t;

/* ========== 公共 API ========== */

/**
 * 初始化 AI 引擎
 */
fall_err_t ai_engine_init(void);

/**
 * 加载模型
 * @param pose_path    YOLO11n-pose kmodel 路径
 * @param temporal_path 时序分类器 kmodel 路径 (可选，NULL=用规则引擎)
 */
fall_err_t ai_engine_load_model(const char *pose_path, const char *temporal_path);

/**
 * 单帧推理: YOLO11n-pose → bbox + keypoints
 * @param ran_detect  输出: 1=本次实际执行了检测, 0=跳过(可沿用上次结果)
 */
fall_err_t ai_engine_detect_and_pose(video_frame_t *frame,
                                      float (*detections)[5],
                                      int max_det, int *count,
                                      pose_result_t *result,
                                      int *ran_detect);

/**
 * 时序分类: 将当前帧特征加入滑动窗口，输出动作类别
 *
 * @param feat    当前帧特征 (41维，可由 ai_engine_extract_feature 生成)
 * @param output  输出 ACTION_COUNT 个类别的概率
 * @return 动作类别
 */
action_class_t ai_engine_classify_action(float *feat, float *output);

/**
 * 从检测结果提取特征向量 (41维)
 */
void ai_engine_extract_feature(pose_result_t *pose, float (*detections)[5],
                                int det_count, float *feat_out);

/**
 * 获取动作类别名称
 */
const char *ai_engine_action_name(action_class_t action);

/**
 * 反初始化
 */
void ai_engine_deinit(void);

/**
 * 获取推理耗时 (ms)
 */
int ai_engine_get_inference_time_ms(void);

/**
 * 获取当前模型方案信息
 */
void ai_engine_get_pipeline_info(ai_pipeline_info_t *info);

#endif /* __AI_ENGINE_H__ */
