cat > expect_client.sh <<'EOF'
#!/usr/bin/expect -f
#
# Usage: ./expect_client.sh <client_path> "<args...>" <input_file> <expected_regex>
#
if { $argc != 4 } {
    puts "Usage: expect_client.sh <client> <args> <input_file> <expected_regex>"
    exit 1
}

set client   [lindex $argv 0]
set args_str [lindex $argv 1]
set inputf   [lindex $argv 2]
set expected [lindex $argv 3]

# split args_str to list
set args [split $args_str " "]

set timeout 10

# read input file content
if { [file exists $inputf] } {
    set fh [open $inputf r]
    set content [read $fh]
    close $fh
} else {
    puts "Input file not found: $inputf"
    exit 2
}

# spawn client with args
spawn $client {*}$args

# feed each line as if typed
foreach line [split $content "\n"] {
    # send only if non-empty or still send newline to simulate Enter
    send -- "$line\r"
    # small short sleep to give client time to consume input
    # (expect doesn't have usleep; using after)
    after 50
}

# wait for expected regexp in output
expect {
    -re $expected {
        puts "PASS: found \"$expected\""
        exit 0
    }
    timeout {
        puts "FAIL: timeout waiting for \"$expected\""
        exit 1
    }
    eof {
        puts "FAIL: EOF reached before \"$expected\""
        exit 1
    }
}
EOF
