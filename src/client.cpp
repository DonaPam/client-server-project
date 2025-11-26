#include <iostream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <fstream>

#define BUFFER_SIZE 1024
#define TIMEOUT_SEC 3
#define MAX_RETRIES 3

struct GraphRequest {
    int num_vertices;
    int num_edges;
    int start_vertex;
    int end_vertex;
};

struct GraphResponse {
    double path_length;
    int path_size;
    bool success;
    char error_message[256];
};

class Client {
private:
    std::string server_ip;
    int server_port;
    std::string protocol;

    GraphRequest get_graph_from_user() {
        GraphRequest request;
        std::string input_method;
        
        std::cout << "Choose input method (keyboard/file): ";
        std::cin >> input_method;
        
        if (input_method == "file") {
            return read_graph_from_file();
        } else {
            return read_graph_from_keyboard();
        }
    }

    GraphRequest read_graph_from_keyboard() {
        GraphRequest request;
        
        std::cout << "Enter number of vertices (>=6): ";
        std::cin >> request.num_vertices;
        
        std::cout << "Enter number of edges (>=6): ";
        std::cin >> request.num_edges;
        
        std::cout << "Enter start vertex: ";
        std::cin >> request.start_vertex;
        
        std::cout << "Enter end vertex: ";
        std::cin >> request.end_vertex;
        
        return request;
    }

    GraphRequest read_graph_from_file() {
        GraphRequest request;
        std::string filename;
        
        std::cout << "Enter filename: ";
        std::cin >> filename;
        
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        
        file >> request.num_vertices >> request.num_edges;
        file >> request.start_vertex >> request.end_vertex;
        
        file.close();
        return request;
    }

    std::pair<std::vector<std::vector<int>>, std::vector<double>> get_matrix_from_user(const GraphRequest& request) {
        std::vector<std::vector<int>> incidence_matrix(request.num_vertices, 
                                                     std::vector<int>(request.num_edges));
        std::vector<double> edge_weights(request.num_edges);
        
        std::cout << "Enter incidence matrix (one row per vertex):" << std::endl;
        for (int i = 0; i < request.num_vertices; i++) {
            std::cout << "Vertex " << i << ": ";
            for (int j = 0; j < request.num_edges; j++) {
                std::cin >> incidence_matrix[i][j];
            }
        }
        
        std::cout << "Enter edge weights: ";
        for (int j = 0; j < request.num_edges; j++) {
            std::cin >> edge_weights[j];
        }
        
        return {incidence_matrix, edge_weights};
    }

    bool reliable_udp_send(int sock, const void* data, size_t size, 
                          struct sockaddr* addr, socklen_t addr_len) {
        char ack_buffer[10];
        int retries = 0;
        
        while (retries < MAX_RETRIES) {
            sendto(sock, data, size, 0, addr, addr_len);
            
            fd_set readfds;
            struct timeval timeout;
            
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            timeout.tv_sec = TIMEOUT_SEC;
            timeout.tv_usec = 0;
            
            int ready = select(sock + 1, &readfds, NULL, NULL, &timeout);
            
            if (ready > 0) {
                ssize_t received = recvfrom(sock, ack_buffer, sizeof(ack_buffer), 0, NULL, NULL);
                if (received > 0 && strncmp(ack_buffer, "ACK", 3) == 0) {
                    return true;
                }
            }
            retries++;
        }
        
        std::cout << "Lost connection with server" << std::endl;
        return false;
    }

    bool reliable_udp_receive(int sock, void* buffer, size_t size, 
                             struct sockaddr* addr, socklen_t* addr_len) {
        char ack_msg[] = "ACK";
        int retries = 0;
        
        while (retries < MAX_RETRIES) {
            fd_set readfds;
            struct timeval timeout;
            
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            timeout.tv_sec = TIMEOUT_SEC;
            timeout.tv_usec = 0;
            
            int ready = select(sock + 1, &readfds, NULL, NULL, &timeout);
            
            if (ready > 0) {
                ssize_t received = recvfrom(sock, buffer, size, 0, addr, addr_len);
                if (received > 0) {
                    // Envoyer ACK
                    sendto(sock, ack_msg, strlen(ack_msg), 0, addr, *addr_len);
                    return true;
                }
            }
            retries++;
        }
        
        return false;
    }

public:
    Client(const std::string& ip, int port, const std::string& proto) 
        : server_ip(ip), server_port(port), protocol(proto) {}

