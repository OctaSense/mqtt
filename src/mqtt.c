/**
 * @file mqtt.c
 * @brief MQTT client implementation
 */

#include "mqtt.h"
#include "mqtt_intl.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

/**
 * @brief Spinlock macros for thread-safe operations
 */
#define LOCK(mqtt) do { \
    while (__sync_lock_test_and_set(&(mqtt)->lock, 1)) { \
        /* Spin until lock acquired */ \
    } \
} while(0)

#define UNLOCK(mqtt) do { \
    __sync_lock_release(&(mqtt)->lock); \
} while(0)

struct mqtt_s {
    mqtt_config_t config;
    mqtt_handler_t handler;
    void *user_data;
    volatile mqtt_state_t state;
    uint16_t next_packet_id;
    uint32_t keepalive_timer;
    uint32_t last_activity;
    bool waiting_pingresp;
    uint8_t missed_pings;

    uint8_t *rbuf;
    size_t rlen;
    size_t rcap;

    volatile int lock;
};


/* Internal helpers */

static size_t get_packet_len(const uint8_t *data, size_t len)
{
    if (len < MQTT_MIN_HEADER_SIZE) return 0;

    uint32_t rem_len = 0;
    size_t mult = MQTT_VARLEN_INITIAL_MULT;
    size_t pos = MQTT_VARLEN_INITIAL_POS;

    do {
        if (pos >= len) return 0;

        uint8_t byte = data[pos];
        rem_len += (byte & MQTT_VARLEN_MASK) * mult;
        mult *= MQTT_VARLEN_MULTIPLIER_BASE;
        pos++;

        if (mult > MQTT_VARLEN_MULTIPLIER_BASE * MQTT_VARLEN_MULTIPLIER_BASE * MQTT_VARLEN_MULTIPLIER_BASE) {
            return 0;
        }
    } while ((data[pos - 1] & MQTT_VARLEN_CONTINUE) != 0);

    return pos + rem_len;
}

static uint8_t* alloc_rbuf(uint8_t *old_buf, size_t old_cap, size_t need, size_t *new_cap_out)
{
    if (old_cap >= need) {
        *new_cap_out = old_cap;
        return old_buf;
    }

    size_t new_cap = old_cap * MQTT_BUFFER_GROWTH_FACTOR;
    if (new_cap < need) new_cap = need;
    if (new_cap < MQTT_REASSEMBLY_BUF_SIZE) new_cap = MQTT_REASSEMBLY_BUF_SIZE;

    uint8_t *new_buf = mqtt_realloc(old_buf, new_cap);
    if (!new_buf) return NULL;

    *new_cap_out = new_cap;
    return new_buf;
}

static uint8_t* detach_rbuf(mqtt_t *mqtt)
{
    uint8_t *buf = mqtt->rbuf;
    mqtt->rbuf = NULL;
    mqtt->rlen = 0;
    mqtt->rcap = 0;
    return buf;
}

/* Public API */

mqtt_t *mqtt_create(const mqtt_config_t *config, const mqtt_handler_t *handler, void *user_data)
{
    if (!config || !handler || !handler->send) return NULL;
    if (!config->client_id || strlen(config->client_id) == 0) return NULL;

    mqtt_t *mqtt = mqtt_calloc(1, sizeof(mqtt_t));
    if (!mqtt) return NULL;

    memcpy(&mqtt->config, config, sizeof(mqtt_config_t));
    memcpy(&mqtt->handler, handler, sizeof(mqtt_handler_t));

    mqtt->user_data = user_data;
    mqtt->state = MQTT_STATE_DISCONNECTED;
    mqtt->next_packet_id = MQTT_PACKET_ID_START;
    mqtt->missed_pings = 0;
    mqtt->rbuf = NULL;
    mqtt->rlen = 0;
    mqtt->rcap = 0;
    mqtt->lock = 0;

    return mqtt;
}

void mqtt_destroy(mqtt_t *mqtt)
{
    if (!mqtt) return;

    if (mqtt->state == MQTT_STATE_CONNECTED || mqtt->state == MQTT_STATE_CONNECTING) {
        mqtt_disconnect(mqtt);
    }

    if (mqtt->rbuf) mqtt_free(mqtt->rbuf);
    mqtt_free(mqtt);
}

