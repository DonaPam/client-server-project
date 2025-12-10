// Compile: g++ server.cpp -o server -std=c++17 -pthread
#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include "protocol.h"
using namespace std;

// ---- Variables globales pour gestion propre ----
atomic<bool> server_running{true};
mutex log_mutex;

void log_msg(const string& msg) {
    lock_guard<mutex> lock(log_mutex);
    cout << "[SERVER] " << msg << endl;
}

// ---- Signal handler ----
void signal_handler(int sig) {
    log_msg("Signal " + to_string(sig) + " received, shutting down...");
    server_running = false;
}

// ---- Conditions de validation ----
bool valid_graph_params(int n, int m, int s, int t) {
    if (n < 6 || n >= 20 || m < 6 || m >= 20) {
        log_msg("Invalid n/m: " + to_string(n) + "/" + to_string(m));
        return false;
    }
    if (s < 0 || s >= n || t < 0 || t >= n) {
        log_msg("Invalid start/end: " + to_string(s) + "/" + to_string(t));
        return false;
    }
    return true;
}

bool validate_incidence_matrix(int n, int m, const vector<vector<int8_t>>& inc) {
    // Vérifie que chaque arête a exactement 2 sommets non nuls
    for (int e = 0; e < m; e++) {
        int count = 0;
        for (int v = 0; v < n; v++) {
            if (inc[v][e] != 0) count++;
        }
        if (count != 2) {
            log_msg("Edge " + to_string(e) + " has " + to_string(count) + " endpoints (expected 2)");
            return false;
        }
    }
    return true;
}

