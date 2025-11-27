// SERVER TCP + UDP
// Compile: g++ server.cpp -o server -std=c++17 -pthread

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"
using namespace std;

// ---- CONDITIONS ----
bool valid_nm(int n, int m){
    return (n >=6 && n <20 && m >=6 && m <20);
}

// ---- SPLIT ----
vector<string> split_ws(string s){
    string t; vector<string> out;
    for(char c: s){
        if(isspace(c)){ if(!t.empty()){ out.push_back(t); t=""; } }
        else t+=c;
    }
    if(!t.empty()) out.push_back(t);
    return out;
}

// ---- DIJKSTRA ----
long long INF=1e18;
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

// ---- BUILD GRAPH ----
vector<vector<pair<int,int>>> build_adj(int n,int m,vector<int>&mat,vector<int>&W){
    vector<vector<pair<int,int>>> adj(n);
    for(int e=0;e<m;e++){
        int a=-1,b=-1;
        for(int v=0; v<n; v++){
            if(mat[v*m+e]!=0){
                if(a==-1) a=v;
                else if(b==-1) b=v;
            }
        }
        if(a!=-1 && b!=-1){
            int w=abs(W[e]);
            adj[a].push_back({b,w});
            adj[b].push_back({a,w});
        }
    }
    return adj;
}

// --------------------------------------------------------
// --------------------- TCP CLIENT HANDLER ---------------
// --------------------------------------------------------
void handle_tcp(int client){
    cout<<"[TCP] Client connecté.\n";

    GraphRequest req;
    if(recv(client,&req,sizeof(req),MSG_WAITALL)!=sizeof(req)){ close(client);return;}

    GraphResponse resp{}; resp.error_code=1; resp.path_length=-1;

    if(!valid_nm(req.vertices,req.edges)){
        strcpy(resp.message,"n et m doivent etre >=6 et <20");
        send(client,&resp,sizeof(resp),0);
        close(client);return;
    }
    int n=req.vertices,m=req.edges;
    vector<int>mat(n*m),W(m);

    if(recv(client,mat.data(),mat.size()*sizeof(int),MSG_WAITALL)!=
       (ssize_t)mat.size()*sizeof(int)){close(client);return;}

    if(recv(client,W.data(),W.size()*sizeof(int),MSG_WAITALL)!=
       (ssize_t)W.size()*sizeof(int)){close(client);return;}

    auto adj=build_adj(n,m,mat,W);
    auto R=dijkstra(n,adj,req.start_node,req.end_node);

    if(!R.ok){
        strcpy(resp.message,"Aucun chemin");
        resp.path_size=0; resp.path_length=-1;
    }else{
        resp.error_code=0;
        resp.path_length=(int)R.dist;
        resp.path_size=R.path.size();
        for(int i=0;i<R.path.size();i++) resp.path[i]=R.path[i];
        strcpy(resp.message,"OK");
    }
    send(client,&resp,sizeof(resp),0);
    close(client);
}


// --------------------------------------------------------
// --------------------- UDP SYSTEM -----------------------
// --------------------------------------------------------

struct Udbuf{vector<string>msg; sockaddr_in addr; bool fin=false;};
unordered_map<string,Udbuf> U;
mutex M;

