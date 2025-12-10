# Makefile
CC=g++
CFLAGS=-std=c++17 -Wall -Wextra -pthread

all: client server

client: client.cpp protocol.h
	$(CC) $(CFLAGS) client.cpp -o client

server: server.cpp protocol.h
	$(CC) $(CFLAGS) server.cpp -o server

test: all
	@echo "=== Lancement des tests ==="
	chmod +x run_tests.sh
	./run_tests.sh

test-quick: all
	@echo "=== Test rapide ==="
	@./server 17000 &
	@sleep 2
	@echo -e "2\ntest_data/simple.txt" | timeout 5 ./client 127.0.0.1 TCP 17000
	@killall server 2>/dev/null || true

clean:
	rm -f client server
	rm -rf logs test_data

.PHONY: all test test-quick clean
