#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include <cstdint>

#pragma pack(push, 1)  // Pas de padding pour l'envoi réseau
struct GraphRequest {
    uint16_t vertices;     // Nombre de sommets (2 octets)
    uint16_t edges;        // Nombre d'arêtes (2 octets)
    uint16_t start_node;   // Sommet de départ (2 octets)
    uint16_t end_node;     // Sommet d'arrivée (2 octets)
    // Total: 8 octets pour l'en-tête
};

struct GraphResponse {
    int32_t path_length;   // Longueur du chemin (-1 si pas de chemin)
    int32_t error_code;    // 0=succès, 1=erreur
    char message[256];     // Message descriptif
};
#pragma pack(pop)

#endif
