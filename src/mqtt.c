/**
 * @file mqtt.c
 * @brief MQTT client implementation
 */

#include "mqtt.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

// Spinlock macros for thread-safe operations
#define LOCK(mqtt) do { \
    while (__sync_lock_test_and_set(&(mqtt)->lock, 1)) { \
        /* Spin until lock acquired */ \
    } \
} while(0)

#define UNLOCK(mqtt) do { \
    __sync_lock_release(&(mqtt)->lock); \
} while(0)



/**
 * @brief Default reassembly buffer size
 */
#define MQTT_REASSEMBLY_BUF_SIZE 4096

/**
 * @brief Maximum MQTT packet size (128KB as per MQTT spec)
 */
#define MQTT_MAX_PACKET_SIZE (128 * 1024)

/**
 * @brief MQTT instance internal structure
 */
struct mqtt_s {
    mqtt_config_t config;         /**< Client configuration */
    mqtt_handler_t handler;       /**< Callback handlers */
    void *user_data;              /**< User context */
    volatile mqtt_state_t state;           /**< Connection state */
    uint16_t next_packet_id;      /**< Next packet identifier */
    uint32_t keep_alive_timer;    /**< Keep-alive timer */
    uint32_t last_activity;       /**< Last activity timestamp */
    bool waiting_pingresp;        /**< Waiting for PINGRESP */
    uint8_t missed_pingresp_count; /**< Count of consecutive missed PINGRESP responses */
    
    // Packet reassembly buffers for handling TCP stream fragmentation
    uint8_t *reassembly_buf;      /**< Buffer for reassembling incomplete packets */
    size_t reassembly_len;        /**< Current length of data in reassembly buffer */
    size_t reassembly_capacity;   /**< Capacity of reassembly buffer */
    
    // Thread safety: spinlock for non-blocking synchronization
    volatile int lock;            /**< Spinlock for thread-safe operations */
};


/**
 * @brief Get the expected length of an MQTT packet
 * @param data Packet data
 * @param len Current data length
 * @return Expected total packet length, or 0 if not enough data to determine
 */
static size_t get_expected_packet_length(const uint8_t *data, size_t len)
{
    if (len < 2) {
        return 0; // Not enough data for fixed header
    }
    
    // Read variable length field
    uint32_t remaining_length = 0;
    size_t multiplier = 1;
    size_t pos = 1; // Start after packet type byte
    
    do {
        if (pos >= len) {
            return 0; // Not enough data for variable length field
        }
        
        uint8_t encoded_byte = data[pos];
        remaining_length += (encoded_byte & 0x7F) * multiplier;
        multiplier *= 128;
        pos++;
        
        if (multiplier > 128 * 128 * 128) {
            return 0; // Invalid packet - variable length too large
        }
    } while ((data[pos - 1] & 0x80) != 0);
    
    // Total packet length = fixed header (1 byte) + variable length bytes + remaining length
    size_t total_length = pos + remaining_length;
    return total_length;
}

/**
 * @brief Ensure reassembly buffer has enough capacity
 * @param mqtt MQTT instance
 * @param needed_capacity Required capacity
 * @return 0 on success, -1 on failure
 */
static int ensure_reassembly_capacity(mqtt_t *mqtt, size_t needed_capacity)
{
    if (mqtt->reassembly_capacity >= needed_capacity) {
        return 0;
    }
    
    // Calculate new capacity (at least double current, but at least needed)
    size_t new_capacity = mqtt->reassembly_capacity * 2;
    if (new_capacity < needed_capacity) {
        new_capacity = needed_capacity;
    }
    
    // Minimum initial capacity
    if (new_capacity < 1024) {
        new_capacity = 1024;
    }
    
    uint8_t *new_buf = mqtt_realloc(mqtt->reassembly_buf, new_capacity);
    if (!new_buf) {
        return -1;
    }
    
    mqtt->reassembly_buf = new_buf;
    mqtt->reassembly_capacity = new_capacity;
    return 0;
}

/**
 * @brief Clear reassembly buffer
 * @param mqtt MQTT instance
 */
static void clear_reassembly_buffer(mqtt_t *mqtt)
{
    if (mqtt && mqtt->reassembly_buf) {
        mqtt_free(mqtt->reassembly_buf);
        mqtt->reassembly_buf = NULL;
        mqtt->reassembly_len = 0;
        mqtt->reassembly_capacity = 0;
    }
}