    void run() {
        while (true) {
            std::string command;
            std::cout << "\nEnter command (or 'exit' to quit): ";
            std::cin >> command;
            
            if (command == "exit") {
                break;
            }
            
            try {
                GraphRequest request = get_graph_from_user();
                auto [incidence_matrix, edge_weights] = get_matrix_from_user(request);
                
                GraphResponse response;
                
                if (protocol == "tcp") {
                    response = send_tcp_request(request, incidence_matrix, edge_weights);
                } else if (protocol == "udp") {
                    response = send_udp_request(request, incidence_matrix, edge_weights);
                } else {
                    std::cout << "Unknown protocol: " << protocol << std::endl;
                    continue;
                }
                
                display_response(response);
                
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << std::endl;
            }
        }
    }

    GraphResponse send_tcp_request(const GraphRequest& request, 
                                 const std::vector<std::vector<int>>& incidence_matrix,
                                 const std::vector<double>& edge_weights) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            throw std::runtime_error("TCP socket creation failed");
        }
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);
        
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sock);
            throw std::runtime_error("TCP connection failed");
        }
        
        // Envoyer la requête de base
        send(sock, &request, sizeof(request), 0);
        
        // Envoyer la matrice d'incidence
        for (int i = 0; i < request.num_vertices; i++) {
            send(sock, incidence_matrix[i].data(), request.num_edges * sizeof(int), 0);
        }
        
        // Envoyer les poids
        send(sock, edge_weights.data(), request.num_edges * sizeof(double), 0);
        
        // Recevoir la réponse
        GraphResponse response;
        recv(sock, &response, sizeof(response), 0);
        
        // Recevoir le chemin si existe
        std::vector<int> path;
        if (response.success && response.path_size > 0) {
            path.resize(response.path_size);
            recv(sock, path.data(), response.path_size * sizeof(int), 0);
            response.path_size = path.size(); // Pour l'affichage
        }
        
        close(sock);
        
        // Stocker le chemin pour l'affichage
        // Note: Dans une vraie implémentation, vous voudriez stocker cela dans la réponse
        std::cout << "Path vertices: ";
        for (int vertex : path) {
            std::cout << vertex << " ";
        }
        std::cout << std::endl;
        
        return response;
    }

    GraphResponse send_udp_request(const GraphRequest& request,
                                 const std::vector<std::vector<int>>& incidence_matrix,
                                 const std::vector<double>& edge_weights) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            throw std::runtime_error("UDP socket creation failed");
        }
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);
        
        GraphResponse response;
        socklen_t server_len = sizeof(server_addr);
        
        // Envoyer la requête avec fiabilité
        if (reliable_udp_send(sock, &request, sizeof(request), 
                            (struct sockaddr*)&server_addr, server_len)) {
            
            // Envoyer la matrice d'incidence
            for (int i = 0; i < request.num_vertices; i++) {
                reliable_udp_send(sock, incidence_matrix[i].data(), request.num_edges * sizeof(int),
                                (struct sockaddr*)&server_addr, server_len);
            }
            
            // Envoyer les poids
            reliable_udp_send(sock, edge_weights.data(), request.num_edges * sizeof(double),
                            (struct sockaddr*)&server_addr, server_len);
            
            // Recevoir la réponse avec fiabilité
            reliable_udp_receive(sock, &response, sizeof(response),
                               (struct sockaddr*)&server_addr, &server_len);
        }
        
        close(sock);
        return response;
    }

    void display_response(const GraphResponse& response) {
        std::cout << "\n=== RESULT ===" << std::endl;
        if (response.success) {
            std::cout << "Shortest path length: " << response.path_length << std::endl;
        } else {
            std::cout << "Error: " << response.error_message << std::endl;
        }
        std::cout << "===============" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cout << "Usage: " << argv[0] << " <server_ip> <port> <protocol(tcp/udp)>" << std::endl;
        std::cout << "Example: " << argv[0] << " 127.0.0.1 8080 tcp" << std::endl;
        return 1;
    }
    
    std::string server_ip = argv[1];
    int port = std::stoi(argv[2]);
    std::string protocol = argv[3];
    
    if (protocol != "tcp" && protocol != "udp") {
        std::cout << "Protocol must be 'tcp' or 'udp'" << std::endl;
        return 1;
    }
    
    try {
        Client client(server_ip, port, protocol);
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
