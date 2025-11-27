// client.cpp
// Compile: g++ client.cpp -o client -std=c++17

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"
using namespace std;

static vector<string> split_ws(const string &s) {
    vector<string> out; string t;
    for (char c : s) {
        if (isspace((unsigned char)c)) { if (!t.empty()) { out.push_back(t); t.clear(); } }
        else t.push_back(c);
    }
    if (!t.empty()) out.push_back(t);
    return out;
}

string gen_id() {
    static mt19937_64 rng((unsigned)chrono::high_resolution_clock::now().time_since_epoch().count());
    uint64_t v = rng();
    string s;
    for (int i=0;i<8;i++){ s.push_back("0123456789ABCDEF"[v&15]); v >>=4; }
    return s;
}

int main(){
    cout<<"Protocol to use? (1=TCP, 2=UDP): ";
    int proto; cin>>proto;
    string server_ip; int port;
    cout<<"Server IP: "; cin>>server_ip;
    cout<<"Server port: "; cin>>port;

    int n,m; cout<<"Number of vertices (7..19): "; cin>>n;
    cout<<"Number of edges (7..19): "; cin>>m;
    if (!(n>6 && n<20 && m>6 && m<20)) { cerr<<"n and m must be >6 and <20\n"; return 1; }
    int s,t; cout<<"Start vertex (0.."<<n-1<<"): "; cin>>s; cout<<"End vertex: "; cin>>t;

    vector<int> mat(n*m,0);
    vector<int> weights(m,1);
    cout<<"Enter each edge as: u v w  (0-based u,v)\n";
    for (int e=0;e<m;e++){
        int u,v,w; cout<<"Edge "<<e<<": "; cin>>u>>v>>w;
        if (u<0||u>=n||v<0||v>=n){ cerr<<"Invalid vertices\n"; return 1;}
        mat[u*m + e] = (w>0? w : -w);
        mat[v*m + e] = (w>0? -w : w);
        weights[e] = abs(w);
    }

    if (proto == 1) {
        // TCP: binary send GraphRequest + matrix + weights, receive GraphResponse
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { perror("socket"); return 1; }
        sockaddr_in srv{}; srv.sin_family = AF_INET; srv.sin_port = htons(port);
        if (inet_pton(AF_INET, server_ip.c_str(), &srv.sin_addr) <= 0) { cerr<<"Bad IP\n"; return 1; }
        if (connect(sock, (sockaddr*)&srv, sizeof(srv)) < 0) { perror("connect"); return 1; }
        GraphRequest req{}; req.vertices = n; req.edges = m; req.start_node = s; req.end_node = t; req.weighted = 1;
        send(sock, &req, sizeof(req), 0);
        send(sock, mat.data(), mat.size()*sizeof(int), 0);
        send(sock, weights.data(), weights.size()*sizeof(int), 0);
        GraphResponse resp; ssize_t r = recv(sock, &resp, sizeof(resp), MSG_WAITALL);
        if (r == sizeof(resp)) {
            if (resp.error_code == 0) {
                cout<<"Path length: "<<resp.path_length<<"\nPath: ";
                for (int i=0;i<resp.path_size;i++){ cout<<resp.path[i]; if(i+1<resp.path_size) cout<<" -> "; }
                cout<<"\n";
            } else cout<<"Error from server: "<<resp.message<<"\n";
        } else cerr<<"Bad response\n";
        close(sock);
    } else {
        // UDP: send text messages as in server expectations
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) { perror("socket"); return 1; }
        sockaddr_in srv{}; srv.sin_family = AF_INET; srv.sin_port = htons(port);
        if (inet_pton(AF_INET, server_ip.c_str(), &srv.sin_addr) <= 0) { cerr<<"Bad IP\n"; return 1; }

        string cid = gen_id();
        // HEADER: cid HEADER n m s t
        string header = cid + " HEADER " + to_string(n) + " " + to_string(m) + " " + to_string(s) + " " + to_string(t);
        sendto(sock, header.c_str(), header.size(), 0, (sockaddr*)&srv, sizeof(srv));
        // ROWS: cid ROW <m values> per row
        for (int i=0;i<n;i++){
            string row = cid + " ROW";
            for (int e=0;e<m;e++) row += " " + to_string(mat[i*m + e]);
            sendto(sock, row.c_str(), row.size(), 0, (sockaddr*)&srv, sizeof(srv));
        }
        // WEIGHTS
        string line = cid + " WEIGHTS";
        for (int e=0;e<m;e++) line += " " + to_string(weights[e]);
        sendto(sock, line.c_str(), line.size(), 0, (sockaddr*)&srv, sizeof(srv));
        // FIN
        string fin = cid + " FIN bye";
        sendto(sock, fin.c_str(), fin.size(), 0, (sockaddr*)&srv, sizeof(srv));

        // wait for response
        char buf[8192]; sockaddr_in from; socklen_t fl = sizeof(from);
        ssize_t nr = recvfrom(sock, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fl);
        if (nr <= 0) { cerr<<"No response\n"; close(sock); return 1; }
        buf[nr] = '\0'; string resp(buf);

       // ======== FORMAT HUMAN-READABLE COMME TCP =========
auto p = split_ws(resp);
if (p.size() >= 4 && p[0] == "SERVER_RESPONSE" && p[1] == "OK") {
    int length = stoi(p[2]);
    int path_size = stoi(p[3]);
    cout << "\n=== RESULT (UDP) ===\n";
    cout << "Path length: " << length << "\n";
    cout << "Path: ";
    for (int i = 0; i < path_size; i++) {
        cout << p[4+i];
        if (i < path_size - 1) cout << "->";
    }
    cout << "\n";
} else {
    cout << "\nError from server: " << resp << "\n";
}

        close(sock);
    }

    return 0;
}
