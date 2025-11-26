#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>

constexpr int MAX_VERTICES = 200;
constexpr int MAX_EDGES = 400;

// Structures de réponse (simple)
struct GraphResponse {
    int path_length;            // -1 si aucun chemin
    int path_size;
    int path[MAX_VERTICES];
    int error_code;             // 0 ok, 1 erreur
    char message[128];
};

// Helper pour formater
inline std::string make_client_header(const std::string &client_id, const std::string &type, const std::string &payload) {
    return client_id + " " + type + " " + payload;
}

// Split utility (space)
inline std::vector<std::string> split_ws(const std::string &s) {
    std::vector<std::string> out;
    std::string tmp;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!tmp.empty()) { out.push_back(tmp); tmp.clear(); }
        } else tmp.push_back(c);
    }
    if (!tmp.empty()) out.push_back(tmp);
    return out;
}

#endif // PROTOCOLS_H
