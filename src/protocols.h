#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include <cstdint>

#pragma pack(push, 1)
struct GraphRequest {
    uint16_t vertices;     // Nombre de sommets
    uint16_t edges;        // Nombre d'arêtes
    uint16_t start_node;   // Sommet de départ
    uint16_t end_node;     // Sommet d'arrivée
    uint8_t weighted;      // 0=non pondéré, 1=pondéré
};

struct GraphResponse {
    int32_t path_length;   // Longueur du chemin (-1 si pas de chemin)
    int32_t error_code;    // 0=succès, 1=erreur
    int32_t path_size;     // Nombre de sommets dans le chemin
    int32_t path[100];     // Chemin complet (max 100 sommets)
    char message[256];     // Message descriptif
};
#pragma pack(pop)

#endif