int mqtt_connect(mqtt_t *mqtt)
{
    if (!mqtt || mqtt->state != MQTT_STATE_DISCONNECTED) return -1;

    uint8_t *buf = mqtt_malloc(MQTT_MIN_PACKET_BUFFER_SIZE);
    if (!buf) return -1;

    size_t len = mqtt_create_connect_packet(&mqtt->config, buf, MQTT_MIN_PACKET_BUFFER_SIZE);

    int ret = -1;
    if (len > 0 && mqtt->handler.send(buf, len, mqtt->user_data) == (int)len) {
        LOCK(mqtt);
        mqtt->state = MQTT_STATE_CONNECTING;
        mqtt->keepalive_timer = 0;
        mqtt->last_activity = 0;
        UNLOCK(mqtt);
        ret = 0;
    }

    mqtt_free(buf);
    return ret;
}

int mqtt_disconnect(mqtt_t *mqtt)
{
    if (!mqtt || mqtt->state == MQTT_STATE_DISCONNECTED) return -1;

    uint8_t buf[MQTT_DISCONNECT_PACKET_SIZE];
    size_t len = mqtt_create_disconnect_packet(buf, sizeof(buf));

    if (len > 0 && mqtt->handler.send) {
        mqtt->handler.send(buf, len, mqtt->user_data);
    }

    LOCK(mqtt);
    mqtt->state = MQTT_STATE_DISCONNECTED;
    uint8_t *old_buf = detach_rbuf(mqtt);
    UNLOCK(mqtt);

    if (old_buf) mqtt_free(old_buf);

    if (mqtt->handler.on_connection) {
        mqtt->handler.on_connection(false, MQTT_CONN_ACCEPTED, mqtt->user_data);
    }

    return 0;
}

int mqtt_publish(mqtt_t *mqtt, const mqtt_message_t *message)
{
    if (!mqtt || !message || !message->topic) return -1;
    if (mqtt->state != MQTT_STATE_CONNECTED) return -1;

    size_t topic_len = strlen(message->topic);
    size_t req_size = MQTT_PUBLISH_VARLEN_MAX + MQTT_PUBLISH_TOPIC_LEN_SIZE + topic_len + message->payload_len;

    uint8_t *buf = mqtt_malloc(req_size);
    if (!buf) return -1;

    size_t len = mqtt_create_publish_packet(message, buf, req_size);
    if (len == 0) {
        mqtt_free(buf);
        return -1;
    }

    int ret = mqtt->handler.send(buf, len, mqtt->user_data);
    mqtt_free(buf);

    return (ret == (int)len) ? 0 : -1;
}

int mqtt_subscribe(mqtt_t *mqtt, const char **topics, const mqtt_qos_t *qos, size_t count)
{
    if (!mqtt || !topics || !qos || count == 0) return -1;
    if (mqtt->state != MQTT_STATE_CONNECTED) return -1;

    uint8_t *buf = mqtt_malloc(MQTT_MIN_PACKET_BUFFER_SIZE);
    if (!buf) return -1;

    uint16_t pkt_id = mqtt_get_packet_id(mqtt);
    size_t len = mqtt_create_subscribe_packet(topics, qos, count, pkt_id, buf, MQTT_MIN_PACKET_BUFFER_SIZE);

    int ret = -1;
    if (len > 0 && mqtt->handler.send(buf, len, mqtt->user_data) == (int)len) {
        ret = 0;
    }

    mqtt_free(buf);
    return ret;
}

int mqtt_unsubscribe(mqtt_t *mqtt, const char **topics, size_t count)
{
    if (!mqtt || !topics || count == 0) return -1;
    if (mqtt->state != MQTT_STATE_CONNECTED) return -1;

    uint8_t *buf = mqtt_malloc(MQTT_MIN_PACKET_BUFFER_SIZE);
    if (!buf) return -1;

    size_t len = mqtt_create_unsubscribe_packet(topics, count, 0, buf, MQTT_MIN_PACKET_BUFFER_SIZE);

    int ret = -1;
    if (len > 0 && mqtt->handler.send(buf, len, mqtt->user_data) == (int)len) {
        ret = 0;
    }

    mqtt_free(buf);
    return ret;
}

