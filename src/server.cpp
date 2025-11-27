// server.cpp
// Compile: g++ server.cpp -o server -std=c++17 -pthread

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

using namespace std;

constexpr int MIN_N = 7;
constexpr int MAX_N = 19;

// ---------- utilitaires ----------
static vector<string> split_ws(const string &s) {
    vector<string> out; string t;
    for (char c : s) {
        if (isspace((unsigned char)c)) { if (!t.empty()) { out.push_back(t); t.clear(); } }
        else t.push_back(c);
    }
    if (!t.empty()) out.push_back(t);
    return out;
}

// Dijkstra
struct PathResult { long long dist; vector<int> path; bool found; };
const long long INF = (long long)4e18;
PathResult dijkstra_adj(int N, const vector<vector<pair<int,int>>> &adj, int S, int T) {
    PathResult pr; pr.found = false; pr.dist = -1;
    vector<long long> dist(N, INF); vector<int> parent(N, -1);
    dist[S] = 0;
    using pli = pair<long long,int>;
    priority_queue<pli, vector<pli>, greater<pli>> pq; pq.push({0,S});
    while(!pq.empty()){
        auto [d,u] = pq.top(); pq.pop();
        if (d != dist[u]) continue;
        if (u == T) break;
        for (auto &e : adj[u]) {
            int v = e.first, w = e.second;
            if (dist[v] > dist[u] + w) { dist[v] = dist[u] + w; parent[v] = u; pq.push({dist[v], v}); }
        }
    }
    if (dist[T] == INF) return pr;
    pr.found = true; pr.dist = dist[T];
    for (int cur = T; cur != -1; cur = parent[cur]) pr.path.push_back(cur);
    reverse(pr.path.begin(), pr.path.end());
    return pr;
}

// Validation simple
bool valid_nm(int n, int m) {
    return (n > 6 && n < 20 && m > 6 && m < 20);
}

// Construction adjacency from incidence (n x m), weights size m
vector<vector<pair<int,int>>> build_adj(int n, int m, const vector<int> &mat, const vector<int> &weights) {
    vector<vector<pair<int,int>>> adj(n);
    for (int e = 0; e < m; ++e) {
        int a=-1,b=-1;
        for (int v=0; v<n; ++v) {
            int val = mat[v*m + e];
            if (val != 0) {
                if (a==-1) a = v;
                else if (b==-1) b = v;
                else { /* hyperedge: ignored extras */ }
            }
        }
        if (a!=-1 && b!=-1) {
            int w = weights[e]; if (w < 0) w = -w;
            if (w > 0) {
                adj[a].push_back({b,w});
                adj[b].push_back({a,w});
            }
        }
    }
    return adj;
}

// ---------- TCP handler: read GraphRequest (binary), matrix, weights; respond GraphResponse ----------
void handle_tcp_client(int client_sock) {
    // read header
    GraphRequest req;
    ssize_t r = recv(client_sock, &req, sizeof(req), MSG_WAITALL);
    if (r != (ssize_t)sizeof(req)) { close(client_sock); return; }

    GraphResponse resp; memset(&resp,0,sizeof(resp)); resp.path_length = -1; resp.path_size = 0; resp.error_code = 1;

    if (!valid_nm(req.vertices, req.edges)) {
        strncpy(resp.message, "n and m must be >6 and <20", sizeof(resp.message)-1);
        send(client_sock, &resp, sizeof(resp), 0);
        close(client_sock); return;
    }
    int n = req.vertices, m = req.edges;
    vector<int> mat(n * m);
    vector<int> weights(m);

    // read matrix
    ssize_t need = (ssize_t)(mat.size() * sizeof(int));
    ssize_t r1 = recv(client_sock, mat.data(), need, MSG_WAITALL);
    if (r1 != need) { close(client_sock); return; }
    // read weights
    need = (ssize_t)(weights.size() * sizeof(int));
    ssize_t r2 = recv(client_sock, weights.data(), need, MSG_WAITALL);
    if (r2 != need) { close(client_sock); return; }

    // compute adjacency + dijkstra
    auto adj = build_adj(n,m,mat,weights);
    PathResult pr = dijkstra_adj(n, adj, req.start_node, req.end_node);
    if (!pr.found) {
        resp.error_code = 1; resp.path_length = -1;
        strncpy(resp.message, "No path found", sizeof(resp.message)-1);
    } else {
        resp.error_code = 0;
        resp.path_length = (int)pr.dist;
        resp.path_size = (int)pr.path.size();
        for (int i=0;i<resp.path_size && i<MAX_VERTICES;i++) resp.path[i]=pr.path[i];
        strncpy(resp.message, "OK", sizeof(resp.message)-1);
    }
    send(client_sock, &resp, sizeof(resp), 0);
    close(client_sock);
}

// ---------- UDP handling: simple text protocol with client_id and messages ----------
struct UDPClientBuffer {
    vector<string> msgs;
    sockaddr_in addr;
    bool finished = false;
};
mutex udp_mtx;
unordered_map<string, UDPClientBuffer> udp_buffers;

