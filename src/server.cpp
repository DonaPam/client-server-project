// server.cpp
// Compile: g++ server.cpp -o server -std=c++17 -pthread

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include "protocol.h"
using namespace std;

/*==========================================================================
 * VALIDATION UTILITIES
 *==========================================================================*/

bool valid_nm(int n, int m){
    return (n >= 6 && n < 20 && m >= 6 && m < 20);
}

/*==========================================================================
 * DIJKSTRA + GRAPH BUILD
 *==========================================================================*/

long long INF = (1LL << 60);

struct PathResult {
    long long dist;
    vector<int> path;
    bool ok;
};

PathResult dijkstra(int n, vector<vector<pair<int,int>>>& adj, int S, int T){
    vector<long long> dist(n, INF);
    vector<int> parent(n, -1);

    dist[S] = 0;
    priority_queue<pair<long long,int>, vector<pair<long long,int>>, greater<>> pq;
    pq.push({0, S});

    while(!pq.empty()){
        auto [d,u] = pq.top();
        pq.pop();
        if(d != dist[u]) continue;
        if(u == T) break;

        for(auto &ed: adj[u]){
            int v = ed.first;
            int w = ed.second;
            if(dist[v] > dist[u] + w){
                dist[v] = dist[u] + w;
                parent[v] = u;
                pq.push({dist[v], v});
            }
        }
    }

    PathResult R;
    if(dist[T] == INF){
        R.ok = false;
        return R;
    }

    R.ok = true;
    R.dist = dist[T];
    int x = T;

    while(x != -1){
        R.path.push_back(x);
        x = parent[x];
    }
    reverse(R.path.begin(), R.path.end());
    return R;
}

vector<vector<pair<int,int>>> build_adj(int n, int m, const vector<int>& flat, const vector<int>& W){
    vector<vector<pair<int,int>>> adj(n);

    for(int e=0; e<m; e++){
        int a=-1, b=-1;
        for(int v=0; v<n; v++){
            int val = flat[v*m + e];
            if(val != 0){
                if(a == -1) a = v;
                else if(b == -1) b = v;
                else { a = -2; break; } // invalid
            }
        }
        if(a < 0 || b < 0) continue;

        int w = W[e];
        if(w < 0) w = -w;

        adj[a].push_back({b, w});
        adj[b].push_back({a, w});
    }
    return adj;
}

/*==========================================================================
 * TCP HANDLING (LIMIT 3 CLIENTS)
 *==========================================================================*/

atomic<int> tcp_clients{0};
const int TCP_LIMIT = 3;

void handle_tcp(int client){
    if(tcp_clients.fetch_add(1) >= TCP_LIMIT){
        tcp_clients--;
        GraphResponse resp{};
        resp.error_code = 1;
        strcpy(resp.message, "Server busy: too many TCP clients");
        send(client, &resp, sizeof(resp), 0);
        close(client);
        return;
    }

    GraphRequest req{};
    if(recv(client, &req, sizeof(req), MSG_WAITALL) != sizeof(req)){
        close(client);
        tcp_clients--;
        return;
    }

    GraphResponse resp{};
    resp.error_code = 1;

    if(!valid_nm(req.vertices, req.edges)){
        strcpy(resp.message, "n/m invalid.");
        send(client, &resp, sizeof(resp), 0);
        close(client);
        tcp_clients--;
        return;
    }

    int n = req.vertices, m = req.edges;
    int S = req.start_node, T = req.end_node;

    if(S < 0 || S >= n || T < 0 || T >= n){
        strcpy(resp.message, "Start/end invalid.");
        send(client, &resp, sizeof(resp), 0);
        close(client);
        tcp_clients--;
        return;
    }

    vector<int> mat(n*m);
    vector<int> W(m);

    ssize_t needMat = n*m*sizeof(int);
    ssize_t needW   = m*sizeof(int);

    if(recv(client, mat.data(), needMat, MSG_WAITALL) != needMat ||
       recv(client, W.data(), needW, MSG_WAITALL) != needW)
    {
        close(client);
        tcp_clients--;
        return;
    }

    /* Validate columns exactly 2 non-zero entries */
    for(int e=0; e<m; e++){
        int cnt=0, pos=-1, neg=-1;
        for(int v=0; v<n; v++){
            int val = mat[v*m+e];
            if(val != 0){
                cnt++;
                if(val > 0) pos=v;
                else        neg=v;
            }
        }
        if(cnt != 2 || pos==-1 || neg==-1){
            strcpy(resp.message, "Invalid incidence matrix");
            send(client,&resp,sizeof(resp),0);
            close(client);
            tcp_clients--;
            return;
        }
    }

    auto adj = build_adj(n,m,mat,W);
    auto R = dijkstra(n,adj,S,T);

    if(!R.ok){
        resp.error_code=1;
        resp.path_length=-1;
        strcpy(resp.message,"No path found");
    } else {
        resp.error_code = 0;
        resp.path_length = R.dist;
        resp.path_size   = R.path.size();
        strcpy(resp.message, "OK");

        for(int i=0;i<R.path.size() && i<64;i++)
            resp.path[i] = R.path[i];
    }

    send(client, &resp, sizeof(resp), 0);
    close(client);
    tcp_clients--;
}

