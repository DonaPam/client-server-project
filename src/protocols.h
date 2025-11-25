#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <vector>

// Fonction d’envoi d’une ligne terminée par '\n'
bool send_line(int sock, const std::string &s);

// Fonction de réception d’une ligne terminée par '\n'
std::string recv_line(int sock);

// Type pour la liste d’adjacence pondérée : (voisin, poids)
using AdjList = std::vector<std::vector<std::pair<int,int>>>;

#endif
