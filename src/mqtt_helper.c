/**
 * @file mqtt_helper.c
 * @brief MQTT helper utilities - memory management and TCP transport layer
 * 
 * Provides memory allocation functions with optional debugging and statistics,
 * and TCP transport layer implementation for MQTT communication.
 */

#include "mqtt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef MQTT_MEM_DEBUG
#include <assert.h>

/**
 * @brief Memory allocation statistics (debug mode only)
 */
static struct {
    size_t total_allocated;      /**< Total bytes allocated */
    size_t total_freed;          /**< Total bytes freed */
    size_t peak_usage;           /**< Peak memory usage */
    size_t current_allocations;  /**< Current number of allocations */
    size_t total_allocations;    /**< Total number of allocations */
    size_t total_frees;          /**< Total number of frees */
} mem_stats = {0};

/**
 * @brief Memory block header (debug mode only)
 */
typedef struct {
    size_t size;                 /**< Size of allocated block */
    const char *file;            /**< Source file where allocated */
    int line;                    /**< Line number where allocated */
    uint32_t magic;              /**< Magic number for validation */
} mem_header_t;

#define MEM_MAGIC 0x4D515454  // "MQTT" in hex

#endif

/**
 * @brief Allocate memory with optional debugging
 * @param size Size to allocate
 * @param file Source file (debug mode only)
 * @param line Line number (debug mode only)
 * @return Pointer to allocated memory, or NULL on failure
 */
static void *mqtt_malloc_internal(size_t size, const char *file, int line)
{
    if (size == 0) {
        return NULL;
    }

#ifdef MQTT_MEM_DEBUG
    // Allocate extra space for header
    size_t total_size = sizeof(mem_header_t) + size;
    mem_header_t *header = (mem_header_t *)malloc(total_size);
    
    if (!header) {
        return NULL;
    }
    
    // Initialize header
    header->size = size;
    header->file = file;
    header->line = line;
    header->magic = MEM_MAGIC;
    
    // Update statistics
    mem_stats.total_allocated += size;
    mem_stats.current_allocations++;
    mem_stats.total_allocations++;
    
    size_t current_usage = mem_stats.total_allocated - mem_stats.total_freed;
    if (current_usage > mem_stats.peak_usage) {
        mem_stats.peak_usage = current_usage;
    }
    
    // Return pointer after header
    return (void *)(header + 1);
#else
    // Simple allocation without debugging
    return malloc(size);
#endif
}

/**
 * @brief Free memory with optional debugging
 * @param ptr Pointer to free
 * @param file Source file (debug mode only)
 * @param line Line number (debug mode only)
 */
static void mqtt_free_internal(void *ptr, const char *file, int line)
{
    if (!ptr) {
        return;
    }

#ifdef MQTT_MEM_DEBUG
    // Get header pointer
    mem_header_t *header = ((mem_header_t *)ptr) - 1;
    
    // Validate magic number
    if (header->magic != MEM_MAGIC) {
        fprintf(stderr, "MQTT MEM ERROR: Invalid memory block freed at %s:%d\n", file, line);
        assert(0);
        return;
    }
    
    // Update statistics
    mem_stats.total_freed += header->size;
    mem_stats.current_allocations--;
    mem_stats.total_frees++;
    
    // Clear magic to detect double-free
    header->magic = 0;
    
    // Free the actual memory
    free(header);
#else
    // Simple free without debugging
    free(ptr);
#endif
}

/**
 * @brief Reallocate memory with optional debugging
 * @param ptr Original pointer
 * @param size New size
 * @param file Source file (debug mode only)
 * @param line Line number (debug mode only)
 * @return Pointer to reallocated memory, or NULL on failure
 */
static void *mqtt_realloc_internal(void *ptr, size_t size, const char *file, int line)
{
    if (!ptr) {
        return mqtt_malloc_internal(size, file, line);
    }
    
    if (size == 0) {
        mqtt_free_internal(ptr, file, line);
        return NULL;
    }

#ifdef MQTT_MEM_DEBUG
    // Get original header
    mem_header_t *old_header = ((mem_header_t *)ptr) - 1;
    
    // Validate magic number
    if (old_header->magic != MEM_MAGIC) {
        fprintf(stderr, "MQTT MEM ERROR: Invalid memory block reallocated at %s:%d\n", file, line);
        assert(0);
        return NULL;
    }
    
    // Reallocate with new size
    size_t total_size = sizeof(mem_header_t) + size;
    mem_header_t *new_header = (mem_header_t *)realloc(old_header, total_size);
    
    if (!new_header) {
        return NULL;
    }
    
    // Update header
    size_t old_size = new_header->size;
    new_header->size = size;
    new_header->file = file;
    new_header->line = line;
    
    // Update statistics
    mem_stats.total_allocated += (size - old_size);
    
    size_t current_usage = mem_stats.total_allocated - mem_stats.total_freed;
    if (current_usage > mem_stats.peak_usage) {
        mem_stats.peak_usage = current_usage;
    }
    
    return (void *)(new_header + 1);
#else
    return realloc(ptr, size);
#endif
}

/**
 * @brief Allocate zero-initialized memory
 * @param nmemb Number of elements
 * @param size Size of each element
 * @param file Source file (debug mode only)
 * @param line Line number (debug mode only)
 * @return Pointer to allocated memory, or NULL on failure
 */
