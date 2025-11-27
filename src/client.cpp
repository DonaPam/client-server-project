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
    for (int i = 0; i < 8; i++) { s.push_back("0123456789ABCDEF"[v & 15]); v >>= 4; }
    return s;
}

int main(int argc, char** argv) {
    if(argc != 4){
        cerr << "Usage: ./calc_client <IP> <TCP|UDP> <PORT>\n";
        return 1;
    }

    string server_ip = argv[1];
    string proto_str = argv[2];
    int port = stoi(argv[3]);
    bool use_tcp = (proto_str == "TCP" || proto_str == "tcp");

    cout << "Client ready. Type 'exit' to quit.\n";

    while(true){
        string line;
        cout << "\nDo you want to enter the graph manually or from file? (manual/file) > ";
        cin >> line;
        if(line == "exit") break;

        int n, m, s, t;
        vector<int> mat, weights;

        if(line == "file"){
            string filename;
            cout << "Filename: ";
            cin >> filename;
            ifstream fin(filename);
            if(!fin){ cerr << "Cannot open file\n"; continue; }

            fin >> n >> m >> s >> t;
            if(!(n>=6 && n<=20 && m>=6 && m<=20)){ cerr << "n and m out of range\n"; continue; }

            mat.assign(n*m,0); weights.assign(m,1);
            for(int e=0;e<m;e++){
                int u,v,w; fin>>u>>v>>w;
                mat[u*m+e] = (w>0?w:-w);
                mat[v*m+e] = (w>0?-w:w);
                weights[e] = abs(w);
            }
            cout << "File read OK ✔\n";
        } else {
            cout << "Number of vertices (6..20): "; cin >> n;
            cout << "Number of edges (6..20): "; cin >> m;
            if(!(n>=6 && n<=20 && m>=6 && m<=20)){ cerr << "Invalid n or m\n"; continue; }

            cout << "Start vertex: "; cin >> s;
            cout << "End vertex: "; cin >> t;

            mat.assign(n*m,0); weights.assign(m,1);
            for(int e=0;e<m;e++){
                int u,v,w; cout << "Edge " << e << ": "; cin >> u >> v >> w;
                mat[u*m+e] = (w>0?w:-w);
                mat[v*m+e] = (w>0?-w:w);
                weights[e] = abs(w);
            }
        }

        if(use_tcp){
            int sock=socket(AF_INET,SOCK_STREAM,0);
            if(sock<0){ perror("socket"); continue; }

            sockaddr_in srv{}; srv.sin_family=AF_INET; srv.sin_port=htons(port);
            inet_pton(AF_INET,server_ip.c_str(),&srv.sin_addr);
            if(connect(sock,(sockaddr*)&srv,sizeof(srv))<0){ perror("connect"); close(sock); continue; }

            GraphRequest req{n,m,s,t,1};
            send(sock,&req,sizeof(req),0);
            send(sock,mat.data(),mat.size()*sizeof(int),0);
            send(sock,weights.data(),weights.size()*sizeof(int),0);

            GraphResponse R;
            if(recv(sock,&R,sizeof(R),MSG_WAITALL)==sizeof(R)){
                cout<<"\n=== RESULT (TCP) ===\nPath length: "<<R.path_length<<"\nPath: ";
                for(int i=0;i<R.path_size;i++) cout<<R.path[i]<<(i+1<R.path_size?"->":"");
                cout<<"\n";
            }
            close(sock);
        } else {
            int sock=socket(AF_INET,SOCK_DGRAM,0);
            sockaddr_in srv{}; srv.sin_family=AF_INET; srv.sin_port=htons(port);
            inet_pton(AF_INET,server_ip.c_str(),&srv.sin_addr);

            string cid=gen_id();
            string H=cid+" HEADER "+to_string(n)+" "+to_string(m)+" "+to_string(s)+" "+to_string(t);
            sendto(sock,H.c_str(),H.size(),0,(sockaddr*)&srv,sizeof(srv));

            for(int i=0;i<n;i++){
                string row=cid+" ROW";
                for(int e=0;e<m;e++) row+=" "+to_string(mat[i*m+e]);
                sendto(sock,row.c_str(),row.size(),0,(sockaddr*)&srv,sizeof(srv));
            }

            string W=cid+" WEIGHTS";
            for(int x:weights) W+=" "+to_string(x);
            sendto(sock,W.c_str(),W.size(),0,(sockaddr*)&srv,sizeof(srv));

            string FIN=cid+" FIN";
            sendto(sock,FIN.c_str(),FIN.size(),0,(sockaddr*)&srv,sizeof(srv));

            char buf[4096]; sockaddr_in from; socklen_t L=sizeof(from);
            int r=recvfrom(sock,buf,4095,0,(sockaddr*)&from,&L); buf[r]='\0';

            auto p=split_ws(buf);
            if(p.size()>=4 && p[1]=="OK"){
                cout<<"\n=== RESULT (UDP) ===\nPath length: "<<p[2]<<"\nPath: ";
                int sz=stoi(p[3]);
                for(int i=0;i<sz;i++) cout<<p[4+i]<<(i+1<sz?"->":"");
                cout<<"\n";
            } else cout<<"Server error: "<<buf<<"\n";

            close(sock);
        }
    }

    cout << "Client exiting...\n";
    return 0;
}