static int process_packet(mqtt_t *mqtt, const uint8_t *data, size_t len)
{
    if (len < MQTT_MIN_HEADER_SIZE) return -1;

    uint8_t pkt_type = (data[0] >> MQTT_FIXED_HEADER_TYPE_SHIFT) & MQTT_FIXED_HEADER_FLAGS_BITS;

    switch (pkt_type) {
        case MQTT_PKT_CONNACK:
            if (len >= MQTT_CONNACK_MIN_SIZE) {
                uint8_t rc = data[MQTT_CONNACK_RC_OFFSET];

                LOCK(mqtt);
                mqtt->state = (rc == MQTT_CONN_ACCEPTED) ? MQTT_STATE_CONNECTED : MQTT_STATE_DISCONNECTED;
                if (rc == MQTT_CONN_ACCEPTED) mqtt->missed_pings = 0;
                UNLOCK(mqtt);

                if (mqtt->handler.on_connection) {
                    mqtt->handler.on_connection(rc == MQTT_CONN_ACCEPTED, (mqtt_conn_return_t)rc, mqtt->user_data);
                }
            }
            break;
            
        case MQTT_PKT_PUBLISH:
            if (len >= MQTT_PUBLISH_MIN_SIZE) {
                size_t pos = MQTT_VARLEN_INITIAL_POS;

                uint32_t rem_len;
                size_t vlen = mqtt_read_variable_length(data + pos, &rem_len);
                if (vlen == 0) return -1;
                pos += vlen;

                char topic[MQTT_TOPIC_BUFFER_SIZE];
                size_t tlen = mqtt_read_string(data + pos, topic, sizeof(topic));
                if (tlen == 0) return -1;
                pos += tlen;

                size_t plen = (pos < len) ? (len - pos) : 0;
                bool retain = (data[0] & MQTT_PUBLISH_FLAG_RETAIN) != 0;

                if (mqtt->handler.on_message) {
                    mqtt_message_t msg = {
                        .topic = topic,
                        .payload = (uint8_t*)(data + pos),
                        .payload_len = plen,
                        .qos = MQTT_QOS_0,
                        .retain = retain
                    };
                    mqtt->handler.on_message(&msg, mqtt->user_data);
                }
            }
            break;

        case MQTT_PKT_SUBACK:
            if (len >= MQTT_SUBACK_MIN_SIZE && mqtt->handler.subscribe_ack) {
                uint16_t pkt_id = (data[MQTT_PACKET_ID_OFFSET] << MQTT_BYTE_SHIFT) | data[MQTT_PACKET_ID_OFFSET + 1];
                size_t cnt = len - MQTT_SUBACK_PAYLOAD_OFFSET;

                if (cnt <= MQTT_MAX_SUBSCRIBE_TOPICS) {
                    mqtt_qos_t codes[MQTT_MAX_SUBSCRIBE_TOPICS];
                    for (size_t i = 0; i < cnt; i++) {
                        codes[i] = (mqtt_qos_t)data[MQTT_SUBACK_PAYLOAD_OFFSET + i];
                    }
                    mqtt->handler.subscribe_ack(pkt_id, codes, cnt, mqtt->user_data);
                }
            }
            break;

        case MQTT_PKT_UNSUBACK:
            if (len >= MQTT_UNSUBACK_MIN_SIZE && mqtt->handler.unsubscribe_ack) {
                uint16_t pkt_id = (data[MQTT_PACKET_ID_OFFSET] << MQTT_BYTE_SHIFT) | data[MQTT_PACKET_ID_OFFSET + 1];
                mqtt->handler.unsubscribe_ack(pkt_id, mqtt->user_data);
            }
            break;

        case MQTT_PKT_PINGRESP:
            LOCK(mqtt);
            mqtt->waiting_pingresp = false;
            mqtt->missed_pings = 0;
            UNLOCK(mqtt);
            break;

        case MQTT_PKT_DISCONNECT:
            LOCK(mqtt);
            mqtt->state = MQTT_STATE_DISCONNECTED;
            UNLOCK(mqtt);

            if (mqtt->handler.on_connection) {
                mqtt->handler.on_connection(false, MQTT_CONN_ACCEPTED, mqtt->user_data);
            }
            break;

        default:
            break;
    }


    return 0;
}

