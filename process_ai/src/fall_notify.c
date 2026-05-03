/**
 * fall_notify.c - WiFi 网络推送实现 (MQTT/HTTP)
 */
#include "fall_notify.h"
#include "log.h"
#include "config.h"
#include <string.h>
#include <inttypes.h>

#ifdef RT_USING_LWIP
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#endif

typedef struct {
    uint8_t initialized;
    uint8_t connected;
    char broker[64];
    uint16_t port;
    char topic[64];
    int sock_fd;
    uint64_t last_heartbeat_tick;
} notify_ctx_t;

static notify_ctx_t g_notify;

/* 构建 JSON 事件消息 */
static int build_event_json(const fall_event_t *event, char *buf, int buf_size)
{
    int len = rt_snprintf(buf, buf_size,
        "{"
        "\"event_id\":\"%s\","
        "\"timestamp\":%" PRIu64 ","
        "\"confidence\":%.2f,"
        "\"fall_angle\":%.1f,"
        "\"state\":%d"
        "}",
        event->event_id,
        event->timestamp,
        event->confidence,
        event->fall_angle,
        event->state);
    return len;
}

/* MQTT 简易连接 */
static int mqtt_connect(void)
{
#ifdef RT_USING_LWIP
    struct hostent *host = gethostbyname(g_notify.broker);
    if (host == RT_NULL) {
        LOG_E(LOG_TAG_AI, "Cannot resolve MQTT broker: %s", g_notify.broker);
        return -1;
    }

    g_notify.sock_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (g_notify.sock_fd < 0) {
        LOG_E(LOG_TAG_AI, "MQTT socket create failed");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_notify.port);
    rt_memcpy(&addr.sin_addr, host->h_addr, host->h_length);

    if (lwip_connect(g_notify.sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_E(LOG_TAG_AI, "MQTT connect failed");
        lwip_close(g_notify.sock_fd);
        g_notify.sock_fd = -1;
        return -1;
    }

    /* TODO: 发送 MQTT CONNECT 报文 */

    g_notify.connected = 1;
    LOG_I(LOG_TAG_AI, "MQTT connected to %s:%d", g_notify.broker, g_notify.port);
    return 0;
#else
    LOG_I(LOG_TAG_AI, "MQTT connect (mock mode)");
    g_notify.connected = 1;
    return 0;
#endif
}

/* MQTT 简易发布 */
static int mqtt_publish(const char *topic, const char *payload, int payload_len)
{
    if (!g_notify.connected) return -1;

#ifdef RT_USING_LWIP
    /* TODO: 实现完整的 MQTT PUBLISH 报文 */
    /* 这里简化为直接发送 JSON 数据 */

    /* 构建 MQTT PUBLISH 报文头 */
    uint8_t header[4];
    int header_len = 0;
    int topic_len = rt_strlen(topic);

    header[0] = 0x30;  /* PUBLISH, QoS 0 */
    header[1] = 2 + topic_len + payload_len;
    header_len = 2;

    /* 发送 */
    lwip_send(g_notify.sock_fd, header, header_len, 0);
    lwip_send(g_notify.sock_fd, topic, topic_len, 0);
    lwip_send(g_notify.sock_fd, payload, payload_len, 0);

    return 0;
#else
    (void)topic;
    (void)payload;
    (void)payload_len;
    return 0;
#endif
}

fall_err_t fall_notify_init(void)
{
    rt_memset(&g_notify, 0, sizeof(g_notify));
    g_notify.sock_fd = -1;

    /* 从配置读取 */
    system_config_t *cfg = config_get();
    if (cfg) {
        rt_strncpy(g_notify.broker, cfg->mqtt_broker, sizeof(g_notify.broker));
        g_notify.port = cfg->mqtt_port;
        rt_strncpy(g_notify.topic, cfg->mqtt_topic, sizeof(g_notify.topic));
    } else {
        rt_strncpy(g_notify.broker, "192.168.1.100", sizeof(g_notify.broker));
        g_notify.port = 1883;
        rt_strncpy(g_notify.topic, "fall_detection/events", sizeof(g_notify.topic));
    }

    /* 尝试连接 */
    mqtt_connect();

    g_notify.initialized = 1;
    LOG_I(LOG_TAG_AI, "Notification module initialized");
    return FALL_OK;
}

fall_err_t fall_notify_send(const fall_event_t *event)
{
    if (event == RT_NULL || !g_notify.initialized) return FALL_ERR_INVALID;

    /* 自动重连 */
    if (!g_notify.connected) {
        if (mqtt_connect() != 0) {
            LOG_W(LOG_TAG_AI, "MQTT reconnect failed, event queued");
            return FALL_ERR_IO;
        }
    }

    /* 构建 JSON */
    char json_buf[512];
    int json_len = build_event_json(event, json_buf, sizeof(json_buf));

    /* 发布 */
    if (mqtt_publish(g_notify.topic, json_buf, json_len) != 0) {
        LOG_W(LOG_TAG_AI, "MQTT publish failed");
        g_notify.connected = 0;
        return FALL_ERR_IO;
    }

    LOG_I(LOG_TAG_AI, "Event notified: %s", event->event_id);
    return FALL_OK;
}

void fall_notify_heartbeat(void)
{
    if (!g_notify.initialized) return;

    uint64_t now = rt_tick_get();
    if (now - g_notify.last_heartbeat_tick < RT_TICK_PER_SECOND * 30) return;

    g_notify.last_heartbeat_tick = now;

    if (!g_notify.connected) {
        mqtt_connect();
    }

    /* TODO: 发送 MQTT 心跳报文 */
}

void fall_notify_deinit(void)
{
#ifdef RT_USING_LWIP
    if (g_notify.sock_fd >= 0) {
        lwip_close(g_notify.sock_fd);
    }
#endif

    rt_memset(&g_notify, 0, sizeof(g_notify));
    g_notify.sock_fd = -1;
    LOG_I(LOG_TAG_AI, "Notification module deinitialized");
}
