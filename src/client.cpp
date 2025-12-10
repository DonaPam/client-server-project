#include <bits/stdc++.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <poll.h>
#include <fcntl.h>
#include <atomic>
#include "protocol.h"
using namespace std;

atomic<bool> exit_requested{false};

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

void show_usage(const char* prog) {
    cout << "Usage:\n";
    cout << "  " << prog << " --ip <IP> --port <PORT> --protocol <TCP|UDP>\n";
    cout << "  " << prog << " -i <IP> -p <PORT> -P <TCP|UDP>\n";
    cout << "  " << prog << " (interactive mode)\n\n";
    cout << "Examples:\n";
    cout << "  " << prog << " --ip 127.0.0.1 --port 1234 --protocol TCP\n";
    cout << "  " << prog << " -i 192.168.1.100 -p 8080 -P UDP\n";
}

bool parse_arguments(int argc, char* argv[], 
                     string& ip, int& proto, int& port) {
    
    if (argc != 7) {
        return false;
    }
    
    for (int i = 1; i < argc; i += 2) {
        string arg = argv[i];
        if (arg == "--ip" || arg == "-i") {
            ip = argv[i + 1];
        } else if (arg == "--port" || arg == "-p") {
            port = stoi(argv[i + 1]);
            if (port < 1 || port > 65535) return false;
        } else if (arg == "--protocol" || arg == "-P") {
            string proto_str = argv[i + 1];
            transform(proto_str.begin(), proto_str.end(), proto_str.begin(), ::toupper);
            if (proto_str == "TCP") proto = 1;
            else if (proto_str == "UDP") proto = 2;
            else return false;
        } else {
            return false;
        }
    }
    
    return port != 0 && proto != 0 && !ip.empty();
}

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
    
    for (int e = 0; e < m; e++) {
        int endpoints = 0;
        for (int v = 0; v < n; v++) {
            if (inc[v][e] != 0) endpoints++;
        }
        if (endpoints != 2) {
            cerr << "Error: edge " << e << " has " << endpoints << " endpoints\n";
            return false;
        }
    }
    
    return true;
}

string generate_session_id() {
    static mt19937_64 rng(chrono::steady_clock::now().time_since_epoch().count());
    uint64_t id = rng();
    stringstream ss;
    ss << hex << setw(16) << setfill('0') << id;
    return ss.str();
}

bool get_graph_data(int& n, int& m, int& s, int& t,
                    vector<vector<int8_t>>& inc, vector<int16_t>& weights) {
    
    cout << "\n=== Graph Input ===\n";
    cout << "Type 'exit' to quit\n\n";
    
    cout << "Data source? (1=manual, 2=file, 3=exit): ";
    string input;
    getline(cin, input);
    
    if (input == "3" || input == "exit") {
        exit_requested = true;
        return false;
    }
    
    if (input == "2") {
        cout << "File name: ";
        string filename;
        getline(cin, filename);
        
        if (filename == "exit") {
            exit_requested = true;
            return false;
        }
        
        ifstream fin(filename);
        if (!fin) {
            cerr << "Cannot open file\n";
            return false;
        }
        
        fin >> n >> m >> s >> t;
        inc.assign(n, vector<int8_t>(m, 0));
        weights.assign(m, 1);
        
        for (int e = 0; e < m; e++) {
            int u, v, w;
            if (!(fin >> u >> v >> w)) {
                cerr << "Invalid file format\n";
                return false;
            }
            
            if (u < 0 || u >= n || v < 0 || v >= n) {
                cerr << "Invalid vertex indices\n";
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
    
    if (input != "1") {
        cerr << "Invalid choice\n";
        return false;
    }
    
    while (true) {
        cout << "Number of vertices (6-19): ";
        getline(cin, input);
        
        if (input == "exit") {
            exit_requested = true;
            return false;
        }
        
        try {
            n = stoi(input);
            if (n >= 6 && n < 20) break;
            cout << "Must be between 6 and 19\n";
        } catch (...) {
            cout << "Invalid number\n";
        }
    }
    
    while (true) {
        cout << "Number of edges (6-19): ";
        getline(cin, input);
        
        if (input == "exit") {
            exit_requested = true;
            return false;
        }
        
        try {
            m = stoi(input);
            if (m >= 6 && m < 20) break;
            cout << "Must be between 6 and 19\n";
        } catch (...) {
            cout << "Invalid number\n";
        }
    }
    
    while (true) {
        cout << "Start vertex (0-" << n-1 << "): ";
        getline(cin, input);
        
        if (input == "exit") {
            exit_requested = true;
            return false;
        }
        
        try {
            s = stoi(input);
            if (s >= 0 && s < n) break;
            cout << "Must be between 0 and " << n-1 << "\n";
        } catch (...) {
            cout << "Invalid number\n";
        }
    }
    
    while (true) {
        cout << "End vertex (0-" << n-1 << "): ";
        getline(cin, input);
        
        if (input == "exit") {
            exit_requested = true;
            return false;
        }
        
        try {
            t = stoi(input);
            if (t >= 0 && t < n) break;
            cout << "Must be between 0 and " << n-1 << "\n";
        } catch (...) {
            cout << "Invalid number\n";
        }
    }
    
    inc.assign(n, vector<int8_t>(m, 0));
    weights.assign(m, 1);
    
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
                cout << "Need 3 numbers (u v weight)\n";
                continue;
            }
            
            try {
                int u = stoi(tokens[0]);
                int v = stoi(tokens[1]);
                int w = stoi(tokens[2]);
                
                if (u < 0 || u >= n || v < 0 || v >= n) {
                    cout << "Vertex indices out of range\n";
                    continue;
                }
                
                if (u == v) {
                    cout << "Self-loops not allowed\n";
                    continue;
                }
                
                inc[u][e] = 1;
                inc[v][e] = -1;
                weights[e] = abs(w);
                break;
                
            } catch (...) {
                cout << "Invalid numbers\n";
            }
        }
    }
    
    return validate_graph_input(n, m, s, t, inc);
}

