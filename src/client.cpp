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

    cout << "■ Source des données ?\n"
         << "1 = saisir manuellement\n"
         << "2 = charger depuis un fichier .txt\n> ";

    int mode; cin >> mode;

    int n,m,s,t;
    vector<int> mat,weights;

    if(mode == 2){
        // ========== LECTURE FICHIER ==========
        string filename;
        cout << "Nom du fichier : ";
        cin >> filename;

        ifstream fin(filename);
        if(!fin){ cerr<<"Impossible d'ouvrir le fichier\n"; return 1; }

        fin >> n >> m;
        fin >> s >> t;

        if(!(n>=6 && n<20 && m>=6 && m<20)){
            cerr<<"Erreur : n et m hors limites [6..19]\n";
            return 1;
        }

        mat.assign(n*m,0);
        weights.assign(m,1);

        for(int e=0;e<m;e++){
            int u,v,w;
            if(!(fin>>u>>v>>w)){ cerr<<"Format fichier invalide\n"; return 1; }

            mat[u*m + e] = (w>0?w:-w);
            mat[v*m + e] = (w>0?-w:w);
            weights[e] = abs(w);
        }

        cout << "\nLecture fichier OK ✔\n\n";
    }
    else {
        // ========== SAISIE MANUELLE ==========
        cout<<"Nombre de sommets (6..19): "; cin>>n;
        cout<<"Nombre d'arêtes (6..19): "; cin>>m;
        if(!(n>=6 && n<20 && m>=6 && m<20)) { cerr<<"n et m invalides\n"; return 1; }

        cout<<"Sommet de départ: "; cin>>s;
        cout<<"Sommet d'arrivée: "; cin>>t;

        mat.assign(n*m,0);
        weights.assign(m,1);

        cout<<"\nEntrer "<<m<<" arêtes sous forme : u v w\n\n";
        for(int e=0;e<m;e++){
            int u,v,w;
            cout<<"Edge "<<e<<" : ";
            cin>>u>>v>>w;

            mat[u*m+e] = (w>0?w:-w);
            mat[v*m+e] = (w>0?-w:w);
            weights[e] = abs(w);
        }
    }

    //===========================
    //🔹 Connexion au SERVEUR
    //===========================
    cout<<"\nProtocole à utiliser ? 1=TCP  2=UDP : ";
    int proto; cin>>proto;

    string server_ip; int port;
    cout<<"IP serveur : "; cin>>server_ip;
    cout<<"Port : "; cin>>port;


    //============================================ TCP MODE ===========================================
    if(proto==1){
        int sock=socket(AF_INET,SOCK_STREAM,0);
        if(sock<0){ perror("socket"); return 1; }

        sockaddr_in srv{};
        srv.sin_family=AF_INET;
        srv.sin_port=htons(port);
        inet_pton(AF_INET,server_ip.c_str(),&srv.sin_addr);

        if(connect(sock,(sockaddr*)&srv,sizeof(srv))<0){ perror("connect"); return 1; }

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
        } else cout<<"Erreur serveur : "<<buf<<"\n";

        close(sock);
    }

    return 0;
}
