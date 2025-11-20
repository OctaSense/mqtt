#!/bin/bash
#
# Progressive Size Test for MQTT Client
# Tests message sizes from 1 byte to 2MB, doubling each time
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"
LOG_DIR="/tmp/mqtt_size_test"

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
echo "[1/3] Starting subscriber..."
./mqtt_client sub 198.19.249.149 1883 topic/sizetest sender 123456 </dev/null > "$LOG_DIR/subscriber.log" 2>&1 &
SUB_PID=$!
echo "      Subscriber PID: $SUB_PID"
sleep 3

if ! ps -p $SUB_PID > /dev/null 2>&1; then
    echo "      ERROR: Subscriber failed to start"
    cat "$LOG_DIR/subscriber.log"
    exit 1
fi

echo "      ✓ Subscriber ready"
echo ""

# Test progressive sizes
echo "[2/3] Testing message sizes..."
echo ""

size=1
max_size=2097152  # 2MB
count=0
failed=0

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

    printf "  [%2d] %8s (%10d bytes) ... " $count "$size_str" $size

    # Generate message and send via stdin to avoid ARG_MAX limit
    # Note: Don't redirect stdin with </dev/null as it would override the pipe
    head -c $size < /dev/zero | tr '\0' 'B' | \
        ./mqtt_client pub 198.19.249.149 1883 topic/sizetest - 100 sender 123456 \
        > "$LOG_DIR/pub_${size}.log" 2>&1 &
    PUB_PID=$!

    # Wait for publish (longer for larger messages)
    # Increased wait times to ensure publisher has time to connect AND send messages
    # Publishers need ~3s to connect + time to publish at least once
    if [ $size -lt 10240 ]; then
        sleep 2
    elif [ $size -lt 102400 ]; then
        sleep 4
    else
        sleep 8
    fi

    # Stop publisher
    kill -9 $PUB_PID 2>/dev/null || true
    wait $PUB_PID 2>/dev/null || true

    # Check if published
    if grep -q "Published:" "$LOG_DIR/pub_${size}.log"; then
        echo "✓"
    else
        echo "✗"
        failed=$((failed + 1))
        if [ $failed -ge 3 ]; then
            echo ""
            echo "Too many failures, stopping test"
            break
        fi
    fi

    size=$((size * 2))
    count=$((count + 1))
done

echo ""

# Check results
echo "[3/3] Verifying results..."
sleep 2

if ps -p $SUB_PID > /dev/null 2>&1; then
    recv_count=$(grep -c "Received.*bytes:" "$LOG_DIR/subscriber.log" || echo "0")
    echo "      Messages sent: $count"
    echo "      Messages received: $recv_count"
    
    if [ $recv_count -eq $count ]; then
        echo "      ✓ All messages received!"
    elif [ $recv_count -gt 0 ]; then
        echo "      ⚠ Partial success ($recv_count/$count)"
    else
        echo "      ✗ No messages received"
    fi
else
    echo "      ✗ Subscriber exited unexpectedly"
fi

# Cleanup
kill -9 $SUB_PID 2>/dev/null || true
sleep 1

echo ""
echo "========================================="
echo "  Test Complete"
echo "========================================="
echo ""
echo "Logs saved in: $LOG_DIR"
echo ""
echo "View subscriber log: cat $LOG_DIR/subscriber.log"
echo "View specific size: cat $LOG_DIR/pub_<size>.log"
echo ""