/*==========================================================================
 * UDP BUFFER (RELIABLE)
 *==========================================================================*/

struct Udbuf {
    sockaddr_in addr;
    int n=-1, m=-1, S=-1, T=-1;
    bool have_header=false;
    bool have_weights=false;
    int received_rows=0;

    vector<vector<int>> rows;
    vector<int> weights;
};

mutex U_m;
unordered_map<string, Udbuf> U;

/*==========================================================================
 * UDP PROCESSOR
 *==========================================================================*/

atomic<int> udp_tasks{0};
const int UDP_LIMIT = 3;

void udp_process(const string& cid, int udp){
    if(udp_tasks.fetch_add(1) >= UDP_LIMIT){
        udp_tasks--;
        return;
    }

    Udbuf buf;
    {
        lock_guard<mutex> lk(U_m);
        buf = U[cid];
        U.erase(cid);
    }

    if(!buf.have_header || !buf.have_weights || buf.received_rows != buf.n){
        string err = cid + " ERROR Incomplete data";
        sendto(udp, err.c_str(), err.size(), 0, 
               (sockaddr*)&buf.addr, sizeof(buf.addr));
        udp_tasks--;
        return;
    }

    int n=buf.n, m=buf.m, S=buf.S, T=buf.T;

    // Flatten
    vector<int> flat(n*m);
    for(int i=0;i<n;i++)
        for(int j=0;j<m;j++)
            flat[i*m+j] = buf.rows[i][j];

    // Validate each column
    for(int e=0;e<m;e++){
        int cnt=0,pos=-1,neg=-1;
        for(int v=0;v<n;v++){
            int val = flat[v*m+e];
            if(val!=0){
                cnt++;
                if(val>0) pos=v;
                else      neg=v;
            }
        }
        if(cnt!=2 || pos==-1 || neg==-1){
            string err = cid + " ERROR Invalid incidence col";
            sendto(udp,err.c_str(),err.size(),0,
                   (sockaddr*)&buf.addr,sizeof(buf.addr));
            udp_tasks--;
            return;
        }
    }

    auto adj = build_adj(n,m,flat,buf.weights);
    auto R = dijkstra(n,adj,S,T);

    if(!R.ok){
        string err = cid + " ERROR No Path";
        sendto(udp,err.c_str(),err.size(),0,
               (sockaddr*)&buf.addr,sizeof(buf.addr));
        udp_tasks--;
        return;
    }

    // Build UDP_RESULT binary
    vector<uint8_t> out(sizeof(UdpPacketHeader) + 4 + 4 + 4*R.path.size());
    UdpPacketHeader* h = (UdpPacketHeader*)out.data();
    memcpy(h->cid, cid.c_str(), 9);
    h->type = UDP_RESULT;

    uint8_t* p = out.data() + sizeof(UdpPacketHeader);

    auto put = [&](int32_t x){
        int32_t y = htonl(x);
        memcpy(p, &y, 4);
        p += 4;
    };

    put((int32_t)R.dist);
    put((int32_t)R.path.size());
    for(int v : R.path) put(v);

    sendto(udp, out.data(), out.size(), 0,
           (sockaddr*)&buf.addr, sizeof(buf.addr));

    udp_tasks--;
}

