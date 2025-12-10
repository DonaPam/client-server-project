// Compile: g++ client.cpp -o client -std=c++17
#include <bits/stdc++.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <getopt.h>
#include <poll.h>
#include <fcntl.h>
#include "protocol.h"
using namespace std;

// ---- Variables globales ----
atomic<bool> exit_requested{false};

// ---- Prototypes ----
void show_usage(const char* prog);
bool parse_arguments(int argc, char* argv[], string& ip, int& proto, int& port);
bool get_graph_data(int& n, int& m, int& s, int& t, 
                    vector<vector<int8_t>>& inc, vector<int16_t>& weights);
bool send_tcp(const string& ip, int port, int n, int m, int s, int t,
              const vector<vector<int8_t>>& inc, const vector<int16_t>& weights);
bool send_udp_reliable(const string& ip, int port, int n, int m, int s, int t,
                       const vector<vector<int8_t>>& inc, const vector<int16_t>& weights);

// ---- Split ----
vector<string> split_ws(const string& s) {
    vector<string> out;
    string token;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!token.empty()) {
                out.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) out.push_back(token);
    return out;
}

// ---- Validation des données ----
bool validate_graph_input(int n, int m, int s, int t, 
                         const vector<vector<int8_t>>& inc) {
    if (n < 6 || n >= 20) {
        cerr << "Error: n must be in [6, 19]\n";
        return false;
    }
    if (m < 6 || m >= 20) {
        cerr << "Error: m must be in [6, 19]\n";
        return false;
    }
    if (s < 0 || s >= n) {
        cerr << "Error: start node must be in [0, " << n-1 << "]\n";
        return false;
    }
    if (t < 0 || t >= n) {
        cerr << "Error: end node must be in [0, " << n-1 << "]\n";
        return false;
    }
    
    // Vérifier matrice d'incidence
    for (int e = 0; e < m; e++) {
        int endpoints = 0;
        for (int v = 0; v < n; v++) {
            if (inc[v][e] != 0) endpoints++;
        }
        if (endpoints != 2) {
            cerr << "Error: edge " << e << " has " << endpoints << " endpoints (expected 2)\n";
            return false;
        }
    }
    
    return true;
}

// ---- Génération ID de session ----
string generate_session_id() {
    static mt19937_64 rng(chrono::steady_clock::now().time_since_epoch().count());
    uint64_t id = rng();
    stringstream ss;
    ss << hex << setw(16) << setfill('0') << id;
    return ss.str();
}

// ---- Affichage usage ----
void show_usage(const char* prog) {
    cout << "Usage:\n";
    cout << "  " << prog << " --ip <IP> --port <PORT> --protocol <TCP|UDP> [options]\n";
    cout << "  " << prog << " (interactive mode)\n\n";
    cout << "Options:\n";
    cout << "  --ip, -i          Server IP address (default: 127.0.0.1)\n";
    cout << "  --port, -p        Server port (required)\n";
    cout << "  --protocol, -P    Protocol: TCP or UDP (required)\n";
    cout << "  --file, -f        Input file (optional)\n";
    cout << "  --help, -h        Show this help\n\n";
    cout << "Examples:\n";
    cout << "  " << prog << " --ip 127.0.0.1 --port 1234 --protocol TCP\n";
    cout << "  " << prog << " --ip 192.168.1.100 --port 8080 --protocol UDP --file graph.txt\n";
    cout << "  " << prog << " (for interactive mode)\n";
}

// ---- Parsing arguments ----
bool parse_arguments(int argc, char* argv[], 
                     string& ip, int& proto, int& port, string& filename) {
    
    static struct option long_options[] = {
        {"ip", required_argument, 0, 'i'},
        {"port", required_argument, 0, 'p'},
        {"protocol", required_argument, 0, 'P'},
        {"file", required_argument, 0, 'f'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    ip = "127.0.0.1";
    proto = 0;
    port = 0;
    filename = "";
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "i:p:P:f:h", 
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'i':
                ip = optarg;
                break;
            case 'p':
                try {
                    port = stoi(optarg);
                    if (port < 1 || port > 65535) {
                        cerr << "Error: Port must be 1-65535\n";
                        return false;
                    }
                } catch (...) {
                    cerr << "Error: Invalid port\n";
                    return false;
                }
                break;
            case 'P': {
                string proto_str = optarg;
                transform(proto_str.begin(), proto_str.end(), proto_str.begin(), ::toupper);
                if (proto_str == "TCP") proto = 1;
                else if (proto_str == "UDP") proto = 2;
                else {
                    cerr << "Error: Protocol must be TCP or UDP\n";
                    return false;
                }
                break;
            }
            case 'f':
                filename = optarg;
                break;
            case 'h':
                show_usage(argv[0]);
                exit(0);
            default:
                return false;
        }
    }
    
    if (port == 0 || proto == 0) {
        return false;
    }
    
    return true;
}

