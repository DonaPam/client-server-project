// client.cpp
// Compile: g++ client.cpp -o client -std=c++17

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
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

void show_usage(const char* program_name) {
    cout << "Usage:\n";
    cout << "  " << program_name << " [IP] [TCP|UDP] [PORT]\n";
    cout << "  " << program_name << " (for interactive mode)\n\n";
    cout << "Examples:\n";
    cout << "  " << program_name << " 127.0.0.1 TCP 1234\n";
    cout << "  " << program_name << " 192.168.1.100 UDP 8080\n";
    cout << "  " << program_name << " (interactive mode)\n";
}

bool parse_arguments(int argc, char* argv[], string& server_ip, int& proto, int& port) {
    if (argc != 4) {
        return false;
    }
    
    server_ip = argv[1];
    
    string protocol_str = argv[2];
    transform(protocol_str.begin(), protocol_str.end(), protocol_str.begin(), ::toupper);
    if (protocol_str == "TCP") {
        proto = 1;
    } else if (protocol_str == "UDP") {
        proto = 2;
    } else {
        cerr << "Error: Protocol must be TCP or UDP\n";
        return false;
    }
    
    try {
        port = stoi(argv[3]);
        if (port < 1 || port > 65535) {
            cerr << "Error: Port must be between 1 and 65535\n";
            return false;
        }
    } catch (const exception& e) {
        cerr << "Error: Invalid port number\n";
        return false;
    }
    
    return true;
}

bool get_graph_data(int& n, int& m, int& s, int& t, vector<int>& mat, vector<int>& weights) {
    cout << "■ Data source?\n"
         << "1 = manual input\n"
         << "2 = load from .txt file\n"
         << "3 = exit\n> ";

    string input;
    cin >> input;
    
    if (input == "exit" || input == "3") {
        cout << "Goodbye!\n";
        return false;
    }
    
    int mode;
    try {
        mode = stoi(input);
    } catch (const exception& e) {
        cerr << "Invalid input\n";
        return false;
    }

    if(mode == 2){
        // ========== FILE READING ==========
        string filename;
        cout << "File name (or 'exit' to quit): ";
        cin >> filename;
        
        if (filename == "exit") {
            cout << "Goodbye!\n";
            return false;
        }

        ifstream fin(filename);
        if(!fin){ cerr<<"Unable to open file\n"; return false; }

        fin >> n >> m;
        fin >> s >> t;

        if(!(n>=6 && n<20 && m>=6 && m<20)){
            cerr<<"Error: n and m out of bounds [6..19]\n";
            return false;
        }

        mat.assign(n*m,0);
        weights.assign(m,1);

        for(int e=0;e<m;e++){
            int u,v,w;
            if(!(fin>>u>>v>>w)){ cerr<<"Invalid file format\n"; return false; }

            mat[u*m + e] = (w>0?w:-w);
            mat[v*m + e] = (w>0?-w:w);
            weights[e] = abs(w);
        }

        cout << "\nFile reading OK ✔\n\n";
    }
    else if(mode == 1) {
        // ========== MANUAL INPUT ==========
        cout<<"Number of vertices (6..19) or 'exit': "; 
        string input_n;
        cin >> input_n;
        if (input_n == "exit") {
            cout << "Goodbye!\n";
            return false;
        }
        n = stoi(input_n);
        
        cout<<"Number of edges (6..19) or 'exit': ";
        string input_m;
        cin >> input_m;
        if (input_m == "exit") {
            cout << "Goodbye!\n";
            return false;
        }
        m = stoi(input_m);
        
        if(!(n>=6 && n<20 && m>=6 && m<20)) { 
            cerr<<"Invalid n and m values\n"; 
            return false; 
        }

        cout<<"Start vertex or 'exit': ";
        string input_s;
        cin >> input_s;
        if (input_s == "exit") {
            cout << "Goodbye!\n";
            return false;
        }
        s = stoi(input_s);
        
        cout<<"End vertex or 'exit': ";
        string input_t;
        cin >> input_t;
        if (input_t == "exit") {
            cout << "Goodbye!\n";
            return false;
        }
        t = stoi(input_t);

        mat.assign(n*m,0);
        weights.assign(m,1);

        cout<<"\nEnter "<<m<<" edges in format: u v w (or 'exit' to quit)\n\n";
        for(int e=0;e<m;e++){
            cout<<"Edge "<<e<<" : ";
            string u_str, v_str, w_str;
            cin >> u_str;
            if (u_str == "exit") {
                cout << "Goodbye!\n";
                return false;
            }
            cin >> v_str;
            if (v_str == "exit") {
                cout << "Goodbye!\n";
                return false;
            }
            cin >> w_str;
            if (w_str == "exit") {
                cout << "Goodbye!\n";
                return false;
            }
            
            int u = stoi(u_str);
            int v = stoi(v_str);
            int w = stoi(w_str);

            mat[u*m+e] = (w>0?w:-w);
            mat[v*m+e] = (w>0?-w:w);
            weights[e] = abs(w);
        }
    }
    else {
        cerr << "Invalid choice\n";
        return false;
    }
    
    return true;
}