void udp_process(string cid,int udp){
    Udbuf buf;
    {
        lock_guard<mutex>lk(M);
        buf=U[cid]; U.erase(cid);
    }

    // HEADER
    string H="";
    for(string&s:buf.msg){
        auto p=split_ws(s);
        if(p.size()>=2 && p[1]=="HEADER"){H=s;break;}
    }
    if(H==""){
        string o="SERVER_RESPONSE ERROR Missing HEADER";
        sendto(udp,o.c_str(),o.size(),0,(sockaddr*)&buf.addr,sizeof(buf.addr));
        return;
    }
    auto hp=split_ws(H);
    if(hp.size()<6){
        string o="SERVER_RESPONSE ERROR Bad HEAD";
        sendto(udp,o.c_str(),o.size(),0,(sockaddr*)&buf.addr,sizeof(buf.addr));
        return;
    }

    int n=stoi(hp[2]),m=stoi(hp[3]),S=stoi(hp[4]),T=stoi(hp[5]);

    if(!valid_nm(n,m)){
        string o="SERVER_RESPONSE ERROR n,m hors limites";
        sendto(udp,o.c_str(),o.size(),0,(sockaddr*)&buf.addr,sizeof(buf.addr));
        return;
    }

    vector<vector<int>>inc(n,vector<int>(m));
    int row=0;
    for(string&s:buf.msg){
        auto p=split_ws(s);
        if(p.size()>=2 && p[1]=="ROW"){
            if(p.size()!=m+2){
                string o="SERVER_RESPONSE ERROR Ligne invalide";
                sendto(udp,o.c_str(),o.size(),0,(sockaddr*)&buf.addr,sizeof(buf.addr));
                return;
            }
            for(int i=0;i<m;i++) inc[row][i]=stoi(p[i+2]);
            row++;
        }
    }

    vector<int>W(m);
    for(string&s:buf.msg){
        auto p=split_ws(s);
        if(p.size()>=3 && p[1]=="WEIGHTS"){
            for(int i=2;i<p.size();i++) W[i-2]=stoi(p[i]);
        }
    }

    vector<int>flat(n*m);
    for(int i=0;i<n;i++)for(int j=0;j<m;j++)flat[i*m+j]=inc[i][j];
    auto adj=build_adj(n,m,flat,W);
    auto R=dijkstra(n,adj,S,T);

    if(!R.ok){
        string o="SERVER_RESPONSE ERROR Aucun chemin";
        sendto(udp,o.c_str(),o.size(),0,(sockaddr*)&buf.addr,sizeof(buf.addr));
        return;
    }

    // FORMAT POUR LE CLIENT UDP
    string o="SERVER_RESPONSE OK "+to_string(R.dist)+" "+to_string(R.path.size());
    for(int v:R.path) o+=" "+to_string(v);
    sendto(udp,o.c_str(),o.size(),0,(sockaddr*)&buf.addr,sizeof(buf.addr));
}

int main(int argc,char**argv){
    if(argc!=2){ cout<<"Usage: ./server <port>\n"; return 0;}
    int PORT=atoi(argv[1]);

    // TCP
    int tcp=socket(AF_INET,SOCK_STREAM,0);
    int udp=socket(AF_INET,SOCK_DGRAM,0);

    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(PORT); a.sin_addr.s_addr=INADDR_ANY;
    bind(tcp,(sockaddr*)&a,sizeof(a));
    listen(tcp,10);

    bind(udp,(sockaddr*)&a,sizeof(a));

    cout<<"SERVER active sur port "<<PORT<<" (TCP+UDP)\n";

    thread([&]{
        while(true){
            sockaddr_in c; socklen_t L=sizeof(c);
            int cl=accept(tcp,(sockaddr*)&c,&L);
            thread(handle_tcp,cl).detach();
        }
    }).detach();

    // ---- UDP loop ----
    while(true){
        char buf[2048];
        sockaddr_in c; socklen_t L=sizeof(c);
        int r=recvfrom(udp,buf,2047,0,(sockaddr*)&c,&L);
        if(r<=0)continue;
        buf[r]='\0';
        cout<<"[UDP] Paquet reçu de "<<inet_ntoa(c.sin_addr)<<":"<<ntohs(c.sin_port)<<"\n";
        auto p=split_ws(buf);
        string cid=p[0];

        lock_guard<mutex>lk(M);
        auto &B=U[cid];
        B.msg.push_back(buf);
        B.addr=c;

        if(p.size()>=2 && p[1]=="FIN"){
            thread(udp_process,cid,udp).detach();
        }
    }
}
