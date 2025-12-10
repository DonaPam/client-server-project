// protocol.h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>

#pragma pack(push, 1)  // Pas de padding pour optimisation réseau

// Structure pour la requête TCP (44 octets)
struct GraphRequest {
    uint16_t vertices;     // n (2 octets)
    uint16_t edges;        // m (2 octets) 
    uint16_t start_node;   // s (2 octets)
    uint16_t end_node;     // t (2 octets)
    uint8_t protocol;      // 1=TCP, 2=UDP (1 octet)
    uint8_t reserved[35];  // Pour alignement futur
    
    GraphRequest() {
        memset(this, 0, sizeof(GraphRequest));
    }
};

// Structure pour la réponse TCP (max 528 octets)
struct GraphResponse {
    int32_t error_code;    // 0=OK, autre=erreur (4 octets)
    int32_t path_length;   // Longueur du chemin (4 octets)
    uint16_t path_size;    // Nombre de sommets dans le chemin (2 octets)
    int32_t path[128];     // Chemin max 128 sommets (512 octets)
    char message[64];      // Message d'erreur (64 octets)
    
    GraphResponse() {
        memset(this, 0, sizeof(GraphResponse));
        error_code = 1;    // Erreur par défaut
        path_length = -1;
    }
};

// Structure pour paquets UDP (optimisé)
struct UdpHeader {
    char client_id[16];    // ID client (16 octets)
    uint16_t packet_type;  // 1=HEADER, 2=DATA, 3=WEIGHTS, 4=FIN, 5=ACK (2 octets)
    uint16_t seq_num;      // Numéro de séquence (2 octets)
    uint16_t total_packets;// Total packets (2 octets)
    uint16_t current_packet;// Packet courant (2 octets)
};

// Structure pour données UDP (header + données)
struct UdpDataPacket {
    UdpHeader header;
    char data[1024];       // Données (taille variable)
};

#pragma pack(pop)

// Constantes
const uint16_t UDP_HEADER = 1;
const uint16_t UDP_DATA = 2;
const uint16_t UDP_WEIGHTS = 3;
const uint16_t UDP_FIN = 4;
const uint16_t UDP_ACK = 5;

const int MAX_VERTICES = 1000;    // Limite supérieure OДЗ
const int MIN_VERTICES = 6;       // Limite inférieure OДЗ
const int MAX_EDGES = 5000;       // Limite supérieure OДЗ

#endif
