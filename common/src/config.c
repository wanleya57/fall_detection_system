/**
 * config.c - 配置管理实现
 */
#include "config.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

static system_config_t g_config;
static uint8_t g_config_loaded = 0;

void config_load_default(system_config_t *config)
{
    config->video_width = DEFAULT_VIDEO_WIDTH;
    config->video_height = DEFAULT_VIDEO_HEIGHT;
    config->video_fps = DEFAULT_VIDEO_FPS;

    config->confidence_threshold = DEFAULT_CONFIDENCE_THRESH;
    config->confirm_frames = DEFAULT_CONFIRM_FRAMES;
    config->angle_threshold = DEFAULT_ANGLE_THRESHOLD;
    config->velocity_threshold = DEFAULT_VELOCITY_THRESH;
    config->cooldown_ms = DEFAULT_COOLDOWN_MS;

    config->alert_enabled = DEFAULT_ALERT_ENABLED;
    config->buzzer_enabled = DEFAULT_BUZZER_ENABLED;
    config->notification_enabled = DEFAULT_NOTIFY_ENABLED;
    config->recording_enabled = DEFAULT_RECORD_ENABLED;

    rt_strncpy(config->wifi_ssid, "K230_AP", sizeof(config->wifi_ssid));
    rt_strncpy(config->wifi_password, "12345678", sizeof(config->wifi_password));
    rt_strncpy(config->mqtt_broker, "192.168.1.100", sizeof(config->mqtt_broker));
    config->mqtt_port = DEFAULT_MQTT_PORT;
    rt_strncpy(config->mqtt_topic, DEFAULT_MQTT_TOPIC, sizeof(config->mqtt_topic));
}

fall_err_t config_load(system_config_t *config)
{
    if (config == RT_NULL) return FALL_ERR_INVALID;

    rt_file_t fp = rt_fopen(CONFIG_FILE_PATH, "r");
    if (fp == RT_NULL) {
        LOG_W(LOG_TAG_SYSTEM, "Config file not found, using defaults");
        config_load_default(config);
        return FALL_OK;
    }

    /* 读取文件内容 (简化版JSON解析) */
    char buf[1024];
    rt_size_t len = rt_fread(buf, 1, sizeof(buf) - 1, fp);
    rt_fclose(fp);

    if (len == 0) {
        LOG_W(LOG_TAG_SYSTEM, "Config file empty, using defaults");
        config_load_default(config);
        return FALL_OK;
    }
    buf[len] = '\0';

    /* 简单的键值解析 (实际项目可用 cJSON 库) */
    config_load_default(config);  /* 先加载默认值 */

    char *val;

    val = rt_strstr(buf, "\"video_width\"");
    if (val) config->video_width = (uint16_t)atoi(val + 13);

    val = rt_strstr(buf, "\"video_height\"");
    if (val) config->video_height = (uint16_t)atoi(val + 14);

    val = rt_strstr(buf, "\"video_fps\"");
    if (val) config->video_fps = (uint8_t)atoi(val + 11);

    val = rt_strstr(buf, "\"confidence_threshold\"");
    if (val) config->confidence_threshold = (float)atof(val + 22);

    val = rt_strstr(buf, "\"confirm_frames\"");
    if (val) config->confirm_frames = atoi(val + 16);

    val = rt_strstr(buf, "\"cooldown_ms\"");
    if (val) config->cooldown_ms = atoi(val + 13);

    val = rt_strstr(buf, "\"alert_enabled\"");
    if (val) config->alert_enabled = (uint8_t)atoi(val + 15);

    val = rt_strstr(buf, "\"wifi_ssid\"");
    if (val) {
        val = rt_strchr(val, ':');
        if (val) {
            val++;
            while (*val == ' ') val++;
            if (*val == '"') val++;
            char *end = rt_strchr(val, '"');
            if (end) {
                rt_size_t copy_len = (rt_size_t)(end - val) < sizeof(config->wifi_ssid) - 1
                                   ? (rt_size_t)(end - val) : sizeof(config->wifi_ssid) - 1;
                rt_memcpy(config->wifi_ssid, val, copy_len);
                config->wifi_ssid[copy_len] = '\0';
            }
        }
    }

    val = rt_strstr(buf, "\"mqtt_broker\"");
    if (val) {
        val = rt_strchr(val, ':');
        if (val) {
            val++;
            while (*val == ' ') val++;
            if (*val == '"') val++;
            char *end = rt_strchr(val, '"');
            if (end) {
                rt_size_t copy_len = (rt_size_t)(end - val) < sizeof(config->mqtt_broker) - 1
                                   ? (rt_size_t)(end - val) : sizeof(config->mqtt_broker) - 1;
                rt_memcpy(config->mqtt_broker, val, copy_len);
                config->mqtt_broker[copy_len] = '\0';
            }
        }
    }

    val = rt_strstr(buf, "\"mqtt_port\"");
    if (val) config->mqtt_port = (uint16_t)atoi(val + 11);

    LOG_I(LOG_TAG_SYSTEM, "Config loaded: %dx%d@%dfps, conf_thresh=%.2f",
          config->video_width, config->video_height, config->video_fps,
          config->confidence_threshold);

    g_config_loaded = 1;
    return FALL_OK;
}

