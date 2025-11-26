// server_udp.cpp
// Compile: g++ server_udp.cpp -o server_udp -std=c++17 -pthread

#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <limits>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocols.h"

using namespace std;

struct Packet {
    string text;
    sockaddr_in addr;
};

// Global shared queue per client_id
unordered_map<string, queue<Packet>> inbox;
unordered_map<string, sockaddr_in> client_addr_map;
unordered_map<string, condition_variable> cvs;
mutex inbox_mtx;

// Utility to convert sockaddr_in to string key
static string addr_to_str(const sockaddr_in &a) {
    char buf[64];
    inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf));
    int port = ntohs(a.sin_port);
    return string(buf) + ":" + to_string(port);
}

// Dijkstra implementation (adj list weighted)
long long INF = (long long)4e18;
struct PathResult {
    long long distance;
    vector<int> path;
    bool found;
};

PathResult dijkstra(int V, const vector<vector<pair<int,int>>> &adj, int S, int T) {
    vector<long long> dist(V, INF);
    vector<int> parent(V, -1);
    dist[S] = 0;
    using pli = pair<long long,int>;
    priority_queue<pli, vector<pli>, greater<pli>> pq;
    pq.push({0, S});
    while (!pq.empty()) {
        auto [d,u] = pq.top(); pq.pop();
        if (d != dist[u]) continue;
        if (u == T) break;
        for (auto &pr : adj[u]) {
            int v = pr.first;
            int w = pr.second;
            if (dist[v] > dist[u] + w) {
                dist[v] = dist[u] + w;
                parent[v] = u;
                pq.push({dist[v], v});
            }
        }
    }
    PathResult r;
    if (dist[T] == INF) { r.found = false; r.distance = -1; return r; }
    r.found = true; r.distance = dist[T];
    // reconstruct
    vector<int> p;
    for (int cur = T; cur != -1; cur = parent[cur]) p.push_back(cur);
    reverse(p.begin(), p.end());
    r.path = move(p);
    return r;
}

