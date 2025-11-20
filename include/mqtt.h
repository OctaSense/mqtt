/**
 * @file mqtt.h
 * @brief MQTT client library header
 * 
 * A lightweight MQTT client implementation focusing on protocol layer only.
 * Transport layer integration is handled through callback interfaces.
 */

#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT quality of service levels
 */
typedef enum {
    MQTT_QOS_0 = 0,  /**< At most once delivery */
    MQTT_QOS_1 = 1,  /**< At least once delivery */
    MQTT_QOS_2 = 2   /**< Exactly once delivery */
} mqtt_qos_t;

/**
 * @brief MQTT connection return codes
 */
typedef enum {
    MQTT_CONN_ACCEPTED = 0,           /**< Connection accepted */
    MQTT_CONN_REFUSED_PROTOCOL = 1,   /**< Unacceptable protocol version */
    MQTT_CONN_REFUSED_ID = 2,         /**< Identifier rejected */
    MQTT_CONN_REFUSED_SERVER = 3,     /**< Server unavailable */
    MQTT_CONN_REFUSED_CREDENTIALS = 4,/**< Bad username or password */
    MQTT_CONN_REFUSED_AUTH = 5        /**< Not authorized */
} mqtt_conn_return_t;

/**
 * @brief MQTT connection state
 */
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,  /**< Not connected */
    MQTT_STATE_CONNECTING,        /**< Connection in progress */
    MQTT_STATE_CONNECTED,         /**< Connected and active */
    MQTT_STATE_DISCONNECTING      /**< Disconnection in progress */
} mqtt_state_t;

/**
 * @brief MQTT packet types
 */
typedef enum {
    MQTT_PKT_CONNECT = 1,
    MQTT_PKT_CONNACK,
    MQTT_PKT_PUBLISH,
    MQTT_PKT_PUBACK,
    MQTT_PKT_PUBREC,
    MQTT_PKT_PUBREL,
    MQTT_PKT_PUBCOMP,
    MQTT_PKT_SUBSCRIBE,
    MQTT_PKT_SUBACK,
    MQTT_PKT_UNSUBSCRIBE,
    MQTT_PKT_UNSUBACK,
    MQTT_PKT_PINGREQ,
    MQTT_PKT_PINGRESP,
    MQTT_PKT_DISCONNECT
} mqtt_packet_type_t;

/**
 * @brief MQTT configuration structure
 */
typedef struct {
    const char *client_id;        /**< Client identifier */
    const char *username;         /**< Optional username */
    const char *password;         /**< Optional password */
    uint16_t keep_alive;          /**< Keep-alive interval in seconds */
    bool clean_session;           /**< Clean session flag */
    uint16_t packet_timeout;      /**< Packet timeout in milliseconds */
    uint16_t max_retry_count;     /**< Maximum retry attempts */
} mqtt_config_t;

/**
 * @brief MQTT message structure
 */
typedef struct {
    const char *topic;            /**< Topic name */
    const uint8_t *payload;       /**< Message payload */
    size_t payload_len;           /**< Payload length */
    mqtt_qos_t qos;               /**< Quality of service */
    bool retain;                  /**< Retain flag */
    uint16_t packet_id;           /**< Packet identifier (for QoS > 0) */
} mqtt_message_t;

/**
 * @brief MQTT handler callbacks
 */
typedef struct {
    /**
     * @brief Send data callback
     * @param data Data to send
     * @param len Data length
     * @param user_data User context
     * @return Number of bytes sent, or -1 on error
     */
    int (*send)(const uint8_t *data, size_t len, void *user_data);
    
    /**
     * @brief Connection status callback
     * @param connected True if connected, false if disconnected
     * @param return_code Connection return code (if applicable)
     * @param user_data User context
     */
    void (*on_connection)(bool connected, mqtt_conn_return_t return_code, void *user_data);
    
    /**
     * @brief Message received callback
     * @param message Received message
     * @param user_data User context
     */
    void (*on_message)(const mqtt_message_t *message, void *user_data);
    
    /**
     * @brief Publish acknowledgment callback
     * @param packet_id Packet identifier
     * @param user_data User context
     */
    void (*publish_ack)(uint16_t packet_id, void *user_data);
    
    /**
     * @brief Subscribe acknowledgment callback
     * @param packet_id Packet identifier
     * @param return_codes QoS levels granted for subscriptions
     * @param count Number of return codes
     * @param user_data User context
     */
    void (*subscribe_ack)(uint16_t packet_id, const mqtt_qos_t *return_codes, size_t count, void *user_data);
    
    /**
     * @brief Unsubscribe acknowledgment callback
     * @param packet_id Packet identifier
     * @param user_data User context
     */
    void (*unsubscribe_ack)(uint16_t packet_id, void *user_data);
} mqtt_handler_t;

/**
 * @brief MQTT instance structure (opaque)
 */
typedef struct mqtt_s mqtt_t;

/**
 * @brief Create a new MQTT instance
 * @param config MQTT configuration
 * @param handler Callback handler functions
 * @param user_data User context passed to callbacks
 * @return New MQTT instance, or NULL on failure
 */
mqtt_t *mqtt_create(const mqtt_config_t *config, const mqtt_handler_t *handler, void *user_data);

