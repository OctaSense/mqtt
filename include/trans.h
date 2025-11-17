/**
 * @file trans.h
 * @brief Transport layer interface for MQTT client
 */

#ifndef MQTT_TRANS_H
#define MQTT_TRANS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Transport layer context
 */
typedef struct trans_context_s trans_context_t;

/**
 * @brief Transport layer configuration
 */
typedef struct {
    const char *host;           /**< Server hostname or IP address */
    uint16_t port;              /**< Server port */
    int socket_timeout_ms;      /**< Socket timeout in milliseconds */
} trans_config_t;

/**
 * @brief Transport layer callbacks
 */
typedef struct {
    /**
     * @brief Data received callback
     * @param data Received data
     * @param len Data length
     * @param user_data User context
     */
    void (*on_data)(const uint8_t *data, size_t len, void *user_data);
    
    /**
     * @brief Connection status callback
     * @param connected Connection status
     * @param user_data User context
     */
    void (*on_connection)(bool connected, void *user_data);
} trans_handler_t;

/**
 * @brief Create transport layer context
 * @param config Transport configuration
 * @param handler Transport callbacks
 * @param user_data User context
 * @return Transport context, or NULL on failure
 */
trans_context_t *trans_create(const trans_config_t *config, const trans_handler_t *handler, void *user_data);

/**
 * @brief Destroy transport layer context
 * @param ctx Transport context
 */
void trans_destroy(trans_context_t *ctx);

/**
 * @brief Connect to server
 * @param ctx Transport context
 * @return 0 on success, -1 on failure
 */
int trans_connect(trans_context_t *ctx);

/**
 * @brief Disconnect from server
 * @param ctx Transport context
 * @return 0 on success, -1 on failure
 */
int trans_disconnect(trans_context_t *ctx);

/**
 * @brief Send data through transport layer
 * @param ctx Transport context
 * @param data Data to send
 * @param len Data length
 * @return Number of bytes sent, or -1 on error
 */
int trans_send(trans_context_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief Process incoming data
 * @param ctx Transport context
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on error
 */
int trans_process(trans_context_t *ctx, int timeout_ms);

/**
 * @brief Get socket file descriptor for select/poll
 * @param ctx Transport context
 * @return Socket file descriptor, or -1 if not connected
 */
int trans_get_fd(const trans_context_t *ctx);

/**
 * @brief Check if transport is connected
 * @param ctx Transport context
 * @return true if connected, false otherwise
 */
bool trans_is_connected(const trans_context_t *ctx);

#endif /* MQTT_TRANS_H */