// ---- Lecture données graphe ----
bool get_graph_data(int& n, int& m, int& s, int& t,
                    vector<vector<int8_t>>& inc, vector<int16_t>& weights,
                    const string& filename = "") {
    
    if (!filename.empty()) {
        // Lecture depuis fichier
        ifstream fin(filename);
        if (!fin) {
            cerr << "Error: Cannot open file " << filename << "\n";
            return false;
        }
        
        fin >> n >> m >> s >> t;
        inc.assign(n, vector<int8_t>(m, 0));
        weights.assign(m, 1);
        
        for (int e = 0; e < m; e++) {
            int u, v, w;
            if (!(fin >> u >> v >> w)) {
                cerr << "Error: Invalid file format\n";
                return false;
            }
            
            // Validation indices
            if (u < 0 || u >= n || v < 0 || v >= n) {
                cerr << "Error: Invalid vertex indices in edge " << e << "\n";
                return false;
            }
            
            inc[u][e] = 1;
            inc[v][e] = -1;
            weights[e] = abs(w);
        }
        
        fin.close();
        cout << "✓ Graph loaded from file\n";
        return validate_graph_input(n, m, s, t, inc);
    }
    
    // Mode interactif
    cout << "\n=== Graph Input ===\n";
    cout << "Type 'exit' at any time to quit\n\n";
    
    // Nombre de sommets
    while (true) {
        cout << "Number of vertices (6-19): ";
        string input;
        getline(cin, input);
        
        if (input == "exit") {
            exit_requested = true;
            return false;
        }
        
        try {
            n = stoi(input);
            if (n >= 6 && n < 20) break;
            cout << "Error: Must be between 6 and 19\n";
        } catch (...) {
            cout << "Error: Invalid number\n";
        }
    }
    
    // Nombre d'arêtes
    while (true) {
        cout << "Number of edges (6-19): ";
        string input;
        getline(cin, input);
        
        if (input == "exit") {
            exit_requested = true;
            return false;
        }
        
        try {
            m = stoi(input);
            if (m >= 6 && m < 20) break;
            cout << "Error: Must be between 6 and 19\n";
        } catch (...) {
            cout << "Error: Invalid number\n";
        }
    }
    
    // Sommets de départ et d'arrivée
    while (true) {
        cout << "Start vertex (0-" << n-1 << "): ";
        string input;
        getline(cin, input);
        
        if (input == "exit") {
            exit_requested = true;
            return false;
        }
        
        try {
            s = stoi(input);
            if (s >= 0 && s < n) break;
            cout << "Error: Must be between 0 and " << n-1 << "\n";
        } catch (...) {
            cout << "Error: Invalid number\n";
        }
    }
    
    while (true) {
        cout << "End vertex (0-" << n-1 << "): ";
        string input;
        getline(cin, input);
        
        if (input == "exit") {
            exit_requested = true;
            return false;
        }
        
        try {
            t = stoi(input);
            if (t >= 0 && t < n) break;
            cout << "Error: Must be between 0 and " << n-1 << "\n";
        } catch (...) {
            cout << "Error: Invalid number\n";
        }
    }
    
    // Initialiser matrices
    inc.assign(n, vector<int8_t>(m, 0));
    weights.assign(m, 1);
    
    // Entrer les arêtes
    cout << "\nEnter " << m << " edges (format: u v weight):\n";
    for (int e = 0; e < m; e++) {
        while (true) {
            cout << "Edge " << e << ": ";
            string line;
            getline(cin, line);
            
            if (line == "exit") {
                exit_requested = true;
                return false;
            }
            
            vector<string> tokens = split_ws(line);
            if (tokens.size() != 3) {
                cout << "Error: Need 3 numbers (u v weight)\n";
                continue;
            }
            
            try {
                int u = stoi(tokens[0]);
                int v = stoi(tokens[1]);
                int w = stoi(tokens[2]);
                
                if (u < 0 || u >= n || v < 0 || v >= n) {
                    cout << "Error: Vertex indices out of range\n";
                    continue;
                }
                
                if (u == v) {
                    cout << "Error: Self-loops not allowed\n";
                    continue;
                }
                
                inc[u][e] = 1;
                inc[v][e] = -1;
                weights[e] = abs(w);
                break;
                
            } catch (...) {
                cout << "Error: Invalid numbers\n";
            }
        }
    }
    
    return validate_graph_input(n, m, s, t, inc);
}

