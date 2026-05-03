/**
 * config.h - 系统配置管理
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "fall_common.h"

/* 默认配置文件路径 */
#define CONFIG_FILE_PATH            "/sdcard/config.json"

/* 默认值 */
#define DEFAULT_VIDEO_WIDTH         720
#define DEFAULT_VIDEO_HEIGHT        480
#define DEFAULT_VIDEO_FPS           30
#define DEFAULT_CONFIDENCE_THRESH   0.6f
#define DEFAULT_CONFIRM_FRAMES      5
#define DEFAULT_ANGLE_THRESHOLD     45.0f
#define DEFAULT_VELOCITY_THRESH     0.3f
#define DEFAULT_COOLDOWN_MS         10000
#define DEFAULT_ALERT_ENABLED       1
#define DEFAULT_BUZZER_ENABLED      1
#define DEFAULT_NOTIFY_ENABLED      1
#define DEFAULT_RECORD_ENABLED      1
#define DEFAULT_MQTT_PORT           1883
#define DEFAULT_MQTT_TOPIC          "fall_detection/events"

/**
 * 加载配置文件
 * @param config 配置结构体指针
 * @return FALL_OK 成功
 */
fall_err_t config_load(system_config_t *config);

/**
 * 保存配置文件
 * @param config 配置结构体指针
 * @return FALL_OK 成功
 */
fall_err_t config_save(const system_config_t *config);

/**
 * 加载默认配置
 */
void config_load_default(system_config_t *config);

/**
 * 获取当前配置 (单例)
 */
system_config_t *config_get(void);

/**
 * 初始化配置管理
 */
fall_err_t config_init(void);

#endif /* __CONFIG_H__ */
