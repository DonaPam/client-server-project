#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstring>

constexpr int MAX_VERTICES = 200;
constexpr int MAX_EDGES = 400;

// GraphRequest pour le mode TCP (envoi binaire)
struct GraphRequest {
    int vertices;    // n
    int edges;       // m
    int start_node;  // 0-based
    int end_node;    // 0-based
    int weighted;    // =1
    int padding;     // alignement
};

// GraphResponse (binaire TCP)
struct GraphResponse {
    int path_length;         // -1 si aucun chemin
    int path_size;           // nombre de sommets
    int path[MAX_VERTICES];  // chemin
    int error_code;          // 0 OK, 1 erreur
    char message[128];
};

#endif // PROTOCOL_H
