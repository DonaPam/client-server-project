// client.cpp
// Compile: g++ client.cpp -o client -std=c++17

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include "protocol.h"
using namespace std;

/* -----------------------------------------------------------------------
 *  UTILS
 * ----------------------------------------------------------------------- */

static vector<string> split_ws(const string &s) {
    vector<string> out; string t;
    for (char c : s) {
        if (isspace((unsigned char)c)) { 
            if (!t.empty()) { out.push_back(t); t.clear(); } 
        }
        else t.push_back(c);
    }
    if (!t.empty()) out.push_back(t);
    return out;
}

// Generate 8-char hex CID + '\0'
string gen_id() {
    static mt19937_64 rng((unsigned)chrono::high_resolution_clock::now().time_since_epoch().count());
    uint64_t v = rng();
    string s;
    for (int i=0;i<8;i++){ s.push_back("0123456789ABCDEF"[v&15]); v >>=4; }
    s.push_back('\0');
    return s;
}

void show_usage(const char* program_name) {
    cout << "Usage:\n"
         << "  " << program_name << " <IP> <TCP|UDP> <PORT>\n"
         << "Example:\n"
         << "  " << program_name << " 127.0.0.1 TCP 1234\n\n";
}

bool parse_arguments(int argc, char* argv[], string& server_ip, int& proto, int& port) {
    if (argc != 4) return false;

    server_ip = argv[1];

    string pr = argv[2];
    transform(pr.begin(), pr.end(), pr.begin(), ::toupper);
    if (pr == "TCP") proto = 1;
    else if (pr == "UDP") proto = 2;
    else return false;

    try {
        port = stoi(argv[3]);
        if (port < 1 || port > 65535) return false;
    } catch(...) { return false; }

    return true;
}

/* -----------------------------------------------------------------------
 *  GRAPH DATA INPUT
 * ----------------------------------------------------------------------- */

bool get_graph_data(int& n, int& m, int& s, int& t, 
                    vector<int>& mat, vector<int>& weights)
{
    cout << "=== Graph Data Input ===\n";
    cout << "Type 'exit' to quit.\n\n";

    string tmp;

    // n
    cout << "Number of vertices [6..19]: ";
    cin >> tmp;
    if (tmp == "exit") return false;
    try { n = stoi(tmp); if(n<6||n>=20) throw 1; } catch(...) {
        cerr << "Invalid n\n"; return false;
    }

    // m
    cout << "Number of edges [6..19]: ";
    cin >> tmp;
    if (tmp == "exit") return false;
    try { m = stoi(tmp); if(m<6||m>=20) throw 1; } catch(...) {
        cerr << "Invalid m\n"; return false;
    }

    // s
    cout << "Start vertex (0.." << (n-1) << "): ";
    cin >> tmp;
    if (tmp == "exit") return false;
    try { s = stoi(tmp); if(s<0||s>=n) throw 1; } catch(...) {
        cerr << "Invalid start\n"; return false;
    }

    // t
    cout << "End vertex (0.." << (n-1) << "): ";
    cin >> tmp;
    if (tmp == "exit") return false;
    try { t = stoi(tmp); if(t<0||t>=n) throw 1; } catch(...) {
        cerr << "Invalid end\n"; return false;
    }

    // initialize
    mat.assign(n*m, 0);
    weights.assign(m, 0);

    cout<<"\nEnter "<<m<<" edges in incidence form.\n";
    cout<<"For each edge e, enter: u v w\n"
        <<"  u = vertex with +w\n"
        <<"  v = vertex with -w\n"
        <<"  w = non-negative weight\n\n";

    for(int e=0; e<m; e++){
        cout<<"Edge "<<e<<": ";
        string u_str, v_str, w_str;
        cin >> u_str;
        if(u_str=="exit") return false;
        cin >> v_str;
        if(v_str=="exit") return false;
        cin >> w_str;
        if(w_str=="exit") return false;

        int u,v,w;
        try {
            u=stoi(u_str); v=stoi(v_str); w=stoi(w_str);
            if(u<0||u>=n||v<0||v>=n||w<0) throw 1;
        } catch(...) {
            cerr<<"Invalid edge\n"; return false;
        }

        mat[u*m + e] = w;
        mat[v*m + e] = -w;
        weights[e] = w;
    }

    return true;
}

/* -----------------------------------------------------------------------
 *  TCP SEND
 * ----------------------------------------------------------------------- */