void process_udp_client_and_respond(const string &client_id, int sockfd) {
    // extract and parse messages from buffer
    UDPClientBuffer buf;
    {
        lock_guard<mutex> lk(udp_mtx);
        buf = udp_buffers[client_id];
        // we will erase entry to avoid reprocessing
        udp_buffers.erase(client_id);
    }
    // find HEADER
    string header_line;
    size_t idx = 0;
    while (idx < buf.msgs.size()) {
        auto parts = split_ws(buf.msgs[idx]);
        if (parts.size() >= 2 && parts[1] == "HEADER") { header_line = buf.msgs[idx]; idx++; break; }
        idx++;
    }
    GraphResponse resp; memset(&resp,0,sizeof(resp)); resp.error_code = 1; resp.path_length = -1;
    if (header_line.empty()) {
        string out = "SERVER_RESPONSE ERROR Missing HEADER";
        sendto(sockfd, out.c_str(), out.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
        return;
    }
    auto hparts = split_ws(header_line);
    if (hparts.size() < 6) {
        string out = "SERVER_RESPONSE ERROR Bad HEADER";
        sendto(sockfd, out.c_str(), out.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
        return;
    }
    int n = stoi(hparts[2]), m = stoi(hparts[3]), s = stoi(hparts[4]), t = stoi(hparts[5]);
    if (!valid_nm(n,m)) {
        string out = "SERVER_RESPONSE ERROR n,m out of range";
        sendto(sockfd, out.c_str(), out.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
        return;
    }
    // collect rows
    vector<vector<int>> incidence(n, vector<int>(m,0));
    int rows_collected = 0;
    for (; idx < buf.msgs.size() && rows_collected < n; ++idx) {
        auto parts = split_ws(buf.msgs[idx]);
        if (parts.size() >= 2 && parts[1] == "ROW") {
            vector<int> row;
            for (size_t k=2;k<parts.size();++k) row.push_back(stoi(parts[k]));
            if ((int)row.size() != m) {
                string out = "SERVER_RESPONSE ERROR Bad ROW size";
                sendto(sockfd, out.c_str(), out.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
                return;
            }
            incidence[rows_collected++] = row;
        }
    }
    // find WEIGHTS
    vector<int> weights;
    for (; idx < buf.msgs.size(); ++idx) {
        auto parts = split_ws(buf.msgs[idx]);
        if (parts.size() >= 2 && parts[1] == "WEIGHTS") {
            for (size_t k=2;k<parts.size();++k) weights.push_back(stoi(parts[k]));
            break;
        }
    }
    if ((int)weights.size() != m) {
        string out = "SERVER_RESPONSE ERROR Bad WEIGHTS";
        sendto(sockfd, out.c_str(), out.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
        return;
    }
    // flatten mat to vector<int>
    vector<int> flat(n*m);
    for (int i=0;i<n;i++) for (int j=0;j<m;j++) flat[i*m+j]=incidence[i][j];
    auto adj = build_adj(n,m,flat,weights);
    PathResult pr = dijkstra_adj(n,adj,s,t);
    if (!pr.found) {
        string out = "SERVER_RESPONSE ERROR NoPath";
        sendto(sockfd, out.c_str(), out.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
        return;
    }
    // OK response: SERVER_RESPONSE OK distance size v0 v1 ...
    string out = "SERVER_RESPONSE OK " + to_string(pr.dist) + " " + to_string((int)pr.path.size());
    for (int v: pr.path) out += " " + to_string(v);
    sendto(sockfd, out.c_str(), out.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
}

// ---------- main ----------
int main(int argc, char* argv[]) {
    if (argc != 2) { cerr << "Usage: ./server <port>\n"; return 1; }
    int PORT = atoi(argv[1]);

    // TCP socket
    int tcpfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpfd < 0) { perror("tcp socket"); return 1; }
    // UDP socket
    int udpfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpfd < 0) { perror("udp socket"); return 1; }

    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(PORT);

    int opt = 1;
    setsockopt(tcpfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(tcpfd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind tcp"); return 1; }
    if (listen(tcpfd, 10) < 0) { perror("listen"); return 1; }

    if (bind(udpfd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind udp"); return 1; }

    cout << "Server listening on port " << PORT << " (TCP + UDP)\n";

    // Thread to accept TCP connections
    thread tcp_thread([&](){
        while (true) {
            sockaddr_in cli{}; socklen_t len = sizeof(cli);
            int client = accept(tcpfd, (sockaddr*)&cli, &len);
            if (client < 0) { perror("accept"); continue; }
            thread t(handle_tcp_client, client);
            t.detach();
        }
    });
    tcp_thread.detach();

    // Main loop for UDP receiving
    while (true) {
        char buf[8192];
        sockaddr_in cli{}; socklen_t len = sizeof(cli);
        ssize_t nread = recvfrom(udpfd, buf, sizeof(buf)-1, 0, (sockaddr*)&cli, &len);
        if (nread <= 0) { if (nread<0) perror("recvfrom"); continue; }
        buf[nread] = '\0';
        string txt(buf);
        auto parts = split_ws(txt);
        if (parts.empty()) continue;
        string cid = parts[0];
        // store into buffer
        {
            lock_guard<mutex> lk(udp_mtx);
            if (udp_buffers.find(cid) == udp_buffers.end()) {
                UDPClientBuffer b;
                b.addr = cli;
                b.msgs.push_back(txt);
                udp_buffers[cid] = move(b);
            } else {
                udp_buffers[cid].msgs.push_back(txt);
                // update addr just in case
                udp_buffers[cid].addr = cli;
            }
            // check if this message is FIN; if so spawn worker to process that client
            if (parts.size() >= 2 && parts[1] == "FIN") {
                // spawn worker
                thread w(process_udp_client_and_respond, cid, udpfd);
                w.detach();
            }
        }
    }

    close(tcpfd); close(udpfd);
    return 0;
}
