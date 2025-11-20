/**
 * @file main.c
 * @brief Simple MQTT client with sub/pub modes
 */

#include "../include/mqtt.h"
#include "../include/trans.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

// Operation modes
typedef enum {
    MODE_SUBSCRIBE,
    MODE_PUBLISH
} operation_mode_t;

// Global state
static mqtt_t *g_mqtt = NULL;
static trans_context_t *g_trans = NULL;
static volatile sig_atomic_t g_running = 1;
static operation_mode_t g_mode;
static const char *g_topic = NULL;
static const char *g_message = NULL;
static char *g_stdin_message = NULL;  // For large messages read from stdin
static uint32_t g_interval_ms = 1000;

/**
 * @brief Get current timestamp string
 */
static void get_timestamp(char *buffer, size_t size)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    time_t now = tv.tv_sec;
    struct tm *tm_info = localtime(&now);

    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             (int)(tv.tv_usec / 1000));
}

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/**
 * @brief Transport layer send callback
 */
static int mqtt_transport_send(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;

    if (!g_trans) {
        return -1;
    }

    return trans_send(g_trans, data, len);
}

/**
 * @brief Connection status callback
 */
static void on_connection(bool connected, mqtt_conn_return_t return_code, void *user_data)
{
    (void)user_data;

    if (connected) {
        printf("[INFO] Connected to MQTT broker\n");
    } else {
        printf("[ERROR] Disconnected from broker, return code: %d\n", return_code);
        g_running = 0;
    }
}

/**
 * @brief Message received callback (for subscribe mode)
 */
static void on_message(const mqtt_message_t *message, void *user_data)
{
    (void)user_data;

    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("[%s] Received %zu bytes: %.*s\n",
           timestamp,
           message->payload_len,
           (int)message->payload_len,
           (const char *)message->payload);
}

/**
 * @brief Subscribe acknowledgment callback
 */
static void subscribe_ack(uint16_t packet_id, const mqtt_qos_t *return_codes, size_t count, void *user_data)
{
    (void)user_data;
    (void)packet_id;
    (void)return_codes;
    (void)count;

    printf("[INFO] Successfully subscribed to topic: %s\n", g_topic);
}

/**
 * @brief Transport layer data received callback
 */
static void trans_data_received(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;

    if (mqtt_input(g_mqtt, data, len) < 0) {
        fprintf(stderr, "[ERROR] Failed to process MQTT data\n");
    }
}

/**
 * @brief Transport layer connection status callback
 */
static void trans_connection_status(bool connected, void *user_data)
{
    (void)user_data;

    if (!connected) {
        printf("[ERROR] Transport connection lost\n");
        g_running = 0;
    }
}

/**
 * @brief Print usage information
 */
static void print_usage(const char *program_name)
{
    printf("Usage:\n");
    printf("  Subscribe mode:\n");
    printf("    %s sub <host> <port> <topic> <username> <password>\n\n", program_name);
    printf("  Publish mode:\n");
    printf("    %s pub <host> <port> <topic> <message> <interval_ms> <username> <password>\n\n", program_name);
    printf("Examples:\n");
    printf("  %s sub 198.19.249.149 1883 topic/recv sender 123456\n", program_name);
    printf("  %s pub 198.19.249.149 1883 topic/recv \"Hello\" 1000 sender 123456\n\n", program_name);
    printf("Large messages from stdin:\n");
    printf("  Use '-' as message to read from stdin (for messages larger than shell argument limits)\n");
    printf("  echo \"Large message\" | %s pub 198.19.249.149 1883 topic/recv - 1000 sender 123456\n\n", program_name);
    printf("Random message generation:\n");
    printf("  Use 'random:SIZE' as message to generate random strings (SIZE: 1-9999 bytes)\n");
    printf("  %s pub 198.19.249.149 1883 topic/recv \"random:256\" 1000 sender 123456\n", program_name);
    printf("  %s pub 198.19.249.149 1883 topic/recv \"random:1024\" 1000 sender 123456\n", program_name);
}

/**
 * @brief Initialize MQTT and transport layers
 */
