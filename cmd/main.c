/**
 * @file main.c
 * @brief MQTT command line client
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

static mqtt_t *g_mqtt = NULL;
static trans_context_t *g_trans = NULL;
static volatile sig_atomic_t g_running = 1;

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\nShutting down...\n");
}

/**
 * @brief Transport layer send callback for MQTT
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
        printf("Connected to MQTT broker\n");
    } else {
        printf("Disconnected from MQTT broker, return code: %d\n", return_code);
    }
}

/**
 * @brief Message received callback
 */
static void on_message(const mqtt_message_t *message, void *user_data)
{
    (void)user_data;
    
    printf("Message received:\n");
    printf("  Topic: %s\n", message->topic);
    printf("  QoS: %d\n", message->qos);
    printf("  Retain: %s\n", message->retain ? "true" : "false");
    printf("  Payload (%zu bytes): ", message->payload_len);
    
    // Print payload as string if printable, otherwise as hex
    int printable = 1;
    for (size_t i = 0; i < message->payload_len && i < 100; i++) {
        if (message->payload[i] < 32 || message->payload[i] > 126) {
            printable = 0;
            break;
        }
    }
    
    if (printable && message->payload_len > 0) {
        printf("%.*s\n", (int)message->payload_len, (const char *)message->payload);
    } else if (message->payload_len > 0) {
        printf("\n");
        for (size_t i = 0; i < message->payload_len && i < 32; i++) {
            printf("%02x ", message->payload[i]);
        }
        if (message->payload_len > 32) {
            printf("...");
        }
        printf("\n");
    } else {
        printf("(empty)\n");
    }
    printf("\n");
}

/**
 * @brief Publish acknowledgment callback
 */
static void publish_ack(uint16_t packet_id, void *user_data)
{
    (void)user_data;
    printf("Publish acknowledged for packet ID: %d\n", packet_id);
}

/**
 * @brief Subscribe acknowledgment callback
 */
static void subscribe_ack(uint16_t packet_id, const mqtt_qos_t *return_codes, size_t count, void *user_data)
{
    (void)user_data;
    printf("Subscribe acknowledged for packet ID: %d\n", packet_id);
    for (size_t i = 0; i < count; i++) {
        printf("  Topic %zu: QoS %d granted\n", i + 1, return_codes[i]);
    }
}

/**
 * @brief Unsubscribe acknowledgment callback
 */
static void unsubscribe_ack(uint16_t packet_id, void *user_data)
{
    (void)user_data;
    printf("Unsubscribe acknowledged for packet ID: %d\n", packet_id);
}

/**
 * @brief Transport layer data received callback
 */
static void trans_data_received(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;
    
    // Feed data to MQTT layer
    if (mqtt_input(g_mqtt, data, len) < 0) {
        fprintf(stderr, "Failed to process MQTT data\n");
    }
}

/**
 * @brief Transport layer connection status callback
 */
static void trans_connection_status(bool connected, void *user_data)
{
    (void)user_data;
    
    if (!connected) {
        printf("Transport connection lost\n");
        g_running = 0;
    }
}

/**
 * @brief Print usage information
 */
static void print_usage(const char *program_name)
{
    printf("Usage: %s <host> <port> <client_id> [username] [password]\n", program_name);
    printf("\n");
    printf("Examples:\n");
    printf("  %s localhost 1883 my_client\n", program_name);
    printf("  %s test.mosquitto.org 1883 my_client user pass\n", program_name);
    printf("\n");
    printf("Commands:\n");
    printf("  subscribe <topic> [qos]        - Subscribe to a topic (QoS 0-2)\n");
    printf("  subscribe_multi <topic1> <qos1> [topic2 qos2 ...] - Subscribe to multiple topics\n");
    printf("  publish <topic> <msg> [qos] [retain] - Publish a message\n");
    printf("  unsubscribe <topic>            - Unsubscribe from a topic\n");
    printf("  unsubscribe_multi <topic1> [topic2 ...] - Unsubscribe from multiple topics\n");
    printf("  quit                          - Exit the program\n");
}

/**
 * @brief Process user commands
 */
