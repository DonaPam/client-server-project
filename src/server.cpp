#include <iostream>
#include <vector>
#include <queue>
#include <limits>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <algorithm>
#include <sstream>
#include <functional>

#define MAX_CLIENTS 3
#define BUFFER_SIZE 1024
#define TIMEOUT_SEC 3
#define MAX_RETRIES 3

struct GraphRequest {
    int num_vertices;
    int num_edges;
    int start_vertex;
    int end_vertex;
    // Les matrices seront sérialisées séparément
};

struct GraphResponse {
    double path_length;
    int path_size;
    bool success;
    char error_message[256];
    // Les sommets du chemin seront sérialisés séparément
};

class Graph {
private:
    int num_vertices;
    int num_edges;
    std::vector<std::vector<int>> incidence_matrix;
    std::vector<double> edge_weights;

public:
    Graph(int vertices, int edges, const std::vector<std::vector<int>>& matrix, const std::vector<double>& weights)
        : num_vertices(vertices), num_edges(edges), incidence_matrix(matrix), edge_weights(weights) {}

    std::pair<double, std::vector<int>> dijkstra(int start, int end) {
        std::vector<double> dist(num_vertices, std::numeric_limits<double>::infinity());
        std::vector<int> prev(num_vertices, -1);
        std::vector<bool> visited(num_vertices, false);
        
        dist[start] = 0;
        
        // Créer la liste d'adjacence à partir de la matrice d'incidence
        std::vector<std::vector<std::pair<int, double>>> adj(num_vertices);
        for (int e = 0; e < num_edges; e++) {
            int u = -1, v = -1;
            for (int i = 0; i < num_vertices; i++) {
                if (incidence_matrix[i][e] == 1) {
                    if (u == -1) u = i;
                    else v = i;
                }
            }
            if (u != -1 && v != -1) {
                adj[u].push_back({v, edge_weights[e]});
                adj[v].push_back({u, edge_weights[e]});
            }
        }
        
        // File de priorité: (distance, sommet)
        std::priority_queue<std::pair<double, int>, 
                          std::vector<std::pair<double, int>>,
                          std::greater<std::pair<double, int>>> pq;
        pq.push({0, start});
        
        while (!pq.empty()) {
            double current_dist = pq.top().first;
            int u = pq.top().second;
            pq.pop();
            
            if (visited[u]) continue;
            visited[u] = true;
            
            if (u == end) break;
            
            for (const auto& neighbor : adj[u]) {
                int v = neighbor.first;
                double weight = neighbor.second;
                
                if (!visited[v] && current_dist + weight < dist[v]) {
                    dist[v] = current_dist + weight;
                    prev[v] = u;
                    pq.push({dist[v], v});
                }
            }
        }
        
        // Reconstruire le chemin
        std::vector<int> path;
        if (dist[end] == std::numeric_limits<double>::infinity()) {
            return {std::numeric_limits<double>::infinity(), path};
        }
        
        for (int at = end; at != -1; at = prev[at]) {
            path.push_back(at);
        }
        std::reverse(path.begin(), path.end());
        
        return {dist[end], path};
    }
};

class Server {
private:
    int server_socket_tcp, server_socket_udp;
    int port;
    bool running;
    pthread_t client_threads[MAX_CLIENTS];
    int client_count;

    // Structure pour passer les données aux threads
    struct ThreadData {
        Server* server;
        int client_socket;
        struct sockaddr_in client_addr;
        socklen_t client_len;
    };

