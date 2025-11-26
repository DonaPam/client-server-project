#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_VERTICES 200
#define MAX_EDGES    400

struct GraphRequest {
    int vertices;                // Nombre de sommets
    int edges;                   // Nombre d'arêtes
    int start_node;              // Source
    int end_node;                // Destination
    int weighted;                // =1 (toujours pondéré maintenant)
};

struct GraphResponse {
    int path_length;             // Distance totale
    int path[MAX_VERTICES];      // Chemin complet
    int path_size;               // Nombre de sommets dans le chemin
    int error_code;              // 0 OK | 1 aucun chemin
    char message[128];           // Message retour
};

#endif
