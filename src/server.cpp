// server.cpp
// Compile: g++ server.cpp -o server -std=c++17 -pthread

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <atomic>
#include "protocol.h"
using namespace std;

bool valid_nm(int n, int m){
    return (n >=6 && n <20 && m >=6 && m <20);
}

// split helper
vector<string> split_ws(const string &s){
    vector<string> out; string t;
    for(char c: s){
        if(isspace((unsigned char)c)){ if(!t.empty()){ out.push_back(t); t.clear(); } }
        else t.push_back(c);
    }
    if(!t.empty()) out.push_back(t);
    return out;
}

// dijkstra + build_adj same as before
long long INF = (1LL<<60);
struct PathResult{ long long dist; vector<int> path; bool ok; };

PathResult dijkstra(int n, vector<vector<pair<int,int>>>& adj,int S,int T){
    vector<long long> dist(n,INF);
    vector<int> parent(n,-1);
    dist[S]=0;
    priority_queue<pair<long long,int>,vector<pair<long long,int>>,greater<>> pq;
    pq.push({0,S});
    while(!pq.empty()){
        auto [d,u]=pq.top(); pq.pop();
        if(d!=dist[u]) continue;
        if(u==T) break;
        for(auto &ed: adj[u]){
            int v=ed.first,w=ed.second;
            if(dist[v]>dist[u]+w){
                dist[v]=dist[u]+w;
                parent[v]=u;
                pq.push({dist[v],v});
            }
        }
    }
    PathResult r;
    if(dist[T]==INF){ r.ok=false; return r; }
    r.ok=true; r.dist=dist[T];
    int x=T;
    while(x!=-1){ r.path.push_back(x); x=parent[x]; }
    reverse(r.path.begin(),r.path.end());
    return r;
}

vector<vector<pair<int,int>>> build_adj(int n,int m, const vector<int>& flat, const vector<int>& W){
    vector<vector<pair<int,int>>> adj(n);
    for(int e=0;e<m;e++){
        int a=-1,b=-1;
        for(int v=0; v<n; v++){
            int val = flat[v*m+e];
            if(val!=0){
                if(a==-1) a=v;
                else if(b==-1) b=v;
                else { a=-2; break; } // more than 2
            }
        }
        if(a<0 || b<0) continue;
        int w = W[e];
        if(w<0) w = -w;
        adj[a].push_back({b,w});
        adj[b].push_back({a,w});
    }
    return adj;
}

// ---------------- TCP handler with concurrency limit ----------------
atomic<int> tcp_clients_count{0};
const int TCP_CLIENTS_MAX = 3;

void handle_tcp(int client){
    if(tcp_clients_count.fetch_add(1) >= TCP_CLIENTS_MAX){
        // already exceed, reject politely
        tcp_clients_count--;
        GraphResponse resp{};
        resp.error_code = 1;
        resp.path_length = -1;
        snprintf(resp.message, sizeof(resp.message), "Server busy: too many TCP clients");
        send(client,&resp,sizeof(resp),0);
        close(client);
        return;
    }

    cout<<"[TCP] Client connected. current="<<tcp_clients_count.load()<<"\n";
    GraphRequest req;
    if(recv(client,&req,sizeof(req),MSG_WAITALL)!=sizeof(req)){ close(client); tcp_clients_count--; return; }

    GraphResponse resp{}; resp.error_code=1; resp.path_length=-1;
    if(!valid_nm(req.vertices, req.edges)){
        snprintf(resp.message,sizeof(resp.message),"n and m should be between >=6 and <20");
        send(client,&resp,sizeof(resp),0); close(client); tcp_clients_count--; return;
    }
    int n=req.vertices, m=req.edges;
    if(req.start_node < 0 || req.start_node >= n || req.end_node < 0 || req.end_node >= n){
        snprintf(resp.message,sizeof(resp.message),"Invalid start/end nodes");
        send(client,&resp,sizeof(resp),0); close(client); tcp_clients_count--; return;
    }

    vector<int> mat(n*m);
    vector<int> W(m);
    ssize_t needMat = (ssize_t)mat.size()*sizeof(int);
    ssize_t got = recv(client,mat.data(),needMat,MSG_WAITALL);
    if(got != needMat){ close(client); tcp_clients_count--; return; }
    ssize_t needW = (ssize_t)W.size()*sizeof(int);
    got = recv(client,W.data(),needW,MSG_WAITALL);
    if(got != needW){ close(client); tcp_clients_count--; return; }

    // Validate matrix: for each column e, exactly 2 non-zero entries, signs opposite
    for(int e=0;e<m;e++){
        int cnt=0; int pos=-1, neg=-1;
        for(int v=0; v<n; v++){
            int val = mat[v*m+e];
            if(val!=0){
                cnt++;
                if(val>0) pos=v; else neg=v;
            }
        }
        if(cnt!=2 || pos==-1 || neg==-1){
            snprintf(resp.message,sizeof(resp.message),"Invalid incidence column %d",e);
            send(client,&resp,sizeof(resp),0); close(client); tcp_clients_count--; return;
        }
    }

    auto adj = build_adj(n,m,mat,W);
    auto R = dijkstra(n,adj,req.start_node,req.end_node);
    if(!R.ok){
        resp.error_code=1;
        resp.path_size=0; resp.path_length=-1;
        snprintf(resp.message,sizeof(resp.message),"No path found");
    } else {
        resp.error_code=0;
        resp.path_length = (int)R.dist;
        resp.path_size = (int)R.path.size();
        if(resp.path_size > (int)(sizeof(resp.path)/sizeof(resp.path[0]))){
            // too large (safety)
            snprintf(resp.message,sizeof(resp.message),"Path too long");
            resp.error_code = 1;
        } else {
            for(size_t i=0;i<R.path.size();i++) resp.path[i]=R.path[i];
            snprintf(resp.message,sizeof(resp.message),"OK");
        }
    }
    send(client,&resp,sizeof(resp),0);
    close(client);
    tcp_clients_count--;
}