    void handle_tcp_client(int client_socket) {
        std::cout << "Handling TCP client..." << std::endl;
        
        // Recevoir la requête de base
        GraphRequest request;
        ssize_t bytes_received = recv(client_socket, &request, sizeof(request), 0);
        
        if (bytes_received <= 0) {
            std::cerr << "Error receiving request from TCP client" << std::endl;
            close(client_socket);
            return;
        }

        // Recevoir la matrice d'incidence
        std::vector<std::vector<int>> incidence_matrix(request.num_vertices, 
                                                     std::vector<int>(request.num_edges));
        std::vector<double> edge_weights(request.num_edges);

        for (int i = 0; i < request.num_vertices; i++) {
            recv(client_socket, incidence_matrix[i].data(), request.num_edges * sizeof(int), 0);
        }
        
        // Recevoir les poids
        recv(client_socket, edge_weights.data(), request.num_edges * sizeof(double), 0);

        // Traiter la requête
        Graph graph(request.num_vertices, request.num_edges, incidence_matrix, edge_weights);
        auto result = graph.dijkstra(request.start_vertex, request.end_vertex);

        // Préparer la réponse
        GraphResponse response;
        response.path_length = result.first;
        response.path_size = result.second.size();
        response.success = (result.first != std::numeric_limits<double>::infinity());
        
        if (!response.success) {
            strncpy(response.error_message, "No path exists between the specified vertices", 
                   sizeof(response.error_message));
        } else {
            response.error_message[0] = '\0';
        }

        // Envoyer la réponse de base
        send(client_socket, &response, sizeof(response), 0);
        
        // Envoyer le chemin si existe
        if (response.success && response.path_size > 0) {
            send(client_socket, result.second.data(), response.path_size * sizeof(int), 0);
        }

        close(client_socket);
        std::cout << "TCP client handled successfully" << std::endl;
    }

    void handle_udp_client(struct sockaddr_in client_addr, socklen_t client_len) {
        std::cout << "Handling UDP client..." << std::endl;
        
        GraphRequest request;
        
        // Recevoir la requête avec fiabilité
        if (reliable_udp_receive(&request, sizeof(request), 
                               (struct sockaddr*)&client_addr, &client_len)) {
            
            // Recevoir la matrice d'incidence
            std::vector<std::vector<int>> incidence_matrix(request.num_vertices, 
                                                         std::vector<int>(request.num_edges));
            std::vector<double> edge_weights(request.num_edges);

            for (int i = 0; i < request.num_vertices; i++) {
                reliable_udp_receive(incidence_matrix[i].data(), request.num_edges * sizeof(int),
                                   (struct sockaddr*)&client_addr, &client_len);
            }
            
            // Recevoir les poids
            reliable_udp_receive(edge_weights.data(), request.num_edges * sizeof(double),
                               (struct sockaddr*)&client_addr, &client_len);

            // Traiter la requête
            Graph graph(request.num_vertices, request.num_edges, incidence_matrix, edge_weights);
            auto result = graph.dijkstra(request.start_vertex, request.end_vertex);

            // Préparer la réponse
            GraphResponse response;
            response.path_length = result.first;
            response.path_size = result.second.size();
            response.success = (result.first != std::numeric_limits<double>::infinity());
            
            if (!response.success) {
                strncpy(response.error_message, "No path exists between the specified vertices", 
                       sizeof(response.error_message));
            } else {
                response.error_message[0] = '\0';
            }

            // Envoyer la réponse avec fiabilité
            reliable_udp_send(&response, sizeof(response),
                            (struct sockaddr*)&client_addr, client_len);
            
            // Envoyer le chemin si existe
            if (response.success && response.path_size > 0) {
                reliable_udp_send(result.second.data(), response.path_size * sizeof(int),
                                (struct sockaddr*)&client_addr, client_len);
            }
        }
        
        std::cout << "UDP client handled" << std::endl;
    }

    bool reliable_udp_receive(void* buffer, size_t size, struct sockaddr* addr, socklen_t* addr_len) {
        char ack_msg[] = "ACK";
        int retries = 0;
        
        while (retries < MAX_RETRIES) {
            fd_set readfds;
            struct timeval timeout;
            
            FD_ZERO(&readfds);
            FD_SET(server_socket_udp, &readfds);
            timeout.tv_sec = TIMEOUT_SEC;
            timeout.tv_usec = 0;
            
            int ready = select(server_socket_udp + 1, &readfds, NULL, NULL, &timeout);
            
            if (ready > 0) {
                ssize_t received = recvfrom(server_socket_udp, buffer, size, 0, addr, addr_len);
                if (received > 0) {
                    // Envoyer ACK
                    sendto(server_socket_udp, ack_msg, strlen(ack_msg), 0, addr, *addr_len);
                    return true;
                }
            }
            retries++;
        }
        
        std::cout << "Lost connection with client" << std::endl;
        return false;
    }