static int initialize_connection(const char *host, uint16_t port,
                                 const char *client_id, const char *username, const char *password)
{
    // Setup transport layer
    trans_config_t trans_config = {
        .host = host,
        .port = port,
        .socket_timeout_ms = 5000
    };

    trans_handler_t trans_handler = {
        .on_data = trans_data_received,
        .on_connection = trans_connection_status
    };

    g_trans = trans_create(&trans_config, &trans_handler, NULL);
    if (!g_trans) {
        fprintf(stderr, "[ERROR] Failed to create transport layer\n");
        return -1;
    }

    if (trans_connect(g_trans) < 0) {
        fprintf(stderr, "[ERROR] Failed to connect to server\n");
        trans_destroy(g_trans);
        return -1;
    }

    // Setup MQTT layer
    mqtt_config_t mqtt_config = {
        .client_id = client_id,
        .username = username,
        .password = password,
        .keep_alive = 60,
        .clean_session = true,
        .packet_timeout = 5000,
        .max_retry_count = 3
    };

    mqtt_handler_t mqtt_handler = {
        .send = mqtt_transport_send,
        .on_connection = on_connection,
        .on_message = on_message,
        .publish_ack = NULL,
        .subscribe_ack = subscribe_ack,
        .unsubscribe_ack = NULL
    };

    g_mqtt = mqtt_create(&mqtt_config, &mqtt_handler, NULL);
    if (!g_mqtt) {
        fprintf(stderr, "[ERROR] Failed to create MQTT instance\n");
        trans_destroy(g_trans);
        return -1;
    }

    if (mqtt_connect(g_mqtt) < 0) {
        fprintf(stderr, "[ERROR] Failed to connect to MQTT broker\n");
        mqtt_destroy(g_mqtt);
        trans_destroy(g_trans);
        return -1;
    }

    return 0;
}

/**
 * @brief Run subscribe mode
 */
static int run_subscribe_mode(void)
{
    printf("[INFO] Subscribe mode: waiting for connection...\n");

    // Wait for CONNACK and connection to be established
    struct timespec last_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    int wait_count = 0;
    while (wait_count < 50) {  // Wait up to 5 seconds, don't exit early on disconnect
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);

        uint32_t elapsed_ms = (current_time.tv_sec - last_time.tv_sec) * 1000 +
                             (current_time.tv_nsec - last_time.tv_nsec) / 1000000;
        last_time = current_time;

        if (mqtt_timer(g_mqtt, elapsed_ms) < 0) {
            fprintf(stderr, "[ERROR] MQTT timer error\n");
            return -1;
        }

        if (trans_process(g_trans, 100) < 0) {
            return -1;
        }

        wait_count++;
    }

    // Reset g_running after wait loop - ignore any disconnects during initial setup
    g_running = 1;

    printf("[INFO] Subscribing to topic '%s'...\n", g_topic);

    // Subscribe to topic
    const char *topics[] = {g_topic};
    mqtt_qos_t qos[] = {MQTT_QOS_0};

    if (mqtt_subscribe(g_mqtt, topics, qos, 1) < 0) {
        fprintf(stderr, "[ERROR] Failed to subscribe\n");
        return -1;
    }

    // Wait briefly for SUBACK
    for (int i = 0; i < 10; i++) {
        if (mqtt_timer(g_mqtt, 10) < 0) {
            fprintf(stderr, "[ERROR] MQTT timer error after subscribe\n");
            return -1;
        }
        if (trans_process(g_trans, 10) < 0) {
            fprintf(stderr, "[ERROR] trans_process error after subscribe\n");
            return -1;
        }
    }

    printf("[INFO] Waiting for messages...\n");

    // Main loop - just process incoming messages
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    while (g_running) {
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);

        uint32_t elapsed_ms = (current_time.tv_sec - last_time.tv_sec) * 1000 +
                             (current_time.tv_nsec - last_time.tv_nsec) / 1000000;
        last_time = current_time;

        if (mqtt_timer(g_mqtt, elapsed_ms) < 0) {
            fprintf(stderr, "[ERROR] MQTT timer error\n");
            break;
        }

        if (trans_process(g_trans, 100) < 0) {
            fprintf(stderr, "[ERROR] trans_process error\n");
            break;
        }
    }

    return 0;
}

/**
 * @brief Generate random string
 */
