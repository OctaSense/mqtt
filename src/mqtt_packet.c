/**
 * @file mqtt_packet.c
 * @brief MQTT packet serialization/deserialization
 */

#include "mqtt.h"
#include <string.h>
#include <assert.h>

/**
 * @brief MQTT fixed header structure
 */
typedef struct {
    uint8_t type : 4;         /**< Packet type */
    uint8_t dup : 1;          /**< Duplicate flag */
    uint8_t qos : 2;          /**< QoS level */
    uint8_t retain : 1;       /**< Retain flag */
    uint32_t remaining_length; /**< Remaining length */
} mqtt_fixed_header_t;

/**
 * @brief Write MQTT variable length integer
 * @param buf Buffer to write to
 * @param value Value to write
 * @return Number of bytes written
 */
static size_t write_variable_length(uint8_t *buf, uint32_t value)
{
    size_t len = 0;
    
    do {
        uint8_t encoded_byte = value % 128;
        value /= 128;
        
        if (value > 0) {
            encoded_byte |= 0x80;
        }
        
        buf[len++] = encoded_byte;
    } while (value > 0);
    
    return len;
}

/**
 * @brief Read MQTT variable length integer
 * @param buf Buffer to read from
 * @param value Output value
 * @return Number of bytes read, or 0 on error
 */
size_t read_variable_length(const uint8_t *buf, uint32_t *value)
{
    uint32_t multiplier = 1;
    size_t len = 0;
    uint8_t encoded_byte;
    
    *value = 0;
    
    do {
        if (len >= 4) {
            return 0; // Maximum 4 bytes for variable length
        }
        
        encoded_byte = buf[len++];
        *value += (encoded_byte & 0x7F) * multiplier;
        multiplier *= 128;
        
    } while ((encoded_byte & 0x80) != 0);
    
    return len;
}

/**
 * @brief Write MQTT string
 * @param buf Buffer to write to
 * @param str String to write
 * @return Number of bytes written
 */
static size_t write_string(uint8_t *buf, const char *str)
{
    if (!str) {
        return 0;
    }
    
    size_t str_len = strlen(str);
    
    // Write length (2 bytes, big-endian)
    buf[0] = (str_len >> 8) & 0xFF;
    buf[1] = str_len & 0xFF;
    
    // Write string data
    memcpy(buf + 2, str, str_len);
    
    return str_len + 2;
}

/**
 * @brief Read MQTT string
 * @param buf Buffer to read from
 * @param str Output string buffer
 * @param max_len Maximum string length
 * @return Number of bytes read, or 0 on error
 */
size_t read_string(const uint8_t *buf, char *str, size_t max_len)
{
    if (!buf || !str || max_len < 3) {
        return 0;
    }
    
    // Read length (2 bytes, big-endian)
    uint16_t str_len = (buf[0] << 8) | buf[1];
    
    if (str_len + 1 > max_len) {
        return 0; // Buffer too small
    }
    
    // Copy string data
    memcpy(str, buf + 2, str_len);
    str[str_len] = '\0';
    
    return str_len + 2;
}

