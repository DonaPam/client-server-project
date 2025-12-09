#!/bin/bash

# Test suite for client-server application
# Configuration
PORT_BASE=15000
SERVER_IP="127.0.0.1"
LOG_DIR="tests/logs"
DATA_DIR="tests/data"
SCRIPTS_DIR="tests/scripts"

# Create log directory
mkdir -p "$LOG_DIR"

# Function to start server
start_server() {
    local port=$1
    local log_file="$LOG_DIR/server_$port.log"
    
    echo "Starting server on port $port..."
    ./server $port > "$log_file" 2>&1 &
    SERVER_PID=$!
    
    # Wait for server to be ready
    sleep 2
    
    # Verify server is running
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "❌ Server failed to start"
        cat "$log_file"
        return 1
    fi
    
    echo "✅ Server started (PID: $SERVER_PID)"
    return 0
}

# Function to stop server
stop_server() {
    local pid=$1
    echo "Stopping server (PID: $pid)..."
    kill $pid 2>/dev/null
    wait $pid 2>/dev/null
    echo "✅ Server stopped"
}

# Function to run a test
run_test() {
    local test_name=$1
    local protocol=$2
    local graph_file=$3
    local port=$4
    
    local log_file="$LOG_DIR/${test_name}.log"
    
    echo -e "\n🔧 Test: $test_name"
    echo "   Protocol: $protocol"
    echo "   Graph: $(basename $graph_file)"
    
    # Check if graph file exists
    if [ ! -f "$graph_file" ]; then
        echo "   ❌ ERROR: Graph file not found: $graph_file"
        return 1
    fi
    
    # Run test with expect
    if expect "$SCRIPTS_DIR/test_client.exp" "$SERVER_IP" "$protocol" "$port" "$graph_file" "$test_name" > "$log_file" 2>&1; then
        echo "   ✅ SUCCESS"
        # Show brief result
        if [ -f "$log_file" ]; then
            grep -E "(RESULT FOUND|Path length:|Path:|✅|❌)" "$log_file" | tail -5
        fi
        return 0
    else
        echo "   ❌ FAILED"
        echo "   See logs: $log_file"
        # Show last 5 lines of error
        tail -10 "$log_file" | sed 's/^/      /'
        return 1
    fi
}

# Function to monitor network traffic
monitor_network() {
    local port=$1
    local protocol=$2
    local log_file="$LOG_DIR/network_${protocol}_${port}.pcap"
    
    echo "   📡 Capturing network traffic on port $port ($protocol)..."
    tcpdump -i lo -n "port $port" -w "$log_file" 2>/dev/null &
    TCPDUMP_PID=$!
    sleep 1
}

stop_network_monitor() {
    kill $TCPDUMP_PID 2>/dev/null 2>/dev/null
}

# Function to verify test environment
verify_environment() {
    echo "Verifying test environment..."
    
    # Check if programs exist
    if [ ! -f "./client" ]; then
        echo "❌ ERROR: client executable not found"
        return 1
    fi
    
    if [ ! -f "./server" ]; then
        echo "❌ ERROR: server executable not found"
        return 1
    fi
    
    # Check if test data exists
    if [ ! -d "$DATA_DIR" ]; then
        echo "❌ ERROR: Test data directory not found: $DATA_DIR"
        return 1
    fi
    
    # Check if expect is installed
    if ! command -v expect >/dev/null 2>&1; then
        echo "❌ ERROR: expect command not found"
        return 1
    fi
    
    # Check if tcpdump is installed
    if ! command -v tcpdump >/dev/null 2>&1; then
        echo "⚠️  WARNING: tcpdump not found, network capture disabled"
    fi
    
    echo "✅ Environment verification complete"
    return 0
}

# ============================
# MAIN TEST EXECUTION
# ============================


echo "CLIENT-SERVER APPLICATION TEST SUITE"


# Verify environment
verify_environment || exit 1

# ============================
# TCP TESTS
# ============================

echo "TCP TESTS"


