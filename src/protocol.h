#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>

constexpr int MAX_VERTICES = 20;
constexpr int MAX_EDGES = 20;
constexpr int UDP_MAX_RETRIES = 3;
constexpr int UDP_TIMEOUT_MS = 3000;

enum UdpPacketType : uint8_t {
    UDP_DATA = 0,
    UDP_ACK = 1,
    UDP_FIN = 2,
    UDP_ERROR = 3
};

#pragma pack(push, 1)
struct UdpPacketHeader {
    uint32_t packet_id;
    uint32_t session_id;
    uint8_t packet_type;
    uint8_t total_chunks;
    uint8_t chunk_index;
    uint8_t reserved;
    uint32_t data_size;
};
#pragma pack(pop)

struct GraphDataUdp {
    uint16_t vertices;
    uint16_t edges;
    uint16_t start_node;
    uint16_t end_node;
    int8_t inc_matrix[20][20];
    int16_t weights[20];
};

struct GraphRequest {
    int vertices;
    int edges;
    int start_node;
    int end_node;
    int weighted;
    int padding;
};

struct GraphResponse {
    int path_length;
    int path_size;
    int path[20];
    int error_code;
    char message[128];
};

#endif
