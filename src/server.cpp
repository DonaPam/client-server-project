#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <arpa/inet.h>
#include <vector>
#include <queue>
#include <limits>
#include <algorithm>
#include "protocols.h"

using namespace std;

const int PORT = 8080;
const int INF = numeric_limits<int>::max();

// Structure pour Dijkstra
struct Edge {
    int target;
    int weight;
};

struct Node {
    int vertex;
    int distance;
    bool operator>(const Node& other) const {
        return distance > other.distance;
    }
};

// Transformation matrice d'incidence → liste d'adjacence avec poids
vector<vector<Edge>> incidenceToAdjacency(
    const vector<vector<int>>& incidenceMatrix, 
    const vector<int>& edgeWeights, 
    int vertices, int edges) {
    
    vector<vector<Edge>> adjacencyList(vertices);
    
    for (int edge = 0; edge < edges; edge++) {
        int u = -1, v = -1;
        
        // Trouver les deux sommets connectés par cette arête
        for (int vertex = 0; vertex < vertices; vertex++) {
            if (incidenceMatrix[vertex][edge] == 1) {
                if (u == -1) {
                    u = vertex;
                } else {
                    v = vertex;
                    break;
                }
            }
        }
        
        if (u != -1 && v != -1 && edgeWeights[edge] > 0) {
            // Ajouter les deux directions pour graphe non orienté
            adjacencyList[u].push_back({v, edgeWeights[edge]});
            adjacencyList[v].push_back({u, edgeWeights[edge]});
        }
    }
    
    return adjacencyList;
}

// Algorithme de Dijkstra avec reconstruction du chemin
pair<int, vector<int>> dijkstraShortestPath(
    const vector<vector<Edge>>& graph, 
    int start, int end) {
    
    int n = graph.size();
    vector<int> distance(n, INF);
    vector<int> previous(n, -1);
    vector<bool> visited(n, false);
    
    priority_queue<Node, vector<Node>, greater<Node>> pq;
    
    distance[start] = 0;
    pq.push({start, 0});
    
    while (!pq.empty()) {
        Node current = pq.top();
        pq.pop();
        
        int u = current.vertex;
        if (visited[u]) continue;
        visited[u] = true;
        
        // Si on a atteint la destination, on peut s'arrêter
        if (u == end) break;
        
        for (const Edge& edge : graph[u]) {
            int v = edge.target;
            int weight = edge.weight;
            
            if (!visited[v] && distance[u] + weight < distance[v]) {
                distance[v] = distance[u] + weight;
                previous[v] = u;
                pq.push({v, distance[v]});
            }
        }
    }
    
    // Reconstruction du chemin
    vector<int> path;
    if (distance[end] != INF) {
        for (int vertex = end; vertex != -1; vertex = previous[vertex]) {
            path.push_back(vertex);
        }
        reverse(path.begin(), path.end());
        return {distance[end], path};
    }
    
    return {-1, path}; // Pas de chemin
}

// Algorithme BFS pour graphes non pondérés avec retour du chemin
pair<int, vector<int>> bfsShortestPath(
    const vector<vector<Edge>>& graph, 
    int start, int end) {
    
    int n = graph.size();
    vector<bool> visited(n, false);
    vector<int> distance(n, -1);
    vector<int> previous(n, -1);
    queue<int> q;
    
    q.push(start);
    visited[start] = true;
    distance[start] = 0;
    previous[start] = -1;
    
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        
        if (u == end) break;
        
        for (const Edge& edge : graph[u]) {
            int v = edge.target;
            if (!visited[v]) {
                visited[v] = true;
                distance[v] = distance[u] + 1;
                previous[v] = u;
                q.push(v);
            }
        }
    }
    
    // Reconstruction du chemin
    vector<int> path;
    if (distance[end] != -1) {
        for (int vertex = end; vertex != -1; vertex = previous[vertex]) {
            path.push_back(vertex);
        }
        reverse(path.begin(), path.end());
        return {distance[end], path};
    }
    
    return {-1, path};
}