bool send_tcp(const string& ip, int port, int n, int m, int s, int t,
              const vector<vector<int8_t>>& inc, const vector<int16_t>& weights) {
    
    cout << "Connecting to " << ip << ":" << port << " via TCP...\n";
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return false;
    }
    
    struct timeval tv{};
    tv.tv_sec = 5;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        cerr << "Invalid IP address\n";
        close(sock);
        return false;
    }
    
    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return false;
    }
    
    cout << "✓ Connected\n";
    
    GraphRequest req{};
    req.vertices = n;
    req.edges = m;
    req.start_node = s;
    req.end_node = t;
    req.weighted = 1;
    
    if (send(sock, &req, sizeof(req), 0) != sizeof(req)) {
        perror("send header");
        close(sock);
        return false;
    }
    
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
    
    if (send(sock, weights.data(), weights.size() * sizeof(int16_t), 0) !=
        (ssize_t)(weights.size() * sizeof(int16_t))) {
        perror("send weights");
        close(sock);
        return false;
    }
    
    cout << "✓ Graph data sent\n";
    
    GraphResponse resp{};
    ssize_t bytes = recv(sock, &resp, sizeof(resp), MSG_WAITALL);
    
    if (bytes != sizeof(resp)) {
        cerr << "Incomplete response\n";
        close(sock);
        return false;
    }
    
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

