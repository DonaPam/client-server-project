cat > test_all.sh <<'EOF'
#!/bin/bash
# Minimal test suite (Bash + Expect) for client.
# Adjust CLIENT path if needed.

CLIENT="../client"   # Path to compiled client relative to tests/
IP="127.0.0.1"

TCP_PORT=5000
UDP_PORT=5001
UDP_BAD_PORT=59999

EXP="./expect_client.sh"

# helper
run_test() {
    name="$1"
    args="$2"
    inputf="$3"
    expected="$4"

    echo "=== Test: $name ==="
    $EXP "$CLIENT" "$args" "$inputf" "$expected"
    rc=$?
    if [ $rc -eq 0 ]; then
        echo "[OK] $name"
    else
        echo "[FAIL] $name (code $rc)"
    fi
    echo
    return $rc
}

# Tests required by the task:

# 2.11.1 TCP operation
run_test "TCP_operation" "$IP TCP $TCP_PORT" "inputs/keyboard_valid.txt" "RESULT \\(TCP\\)"

# 2.11.2 UDP operation (normal)
run_test "UDP_operation" "$IP UDP $UDP_PORT" "inputs/keyboard_valid.txt" "RESULT \\(UDP\\)"

# 2.11.2 UDP client when server unreachable (expect client prints some error or timeout message)
# We look for "Perte de connexion" or "Timeout" or "No or incomplete response" (some variants).
run_test "UDP_server_unreachable" "$IP UDP $UDP_BAD_PORT" "inputs/keyboard_valid.txt" "Perte de connexion|Timeout|No or incomplete response"

# 2.11.3 Keyboard input (stdin)
run_test "Keyboard_input" "$IP TCP $TCP_PORT" "inputs/keyboard_valid.txt" "RESULT \\(TCP\\)"

# 2.11.4 File input
run_test "File_input_n6" "$IP TCP $TCP_PORT" "inputs/file_n6.txt" "Graph data read successfully|RESULT"

# 2.11.5 Boundary n tests (n=5 invalid, n=6 lower bound, n=19 upper bound, n=20 invalid)
run_test "n=5_invalid"  "$IP TCP $TCP_PORT" "inputs/file_n5.txt" "Invalid n|Invalid"
run_test "n=6_lower"    "$IP TCP $TCP_PORT" "inputs/file_n6.txt" "Graph data read successfully|RESULT"
run_test "n=19_upper"   "$IP TCP $TCP_PORT" "inputs/file_n19.txt" "Graph data read successfully|RESULT"
run_test "n=20_invalid" "$IP TCP $TCP_PORT" "inputs/file_n20.txt" "Invalid n|Invalid"

# 2.11.6 Middle of domain (n=12)
run_test "n=12_middle" "$IP TCP $TCP_PORT" "inputs/file_n12.txt" "Graph data read successfully|RESULT"

echo "=== TEST SUITE FINISHED ==="
EOF
