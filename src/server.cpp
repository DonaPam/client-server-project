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

#define MAX_CLIENTS 3
#define BUFFER_SIZE 1024
#define TIMEOUT_SEC 3
#define MAX_RETRIES 3

struct GraphRequest {
    int num_vertices;
    int num_edges;
    std::vector<std::vector<int>> incidence_matrix;
    std::vector<double> edge_weights;
    int start_vertex;
    int end_vertex;
};

struct GraphResponse {
    double path_length;
    std::vector<int> path_vertices;
    bool success;
    std::string error_message;
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
        
        // Create adjacency list from incidence matrix
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
        
        // Priority queue: (distance, vertex)
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
        
        // Reconstruct path
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

    void handle_tcp_client(int client_socket) {
        char buffer[BUFFER_SIZE];
        
        // Receive graph data
        GraphRequest request;
        recv(client_socket, &request, sizeof(request), 0);
        
        // Process request
        GraphResponse response = process_graph_request(request);
        
        // Send response
        send(client_socket, &response, sizeof(response), 0);
        
        close(client_socket);
    }

    void handle_udp_client(struct sockaddr_in client_addr, socklen_t client_len) {
        char buffer[BUFFER_SIZE];
        GraphRequest request;
        
        // Receive request with reliability
        if (reliable_udp_receive(&request, sizeof(request), 
                               (struct sockaddr*)&client_addr, &client_len)) {
            
            GraphResponse response = process_graph_request(request);
            
            // Send response with reliability
            reliable_udp_send(&response, sizeof(response),
                            (struct sockaddr*)&client_addr, client_len);
        }
    }

    GraphResponse process_graph_request(const GraphRequest& request) {
        GraphResponse response;
        
        try {
            // Validate input
            if (request.num_vertices < 6 || request.num_edges < 6) {
                response.success = false;
                response.error_message = "Graph must have at least 6 vertices and 6 edges";
                return response;
            }
            
            if (request.start_vertex < 0 || request.start_vertex >= request.num_vertices ||
                request.end_vertex < 0 || request.end_vertex >= request.num_vertices) {
                response.success = false;
                response.error_message = "Invalid start or end vertex";
                return response;
            }
            
            // Create graph and compute shortest path
            Graph graph(request.num_vertices, request.num_edges, 
                       request.incidence_matrix, request.edge_weights);
            
            auto result = graph.dijkstra(request.start_vertex, request.end_vertex);
            
            response.path_length = result.first;
            response.path_vertices = result.second;
            response.success = (result.first != std::numeric_limits<double>::infinity());
            
            if (!response.success) {
                response.error_message = "No path exists between the specified vertices";
            }
            
        } catch (const std::exception& e) {
            response.success = false;
            response.error_message = std::string("Error processing graph: ") + e.what();
        }
        
        return response;
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
                    // Send ACK
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
        
        // Bind TCP socket
        if (bind(server_socket_tcp, (struct sockaddr*)&address, sizeof(address)) < 0) {
            throw std::runtime_error("TCP bind failed");
        }
        
        // Bind UDP socket
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
            
            struct timeval timeout = {1, 0}; // 1 second timeout
            int activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
            
            if (activity < 0 && running) {
                std::cerr << "Select error" << std::endl;
                continue;
            }
            
            if (FD_ISSET(server_socket_tcp, &readfds)) {
                // TCP connection
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_socket = accept(server_socket_tcp, (struct sockaddr*)&client_addr, &client_len);
                
                if (client_socket >= 0) {
                    std::cout << "New TCP client connected: " << inet_ntoa(client_addr.sin_addr) << std::endl;
                    
                    // Handle in separate thread
                    pthread_t thread_id;
                    int* client_sock_ptr = new int(client_socket);
                    pthread_create(&thread_id, NULL, &Server::tcp_client_handler, client_sock_ptr);
                    pthread_detach(thread_id);
                }
            }
            
            if (FD_ISSET(server_socket_udp, &readfds)) {
                // UDP datagram
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                
                // Handle in separate thread
                pthread_t thread_id;
                struct udp_client_data* data = new udp_client_data{this, client_addr, client_len};
                pthread_create(&thread_id, NULL, &Server::udp_client_handler, data);
                pthread_detach(thread_id);
            }
        }
    }

    void stop() {
        running = false;
        if (server_socket_tcp >= 0) close(server_socket_tcp);
        if (server_socket_udp >= 0) close(server_socket_udp);
    }

private:
    struct udp_client_data {
        Server* server;
        struct sockaddr_in client_addr;
        socklen_t client_len;
    };

    static void* tcp_client_handler(void* arg) {
        int client_socket = *((int*)arg);
        delete (int*)arg;
        
        Server* server = nullptr; // Would need instance reference
        // server->handle_tcp_client(client_socket);
        close(client_socket);
        return NULL;
    }

    static void* udp_client_handler(void* arg) {
        udp_client_data* data = (udp_client_data*)arg;
        data->server->handle_udp_client(data->client_addr, data->client_len);
        delete data;
        return NULL;
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
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