// ---- Communication TCP ----
bool send_tcp(const string& ip, int port, int n, int m, int s, int t,
              const vector<vector<int8_t>>& inc, const vector<int16_t>& weights) {
    
    cout << "Connecting to " << ip << ":" << port << " via TCP...\n";
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return false;
    }
    
    // Timeout
    struct timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        cerr << "Error: Invalid IP address\n";
        close(sock);
        return false;
    }
    
    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return false;
    }
    
    cout << "✓ Connected\n";
    
    // Préparer requête
    GraphRequest req{};
    req.vertices = n;
    req.edges = m;
    req.start_node = s;
    req.end_node = t;
    req.weighted = 1;
    
    // Envoyer requête
    if (send(sock, &req, sizeof(req), 0) != sizeof(req)) {
        perror("send header");
        close(sock);
        return false;
    }
    
    // Envoyer matrice d'incidence (aplatie)
    vector<int8_t> flat_inc(n * m);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            flat_inc[i * m + j] = inc[i][j];
        }
    }
    
    if (send(sock, flat_inc.data(), flat_inc.size() * sizeof(int8_t), 0) != 
        (ssize_t)(flat_inc.size() * sizeof(int8_t))) {
        perror("send matrix");
        close(sock);
        return false;
    }
    
    // Envoyer poids
    if (send(sock, weights.data(), weights.size() * sizeof(int16_t), 0) !=
        (ssize_t)(weights.size() * sizeof(int16_t))) {
        perror("send weights");
        close(sock);
        return false;
    }
    
    cout << "✓ Graph data sent\n";
    
    // Recevoir réponse
    GraphResponse resp{};
    ssize_t bytes = recv(sock, &resp, sizeof(resp), MSG_WAITALL);
    
    if (bytes != sizeof(resp)) {
        cerr << "Error: Incomplete response from server\n";
        close(sock);
        return false;
    }
    
    // Afficher résultat
    cout << "\n=== RESULT (TCP) ===\n";
    if (resp.error_code == 0) {
        cout << "Path length: " << resp.path_length << "\n";
        cout << "Path: ";
        for (int i = 0; i < resp.path_size; i++) {
            cout << resp.path[i];
            if (i < resp.path_size - 1) cout << " -> ";
        }
        cout << "\n";
    } else {
        cout << "Error: " << resp.message << "\n";
    }
    
    close(sock);
    return true;
}

