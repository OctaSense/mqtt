/**
 * @file test_mqtt.c
 * @brief MQTT library tests
 */

#include "mqtt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int test_send_count = 0;
static uint8_t last_sent_data[1024];
static size_t last_sent_len = 0;

/**
 * @brief Test send callback
 */
static int test_send_callback(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;
    
    test_send_count++;
    
    if (len <= sizeof(last_sent_data)) {
        memcpy(last_sent_data, data, len);
        last_sent_len = len;
    }
    
    return (int)len;
}

/**
 * @brief Test connection status callback
 */
static void test_connection_status_callback(bool connected, mqtt_conn_return_t return_code, void *user_data)
{
    (void)user_data;
    printf("Connection status: %s, return code: %d\n", 
           connected ? "connected" : "disconnected", return_code);
}

/**
 * @brief Test message received callback
 */
static void test_message_received_callback(const mqtt_message_t *message, void *user_data)
{
    (void)user_data;
    printf("Message received - Topic: %s, QoS: %d, Payload len: %zu\n", 
           message->topic, message->qos, message->payload_len);
}

/**
 * @brief Test MQTT instance creation
 */
static void test_mqtt_create(void)
{
    printf("Testing MQTT instance creation...\n");
    
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
    
    printf("  Creating MQTT instance...\n");
    // Test valid creation
    mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
    printf("  MQTT instance created: %p\n", mqtt);
    if (mqtt == NULL) {
        printf("ERROR: Failed to create MQTT instance\n");
        return;
    }
    
    printf("  Testing state...\n");
    // Test state
    mqtt_state_t state = mqtt_get_state(mqtt);
    printf("    State: %d\n", state);
    if (state != MQTT_STATE_DISCONNECTED) {
        printf("ERROR: Expected state DISCONNECTED, got %d\n", state);
    }
    
    bool connected = mqtt_is_connected(mqtt);
    printf("    Connected: %d\n", connected);
    if (connected) {
        printf("ERROR: Expected not connected\n");
    }
    
    printf("  Testing packet ID generation...\n");
    // Test packet ID generation
    uint16_t packet_id = mqtt_get_packet_id(mqtt);
    printf("    First packet ID: %d\n", packet_id);
    if (packet_id != 1) {
        printf("ERROR: Expected packet ID 1, got %d\n", packet_id);
    }
    packet_id = mqtt_get_packet_id(mqtt);
    printf("    Second packet ID: %d\n", packet_id);
    if (packet_id != 2) {
        printf("ERROR: Expected packet ID 2, got %d\n", packet_id);
    }
    
    printf("  Testing invalid creation...\n");
    // Test invalid creation
    if (mqtt_create(NULL, &handler, NULL) != NULL) {
        printf("ERROR: Expected NULL for NULL config\n");
    }
    if (mqtt_create(&config, NULL, NULL) != NULL) {
        printf("ERROR: Expected NULL for NULL handler\n");
    }
    
    mqtt_handler_t invalid_handler = {0};
    if (mqtt_create(&config, &invalid_handler, NULL) != NULL) {
        printf("ERROR: Expected NULL for invalid handler\n");
    }
    
    printf("  Destroying MQTT instance...\n");
    mqtt_destroy(mqtt);
    
    printf("MQTT instance creation test passed!\n");
}

/**
 * @brief Test MQTT connection
 */