bool send_graph_to_server_tcp(
    const string& server_ip, int port,
    int n,int m,int s,int t,
    const vector<int>& mat,
    const vector<int>& weights)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock<0){ perror("socket"); return false; }

    sockaddr_in srv{}; 
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    inet_pton(AF_INET, server_ip.c_str(), &srv.sin_addr);

    if(connect(sock,(sockaddr*)&srv,sizeof(srv))<0){
        perror("connect");
        close(sock);
        return false;
    }

    GraphRequest req{n,m,s,t,0};
    if(send(sock,&req,sizeof(req),0)!=sizeof(req)){
        perror("send req");
        close(sock); return false;
    }
    if(send(sock,mat.data(),mat.size()*sizeof(int),0) 
       != (ssize_t)mat.size()*sizeof(int)){
        perror("send mat");
        close(sock); return false;
    }
    if(send(sock,weights.data(),weights.size()*sizeof(int),0)
       != (ssize_t)weights.size()*sizeof(int)){
        perror("send weights");
        close(sock); return false;
    }

    GraphResponse R{};
    ssize_t got = recv(sock,&R,sizeof(R),MSG_WAITALL);

    if(got==sizeof(R)){
        if(R.error_code==0){
            cout<<"\n=== RESULT (TCP) ===\n"
                <<"Path length: "<<R.path_length<<"\n"
                <<"Path: ";
            for(int i=0;i<R.path_size;i++)
                cout<<R.path[i]<<(i+1<R.path_size?"->":"");
            cout<<"\n";
        }
        else{
            cout<<"Server error: "<<R.message<<"\n";
        }
    }
    else cerr<<"No or incomplete response\n";

    close(sock);
    return true;
}

/* -----------------------------------------------------------------------
 *  UDP RELIABLE SEND
 * ----------------------------------------------------------------------- */