fall_err_t config_save(const system_config_t *config)
{
    if (config == RT_NULL) return FALL_ERR_INVALID;

    rt_file_t fp = rt_fopen(CONFIG_FILE_PATH, "w");
    if (fp == RT_NULL) {
        LOG_E(LOG_TAG_SYSTEM, "Failed to open config file for writing");
        return FALL_ERR_IO;
    }

    char buf[1024];
    rt_snprintf(buf, sizeof(buf),
        "{\n"
        "  \"video_width\": %d,\n"
        "  \"video_height\": %d,\n"
        "  \"video_fps\": %d,\n"
        "  \"confidence_threshold\": %d.%d,\n"
        "  \"confirm_frames\": %d,\n"
        "  \"angle_threshold\": %d.%d,\n"
        "  \"velocity_threshold\": %d.%d,\n"
        "  \"cooldown_ms\": %d,\n"
        "  \"alert_enabled\": %d,\n"
        "  \"buzzer_enabled\": %d,\n"
        "  \"notification_enabled\": %d,\n"
        "  \"recording_enabled\": %d,\n"
        "  \"wifi_ssid\": \"%s\",\n"
        "  \"wifi_password\": \"%s\",\n"
        "  \"mqtt_broker\": \"%s\",\n"
        "  \"mqtt_port\": %d,\n"
        "  \"mqtt_topic\": \"%s\"\n"
        "}\n",
        config->video_width, config->video_height, config->video_fps,
        (int)config->confidence_threshold,
        (int)((config->confidence_threshold - (int)config->confidence_threshold) * 10),
        config->confirm_frames,
        (int)config->angle_threshold,
        (int)((config->angle_threshold - (int)config->angle_threshold) * 10),
        (int)config->velocity_threshold,
        (int)((config->velocity_threshold - (int)config->velocity_threshold) * 10),
        config->cooldown_ms,
        config->alert_enabled, config->buzzer_enabled,
        config->notification_enabled, config->recording_enabled,
        config->wifi_ssid, config->wifi_password,
        config->mqtt_broker, config->mqtt_port, config->mqtt_topic);

    rt_size_t written = rt_fwrite(buf, 1, rt_strlen(buf), fp);
    rt_fclose(fp);

    if (written == 0) {
        LOG_E(LOG_TAG_SYSTEM, "Failed to write config file");
        return FALL_ERR_IO;
    }

    LOG_I(LOG_TAG_SYSTEM, "Config saved to %s", CONFIG_FILE_PATH);
    return FALL_OK;
}

system_config_t *config_get(void)
{
    return &g_config;
}

fall_err_t config_init(void)
{
    rt_memset(&g_config, 0, sizeof(g_config));
    return config_load(&g_config);
}