int mqtt_input(mqtt_t *mqtt, const uint8_t *data, size_t len)
{
    if (!mqtt || !data || len == 0) return -1;

    size_t consumed = len;
    const uint8_t *process_data = data;
    size_t process_len = len;
    uint8_t *working_buf = NULL;

    LOCK(mqtt);
    size_t rlen = mqtt->rlen;
    uint8_t *rbuf = mqtt->rbuf;
    size_t rcap = mqtt->rcap;
    UNLOCK(mqtt);

    if (rlen > 0) {
        size_t need = rlen + len;
        size_t new_cap;
        uint8_t *new_buf = alloc_rbuf(rbuf, rcap, need, &new_cap);
        if (!new_buf) return -1;

        memcpy(new_buf + rlen, data, len);
        working_buf = new_buf;
        process_data = new_buf;
        process_len = rlen + len;

        LOCK(mqtt);
        mqtt->rbuf = new_buf;
        mqtt->rcap = new_cap;
        mqtt->rlen = 0;
        UNLOCK(mqtt);
    }

    const uint8_t *cur = process_data;
    size_t rem = process_len;

    while (rem > 0) {
        size_t exp_len = get_packet_len(cur, rem);
        if (exp_len == 0 || rem < exp_len) break;

        if (exp_len <= MQTT_MAX_PACKET_SIZE) {
            process_packet(mqtt, cur, exp_len);
        }

        cur += exp_len;
        rem -= exp_len;
    }

    if (rem > 0) {
        size_t new_cap;
        LOCK(mqtt);
        rbuf = mqtt->rbuf;
        rcap = mqtt->rcap;
        UNLOCK(mqtt);

        uint8_t *new_buf = alloc_rbuf(rbuf, rcap, rem, &new_cap);
        if (!new_buf) return -1;

        if (new_buf != cur) {
            memmove(new_buf, cur, rem);
        }

        LOCK(mqtt);
        mqtt->rbuf = new_buf;
        mqtt->rcap = new_cap;
        mqtt->rlen = rem;
        mqtt->last_activity = 0;
        UNLOCK(mqtt);
    } else {
        LOCK(mqtt);
        mqtt->rlen = 0;
        mqtt->last_activity = 0;
        UNLOCK(mqtt);
    }

    return (int)consumed;
}

int mqtt_timer(mqtt_t *mqtt, uint32_t elapsed_ms)
{
    if (!mqtt) return -1;

    LOCK(mqtt);

    if (mqtt->state == MQTT_STATE_CONNECTED && mqtt->config.keep_alive > 0) {
        mqtt->keepalive_timer += elapsed_ms;

        uint32_t ka_ms = mqtt->config.keep_alive * MQTT_KEEPALIVE_MS_MULTIPLIER;
        if (mqtt->keepalive_timer >= ka_ms) {
            if (!mqtt->waiting_pingresp) {
                uint8_t ping[MQTT_PINGREQ_PACKET_SIZE];
                size_t plen = mqtt_create_pingreq_packet(ping, sizeof(ping));

                mqtt->waiting_pingresp = true;
                mqtt->keepalive_timer = 0;

                UNLOCK(mqtt);

                if (plen > 0 && mqtt->handler.send) {
                    mqtt->handler.send(ping, plen, mqtt->user_data);
                }

                return 0;
            } else {
                mqtt->missed_pings++;

                if (mqtt->missed_pings >= MQTT_PINGRESP_MAX_MISSED) {
                    mqtt->state = MQTT_STATE_DISCONNECTED;
                    UNLOCK(mqtt);

                    if (mqtt->handler.on_connection) {
                        mqtt->handler.on_connection(false, MQTT_CONN_REFUSED_SERVER, mqtt->user_data);
                    }
                    return 0;
                }
            }
        }
    }

    UNLOCK(mqtt);
    return 0;
}

mqtt_state_t mqtt_get_state(const mqtt_t *mqtt)
{
    if (!mqtt) return MQTT_STATE_DISCONNECTED;

    mqtt_t *m = (mqtt_t *)mqtt;
    LOCK(m);
    mqtt_state_t st = mqtt->state;
    UNLOCK(m);

    return st;
}

bool mqtt_is_connected(const mqtt_t *mqtt)
{
    if (!mqtt) return false;

    mqtt_t *m = (mqtt_t *)mqtt;
    LOCK(m);
    bool conn = (mqtt->state == MQTT_STATE_CONNECTED);
    UNLOCK(m);

    return conn;
}

uint16_t mqtt_get_packet_id(mqtt_t *mqtt)
{
    if (!mqtt) return 0;

    LOCK(mqtt);

    uint16_t id = mqtt->next_packet_id++;
    if (mqtt->next_packet_id == 0) mqtt->next_packet_id = MQTT_PACKET_ID_START;

    UNLOCK(mqtt);
    return id;
}