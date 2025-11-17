/**
 * @file test_comprehensive.c
 * @brief Comprehensive MQTT library tests
 */

#include "mqtt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

static int test_send_count = 0;
static int connection_callback_count = 0;
static int message_callback_count = 0;
static int publish_ack_callback_count = 0;
static int subscribe_ack_callback_count = 0;
static int unsubscribe_ack_callback_count = 0;

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
 * @brief Test publish acknowledgment callback
 */
static void test_publish_ack_callback(uint16_t packet_id, void *user_data)
{
    (void)user_data;
    printf("Publish ACK callback - Packet ID: %d\n", packet_id);
    publish_ack_callback_count++;
}

/**
 * @brief Test subscribe acknowledgment callback
 */
static void test_subscribe_ack_callback(uint16_t packet_id, const mqtt_qos_t *return_codes, size_t count, void *user_data)
{
    (void)user_data;
    printf("Subscribe ACK callback - Packet ID: %d, Count: %zu\n", packet_id, count);
    subscribe_ack_callback_count++;
}

/**
 * @brief Test unsubscribe acknowledgment callback
 */
static void test_unsubscribe_ack_callback(uint16_t packet_id, void *user_data)
{
    (void)user_data;
    printf("Unsubscribe ACK callback - Packet ID: %d\n", packet_id);
    unsubscribe_ack_callback_count++;
}

/**
 * @brief Test all callback types
 */
static void test_all_callbacks(void)
{
    printf("Testing all callback types...\n");
    
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
        .subscribe_ack = test_subscribe_ack_callback,
        .unsubscribe_ack = test_unsubscribe_ack_callback
    };
    
    mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
    assert(mqtt != NULL);
    
    // Reset test counters
    test_send_count = 0;
    connection_callback_count = 0;
    message_callback_count = 0;
    publish_ack_callback_count = 0;
    subscribe_ack_callback_count = 0;
    unsubscribe_ack_callback_count = 0;
    
    printf("  Initial state: %d\n", mqtt_get_state(mqtt));
    
    // Test CONNACK packet
    printf("  Testing CONNACK packet...\n");
    uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK
    int result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
    printf("    Result: %d, State: %d\n", result, mqtt_get_state(mqtt));
    assert(result == sizeof(connack_packet));
    
    printf("    Connection callback count: %d\n", connection_callback_count);
    assert(connection_callback_count == 1);
    
    // Test PUBLISH packet
    printf("  Testing PUBLISH packet...\n");
    uint8_t publish_packet[] = {
        0x30, 0x11, // PUBLISH header + remaining length (17 bytes)
        0x00, 0x0A, 't', 'e', 's', 't', '/', 't', 'o', 'p', 'i', 'c', // topic (10 bytes)
        'h', 'e', 'l', 'l', 'o' // payload (5 bytes)
    };
    result = mqtt_input(mqtt, publish_packet, sizeof(publish_packet));
    printf("    Result: %d\n", result);
    assert(result == sizeof(publish_packet));
    
    printf("    Message callback count: %d\n", message_callback_count);
    assert(message_callback_count == 1);
    
    // Test PUBACK packet
    printf("  Testing PUBACK packet...\n");
    uint8_t puback_packet[] = {0x40, 0x02, 0x00, 0x01}; // PUBACK for packet ID 1

    result = mqtt_input(mqtt, puback_packet, sizeof(puback_packet));
    printf("    Result: %d\n", result);
    assert(result == sizeof(puback_packet));
    
    printf("    Publish ACK callback count: %d\n", publish_ack_callback_count);
    assert(publish_ack_callback_count == 1);
    
    // Test SUBACK packet
    printf("  Testing SUBACK packet...\n");
    uint8_t suback_packet[] = {0x90, 0x03, 0x00, 0x02, 0x00}; // SUBACK for packet ID 2
    result = mqtt_input(mqtt, suback_packet, sizeof(suback_packet));
    printf("    Result: %d\n", result);
    assert(result == sizeof(suback_packet));
    
    printf("    Subscribe ACK callback count: %d\n", subscribe_ack_callback_count);
    assert(subscribe_ack_callback_count == 1);
    
    // Test UNSUBACK packet
    printf("  Testing UNSUBACK packet...\n");
    uint8_t unsuback_packet[] = {0xB0, 0x02, 0x00, 0x03}; // UNSUBACK for packet ID 3
    result = mqtt_input(mqtt, unsuback_packet, sizeof(unsuback_packet));
    printf("    Result: %d\n", result);
    assert(result == sizeof(unsuback_packet));
    
    printf("    Unsubscribe ACK callback count: %d\n", unsubscribe_ack_callback_count);
    assert(unsubscribe_ack_callback_count == 1);
    
    // Test PINGRESP packet
    printf("  Testing PINGRESP packet...\n");
    uint8_t pingresp_packet[] = {0xD0, 0x00}; // PINGRESP
    result = mqtt_input(mqtt, pingresp_packet, sizeof(pingresp_packet));
    printf("    Result: %d\n", result);
    assert(result == sizeof(pingresp_packet));
    
    mqtt_destroy(mqtt);
    
    printf("All callbacks test passed!\n");
}

/**
 * @brief Test timer functionality
 */
static void test_timer_functionality(void)
{
    printf("Testing timer functionality...\n");
    
    mqtt_config_t config = {
        .client_id = "test_client",
        .username = NULL,
        .password = NULL,
        .keep_alive = 1, // Short keep-alive for testing
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
    
    // Connect first
    int result = mqtt_connect(mqtt);
    assert(result == 0);
    
    // Simulate receiving CONNACK to set state to CONNECTED
    uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00};
    result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
    assert(result == sizeof(connack_packet));
    assert(mqtt_is_connected(mqtt));
    
    // Test timer with connected state
    printf("  Testing timer in connected state...\n");
    int initial_send_count = test_send_count;
    
    // First call should not send PINGREQ immediately
    mqtt_timer(mqtt, 500);
    printf("    Send count after 500ms: %d\n", test_send_count);
    assert(test_send_count == initial_send_count);
    
    // After keep-alive interval, should send PINGREQ
    mqtt_timer(mqtt, 1500);
    printf("    Send count after 1500ms: %d\n", test_send_count);
    assert(test_send_count == initial_send_count + 1);
    
    mqtt_destroy(mqtt);
    
    printf("Timer functionality test passed!\n");
}

int main(void)
{
    printf("Starting comprehensive MQTT tests...\n\n");
    
    test_all_callbacks();
    printf("\n");
    test_timer_functionality();
    
    printf("\nComprehensive tests completed successfully!\n");
    return 0;
}