bool send_graph_to_server_udp(
    const string& server_ip, int port,
    int n,int m,int s,int t,
    const vector<int>& mat,
    const vector<int>& weights)
{
    int sock = socket(AF_INET,SOCK_DGRAM,0);
    if(sock<0){ perror("socket"); return false; }

    sockaddr_in srv{};
    srv.sin_family=AF_INET;
    srv.sin_port=htons(port);
    inet_pton(AF_INET,server_ip.c_str(),&srv.sin_addr);

    string cid_s = gen_id();
    char cid[9]; memcpy(cid,cid_s.c_str(),9);

    /* -------- helper lambdas to send packets -------- */

    auto send_header = [&](int sock)->bool{
        vector<uint8_t> buf(sizeof(UdpPacketHeader)+16);
        UdpPacketHeader* h=(UdpPacketHeader*)buf.data();
        memcpy(h->cid,cid,9); h->type=UDP_HEADER;
        uint8_t* p = buf.data()+sizeof(UdpPacketHeader);
        auto put = [&](int32_t x){ int32_t y=htonl(x); memcpy(p,&y,4); p+=4; };
        put(n); put(m); put(s); put(t);
        return sendto(sock,buf.data(),buf.size(),0,(sockaddr*)&srv,sizeof(srv))
                == (ssize_t)buf.size();
    };

    auto send_row = [&](int sock,int row)->bool{
        vector<uint8_t> buf(sizeof(UdpPacketHeader)+4 + m*4);
        UdpPacketHeader* h=(UdpPacketHeader*)buf.data();
        memcpy(h->cid,cid,9); h->type=UDP_ROW;
        uint8_t* p = buf.data()+sizeof(UdpPacketHeader);
        auto put = [&](int32_t x){ int32_t y=htonl(x); memcpy(p,&y,4); p+=4; };
        put(row);
        for(int j=0;j<m;j++) put(mat[row*m+j]);
        return sendto(sock,buf.data(),buf.size(),0,(sockaddr*)&srv,sizeof(srv))
                == (ssize_t)buf.size();
    };

    auto send_weights = [&](int sock)->bool{
        vector<uint8_t> buf(sizeof(UdpPacketHeader)+4 + m*4);
        UdpPacketHeader* h=(UdpPacketHeader*)buf.data();
        memcpy(h->cid,cid,9); h->type=UDP_WEIGHTS;
        uint8_t* p = buf.data()+sizeof(UdpPacketHeader);
        auto put = [&](int32_t x){ int32_t y=htonl(x); memcpy(p,&y,4); p+=4; };
        put(m);
        for(int j=0;j<m;j++) put(weights[j]);
        return sendto(sock,buf.data(),buf.size(),0,(sockaddr*)&srv,sizeof(srv))
                == (ssize_t)buf.size();
    };

    auto send_fin = [&](int sock)->bool{
        vector<uint8_t> buf(sizeof(UdpPacketHeader));
        UdpPacketHeader* h=(UdpPacketHeader*)buf.data();
        memcpy(h->cid,cid,9); h->type=UDP_FIN;
        return sendto(sock,buf.data(),buf.size(),0,(sockaddr*)&srv,sizeof(srv))
                == (ssize_t)buf.size();
    };

    /* -------- sequence sender -------- */

    auto send_sequence = [&](){
        send_header(sock);
        for(int i=0;i<n;i++) send_row(sock,i);
        send_weights(sock);
        send_fin(sock);
    };

    /* -------- retry loop (ACK) -------- */

    bool acked = false;
    int attempts = 0;
    const int MAX_ATTEMPTS = 3;

    uint8_t recvbuf[4096];

    while(attempts < MAX_ATTEMPTS && !acked){
        attempts++;
        send_sequence();

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock,&fds);
        struct timeval tv; tv.tv_sec=3; tv.tv_usec=0;

        int rv = select(sock+1, &fds, nullptr, nullptr, &tv);
        if(rv>0 && FD_ISSET(sock,&fds)){
            sockaddr_in from; socklen_t L=sizeof(from);
            ssize_t r = recvfrom(sock, recvbuf, sizeof(recvbuf), 0,
                                 (sockaddr*)&from, &L);

            if(r >= (ssize_t)sizeof(UdpPacketHeader)){
                UdpPacketHeader* h=(UdpPacketHeader*)recvbuf;

                if(strncmp(h->cid,cid,8)==0){
                    if(h->type == UDP_ACK){
                        acked = true;
                        break;
                    }
                    else if(h->type == UDP_RESULT){
                        // server sent result directly
                        uint8_t* p = recvbuf+sizeof(UdpPacketHeader);
                        auto get = [&](int32_t& x){
                            x = ntohl(*(int32_t*)p); p+=4;
                        };
                        int32_t dist, sz;
                        get(dist); get(sz);

                        cout<<"\n=== RESULT (UDP) ===\nPath length: "<<dist<<"\nPath: ";
                        for(int i=0;i<sz;i++){
                            int32_t node; get(node);
                            cout<<node<<(i+1<sz?"->":"");
                        }
                        cout<<"\n";
                        close(sock);
                        return true;
                    }
                }
            }
        }
        // else timeout → retry
    }

    if(!acked){
        cout<<"Perte de connexion (UDP) après "<<attempts<<" tentatives.\n";
        close(sock);
        return false;
    }

    /* -------- ACK received → wait for server result -------- */

    fd_set fds;
    FD_ZERO(&fds); FD_SET(sock,&fds);
    struct timeval tv; tv.tv_sec=10; tv.tv_usec=0;

    int rv = select(sock+1,&fds,nullptr,nullptr,&tv);
    if(rv>0 && FD_ISSET(sock,&fds)){
        sockaddr_in from; socklen_t L=sizeof(from);
        ssize_t r = recvfrom(sock, recvbuf, sizeof(recvbuf),0,(sockaddr*)&from,&L);

        if(r >= (ssize_t)sizeof(UdpPacketHeader)){
            UdpPacketHeader* h=(UdpPacketHeader*)recvbuf;
            if(strncmp(h->cid,cid,8)==0 && h->type==UDP_RESULT){
                uint8_t* p = recvbuf+sizeof(UdpPacketHeader);
                int32_t dist, sz; 
                auto get = [&](int32_t& x){ x = ntohl(*(int32_t*)p); p+=4; };
                get(dist); get(sz);

                cout<<"\n=== RESULT (UDP) ===\nPath length: "<<dist<<"\nPath: ";
                for(int i=0;i<sz;i++){
                    int32_t node; get(node);
                    cout<<node<<(i+1<sz?"->":"");
                }
                cout<<"\n";
                close(sock);
                return true;
            }
        }
        cout<<"Unexpected server packet.\n";
    }
    else{
        cout<<"Timeout waiting final server result after ACK\n";
    }

    close(sock);
    return false;
}

/* -----------------------------------------------------------------------
 * DISPATCH
 * ----------------------------------------------------------------------- */

bool send_graph_to_server(const string& server_ip, int proto, int port,
                         int n, int m, int s, int t,
                         const vector<int>& mat, const vector<int>& weights)
{
    if(proto==1) return send_graph_to_server_tcp(server_ip, port, n,m,s,t,mat,weights);
    else         return send_graph_to_server_udp(server_ip, port, n,m,s,t,mat,weights);
}

/* -----------------------------------------------------------------------
 * MAIN
 * ----------------------------------------------------------------------- */

int main(int argc, char* argv[]){
    string server_ip;
    int proto = 0, port = 0;

    if(!parse_arguments(argc,argv,server_ip,proto,port)){
        show_usage(argv[0]);
        return 1;
    }

    cout << "=== Graph Theory Client ===\n";
    cout << "Server: " << server_ip 
         << ":" << port 
         << " (" << (proto==1?"TCP":"UDP") << ")\n";

    int n,m,s,t;
    vector<int> mat, weights;

    if(!get_graph_data(n,m,s,t,mat,weights))
        return 0;

    send_graph_to_server(server_ip, proto, port, n,m,s,t,mat,weights);

    return 0;
}