static void generate_random_string(char *str, size_t len)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t charset_size = sizeof(charset) - 1;

    for (size_t i = 0; i < len - 1; i++) {
        str[i] = charset[rand() % charset_size];
    }
    str[len - 1] = '\0';
}

/**
 * @brief Read message from stdin (for large messages)
 */
static char* read_message_from_stdin(size_t *out_len)
{
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = malloc(capacity);

    if (!buffer) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for stdin message\n");
        return NULL;
    }

    int c;
    while ((c = getchar()) != EOF) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *new_buffer = realloc(buffer, capacity);
            if (!new_buffer) {
                fprintf(stderr, "[ERROR] Failed to reallocate memory\n");
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }
        buffer[length++] = (char)c;
    }

    buffer[length] = '\0';
    *out_len = length;
    return buffer;
}

/**
 * @brief Run publish mode
 */
static int run_publish_mode(void)
{
    printf("[INFO] Publish mode: waiting for connection...\n");

    // Wait for CONNACK and connection to be established
    struct timespec last_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    int wait_count = 0;
    while (wait_count < 50) {  // Wait up to 5 seconds, don't exit early on disconnect
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);

        uint32_t elapsed_ms = (current_time.tv_sec - last_time.tv_sec) * 1000 +
                             (current_time.tv_nsec - last_time.tv_nsec) / 1000000;
        last_time = current_time;

        if (mqtt_timer(g_mqtt, elapsed_ms) < 0) {
            fprintf(stderr, "[ERROR] MQTT timer error\n");
            return -1;
        }

        if (trans_process(g_trans, 100) < 0) {
            return -1;
        }

        wait_count++;
    }

    // Reset g_running after wait loop - ignore any disconnects during initial setup
    g_running = 1;

    // Determine message source
    const char *actual_message = g_stdin_message ? g_stdin_message : g_message;
    bool use_random = false;
    size_t random_size = 0;

    if (g_stdin_message) {
        printf("[INFO] Sending stdin message (%zu bytes) to topic '%s' every %u ms\n",
               strlen(g_stdin_message), g_topic, g_interval_ms);
    } else if (strncmp(g_message, "random:", 7) == 0) {
        random_size = atoi(g_message + 7);
        if (random_size > 0 && random_size < 10000) {
            use_random = true;
            printf("[INFO] Sending random %zu-byte messages to topic '%s' every %u ms\n",
                   random_size, g_topic, g_interval_ms);
        } else {
            fprintf(stderr, "[ERROR] Invalid random size (must be 1-9999): %zu\n", random_size);
            return -1;
        }
    } else {
        printf("[INFO] Sending '%s' to topic '%s' every %u ms\n",
               actual_message, g_topic, g_interval_ms);
    }

    // Initialize random seed
    srand(time(NULL));

    struct timespec last_publish;
    clock_gettime(CLOCK_MONOTONIC, &last_time);
    last_publish = last_time;

    uint32_t publish_elapsed = 0;
    uint32_t message_count = 0;

    while (g_running) {
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);

        uint32_t elapsed_ms = (current_time.tv_sec - last_time.tv_sec) * 1000 +
                             (current_time.tv_nsec - last_time.tv_nsec) / 1000000;
        last_time = current_time;

        publish_elapsed += elapsed_ms;

        // Check if it's time to publish
        if (publish_elapsed >= g_interval_ms) {
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));

            char *payload = NULL;
            size_t payload_len = 0;

            if (use_random) {
                // Allocate buffer for random message
                size_t total_size = random_size + 128;  // Extra space for timestamp and counter
                payload = malloc(total_size);
                if (!payload) {
                    fprintf(stderr, "[ERROR] Failed to allocate memory\n");
                    break;
                }

                // Generate random string
                char *random_str = malloc(random_size + 1);
                if (!random_str) {
                    free(payload);
                    fprintf(stderr, "[ERROR] Failed to allocate memory\n");
                    break;
                }
                generate_random_string(random_str, random_size + 1);

                snprintf(payload, total_size, "[%s] #%u %s", timestamp, message_count++, random_str);
                payload_len = strlen(payload);

                printf("[%s] Published: %zu bytes (random)\n", timestamp, payload_len);

                free(random_str);
            } else {
                // Use fixed message (either from command line or stdin)
                size_t buf_size = strlen(actual_message) + 128;
                payload = malloc(buf_size);
                if (!payload) {
                    fprintf(stderr, "[ERROR] Failed to allocate memory\n");
                    break;
                }

                snprintf(payload, buf_size, "[%s] %s #%u", timestamp, actual_message, message_count++);
                payload_len = strlen(payload);

                printf("[%s] Published: %s\n", timestamp, payload);
            }

            mqtt_message_t msg = {
                .topic = g_topic,
                .payload = (const uint8_t *)payload,
                .payload_len = payload_len,
                .qos = MQTT_QOS_0,
                .retain = false,
                .packet_id = mqtt_get_packet_id(g_mqtt)
            };

            if (mqtt_publish(g_mqtt, &msg) < 0) {
                fprintf(stderr, "[ERROR] Failed to publish\n");
            }

            free(payload);
            publish_elapsed = 0;
        }

        if (mqtt_timer(g_mqtt, elapsed_ms) < 0) {
            fprintf(stderr, "[ERROR] MQTT timer error\n");
            break;
        }

        if (trans_process(g_trans, 10) < 0) {
            break;
        }
    }

    return 0;
}