mqtt_t *mqtt_create(const mqtt_config_t *config, const mqtt_handler_t *handler, void *user_data)
{
    if (!config || !handler || !handler->send) {
        return NULL;
    }

    // Validate required configuration
    if (!config->client_id || strlen(config->client_id) == 0) {
        return NULL;
    }

    mqtt_t *mqtt = mqtt_calloc(1, sizeof(mqtt_t));
    if (!mqtt) {
        return NULL;
    }

    // Copy configuration
    memcpy(&mqtt->config, config, sizeof(mqtt_config_t));
    
    // Copy handlers
    memcpy(&mqtt->handler, handler, sizeof(mqtt_handler_t));
    
    mqtt->user_data = user_data;
    mqtt->state = MQTT_STATE_DISCONNECTED;
    mqtt->next_packet_id = 1;
    mqtt->missed_pingresp_count = 0;
    
    // Initialize packet reassembly buffers
    mqtt->reassembly_buf = NULL;
    mqtt->reassembly_len = 0;
    mqtt->reassembly_capacity = 0;
    
    // Initialize spinlock
    mqtt->lock = 0;
    
    return mqtt;
}

void mqtt_destroy(mqtt_t *mqtt)
{
    if (!mqtt) {
        return;
    }
    
    // Disconnect if still connected
    if (mqtt->state == MQTT_STATE_CONNECTED || mqtt->state == MQTT_STATE_CONNECTING) {
        mqtt_disconnect(mqtt);
    }
    
    // Free reassembly buffer
    if (mqtt->reassembly_buf) {
        mqtt_free(mqtt->reassembly_buf);
    }
    
    mqtt_free(mqtt);
}

int mqtt_connect(mqtt_t *mqtt)
{
    if (!mqtt) {
        return -1;
    }

    if (mqtt->state != MQTT_STATE_DISCONNECTED) {
        return -1;
    }

    // Create and send CONNECT packet
    uint8_t packet_buf[1024];
    size_t packet_len = create_connect_packet(&mqtt->config, packet_buf, sizeof(packet_buf));
    
    if (packet_len == 0) {
        return -1;
    }
    
    if (mqtt->handler.send(packet_buf, packet_len, mqtt->user_data) != (int)packet_len) {
        return -1;
    }
    
    LOCK(mqtt);
    mqtt->state = MQTT_STATE_CONNECTING;
    mqtt->keep_alive_timer = 0;
    mqtt->last_activity = 0; // TODO: Use actual timestamp
    UNLOCK(mqtt);

    return 0;
}

int mqtt_disconnect(mqtt_t *mqtt)
{
    if (!mqtt) {
        return -1;
    }

    if (mqtt->state == MQTT_STATE_DISCONNECTED) {
        return -1;
    }

    // Send DISCONNECT packet
    uint8_t packet_buf[2];
    size_t packet_len = create_disconnect_packet(packet_buf, sizeof(packet_buf));
    
    if (packet_len > 0 && mqtt->handler.send) {
        mqtt->handler.send(packet_buf, packet_len, mqtt->user_data);
    }
    
    LOCK(mqtt);
    mqtt->state = MQTT_STATE_DISCONNECTED;
    
    // Clear reassembly buffer on disconnect
    clear_reassembly_buffer(mqtt);
    
    UNLOCK(mqtt);
    
    // Call connection callback outside of lock
    if (mqtt->handler.on_connection) {
        mqtt->handler.on_connection(false, MQTT_CONN_ACCEPTED, mqtt->user_data);
    }
    
    return 0;
}

int mqtt_publish(mqtt_t *mqtt, const mqtt_message_t *message)
{
    if (!mqtt || !message || !message->topic) {
        return -1;
    }
    
    if (mqtt->state != MQTT_STATE_CONNECTED) {
        return -1;
    }

    // Only QoS 0 is supported
    if (message->qos != MQTT_QOS_0) {
        return -1;
    }

    // Calculate required buffer size for PUBLISH packet (QoS 0 only)
    // Fixed header: 1 byte + variable length bytes (max 4) + topic + payload
    size_t topic_len = strlen(message->topic);
    size_t fixed_header_size = 1 + 4; // packet type + max variable length bytes
    size_t topic_field_size = 2 + topic_len; // length + topic
    size_t payload_size = message->payload_len;
    
    size_t required_size = fixed_header_size + topic_field_size + payload_size;
    
    // Allocate buffer dynamically based on actual message size
    uint8_t *packet_buf = mqtt_malloc(required_size);
    if (!packet_buf) {
        return -1;
    }
    
    size_t packet_len = create_publish_packet(message, packet_buf, required_size);
    
    if (packet_len == 0) {
        mqtt_free(packet_buf);
        return -1;
    }
    
    if (mqtt->handler.send(packet_buf, packet_len, mqtt->user_data) != (int)packet_len) {
        mqtt_free(packet_buf);
        return -1;
    }

    // Free the buffer (QoS 0 only - no retransmission needed)
    mqtt_free(packet_buf);

    return 0;
}