/*==========================================================================
 * MAIN SERVER LOOP
 *==========================================================================*/

int main(int argc,char**argv){
    if(argc!=2){
        cout<<"Usage: ./server <port>\n";
        return 0;
    }

    int PORT = atoi(argv[1]);

    int tcp = socket(AF_INET,SOCK_STREAM,0);
    int udp = socket(AF_INET,SOCK_DGRAM,0);

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port   = htons(PORT);
    a.sin_addr.s_addr = INADDR_ANY;

    bind(tcp,(sockaddr*)&a,sizeof(a));
    listen(tcp,10);

    bind(udp,(sockaddr*)&a,sizeof(a));

    cout<<"Server running on port "<<PORT<<" (TCP + UDP)\n";

    // TCP accept loop
    thread([&](){
        while(true){
            sockaddr_in c;
            socklen_t L=sizeof(c);
            int cl = accept(tcp,(sockaddr*)&c,&L);
            if(cl>=0){
                thread(handle_tcp, cl).detach();
            }
        }
    }).detach();

    // UDP loop
    while(true){
        uint8_t buf_raw[4096];
        sockaddr_in from; socklen_t L=sizeof(from);
        ssize_t r = recvfrom(udp, buf_raw, sizeof(buf_raw), 0,
                             (sockaddr*)&from, &L);

        if(r < (ssize_t)sizeof(UdpPacketHeader)) continue;

        UdpPacketHeader *h = (UdpPacketHeader*)buf_raw;
        string cid(h->cid, 8);

        cout << "[UDP] Received packet type=" << (int)h->type 
         << " from CID=" << cid 
         << " size=" << r << " bytes\n";
        
        lock_guard<mutex> lk(U_m);
        auto &B = U[cid];
        B.addr = from;

        if(h->type == UDP_HEADER){
            uint8_t* p = buf_raw + sizeof(UdpPacketHeader);
            int32_t n = ntohl(*(int32_t*)p); p+=4;
            int32_t m = ntohl(*(int32_t*)p); p+=4;
            int32_t S = ntohl(*(int32_t*)p); p+=4;
            int32_t T = ntohl(*(int32_t*)p); p+=4;

            if(!valid_nm(n,m)){
                string err = cid + " ERROR Invalid n/m";
                sendto(udp, err.c_str(), err.size(), 0,
                       (sockaddr*)&from, sizeof(from));
                continue;
            }

            B.n = n; B.m = m; B.S = S; B.T = T;
            B.rows.assign(n, vector<int>(m, 0));
            B.weights.assign(m, 0);
            B.have_header = true;
        }

        else if(h->type == UDP_ROW){
            if(!B.have_header) continue;
            uint8_t* p = buf_raw + sizeof(UdpPacketHeader);
            int32_t row = ntohl(*(int32_t*)p); p+=4;

            if(row < 0 || row >= B.n) continue;

            for(int j=0;j<B.m;j++){
                int32_t val = ntohl(*(int32_t*)p); p+=4;
                B.rows[row][j] = val;
            }
            B.received_rows++;
        }

        else if(h->type == UDP_WEIGHTS){
            if(!B.have_header) continue;
            uint8_t* p = buf_raw + sizeof(UdpPacketHeader);
            int32_t m2 = ntohl(*(int32_t*)p); p+=4;

            if(m2 != B.m) continue;

            for(int j=0;j<B.m;j++){
                int32_t w = ntohl(*(int32_t*)p); p+=4;
                B.weights[j] = w;
            }
            B.have_weights = true;
        }

        else if(h->type == UDP_FIN){
            // SEND ACK IMMEDIATELY
            vector<uint8_t> ack(sizeof(UdpPacketHeader));
            UdpPacketHeader *ah = (UdpPacketHeader*)ack.data();
            memcpy(ah->cid, h->cid, 9);
            ah->type = UDP_ACK;

            sendto(udp, ack.data(), ack.size(), 0,
                   (sockaddr*)&from, sizeof(from));

            // Process in thread
            string cid_copy = cid;
            thread([cid_copy, udp](){ udp_process(cid_copy, udp); }).detach();
        }
    }

    return 0;
}