/**
 * @brief Main function
 */
int main(int argc, char *argv[])
{
    // Disable stdout buffering to see output immediately
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // Parse command line arguments
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *mode_str = argv[1];

    if (strcmp(mode_str, "sub") == 0) {
        // Subscribe mode: sub <host> <port> <topic> <username> <password>
        if (argc != 7) {
            fprintf(stderr, "Error: Invalid arguments for subscribe mode\n\n");
            print_usage(argv[0]);
            return 1;
        }

        g_mode = MODE_SUBSCRIBE;
        const char *host = argv[2];
        uint16_t port = (uint16_t)atoi(argv[3]);
        g_topic = argv[4];
        const char *username = argv[5];
        const char *password = argv[6];

        char client_id[64];
        snprintf(client_id, sizeof(client_id), "sub_client_%d", getpid());

        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        if (initialize_connection(host, port, client_id, username, password) < 0) {
            return 1;
        }

        // Wait for connection to establish
        sleep(1);

        int ret = run_subscribe_mode();

        mqtt_disconnect(g_mqtt);
        mqtt_destroy(g_mqtt);
        trans_destroy(g_trans);

        return ret;

    } else if (strcmp(mode_str, "pub") == 0) {
        // Publish mode: pub <host> <port> <topic> <message> <interval_ms> <username> <password>
        if (argc != 9) {
            fprintf(stderr, "Error: Invalid arguments for publish mode\n\n");
            print_usage(argv[0]);
            return 1;
        }

        g_mode = MODE_PUBLISH;
        const char *host = argv[2];
        uint16_t port = (uint16_t)atoi(argv[3]);
        g_topic = argv[4];
        const char *message_arg = argv[5];
        g_interval_ms = (uint32_t)atoi(argv[6]);
        const char *username = argv[7];
        const char *password = argv[8];

        // Check if message should be read from stdin
        if (strcmp(message_arg, "-") == 0) {
            size_t len = 0;
            g_stdin_message = read_message_from_stdin(&len);
            if (!g_stdin_message) {
                fprintf(stderr, "Error: Failed to read message from stdin\n");
                return 1;
            }
            g_message = g_stdin_message;  // For compatibility
        } else {
            g_message = message_arg;
        }

        if (g_interval_ms < 10) {
            g_interval_ms = 10; // Minimum 10ms
        }

        char client_id[64];
        snprintf(client_id, sizeof(client_id), "pub_client_%d", getpid());

        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        if (initialize_connection(host, port, client_id, username, password) < 0) {
            return 1;
        }

        // Wait for connection to establish
        sleep(1);

        int ret = run_publish_mode();

        mqtt_disconnect(g_mqtt);
        mqtt_destroy(g_mqtt);
        trans_destroy(g_trans);

        // Cleanup stdin message if allocated
        if (g_stdin_message) {
            free(g_stdin_message);
        }

        return ret;

    } else {
        fprintf(stderr, "Error: Unknown mode '%s'\n\n", mode_str);
        print_usage(argv[0]);
        return 1;
    }
}
