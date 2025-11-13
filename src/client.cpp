#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

using namespace std;

int main() {
    cout << "=== Client TCP Basique ===" << endl;
    
    // 1. Création du socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        cerr << "Erreur création socket" << endl;
        return 1;
    }
    
    // 2. Configuration serveur
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);
    
    // Convertir l'adresse IP
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        cerr << "Adresse IP invalide" << endl;
        return 1;
    }
    
    // 3. Connexion
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        cerr << "Erreur connexion" << endl;
        return 1;
    }
    
    cout << "✅ Connecté au serveur!" << endl;
    
    // 4. Envoyer un message
    const char* message = "Hello from client!";
    send(sock, message, strlen(message), 0);
    cout << "Message envoyé: " << message << endl;
    
    // 5. Recevoir la réponse
    char buffer[1024] = {0};
    int bytes_read = read(sock, buffer, 1024);
    cout << "Réponse reçue: " << buffer << endl;
    
    // 6. Fermeture
    close(sock);
    
    return 0;
}