// ---------------- UDP handling with ACKs and concurrency limit ----------------
struct Udbuf {
    sockaddr_in addr;
    int n=-1,m=-1,S=-1,T=-1;
    vector<vector<int>> rows; // rows by index
    vector<int> weights;
    bool have_header=false;
    bool have_weights=false;
    int received_rows=0;
    chrono::steady_clock::time_point last_seen;
};

mutex U_m;
unordered_map<string, Udbuf> U;

atomic<int> udp_tasks_count{0};
const int UDP_TASKS_MAX = 3;

void udp_process_and_reply(const string& cid, int udp_sock){
    // limit concurrent processing
    if(udp_tasks_count.fetch_add(1) >= UDP_TASKS_MAX){
        udp_tasks_count--;
        // queueing could be implemented; for now respond busy
        // send SERVER_RESPONSE ERROR BUSY
        return;
    }

    Udbuf buf;
    {
        lock_guard<mutex> lk(U_m);
        buf = U[cid];
        U.erase(cid);
    }

    // validate
    if(!buf.have_header || !buf.have_weights || buf.received_rows != buf.n){
        // send error
        string err = string(cid) + " ERROR Incomplete data";
        sendto(udp_sock, err.c_str(), err.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
        udp_tasks_count--;
        return;
    }
    int n = buf.n, m = buf.m, S = buf.S, T = buf.T;
    if(!valid_nm(n,m) || S<0 || S>=n || T<0 || T>=n){
        string err = string(cid) + " ERROR Bad params";
        sendto(udp_sock, err.c_str(), err.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
        udp_tasks_count--;
        return;
    }

    // flatten
    vector<int> flat(n*m);
    for(int i=0;i<n;i++) for(int j=0;j<m;j++) flat[i*m+j]=buf.rows[i][j];

    // validate columns
    for(int e=0;e<m;e++){
        int cnt=0; int pos=-1, neg=-1;
        for(int v=0; v<n; v++){
            int val = flat[v*m+e];
            if(val!=0){
                cnt++;
                if(val>0) pos=v; else neg=v;
            }
        }
        if(cnt!=2 || pos==-1 || neg==-1){
            string err = string(cid) + " ERROR Invalid incidence col";
            sendto(udp_sock, err.c_str(), err.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
            udp_tasks_count--;
            return;
        }
    }

    auto adj = build_adj(n,m,flat,buf.weights);
    auto R = dijkstra(n,adj,S,T);
    if(!R.ok){
        // send error
        string err = string(cid) + " ERROR No Path";
        sendto(udp_sock, err.c_str(), err.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));
        udp_tasks_count--;
        return;
    }

    // build UDP_RESULT binary: header + dist + sz + nodes
    vector<uint8_t> out(sizeof(UdpPacketHeader) + 4 + 4 + 4*R.path.size());
    UdpPacketHeader *h = (UdpPacketHeader*)out.data();
    memcpy(h->cid, cid.c_str(), 9);
    h->type = UDP_RESULT;
    uint8_t *p = out.data() + sizeof(UdpPacketHeader);
    int32_t tmp = htonl((int32_t)R.dist); memcpy(p,&tmp,4); p+=4;
    tmp = htonl((int32_t)R.path.size()); memcpy(p,&tmp,4); p+=4;
    for(int v: R.path){ tmp = htonl(v); memcpy(p,&tmp,4); p+=4; }
    sendto(udp_sock, out.data(), out.size(), 0, (sockaddr*)&buf.addr, sizeof(buf.addr));

    udp_tasks_count--;
}

int main(int argc,char**argv){
    if(argc!=2){ cout<<"Usage: ./server <port>\n"; return 0;}
    int PORT=atoi(argv[1]);

    int tcp_sock = socket(AF_INET,SOCK_STREAM,0);
    int udp_sock = socket(AF_INET,SOCK_DGRAM,0);
    if(tcp_sock<0||udp_sock<0){ perror("socket"); return 1; }

    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(PORT); a.sin_addr.s_addr=INADDR_ANY;
    bind(tcp_sock,(sockaddr*)&a,sizeof(a));
    bind(udp_sock,(sockaddr*)&a,sizeof(a));
    listen(tcp_sock,10);
    cout<<"SERVER active on port "<<PORT<<" (TCP+UDP)\n";

    // TCP accept loop in a thread
    thread([&](){
        while(true){
            sockaddr_in c; socklen_t L=sizeof(c);
            int cl = accept(tcp_sock,(sockaddr*)&c,&L);
            if(cl<0) continue;
            thread(handle_tcp, cl).detach();
        }
    }).detach();

    // UDP main loop
    while(true){
        uint8_t buf[4096];
        sockaddr_in c; socklen_t L=sizeof(c);
        ssize_t r = recvfrom(udp_sock, buf, sizeof(buf)-1, 0, (sockaddr*)&c, &L);
        if(r<=0) continue;
        if(r < (ssize_t)sizeof(UdpPacketHeader)) continue;
        UdpPacketHeader *h = (UdpPacketHeader*)buf;
        string cid(h->cid, 8);

        lock_guard<mutex> lk(U_m);
        auto &Ue = U[cid];
        Ue.addr = c;
        Ue.last_seen = chrono::steady_clock::now();

        if(h->type == UDP_HEADER){
            if(r < (ssize_t)(sizeof(UdpPacketHeader)+4*4)) continue;
            uint8_t *p = buf + sizeof(UdpPacketHeader);
            int32_t n = ntohl(*(int32_t*)p); p+=4;
            int32_t m = ntohl(*(int32_t*)p); p+=4;
            int32_t S = ntohl(*(int32_t*)p); p+=4;
            int32_t T = ntohl(*(int32_t*)p); p+=4;
            Ue.n = n; Ue.m = m; Ue.S = S; Ue.T = T;
            if(valid_nm(n,m)){
                Ue.rows.assign(n, vector<int>(m,0));
                Ue.weights.assign(m,0);
                Ue.have_header = true;
            } else {
                // invalid header -> send immediate error
                string err = cid + string(" ERROR Bad header");
                sendto(udp_sock, err.c_str(), err.size(), 0, (sockaddr*)&c, sizeof(c));
            }
        } else if(h->type == UDP_ROW){
            if(!Ue.have_header) continue;
            if(r < (ssize_t)(sizeof(UdpPacketHeader)+4 + 4*Ue.m)) continue;
            uint8_t *p = buf + sizeof(UdpPacketHeader);
            int32_t row = ntohl(*(int32_t*)p); p+=4;
            if(row < 0 || row >= Ue.n) continue;
            for(int j=0;j<Ue.m;j++){
                int32_t val = ntohl(*(int32_t*)p); p+=4;
                Ue.rows[row][j] = val;
            }
            Ue.received_rows++;
        } else if(h->type == UDP_WEIGHTS){
            if(!Ue.have_header) continue;
            if(r < (ssize_t)(sizeof(UdpPacketHeader)+4 + 4*Ue.m)) continue;
            uint8_t *p = buf + sizeof(UdpPacketHeader);
            int32_t m_read = ntohl(*(int32_t*)p); p+=4;
            if(m_read != Ue.m) continue;
            for(int j=0;j<Ue.m;j++){
                int32_t w = ntohl(*(int32_t*)p); p+=4;
                Ue.weights[j] = w;
            }
            Ue.have_weights = true;
        } else if(h->type == UDP_FIN){
            // send ACK immediately
            vector<uint8_t> ack(sizeof(UdpPacketHeader));
            UdpPacketHeader *ah = (UdpPacketHeader*)ack.data();
            memcpy(ah->cid, h->cid, 9);
            ah->type = UDP_ACK;
            sendto(udp_sock, ack.data(), ack.size(), 0, (sockaddr*)&c, sizeof(c));

            // spawn processing thread
            string cid_s = cid;
            thread([cid_s, udp_sock](){
                udp_process_and_reply(cid_s, udp_sock);
            }).detach();
        }
    }

    close(tcp_sock);
    close(udp_sock);
    return 0;
}
