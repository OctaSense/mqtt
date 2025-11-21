#!/bin/bash
#
# MQTT Progressive Size Test - Subscriber and Publisher
# Tests message sizes from 1 byte to 2MB, doubling each time
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"
LOG_DIR="/tmp/mqtt_test"

# Configuration
BROKER="198.19.249.149"
PORT="1883"
TOPIC="topic/sizetest"
USERNAME="sender"
PASSWORD="123456"
INTERVAL=2000  # 2 seconds between publishes

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    pkill -9 mqtt_client 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

# Clean up any existing processes
pkill -9 mqtt_client 2>/dev/null || true
sleep 1

# Create log directory
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*.log 2>/dev/null || true

cd "$BUILD_DIR"

echo "========================================="
echo "  MQTT Progressive Size Test"
echo "  Testing from 1 byte to 2MB"
echo "========================================="
echo ""

# Start subscriber
echo "[1/2] Starting subscriber..."
./mqtt_client sub "$BROKER" "$PORT" "$TOPIC" "$USERNAME" "$PASSWORD" \
    > "$LOG_DIR/subscriber.log" 2>&1 &
SUB_PID=$!
echo "      Subscriber PID: $SUB_PID"

# Wait for subscriber to be ready
sleep 3

if ! ps -p $SUB_PID > /dev/null 2>&1; then
    echo "      ERROR: Subscriber failed to start"
    cat "$LOG_DIR/subscriber.log"
    exit 1
fi

echo "      ✓ Subscriber ready"
echo ""

# Start publisher
echo "[2/2] Starting publisher with progressive sizes..."
echo ""

size=1
max_size=2097152  # 2MB
count=0

while [ $size -le $max_size ]; do
    # Display size in human-readable format
    if [ $size -lt 1024 ]; then
        size_str="${size}B"
    elif [ $size -lt 1048576 ]; then
        size_kb=$((size / 1024))
        size_str="${size_kb}KB"
    else
        size_mb=$((size / 1048576))
        size_str="${size_mb}MB"
    fi

    printf "  [%2d] Publishing %8s (%10d bytes) ... " $count "$size_str" $size

    # Generate and publish message via stdin
    head -c $size < /dev/zero | tr '\0' 'X' | \
        ./mqtt_client pub "$BROKER" "$PORT" "$TOPIC" - "$INTERVAL" "$USERNAME" "$PASSWORD" \
        > "$LOG_DIR/pub_${size}.log" 2>&1 &
    PUB_PID=$!

    # Wait 10 seconds: 5s for connection + 2s interval + 2s publishing + 1s buffer
    sleep 10

    # Stop publisher
    kill -9 $PUB_PID 2>/dev/null || true
    wait $PUB_PID 2>/dev/null || true

    # Check if published successfully
    if grep -q "Published:" "$LOG_DIR/pub_${size}.log"; then
        echo "✓"
    else
        echo "✗"
        echo "      Failed to publish, check log: $LOG_DIR/pub_${size}.log"
    fi

    size=$((size * 2))
    count=$((count + 1))
done

echo ""
echo "Publishing complete. Waiting 3 seconds for final messages..."
sleep 3

# Check results
echo ""
echo "Checking results..."
if ps -p $SUB_PID > /dev/null 2>&1; then
    recv_count=$(grep -c "Received.*bytes:" "$LOG_DIR/subscriber.log" 2>/dev/null || echo "0")
    echo "  Messages published: $count"
    echo "  Messages received: $recv_count"
    echo ""

    if [ "$recv_count" = "$count" ]; then
        echo "  ✓ SUCCESS: All messages received!"
    elif [ $recv_count -gt 0 ]; then
        echo "  ⚠ WARNING: Partial success ($recv_count/$count)"
    else
        echo "  ✗ FAILURE: No messages received"
    fi
else
    echo "  ✗ ERROR: Subscriber exited unexpectedly"
    cat "$LOG_DIR/subscriber.log"
fi

echo ""
echo "========================================="
echo "  Test Complete"
echo "========================================="
echo ""
echo "Logs saved in: $LOG_DIR"
echo "  - Subscriber: $LOG_DIR/subscriber.log"
echo "  - Publishers: $LOG_DIR/pub_*.log"
echo ""
