#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <cstring>
#include "protocols.h"

using namespace std;

int main() {
    string server_ip;
    int port;

    cout << "=== CLIENT - GRAPH MANUAL INPUT MODE ===" << endl;

    // --- CHOIX DE L’IP + PORT ---
    cout << "Enter server IP (default 127.0.0.1) : ";
    cin >> server_ip;
    cout << "Enter server port : ";
    cin >> port;

    // --- SAISIE DU GRAPHE ---
    GraphRequest request;    
    cout << "\nEnter number of vertices: ";
    cin >> request.vertices;
    cout << "Enter number of edges: ";
    cin >> request.edges;

    request.weighted = 1; // ON TRAVAILLE AVEC MATRICE D’INCIDENCE PONDÉRÉE

    // Saisie des sommets de départ/fin
    cout << "Enter start vertex (0-" << request.vertices-1 << "): ";
    cin >> request.start_node;
    cout << "Enter end vertex (0-" << request.vertices-1 << "): ";
    cin >> request.end_node;

    // --- MATRICE D’INCIDENCE ---
    vector<int> matrix_data(request.vertices * request.edges, 0);
    vector<int> edgeWeights(request.edges);

    cout << "\n=== Manual edge entry ===" << endl;
    cout << "For each edge, enter two vertices connected + its weight." << endl;
    cout << "Format: u v w  (u and v are vertices, w is weight)" << endl << endl;

    for(int e=0; e<request.edges; e++){
        int u, v, w;
        cout << "Edge " << e << " : ";
        cin >> u >> v >> w;

        // On stocke l’arête dans la matrice d’incidence
        matrix_data[u * request.edges + e] = +w;  // +w pour un sommet
        matrix_data[v * request.edges + e] = -w;  // -w pour l’autre

        edgeWeights[e] = w;
    }

    // --- CREATION SOCKET ---
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);

    cout << "\nConnecting to server..." << endl;
    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
        perror("Connection failed");
        return 1;
    }
    cout << "Connected ✓" << endl;

    // --- ENVOI AU SERVEUR (header + matrice + poids) ---
    send(sock, &request, sizeof(request), 0);
    send(sock, matrix_data.data(), matrix_data.size()*sizeof(int), 0);
    send(sock, edgeWeights.data(), edgeWeights.size()*sizeof(int), 0);

    // --- RÉCEPTION RESULTAT ---
    GraphResponse response;
    recv(sock, &response, sizeof(response), 0);

    cout << "\n========== SERVER RESPONSE ==========" << endl;
    cout << "Shortest path length = " << response.path_length << endl;
    cout << "Path = ";

    for(int i=0 ; i<response.path_size ; i++){
        cout << response.path[i];
        if(i < response.path_size-1) cout << " -> ";
    }
    cout << endl;

    cout << "=====================================\n";

    close(sock);
    return 0;
}