bool send_udp_reliable(const string& ip, int port, int n, int m, int s, int t,
                       const vector<vector<int8_t>>& inc, const vector<int16_t>& weights) {
    
    cout << "Connecting to " << ip << ":" << port << " via UDP (reliable)...\n";
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return false;
    }
    
    struct timeval tv{};
    tv.tv_sec = UDP_TIMEOUT_MS / 1000;
    tv.tv_usec = (UDP_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        cerr << "Invalid IP address\n";
        close(sock);
        return false;
    }
    
    string session_id_str = generate_session_id();
    hash<string> hasher;
    uint32_t session_id = static_cast<uint32_t>(hasher(session_id_str));
    
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
    
    const size_t chunk_size = 1024;
    const uint8_t* data_ptr = (uint8_t*)&graph_data;
    size_t total_size = sizeof(GraphDataUdp);
    
    cout << "Sending data...\n";
    
    UdpPacketHeader header{};
    header.packet_id = 1;
    header.session_id = session_id;
    header.packet_type = UDP_DATA;
    header.total_chunks = 1;
    header.chunk_index = 0;
    header.data_size = total_size;
    
    vector<uint8_t> packet(sizeof(header) + total_size);
    memcpy(packet.data(), &header, sizeof(header));
    memcpy(packet.data() + sizeof(header), data_ptr, total_size);
    
    bool ack_received = false;
    for (int attempt = 0; attempt < UDP_MAX_RETRIES && !ack_received; attempt++) {
        if (attempt > 0) {
            cout << "  Retransmitting (attempt " << attempt+1 << ")\n";
        }
        
        ssize_t sent = sendto(sock, packet.data(), packet.size(), 0,
                             (sockaddr*)&server_addr, sizeof(server_addr));
        
        if (sent != (ssize_t)packet.size()) {
            perror("sendto");
            continue;
        }
        
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
            cout << "  ✓ Acknowledged\n";
        }
    }
    
    if (!ack_received) {
        cerr << "Error: Failed after " << UDP_MAX_RETRIES << " attempts\n";
        cerr << "Lost connection with server\n";
        close(sock);
        return false;
    }
    
    UdpPacketHeader fin_header{};
    fin_header.packet_id = 0;
    fin_header.session_id = session_id;
    fin_header.packet_type = UDP_FIN;
    
    sendto(sock, &fin_header, sizeof(fin_header), 0,
           (sockaddr*)&server_addr, sizeof(server_addr));
    
    cout << "✓ All data sent\n";
    cout << "Waiting for response...\n";
    
    string full_response;
    bool fin_received = false;
    
    while (!fin_received) {
        UdpPacketHeader resp_header{};
        sockaddr_in from_addr{};
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t bytes = recvfrom(sock, &resp_header, sizeof(resp_header), 0,
                                (sockaddr*)&from_addr, &from_len);
        
        if (bytes == sizeof(resp_header) && resp_header.session_id == session_id) {
            if (resp_header.packet_type == UDP_DATA) {
                vector<uint8_t> data(resp_header.data_size);
                bytes = recvfrom(sock, data.data(), data.size(), 0,
                                (sockaddr*)&from_addr, &from_len);
                
                if (bytes > 0) {
                    full_response.append((char*)data.data(), data.size());
                    
                    UdpPacketHeader ack{};
                    ack.packet_id = resp_header.packet_id;
                    ack.session_id = session_id;
                    ack.packet_type = UDP_ACK;
                    sendto(sock, &ack, sizeof(ack), 0,
                          (sockaddr*)&from_addr, from_len);
                }
            } 
            else if (resp_header.packet_type == UDP_FIN) {
                fin_received = true;
            }
            else if (resp_header.packet_type == UDP_ERROR) {
                cerr << "Error from server\n";
                close(sock);
                return false;
            }
        } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recvfrom");
            break;
        }
    }
    
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
        cout << "Invalid response from server\n";
        close(sock);
        return false;
    }
    
    close(sock);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc == 7) {
        string ip;
        int proto, port;
        
        if (!parse_arguments(argc, argv, ip, proto, port)) {
            show_usage(argv[0]);
            return 1;
        }
        
        int n, m, s, t;
        vector<vector<int8_t>> inc;
        vector<int16_t> weights;
        
        if (!get_graph_data(n, m, s, t, inc, weights)) {
            if (!exit_requested) cerr << "Failed to get graph data\n";
            return 0;
        }
        
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
    else if (argc == 1) {
        cout << "=== Graph Theory Client (Interactive Mode) ===\n";
        cout << "Type 'exit' to quit\n\n";
        
        while (!exit_requested) {
            int n, m, s, t;
            vector<vector<int8_t>> inc;
            vector<int16_t> weights;
            
            if (!get_graph_data(n, m, s, t, inc, weights)) {
                break;
            }
            
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
            
            bool success = false;
            if (proto == 1) {
                success = send_tcp(ip, port, n, m, s, t, inc, weights);
            } else if (proto == 2) {
                success = send_udp_reliable(ip, port, n, m, s, t, inc, weights);
            }
            
            if (!success) {
                cout << "Failed to communicate with server\n";
            }
            
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
    else {
        show_usage(argv[0]);
        return 1;
    }
}