// ---- Split ----
vector<string> split_ws(const string& s) {
    vector<string> out;
    string token;
    for (char c : s) {
        if (isspace(c)) {
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

// ---- Dijkstra ----
struct PathResult {
    long long dist;
    vector<int> path;
    bool ok;
};

PathResult dijkstra(int n, const vector<vector<pair<int,int>>>& adj, int S, int T) {
    const long long INF = 1e18;
    vector<long long> dist(n, INF);
    vector<int> parent(n, -1);
    dist[S] = 0;
    
    priority_queue<pair<long long,int>, vector<pair<long long,int>>, greater<>> pq;
    pq.push({0, S});
    
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d != dist[u]) continue;
        if (u == T) break;
        
        for (const auto& [v, w] : adj[u]) {
            if (dist[v] > dist[u] + w) {
                dist[v] = dist[u] + w;
                parent[v] = u;
                pq.push({dist[v], v});
            }
        }
    }
    
    PathResult result;
    if (dist[T] == INF) {
        result.ok = false;
        return result;
    }
    
    result.ok = true;
    result.dist = dist[T];
    int cur = T;
    while (cur != -1) {
        result.path.push_back(cur);
        cur = parent[cur];
    }
    reverse(result.path.begin(), result.path.end());
    return result;
}

// ---- Build graph ----
vector<vector<pair<int,int>>> build_adj(int n, int m, 
                                        const vector<vector<int8_t>>& inc,
                                        const vector<int16_t>& weights) {
    vector<vector<pair<int,int>>> adj(n);
    
    for (int e = 0; e < m; e++) {
        int u = -1, v = -1;
        for (int vertex = 0; vertex < n; vertex++) {
            if (inc[vertex][e] != 0) {
                if (u == -1) u = vertex;
                else if (v == -1) v = vertex;
            }
        }
        
        if (u != -1 && v != -1) {
            int w = abs(weights[e]);
            adj[u].push_back({v, w});
            adj[v].push_back({u, w});
        }
    }
    
    return adj;
}

// ---- TCP Client Handler avec multiplexage ----
void handle_tcp_client(int client_fd, const sockaddr_in& client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    
    log_msg("TCP client connected from " + string(client_ip) + ":" + to_string(client_port));
    
    // Recevoir la requête
    GraphRequest req;
    ssize_t bytes = recv(client_fd, &req, sizeof(req), MSG_WAITALL);
    
    if (bytes != sizeof(req)) {
        log_msg("TCP client " + string(client_ip) + " sent invalid request");
        close(client_fd);
        return;
    }
    
    // Validation
    if (!valid_graph_params(req.vertices, req.edges, req.start_node, req.end_node)) {
        GraphResponse resp{};
        resp.error_code = 1;
        resp.path_length = -1;
        strcpy(resp.message, "Invalid graph parameters");
        send(client_fd, &resp, sizeof(resp), 0);
        close(client_fd);
        return;
    }
    
    int n = req.vertices, m = req.edges;
    vector<vector<int8_t>> inc(n, vector<int8_t>(m, 0));
    vector<int16_t> weights(m, 1);
    
    // Recevoir matrice d'incidence
    bytes = recv(client_fd, inc[0].data(), n * m * sizeof(int8_t), MSG_WAITALL);
    if (bytes != (ssize_t)(n * m * sizeof(int8_t))) {
        log_msg("Failed to receive incidence matrix from " + string(client_ip));
        close(client_fd);
        return;
    }
    
    // Recevoir poids
    bytes = recv(client_fd, weights.data(), m * sizeof(int16_t), MSG_WAITALL);
    if (bytes != (ssize_t)(m * sizeof(int16_t))) {
        log_msg("Failed to receive weights from " + string(client_ip));
        close(client_fd);
        return;
    }
    
    // Validation de la matrice
    if (!validate_incidence_matrix(n, m, inc)) {
        GraphResponse resp{};
        resp.error_code = 1;
        resp.path_length = -1;
        strcpy(resp.message, "Invalid incidence matrix");
        send(client_fd, &resp, sizeof(resp), 0);
        close(client_fd);
        return;
    }
    
    // Construction et calcul
    auto adj = build_adj(n, m, inc, weights);
    auto result = dijkstra(n, adj, req.start_node, req.end_node);
    
    // Préparer réponse
    GraphResponse resp{};
    if (!result.ok) {
        resp.error_code = 1;
        resp.path_length = -1;
        resp.path_size = 0;
        strcpy(resp.message, "No path found");
    } else {
        resp.error_code = 0;
        resp.path_length = (int)result.dist;
        resp.path_size = min((int)result.path.size(), MAX_VERTICES);
        for (int i = 0; i < resp.path_size; i++) {
            resp.path[i] = result.path[i];
        }
        strcpy(resp.message, "OK");
    }
    
    // Envoyer réponse
    send(client_fd, &resp, sizeof(resp), 0);
    log_msg("TCP response sent to " + string(client_ip));
    
    close(client_fd);
}

// ---- UDP Handler fiable ----
bool send_udp_ack(int udp_fd, const sockaddr_in& client_addr, uint32_t packet_id, uint32_t session_id) {
    UdpPacketHeader ack_header{};
    ack_header.packet_id = packet_id;
    ack_header.session_id = session_id;
    ack_header.packet_type = UDP_ACK;
    ack_header.total_chunks = 1;
    ack_header.chunk_index = 0;
    ack_header.data_size = 0;
    
    return sendto(udp_fd, &ack_header, sizeof(ack_header), 0,
                  (const sockaddr*)&client_addr, sizeof(client_addr)) > 0;
}

void handle_udp_session(int udp_fd, const string& session_id_str, 
                        const vector<vector<uint8_t>>& data_chunks,
                        const sockaddr_in& client_addr) {
    
    uint32_t session_id = stoul(session_id_str);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    
    // Reconstruire les données
    vector<uint8_t> full_data;
    for (const auto& chunk : data_chunks) {
        full_data.insert(full_data.end(), chunk.begin(), chunk.end());
    }
    
    if (full_data.size() < sizeof(GraphDataUdp)) {
        log_msg("UDP incomplete data from " + string(client_ip));
        return;
    }
    
    GraphDataUdp* graph_data = reinterpret_cast<GraphDataUdp*>(full_data.data());
    
    // Validation
    if (!valid_graph_params(graph_data->vertices, graph_data->edges, 
                           graph_data->start_node, graph_data->end_node)) {
        UdpPacketHeader error_header{};
        error_header.packet_id = 0;
        error_header.session_id = session_id;
        error_header.packet_type = UDP_ERROR;
        error_header.data_size = 0;
        sendto(udp_fd, &error_header, sizeof(error_header), 0,
               (const sockaddr*)&client_addr, sizeof(client_addr));
        return;
    }
    
    int n = graph_data->vertices;
    int m = graph_data->edges;
    
    // Convertir en format interne
    vector<vector<int8_t>> inc(n, vector<int8_t>(m));
    vector<int16_t> weights(m);
    
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            inc[i][j] = graph_data->inc_matrix[i][j];
        }
    }
    
    for (int j = 0; j < m; j++) {
        weights[j] = graph_data->weights[j];
    }
    
    // Validation
    if (!validate_incidence_matrix(n, m, inc)) {
        UdpPacketHeader error_header{};
        error_header.packet_id = 0;
        error_header.session_id = session_id;
        error_header.packet_type = UDP_ERROR;
        sendto(udp_fd, &error_header, sizeof(error_header), 0,
               (const sockaddr*)&client_addr, sizeof(client_addr));
        return;
    }
    
    // Calcul
    auto adj = build_adj(n, m, inc, weights);
    auto result = dijkstra(n, adj, graph_data->start_node, graph_data->end_node);
    
    // Préparer réponse
    if (!result.ok) {
        UdpPacketHeader error_header{};
        error_header.packet_id = 0;
        error_header.session_id = session_id;
        error_header.packet_type = UDP_ERROR;
        strcpy((char*)error_header.reserved, "No path");
        sendto(udp_fd, &error_header, sizeof(error_header), 0,
               (const sockaddr*)&client_addr, sizeof(client_addr));
        return;
    }
    
    // Envoyer réponse en chunks
    string response_data = "OK " + to_string(result.dist) + " " + 
                          to_string(result.path.size());
    for (int v : result.path) {
        response_data += " " + to_string(v);
    }
    
    const size_t chunk_size = 512;
    size_t total_chunks = (response_data.size() + chunk_size - 1) / chunk_size;
    
    for (size_t i = 0; i < total_chunks; i++) {
        UdpPacketHeader resp_header{};
        resp_header.packet_id = i + 1;
        resp_header.session_id = session_id;
        resp_header.packet_type = UDP_DATA;
        resp_header.total_chunks = total_chunks;
        resp_header.chunk_index = i;
        
        size_t offset = i * chunk_size;
        size_t size = min(chunk_size, response_data.size() - offset);
        resp_header.data_size = size;
        
        // Envoyer header + données
        vector<uint8_t> packet(sizeof(resp_header) + size);
        memcpy(packet.data(), &resp_header, sizeof(resp_header));
        memcpy(packet.data() + sizeof(resp_header), 
               response_data.data() + offset, size);
        
        sendto(udp_fd, packet.data(), packet.size(), 0,
               (const sockaddr*)&client_addr, sizeof(client_addr));
    }
    
    // Envoyer FIN
    UdpPacketHeader fin_header{};
    fin_header.packet_id = 0;
    fin_header.session_id = session_id;
    fin_header.packet_type = UDP_FIN;
    sendto(udp_fd, &fin_header, sizeof(fin_header), 0,
           (const sockaddr*)&client_addr, sizeof(client_addr));
    
    log_msg("UDP response sent to " + string(client_ip));
}

