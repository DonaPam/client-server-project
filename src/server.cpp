#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

using namespace std;

const int PORT = 8080;

int main() {
    cout << "=== Serveur TCP Basique ===" << endl;
    
    // 1. Création du socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        cerr << "Erreur création socket" << endl;
        return 1;
    }
    
    // 2. Configuration de l'adresse
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    // 3. Liaison du socket
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        cerr << "Erreur bind" << endl;
        close(server_fd);
        return 1;
    }
    
    // 4. Mise en écoute
    if (listen(server_fd, 3) < 0) {
        cerr << "Erreur listen" << endl;
        close(server_fd);
        return 1;
    }
    
    cout << "✅ Serveur en écoute sur le port " << PORT << endl;
    
    // 5. Accepter une connexion
    int addrlen = sizeof(address);
    int client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
    if (client_socket < 0) {
        cerr << "Erreur accept" << endl;
        close(server_fd);
        return 1;
    }
    
    cout << "✅ Client connecté!" << endl;
    
    // 6. Recevoir un message simple
    char buffer[1024] = {0};
    int bytes_read = read(client_socket, buffer, 1024);
    cout << "Message reçu: " << buffer << endl;
    
    // 7. Répondre
    const char* response = "Hello from server!";
    send(client_socket, response, strlen(response), 0);
    
    // 8. Fermeture
    close(client_socket);
    close(server_fd);
    
    cout << "Serveur arrêté." << endl;
    return 0;
}
