#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "protocols.h"

using namespace std;

bool send_line(int sock, const string &s) {
    string msg = s + "\n";
    size_t total = 0;
    while (total < msg.size()) {
        ssize_t sent = send(sock, msg.data() + total, msg.size() - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <IP_serveur> <port>\n";
        return 1;
    }
    const char* host = argv[1];
    const char* port = argv[2];

    int n, m;
    cout << "Entrer n (sommets) et m (arêtes): ";
    cin >> n >> m;

    vector<vector<int>> A(n, vector<int>(m));

    cout << "Entrer la matrice d'incidence pondérée (" << n << " lignes de " << m << " valeurs) :\n";
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j)
            cin >> A[i][j];

    int s, t;
    cout << "Entrer sommet de départ et sommet d'arrivée: ";
    cin >> s >> t;

    //---------------------------
    // Résolution DNS
    //---------------------------
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err) {
        cerr << "getaddrinfo: " << gai_strerror(err) << endl;
        return 1;
    }

    //---------------------------
    // Création du socket
    //---------------------------
    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) { perror("socket"); return 1; }

    //---------------------------
    // Connexion au serveur
    //---------------------------
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }
    freeaddrinfo(res);

    //---------------------------
    // Envoi de n et m
    //---------------------------
    {
        ostringstream oss;
        oss << n << " " << m;
        send_line(sockfd, oss.str());
    }

    //---------------------------
    // Envoi de la matrice
    //---------------------------
    for (int i = 0; i < n; ++i) {
        ostringstream oss;
        for (int j = 0; j < m; ++j) {
            if (j) oss << " ";
            oss << A[i][j];
        }
        send_line(sockfd, oss.str());
    }

    //---------------------------
    // Envoi de s et t
    //---------------------------
    {
        ostringstream oss;
        oss << s << " " << t;
        send_line(sockfd, oss.str());
    }

    //---------------------------
    // Réception du résultat
    //---------------------------
    char buffer[256];
    ssize_t bytes = recv(sockfd, buffer, sizeof(buffer)-1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        cout << ">>> Réponse du serveur : " << buffer;
    } else {
        cerr << "Erreur: pas de réponse du serveur.\n";
    }

    close(sockfd);
    return 0;
}