// ---- Communication UDP fiable ----
bool send_udp_reliable(const string& ip, int port, int n, int m, int s, int t,
                       const vector<vector<int8_t>>& inc, const vector<int16_t>& weights) {
    
    cout << "Connecting to " << ip << ":" << port << " via UDP (reliable)...\n";
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return false;
    }
    
    // Timeout pour recvfrom
    struct timeval tv{};
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        cerr << "Error: Invalid IP address\n";
        close(sock);
        return false;
    }
    
    // Générer session ID
    string session_id_str = generate_session_id();
    uint32_t session_id = hash<string>{}(session_id_str);
    
    // Préparer données
    GraphDataUdp graph_data{};
    graph_data.vertices = n;
    graph_data.edges = m;
    graph_data.start_node = s;
    graph_data.end_node = t;
    
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            graph_data.inc_matrix[i][j] = inc[i][j];
        }
    }
    
    for (int j = 0; j < m; j++) {
        graph_data.weights[j] = weights[j];
    }
    
    // Diviser en chunks
    const size_t chunk_size = 1024;
    const uint8_t* data_ptr = reinterpret_cast<uint8_t*>(&graph_data);
    size_t total_size = sizeof(GraphDataUdp);
    size_t total_chunks = (total_size + chunk_size - 1) / chunk_size;
    
    cout << "Sending " << total_chunks << " chunk(s)...\n";
    
    // Envoyer chaque chunk avec retransmission
    for (size_t chunk_idx = 0; chunk_idx < total_chunks; chunk_idx++) {
        size_t offset = chunk_idx * chunk_size;
        size_t data_size = min(chunk_size, total_size - offset);
        
        UdpPacketHeader header{};
        header.packet_id = chunk_idx + 1;
        header.session_id = session_id;
        header.packet_type = UDP_DATA;
        header.total_chunks = total_chunks;
        header.chunk_index = chunk_idx;
        header.data_size = data_size;
        
        // Créer paquet
        vector<uint8_t> packet(sizeof(header) + data_size);
        memcpy(packet.data(), &header, sizeof(header));
        memcpy(packet.data() + sizeof(header), data_ptr + offset, data_size);
        
        // Envoyer avec retransmission
        bool ack_received = false;
        for (int attempt = 0; attempt < UDP_MAX_RETRIES && !ack_received; attempt++) {
            if (attempt > 0) {
                cout << "  Retransmitting chunk " << chunk_idx << " (attempt " << attempt+1 << ")\n";
            }
            
            // Envoyer
            ssize_t sent = sendto(sock, packet.data(), packet.size(), 0,
                                 (sockaddr*)&server_addr, sizeof(server_addr));
            
            if (sent != (ssize_t)packet.size()) {
                perror("sendto");
                continue;
            }
            
            // Attendre ACK
            UdpPacketHeader ack_header{};
            sockaddr_in from_addr{};
            socklen_t from_len = sizeof(from_addr);
            
            ssize_t recv_bytes = recvfrom(sock, &ack_header, sizeof(ack_header), 0,
                                         (sockaddr*)&from_addr, &from_len);
            
            if (recv_bytes == sizeof(ack_header) &&
                ack_header.packet_type == UDP_ACK &&
                ack_header.packet_id == header.packet_id &&
                ack_header.session_id == session_id) {
                ack_received = true;
                cout << "  ✓ Chunk " << chunk_idx << " acknowledged\n";
            }
        }
        
        if (!ack_received) {
            cerr << "Error: Failed to send chunk " << chunk_idx << " after " 
                 << UDP_MAX_RETRIES << " attempts\n";
            cerr << "Lost connection with server\n";
            close(sock);
            return false;
        }
    }
    
    // Envoyer FIN
    UdpPacketHeader fin_header{};
    fin_header.packet_id = 0;
    fin_header.session_id = session_id;
    fin_header.packet_type = UDP_FIN;
    
    sendto(sock, &fin_header, sizeof(fin_header), 0,
           (sockaddr*)&server_addr, sizeof(server_addr));
    
    cout << "✓ All data sent and acknowledged\n";
    cout << "Waiting for response...\n";
    
    // Recevoir réponse
    vector<string> response_chunks;
    bool fin_received = false;
    
    while (!fin_received) {
        UdpPacketHeader resp_header{};
        sockaddr_in from_addr{};
        socklen_t from_len = sizeof(from_addr);
        
        // Lire header
        ssize_t bytes = recvfrom(sock, &resp_header, sizeof(resp_header), MSG_PEEK,
                                (sockaddr*)&from_addr, &from_len);
        
        if (bytes == sizeof(resp_header) && resp_header.session_id == session_id) {
            if (resp_header.packet_type == UDP_DATA) {
                // Lire données complètes
                vector<uint8_t> buffer(sizeof(resp_header) + resp_header.data_size);
                bytes = recvfrom(sock, buffer.data(), buffer.size(), 0,
                                (sockaddr*)&from_addr, &from_len);
                
                if (bytes > 0) {
                    string chunk_data(reinterpret_cast<char*>(buffer.data() + sizeof(resp_header)),
                                     resp_header.data_size);
                    response_chunks.push_back(chunk_data);
                    
                    // Envoyer ACK
                    UdpPacketHeader ack{};
                    ack.packet_id = resp_header.packet_id;
                    ack.session_id = session_id;
                    ack.packet_type = UDP_ACK;
                    sendto(sock, &ack, sizeof(ack), 0,
                          (sockaddr*)&from_addr, from_len);
                }
            } 
            else if (resp_header.packet_type == UDP_FIN) {
                recvfrom(sock, &resp_header, sizeof(resp_header), 0,
                        (sockaddr*)&from_addr, &from_len);
                fin_received = true;
            }
            else if (resp_header.packet_type == UDP_ERROR) {
                recvfrom(sock, &resp_header, sizeof(resp_header), 0,
                        (sockaddr*)&from_addr, &from_len);
                cerr << "Error from server\n";
                close(sock);
                return false;
            }
        } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recvfrom");
            break;
        }
    }
    
    // Reconstruire réponse
    string full_response;
    for (const auto& chunk : response_chunks) {
        full_response += chunk;
    }
    
    // Parser réponse
    vector<string> tokens = split_ws(full_response);
    if (tokens.size() >= 3 && tokens[0] == "OK") {
        cout << "\n=== RESULT (UDP reliable) ===\n";
        cout << "Path length: " << tokens[1] << "\n";
        
        int path_size = stoi(tokens[2]);
        cout << "Path: ";
        for (int i = 0; i < path_size; i++) {
            cout << tokens[3 + i];
            if (i < path_size - 1) cout << " -> ";
        }
        cout << "\n";
    } else {
        cout << "Error: Invalid response from server\n";
        close(sock);
        return false;
    }
    
    close(sock);
    return true;
}