TCP_PORT=$((PORT_BASE + 1))
if start_server $TCP_PORT; then
    TCP_SERVER_PID=$SERVER_PID
    
    # Test 1: Simple graph
    monitor_network $TCP_PORT "TCP"
    run_test "TCP_simple" "TCP" "$DATA_DIR/graph_simple.txt" $TCP_PORT
    stop_network_monitor
    
    # Test 2: Medium graph
    sleep 1
    run_test "TCP_medium" "TCP" "$DATA_DIR/graph_medium.txt" $TCP_PORT
    
    # Test 3: Error validation
    echo -e "\n🔧 Test: TCP_error_validation"
    echo "   Testing input validation with invalid data..."
    ./client $SERVER_IP TCP $TCP_PORT << 'EOF' 2>&1 | tee "$LOG_DIR/TCP_error.log"
1
5  # n too small
6
0
5
EOF
    echo "   Error test completed"
    
    stop_server $TCP_SERVER_PID
else
    echo "❌ Failed to start TCP server, skipping TCP tests"
fi

# ============================
# UDP TESTS
# ============================

echo "UDP TESTS"


UDP_PORT=$((PORT_BASE + 2))
if start_server $UDP_PORT; then
    UDP_SERVER_PID=$SERVER_PID
    
    # Test 4: UDP with simple graph
    monitor_network $UDP_PORT "UDP"
    run_test "UDP_simple" "UDP" "$DATA_DIR/graph_simple.txt" $UDP_PORT
    stop_network_monitor
    
    # Test 5: UDP with medium graph
    sleep 1
    run_test "UDP_medium" "UDP" "$DATA_DIR/graph_medium.txt" $UDP_PORT
    
    stop_server $UDP_SERVER_PID
else
    echo "❌ Failed to start UDP server, skipping UDP tests"
fi

# ============================
# ADVANCED TESTS
# ============================

echo "ADVANCED TESTS"


# Load test with 3 concurrent clients
echo -e "\n🔧 Test: 3 concurrent clients"
ADV_PORT=$((PORT_BASE + 3))
if start_server $ADV_PORT; then
    ADV_SERVER_PID=$SERVER_PID
    
    echo "   Starting 3 clients simultaneously..."
    for i in {1..3}; do
        (expect "$SCRIPTS_DIR/test_client.exp" "$SERVER_IP" "TCP" "$ADV_PORT" "$DATA_DIR/graph_simple.txt" "load_client_$i" > "$LOG_DIR/load_client_$i.log" 2>&1 &)
        echo "   Client $i started"
        sleep 0.5
    done
    
    echo "   Waiting 10 seconds for processing..."
    sleep 10
    
    # Check if all clients completed
    echo "   Checking client completion..."
    completed=0
    for i in {1..3}; do
        if grep -q "RESULT FOUND\|CONNECTION CLOSED" "$LOG_DIR/load_client_$i.log" 2>/dev/null; then
            completed=$((completed + 1))
        fi
    done
    echo "   $completed out of 3 clients completed successfully"
    
    stop_server $ADV_SERVER_PID
else
    echo "❌ Failed to start server for load test"
fi

# ============================
# RESULTS ANALYSIS
# ============================

echo "TEST RESULTS SUMMARY"


echo -e "\n📊 Log files generated:"
ls -la "$LOG_DIR"/*.log 2>/dev/null | head -10

echo -e "\n🔍 Network captures:"
ls -la "$LOG_DIR"/*.pcap 2>/dev/null 2>/dev/null || echo "   No network captures found"

echo -e "\n📈 Test statistics:"
echo "TCP tests passed:  $(grep -l '✅ SUCCESS' $LOG_DIR/TCP_*.log 2>/dev/null | wc -l)"
echo "UDP tests passed:  $(grep -l '✅ SUCCESS' $LOG_DIR/UDP_*.log 2>/dev/null | wc -l)"
echo "Total tests run:   $(ls $LOG_DIR/*.log 2>/dev/null | grep -v server_ | wc -l)"

echo -e "\n🔎 Error summary:"
if grep -r -i "error\|fail\|timeout" "$LOG_DIR/" 2>/dev/null | grep -v ".pcap" | head -5; then
    echo "   Some errors detected - check logs for details"
else
    echo "   No critical errors detected"
fi

echo -e "\n✅ All tests completed!"
echo "Logs are available in: $LOG_DIR/"
echo "To view detailed results: tail -f $LOG_DIR/*.log"
