/**
 * @file test_simple.c
 * @brief Simple MQTT library tests
 */

#include "mqtt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int test_send_count = 0;
static int connection_callback_count = 0;
static int message_callback_count = 0;

/**
 * @brief Test send callback
 */
static int test_send_callback(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;
    (void)data;
    (void)len;
    
    test_send_count++;
    return (int)len;
}

/**
 * @brief Test connection status callback
 */
static void test_connection_status_callback(bool connected, mqtt_conn_return_t return_code, void *user_data)
{
    (void)user_data;
    printf("Connection callback: %s, return code: %d\n", 
           connected ? "connected" : "disconnected", return_code);
    connection_callback_count++;
}

/**
 * @brief Test message received callback
 */
static void test_message_received_callback(const mqtt_message_t *message, void *user_data)
{
    (void)user_data;
    printf("Message callback - Topic: %s, QoS: %d, Payload len: %zu\n", 
           message->topic, message->qos, message->payload_len);
    message_callback_count++;
}

/**
 * @brief Test basic callback functionality
 */
static void test_basic_callbacks(void)
{
    printf("Testing basic callback functionality...\n");
    
    mqtt_config_t config = {
        .client_id = "test_client",
        .username = NULL,
        .password = NULL,
        .keep_alive = 60,
        .clean_session = true,
        .packet_timeout = 5000,
        .max_retry_count = 3
    };
    
    mqtt_handler_t handler = {
        .send = test_send_callback,
        .on_connection = test_connection_status_callback,
        .on_message = test_message_received_callback,
        .publish_ack = NULL,
        .subscribe_ack = NULL,
        .unsubscribe_ack = NULL
    };
    
    mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
    assert(mqtt != NULL);
    
    // Reset test counters
    test_send_count = 0;
    connection_callback_count = 0;
    message_callback_count = 0;
    
    // Test CONNACK packet
    printf("  Testing CONNACK packet...\n");
    uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK
    int result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
    printf("    Result: %d\n", result);
    assert(result == sizeof(connack_packet));
    
    printf("    Connection callback count: %d\n", connection_callback_count);
    assert(connection_callback_count == 1);
    
    // Test PUBLISH packet
    printf("  Testing PUBLISH packet...\n");
    uint8_t publish_packet[] = {
        0x30, 0x0F, // PUBLISH header + remaining length
        0x00, 0x07, 't', 'e', 's', 't', '/', 't', 'o', 'p', 'i', 'c', // topic
        'h', 'e', 'l', 'l', 'o' // payload
    };
    result = mqtt_input(mqtt, publish_packet, sizeof(publish_packet));
    printf("    Result: %d\n", result);
    assert(result == sizeof(publish_packet));
    
    printf("    Message callback count: %d\n", message_callback_count);
    assert(message_callback_count == 1);
    
    // Test PINGRESP packet
    printf("  Testing PINGRESP packet...\n");
    uint8_t pingresp_packet[] = {0xD0, 0x00}; // PINGRESP
    result = mqtt_input(mqtt, pingresp_packet, sizeof(pingresp_packet));
    printf("    Result: %d\n", result);
    assert(result == sizeof(pingresp_packet));
    
    mqtt_destroy(mqtt);
    
    printf("Basic callback test passed!\n");
}

int main(void)
{
    printf("Starting simple MQTT tests...\n\n");
    
    test_basic_callbacks();
    
    printf("\nSimple tests completed successfully!\n");
    return 0;
}