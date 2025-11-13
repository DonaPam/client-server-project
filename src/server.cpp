#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <arpa/inet.h>
#include <vector>
#include "protocols.h"

using namespace std;

const int PORT = 8080;

// Simulated shortest path calculation (to be replaced with BFS)
int calculateShortestPath(const GraphRequest& request, vector<vector<int>>& incidenceMatrix) {
    cout << "Calculating shortest path from " << request.start_node 
         << " to " << request.end_node << endl;
    cout << "Graph: " << request.vertices << " vertices, " 
         << request.edges << " edges" << endl;
    
    // TEMPORARY: Return simple calculation
    // Later: Implement BFS algorithm
    return abs(request.start_node - request.end_node) + 1;
}

int main() {
    cout << "Graph Calculation Server" << endl;
    
    // Socket setup
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
        
        // Receive graph data
        GraphRequest request;
        ssize_t bytes_read = recv(client_socket, &request, sizeof(request), 0);
        
        if (bytes_read == sizeof(request)) {
            cout << "Graph data received:" << endl;
            cout << " - Vertices: " << request.vertices << endl;
            cout << " - Edges: " << request.edges << endl;
            cout << " - Start node: " << request.start_node << endl;
            cout << " - End node: " << request.end_node << endl;
            
            // Receive incidence matrix
            int matrix_size = request.vertices * request.edges;
            vector<int> matrix_data(matrix_size);
            
            bytes_read = recv(client_socket, matrix_data.data(), 
                            matrix_size * sizeof(int), 0);
            
            if (bytes_read == matrix_size * sizeof(int)) {
                cout << "Incidence matrix received (" 
                     << matrix_size << " elements)" << endl;
                
                // Reconstruct 2D matrix
                vector<vector<int>> incidenceMatrix(request.vertices, 
                                                   vector<int>(request.edges));
                int index = 0;
                for (int i = 0; i < request.vertices; i++) {
                    for (int j = 0; j < request.edges; j++) {
                        incidenceMatrix[i][j] = matrix_data[index++];
                    }
                }
                
                // Calculate shortest path
                int result = calculateShortestPath(request, incidenceMatrix);
                
                // Prepare response
                GraphResponse response;
                response.path_length = result;
                response.error_code = 0;
                snprintf(response.message, sizeof(response.message),
                        "Path %d to %d: length = %d",
                        request.start_node, request.end_node, result);
                
                // Send response
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
