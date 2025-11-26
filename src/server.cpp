#include <iostream>
#include <vector>
#include <queue>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

using namespace std;

// ===================== DIJKSTRA ===================== //
GraphResponse computeShortestPath(int V, int E, 
    vector<int> incidence, vector<int> weights,
    int start, int end)
{
    GraphResponse res{};
    res.path_size = 0;

    vector<vector<pair<int,int>>> adj(V); // pair <voisin, poids>

    for(int e=0; e<E; e++){
        int a=-1, b=-1;
        for(int v=0; v<V; v++){
            if(incidence[v*E + e] == 1){
                if(a==-1) a=v;
                else b=v;
            }
        }
        if(a!=-1 && b!=-1){
            adj[a].push_back({b,weights[e]});
            adj[b].push_back({a,weights[e]});
        }
    }

    vector<int> dist(V,1e9), parent(V,-1);
    dist[start]=0;

    priority_queue<pair<int,int>,vector<pair<int,int>>,greater<>> pq;
    pq.push({0,start});

    while(!pq.empty()){
        auto [d,u]=pq.top(); pq.pop();
        if(d!=dist[u]) continue;
        if(u==end) break;

        for(auto [v,w]: adj[u]){
            if(dist[u]+w < dist[v]){
                dist[v]=dist[u]+w;
                parent[v]=u;
                pq.push({dist[v],v});
            }
        }
    }

    if(dist[end]==1e9){
        strcpy(res.message,"Aucun chemin trouvé");
        res.error_code=1;
        return res;
    }

    res.path_length = dist[end];

    int cur=end;
    while(cur!=-1){
        res.path[res.path_size++] = cur;
        cur = parent[cur];
    }
    reverse(res.path,res.path+res.path_size);

    strcpy(res.message,"Chemin trouvé avec succès");
    return res;
}

// ===================== MAIN SERVER ===================== //
int main(int argc,char* argv[]){
    if(argc!=2){
        cout << "Usage : ./server <port>\nExemple : ./server 5050\n";
        return 1;
    }
    int PORT = atoi(argv[1]);

    int server_fd = socket(AF_INET,SOCK_STREAM,0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd,(sockaddr*)&addr,sizeof(addr));
    listen(server_fd,5);

    cout<<"\n===== SERVEUR DEMARRE sur port "<<PORT<<" =====\n";

    while(1){
        int client = accept(server_fd,nullptr,nullptr);
        cout<<"\nClient connecté\n";

        GraphRequest req;
        recv(client,&req,sizeof(req),0);

        vector<int> matrix(req.vertices*req.edges);
        vector<int> weights(req.edges);

        recv(client,matrix.data(),matrix.size()*sizeof(int),0);
        recv(client,weights.data(),weights.size()*sizeof(int),0);

        GraphResponse result = computeShortestPath(
            req.vertices,req.edges,matrix,weights,req.start_node,req.end_node
        );

        send(client,&result,sizeof(result),0);
        close(client);
    }
}
