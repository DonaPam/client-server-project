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

// Function to generate a random connected graph based on user parameters
void generateRandomGraphFromInput(GraphRequest& request, vector<int>& matrix_data) {
    cout << "GRAPH CONFIGURATION " << endl;
    
    // User input for graph size
    cout << "Enter number of vertices (minimum 6): ";
    cin >> request.vertices;
    
    cout << "Enter number of edges (minimum 6): ";
    cin >> request.edges;
    
    // Input validation
    if (request.vertices < 6) {
        cout << "Warning: Using minimum 6 vertices" << endl;
        request.vertices = 6;
    }
    
    if (request.edges < 6) {
        cout << "Warning: Using minimum 6 edges" << endl;
        request.edges = 6;
    }
    
    // User input for start and end nodes
    cout << "Enter start node (0 to " << request.vertices - 1 << "): ";
    cin >> request.start_node;
    
    cout << "Enter end node (0 to " << request.vertices - 1 << "): ";
    cin >> request.end_node;
    
    // Validate nodes
    if (request.start_node < 0 || request.start_node >= request.vertices) {
        request.start_node = 0;
        cout << "Invalid start node, using 0" << endl;
    }
    
    if (request.end_node < 0 || request.end_node >= request.vertices) {
        request.end_node = request.vertices - 1;
        cout << "Invalid end node, using " << request.end_node << endl;
    }
    
    if (request.start_node == request.end_node) {
        request.end_node = (request.start_node + 1) % request.vertices;
        cout << "Start and end nodes are same, using end node: " << request.end_node << endl;
    }
    
    cout << "\nGraph parameters configured:" << endl;
    cout << " - Vertices: " << request.vertices << endl;
    cout << " - Edges: " << request.edges << endl;
    cout << " - Path: " << request.start_node << " -> " << request.end_node << endl;
    
    // Random number generator
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> vertex_dist(0, request.vertices - 1);
    
    // Initialize incidence matrix with zeros
    vector<vector<int>> incidenceMatrix(request.vertices, vector<int>(request.edges, 0));
    
    cout << "\nGenerating random connections..." << endl;
    
    // Generate a connected graph
    // Step 1: Create a spanning tree to ensure connectivity
    vector<bool> connected(request.vertices, false);
    connected[0] = true;
    int connected_count = 1;
    
    for (int edge = 0; edge < request.vertices - 1 && edge < request.edges; edge++) {
        int connected_vertex, new_vertex;
        
        // Find a connected vertex
        do {
            connected_vertex = vertex_dist(gen);
        } while (!connected[connected_vertex]);
        
        // Find a non-connected vertex
        do {
            new_vertex = vertex_dist(gen);
        } while (connected[new_vertex]);
        
        // Connect them
        incidenceMatrix[connected_vertex][edge] = 1;
        incidenceMatrix[new_vertex][edge] = 1;
        connected[new_vertex] = true;
        connected_count++;
    }
    
    // Step 2: Add remaining edges randomly between any two distinct vertices
    for (int edge = request.vertices - 1; edge < request.edges; edge++) {
        int v1, v2;
        int attempts = 0;
        
        do {
            v1 = vertex_dist(gen);
            v2 = vertex_dist(gen);
            attempts++;
            
            // Avoid infinite loop
            if (attempts > 100) {
                cout << "Warning: Could not find valid edge connection after 100 attempts" << endl;
                break;
            }
        } while (v1 == v2);
        
        incidenceMatrix[v1][edge] = 1;
        incidenceMatrix[v2][edge] = 1;
    }
    
    // Validate the matrix
    cout << "Validating incidence matrix..." << endl;
    int valid_edges = 0;
    for (int edge = 0; edge < request.edges; edge++) {
        int connection_count = 0;
        for (int vertex = 0; vertex < request.vertices; vertex++) {
            if (incidenceMatrix[vertex][edge] == 1) {
                connection_count++;
            }
        }
        if (connection_count == 2) {
            valid_edges++;
        } else {
            cout << "Edge " << edge << " has " << connection_count << " connections (fixing...)" << endl;
            // Fix invalid edges by ensuring exactly 2 connections
            int current_connections = 0;
            for (int vertex = 0; vertex < request.vertices && current_connections < 2; vertex++) {
                if (incidenceMatrix[vertex][edge] == 1) {
                    current_connections++;
                }
            }
            // Add missing connections
            for (int vertex = 0; vertex < request.vertices && current_connections < 2; vertex++) {
                if (incidenceMatrix[vertex][edge] == 0) {
                    incidenceMatrix[vertex][edge] = 1;
                    current_connections++;
                }
            }
            // Remove excess connections
            for (int vertex = 0; vertex < request.vertices && current_connections > 2; vertex++) {
                if (incidenceMatrix[vertex][edge] == 1) {
                    incidenceMatrix[vertex][edge] = 0;
                    current_connections--;
                }
            }
        }
    }
    
    cout << "Matrix validation completed: " << valid_edges << "/" << request.edges << " edges valid" << endl;
    
    // Display the generated matrix (optional - can be commented out for large graphs)
    if (request.vertices <= 10 && request.edges <= 15) {
        cout << "\nGenerated incidence matrix:" << endl;
        cout << "   ";
        for (int j = 0; j < request.edges; j++) {
            cout << "E" << j << " ";
        }
        cout << endl;
        
        for (int i = 0; i < request.vertices; i++) {
            cout << "V" << i << " ";
            for (int j = 0; j < request.edges; j++) {
                cout << " " << incidenceMatrix[i][j] << " ";
            }
            cout << endl;
        }
    } else {
        cout << "Graph too large for matrix display" << endl;
    }
    
    // Flatten matrix for transmission
    matrix_data.clear();
    for (const auto& row : incidenceMatrix) {
        for (int val : row) {
            matrix_data.push_back(val);
        }
    }
    
    cout << "Random graph generation completed successfully" << endl;
}