// ---- Main ----
int main(int argc, char* argv[]) {
    // Mode ligne de commande
    if (argc > 1) {
        string ip, filename;
        int proto, port;
        
        if (!parse_arguments(argc, argv, ip, proto, port, filename)) {
            show_usage(argv[0]);
            return 1;
        }
        
        // Obtenir données
        int n, m, s, t;
        vector<vector<int8_t>> inc;
        vector<int16_t> weights;
        
        if (!get_graph_data(n, m, s, t, inc, weights, filename)) {
            if (!exit_requested) {
                cerr << "Failed to get graph data\n";
            }
            return 0;
        }
        
        // Envoyer selon protocole
        bool success = false;
        if (proto == 1) {
            success = send_tcp(ip, port, n, m, s, t, inc, weights);
        } else if (proto == 2) {
            success = send_udp_reliable(ip, port, n, m, s, t, inc, weights);
        }
        
        if (!success) {
            cerr << "Failed to communicate with server\n";
            return 1;
        }
        
        return 0;
    }
    
    // Mode interactif
    cout << "=== Graph Theory Client (Interactive Mode) ===\n";
    cout << "Type 'exit' at any prompt to quit\n\n";
    
    while (!exit_requested) {
        // Demander données
        int n, m, s, t;
        vector<vector<int8_t>> inc;
        vector<int16_t> weights;
        
        if (!get_graph_data(n, m, s, t, inc, weights)) {
            break;
        }
        
        // Demander protocole
        int proto = 0;
        while (proto == 0 && !exit_requested) {
            cout << "\nProtocol? (1=TCP, 2=UDP, exit): ";
            string input;
            getline(cin, input);
            
            if (input == "exit") {
                exit_requested = true;
                break;
            }
            
            if (input == "1") proto = 1;
            else if (input == "2") proto = 2;
            else cout << "Invalid choice\n";
        }
        
        if (exit_requested) break;
        
        // Demander IP
        string ip;
        while (ip.empty() && !exit_requested) {
            cout << "Server IP (default: 127.0.0.1): ";
            getline(cin, ip);
            
            if (ip == "exit") {
                exit_requested = true;
                break;
            }
            
            if (ip.empty()) ip = "127.0.0.1";
        }
        
        if (exit_requested) break;
        
        // Demander port
        int port = 0;
        while (port == 0 && !exit_requested) {
            cout << "Server port: ";
            string input;
            getline(cin, input);
            
            if (input == "exit") {
                exit_requested = true;
                break;
            }
            
            try {
                port = stoi(input);
                if (port < 1 || port > 65535) {
                    cout << "Port must be 1-65535\n";
                    port = 0;
                }
            } catch (...) {
                cout << "Invalid port\n";
            }
        }
        
        if (exit_requested) break;
        
        // Envoyer
        bool success = false;
        if (proto == 1) {
            success = send_tcp(ip, port, n, m, s, t, inc, weights);
        } else if (proto == 2) {
            success = send_udp_reliable(ip, port, n, m, s, t, inc, weights);
        }
        
        if (!success) {
            cout << "Failed to communicate with server\n";
        }
        
        // Continuer?
        cout << "\nPress Enter to continue or type 'exit' to quit: ";
        string choice;
        getline(cin, choice);
        
        if (choice == "exit") {
            break;
        }
        
        cout << "\n" << string(50, '=') << "\n\n";
    }
    
    cout << "\nGoodbye!\n";
    return 0;
}
