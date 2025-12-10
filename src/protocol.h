// protocol.h
#pragma once
#include <cstdint>

#ifdef _MSC_VER
#pragma pack(push,1)
#endif
// Binary TCP request (client -> server)
struct GraphRequest {
    int32_t vertices;
    int32_t edges;
    int32_t start_node;
    int32_t end_node;
    int32_t reserved; // future flags
};

// Binary TCP response (server -> client)
struct GraphResponse {
    int32_t error_code;    // 0 = ok, 1 = error
    int32_t path_length;   // total weight or -1 if error
    int32_t path_size;     // number of vertices in path
    char message[128];     // null-terminated message
    int32_t path[64];      // up to 64 nodes (safe guard)
};

// UDP packet types
enum UdpType : uint8_t {
    UDP_HEADER = 1,
    UDP_ROW = 2,
    UDP_WEIGHTS = 3,
    UDP_FIN = 4,
    UDP_ACK = 5,
    UDP_RESULT = 6
};

// Binary UDP header (fixed)
struct UdpPacketHeader {
    char cid[9];      // 8 hex chars + '\0'
    uint8_t type;     // UdpType
    // followed by payload depending on type
} __attribute__((packed));

#ifdef _MSC_VER
#pragma pack(pop)
#endif
