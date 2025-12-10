// server.cpp - Version corrigée
// Compile: g++ server.cpp -o server -std=c++17 -pthread

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <atomic>
#include "protocol.h"
using namespace std;

// ==================== VARIABLES GLOBALES ====================
atomic<bool> running(true);
mutex client_count_mutex;
int active_clients = 0;
const int MAX_CLIENTS = 10;  // Plus que 3 comme exigé

// ==================== FONCTIONS UTILITAIRES ====================
vector<string> split_ws(const string &s) {
    vector<string> out;
    string t;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!t.empty()) {
                out.push_back(t);
                t.clear();
            }
        } else {
            t.push_back(c);
        }
    }
    if (!t.empty()) out.push_back(t);
    return out;
}

bool valid_graph_params(int n, int m) {
    return (n >= MIN_VERTICES && n <= MAX_VERTICES && 
            m >= MIN_VERTICES && m <= MAX_EDGES);
}

// ==================== DIJKSTRA (poids non-négatifs) ====================
struct PathResult {
    long long dist;
    vector<int> path;
    bool ok;
    
    PathResult() : dist(LLONG_MAX), ok(false) {}
};

PathResult dijkstra(int n, vector<vector<pair<int, int>>>& adj, int S, int T) {
    const long long INF = LLONG_MAX / 2;
    vector<long long> dist(n, INF);
    vector<int> parent(n, -1);
    vector<bool> visited(n, false);
    
    dist[S] = 0;
    
    // Priority queue avec paires (distance, sommet)
    using pii = pair<long long, int>;
    priority_queue<pii, vector<pii>, greater<pii>> pq;
    pq.push({0, S});
    
    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        
        if (visited[u]) continue;
        visited[u] = true;
        
        if (u == T) break;
        
        for (auto &edge : adj[u]) {
            int v = edge.first;
            int w = edge.second;
            
            if (!visited[v] && dist[u] + w < dist[v]) {
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
    
    // Reconstruire chemin
    int node = T;
    while (node != -1) {
        result.path.push_back(node);
        node = parent[node];
    }
    reverse(result.path.begin(), result.path.end());
    
    return result;
}

// ==================== CONSTRUCTION GRAPHE ====================
vector<vector<pair<int, int>>> build_adjacency(int n, int m, 
                                              vector<int>& inc_mat, 
                                              vector<int>& weights) {
    vector<vector<pair<int, int>>> adj(n);
    
    for (int e = 0; e < m; e++) {
        int u = -1, v = -1;
        
        // Trouver les deux sommets connectés par cette arête
        for (int vertex = 0; vertex < n; vertex++) {
            int val = inc_mat[vertex * m + e];
            if (val != 0) {
                if (u == -1) {
                    u = vertex;
                } else if (v == -1) {
                    v = vertex;
                    break;
                }
            }
        }
        
        if (u != -1 && v != -1 && weights[e] >= 0) {
            // Graphe non-orienté, arête dans les deux directions
            adj[u].push_back({v, weights[e]});
            adj[v].push_back({u, weights[e]});
        }
    }
    
    return adj;
}

// ==================== GESTIONNAIRE TCP ====================
void handle_tcp_client(int client_fd, sockaddr_in client_addr) {
    {
        lock_guard<mutex> lock(client_count_mutex);
        if (active_clients >= MAX_CLIENTS) {
            cerr << "Refusé: trop de clients (" << active_clients << "/" 
                 << MAX_CLIENTS << ")" << endl;
            close(client_fd);
            return;
        }
        active_clients++;
    }
    
    cout << "[TCP] Client connecté: " << inet_ntoa(client_addr.sin_addr) 
         << ":" << ntohs(client_addr.sin_port) 
         << " (" << active_clients << " clients actifs)" << endl;
    
    // Recevoir requête
    GraphRequest req;
    ssize_t bytes = recv(client_fd, &req, sizeof(req), MSG_WAITALL);
    
    if (bytes != sizeof(req)) {
        cerr << "[TCP] Requête incomplète" << endl;
        close(client_fd);
        {
            lock_guard<mutex> lock(client_count_mutex);
            active_clients--;
        }
        return;
    }
    
    // Préparer réponse
    GraphResponse resp;
    resp.error_code = 1; // Erreur par défaut
    
    // Valider paramètres
    if (!valid_graph_params(req.vertices, req.edges)) {
        snprintf(resp.message, sizeof(resp.message), 
                "Paramètres invalides: n=%d (min=%d,max=%d), m=%d (min=%d,max=%d)",
                req.vertices, MIN_VERTICES, MAX_VERTICES,
                req.edges, MIN_VERTICES, MAX_EDGES);
        send(client_fd, &resp, sizeof(resp), 0);
        close(client_fd);
        {
            lock_guard<mutex> lock(client_count_mutex);
            active_clients--;
        }
        return;
    }
    
    int n = req.vertices;
    int m = req.edges;
    
    // Recevoir matrice d'incidence
    vector<int> inc_mat(n * m);
    bytes = recv(client_fd, inc_mat.data(), inc_mat.size() * sizeof(int), MSG_WAITALL);
    
    if (bytes != (ssize_t)(inc_mat.size() * sizeof(int))) {
        strcpy(resp.message, "Erreur réception matrice");
        send(client_fd, &resp, sizeof(resp), 0);
        close(client_fd);
        {
            lock_guard<mutex> lock(client_count_mutex);
            active_clients--;
        }
        return;
    }
    
    // Recevoir poids
    vector<int> weights(m);
    bytes = recv(client_fd, weights.data(), weights.size() * sizeof(int), MSG_WAITALL);
    
    if (bytes != (ssize_t)(weights.size() * sizeof(int))) {
        strcpy(resp.message, "Erreur réception poids");
        send(client_fd, &resp, sizeof(resp), 0);
        close(client_fd);
        {
            lock_guard<mutex> lock(client_count_mutex);
            active_clients--;
        }
        return;
    }
    
    // Vérifier poids non-négatifs
    bool valid_weights = true;
    for (int w : weights) {
        if (w < 0) {
            valid_weights = false;
            break;
        }
    }
    
    if (!valid_weights) {
        strcpy(resp.message, "Poids négatifs non autorisés");
        send(client_fd, &resp, sizeof(resp), 0);
        close(client_fd);
        {
            lock_guard<mutex> lock(client_count_mutex);
            active_clients--;
        }
        return;
    }
    
    // Construire graphe et exécuter Dijkstra
    auto adj = build_adjacency(n, m, inc_mat, weights);
    auto result = dijkstra(n, adj, req.start_node, req.end_node);
    
    if (!result.ok) {
        resp.error_code = 2;
        resp.path_length = -1;
        resp.path_size = 0;
        strcpy(resp.message, "Pas de chemin trouvé");
    } else {
        resp.error_code = 0;
        resp.path_length = result.dist;
        resp.path_size = min((size_t)128, result.path.size());
        
        for (size_t i = 0; i < resp.path_size; i++) {
            resp.path[i] = result.path[i];
        }
        
        strcpy(resp.message, "OK");
    }
    
    // Envoyer réponse
    send(client_fd, &resp, sizeof(resp), 0);
    close(client_fd);
    
    {
        lock_guard<mutex> lock(client_count_mutex);
        active_clients--;
    }
    
    cout << "[TCP] Client déconnecté (" << active_clients << " clients restants)" << endl;
}

// ==================== GESTIONNAIRE UDP ====================
class UDPServer {
private:
    int udp_socket;
    unordered_map<string, pair<vector<string>, sockaddr_in>> client_data;
    mutex data_mutex;
    
    void send_ack(const string& client_id, const sockaddr_in& client_addr) {
        UdpDataPacket ack;
        memcpy(ack.header.client_id, client_id.c_str(), 16);
        ack.header.packet_type = UDP_ACK;
        ack.header.seq_num = 0;
        ack.header.total_packets = 1;
        ack.header.current_packet = 0;
        
        sendto(udp_socket, &ack, sizeof(UdpHeader), 0,
               (const sockaddr*)&client_addr, sizeof(client_addr));
    }
    
    void process_client_data(const string& client_id) {
        lock_guard<mutex> lock(data_mutex);
        
        if (client_data.find(client_id) == client_data.end()) {
            return;
        }
        
        auto& data = client_data[client_id];
        auto& messages = data.first;
        sockaddr_in client_addr = data.second;
        
        // Traiter les messages
        int n = 0, m = 0, s = 0, t = 0;
        vector<vector<int>> inc_mat;
        vector<int> weights;
        
        for (const string& msg : messages) {
            auto parts = split_ws(msg);
            if (parts.size() < 2) continue;
            
            if (parts[1] == "HEADER" && parts.size() >= 6) {
                n = stoi(parts[2]);
                m = stoi(parts[3]);
                s = stoi(parts[4]);
                t = stoi(parts[5]);
                inc_mat.resize(n, vector<int>(m, 0));
            } 
            else if (parts[1] == "ROW") {
                int row_idx = stoi(parts[0]); // Première partie = numéro de ligne
                if (row_idx >= 0 && row_idx < n) {
                    for (int j = 0; j < m && j + 2 < (int)parts.size(); j++) {
                        inc_mat[row_idx][j] = stoi(parts[j + 2]);
                    }
                }
            }
            else if (parts[1] == "WEIGHTS") {
                for (size_t i = 2; i < parts.size(); i++) {
                    weights.push_back(stoi(parts[i]));
                }
            }
        }
        
        // Vérifier données complètes
        if (n == 0 || m == 0 || weights.size() != (size_t)m) {
            string error_msg = "SERVER_RESPONSE ERROR Données incomplètes";
            sendto(udp_socket, error_msg.c_str(), error_msg.size(), 0,
                   (const sockaddr*)&client_addr, sizeof(client_addr));
            client_data.erase(client_id);
            return;
        }
        
        // Valider paramètres
        if (!valid_graph_params(n, m)) {
            string error_msg = "SERVER_RESPONSE ERROR Paramètres hors limites";
            sendto(udp_socket, error_msg.c_str(), error_msg.size(), 0,
                   (const sockaddr*)&client_addr, sizeof(client_addr));
            client_data.erase(client_id);
            return;
        }
        
        // Convertir matrice 2D en 1D
        vector<int> flat_mat(n * m);
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < m; j++) {
                flat_mat[i * m + j] = inc_mat[i][j];
            }
        }
        
        // Exécuter Dijkstra
        auto adj = build_adjacency(n, m, flat_mat, weights);
        auto result = dijkstra(n, adj, s, t);
        
        // Préparer réponse
        string response;
        if (!result.ok) {
            response = "SERVER_RESPONSE ERROR Pas de chemin";
        } else {
            response = "SERVER_RESPONSE OK " + to_string(result.dist) + 
                      " " + to_string(result.path.size());
            for (int v : result.path) {
                response += " " + to_string(v);
            }
        }
        
        // Envoyer réponse
        sendto(udp_socket, response.c_str(), response.size(), 0,
               (const sockaddr*)&client_addr, sizeof(client_addr));
        
        // Nettoyer
        client_data.erase(client_id);
    }
    
public:
    UDPServer(int port) {
        udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket < 0) {
            perror("socket UDP");
            exit(1);
        }
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        
        if (bind(udp_socket, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind UDP");
            close(udp_socket);
            exit(1);
        }
        
        cout << "[UDP] Socket lié au port " << port << endl;
    }
    
    void run() {
        while (running) {
            char buffer[2048];
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            ssize_t bytes = recvfrom(udp_socket, buffer, sizeof(buffer) - 1, 0,
                                    (sockaddr*)&client_addr, &client_len);
            
            if (bytes <= 0) continue;
            
            buffer[bytes] = '\0';
            string message(buffer);
            auto parts = split_ws(message);
            
            if (parts.size() < 2) continue;
            
            string client_id = parts[0];
            string packet_type = parts[1];
            
            cout << "[UDP] Paquet de " << inet_ntoa(client_addr.sin_addr) 
                 << ":" << ntohs(client_addr.sin_port) 
                 << " Type: " << packet_type << endl;
            
            // Envoyer ACK immédiatement
            send_ack(client_id, client_addr);
            
            {
                lock_guard<mutex> lock(data_mutex);
                client_data[client_id].first.push_back(message);
                client_data[client_id].second = client_addr;
                
                // Si FIN reçu, traiter
                if (packet_type == "FIN") {
                    thread([this, client_id]() {
                        this->process_client_data(client_id);
                    }).detach();
                }
            }
        }
    }
    
    ~UDPServer() {
        if (udp_socket >= 0) close(udp_socket);
    }
};

