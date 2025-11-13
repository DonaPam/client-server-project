#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <random>
#include <algorithm>
#include "protocols.h"

using namespace std;

const int PORT = 8080;
const char* SERVER_IP = "127.0.0.1";

// Fonction pour générer un graphe pondéré aléatoire
void generateWeightedRandomGraph(GraphRequest& request, vector<int>& matrix_data, vector<int>& edgeWeights) {
    cout << "=== WEIGHTED GRAPH CONFIGURATION ===" << endl;
    
    // Saisie utilisateur
    cout << "Enter number of vertices (minimum 6): ";
    cin >> request.vertices;
    
    cout << "Enter number of edges (minimum 6): ";
    cin >> request.edges;
    
    cout << "Enter start node (0 to " << request.vertices - 1 << "): ";
    cin >> request.start_node;
    
    cout << "Enter end node (0 to " << request.vertices - 1 << "): ");
    cin >> request.end_node;
    
    // Validation
    request.vertices = max(6, request.vertices);
    request.edges = max(6, request.edges);
    request.start_node = max(0, min(request.vertices - 1, request.start_node));
    request.end_node = max(0, min(request.vertices - 1, request.end_node));
    
    if (request.start_node == request.end_node) {
        request.end_node = (request.start_node + 1) % request.vertices;
    }
    
    request.weighted = 1; // Graphe pondéré
    
    cout << "\nGraph parameters:" << endl;
    cout << " - Vertices: " << request.vertices << endl;
    cout << " - Edges: " << request.edges << endl;
    cout << " - Path: " << request.start_node << " -> " << request.end_node << endl;
    cout << " - Weighted: Yes" << endl;
    
    // Générateur aléatoire
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> vertex_dist(0, request.vertices - 1);
    uniform_int_distribution<> weight_dist(1, 10); // Poids entre 1 et 10
    
    // Initialisation de la matrice
    vector<vector<int>> incidenceMatrix(request.vertices, vector<int>(request.edges, 0));
    
    cout << "\nGenerating random weighted graph..." << endl;
    
    // Étape 1: Créer un arbre couvrant pour assurer la connectivité
    vector<bool> connected(request.vertices, false);
    connected[0] = true;
    
    for (int edge = 0; edge < request.vertices - 1 && edge < request.edges; edge++) {
        int connected_vertex, new_vertex;
        
        do { connected_vertex = vertex_dist(gen); } while (!connected[connected_vertex]);
        do { new_vertex = vertex_dist(gen); } while (connected[new_vertex]);
        
        incidenceMatrix[connected_vertex][edge] = 1;
        incidenceMatrix[new_vertex][edge] = 1;
        connected[new_vertex] = true;
    }
    
    // Étape 2: Ajouter les arêtes restantes
    for (int edge = request.vertices - 1; edge < request.edges; edge++) {
        int v1, v2;
        int attempts = 0;
        
        do {
            v1 = vertex_dist(gen);
            v2 = vertex_dist(gen);
            attempts++;
        } while (v1 == v2 && attempts < 100);
        
        incidenceMatrix[v1][edge] = 1;
        incidenceMatrix[v2][edge] = 1;
    }
    
    // Génération des poids aléatoires
    edgeWeights.resize(request.edges);
    for (int i = 0; i < request.edges; i++) {
        edgeWeights[i] = weight_dist(gen);
    }
    
    // Validation et correction
    for (int edge = 0; edge < request.edges; edge++) {
        int connections = 0;
        for (int vertex = 0; vertex < request.vertices; vertex++) {
            if (incidenceMatrix[vertex][edge] == 1) connections++;
        }
        
        // Corriger si nécessaire
        while (connections < 2) {
            int vertex = vertex_dist(gen);
            if (incidenceMatrix[vertex][edge] == 0) {
                incidenceMatrix[vertex][edge] = 1;
                connections++;
            }
        }
        while (connections > 2) {
            int vertex = vertex_dist(gen);
            if (incidenceMatrix[vertex][edge] == 1) {
                incidenceMatrix[vertex][edge] = 0;
                connections--;
            }
        }
    }
    
    // Affichage pour les petits graphes
    if (request.vertices <= 10 && request.edges <= 15) {
        cout << "\nGenerated incidence matrix with weights:" << endl;
        cout << "   ";
        for (int j = 0; j < request.edges; j++) {
            cout << "E" << j << "(W" << edgeWeights[j] << ") ";
        }
        cout << endl;
        
        for (int i = 0; i < request.vertices; i++) {
            cout << "V" << i << " ";
            for (int j = 0; j < request.edges; j++) {
                cout << "  " << incidenceMatrix[i][j] << "   ";
            }
            cout << endl;
        }
    }
    
    // Applatissement de la matrice
    matrix_data.clear();
    for (const auto& row : incidenceMatrix) {
        for (int val : row) {
            matrix_data.push_back(val);
        }
    }
    
    cout << "Weighted graph generation completed" << endl;
}

int main() {
    cout << "=== Weighted Graph Client ===" << endl;
    
    // Configuration socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket() failed");
        return 1;
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton() failed");
        close(sock);
        return 1;
    }
    
    // Connexion au serveur
    cout << "Connecting to server..." << endl;
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect() failed");
        close(sock);
        return 1;
    }
    cout << "Connected to server successfully!" << endl;
    
    // Génération du graphe pondéré
    GraphRequest request;
    vector<int> matrix_data;
    vector<int> edgeWeights;
    
    generateWeightedRandomGraph(request, matrix_data, edgeWeights);
    
    // Envoi des données au serveur
    cout << "\nSending graph data to server..." << endl;
    
    // En-tête
    ssize_t bytes_sent = send(sock, &request, sizeof(request), 0);
    if (bytes_sent != sizeof(request)) {
        perror("Error sending request header");
        close(sock);
        return 1;
    }
    cout << "Header sent (" << bytes_sent << " bytes)" << endl;
    
    // Matrice d'incidence
    bytes_sent = send(sock, matrix_data.data(), matrix_data.size() * sizeof(int), 0);
    if (bytes_sent != matrix_data.size() * sizeof(int)) {
        perror("Error sending matrix data");
        close(sock);
        return 1;
    }
    cout << "Matrix sent (" << bytes_sent << " bytes)" << endl;
    
    // Poids des arêtes
    bytes_sent = send(sock, edgeWeights.data(), edgeWeights.size() * sizeof(int), 0);
    if (bytes_sent != edgeWeights.size() * sizeof(int)) {
        perror("Error sending weight data");
        close(sock);
        return 1;
    }
    cout << "Weights sent (" << bytes_sent << " bytes)" << endl;
    
    // Réception de la réponse
    cout << "Waiting for server response..." << endl;
    GraphResponse response;
    ssize_t bytes_read = recv(sock, &response, sizeof(response), 0);
    
    if (bytes_read == sizeof(response)) {
        cout << "\n=== CALCULATION RESULT ===" << endl;
        cout << "Message: " << response.message << endl;
        cout << "Path length: " << response.path_length << endl;
        cout << "Path size: " << response.path_size << " vertices" << endl;
        
        if (response.path_size > 0) {
            cout << "Complete path: ";
            for (int i = 0; i < response.path_size; i++) {
                cout << response.path[i];
                if (i < response.path_size - 1) cout << " -> ";
            }
            cout << endl;
        }
        
        cout << "Status: " << (response.error_code == 0 ? "Success" : "Error") << endl;
    } else {
        cout << "Incomplete response from server" << endl;
    }
    
    close(sock);
    cout << "\nConnection closed" << endl;
    
    return 0;
}