// Function to use the predefined 6x6 test matrix
void usePredefinedTestGraph(GraphRequest& request, vector<int>& matrix_data) {
    cout << "Using predefined 6x6 test graph..." << endl;
    
    request.vertices = 6;
    request.edges = 6;
    request.start_node = 0;
    request.end_node = 5;
    
    // Predefined valid 6x6 incidence matrix
    vector<vector<int>> test_matrix = {
        {1, 1, 0, 0, 0, 0},  // V0: edges 0,1
        {1, 0, 1, 0, 0, 0},  // V1: edges 0,2
        {0, 1, 1, 1, 0, 0},  // V2: edges 1,2,3
        {0, 0, 0, 1, 1, 0},  // V3: edges 3,4
        {0, 0, 0, 0, 1, 1},  // V4: edges 4,5
        {0, 0, 0, 0, 0, 1}   // V5: edge 5
    };
    
    cout << "Predefined graph parameters:" << endl;
    cout << " - Vertices: " << request.vertices << endl;
    cout << " - Edges: " << request.edges << endl;
    cout << " - Path: " << request.start_node << " -> " << request.end_node << endl;
    
    // Display the matrix
    cout << "\nPredefined incidence matrix:" << endl;
    cout << "   ";
    for (int j = 0; j < request.edges; j++) {
        cout << "E" << j << " ";
    }
    cout << endl;
    
    for (int i = 0; i < request.vertices; i++) {
        cout << "V" << i << " ";
        for (int j = 0; j < request.edges; j++) {
            cout << " " << test_matrix[i][j] << " ";
        }
        cout << endl;
    }
    
    // Flatten matrix for transmission
    matrix_data.clear();
    for (const auto& row : test_matrix) {
        for (int val : row) {
            matrix_data.push_back(val);
        }
    }
}

int main() {
    cout << "Graph Client - Hybrid Input " << endl;
    
    int choice;
    cout << "\nChoose graph input method:" << endl;
    cout << "1. Custom graph with random connections" << endl;
    cout << "2. Predefined 6x6 test graph" << endl;
    cout << "Enter your choice (1 or 2): ";
    cin >> choice;
    
    // Socket configuration
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
    
    // Connection to server
    cout << "\nConnecting to server..." << endl;
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect() failed");
        close(sock);
        return 1;
    }
    cout << "Connected to server successfully!" << endl;
    
    // Generate graph based on user choice
    GraphRequest request;
    vector<int> matrix_data;
    
    if (choice == 1) {
        generateRandomGraphFromInput(request, matrix_data);
    } else {
        usePredefinedTestGraph(request, matrix_data);
    }
    
    // Send data to server
    cout << "\nSending graph data to server..." << endl;
    
    // Send header
    ssize_t bytes_sent = send(sock, &request, sizeof(request), 0);
    if (bytes_sent != sizeof(request)) {
        perror("Error sending request header");
        close(sock);
        return 1;
    }
    cout << "Header sent (" << bytes_sent << " bytes)" << endl;
    
    // Send matrix
    bytes_sent = send(sock, matrix_data.data(), matrix_data.size() * sizeof(int), 0);
    if (bytes_sent != matrix_data.size() * sizeof(int)) {
        perror("Error sending matrix data");
        close(sock);
        return 1;
    }
    cout << "Matrix data sent (" << bytes_sent << " bytes)" << endl;
    
    // Receive response
    cout << "Waiting for server response..." << endl;
    GraphResponse response;
    ssize_t bytes_read = recv(sock, &response, sizeof(response), 0);
    
    if (bytes_read == sizeof(response)) {
        cout << "\n CALCULATION RESULT " << endl;
        cout << "Message: " << response.message << endl;
        cout << "Path length: " << response.path_length << endl;
        cout << "Status: " << (response.error_code == 0 ? "Success" : "Error") << endl;
    } else {
        cout << "Incomplete response from server" << endl;
    }
    
    close(sock);
    cout << "\nConnection closed" << endl;
    
    return 0;
}