static int process_command(const char *command)
{
    char cmd[256];
    char topic[256];
    char message[1024];
    int qos_level = 0;
    int retain_flag = 0;
    
    // Parse subscribe with QoS
    if (sscanf(command, "subscribe %255s %d", topic, &qos_level) >= 1) {
        mqtt_qos_t qos = MQTT_QOS_0;
        if (qos_level >= 0 && qos_level <= 2) {
            qos = (mqtt_qos_t)qos_level;
        }
        
        printf("Subscribing to topic: %s with QoS %d\n", topic, qos);
        
        const char *topics[] = {topic};
        mqtt_qos_t qos_arr[] = {qos};
        
        if (mqtt_subscribe(g_mqtt, topics, qos_arr, 1) < 0) {
            fprintf(stderr, "Failed to subscribe\n");
        }
        
    // Parse publish with QoS and retain
    } else if (sscanf(command, "publish %255s %1023[^\n] %d %d", topic, message, &qos_level, &retain_flag) >= 2) {
        mqtt_qos_t qos = MQTT_QOS_0;
        if (qos_level >= 0 && qos_level <= 2) {
            qos = (mqtt_qos_t)qos_level;
        }
        
        printf("Publishing to topic: %s, message: %s, QoS: %d, retain: %s\n", 
               topic, message, qos, retain_flag ? "true" : "false");
        
        mqtt_message_t pub_msg = {
            .topic = topic,
            .payload = (const uint8_t *)message,
            .payload_len = strlen(message),
            .qos = qos,
            .retain = retain_flag != 0,
            .packet_id = mqtt_get_packet_id(g_mqtt)
        };
        
        if (mqtt_publish(g_mqtt, &pub_msg) < 0) {
            fprintf(stderr, "Failed to publish\n");
        }
        
    // Parse simple publish (backward compatibility)
    } else if (sscanf(command, "publish %255s %1023[^\n]", topic, message) == 2) {
        printf("Publishing to topic: %s, message: %s\n", topic, message);
        
        mqtt_message_t pub_msg = {
            .topic = topic,
            .payload = (const uint8_t *)message,
            .payload_len = strlen(message),
            .qos = MQTT_QOS_0,
            .retain = false,
            .packet_id = mqtt_get_packet_id(g_mqtt)
        };
        
        if (mqtt_publish(g_mqtt, &pub_msg) < 0) {
            fprintf(stderr, "Failed to publish\n");
        }
        
    // Parse unsubscribe
    } else if (sscanf(command, "unsubscribe %255s", topic) == 1) {
        printf("Unsubscribing from topic: %s\n", topic);
        
        const char *topics[] = {topic};
        
        if (mqtt_unsubscribe(g_mqtt, topics, 1) < 0) {
            fprintf(stderr, "Failed to unsubscribe\n");
        }
        
    } else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
        printf("Exiting...\n");
        g_running = 0;
        
    } else if (strcmp(command, "help") == 0) {
        printf("Available commands:\n");
        printf("  subscribe <topic> [qos]        - Subscribe to a topic (QoS 0-2)\n");
        printf("  publish <topic> <msg> [qos] [retain] - Publish a message\n");
        printf("  unsubscribe <topic>            - Unsubscribe from a topic\n");
        printf("  quit                          - Exit the program\n");
        printf("  help                          - Show this help\n");
        
    } else {
        printf("Unknown command: %s\n", command);
        printf("Type 'help' for available commands\n");
    }
    
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *host = argv[1];
    uint16_t port = (uint16_t)atoi(argv[2]);
    const char *client_id = argv[3];
    const char *username = argc > 4 ? argv[4] : NULL;
    const char *password = argc > 5 ? argv[5] : NULL;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Setup transport layer configuration
    trans_config_t trans_config = {
        .host = host,
        .port = port,
        .socket_timeout_ms = 5000
    };
    
    // Setup transport layer handlers
    trans_handler_t trans_handler = {
        .on_data = trans_data_received,
        .on_connection = trans_connection_status
    };
    
    // Create transport layer context
    g_trans = trans_create(&trans_config, &trans_handler, NULL);
    if (!g_trans) {
        fprintf(stderr, "Failed to create transport layer\n");
        return 1;
    }
    
    // Connect to server
    if (trans_connect(g_trans) < 0) {
        fprintf(stderr, "Failed to connect to server\n");
        trans_destroy(g_trans);
        return 1;
    }
    
    // Setup MQTT configuration
    mqtt_config_t config = {
        .client_id = client_id,
        .username = username,
        .password = password,
        .keep_alive = 60,
        .clean_session = true,
        .packet_timeout = 5000,
        .max_retry_count = 3
    };
    
    // Setup MQTT handlers
    mqtt_handler_t handler = {
        .send = mqtt_transport_send,
        .on_connection = on_connection,
        .on_message = on_message,
        .publish_ack = publish_ack,
        .subscribe_ack = subscribe_ack,
        .unsubscribe_ack = unsubscribe_ack
    };
    
    // Create MQTT instance
    g_mqtt = mqtt_create(&config, &handler, NULL);
    if (!g_mqtt) {
        fprintf(stderr, "Failed to create MQTT instance\n");
        trans_destroy(g_trans);
        return 1;
    }
    
    // Connect to MQTT broker
    if (mqtt_connect(g_mqtt) < 0) {
        fprintf(stderr, "Failed to connect to MQTT broker\n");
        mqtt_destroy(g_mqtt);
        trans_destroy(g_trans);
        return 1;
    }
    
    printf("MQTT client started. Type 'help' for commands.\n");
    
    struct timespec last_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);
    
    // Main loop
    while (g_running) {
        // Calculate elapsed time for timer
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        
        uint32_t elapsed_ms = (current_time.tv_sec - last_time.tv_sec) * 1000 +
                             (current_time.tv_nsec - last_time.tv_nsec) / 1000000;
        last_time = current_time;
        
        // Process MQTT timer
        if (mqtt_timer(g_mqtt, elapsed_ms) < 0) {
            fprintf(stderr, "MQTT timer error\n");
            break;
        }
        
        // Process transport layer
        if (trans_process(g_trans, 100) < 0) {
            break;
        }
        
        // Check for user input
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        
        int trans_fd = trans_get_fd(g_trans);
        if (trans_fd >= 0) {
            FD_SET(trans_fd, &read_fds);
        }
        
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000}; // 100ms
        
        int max_fd = trans_fd > STDIN_FILENO ? trans_fd : STDIN_FILENO;
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ready > 0) {
            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                // User input
                char input[1024];
                if (fgets(input, sizeof(input), stdin)) {
                    // Remove newline
                    input[strcspn(input, "\n")] = 0;
                    process_command(input);
                }
            }
            
            if (FD_ISSET(trans_fd, &read_fds)) {
                // More data available from transport
                trans_process(g_trans, 0);
            }
        } else if (ready < 0 && errno != EINTR) {
            perror("select failed");
            break;
        }
    }
    
    // Cleanup
    if (g_mqtt) {
        mqtt_disconnect(g_mqtt);
        mqtt_destroy(g_mqtt);
    }
    
    if (g_trans) {
        trans_destroy(g_trans);
    }
    
    printf("MQTT client stopped\n");
    return 0;
}