// ---- Main Server ----
int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <port>\n";
        return 1;
    }
    
    int port = stoi(argv[1]);
    
    // Configuration des signaux
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Création sockets
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (tcp_fd < 0 || udp_fd < 0) {
        perror("socket");
        return 1;
    }
    
    // Options de socket
    int opt = 1;
    setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configuration adresse
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(tcp_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0 ||
        bind(udp_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(tcp_fd, 10) < 0) {
        perror("listen");
        return 1;
    }
    
    // Mode non-bloquant pour UDP
    fcntl(udp_fd, F_SETFL, O_NONBLOCK);
    
    log_msg("Server started on port " + to_string(port) + " (TCP+UDP)");
    log_msg("Press Ctrl+C to stop");
    
    // Structures pour multiplexage
    struct pollfd fds[2];
    fds[0].fd = tcp_fd;
    fds[0].events = POLLIN;
    fds[1].fd = udp_fd;
    fds[1].events = POLLIN;
    
    // Structures pour sessions UDP
    unordered_map<string, pair<sockaddr_in, vector<vector<uint8_t>>>> udp_sessions;
    unordered_map<string, chrono::steady_clock::time_point> session_timeouts;
    mutex udp_mutex;
    
    // Thread pool pour TCP clients (max 10 threads simultanés)
    vector<thread> tcp_threads;
    mutex threads_mutex;
    
    // Boucle principale
    while (server_running) {
        int ready = poll(fds, 2, 100); // Timeout 100ms
        
        if (ready < 0 && errno != EINTR) {
            perror("poll");
            break;
        }
        
        // Accepter connexions TCP
        if (fds[0].revents & POLLIN) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(tcp_fd, (sockaddr*)&client_addr, &client_len);
            
            if (client_fd >= 0) {
                // Nettoyer les threads terminés
                {
                    lock_guard<mutex> lock(threads_mutex);
                    tcp_threads.erase(
                        remove_if(tcp_threads.begin(), tcp_threads.end(),
                                  [](thread& t) { return !t.joinable(); }),
                        tcp_threads.end()
                    );
                    
                    // Limiter à 10 threads simultanés
                    if (tcp_threads.size() < 10) {
                        tcp_threads.emplace_back([client_fd, client_addr]() {
                            handle_tcp_client(client_fd, client_addr);
                        });
                    } else {
                        log_msg("Too many TCP clients, rejecting connection");
                        close(client_fd);
                    }
                }
            }
        }
        
        // Traiter données UDP
        if (fds[1].revents & POLLIN) {
            UdpPacketHeader header;
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            
            // Lire header
            ssize_t bytes = recvfrom(udp_fd, &header, sizeof(header), MSG_PEEK,
                                    (sockaddr*)&client_addr, &client_len);
            
            if (bytes == sizeof(header)) {
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                string session_key = string(client_ip) + ":" + 
                                   to_string(ntohs(client_addr.sin_port)) + ":" +
                                   to_string(header.session_id);
                
                // Envoyer ACK immédiatement
                if (header.packet_type == UDP_DATA) {
                    send_udp_ack(udp_fd, client_addr, header.packet_id, header.session_id);
                    
                    // Lire données complètes
                    vector<uint8_t> buffer(sizeof(header) + header.data_size);
                    bytes = recvfrom(udp_fd, buffer.data(), buffer.size(), 0,
                                    (sockaddr*)&client_addr, &client_len);
                    
                    if (bytes > 0) {
                        lock_guard<mutex> lock(udp_mutex);
                        
                        // Stocker données
                        vector<uint8_t> data(buffer.begin() + sizeof(header), buffer.end());
                        if (udp_sessions.find(session_key) == udp_sessions.end()) {
                            udp_sessions[session_key] = {client_addr, {}};
                        }
                        udp_sessions[session_key].second.push_back(data);
                        session_timeouts[session_key] = chrono::steady_clock::now();
                        
                        // Si FIN reçu, traiter la session
                        if (header.packet_type == UDP_FIN) {
                            thread([udp_fd, session_key, &udp_sessions, &udp_mutex]() {
                                lock_guard<mutex> lock(udp_mutex);
                                if (udp_sessions.find(session_key) != udp_sessions.end()) {
                                    auto& session = udp_sessions[session_key];
                                    handle_udp_session(udp_fd, 
                                                      session_key.substr(session_key.find_last_of(':') + 1),
                                                      session.second, session.first);
                                    udp_sessions.erase(session_key);
                                }
                            }).detach();
                        }
                    }
                }
            }
            
            // Nettoyer les sessions UDP expirées (> 30 secondes)
            auto now = chrono::steady_clock::now();
            lock_guard<mutex> lock(udp_mutex);
            for (auto it = session_timeouts.begin(); it != session_timeouts.end();) {
                if (chrono::duration_cast<chrono::seconds>(now - it->second).count() > 30) {
                    udp_sessions.erase(it->first);
                    it = session_timeouts.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    
    // Nettoyage
    log_msg("Shutting down server...");
    
    // Fermer sockets
    close(tcp_fd);
    close(udp_fd);
    
    // Attendre threads TCP
    {
        lock_guard<mutex> lock(threads_mutex);
        for (auto& t : tcp_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
    
    log_msg("Server stopped");
    return 0;
}
