/**
 * @file test_final.c
 * @brief Final MQTT library tests
 */

#include "mqtt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

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
 * @brief Test all callback types in separate instances
 */
static void test_all_callbacks_separate(void)
{
    printf("Testing all callback types in separate instances...\n");
    
    // Test 1: CONNACK
    printf("\n  Test 1: CONNACK callback\n");
    {
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
            .on_message = NULL,
            .publish_ack = NULL,
            .subscribe_ack = NULL,
            .unsubscribe_ack = NULL
        };
        
        mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
        assert(mqtt != NULL);
        
        connection_callback_count = 0;
        
        uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK
        int result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
        assert(result == sizeof(connack_packet));
        assert(connection_callback_count == 1);
        
        mqtt_destroy(mqtt);
    }
    
    // Test 2: PUBLISH
    printf("\n  Test 2: PUBLISH callback\n");
    {
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
            .on_connection = NULL,
            .on_message = test_message_received_callback,
            .publish_ack = NULL,
            .subscribe_ack = NULL,
            .unsubscribe_ack = NULL
        };
        
        mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
        assert(mqtt != NULL);
        
        message_callback_count = 0;
        
        // First connect
        uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK
        int result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
        assert(result == sizeof(connack_packet));
        
        // Then publish
        uint8_t publish_packet[] = {
            0x30, 0x0F, // PUBLISH header + remaining length
            0x00, 0x07, 't', 'e', 's', 't', '/', 't', 'o', 'p', 'i', 'c', // topic
            'h', 'e', 'l', 'l', 'o' // payload
        };
        result = mqtt_input(mqtt, publish_packet, sizeof(publish_packet));
        assert(result == sizeof(publish_packet));
        assert(message_callback_count == 1);
        
        mqtt_destroy(mqtt);
    }
    
    // Test 3: PUBACK
    printf("\n  Test 3: PUBACK callback\n");
    {
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
            .on_connection = NULL,
            .on_message = NULL,
            .publish_ack = test_publish_ack_callback,
            .subscribe_ack = NULL,
            .unsubscribe_ack = NULL
        };
        
        mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
        assert(mqtt != NULL);
        
        publish_ack_callback_count = 0;
        
        // First connect
        uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK
        int result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
        assert(result == sizeof(connack_packet));
        
        // Then puback
        uint8_t puback_packet[] = {0x40, 0x02, 0x00, 0x01}; // PUBACK for packet ID 1
        result = mqtt_input(mqtt, puback_packet, sizeof(puback_packet));
        assert(result == sizeof(puback_packet));
        assert(publish_ack_callback_count == 1);
        
        mqtt_destroy(mqtt);
    }
    
    // Test 4: SUBACK
    printf("\n  Test 4: SUBACK callback\n");
    {
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
            .on_connection = NULL,
            .on_message = NULL,
            .publish_ack = NULL,
            .subscribe_ack = test_subscribe_ack_callback,
            .unsubscribe_ack = NULL
        };
        
        mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
        assert(mqtt != NULL);
        
        subscribe_ack_callback_count = 0;
        
        // First connect
        uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK
        int result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
        assert(result == sizeof(connack_packet));
        
        // Then suback
        uint8_t suback_packet[] = {0x90, 0x03, 0x00, 0x02, 0x00}; // SUBACK for packet ID 2
        result = mqtt_input(mqtt, suback_packet, sizeof(suback_packet));
        assert(result == sizeof(suback_packet));
        assert(subscribe_ack_callback_count == 1);
        
        mqtt_destroy(mqtt);
    }
    
    // Test 5: UNSUBACK
    printf("\n  Test 5: UNSUBACK callback\n");
    {
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
            .on_connection = NULL,
            .on_message = NULL,
            .publish_ack = NULL,
            .subscribe_ack = NULL,
            .unsubscribe_ack = test_unsubscribe_ack_callback
        };
        
        mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
        assert(mqtt != NULL);
        
        unsubscribe_ack_callback_count = 0;
        
        // First connect
        uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK
        int result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
        assert(result == sizeof(connack_packet));
        
        // Then unsuback
        uint8_t unsuback_packet[] = {0xB0, 0x02, 0x00, 0x03}; // UNSUBACK for packet ID 3
        result = mqtt_input(mqtt, unsuback_packet, sizeof(unsuback_packet));
        assert(result == sizeof(unsuback_packet));
        assert(unsubscribe_ack_callback_count == 1);
        
        mqtt_destroy(mqtt);
    }
    
    printf("All callbacks test passed!\n");
}

/**
 * @brief Stress test thread function
 */
static void *stress_thread(void *arg)
{
    mqtt_t *mqtt = (mqtt_t *)arg;
    
    for (int i = 0; i < 50; i++) {
        // Simulate incoming packets from network
        uint8_t test_packet[] = {0x20, 0x00}; // PINGRESP
        mqtt_input(mqtt, test_packet, sizeof(test_packet));
        
        // Call timer periodically
        mqtt_timer(mqtt, 1000);
        
        // Small delay to allow other threads to run
        usleep(1000);
    }
    
    return NULL;
}

/**
 * @brief Test thread safety with concurrent access
 */
static void test_thread_safety_concurrent_access(void)
{
    printf("\nTesting thread safety with concurrent access...\n");
    
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
    
    // Create multiple threads for concurrent access
    pthread_t threads[3];
    
    printf("  Creating 3 stress threads...\n");
    for (int i = 0; i < 3; i++) {
        int result = pthread_create(&threads[i], NULL, stress_thread, mqtt);
        assert(result == 0);
    }
    
    printf("  Waiting for threads to complete...\n");
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("  Threads completed successfully\n");
    
    // Verify no deadlocks occurred
    printf("  Callback counts:\n");
    printf("    Send: %d\n", test_send_count);
    printf("    Connection: %d\n", connection_callback_count);
    printf("    Message: %d\n", message_callback_count);
    printf("    Publish ACK: %d\n", publish_ack_callback_count);
    printf("    Subscribe ACK: %d\n", subscribe_ack_callback_count);
    printf("    Unsubscribe ACK: %d\n", unsubscribe_ack_callback_count);
    
    mqtt_destroy(mqtt);
    
    printf("Thread safety concurrent access test passed!\n");
}

int main(void)
{
    printf("Starting final MQTT tests...\n\n");
    
    test_all_callbacks_separate();
    test_thread_safety_concurrent_access();
    
    printf("\nFinal tests completed successfully!\n");
    return 0;
}