bool send_graph_to_server(const string& server_ip, int proto, int port, 
                         int n, int m, int s, int t, 
                         const vector<int>& mat, const vector<int>& weights) {
    //============================================ TCP MODE ===========================================
    if(proto==1){
        int sock=socket(AF_INET,SOCK_STREAM,0);
        if(sock<0){ perror("socket"); return false; }

        sockaddr_in srv{};
        srv.sin_family=AF_INET;
        srv.sin_port=htons(port);
        inet_pton(AF_INET,server_ip.c_str(),&srv.sin_addr);

        if(connect(sock,(sockaddr*)&srv,sizeof(srv))<0){ perror("connect"); return false; }

        GraphRequest req{n,m,s,t,1};
        send(sock,&req,sizeof(req),0);
        send(sock,mat.data(),mat.size()*sizeof(int),0);
        send(sock,weights.data(),weights.size()*sizeof(int),0);

        GraphResponse R;
        if(recv(sock,&R,sizeof(R),MSG_WAITALL)==sizeof(R)){
            cout<<"\n=== RESULT (TCP) ===\nPath length: "<<R.path_length<<"\nPath: ";
            for(int i=0;i<R.path_size;i++){
                cout<<R.path[i]<<(i+1<R.path_size?"->":"");
            }
            cout<<"\n";
        }
        close(sock);
        return true;
    }
    //============================================ UDP MODE ===========================================
    else {
        int sock=socket(AF_INET,SOCK_DGRAM,0);
        sockaddr_in srv{}; srv.sin_family=AF_INET;
        srv.sin_port=htons(port);
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

        char buf[4096];
        sockaddr_in from; socklen_t L=sizeof(from);
        int r=recvfrom(sock,buf,4095,0,(sockaddr*)&from,&L);
        buf[r]='\0';

        auto p=split_ws(buf);
        if(p.size()>=4 && p[1]=="OK"){
            cout<<"\n=== RESULT (UDP) ===\nPath length: "<<p[2]<<"\nPath: ";
            int sz=stoi(p[3]);
            for(int i=0;i<sz;i++){
                cout<<p[4+i]<<(i+1<sz?"->":"");
            } cout<<"\n";
        } else {
            cout<<"Server error: "<<buf<<"\n";
            close(sock);
            return false;
        }

        close(sock);
        return true;
    }
}

int main(int argc, char* argv[]){
    string server_ip;
    int proto = 0;
    int port = 0;

    // Mode avec arguments en ligne de commande
    if (argc == 4) {
        if (!parse_arguments(argc, argv, server_ip, proto, port)) {
            show_usage(argv[0]);
            return 1;
        }
        
        // Dans le mode avec arguments, on demande les données du graphe
        int n, m, s, t;
        vector<int> mat, weights;
        
        cout << "=== Graph Theory Client (Direct Mode) ===\n";
        cout << "Server: " << server_ip << ":" << port << " (" << (proto == 1 ? "TCP" : "UDP") << ")\n";
        cout << "Type 'exit' at any prompt to quit\n\n";
        
        if (!get_graph_data(n, m, s, t, mat, weights)) {
            return 0;
        }
        
        // Obtenir les détails de connexion en mode interactif
        if (argc == 1) {
            cout<<"\nProtocol to use? 1=TCP  2=UDP (or 'exit'): ";
            string proto_str;
            cin >> proto_str;
            if (proto_str == "exit") {
                cout << "Goodbye!\n";
                return 0;
            }
            proto = stoi(proto_str);
            
            cout<<"Server IP (or 'exit'): "; 
            cin >> server_ip;
            if (server_ip == "exit") {
                cout << "Goodbye!\n";
                return 0;
            }
            
            cout<<"Port (or 'exit'): ";
            string port_str;
            cin >> port_str;
            if (port_str == "exit") {
                cout << "Goodbye!\n";
                return 0;
            }
            port = stoi(port_str);
        }
        
        send_graph_to_server(server_ip, proto, port, n, m, s, t, mat, weights);
        return 0;
    }
    // Mode interactif
    else if (argc == 1) {
        cout << "=== Graph Theory Client ===\n";
        cout << "Type 'exit' at any prompt to quit\n\n";
        
        while(true) {
            int n, m, s, t;
            vector<int> mat, weights;
            
            if (!get_graph_data(n, m, s, t, mat, weights)) {
                break;
            }
            
            cout<<"\nProtocol to use? 1=TCP  2=UDP (or 'exit'): ";
            string proto_str;
            cin >> proto_str;
            if (proto_str == "exit") {
                cout << "Goodbye!\n";
                break;
            }
            int proto = stoi(proto_str);
            
            cout<<"Server IP (or 'exit'): "; 
            cin >> server_ip;
            if (server_ip == "exit") {
                cout << "Goodbye!\n";
                break;
            }
            
            cout<<"Port (or 'exit'): ";
            string port_str;
            cin >> port_str;
            if (port_str == "exit") {
                cout << "Goodbye!\n";
                break;
            }
            int port = stoi(port_str);
            
            send_graph_to_server(server_ip, proto, port, n, m, s, t, mat, weights);
            
            cout << "\nPress Enter to continue or type 'exit' to quit: ";
            string continue_cmd;
            cin.ignore();
            getline(cin, continue_cmd);
            if (continue_cmd == "exit") {
                cout << "Goodbye!\n";
                break;
            }
        }
    }
    else {
        show_usage(argv[0]);
        return 1;
    }

    return 0;
}
