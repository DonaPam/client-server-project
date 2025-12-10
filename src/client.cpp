// client.cpp - Version corrigée
// Compile: g++ client.cpp -o client -std=c++17

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include "protocol.h"
using namespace std;

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

string gen_client_id() {
    static mt19937_64 rng(chrono::high_resolution_clock::now().time_since_epoch().count());
    uint64_t v = rng();
    string s(16, '0');
    for (int i = 0; i < 16; i++) {
        s[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    return s;
}

// ==================== UDP FIABLE ====================
class ReliableUDP {
private:
    int sock;
    sockaddr_in server_addr;
    string client_id;
    uint16_t seq_num;
    
    bool send_with_ack(const UdpDataPacket& packet, size_t data_size) {
        const int MAX_RETRIES = 3;
        const int TIMEOUT_MS = 3000;
        
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            // Envoi du paquet
            sendto(sock, &packet, sizeof(UdpHeader) + data_size, 0,
                   (sockaddr*)&server_addr, sizeof(server_addr));
            
            // Attente ACK avec timeout
            struct pollfd pfd;
            pfd.fd = sock;
            pfd.events = POLLIN;
            
            int ret = poll(&pfd, 1, TIMEOUT_MS);
            
            if (ret > 0) {
                // Données reçues, vérifier si c'est un ACK
                UdpDataPacket ack_packet;
                sockaddr_in from;
                socklen_t from_len = sizeof(from);
                
                ssize_t bytes = recvfrom(sock, &ack_packet, sizeof(ack_packet), 0,
                                         (sockaddr*)&from, &from_len);
                
                if (bytes >= sizeof(UdpHeader)) {
                    // Vérifier que c'est bien notre serveur
                    if (from.sin_addr.s_addr == server_addr.sin_addr.s_addr &&
                        from.sin_port == server_addr.sin_port &&
                        ack_packet.header.packet_type == UDP_ACK &&
                        memcmp(ack_packet.header.client_id, client_id.c_str(), 16) == 0) {
                        return true; // ACK reçu
                    }
                }
            } else if (ret == 0) {
                // Timeout
                cerr << "Timeout, tentative " << (attempt + 1) << "/3..." << endl;
            }
        }
        
        // Échec après 3 tentatives
        cerr << "ERREUR: Perte de connexion avec le serveur après 3 tentatives" << endl;
        return false;
    }
    
public:
    ReliableUDP(int socket, const sockaddr_in& addr, const string& id) 
        : sock(socket), server_addr(addr), client_id(id), seq_num(0) {}
    
    bool send_data(const void* data, size_t size, uint16_t packet_type) {
        UdpDataPacket packet;
        
        // Remplir header
        memcpy(packet.header.client_id, client_id.c_str(), 16);
        packet.header.packet_type = packet_type;
        packet.header.seq_num = seq_num++;
        packet.header.total_packets = 1;
        packet.header.current_packet = 0;
        
        // Copier données
        size_t to_copy = min(size, sizeof(packet.data));
        memcpy(packet.data, data, to_copy);
        
        return send_with_ack(packet, to_copy);
    }
    
    bool send_graph_data(int n, int m, int s, int t, 
                        const vector<int>& mat, const vector<int>& weights) {
        // 1. Envoyer HEADER
        string header_data = to_string(n) + " " + to_string(m) + " " + 
                            to_string(s) + " " + to_string(t);
        
        if (!send_data(header_data.c_str(), header_data.size() + 1, UDP_HEADER)) {
            return false;
        }
        
        // 2. Envoyer matrice d'incidence par lignes
        for (int i = 0; i < n; i++) {
            string row_data;
            for (int e = 0; e < m; e++) {
                row_data += to_string(mat[i * m + e]) + " ";
            }
            
            if (!send_data(row_data.c_str(), row_data.size() + 1, UDP_DATA)) {
                return false;
            }
        }
        
        // 3. Envoyer poids
        string weights_data;
        for (int w : weights) {
            weights_data += to_string(w) + " ";
        }
        
        if (!send_data(weights_data.c_str(), weights_data.size() + 1, UDP_WEIGHTS)) {
            return false;
        }
        
        // 4. Envoyer FIN
        if (!send_data("", 1, UDP_FIN)) {
            return false;
        }
        
        return true;
    }
};

// ==================== ENTRÉE DONNÉES ====================
bool get_graph_data(int& n, int& m, int& s, int& t, 
                   vector<int>& mat, vector<int>& weights) {
    cout << "=== Source des données ===\n";
    cout << "1 = Saisie manuelle\n";
    cout << "2 = Lecture depuis fichier\n";
    cout << "3 = Quitter\n";
    cout << "Choix: ";
    
    string input;
    cin >> input;
    
    if (input == "exit" || input == "3") {
        cout << "Au revoir!\n";
        return false;
    }
    
    int mode;
    try {
        mode = stoi(input);
    } catch (...) {
        cerr << "Entrée invalide\n";
        return false;
    }
    
    if (mode == 2) {
        // Lecture fichier
        string filename;
        cout << "Nom du fichier: ";
        cin >> filename;
        
        if (filename == "exit") return false;
        
        ifstream fin(filename);
        if (!fin) {
            cerr << "Impossible d'ouvrir le fichier\n";
            return false;
        }
        
        fin >> n >> m;
        fin >> s >> t;
        
        // Validation selon OДЗ
        if (n < MIN_VERTICES || n > MAX_VERTICES || 
            m < MIN_VERTICES || m > MAX_EDGES) {
            cerr << "ERREUR: n doit être entre " << MIN_VERTICES 
                 << " et " << MAX_VERTICES 
                 << ", m entre " << MIN_VERTICES 
                 << " et " << MAX_EDGES << endl;
            return false;
        }
        
        mat.assign(n * m, 0);
        weights.assign(m, 0);
        
        for (int e = 0; e < m; e++) {
            int u, v, w;
            if (!(fin >> u >> v >> w)) {
                cerr << "Format de fichier invalide\n";
                return false;
            }
            
            // Validation poids (non-négatifs pour 3ème année)
            if (w < 0) {
                cerr << "ERREUR: Les poids doivent être non-négatifs\n";
                return false;
            }
            
            // Matrice d'incidence signée
            mat[u * m + e] = w;
            mat[v * m + e] = -w;
            weights[e] = w;
        }
        
        cout << "✓ Fichier lu avec succès\n";
        
    } else if (mode == 1) {
        // Saisie manuelle
        cout << "Nombre de sommets (" << MIN_VERTICES << "-" << MAX_VERTICES << "): ";
        cin >> input;
        if (input == "exit") return false;
        n = stoi(input);
        
        cout << "Nombre d'arêtes (" << MIN_VERTICES << "-" << MAX_EDGES << "): ";
        cin >> input;
        if (input == "exit") return false;
        m = stoi(input);
        
        // Validation OДЗ
        if (n < MIN_VERTICES || n > MAX_VERTICES || 
            m < MIN_VERTICES || m > MAX_EDGES) {
            cerr << "ERREUR: Valeurs hors limites\n";
            return false;
        }
        
        cout << "Sommet de départ (0-" << n-1 << "): ";
        cin >> input;
        if (input == "exit") return false;
        s = stoi(input);
        
        cout << "Sommet d'arrivée (0-" << n-1 << "): ";
        cin >> input;
        if (input == "exit") return false;
        t = stoi(input);
        
        mat.assign(n * m, 0);
        weights.assign(m, 0);
        
        cout << "\nSaisie des " << m << " arêtes (format: u v poids):\n";
        for (int e = 0; e < m; e++) {
            cout << "Arête " << e << ": ";
            string u_str, v_str, w_str;
            cin >> u_str;
            if (u_str == "exit") return false;
            cin >> v_str;
            if (v_str == "exit") return false;
            cin >> w_str;
            if (w_str == "exit") return false;
            
            int u = stoi(u_str);
            int v = stoi(v_str);
            int w = stoi(w_str);
            
            // Validation poids non-négatifs
            if (w < 0) {
                cerr << "ERREUR: Poids non-négatifs seulement\n";
                return false;
            }
            
            // Matrice d'incidence
            mat[u * m + e] = w;
            mat[v * m + e] = -w;
            weights[e] = w;
        }
    } else {
        cerr << "Choix invalide\n";
        return false;
    }
    
    return true;
}

// ==================== COMMUNICATION ====================
bool send_tcp(const string& server_ip, int port,
             int n, int m, int s, int t,
             const vector<int>& mat, const vector<int>& weights) {
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return false;
    }
    
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    inet_pton(AF_INET, server_ip.c_str(), &srv.sin_addr);
    
    // Timeout de connexion
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    if (connect(sock, (sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("connect");
        close(sock);
        return false;
    }
    
    // Préparer requête
    GraphRequest req;
    req.vertices = n;
    req.edges = m;
    req.start_node = s;
    req.end_node = t;
    req.protocol = 1; // TCP
    
    // Envoyer requête
    if (send(sock, &req, sizeof(req), 0) != sizeof(req)) {
        perror("send header");
        close(sock);
        return false;
    }
    
    // Envoyer matrice
    if (send(sock, mat.data(), mat.size() * sizeof(int), 0) 
        != (ssize_t)(mat.size() * sizeof(int))) {
        perror("send matrix");
        close(sock);
        return false;
    }
    
    // Envoyer poids
    if (send(sock, weights.data(), weights.size() * sizeof(int), 0)
        != (ssize_t)(weights.size() * sizeof(int))) {
        perror("send weights");
        close(sock);
        return false;
    }
    
    // Recevoir réponse
    GraphResponse resp;
    ssize_t bytes = recv(sock, &resp, sizeof(resp), MSG_WAITALL);
    
    if (bytes == sizeof(resp)) {
        cout << "\n=== RÉSULTAT (TCP) ===" << endl;
        if (resp.error_code == 0) {
            cout << "Longueur du chemin: " << resp.path_length << endl;
            cout << "Chemin: ";
            for (int i = 0; i < resp.path_size; i++) {
                cout << resp.path[i];
                if (i < resp.path_size - 1) cout << " → ";
            }
            cout << endl;
        } else {
            cout << "ERREUR: " << resp.message << endl;
        }
    } else {
        cerr << "Réponse incomplète du serveur" << endl;
    }
    
    close(sock);
    return true;
}

bool send_udp(const string& server_ip, int port,
             int n, int m, int s, int t,
             const vector<int>& mat, const vector<int>& weights) {
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket UDP");
        return false;
    }
    
    // Socket non-bloquant pour timeout
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    inet_pton(AF_INET, server_ip.c_str(), &srv.sin_addr);
    
    string client_id = gen_client_id();
    ReliableUDP rudp(sock, srv, client_id);
    
    cout << "Connexion UDP fiable établie (ID: " << client_id << ")" << endl;
    
    // Envoyer données avec mécanisme fiable
    if (!rudp.send_graph_data(n, m, s, t, mat, weights)) {
        cerr << "Échec de l'envoi des données" << endl;
        close(sock);
        return false;
    }
    
    // Attendre réponse finale
    UdpDataPacket response;
    sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    
    int ret = poll(&pfd, 1, 10000); // 10 secondes timeout
    
    if (ret > 0) {
        ssize_t bytes = recvfrom(sock, &response, sizeof(response), 0,
                                (sockaddr*)&from, &from_len);
        
        if (bytes > 0) {
            response.data[min((size_t)bytes - sizeof(UdpHeader), sizeof(response.data) - 1)] = '\0';
            
            if (strncmp(response.data, "OK ", 3) == 0) {
                auto parts = split_ws(response.data);
                if (parts.size() >= 3) {
                    cout << "\n=== RÉSULTAT (UDP) ===" << endl;
                    cout << "Longueur du chemin: " << parts[1] << endl;
                    cout << "Chemin: ";
                    int path_size = stoi(parts[2]);
                    for (int i = 0; i < path_size; i++) {
                        cout << parts[3 + i];
                        if (i < path_size - 1) cout << " → ";
                    }
                    cout << endl;
                }
            } else {
                cout << "ERREUR serveur: " << response.data << endl;
            }
        }
    } else {
        cerr << "Timeout: Pas de réponse du serveur" << endl;
    }
    
    close(sock);
    return true;
}

// ==================== MAIN ====================
void show_usage(const char* prog) {
    cout << "Usage:\n";
    cout << "  " << prog << " <IP> <TCP|UDP> <PORT>\n";
    cout << "  " << prog << "                  (mode interactif)\n";
    cout << "Exemples:\n";
    cout << "  " << prog << " 127.0.0.1 TCP 8080\n";
    cout << "  " << prog << " 192.168.1.10 UDP 9090\n";
}

int main(int argc, char* argv[]) {
    cout << "=== Client Graphes Théorie (3ème année) ===\n";
    cout << "Poids d'arêtes: non-négatifs arbitraires\n";
    
    if (argc == 4) {
        // Mode ligne de commande
        string server_ip = argv[1];
        string protocol_str = argv[2];
        int port;
        
        transform(protocol_str.begin(), protocol_str.end(), protocol_str.begin(), ::toupper);
        
        try {
            port = stoi(argv[3]);
        } catch (...) {
            cerr << "Port invalide\n";
            show_usage(argv[0]);
            return 1;
        }
        
        int proto = (protocol_str == "TCP") ? 1 : 
                   (protocol_str == "UDP") ? 2 : 0;
        
        if (proto == 0 || port < 1 || port > 65535) {
            show_usage(argv[0]);
            return 1;
        }
        
        // Obtenir données du graphe
        int n, m, s, t;
        vector<int> mat, weights;
        
        if (!get_graph_data(n, m, s, t, mat, weights)) {
            return 0;
        }
        
        // Envoyer selon protocole
        if (proto == 1) {
            send_tcp(server_ip, port, n, m, s, t, mat, weights);
        } else {
            send_udp(server_ip, port, n, m, s, t, mat, weights);
        }
        
    } else if (argc == 1) {
        // Mode interactif
        while (true) {
            cout << "\n=== Nouvelle Requête ===\n";
            
            // Demander protocole
            cout << "Protocole (TCP/UDP/exit): ";
            string protocol_str;
            cin >> protocol_str;
            transform(protocol_str.begin(), protocol_str.end(), protocol_str.begin(), ::toupper);
            
            if (protocol_str == "EXIT") {
                cout << "Au revoir!\n";
                break;
            }
            
            if (protocol_str != "TCP" && protocol_str != "UDP") {
                cerr << "Protocole invalide\n";
                continue;
            }
            
            // Demander IP et port
            cout << "IP du serveur: ";
            string server_ip;
            cin >> server_ip;
            if (server_ip == "exit") break;
            
            cout << "Port: ";
            string port_str;
            cin >> port_str;
            if (port_str == "exit") break;
            
            int port;
            try {
                port = stoi(port_str);
            } catch (...) {
                cerr << "Port invalide\n";
                continue;
            }
            
            // Obtenir données du graphe
            int n, m, s, t;
            vector<int> mat, weights;
            
            if (!get_graph_data(n, m, s, t, mat, weights)) {
                break;
            }
            
            // Envoyer
            if (protocol_str == "TCP") {
                send_tcp(server_ip, port, n, m, s, t, mat, weights);
            } else {
                send_udp(server_ip, port, n, m, s, t, mat, weights);
            }
            
            // Continuer?
            cout << "\nNouvelle requête? (oui/non): ";
            string response;
            cin >> response;
            if (response == "non" || response == "exit") {
                cout << "Au revoir!\n";
                break;
            }
        }
    } else {
        show_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
