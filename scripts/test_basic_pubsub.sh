#!/bin/bash
#
# Basic Pub/Sub Test for MQTT Client
# Quick verification that pub/sub works correctly
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

# Clean up
pkill -9 mqtt_client 2>/dev/null || true
sleep 1

cd "$BUILD_DIR"

echo "========================================="
echo "  MQTT Basic Pub/Sub Test"
echo "========================================="
echo ""

# Start subscriber
echo "Starting subscriber..."
./mqtt_client sub 198.19.249.149 1883 topic/test sender 123456 </dev/null > /tmp/mqtt_basic_sub.log 2>&1 &
SUB_PID=$!
sleep 3

# Start publisher
echo "Starting publisher..."
./mqtt_client pub 198.19.249.149 1883 topic/test "Hello World" 500 sender 123456 </dev/null > /tmp/mqtt_basic_pub.log 2>&1 &
PUB_PID=$!

echo "Running for 10 seconds..."
sleep 10

# Show results
echo ""
echo "========================================="
echo "  Results"
echo "========================================="
echo ""
echo "Publisher (last 10 lines):"
tail -10 /tmp/mqtt_basic_pub.log

echo ""
echo "Subscriber (last 10 lines):"
tail -10 /tmp/mqtt_basic_sub.log

# Cleanup
kill -9 $SUB_PID $PUB_PID 2>/dev/null || true

echo ""
echo "Test complete!"
echo ""
