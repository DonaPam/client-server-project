#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>

using namespace std;

const int PORT = 8080;
const int BUFFER_SIZE = 1024;

int main() {
    cout << "=== Serveur TCP pour Graphes ===" << endl;
    cout << "Initialisation..." << endl;
    
    // === ÉTAPE 1 : Création du socket ===
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("❌ socket() failed");
        exit(EXIT_FAILURE);
    }
    cout << "✅ Socket créé (fd: " << server_fd << ")" << endl;
    
    // === ÉTAPE 2 : Configuration de l'adresse ===
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    
    // Initialiser la structure à zéro
    memset(&address, 0, sizeof(address));
    
    // Configurer l'adresse
    address.sin_family = AF_INET;           // IPv4
    address.sin_addr.s_addr = INADDR_ANY;   // Accepter toutes les interfaces
    address.sin_port = htons(PORT);         // Port en ordre réseau
    
    cout << "✅ Adresse configurée: 0.0.0.0:" << PORT << endl;
    
    // === ÉTAPE 3 : Liaison (bind) ===
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("❌ bind() failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    cout << "✅ Socket lié au port " << PORT << endl;
    
    // === ÉTAPE 4 : Mise en écoute ===
    if (listen(server_fd, 3) < 0) {
        perror("❌ listen() failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    cout << "✅ En écoute sur le port " << PORT << endl;
    cout << "En attente de connexions clients..." << endl;
    
    // === ÉTAPE 5 : Boucle principale d'acceptation ===
    while (true) {
        cout << "\n--- En attente d'un client ---" << endl;
        
        // Accepter une nouvelle connexion
        int client_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (client_socket < 0) {
            perror("❌ accept() failed");
            continue;  // Continuer même en cas d'erreur
        }
        
        // Afficher les informations du client
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
        cout << "✅ Client connecté! IP: " << client_ip 
             << ", Port: " << ntohs(address.sin_port) << endl;
        
        // === ÉTAPE 6 : Réception des données ===
        char buffer[BUFFER_SIZE] = {0};
        ssize_t bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
        
        if (bytes_read > 0) {
            cout << "📨 Message reçu (" << bytes_read << " bytes): " << buffer << endl;
            
            // === ÉTAPE 7 : Traitement (simulé pour l'instant) ===
            cout << "⚙️  Traitement du graphe..." << endl;
            
            // Réponse simulée
            const char* response = "SERVER: Chemin minimal = 5 (0-2-4-5)";
            
            // === ÉTAPE 8 : Envoi de la réponse ===
            ssize_t bytes_sent = send(client_socket, response, strlen(response), 0);
            if (bytes_sent > 0) {
                cout << "📤 Réponse envoyée (" << bytes_sent << " bytes)" << endl;
            } else {
                perror("❌ send() failed");
            }
        } else if (bytes_read == 0) {
            cout << "🔌 Client déconnecté" << endl;
        } else {
            perror("❌ read() failed");
        }
        
        // === ÉTAPE 9 : Fermeture de la connexion client ===
        close(client_socket);
        cout << "✅ Connexion client fermée" << endl;
        
        // Pour l'instant, on quitte après un client
        // Plus tard, on gardera la boucle pour plusieurs clients
        break;
    }
    
    // === ÉTAPE 10 : Fermeture du socket serveur ===
    close(server_fd);
    cout << "\n=== Serveur arrêté ===" << endl;
    
    return 0;
}