/**
 * @brief Create CONNECT packet
 * @param config MQTT configuration
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_connect_packet(const mqtt_config_t *config, uint8_t *buf, size_t buf_len)
{
    if (!config || !buf || buf_len < 10) {
        return 0;
    }
    
    // Validate required fields
    if (!config->client_id || strlen(config->client_id) == 0) {
        return 0;
    }
    
    size_t pos = 0;

    // Fixed header
    buf[pos++] = (MQTT_PKT_CONNECT << 4); // CONNECT packet type

    // Reserve maximum space for remaining length (4 bytes)
    size_t remaining_length_pos = pos;
    pos += 4;  // Reserve 4 bytes for remaining length

    // Write variable header and payload
    size_t var_header_start = pos;

    // Protocol name "MQTT"
    buf[pos++] = 0x00;
    buf[pos++] = 0x04;
    buf[pos++] = 'M';
    buf[pos++] = 'Q';
    buf[pos++] = 'T';
    buf[pos++] = 'T';

    // Protocol level (4 = MQTT 3.1.1)
    buf[pos++] = 0x04;

    // Connect flags
    uint8_t connect_flags = 0;
    if (config->clean_session) {
        connect_flags |= 0x02;
    }
    if (config->username) {
        connect_flags |= 0x80;
    }
    if (config->password) {
        connect_flags |= 0x40;
    }
    buf[pos++] = connect_flags;

    // Keep alive (2 bytes, big-endian)
    buf[pos++] = (config->keep_alive >> 8) & 0xFF;
    buf[pos++] = config->keep_alive & 0xFF;

    // Payload: Client ID
    pos += write_string(buf + pos, config->client_id);

    // Optional: Username
    if (config->username) {
        pos += write_string(buf + pos, config->username);
    }

    // Optional: Password
    if (config->password) {
        pos += write_string(buf + pos, config->password);
    }

    // Calculate actual remaining length
    uint32_t remaining_length = pos - var_header_start;

    // Write remaining length and get actual bytes used
    uint8_t temp_buf[4];
    size_t len_bytes = write_variable_length(temp_buf, remaining_length);

    // Move variable header to correct position (closing the gap)
    if (len_bytes < 4) {
        memmove(buf + remaining_length_pos + len_bytes, buf + var_header_start, remaining_length);
    }

    // Copy the remaining length bytes
    memcpy(buf + remaining_length_pos, temp_buf, len_bytes);

    return 1 + len_bytes + remaining_length;
}

/**
 * @brief Create PUBLISH packet
 * @param message Message to publish
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_publish_packet(const mqtt_message_t *message, uint8_t *buf, size_t buf_len)
{
    if (!message || !buf || buf_len < 10) {
        return 0;
    }
    
    // Validate required fields
    if (!message->topic || strlen(message->topic) == 0) {
        return 0;
    }
    
    size_t pos = 0;

    // Fixed header (QoS 0 only)
    uint8_t fixed_header = (MQTT_PKT_PUBLISH << 4);
    if (message->retain) {
        fixed_header |= 0x01;
    }
    // QoS is always 0, so no need to set QoS bits
    buf[pos++] = fixed_header;

    // Reserve maximum space for remaining length (4 bytes)
    size_t remaining_length_pos = pos;
    pos += 4;  // Reserve 4 bytes for remaining length

    // Write variable header and payload
    size_t var_header_start = pos;

    // Topic name
    pos += write_string(buf + pos, message->topic);

    // Payload
    if (message->payload && message->payload_len > 0) {
        memcpy(buf + pos, message->payload, message->payload_len);
        pos += message->payload_len;
    }

    // Calculate actual remaining length
    uint32_t remaining_length = pos - var_header_start;

    // Write remaining length and get actual bytes used
    uint8_t temp_buf[4];
    size_t len_bytes = write_variable_length(temp_buf, remaining_length);

    // Move variable header to correct position (closing the gap)
    if (len_bytes < 4) {
        memmove(buf + remaining_length_pos + len_bytes, buf + var_header_start, remaining_length);
    }

    // Copy the remaining length bytes
    memcpy(buf + remaining_length_pos, temp_buf, len_bytes);

    return 1 + len_bytes + remaining_length;
}

/**
 * @brief Create PINGREQ packet
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_pingreq_packet(uint8_t *buf, size_t buf_len)
{
    if (!buf || buf_len < 2) {
        return 0;
    }
    
    buf[0] = (MQTT_PKT_PINGREQ << 4);
    buf[1] = 0x00; // Remaining length = 0
    
    return 2;
}

/**
 * @brief Create DISCONNECT packet
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_disconnect_packet(uint8_t *buf, size_t buf_len)
{
    if (!buf || buf_len < 2) {
        return 0;
    }
    
    buf[0] = (MQTT_PKT_DISCONNECT << 4);
    buf[1] = 0x00; // Remaining length = 0
    
    return 2;
}

/**
 * @brief Create SUBSCRIBE packet
 * @param topics Array of topic names
 * @param qos Array of QoS levels
 * @param count Number of topics
 * @param packet_id Packet identifier
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_subscribe_packet(const char **topics, const mqtt_qos_t *qos, size_t count, 
                              uint16_t packet_id, uint8_t *buf, size_t buf_len)
{
    if (!topics || !qos || count == 0 || !buf || buf_len < 10) {
        return 0;
    }
    
    // For QoS 0 only, packet ID is not required
    // (packet_id parameter kept for API compatibility but ignored)
    
    size_t pos = 0;

    // Fixed header (SUBSCRIBE must use flags 0x02)
    buf[pos++] = (MQTT_PKT_SUBSCRIBE << 4) | 0x02; // SUBSCRIBE requires QoS 1 in fixed header

    // Variable header starts after remaining length
    size_t var_header_pos = pos + 1; // +1 for remaining length byte

    // Packet ID (SUBSCRIBE always requires packet ID, even for QoS 0 subscriptions)
    buf[var_header_pos++] = (packet_id >> 8) & 0xFF;
    buf[var_header_pos++] = packet_id & 0xFF;

    // Payload: topic-QoS pairs
    for (size_t i = 0; i < count; i++) {
        if (!topics[i]) {
            return 0;
        }
        
        // Write topic string
        size_t topic_bytes = write_string(buf + var_header_pos, topics[i]);
        if (topic_bytes == 0) {
            return 0;
        }
        var_header_pos += topic_bytes;
        
        // Write QoS
        if (var_header_pos >= buf_len) {
            return 0;
        }
        buf[var_header_pos++] = qos[i] & 0x03;
    }

    // Calculate remaining length
    uint32_t remaining_length = var_header_pos - (pos + 1);

    // Write remaining length
    size_t len_bytes = write_variable_length(buf + pos, remaining_length);

    // If remaining length takes more than 1 byte, need to move variable header
    if (len_bytes > 1) {
        memmove(buf + pos + len_bytes, buf + pos + 1, remaining_length);
    }

    return 1 + len_bytes + remaining_length;
}

/**
 * @brief Create UNSUBSCRIBE packet
 * @param topics Array of topic names
 * @param count Number of topics
 * @param packet_id Packet identifier
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_unsubscribe_packet(const char **topics, size_t count, uint16_t packet_id,
                                uint8_t *buf, size_t buf_len)
{
    if (!topics || count == 0 || !buf || buf_len < 10) {
        return 0;
    }
    
    // For QoS 0 only, packet ID is not required
    // (packet_id parameter kept for API compatibility but ignored)
    
    size_t pos = 0;
    
    // Fixed header (QoS 0 only)
    buf[pos++] = (MQTT_PKT_UNSUBSCRIBE << 4); // UNSUBSCRIBE with QoS 0
    
    // Variable header starts after remaining length
    size_t var_header_pos = pos + 1; // +1 for remaining length byte
    
    // No packet ID for QoS 0
    
    // Payload: topic strings
    for (size_t i = 0; i < count; i++) {
        if (!topics[i]) {
            return 0;
        }
        
        // Write topic string
        size_t topic_bytes = write_string(buf + var_header_pos, topics[i]);
        if (topic_bytes == 0) {
            return 0;
        }
        var_header_pos += topic_bytes;
    }

    // Calculate remaining length
    uint32_t remaining_length = var_header_pos - (pos + 1);

    // Write remaining length
    size_t len_bytes = write_variable_length(buf + pos, remaining_length);

    // If remaining length takes more than 1 byte, need to move variable header
    if (len_bytes > 1) {
        memmove(buf + pos + len_bytes, buf + pos + 1, remaining_length);
    }

    return 1 + len_bytes + remaining_length;
}