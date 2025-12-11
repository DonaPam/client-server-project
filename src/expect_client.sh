cat > expect_client.sh <<'EOF'
#!/usr/bin/expect -f

if { $argc != 4 } {
    puts "Usage: expect_client.sh <client> <args> <input_file> <expected_regex>"
    exit 1
}

set client   [lindex $argv 0]
set args_str [lindex $argv 1]
set inputf   [lindex $argv 2]
set expected [lindex $argv 3]

set args [split $args_str " "]
set timeout 15

set fh [open $inputf r]
set content [read $fh]
close $fh

spawn $client {*}$args

foreach line [split $content "\n"] {
    send -- "$line\r"
    after 80
}

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
        puts "FAIL: EOF before \"$expected\""
        exit 1
    }
}
EOF
