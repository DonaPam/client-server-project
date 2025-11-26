// client_udp.cpp
// Compile: g++ client_udp.cpp -o client_udp -std=c++17

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocols.h"

using namespace std;

// generate small random client id
string gen_client_id() {
    static std::mt19937_64 rng((unsigned)chrono::high_resolution_clock::now().time_since_epoch().count());
    uint64_t v = rng();
    string s;
    for (int i = 0; i < 8; ++i) {
        int d = v & 0xFF;
        char c = "0123456789ABCDEF"[d % 16];
        s.push_back(c);
        v >>= 8;
    }
    return s;
}

int main(int argc, char* argv[]) {
    cout << "=== UDP client (manual input) ===\n";
    string server_ip;
    int server_port;
    cout << "Server IP (e.g. 192.168.0.10): ";
    cin >> server_ip;
    cout << "Server port (e.g. 5050): ";
    cin >> server_port;

    // Graph input
    int V, E;
    cout << "Number of vertices: "; cin >> V;
    cout << "Number of edges: "; cin >> E;
    int s, t;
    cout << "Start vertex (0..V-1): "; cin >> s;
    cout << "End vertex (0..V-1): "; cin >> t;

    vector<vector<int>> incidence(V, vector<int>(E, 0));
    vector<int> weights(E, 1);

    cout << "Enter edges (for each edge: u v weight)  (0-based vertices)\n";
    for (int e = 0; e < E; ++e) {
        int u,v,w;
        cout << "Edge " << e << " : ";
        cin >> u >> v >> w;
        if (u<0||u>=V||v<0||v>=V) { cerr<<"Invalid vertices\n"; return 1; }
        incidence[u][e] = (w>0? w : -w); // place w (positive) at u as +w
        incidence[v][e] = (w>0? -w : w); // opposite sign at v
        weights[e] = (w>0? w : -w);
    }

    // Build UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip.c_str(), &srv.sin_addr) <= 0) { cerr<<"Bad server IP\n"; return 1; }

    string cid = gen_client_id();

    // Send HEADER
    {
        string payload = to_string(V) + " " + to_string(E) + " " + to_string(s) + " " + to_string(t);
        string msg = make_client_header(cid, "HEADER", payload);
        sendto(sockfd, msg.c_str(), msg.size(), 0, (sockaddr*)&srv, sizeof(srv));
    }
    // Send rows
    for (int i = 0; i < V; ++i) {
        string line;
        for (int e = 0; e < E; ++e) {
            if (e) line += " ";
            line += to_string(incidence[i][e]);
        }
        string msg = make_client_header(cid, "ROW", line);
        sendto(sockfd, msg.c_str(), msg.size(), 0, (sockaddr*)&srv, sizeof(srv));
    }
    // Send weights
    {
        string line;
        for (int e = 0; e < E; ++e) {
            if (e) line += " ";
            line += to_string(weights[e]);
        }
        string msg = make_client_header(cid, "WEIGHTS", line);
        sendto(sockfd, msg.c_str(), msg.size(), 0, (sockaddr*)&srv, sizeof(srv));
    }
    // Send FIN
    {
        string msg = make_client_header(cid, "FIN", "bye");
        sendto(sockfd, msg.c_str(), msg.size(), 0, (sockaddr*)&srv, sizeof(srv));
    }

    // Wait for response (blocking)
    char buf[8192];
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    ssize_t n = recvfrom(sockfd, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromlen);
    if (n <= 0) { cerr << "No response\n"; close(sockfd); return 1; }
    buf[n] = '\0';
    string response(buf);
    cout << "Server response: " << response << "\n";

    close(sockfd);
    return 0;
}
