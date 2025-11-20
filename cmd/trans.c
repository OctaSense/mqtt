/**
 * @file trans.c
 * @brief Transport layer implementation for MQTT client
 */

#include "../include/trans.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/**
 * @brief Transport layer context structure
 */
struct trans_context_s {
    trans_config_t config;          /**< Transport configuration */
    trans_handler_t handler;        /**< Transport callbacks */
    void *user_data;                /**< User context */
    int socket_fd;                  /**< Socket file descriptor */
    bool connected;                 /**< Connection status */
    uint8_t *recv_buffer;           /**< Receive buffer */
    size_t recv_buffer_size;        /**< Receive buffer size */
};

/**
 * @brief Default receive buffer size
 */
#define TRANS_DEFAULT_BUFFER_SIZE 4096

/**
 * @brief Create transport layer context
 */
trans_context_t *trans_create(const trans_config_t *config, const trans_handler_t *handler, void *user_data)
{
    if (!config || !handler) {
        return NULL;
    }
    
    trans_context_t *ctx = calloc(1, sizeof(trans_context_t));
    if (!ctx) {
        return NULL;
    }
    
    // Copy configuration
    ctx->config = *config;
    ctx->handler = *handler;
    ctx->user_data = user_data;
    ctx->socket_fd = -1;
    ctx->connected = false;
    
    // Allocate receive buffer
    ctx->recv_buffer_size = TRANS_DEFAULT_BUFFER_SIZE;
    ctx->recv_buffer = malloc(ctx->recv_buffer_size);
    if (!ctx->recv_buffer) {
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

/**
 * @brief Destroy transport layer context
 */
void trans_destroy(trans_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    
    // Disconnect if connected
    if (ctx->connected) {
        trans_disconnect(ctx);
    }
    
    // Free resources
    if (ctx->recv_buffer) {
        free(ctx->recv_buffer);
    }
    
    free(ctx);
}

/**
 * @brief Connect to server
 */
int trans_connect(trans_context_t *ctx)
{
    if (!ctx || ctx->connected) {
        return -1;
    }
    
    struct sockaddr_in server_addr;
    
    // Create socket
    ctx->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->socket_fd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(ctx->socket_fd, F_GETFL, 0);
    if (fcntl(ctx->socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl failed");
        close(ctx->socket_fd);
        ctx->socket_fd = -1;
        return -1;
    }
    
    // Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ctx->config.port);
    
    if (inet_pton(AF_INET, ctx->config.host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", ctx->config.host);
        close(ctx->socket_fd);
        ctx->socket_fd = -1;
        return -1;
    }
    
    // Connect to server
    if (connect(ctx->socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            perror("connection failed");
            close(ctx->socket_fd);
            ctx->socket_fd = -1;
            return -1;
        }
        
        // Connection in progress, wait for completion
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(ctx->socket_fd, &write_fds);
        
        struct timeval timeout = {
            .tv_sec = ctx->config.socket_timeout_ms / 1000,
            .tv_usec = (ctx->config.socket_timeout_ms % 1000) * 1000
        };
        
        int ready = select(ctx->socket_fd + 1, NULL, &write_fds, NULL, &timeout);
        if (ready <= 0) {
            fprintf(stderr, "Connection timeout or error\n");
            close(ctx->socket_fd);
            ctx->socket_fd = -1;
            return -1;
        }
        
        // Check if connection succeeded
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(ctx->socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            perror("getsockopt failed");
            close(ctx->socket_fd);
            ctx->socket_fd = -1;
            return -1;
        }
        
        if (error != 0) {
            fprintf(stderr, "Connection failed: %s\n", strerror(error));
            close(ctx->socket_fd);
            ctx->socket_fd = -1;
            return -1;
        }
    }
    
    ctx->connected = true;
    
    // Notify connection established
    if (ctx->handler.on_connection) {
        ctx->handler.on_connection(true, ctx->user_data);
    }
    
    printf("Connected to %s:%d\n", ctx->config.host, ctx->config.port);
    return 0;
}

/**
 * @brief Disconnect from server
 */
int trans_disconnect(trans_context_t *ctx)
{
    if (!ctx || !ctx->connected) {
        return -1;
    }
    
    // Close socket
    if (ctx->socket_fd >= 0) {
        close(ctx->socket_fd);
        ctx->socket_fd = -1;
    }
    
    ctx->connected = false;
    
    // Notify connection closed
    if (ctx->handler.on_connection) {
        ctx->handler.on_connection(false, ctx->user_data);
    }
    
    printf("Disconnected from %s:%d\n", ctx->config.host, ctx->config.port);
    return 0;
}

/**
 * @brief Send data through transport layer
 */
int trans_send(trans_context_t *ctx, const uint8_t *data, size_t len)
{
    if (!ctx || !ctx->connected || !data || len == 0) {
        return -1;
    }
    
    ssize_t sent = send(ctx->socket_fd, data, len, 0);
    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("send failed");
            return -1;
        }
        return 0; // Would block
    }

    printf("Sent %zd bytes", sent);
    if (sent > 0 && sent <= 20) {
        printf(" [");
        for (ssize_t i = 0; i < sent; i++) {
            printf("%02x ", data[i]);
        }
        printf("]");
    }
    printf("\n");
    return (int)sent;
}

/**
 * @brief Process incoming data
 */
int trans_process(trans_context_t *ctx, int timeout_ms)
{
    if (!ctx || !ctx->connected) {
        return -1;
    }
    
    // Check if data is available
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(ctx->socket_fd, &read_fds);
    
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };
    
    int ready = select(ctx->socket_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (ready <= 0) {
        return 0; // Timeout or error (non-fatal)
    }
    
    // Read available data
    ssize_t bytes_read = recv(ctx->socket_fd, ctx->recv_buffer, ctx->recv_buffer_size, 0);
    
    if (bytes_read > 0) {
        printf("Received %zd bytes from transport layer\n", bytes_read);
        
        // Notify data received
        if (ctx->handler.on_data) {
            ctx->handler.on_data(ctx->recv_buffer, bytes_read, ctx->user_data);
        }
    } else if (bytes_read == 0) {
        printf("Connection closed by server\n");
        trans_disconnect(ctx);
        return -1;
    } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("recv failed");
        trans_disconnect(ctx);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Get socket file descriptor for select/poll
 */
int trans_get_fd(const trans_context_t *ctx)
{
    if (!ctx || !ctx->connected) {
        return -1;
    }
    return ctx->socket_fd;
}

/**
 * @brief Check if transport is connected
 */
bool trans_is_connected(const trans_context_t *ctx)
{
    return ctx && ctx->connected;
}