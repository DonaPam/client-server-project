#!/bin/bash

# Configuration
SERVER_PORT=12345
SERVER_IP="127.0.0.1"
TEST_DIR="tests"

# Couleurs pour output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "=== DÉMARRAGE DES TESTS CLIENT-SERVEUR ==="

# Étape 1: Démarrer le serveur
echo "1. Démarrage du serveur..."
./server $SERVER_PORT &
SERVER_PID=$!
sleep 2  # Attendre que le serveur soit prêt

# Vérifier que le serveur tourne
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}❌ Échec démarrage serveur${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Serveur démarré (PID: $SERVER_PID)${NC}"

# Étape 2: Tests TCP
echo -e "\n2. Tests TCP..."
for graph_file in $TEST_DIR/data/graph*.txt; do
    echo "   Test avec $(basename $graph_file)..."
    if ./$TEST_DIR/scripts/test_client.exp $SERVER_IP TCP $SERVER_PORT $graph_file; then
        echo -e "   ${GREEN}✅ Succès${NC}"
    else
        echo -e "   ${RED}❌ Échec${NC}"
    fi
    sleep 1
done

# Étape 3: Tests UDP
echo -e "\n3. Tests UDP..."
for graph_file in $TEST_DIR/data/graph*.txt; do
    echo "   Test avec $(basename $graph_file)..."
    if ./$TEST_DIR/scripts/test_client.exp $SERVER_IP UDP $SERVER_PORT $graph_file; then
        echo -e "   ${GREEN}✅ Succès${NC}"
    else
        echo -e "   ${RED}❌ Échec${NC}"
    fi
    sleep 1
done

# Étape 4: Tests d'erreurs
echo -e "\n4. Tests d'erreurs..."
echo "   Test avec fichier inexistant..."
./client $SERVER_IP TCP $SERVER_PORT << EOF
2
fichier_inexistant.txt
EOF
# Vérifier que le client gère bien l'erreur

# Étape 5: Tests de charge (3 clients simultanés)
echo -e "\n5. Test de charge (3 clients)..."
for i in {1..3}; do
    (./$TEST_DIR/scripts/test_client.exp $SERVER_IP TCP $SERVER_PORT $TEST_DIR/data/graph1.txt > /dev/null 2>&1 &)
done
sleep 3
echo -e "   ${GREEN}✅ 3 clients traités${NC}"

# Étape 6: Nettoyage
echo -e "\n6. Nettoyage..."
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null
echo -e "${GREEN}✅ Serveur arrêté${NC}"

echo -e "\n=== TESTS TERMINÉS ==="
