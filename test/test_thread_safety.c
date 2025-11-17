/**
 * @file test_thread_safety.c
 * @brief Thread safety tests for MQTT library
 */

#include "mqtt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_THREADS 5
#define ITERATIONS_PER_THREAD 100

static volatile int test_send_count = 0;
static volatile int connection_callback_count = 0;
static volatile int message_callback_count = 0;
static volatile int publish_ack_callback_count = 0;
static volatile int subscribe_ack_callback_count = 0;
static volatile int unsubscribe_ack_callback_count = 0;

static pthread_mutex_t callback_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Thread-safe send callback
 */
static int test_send_callback(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;
    (void)data;
    (void)len;
    
    __sync_fetch_and_add(&test_send_count, 1);
    return (int)len;
}

/**
 * @brief Thread-safe connection status callback
 */
static void test_connection_status_callback(bool connected, mqtt_conn_return_t return_code, void *user_data)
{
    (void)user_data;
    (void)connected;
    (void)return_code;
    
    __sync_fetch_and_add(&connection_callback_count, 1);
}

/**
 * @brief Thread-safe message received callback
 */
static void test_message_received_callback(const mqtt_message_t *message, void *user_data)
{
    (void)user_data;
    (void)message;
    
    __sync_fetch_and_add(&message_callback_count, 1);
}

/**
 * @brief Thread-safe publish acknowledgment callback
 */
static void test_publish_ack_callback(uint16_t packet_id, void *user_data)
{
    (void)user_data;
    (void)packet_id;
    
    __sync_fetch_and_add(&publish_ack_callback_count, 1);
}

/**
 * @brief Thread-safe subscribe acknowledgment callback
 */
static void test_subscribe_ack_callback(uint16_t packet_id, const mqtt_qos_t *return_codes, size_t count, void *user_data)
{
    (void)user_data;
    (void)packet_id;
    (void)return_codes;
    (void)count;
    
    __sync_fetch_and_add(&subscribe_ack_callback_count, 1);
}

/**
 * @brief Thread-safe unsubscribe acknowledgment callback
 */
static void test_unsubscribe_ack_callback(uint16_t packet_id, void *user_data)
{
    (void)user_data;
    (void)packet_id;
    
    __sync_fetch_and_add(&unsubscribe_ack_callback_count, 1);
}

/**
 * @brief Stress test thread function
 */
static void *stress_thread(void *arg)
{
    mqtt_t *mqtt = (mqtt_t *)arg;
    
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        // Simulate incoming packets from network
        uint8_t test_packet[] = {0x20, 0x00}; // PINGRESP
        mqtt_input(mqtt, test_packet, sizeof(test_packet));
        
        // Call timer periodically
        mqtt_timer(mqtt, 1000);
        
        // Small delay to allow other threads to run
        usleep(100);
    }
    
    return NULL;
}

/**
 * @brief Test thread safety with concurrent access
 */
