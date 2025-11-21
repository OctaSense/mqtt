/**
 * @file mqtt_intl.h
 * @brief MQTT internal interfaces and constants
 *
 * This header contains internal definitions used across MQTT implementation modules.
 * It should only be included by MQTT library source files, not by users of the library.
 */

#ifndef MQTT_INTL_H
#define MQTT_INTL_H

#include "mqtt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal constants
 */

/** @brief Maximum MQTT packet size (128KB as per MQTT spec) */
#define MQTT_MAX_PACKET_SIZE (128 * 1024)

/** @brief Default reassembly buffer size */
#define MQTT_REASSEMBLY_BUF_SIZE 1024

/** @brief Variable length encoding constants */
#define MQTT_VARLEN_MASK 0x7F
#define MQTT_VARLEN_CONTINUE 0x80
#define MQTT_VARLEN_MULTIPLIER_BASE 128
#define MQTT_VARLEN_MAX_BYTES 4

/** @brief MQTT protocol constants */
#define MQTT_PROTOCOL_LEVEL_3_1_1 0x04
#define MQTT_PROTOCOL_NAME_LEN 4

/** @brief CONNECT packet flags */
#define MQTT_CONNECT_FLAG_CLEAN_SESSION 0x02
#define MQTT_CONNECT_FLAG_USERNAME 0x80
#define MQTT_CONNECT_FLAG_PASSWORD 0x40

/** @brief PUBLISH packet flags */
#define MQTT_PUBLISH_FLAG_RETAIN 0x01
#define MQTT_PUBLISH_FLAG_QOS_MASK 0x06
#define MQTT_PUBLISH_FLAG_QOS_SHIFT 1

/** @brief Fixed header flags */
#define MQTT_FIXED_HEADER_TYPE_MASK 0xF0
#define MQTT_FIXED_HEADER_TYPE_SHIFT 4
#define MQTT_FIXED_HEADER_FLAGS_MASK 0x0F

/** @brief SUBSCRIBE packet flags */
#define MQTT_SUBSCRIBE_FIXED_FLAGS 0x02

/** @brief QoS bit positions in fixed header */
#define MQTT_QOS_BITS_SHIFT 1
#define MQTT_QOS_BITS_MASK 0x03

/** @brief Keep-alive timeout constants */
#define MQTT_PINGRESP_MAX_MISSED 3

/** @brief Minimum buffer sizes */
#define MQTT_MIN_PACKET_BUFFER_SIZE 1024

/** @brief Packet size constants */
#define MQTT_MIN_HEADER_SIZE 2
#define MQTT_DISCONNECT_PACKET_SIZE 2
#define MQTT_PINGREQ_PACKET_SIZE 2
#define MQTT_CONNACK_MIN_SIZE 4
#define MQTT_PUBLISH_MIN_SIZE 4
#define MQTT_PUBACK_MIN_SIZE 4
#define MQTT_PUBACK_PACKET_SIZE 4
#define MQTT_SUBACK_MIN_SIZE 5
#define MQTT_UNSUBACK_MIN_SIZE 4

/** @brief Packet structure offsets */
#define MQTT_CONNACK_RC_OFFSET 3
#define MQTT_PACKET_ID_OFFSET 2
#define MQTT_SUBACK_PAYLOAD_OFFSET 4
#define MQTT_PUBACK_REMAINING_LENGTH 0x02

/** @brief Variable length encoding initial values */
#define MQTT_VARLEN_INITIAL_POS 1
#define MQTT_VARLEN_INITIAL_MULT 1

/** @brief Buffer management constants */
#define MQTT_BUFFER_GROWTH_FACTOR 2
#define MQTT_TOPIC_BUFFER_SIZE 256
#define MQTT_MAX_SUBSCRIBE_TOPICS 16

/** @brief Packet ID constants */
#define MQTT_PACKET_ID_START 1
#define MQTT_PACKET_ID_SIZE 2

/** @brief Bit manipulation constants */
#define MQTT_BYTE_SHIFT 8
#define MQTT_BYTE_MASK 0xFF
#define MQTT_FIXED_HEADER_FLAGS_BITS 0x0F

/** @brief Keep-alive conversion */
#define MQTT_KEEPALIVE_MS_MULTIPLIER 1000

/** @brief PUBLISH packet structure sizes */
#define MQTT_PUBLISH_VARLEN_MAX 5
#define MQTT_PUBLISH_TOPIC_LEN_SIZE 2


/**
 * @brief Internal packet creation functions
 * These functions are used internally by the MQTT library to construct protocol packets
 */

/**
 * @brief Create CONNECT packet
 * @param config MQTT configuration
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t mqtt_create_connect_packet(const mqtt_config_t *config, uint8_t *buf, size_t buf_len);

/**
 * @brief Create PUBLISH packet
 * @param message Message to publish
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t mqtt_create_publish_packet(const mqtt_message_t *message, uint8_t *buf, size_t buf_len);

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
size_t mqtt_create_subscribe_packet(const char **topics, const mqtt_qos_t *qos, size_t count,
                                    uint16_t packet_id, uint8_t *buf, size_t buf_len);

/**
 * @brief Create UNSUBSCRIBE packet
 * @param topics Array of topic names
 * @param count Number of topics
 * @param packet_id Packet identifier
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t mqtt_create_unsubscribe_packet(const char **topics, size_t count, uint16_t packet_id,
                                      uint8_t *buf, size_t buf_len);

/**
 * @brief Create PINGREQ packet
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t mqtt_create_pingreq_packet(uint8_t *buf, size_t buf_len);

/**
 * @brief Create DISCONNECT packet
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t mqtt_create_disconnect_packet(uint8_t *buf, size_t buf_len);

/**
 * @brief Read MQTT variable length integer
 * @param buf Buffer to read from
 * @param value Output value
 * @return Number of bytes read, or 0 on error
 */
size_t mqtt_read_variable_length(const uint8_t *buf, uint32_t *value);

/**
 * @brief Read MQTT string
 * @param buf Buffer to read from
 * @param str Output string buffer
 * @param max_len Maximum string length
 * @return Number of bytes read, or 0 on error
 */
size_t mqtt_read_string(const uint8_t *buf, char *str, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_INTL_H */