/**
 * @brief Destroy an MQTT instance
 * @param mqtt MQTT instance to destroy
 */
void mqtt_destroy(mqtt_t *mqtt);

/**
 * @brief Connect to MQTT broker
 * @param mqtt MQTT instance
 * @return 0 on success, -1 on failure
 */
int mqtt_connect(mqtt_t *mqtt);

/**
 * @brief Disconnect from MQTT broker
 * @param mqtt MQTT instance
 * @return 0 on success, -1 on failure
 */
int mqtt_disconnect(mqtt_t *mqtt);

/**
 * @brief Publish a message
 * @param mqtt MQTT instance
 * @param message Message to publish
 * @return 0 on success, -1 on failure
 */
int mqtt_publish(mqtt_t *mqtt, const mqtt_message_t *message);

/**
 * @brief Subscribe to topics
 * @param mqtt MQTT instance
 * @param topics Array of topic names
 * @param qos Array of QoS levels
 * @param count Number of topics
 * @return 0 on success, -1 on failure
 */
int mqtt_subscribe(mqtt_t *mqtt, const char **topics, const mqtt_qos_t *qos, size_t count);

/**
 * @brief Unsubscribe from topics
 * @param mqtt MQTT instance
 * @param topics Array of topic names
 * @param count Number of topics
 * @return 0 on success, -1 on failure
 */
int mqtt_unsubscribe(mqtt_t *mqtt, const char **topics, size_t count);

/**
 * @brief Process incoming data from transport layer
 * @param mqtt MQTT instance
 * @param data Incoming data
 * @param len Data length
 * @return Number of bytes processed, or -1 on error
 */
int mqtt_input(mqtt_t *mqtt, const uint8_t *data, size_t len);

/**
 * @brief Drive MQTT timer operations
 * @param mqtt MQTT instance
 * @param elapsed_ms Elapsed time in milliseconds
 * @return 0 on success, -1 on failure
 */
int mqtt_timer(mqtt_t *mqtt, uint32_t elapsed_ms);

/**
 * @brief Get current connection state
 * @param mqtt MQTT instance
 * @return Current connection state
 */
mqtt_state_t mqtt_get_state(const mqtt_t *mqtt);

/**
 * @brief Check if MQTT instance is connected
 * @param mqtt MQTT instance
 * @return True if connected, false otherwise
 */
bool mqtt_is_connected(const mqtt_t *mqtt);

/**
 * @brief Get next available packet identifier
 * @param mqtt MQTT instance
 * @return Packet identifier (0 if none available)
 */
uint16_t mqtt_get_packet_id(mqtt_t *mqtt);

/**
 * @brief Memory management functions
 */

/**
 * @brief Allocate memory
 * @param size Size to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *mqtt_malloc(size_t size);

/**
 * @brief Free memory
 * @param ptr Pointer to free
 */
void mqtt_free(void *ptr);

/**
 * @brief Reallocate memory
 * @param ptr Original pointer
 * @param size New size
 * @return Pointer to reallocated memory, or NULL on failure
 */
void *mqtt_realloc(void *ptr, size_t size);

/**
 * @brief Allocate zero-initialized memory
 * @param nmemb Number of elements
 * @param size Size of each element
 * @return Pointer to allocated memory, or NULL on failure
 */
void *mqtt_calloc(size_t nmemb, size_t size);

/**
 * @brief Duplicate a string
 * @param str String to duplicate
 * @return Duplicated string, or NULL on failure
 */
char *mqtt_strdup(const char *str);

#ifdef MQTT_MEM_DEBUG

/**
 * @brief Print memory statistics
 */
void mqtt_mem_stats(void);

/**
 * @brief Check for memory leaks
 * @return Number of memory leaks detected
 */
size_t mqtt_mem_leaks(void);

/**
 * @brief Reset memory statistics
 */
void mqtt_mem_reset_stats(void);

#endif


/**
 * @brief Packet creation functions (internal use)
 */

/**
 * @brief Create CONNECT packet
 * @param config MQTT configuration
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_connect_packet(const mqtt_config_t *config, uint8_t *buf, size_t buf_len);

/**
 * @brief Create PUBLISH packet
 * @param message Message to publish
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_publish_packet(const mqtt_message_t *message, uint8_t *buf, size_t buf_len);

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
size_t create_unsubscribe_packet(const char **topics, size_t count, uint16_t packet_id,
                                uint8_t *buf, size_t buf_len);

/**
 * @brief Create PINGREQ packet
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_pingreq_packet(uint8_t *buf, size_t buf_len);

/**
 * @brief Create DISCONNECT packet
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Packet length, or 0 on error
 */
size_t create_disconnect_packet(uint8_t *buf, size_t buf_len);

/**
 * @brief Read MQTT variable length integer
 * @param buf Buffer to read from
 * @param value Output value
 * @return Number of bytes read, or 0 on error
 */
size_t read_variable_length(const uint8_t *buf, uint32_t *value);

/**
 * @brief Read MQTT string
 * @param buf Buffer to read from
 * @param str Output string buffer
 * @param max_len Maximum string length
 * @return Number of bytes read, or 0 on error
 */
size_t read_string(const uint8_t *buf, char *str, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_H */