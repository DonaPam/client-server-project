cat > test_all.sh <<'EOF'
#!/bin/bash

CLIENT="../client"
IP="127.0.0.1"
TCP_PORT=5000
UDP_PORT=5001
UDP_BAD_PORT=59999
EXP="./expect_client.sh"

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
        echo "[FAIL] $name"
    fi
    echo
}

# 2.11.1 – TCP
run_test "TCP_operation" "$IP TCP $TCP_PORT" "inputs/keyboard_valid.txt" "RESULT"

# 2.11.2 – UDP normal
run_test "UDP_operation" "$IP UDP $UDP_PORT" "inputs/keyboard_valid.txt" "RESULT"

# 2.11.2 – UDP unreachable
run_test "UDP_server_unreachable" "$IP UDP $UDP_BAD_PORT" "inputs/keyboard_valid.txt" "Perte de connexion|Timeout"

# 2.11.3 – Keyboard
run_test "Keyboard_input" "$IP TCP $TCP_PORT" "inputs/keyboard_valid.txt" "RESULT"

# 2.11.4 – File input
run_test "File_input_n6" "$IP TCP $TCP_PORT" "inputs/file_n6.txt" "RESULT"

# 2.11.5 – Boundary tests
run_test "n=5_invalid"  "$IP TCP $TCP_PORT" "inputs/file_n5.txt"  "Invalid"
run_test "n=6_lower"    "$IP TCP $TCP_PORT" "inputs/file_n6.txt"  "RESULT"
run_test "n=12_middle"  "$IP TCP $TCP_PORT" "inputs/file_n12.txt" "RESULT"
run_test "n=19_upper"   "$IP TCP $TCP_PORT" "inputs/file_n19.txt" "RESULT"
run_test "n=20_invalid" "$IP TCP $TCP_PORT" "inputs/file_n20.txt" "Invalid"

echo "=== ALL TESTS COMPLETED ==="
EOF