int mqtt_subscribe(mqtt_t *mqtt, const char **topics, const mqtt_qos_t *qos, size_t count)
{
    if (!mqtt || !topics || !qos || count == 0) {
        return -1;
    }
    
    if (mqtt->state != MQTT_STATE_CONNECTED) {
        return -1;
    }

    // Only QoS 0 is supported - validate all requested QoS levels
    for (size_t i = 0; i < count; i++) {
        if (qos[i] != MQTT_QOS_0) {
            return -1;
        }
    }

    // Create and send SUBSCRIBE packet (packet ID required even for QoS 0 subscriptions)
    uint8_t packet_buf[1024];
    uint16_t packet_id = mqtt_get_packet_id(mqtt);
    size_t packet_len = create_subscribe_packet(topics, qos, count, packet_id, packet_buf, sizeof(packet_buf));

    if (packet_len == 0) {
        return -1;
    }

    if (mqtt->handler.send(packet_buf, packet_len, mqtt->user_data) != (int)packet_len) {
        return -1;
    }

    return 0;
}

int mqtt_unsubscribe(mqtt_t *mqtt, const char **topics, size_t count)
{
    if (!mqtt || !topics || count == 0) {
        return -1;
    }

    if (mqtt->state != MQTT_STATE_CONNECTED) {
        return -1;
    }

    // Create and send UNSUBSCRIBE packet (QoS 0 only)
    uint8_t packet_buf[1024];
    size_t packet_len = create_unsubscribe_packet(topics, count, 0, packet_buf, sizeof(packet_buf));
    
    if (packet_len == 0) {
        return -1;
    }
    
    if (mqtt->handler.send(packet_buf, packet_len, mqtt->user_data) != (int)packet_len) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Parse and process incoming MQTT packet
 * @param mqtt MQTT instance
 * @param data Packet data
 * @param len Packet length
 * @return 0 on success, -1 on error
 */
static int process_packet(mqtt_t *mqtt, const uint8_t *data, size_t len)
{
    if (len < 2) {
        return -1; // Minimum packet length is 2 bytes
    }
    
    uint8_t packet_type = (data[0] >> 4) & 0x0F;
    
    switch (packet_type) {
        case MQTT_PKT_CONNACK:
            if (len >= 4) {
                uint8_t return_code = data[3];
                
                if (return_code == MQTT_CONN_ACCEPTED) {
                    // Update state under lock
                    LOCK(mqtt);
                    mqtt->state = MQTT_STATE_CONNECTED;
                    mqtt->missed_pingresp_count = 0; // Reset missed counter on successful connection
                    UNLOCK(mqtt);
                    
                    // Call callback without lock
                    if (mqtt->handler.on_connection) {
                        mqtt->handler.on_connection(true, MQTT_CONN_ACCEPTED, mqtt->user_data);
                    }
                } else {
                    // Update state under lock
                    LOCK(mqtt);
                    mqtt->state = MQTT_STATE_DISCONNECTED;
                    UNLOCK(mqtt);
                    
                    // Call callback without lock
                    if (mqtt->handler.on_connection) {
                        mqtt->handler.on_connection(false, (mqtt_conn_return_t)return_code, mqtt->user_data);
                    }
                }
            }
            break;
            
        case MQTT_PKT_PUBLISH:
            // Parse PUBLISH packet
            if (len >= 4) {
                size_t pos = 1; // Skip fixed header
                
                // Read variable length
                uint32_t remaining_length;
                size_t var_len_bytes = read_variable_length(data + pos, &remaining_length);
                if (var_len_bytes == 0) {
                    return -1;
                }
                pos += var_len_bytes;
                
                // Read topic name
                char topic[256];
                size_t topic_bytes = read_string(data + pos, topic, sizeof(topic));
                if (topic_bytes == 0) {
                    return -1;
                }
                pos += topic_bytes;
                
                // Read packet ID for QoS > 0
                uint8_t qos = (data[0] >> 1) & 0x03;
                uint16_t packet_id = 0;
                if (qos > MQTT_QOS_0) {
                    if (pos + 2 > len) {
                        return -1;
                    }
                    packet_id = (data[pos] << 8) | data[pos + 1];
                    pos += 2;
                }
                
                // Read payload
                size_t payload_len = 0;
                if (pos < len) {
                    payload_len = len - pos;
                }
                
                bool retain = (data[0] & 0x01) != 0;
                
                // Call message callback without lock
                if (mqtt->handler.on_message) {
                    mqtt_message_t message = {
                        .topic = topic,
                        .payload = (uint8_t*)(data + pos),
                        .payload_len = payload_len,
                        .qos = (mqtt_qos_t)qos,
                        .retain = retain
                    };
                    mqtt->handler.on_message(&message, mqtt->user_data);
                }
                
                // Send PUBACK for QoS 1
                if (qos == MQTT_QOS_1 && mqtt->handler.send) {
                    uint8_t puback_buf[4] = {
                        (MQTT_PKT_PUBACK << 4),
                        0x02, // Remaining length
                        (packet_id >> 8) & 0xFF,
                        packet_id & 0xFF
                    };
                    mqtt->handler.send(puback_buf, sizeof(puback_buf), mqtt->user_data);
                }
            }
            break;
            
        case MQTT_PKT_PUBACK:
            if (len >= 4) {
                uint16_t packet_id = (data[2] << 8) | data[3];
                if (mqtt->handler.publish_ack) {
                    mqtt->handler.publish_ack(packet_id, mqtt->user_data);
                }
            }
            break;
            
        case MQTT_PKT_SUBACK:
            if (len >= 5) {
                uint16_t packet_id = (data[2] << 8) | data[3];
                size_t return_code_count = len - 4;
                
                if (mqtt->handler.subscribe_ack && return_code_count <= 16) {
                    mqtt_qos_t return_codes[16];
                    for (size_t i = 0; i < return_code_count; i++) {
                        return_codes[i] = (mqtt_qos_t)data[4 + i];
                    }
                    mqtt->handler.subscribe_ack(packet_id, return_codes, return_code_count, mqtt->user_data);
                }
            }
            break;
            
        case MQTT_PKT_UNSUBACK:
            if (len >= 4) {
                uint16_t packet_id = (data[2] << 8) | data[3];
                if (mqtt->handler.unsubscribe_ack) {
                    mqtt->handler.unsubscribe_ack(packet_id, mqtt->user_data);
                }
            }
            break;
            
        case MQTT_PKT_PINGRESP:
            LOCK(mqtt);
            mqtt->waiting_pingresp = false;
            mqtt->missed_pingresp_count = 0; // Reset missed counter on successful PINGRESP
            UNLOCK(mqtt);
            break;
            
        case MQTT_PKT_DISCONNECT:
            // Update state under lock
            LOCK(mqtt);
            mqtt->state = MQTT_STATE_DISCONNECTED;
            UNLOCK(mqtt);
            
            // Call callback without lock
            if (mqtt->handler.on_connection) {
                mqtt->handler.on_connection(false, MQTT_CONN_ACCEPTED, mqtt->user_data);
            }
            break;
            
        default:
            // Unknown packet type - ignore
            break;
    }
    
    return 0;
}

int mqtt_input(mqtt_t *mqtt, const uint8_t *data, size_t len)
{
    if (!mqtt || !data || len == 0) {
        return -1;
    }

    // Buffer management under lock
    LOCK(mqtt);
    
    // Track how much new data we're processing
    size_t new_data_len = len;
    
    // Combine with existing reassembly data if any
    if (mqtt->reassembly_len > 0) {
        // Combine with new data
        size_t needed_capacity = mqtt->reassembly_len + len;
        if (ensure_reassembly_capacity(mqtt, needed_capacity) != 0) {
            UNLOCK(mqtt);
            return -1;
        }
        
        // Append new data to reassembly buffer
        memcpy(mqtt->reassembly_buf + mqtt->reassembly_len, data, len);
        
        // Reset tracking variables to process reassembly buffer
        data = mqtt->reassembly_buf;
        len = mqtt->reassembly_len + len;
        mqtt->reassembly_len = 0; // Will be reset if we have incomplete packets
    }
    
    // Process complete packets from the data
    const uint8_t *current_data = data;
    size_t remaining_len = len;
    
    while (remaining_len > 0) {
        // Check if we have enough data to determine packet length
        size_t expected_len = get_expected_packet_length(current_data, remaining_len);
        
        if (expected_len == 0) {
            // Not enough data to determine packet length - save for reassembly
            break;
        }
        
        if (remaining_len < expected_len) {
            // Incomplete packet - save for reassembly
            break;
        }
        
        // We have a complete packet - process it immediately
        if (expected_len <= MQTT_MAX_PACKET_SIZE) {
            // Copy packet data to process outside of lock
            uint8_t packet_buffer[MQTT_MAX_PACKET_SIZE];
            memcpy(packet_buffer, current_data, expected_len);
            
            UNLOCK(mqtt);
            
            // Process packet and call callbacks without lock
            process_packet(mqtt, packet_buffer, expected_len);
            
            LOCK(mqtt);
        }
        
        current_data += expected_len;
        remaining_len -= expected_len;
    }
    
    // Handle incomplete data that needs reassembly
    if (remaining_len > 0) {
        // Save incomplete data to reassembly buffer
        if (ensure_reassembly_capacity(mqtt, remaining_len) != 0) {
            UNLOCK(mqtt);
            return -1;
        }
        memcpy(mqtt->reassembly_buf, current_data, remaining_len);
        mqtt->reassembly_len = remaining_len;
    } else {
        // Clear reassembly buffer if we processed all data
        mqtt->reassembly_len = 0;
    }
    
    // Update last activity timestamp
    mqtt->last_activity = 0; // TODO: Use actual timestamp
    
    UNLOCK(mqtt);
    
    // Always return that we consumed all the new data we were given
    return (int)new_data_len;
}

int mqtt_timer(mqtt_t *mqtt, uint32_t elapsed_ms)
{
    if (!mqtt) {
        return -1;
    }

    LOCK(mqtt);
    
    // Update keep-alive timer
    if (mqtt->state == MQTT_STATE_CONNECTED && mqtt->config.keep_alive > 0) {
        mqtt->keep_alive_timer += elapsed_ms;
        
        // Check if we need to send PINGREQ
        uint32_t keep_alive_ms = mqtt->config.keep_alive * 1000;
        if (mqtt->keep_alive_timer >= keep_alive_ms) {
            if (!mqtt->waiting_pingresp) {
                // Send PINGREQ - prepare data outside of lock
                uint8_t pingreq_buf[2];
                size_t pingreq_len = create_pingreq_packet(pingreq_buf, sizeof(pingreq_buf));
                
                // Update state under lock
                mqtt->waiting_pingresp = true;
                mqtt->keep_alive_timer = 0;
                
                UNLOCK(mqtt);
                
                // Send PINGREQ outside of lock
                if (pingreq_len > 0 && mqtt->handler.send) {
                    mqtt->handler.send(pingreq_buf, pingreq_len, mqtt->user_data);
                }
                
                return 0;
            } else {
                // PINGREQ timeout - increment missed counter
                mqtt->missed_pingresp_count++;
                
                // Disconnect after 3 consecutive missed PINGRESP responses
                if (mqtt->missed_pingresp_count >= 3) {
                    mqtt->state = MQTT_STATE_DISCONNECTED;
                    UNLOCK(mqtt);
                    
                    // Call connection callback outside of lock
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
    if (!mqtt) {
        return MQTT_STATE_DISCONNECTED;
    }
    
    // Cast away const to access lock for thread safety
    mqtt_t *non_const_mqtt = (mqtt_t *)mqtt;
    LOCK(non_const_mqtt);
    mqtt_state_t state = mqtt->state;
    UNLOCK(non_const_mqtt);
    
    return state;
}

bool mqtt_is_connected(const mqtt_t *mqtt)
{
    if (!mqtt) {
        return false;
    }
    
    // Cast away const to access lock for thread safety
    mqtt_t *non_const_mqtt = (mqtt_t *)mqtt;
    LOCK(non_const_mqtt);
    bool connected = (mqtt->state == MQTT_STATE_CONNECTED);
    UNLOCK(non_const_mqtt);
    
    return connected;
}

uint16_t mqtt_get_packet_id(mqtt_t *mqtt)
{
    if (!mqtt) {
        return 0;
    }

    LOCK(mqtt);
    
    uint16_t packet_id = mqtt->next_packet_id;
    mqtt->next_packet_id++;
    
    // Packet ID 0 is reserved, wrap around
    if (mqtt->next_packet_id == 0) {
        mqtt->next_packet_id = 1;
    }
    
    UNLOCK(mqtt);
    return packet_id;
}