// Worker thread: consumes packets for a given client_id
void client_worker(const string client_id, int sockfd) {
    // get client addr
    sockaddr_in client_addr;
    {
        lock_guard<mutex> lk(inbox_mtx);
        client_addr = client_addr_map[client_id];
    }

    // wait and collect messages until FIN
    vector<string> messages;
    while (true) {
        unique_lock<mutex> lk(inbox_mtx);
        cvs[client_id].wait(lk, [&]{ return !inbox[client_id].empty(); });
        Packet pkt = std::move(inbox[client_id].front());
        inbox[client_id].pop();
        lk.unlock();

        string line = pkt.text;
        messages.push_back(line);

        // parse to find type
        auto parts = split_ws(line);
        if (parts.size() >= 2 && parts[1] == "FIN") {
            break;
        }
    }

    // parse collected messages: first HEADER, then n ROW messages, then WEIGHTS, then FIN
    // find HEADER
    string header_line;
    size_t idx = 0;
    for (; idx < messages.size(); ++idx) {
        auto p = split_ws(messages[idx]);
        if (p.size() >= 2 && p[1] == "HEADER") { header_line = messages[idx]; idx++; break; }
    }
    if (header_line.empty()) {
        // error: no header
        string resp = "SERVER_RESPONSE ERROR Missing HEADER";
        sendto(sockfd, resp.c_str(), resp.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
        return;
    }
    auto h_parts = split_ws(header_line);
    // header format: client_id HEADER n m start end
    if (h_parts.size() < 6) {
        string resp = "SERVER_RESPONSE ERROR Bad HEADER";
        sendto(sockfd, resp.c_str(), resp.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
        return;
    }
    int n = stoi(h_parts[2]);
    int m = stoi(h_parts[3]);
    int s = stoi(h_parts[4]);
    int t = stoi(h_parts[5]);

    // collect n ROW messages
    vector<vector<int>> incidence(n, vector<int>(m, 0));
    int rows_collected = 0;
    for (; idx < messages.size() && rows_collected < n; ++idx) {
        auto p = split_ws(messages[idx]);
        if (p.size() >= 2 && p[1] == "ROW") {
            // payload starts at p[2]...
            vector<int> row;
            for (size_t k = 2; k < p.size(); ++k) row.push_back(stoi(p[k]));
            if ((int)row.size() != m) {
                string resp = "SERVER_RESPONSE ERROR Bad ROW length";
                sendto(sockfd, resp.c_str(), resp.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
                return;
            }
            incidence[rows_collected++] = row;
        }
    }
    // next should be WEIGHTS
    vector<int> weights;
    for (; idx < messages.size(); ++idx) {
        auto p = split_ws(messages[idx]);
        if (p.size() >= 2 && p[1] == "WEIGHTS") {
            for (size_t k = 2; k < p.size(); ++k) weights.push_back(stoi(p[k]));
            break;
        }
    }
    if ((int)weights.size() != m) {
        string resp = "SERVER_RESPONSE ERROR Bad WEIGHTS";
        sendto(sockfd, resp.c_str(), resp.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
        return;
    }

    // Build adjacency weighted: for each edge (column) find two vertices with non-zero
    vector<vector<pair<int,int>>> adj(n);
    for (int e = 0; e < m; ++e) {
        int a = -1, b = -1;
        for (int v = 0; v < n; ++v) {
            if (incidence[v][e] != 0) {
                if (a == -1) a = v;
                else if (b == -1) b = v;
                else {
                    // more than 2 vertices: ignore extras (or consider clique — not implemented here)
                }
            }
        }
        if (a != -1 && b != -1) {
            int w = weights[e];
            if (w < 0) w = -w; // absolute
            if (w > 0) {
                adj[a].push_back({b, w});
                adj[b].push_back({a, w});
            }
        }
    }

    // run Dijkstra
    PathResult pr = dijkstra(n, adj, s, t);

    // prepare response string (text)
    string resp;
    if (!pr.found) {
        resp = "SERVER_RESPONSE ERROR NoPath";
    } else {
        // format: SERVER_RESPONSE OK distance path_size v0 v1 v2 ...
        resp = "SERVER_RESPONSE OK " + to_string(pr.distance) + " " + to_string((int)pr.path.size());
        for (int v : pr.path) resp += " " + to_string(v);
    }

    sendto(sockfd, resp.c_str(), resp.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
}

// main: listens and dispatches
int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./server_udp <port>\n";
        return 1;
    }
    int PORT = atoi(argv[1]);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    sockaddr_in srvaddr{};
    srvaddr.sin_family = AF_INET;
    srvaddr.sin_addr.s_addr = INADDR_ANY;
    srvaddr.sin_port = htons(PORT);

    if (bind(sockfd, (sockaddr*)&srvaddr, sizeof(srvaddr)) < 0) { perror("bind"); close(sockfd); return 1; }

    cout << "UDP server listening on port " << PORT << " ...\n";

    // main receive loop: for each datagram, parse client_id and push into that client's queue
    while (true) {
        char buf[8192];
        sockaddr_in cliaddr{};
        socklen_t cli_len = sizeof(cliaddr);
        ssize_t nread = recvfrom(sockfd, buf, sizeof(buf)-1, 0, (sockaddr*)&cliaddr, &cli_len);
        if (nread < 0) {
            perror("recvfrom");
            continue;
        }
        buf[nread] = '\0';
        string txt(buf);
        // expected format: "<client_id> <TYPE> payload..."
        auto parts = split_ws(txt);
        if (parts.empty()) continue;
        string cid = parts[0];

        // register client addr and queue if new
        {
            lock_guard<mutex> lk(inbox_mtx);
            if (inbox.find(cid) == inbox.end()) {
                // create queue, cv, store addr
                inbox[cid] = queue<Packet>();
                client_addr_map[cid] = cliaddr;
                cvs[cid]; // default constructed
                // spawn worker
                thread t(client_worker, cid, sockfd);
                t.detach();
            } else {
                // update addr mapping in case ephemeral port changed
                client_addr_map[cid] = cliaddr;
            }
            // push packet
            inbox[cid].push(Packet{txt, cliaddr});
            cvs[cid].notify_one();
        }
    }

    close(sockfd);
    return 0;
}
