#!/bin/bash

SERVER_ADDRESS="127.0.0.1"
TCP_PORT=5000
UDP_PORT=5001

echo "=========================================="
echo "    SIMULTANEOUS TESTS: TCP + UDP"
echo "=========================================="
echo

############################################
# Function: Launch a TCP or UDP client
############################################
run_client() {
PROTOCOL=$1
PORT=$2

expect <<EOF
set timeout 5

spawn ./client $SERVER_ADDRESS $PROTOCOL $PORT

# Menu
expect "Your choice:"
send "1\r"

# Vertices count
expect "vertices:"
send "4\r"

# Edges count
expect "edges:"
send "4\r"

# Edges
expect "u v w (0-indexed):"
send "0 1 3\r"
expect "u v w (0-indexed):"
send "1 2 1\r"
expect "u v w (0-indexed):"
send "2 3 2\r"
expect "u v w (0-indexed):"
send "0 3 10\r"

# Start vertex
expect "vertex:"
send "0\r"

# Expected result
expect {
    "Shortest path" { puts "$PROTOCOL client completed successfully"; }
    timeout { puts "$PROTOCOL client TIMEOUT"; exit 1; }
}
EOF
}

############################################
# 3 simultaneous TCP clients
############################################
echo "=== Running 3 simultaneous TCP clients ==="

run_client TCP $TCP_PORT & PID1=$!
run_client TCP $TCP_PORT & PID2=$!
run_client TCP $TCP_PORT & PID3=$!

wait $PID1; R1=$?
wait $PID2; R2=$?
wait $PID3; R3=$?

if [[ $R1 -eq 0 && $R2 -eq 0 && $R3 -eq 0 ]]; then
    echo "SUCCESS: All 3 TCP clients finished correctly"
else
    echo "FAILURE: One or more TCP clients failed"
fi
echo

############################################
# 3 simultaneous UDP clients
############################################
echo "=== Running 3 simultaneous UDP clients ==="

run_client UDP $UDP_PORT & PID4=$!
run_client UDP $UDP_PORT & PID5=$!
run_client UDP $UDP_PORT & PID6=$!

wait $PID4; R4=$?
wait $PID5; R5=$?
wait $PID6; R6=$?

if [[ $R4 -eq 0 && $R5 -eq 0 && $R6 -eq 0 ]]; then
    echo "SUCCESS: All 3 UDP clients finished correctly"
else
    echo "FAILURE: One or more UDP clients failed"
fi

echo
echo "=========================================="
echo "       END OF SIMULTANEOUS TESTS"
echo "=========================================="