static void test_thread_safety_concurrent_access(void)
{
    printf("Testing thread safety with concurrent access...\n");
    
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
    pthread_t threads[NUM_THREADS];
    
    printf("  Creating %d stress threads...\n", NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; i++) {
        int result = pthread_create(&threads[i], NULL, stress_thread, mqtt);
        assert(result == 0);
    }
    
    printf("  Waiting for threads to complete...\n");
    for (int i = 0; i < NUM_THREADS; i++) {
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

/**
 * @brief Test callback execution in unlocked state
 */
static void test_callback_unlocked_state(void)
{
    printf("Testing callback execution in unlocked state...\n");
    
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
    
    // Test various packet types that trigger callbacks
    
    // 1. CONNACK packet (triggers connection callback)
    uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK
    int result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
    assert(result == sizeof(connack_packet));
    
    // 2. PUBLISH packet (triggers message callback)
    uint8_t publish_packet[] = {
        0x30, 0x0F, // PUBLISH header + remaining length
        0x00, 0x07, 't', 'e', 's', 't', '/', 't', 'o', 'p', 'i', 'c', // topic
        'h', 'e', 'l', 'l', 'o' // payload
    };
    result = mqtt_input(mqtt, publish_packet, sizeof(publish_packet));
    assert(result == sizeof(publish_packet));
    
    // 3. PUBACK packet (triggers publish_ack callback)
    uint8_t puback_packet[] = {0x40, 0x02, 0x00, 0x01}; // PUBACK for packet ID 1
    result = mqtt_input(mqtt, puback_packet, sizeof(puback_packet));
    assert(result == sizeof(puback_packet));
    
    // 4. SUBACK packet (triggers subscribe_ack callback)
    uint8_t suback_packet[] = {0x90, 0x03, 0x00, 0x02, 0x00}; // SUBACK for packet ID 2
    result = mqtt_input(mqtt, suback_packet, sizeof(suback_packet));
    assert(result == sizeof(suback_packet));
    
    // 5. UNSUBACK packet (triggers unsubscribe_ack callback)
    uint8_t unsuback_packet[] = {0xB0, 0x02, 0x00, 0x03}; // UNSUBACK for packet ID 3
    result = mqtt_input(mqtt, unsuback_packet, sizeof(unsuback_packet));
    assert(result == sizeof(unsuback_packet));
    
    printf("  Callback counts after packet processing:\n");
    printf("    Send: %d\n", test_send_count);
    printf("    Connection: %d\n", connection_callback_count);
    printf("    Message: %d\n", message_callback_count);
    printf("    Publish ACK: %d\n", publish_ack_callback_count);
    printf("    Subscribe ACK: %d\n", subscribe_ack_callback_count);
    printf("    Unsubscribe ACK: %d\n", unsubscribe_ack_callback_count);
    
    // Verify callbacks were executed
    assert(connection_callback_count > 0);
    assert(message_callback_count > 0);
    assert(publish_ack_callback_count > 0);
    assert(subscribe_ack_callback_count > 0);
    assert(unsubscribe_ack_callback_count > 0);
    
    mqtt_destroy(mqtt);
    
    printf("Callback unlocked state test passed!\n");
}

/**
 * @brief Test timer keep-alive thread safety
 */
static void test_timer_keep_alive_safety(void)
{
    printf("Testing timer keep-alive thread safety...\n");
    
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
        .publish_ack = test_publish_ack_callback,
        .subscribe_ack = test_subscribe_ack_callback,
        .unsubscribe_ack = test_unsubscribe_ack_callback
    };
    
    mqtt_t *mqtt = mqtt_create(&config, &handler, NULL);
    assert(mqtt != NULL);
    
    // Reset test counters
    test_send_count = 0;
    
    // Connect first to enable keep-alive
    int result = mqtt_connect(mqtt);
    assert(result == 0);
    
    // Simulate receiving CONNACK to set state to CONNECTED
    uint8_t connack_packet[] = {0x20, 0x02, 0x00, 0x00};
    result = mqtt_input(mqtt, connack_packet, sizeof(connack_packet));
    assert(result == sizeof(connack_packet));
    assert(mqtt_is_connected(mqtt));
    
    // Call timer multiple times to trigger keep-alive
    int initial_send_count = test_send_count;
    
    // First call should not send PINGREQ immediately
    mqtt_timer(mqtt, 500);
    assert(test_send_count == initial_send_count);
    
    // After keep-alive interval, should send PINGREQ
    mqtt_timer(mqtt, 1500);
    assert(test_send_count == initial_send_count + 1);
    
    printf("  Send count after timer calls: %d\n", test_send_count);
    
    mqtt_destroy(mqtt);
    
    printf("Timer keep-alive safety test passed!\n");
}

int main(void)
{
    printf("Starting MQTT thread safety tests...\n\n");
    
    printf("=== Starting test_callback_unlocked_state ===\n");
    test_callback_unlocked_state();
    
    printf("\n=== Starting test_timer_keep_alive_safety ===\n");
    test_timer_keep_alive_safety();
    
    printf("\n=== Starting test_thread_safety_concurrent_access ===\n");
    test_thread_safety_concurrent_access();
    
    printf("\nAll thread safety tests completed successfully!\n");
    return 0;
}