    bool reliable_udp_send(const void* data, size_t size, struct sockaddr* addr, socklen_t addr_len) {
        char ack_buffer[10];
        int retries = 0;
        
        while (retries < MAX_RETRIES) {
            sendto(server_socket_udp, data, size, 0, addr, addr_len);
            
            fd_set readfds;
            struct timeval timeout;
            
            FD_ZERO(&readfds);
            FD_SET(server_socket_udp, &readfds);
            timeout.tv_sec = TIMEOUT_SEC;
            timeout.tv_usec = 0;
            
            int ready = select(server_socket_udp + 1, &readfds, NULL, NULL, &timeout);
            
            if (ready > 0) {
                ssize_t received = recvfrom(server_socket_udp, ack_buffer, sizeof(ack_buffer), 0, NULL, NULL);
                if (received > 0 && strncmp(ack_buffer, "ACK", 3) == 0) {
                    return true;
                }
            }
            retries++;
        }
        
        return false;
    }

    static void* tcp_client_handler(void* arg) {
        ThreadData* data = (ThreadData*)arg;
        data->server->handle_tcp_client(data->client_socket);
        delete data;
        return NULL;
    }

    static void* udp_client_handler(void* arg) {
        ThreadData* data = (ThreadData*)arg;
        data->server->handle_udp_client(data->client_addr, data->client_len);
        delete data;
        return NULL;
    }

public:
    Server(int p) : port(p), running(false), client_count(0) {
        server_socket_tcp = socket(AF_INET, SOCK_STREAM, 0);
        server_socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
        
        if (server_socket_tcp < 0 || server_socket_udp < 0) {
            throw std::runtime_error("Socket creation failed");
        }
        
        int opt = 1;
        setsockopt(server_socket_tcp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    ~Server() {
        stop();
    }

    void start() {
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        // Liaison socket TCP
        if (bind(server_socket_tcp, (struct sockaddr*)&address, sizeof(address)) < 0) {
            throw std::runtime_error("TCP bind failed");
        }
        
        // Liaison socket UDP
        if (bind(server_socket_udp, (struct sockaddr*)&address, sizeof(address)) < 0) {
            throw std::runtime_error("UDP bind failed");
        }
        
        listen(server_socket_tcp, MAX_CLIENTS);
        
        running = true;
        std::cout << "Server started on port " << port << std::endl;
        std::cout << "Waiting for connections..." << std::endl;
        
        fd_set readfds;
        while (running) {
            FD_ZERO(&readfds);
            FD_SET(server_socket_tcp, &readfds);
            FD_SET(server_socket_udp, &readfds);
            
            int max_fd = std::max(server_socket_tcp, server_socket_udp);
            
            struct timeval timeout = {1, 0}; // timeout de 1 seconde
            int activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
            
            if (activity < 0 && running) {
                std::cerr << "Select error" << std::endl;
                continue;
            }
            
            if (FD_ISSET(server_socket_tcp, &readfds)) {
                // Connexion TCP
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_socket = accept(server_socket_tcp, (struct sockaddr*)&client_addr, &client_len);
                
                if (client_socket >= 0) {
                    std::cout << "New TCP client connected: " << inet_ntoa(client_addr.sin_addr) << std::endl;
                    
                    // Gérer dans un thread séparé
                    ThreadData* data = new ThreadData{this, client_socket, client_addr, client_len};
                    pthread_t thread_id;
                    pthread_create(&thread_id, NULL, &Server::tcp_client_handler, data);
                    pthread_detach(thread_id);
                }
            }
            
            if (FD_ISSET(server_socket_udp, &readfds)) {
                // Datagramme UDP
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                
                // Créer une structure de données pour le thread
                ThreadData* data = new ThreadData{this, -1, client_addr, client_len};
                
                // Gérer dans un thread séparé
                pthread_t thread_id;
                pthread_create(&thread_id, NULL, &Server::udp_client_handler, data);
                pthread_detach(thread_id);
            }
        }
    }

    void stop() {
        running = false;
        if (server_socket_tcp >= 0) {
            close(server_socket_tcp);
            server_socket_tcp = -1;
        }
        if (server_socket_udp >= 0) {
            close(server_socket_udp);
            server_socket_udp = -1;
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }
    
    int port = std::stoi(argv[1]);
    
    try {
        Server server(port);
        
        // Gérer le signal SIGINT pour arrêter proprement
        std::cout << "Server running. Press Ctrl+C to stop." << std::endl;
        
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