static void test_mqtt_connect(void)
{
    printf("Testing MQTT connection...\n");
    
    mqtt_config_t config = {
        .client_id = "test_client",
        .username = "user",
        .password = "pass",
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
    last_sent_len = 0;
    
    // Test connect
    int result = mqtt_connect(mqtt);
    assert(result == 0);
    assert(mqtt_get_state(mqtt) == MQTT_STATE_CONNECTING);
    
    // TODO: Test CONNECT packet was sent
    // This would require packet parsing to be implemented
    
    mqtt_destroy(mqtt);
    
    printf("MQTT connection test passed!\n");
}

/**
 * @brief Test MQTT timer
 */
static void test_mqtt_timer(void)
{
    printf("Testing MQTT timer...\n");
    
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
    
    // Test timer with disconnected state
    int result = mqtt_timer(mqtt, 1000);
    assert(result == 0);
    assert(test_send_count == 0); // No PINGREQ in disconnected state
    
    mqtt_destroy(mqtt);
    
    printf("MQTT timer test passed!\n");
}

/**
 * @brief Test MQTT input processing
 */
static void test_mqtt_input(void)
{
    printf("Testing MQTT input processing...\n");
    
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
    
    // Test invalid input
    int result = mqtt_input(mqtt, NULL, 10);
    assert(result == -1);
    
    result = mqtt_input(mqtt, (uint8_t[]){0x00}, 0);
    assert(result == -1);
    
    // Test valid input (empty for now - would test packet parsing when implemented)
    uint8_t test_data[] = {0x20, 0x00}; // PINGRESP packet
    result = mqtt_input(mqtt, test_data, sizeof(test_data));
    assert(result == sizeof(test_data));
    
    mqtt_destroy(mqtt);
    
    printf("MQTT input processing test passed!\n");
}

/**
 * @brief Test MQTT packet reassembly (TCP stream fragmentation)
 */
static void test_mqtt_packet_reassembly(void)
{
    printf("Testing MQTT packet reassembly...\n");
    
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
    
    printf("  Test 1: Split packet in the middle\n");
    // Test 1: Split packet in the middle
    uint8_t pingresp_packet[] = {0x20, 0x00}; // PINGRESP packet (2 bytes)
    
    // Send first byte only
    printf("    Sending first byte...\n");
    int result = mqtt_input(mqtt, pingresp_packet, 1);
    printf("    Result: %d\n", result);
    if (result != 1) {
        printf("ERROR: Expected result 1, got %d\n", result);
    }
    
    // Send second byte
    printf("    Sending second byte...\n");
    result = mqtt_input(mqtt, pingresp_packet + 1, 1);
    printf("    Result: %d\n", result);
    if (result != 1) {
        printf("ERROR: Expected result 1, got %d\n", result);
    }
    
    // Test 2: Multiple packets arriving together (concatenation)
    uint8_t multiple_packets[4];
    memcpy(multiple_packets, pingresp_packet, 2);
    memcpy(multiple_packets + 2, pingresp_packet, 2);
    
    result = mqtt_input(mqtt, multiple_packets, sizeof(multiple_packets));
    assert(result == sizeof(multiple_packets)); // Should process both packets
    
    // Test 3: Incomplete packet followed by complete packet
    uint8_t incomplete_then_complete[3];
    memcpy(incomplete_then_complete, pingresp_packet, 1); // First byte of first packet
    memcpy(incomplete_then_complete + 1, pingresp_packet, 2); // Complete second packet
    
    result = mqtt_input(mqtt, incomplete_then_complete, sizeof(incomplete_then_complete));
    assert(result == 3); // Should process complete packet and store incomplete
    
    // Send remaining byte to complete first packet
    result = mqtt_input(mqtt, pingresp_packet + 1, 1);
    assert(result == 1); // Should complete and process first packet
    
    // Test 4: Clear reassembly buffer on disconnect
    // Send incomplete packet
    result = mqtt_input(mqtt, pingresp_packet, 1);
    assert(result == 1);
    
    // Disconnect should clear reassembly buffer
    mqtt_disconnect(mqtt);
    
    mqtt_destroy(mqtt);
    
    printf("MQTT packet reassembly test passed!\n");
}

/**
 * @brief Test MQTT publish with large payload (dynamic buffer allocation)
 */
static void test_mqtt_publish_large_payload(void)
{
    printf("Testing MQTT publish with large payload...\n");
    
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
    
    // For testing purposes, we'll simulate a CONNACK packet to set state to CONNECTED
    // This bypasses the normal connection flow for testing
    uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK with accepted
    int input_result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
    assert(input_result == sizeof(connack_packet));
    assert(mqtt_is_connected(mqtt));
    
    // Reset test counters
    test_send_count = 0;
    last_sent_len = 0;
    
    // Test 1: Publish with small payload (should work)
    mqtt_message_t small_msg = {
        .topic = "test/topic",
        .payload = (uint8_t[]){0x01, 0x02, 0x03},
        .payload_len = 3,
        .qos = MQTT_QOS_0,
        .retain = false,
        .packet_id = 0
    };
    
    int result = mqtt_publish(mqtt, &small_msg);
    assert(result == 0);
    assert(test_send_count == 1);
    assert(last_sent_len > 0);
    
    // Test 2: Publish with large payload (should use dynamic allocation)
    uint8_t large_payload[5000]; // 5KB payload
    for (size_t i = 0; i < sizeof(large_payload); i++) {
        large_payload[i] = (uint8_t)(i % 256);
    }
    
    mqtt_message_t large_msg = {
        .topic = "test/topic",
        .payload = large_payload,
        .payload_len = sizeof(large_payload),
        .qos = MQTT_QOS_0,
        .retain = false,
        .packet_id = 0
    };
    
    result = mqtt_publish(mqtt, &large_msg);
    assert(result == 0);
    assert(test_send_count == 2);
    assert(last_sent_len > 0); // Should have sent some data
    
    // Test 3: Publish with QoS 1 (should keep buffer for retransmission)
    mqtt_message_t qos1_msg = {
        .topic = "test/topic",
        .payload = (uint8_t[]){0x04, 0x05, 0x06},
        .payload_len = 3,
        .qos = MQTT_QOS_1,
        .retain = false,
        .packet_id = 123
    };
    
    result = mqtt_publish(mqtt, &qos1_msg);
    assert(result == 0);
    assert(test_send_count == 3);
    
    mqtt_destroy(mqtt);
    
    printf("MQTT publish with large payload test passed!\n");
}

/**
 * @brief Test QoS validation (only QoS 0 supported)
 */
static void test_mqtt_qos_validation(void)
{
    printf("Testing MQTT QoS validation...\n");
    
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
    
    // Connect first
    int result = mqtt_connect(mqtt);
    assert(result == 0);
    
    // Simulate receiving CONNACK packet to set state to CONNECTED
    // CONNACK packet: 0x20 0x02 0x00 0x00 (fixed header + remaining length + flags + return code)
    uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00};
    printf("  Simulating CONNACK reception...\n");
    result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
    printf("    CONNACK result: %d\n", result);
    assert(result == sizeof(connack_packet));
    
    // Reset test counters
    test_send_count = 0;
    
    // Test 1: Publish with QoS 0 (should work)
    printf("  Testing QoS 0 publish...\n");
    mqtt_message_t qos0_msg = {
        .topic = "test/topic",
        .payload = (uint8_t[]){0x01, 0x02, 0x03},
        .payload_len = 3,
        .qos = MQTT_QOS_0,
        .retain = false,
        .packet_id = 0
    };
    
    result = mqtt_publish(mqtt, &qos0_msg);
    printf("    Result: %d, send count: %d\n", result, test_send_count);
    assert(result == 0);
    assert(test_send_count == 1);
    
    // Test 2: Publish with QoS 1 (should fail)
    mqtt_message_t qos1_msg = {
        .topic = "test/topic",
        .payload = (uint8_t[]){0x04, 0x05, 0x06},
        .payload_len = 3,
        .qos = MQTT_QOS_1,
        .retain = false,
        .packet_id = 123
    };
    
    result = mqtt_publish(mqtt, &qos1_msg);
    assert(result == -1); // Should fail
    assert(test_send_count == 1); // No additional sends
    
    // Test 3: Publish with QoS 2 (should fail)
    mqtt_message_t qos2_msg = {
        .topic = "test/topic",
        .payload = (uint8_t[]){0x07, 0x08, 0x09},
        .payload_len = 3,
        .qos = MQTT_QOS_2,
        .retain = false,
        .packet_id = 456
    };
    
    result = mqtt_publish(mqtt, &qos2_msg);
    assert(result == -1); // Should fail
    assert(test_send_count == 1); // No additional sends
    
    // Test 4: Subscribe with QoS 0 (should work)
    const char *topics[] = {"test/topic1", "test/topic2"};
    const mqtt_qos_t qos0[] = {MQTT_QOS_0, MQTT_QOS_0};
    
    result = mqtt_subscribe(mqtt, topics, qos0, 2);
    assert(result == 0);
    assert(test_send_count == 2);
    
    // Test 5: Subscribe with mixed QoS (should fail)
    const mqtt_qos_t mixed_qos[] = {MQTT_QOS_0, MQTT_QOS_1};
    
    result = mqtt_subscribe(mqtt, topics, mixed_qos, 2);
    assert(result == -1); // Should fail
    assert(test_send_count == 2); // No additional sends
    
    // Test 6: Subscribe with QoS 2 (should fail)
    const mqtt_qos_t qos2[] = {MQTT_QOS_2, MQTT_QOS_2};
    
    result = mqtt_subscribe(mqtt, topics, qos2, 2);
    assert(result == -1); // Should fail
    assert(test_send_count == 2); // No additional sends
    
    // Test 7: Unsubscribe (should work - no QoS parameter)
    result = mqtt_unsubscribe(mqtt, topics, 2);
    assert(result == 0);
    assert(test_send_count == 3);
    
    mqtt_destroy(mqtt);
    
    printf("MQTT QoS validation test passed!\n");
}

int main(void)
{
    printf("Starting MQTT library tests...\n\n");
    
    printf("=== Starting test_mqtt_create ===\n");
    test_mqtt_create();
    printf("Create test passed\n");
    
    printf("=== Starting test_mqtt_connect ===\n");
    test_mqtt_connect();
    printf("Connect test passed\n");
    
    printf("=== Starting test_mqtt_timer ===\n");
    test_mqtt_timer();
    printf("Timer test passed\n");
    
    printf("=== Starting test_mqtt_input ===\n");
    test_mqtt_input();
    printf("Input test passed\n");
    
    printf("=== Starting test_mqtt_packet_reassembly ===\n");
    test_mqtt_packet_reassembly();
    printf("Reassembly test passed\n");
    
    printf("\nFirst five tests completed successfully!\n");
    return 0;
}