# MQTT C Library

A lightweight MQTT client library written in C, focusing on protocol implementation without transport layer dependencies.

## Features

- ✅ MQTT 3.1.1 protocol support
- ✅ QoS 0, 1, 2 support
- ✅ Clean session support
- ✅ Keep-alive and heartbeat
- ✅ Packet retransmission
- ✅ Connection status callbacks
- ✅ Message reception callbacks
- ✅ Publish/subscribe/unsubscribe
- ✅ Transport layer agnostic
- ✅ Timer-driven operation
- ✅ Thread-safe implementation

## Architecture Overview

### Core Components

1. **MQTT Instance (mqtt_t)**
   - Manages connection state and session data
   - Contains handler functions for callbacks
   - Handles packet sequencing and flow control

2. **Packet Processing**
   - MQTT packet serialization/deserialization
   - Fixed header and variable header handling
   - Payload management

3. **Timer Management**
   - Keep-alive timer for heartbeat
   - Packet timeout handling
   - Retransmission logic

4. **Callback Interface**
   - Connection status callbacks
   - Message receive callbacks
   - Publish acknowledgment callbacks

### Key Interfaces

- **mqtt_input()**: Feed data from transport layer to MQTT layer
- **mqtt_timer()**: Drive timeout and heartbeat operations
- **mqtt_send()**: Send data through transport layer (callback)
- **mqtt_connect()**: Initiate connection
- **mqtt_publish()**: Publish messages
- **mqtt_subscribe()**: Subscribe to topics
- **mqtt_unsubscribe()**: Unsubscribe from topics

## Building

```bash
mkdir build && cd build
cmake ..
make
```

This will build:
- `libmqtt.a`: The MQTT library
- `mqtt_client`: Command line client
- `test_mqtt`: Unit tests

## Directory Structure

```
mqtt/
├── include/
│   ├── mqtt.h
│   └── trans.h
├── src/
│   ├── mqtt.c
│   ├── mqtt_packet.c
│   └── mqtt_helper.c
├── cmd/
│   ├── main.c
│   └── trans.c
├── test/
│   ├── test_mqtt.c
│   └── test_comprehensive.c
├── CMakeLists.txt
└── README.md
```

## Command Line Client Usage

```bash
# Basic usage
./mqtt_client <host> <port> <client_id> [username] [password]

# Examples
./mqtt_client localhost 1883 my_client
./mqtt_client test.mosquitto.org 1883 my_client
./mqtt_client broker.hivemq.com 1883 my_client user pass
```

### Available Commands

Once connected, you can use these commands:

- `subscribe <topic>` - Subscribe to a topic
- `publish <topic> <message>` - Publish a message
- `quit` or `exit` - Exit the program
- `help` - Show available commands

### Example Session

```bash
$ ./mqtt_client test.mosquitto.org 1883 my_client
Connected to test.mosquitto.org:1883
Connected to MQTT broker
MQTT client started. Type 'help' for commands.

> subscribe test/topic
Subscribing to topic: test/topic

> publish test/topic "Hello MQTT!"
Publishing to topic: test/topic, message: Hello MQTT!

> quit
Exiting...
MQTT client stopped
```

## Library Integration

### Basic Usage

```c
#include "mqtt.h"

// Configuration
mqtt_config_t config = {
    .client_id = "my_client",
    .username = NULL,
    .password = NULL,
    .keep_alive = 60,
    .clean_session = true,
    .packet_timeout = 5000,
    .max_retry_count = 3
};

// Handlers
mqtt_handler_t handler = {
    .send = my_send_callback,
    .on_connection = my_connection_callback,
    .on_message = my_message_callback,
    .publish_ack = my_publish_ack_callback,
    .subscribe_ack = my_subscribe_ack_callback,
    .unsubscribe_ack = my_unsubscribe_ack_callback
};

// Create MQTT instance
mqtt_t *mqtt = mqtt_create(&config, &handler, user_data);

// Connect
mqtt_connect(mqtt);

// Main loop
while (running) {
    // Read from transport layer and feed to MQTT
    mqtt_input(mqtt, data, len);
    
    // Drive timer operations
    mqtt_timer(mqtt, elapsed_ms);
    
    // Publish messages
    mqtt_message_t message = {
        .topic = "test/topic",
        .payload = (uint8_t*)"Hello",
        .payload_len = 5,
        .qos = MQTT_QOS_0,
        .retain = false
    };
    mqtt_publish(mqtt, &message);
    
    // Subscribe to topics
    const char *topics[] = {"test/topic"};
    mqtt_qos_t qos[] = {MQTT_QOS_0};
    mqtt_subscribe(mqtt, topics, qos, 1);
}

// Cleanup
mqtt_destroy(mqtt);
```

### Callback Functions

```c
// Send data through transport layer
int my_send_callback(const uint8_t *data, size_t len, void *user_data) {
    return send(socket_fd, data, len, 0);
}

// Connection status
void my_connection_callback(bool connected, mqtt_conn_return_t return_code, void *user_data) {
    printf("Connection: %s, code: %d\n", connected ? "connected" : "disconnected", return_code);
}

// Message received
void my_message_callback(const mqtt_message_t *message, void *user_data) {
    printf("Message: %s - %.*s\n", message->topic, (int)message->payload_len, message->payload);
}
```

## Testing

Run the unit tests:

```bash
./test_mqtt
```

Test the command line client with public MQTT brokers:

```bash
# Test with public broker
./mqtt_client test.mosquitto.org 1883 test_client

# Subscribe and publish in separate terminals
# Terminal 1: Subscribe
> subscribe test/topic

# Terminal 2: Publish  
> publish test/topic "Hello World"
```

## Architecture Details

### Thread Safety

The library implements thread-safe operations using spinlocks:

```c
// Spinlock macros for thread-safe operations
#define LOCK(mqtt) do { \
    while (__sync_lock_test_and_set(&(mqtt)->lock, 1)) { \
        /* Spin until lock acquired */ \
    } \
} while(0)

#define UNLOCK(mqtt) do { \
    __sync_lock_release(&(mqtt)->lock); \
} while(0)
```

### Transport Layer Abstraction

The library separates the MQTT protocol from the transport layer:

- **Protocol Layer**: Pure MQTT implementation in `src/`
- **Transport Layer**: TCP socket management in `cmd/trans.c`
- **Application Layer**: Command line client in `cmd/main.c`

This allows easy integration with different transport mechanisms (TCP, TLS, WebSocket, etc.).

## Limitations

- Currently implements basic packet structure
- Full packet parsing and state machine implemented
- No TLS support (transport layer responsibility)
- Thread-safe with immediate packet processing

## Package Distribution

The project can be packaged as a clean tar.gz file:

```bash
tar -xzf mqtt-library-clean.tar.gz
cd mqtt
mkdir build && cd build
cmake ..
make
```