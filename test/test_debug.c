/**
 * @file test_debug.c
 * @brief Debug MQTT library tests
 */

#include "mqtt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int test_send_count = 0;
static int connection_callback_count = 0;
static int message_callback_count = 0;
static int publish_ack_callback_count = 0;

/**
 * @brief Test send callback
 */
static int test_send_callback(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;
    printf("Send callback: %zu bytes\n", len);
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
 * @brief Test publish acknowledgment callback
 */
static void test_publish_ack_callback(uint16_t packet_id, void *user_data)
{
    (void)user_data;
    printf("Publish ACK callback - Packet ID: %d\n", packet_id);
    publish_ack_callback_count++;
}

/**
 * @brief Test PUBACK packet specifically
 */
static void test_puback_packet(void)
{
    printf("Testing PUBACK packet specifically...\n");
    
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
        .publish_ack = test_publish_ack_callback,
        .subscribe_ack = NULL,
        .unsubscribe_ack = NULL
    };
    
    mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
    assert(mqtt != NULL);
    
    // Reset test counters
    test_send_count = 0;
    connection_callback_count = 0;
    message_callback_count = 0;
    publish_ack_callback_count = 0;
    
    printf("  Initial state: %d\n", mqtt_get_state(mqtt));
    
    // First connect
    printf("  Testing CONNACK packet...\n");
    uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK
    int result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
    printf("    Result: %d, State: %d\n", result, mqtt_get_state(mqtt));
    assert(result == sizeof(connack_packet));
    
    printf("    Connection callback count: %d\n", connection_callback_count);
    assert(connection_callback_count == 1);
    
    // Now test PUBACK
    printf("  Testing PUBACK packet...\n");
    uint8_t puback_packet[] = {0x40, 0x02, 0x00, 0x01}; // PUBACK for packet ID 1
    printf("    PUBACK bytes: %02x %02x %02x %02x\n", 
           puback_packet[0], puback_packet[1], puback_packet[2], puback_packet[3]);
    
    result = mqtt_input(mqtt, puback_packet, sizeof(puback_packet));
    printf("    Result: %d\n", result);
    printf("    Publish ACK callback count: %d\n", publish_ack_callback_count);
    
    // Try another PUBACK with different packet ID
    printf("  Testing another PUBACK packet...\n");
    uint8_t puback_packet2[] = {0x40, 0x02, 0x00, 0x02}; // PUBACK for packet ID 2
    result = mqtt_input(mqtt, puback_packet2, sizeof(puback_packet2));
    printf("    Result: %d\n", result);
    printf("    Publish ACK callback count: %d\n", publish_ack_callback_count);
    
    mqtt_destroy(mqtt);
    
    printf("PUBACK test completed.\n");
}

int main(void)
{
    printf("Starting debug MQTT tests...\n\n");
    
    test_puback_packet();
    
    printf("\nDebug tests completed!\n");
    return 0;
}