#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>

constexpr int MAX_VERTICES = 200;
constexpr int MAX_EDGES = 400;
constexpr int UDP_MAX_RETRIES = 3;
constexpr int UDP_TIMEOUT_MS = 3000;

// Types de paquets UDP
enum UdpPacketType : uint8_t {
    UDP_DATA = 0,
    UDP_ACK = 1,
    UDP_FIN = 2,
    UDP_ERROR = 3
};

// Header pour chaque paquet UDP (fiable)
struct UdpPacketHeader {
    uint32_t packet_id;      // ID unique du paquet
    uint32_t session_id;     // ID de session client
    uint8_t packet_type;     // UdpPacketType
    uint8_t total_chunks;    // Nombre total de chunks
    uint8_t chunk_index;     // Index actuel (0-based)
    uint8_t reserved;        // Alignement
    uint32_t data_size;      // Taille des données utiles
};

// Structure compacte pour les données de graphe (UDP)
struct GraphDataUdp {
    uint16_t vertices;       // n (0-65535)
    uint16_t edges;          // m (0-65535)
    uint16_t start_node;
    uint16_t end_node;
    int8_t inc_matrix[MAX_VERTICES][MAX_EDGES]; // -128..127
    int16_t weights[MAX_EDGES]; // -32768..32767
};

// Request TCP (inchangé mais aligné)
struct GraphRequest {
    int vertices;
    int edges;
    int start_node;
    int end_node;
    int weighted;
    int padding;
};

// Response TCP
struct GraphResponse {
    int path_length;
    int path_size;
    int path[MAX_VERTICES];
    int error_code;
    char message[128];
};

#endif // PROTOCOL_H