// ==================== GESTION SIGNALS ====================
void signal_handler(int sig) {
    cout << "\nSignal " << sig << " reçu, arrêt du serveur..." << endl;
    running = false;
}

// ==================== MAIN ====================
int main(int argc, char* argv[]) {
    if (argc != 2) {
        cout << "Usage: ./server <port>" << endl;
        return 1;
    }
    
    int port = stoi(argv[1]);
    
    // Configurer handlers de signaux
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Socket TCP
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0) {
        perror("socket TCP");
        return 1;
    }
    
    // Réutiliser l'adresse
    int opt = 1;
    setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(tcp_socket, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind TCP");
        close(tcp_socket);
        return 1;
    }
    
    if (listen(tcp_socket, MAX_CLIENTS) < 0) {
        perror("listen");
        close(tcp_socket);
        return 1;
    }
    
    cout << "=== Serveur Graphes Théorie ===" << endl;
    cout << "Port: " << port << endl;
    cout << "Clients maximum: " << MAX_CLIENTS << endl;
    cout << "Poids: non-négatifs arbitraires (3ème année)" << endl;
    cout << "Algorithmes: Dijkstra" << endl;
    cout << "Appuyez sur Ctrl+C pour arrêter" << endl;
    
    // Démarrer serveur UDP dans un thread
    UDPServer udp_server(port);
    thread udp_thread([&udp_server]() {
        udp_server.run();
    });
    
    // Accepter connections TCP
    fd_set read_fds;
    int max_fd = tcp_socket;
    
    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(tcp_socket, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0 && errno != EINTR) {
            perror("select");
            break;
        }
        
        if (activity > 0 && FD_ISSET(tcp_socket, &read_fds)) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(tcp_socket, (sockaddr*)&client_addr, &client_len);
            
            if (client_fd >= 0) {
                // Démarrer thread pour client TCP
                thread([client_fd, client_addr]() {
                    handle_tcp_client(client_fd, client_addr);
                }).detach();
            }
        }
    }
    
    // Nettoyage
    cout << "Arrêt du serveur..." << endl;
    running = false;
    
    if (udp_thread.joinable()) {
        udp_thread.join();
    }
    
    close(tcp_socket);
    
    {
        lock_guard<mutex> lock(client_count_mutex);
        if (active_clients > 0) {
            cout << "Attente de déconnexion de " << active_clients << " client(s)..." << endl;
            sleep(2);
        }
    }
    
    cout << "Serveur arrêté" << endl;
    return 0;
}