static void *mqtt_calloc_internal(size_t nmemb, size_t size, const char *file, int line)
{
    size_t total_size = nmemb * size;
    void *ptr = mqtt_malloc_internal(total_size, file, line);
    
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    
    return ptr;
}

/**
 * @brief Duplicate a string
 * @param str String to duplicate
 * @param file Source file (debug mode only)
 * @param line Line number (debug mode only)
 * @return Duplicated string, or NULL on failure
 */
static char *mqtt_strdup_internal(const char *str, const char *file, int line)
{
    if (!str) {
        return NULL;
    }
    
    size_t len = strlen(str) + 1;
    char *dup = (char *)mqtt_malloc_internal(len, file, line);
    
    if (dup) {
        memcpy(dup, str, len);
    }
    
    return dup;
}

// Public API functions

void *mqtt_malloc(size_t size)
{
    return mqtt_malloc_internal(size, __FILE__, __LINE__);
}

void mqtt_free(void *ptr)
{
    mqtt_free_internal(ptr, __FILE__, __LINE__);
}

void *mqtt_realloc(void *ptr, size_t size)
{
    return mqtt_realloc_internal(ptr, size, __FILE__, __LINE__);
}

void *mqtt_calloc(size_t nmemb, size_t size)
{
    return mqtt_calloc_internal(nmemb, size, __FILE__, __LINE__);
}

char *mqtt_strdup(const char *str)
{
    return mqtt_strdup_internal(str, __FILE__, __LINE__);
}

#ifdef MQTT_MEM_DEBUG

/**
 * @brief Print memory statistics
 */
void mqtt_mem_stats(void)
{
    size_t current_usage = mem_stats.total_allocated - mem_stats.total_freed;
    
    printf("MQTT Memory Statistics:\n");
    printf("  Total allocated: %zu bytes\n", mem_stats.total_allocated);
    printf("  Total freed:     %zu bytes\n", mem_stats.total_freed);
    printf("  Current usage:   %zu bytes\n", current_usage);
    printf("  Peak usage:      %zu bytes\n", mem_stats.peak_usage);
    printf("  Current allocs:  %zu\n", mem_stats.current_allocations);
    printf("  Total allocs:    %zu\n", mem_stats.total_allocations);
    printf("  Total frees:     %zu\n", mem_stats.total_frees);
    
    if (mem_stats.total_allocations > 0) {
        printf("  Leaks detected:  %s\n", 
               mem_stats.current_allocations > 0 ? "YES" : "NO");
    }
}

/**
 * @brief Check for memory leaks
 * @return Number of memory leaks detected
 */
size_t mqtt_mem_leaks(void)
{
    return mem_stats.current_allocations;
}

/**
 * @brief Reset memory statistics
 */
void mqtt_mem_reset_stats(void)
{
    memset(&mem_stats, 0, sizeof(mem_stats));
}

#endif

// TCP Transport Layer Implementation

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/**
 * @brief Transport layer send callback for TCP sockets
 * @param socket_fd Socket file descriptor
 * @param data Data to send
 * @param len Length of data
 * @return Number of bytes sent, or -1 on error
 */
int mqtt_tcp_send(int socket_fd, const uint8_t *data, size_t len)
{
    if (socket_fd < 0) {
        return -1;
    }
    
    ssize_t sent = send(socket_fd, data, len, 0);
    if (sent < 0) {
        return -1;
    }
    
    return (int)sent;
}

/**
 * @brief Connect to MQTT broker via TCP
 * @param host Broker hostname or IP address
 * @param port Broker port
 * @return Socket file descriptor on success, -1 on failure
 */
int mqtt_tcp_connect(const char *host, uint16_t port)
{
    struct sockaddr_in server_addr;
    
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return -1;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        close(socket_fd);
        return -1;
    }
    
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd);
        return -1;
    }
    
    return socket_fd;
}

/**
 * @brief Read data from TCP socket and process it
 * @param socket_fd Socket file descriptor
 * @param buffer Buffer to store received data
 * @param buffer_size Size of buffer
 * @param process_callback Callback function to process received data
 * @param user_data User data passed to callback
 * @return Number of bytes read, 0 on connection closed, -1 on error
 */
int mqtt_tcp_read_and_process(int socket_fd, uint8_t *buffer, size_t buffer_size,
                             int (*process_callback)(const uint8_t *data, size_t len, void *user_data),
                             void *user_data)
{
    if (socket_fd < 0) {
        return -1;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    ssize_t bytes_read = recv(socket_fd, buffer, buffer_size, 0);
    
    if (bytes_read > 0) {
        // Process the received data
        if (process_callback && process_callback(buffer, bytes_read, user_data) < 0) {
            return -1;
        }
    } else if (bytes_read == 0) {
        // Connection closed by server
        return 0;
    } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // Error (not just no data available)
        return -1;
    }
    
    return (int)bytes_read;
}

/**
 * @brief Close TCP connection
 * @param socket_fd Socket file descriptor to close
 */
void mqtt_tcp_close(int socket_fd)
{
    if (socket_fd >= 0) {
        close(socket_fd);
    }
}