int main() {
    cout << "Graph Calculation Server (Dijkstra) " << endl;
    
    // Configuration socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket() failed");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind() failed");
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 3) < 0) {
        perror("listen() failed");
        close(server_fd);
        return 1;
    }
    
    cout << "Server listening on port " << PORT << endl;
    
    while (true) {
        cout << "Waiting for client connection..." << endl;
        
        int client_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (client_socket < 0) {
            perror("accept() failed");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
        cout << "Client connected: " << client_ip << endl;
        
        // Réception des données du graphe
        GraphRequest request;
        ssize_t bytes_read = recv(client_socket, &request, sizeof(request), 0);
        
        if (bytes_read == sizeof(request)) {
            cout << "Graph data received:" << endl;
            cout << " - Vertices: " << request.vertices << endl;
            cout << " - Edges: " << request.edges << endl;
            cout << " - Start: " << request.start_node << endl;
            cout << " - End: " << request.end_node << endl;
            cout << " - Weighted: " << (request.weighted ? "Yes" : "No") << endl;
            
            // Réception de la matrice d'incidence
            int matrix_size = request.vertices * request.edges;
            vector<int> matrix_data(matrix_size);
            
            bytes_read = recv(client_socket, matrix_data.data(), 
                            matrix_size * sizeof(int), 0);
            
            if (bytes_read == matrix_size * sizeof(int)) {
                cout << "Incidence matrix received (" << matrix_size << " elements)" << endl;
                
                // Réception des poids des arêtes
                vector<int> edgeWeights(request.edges, 1); // Par défaut = 1
                if (request.weighted) {
                    bytes_read = recv(client_socket, edgeWeights.data(), 
                                    request.edges * sizeof(int), 0);
                    if (bytes_read == request.edges * sizeof(int)) {
                        cout << "Edge weights received (" << request.edges << " weights)" << endl;
                    } else {
                        cout << "Using default weights (1)" << endl;
                    }
                }
                
                // Reconstruction de la matrice 2D
                vector<vector<int>> incidenceMatrix(request.vertices, 
                                                   vector<int>(request.edges));
                int index = 0;
                for (int i = 0; i < request.vertices; i++) {
                    for (int j = 0; j < request.edges; j++) {
                        incidenceMatrix[i][j] = matrix_data[index++];
                    }
                }
                
                // Transformation en liste d'adjacence
                auto adjacencyList = incidenceToAdjacency(incidenceMatrix, edgeWeights, 
                                                         request.vertices, request.edges);
                
                // Calcul du plus court chemin
                pair<int, vector<int>> result;
                if (request.weighted) {
                    cout << "Running Dijkstra algorithm..." << endl;
                    result = dijkstraShortestPath(adjacencyList, request.start_node, request.end_node);
                } else {
                    cout << "Running BFS algorithm..." << endl;
                    result = bfsShortestPath(adjacencyList, request.start_node, request.end_node);
                }
                
                // Préparation de la réponse
                GraphResponse response;
                response.path_length = result.first;
                response.error_code = (result.first == -1) ? 1 : 0;
                response.path_size = min(100, (int)result.second.size());
                
                // Copie du chemin dans la réponse
                for (int i = 0; i < response.path_size; i++) {
                    response.path[i] = result.second[i];
                }
                
                // Formatage du message
                if (result.first == -1) {
                    snprintf(response.message, sizeof(response.message),
                            "No path found from %d to %d", 
                            request.start_node, request.end_node);
                } else {
                    string path_str = "";
                    for (int i = 0; i < response.path_size; i++) {
                        path_str += to_string(result.second[i]);
                        if (i < response.path_size - 1) path_str += "-";
                    }
                    snprintf(response.message, sizeof(response.message),
                            "Shortest path %d->%d: length=%d, path=%s", 
                            request.start_node, request.end_node, result.first, path_str.c_str());
                }
                
                // Envoi de la réponse
                send(client_socket, &response, sizeof(response), 0);
                cout << "Response sent: " << response.message << endl;
                
            } else {
                cout << "Error receiving matrix data" << endl;
                GraphResponse response;
                response.path_length = -1;
                response.error_code = 1;
                strcpy(response.message, "Error: matrix data incomplete");
                send(client_socket, &response, sizeof(response), 0);
            }
            
        } else {
            cout << "Invalid data received from client" << endl;
        }
        
        close(client_socket);
        cout << "Client connection closed" << endl;
    }
    
    close(server_fd);
    return 0;
}
