#!/bin/bash
# run_tests.sh - Tests conformes aux exigences 2.11

set -e  # Arrêter en cas d'erreur

echo "========================================"
echo "TESTS CLIENT-SERVEUR - Exigences 2.11"
echo "========================================"

# Configuration
PORT=17000
SERVER_IP="127.0.0.1"
TIMEOUT=8  # 8 secondes max par test
PASS=0
FAIL=0
TOTAL=0

# Créer répertoires
mkdir -p test_data logs

# ========================================
# FONCTIONS UTILITAIRES
# ========================================

# Créer les fichiers de test
create_test_files() {
    echo "Création des fichiers de test..."
    
    # 2.11.3/2.11.4: Test simple
    cat > test_data/simple.txt << 'EOF'
6 6
0 5
0 1 5
0 2 3
1 2 2
1 3 7
2 3 1
3 4 4
4 5 2
EOF
    
    # 2.11.5: n=5 (MIN-1) - DEVRAIT ÉCHOUER
    cat > test_data/n5.txt << 'EOF'
5 6
0 4
0 1 1
0 2 2
1 3 3
2 3 4
1 4 5
3 4 6
EOF
    
    # 2.11.5: n=6 (MIN) - DEVRAIT RÉUSSIR
    cat > test_data/n6.txt << 'EOF'
6 6
0 5
0 1 1
0 2 2
1 2 3
1 3 4
2 4 5
3 5 6
EOF
    
    # 2.11.5: n=19 (MAX si limite=20) - DEVRAIT RÉUSSIR
    cat > test_data/n19.txt << 'EOF'
19 10
0 18
0 1 1
1 2 2
2 3 3
3 4 4
4 5 5
5 6 6
6 7 7
7 8 8
8 9 9
9 10 10
10 11 1
11 12 2
12 13 3
13 14 4
14 15 5
15 16 6
16 17 7
17 18 8
EOF
    
    # 2.11.5: n=20 (MAX+1) - DEVRAIT ÉCHOUER
    cat > test_data/n20.txt << 'EOF'
20 6
0 19
0 1 1
0 2 2
1 3 3
2 3 4
1 4 5
3 4 6
EOF
    
    # 2.11.6: Milieu OДЗ (n=12)
    cat > test_data/middle.txt << 'EOF'
12 10
0 11
0 1 3
1 2 4
2 3 2
3 4 5
4 5 1
5 6 6
6 7 2
7 8 3
8 9 4
9 10 1
10 11 2
EOF
    
    echo "✅ Fichiers de test créés"
}

# Exécuter un test et vérifier le résultat
run_test() {
    local test_name=$1
    local protocol=$2
    local input_method=$3  # "file" ou "keyboard"
    local test_file=$4     # fichier ou "keyboard_input"
    local expected_result=$5  # "success" ou "failure"
    
    ((TOTAL++))
    echo -e "\n🔧 Test $TOTAL: $test_name"
    echo "   Protocole: $protocol, Méthode: $input_method"
    
    local output_file="logs/${test_name}.log"
    
    # Préparer l'entrée
    if [ "$input_method" = "file" ]; then
        # Entrée depuis fichier
        cat > input.tmp << EOF
2
$test_file
EOF
    else
        # Entrée clavier (données directes)
        cat > input.tmp << EOF
1
6
6
0
5
0 1 5
0 2 3
1 2 2
1 3 7
2 3 1
3 4 4
4 5 2
EOF
    fi
    
    # Exécuter avec timeout
    timeout $TIMEOUT ./client $SERVER_IP $protocol $PORT < input.tmp > "$output_file" 2>&1
    local exit_code=$?
    
    # Analyser résultat
    local result=""
    if [ $exit_code -eq 124 ]; then
        result="timeout"
    elif grep -q "RESULT\|Path length:" "$output_file"; then
        result="success"
    elif grep -q "ERROR\|error\|invalid\|Invalid" "$output_file"; then
        result="failure"
    else
        result="unknown"
    fi
    
    # Vérifier si conforme aux attentes
    if [ "$result" = "$expected_result" ] || [ "$expected_result" = "any" ]; then
        echo "   ✅ SUCCÈS: Comportement attendu ($result)"
        ((PASS++))
        return 0
    else
        echo "   ❌ ÉCHEC: Attendu $expected_result, obtenu $result"
        echo "   Sortie (dernières lignes):"
        tail -5 "$output_file" | sed 's/^/      /'
        ((FAIL++))
        return 1
    fi
}

# Tester UDP avec serveur indisponible
test_udp_no_server() {
    ((TOTAL++))
    echo -e "\n🔧 Test $TOTAL: UDP avec serveur indisponible (2.11.2)"
    
    # Arrêter serveur si running
    kill $SERVER_PID 2>/dev/null || true
    sleep 1
    
    # Exécuter client - devrait timeout ou échouer
    timeout 10 ./client $SERVER_IP UDP $PORT << 'EOF' > logs/udp_no_server.log 2>&1
1
6
6
0
5
0 1 1
0 2 2
1 2 3
1 3 4
2 4 5
3 5 6
EOF
    
    local exit_code=$?
    
    # Le test réussit si le client détecte la perte de connexion
    if [ $exit_code -eq 124 ] || grep -q "Connection lost\|timeout\|Perte" logs/udp_no_server.log; then
        echo "   ✅ SUCCÈS: Client détecte serveur indisponible"
        ((PASS++))
    else
        echo "   ❌ ÉCHEC: Client ne détecte pas serveur indisponible"
        ((FAIL++))
    fi
    
    # Redémarrer serveur pour tests suivants
    start_server
}

# Démarrer serveur
start_server() {
    echo "Démarrage serveur sur port $PORT..."
    ./server $PORT > logs/server.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "❌ Impossible de démarrer le serveur"
        cat logs/server.log
        exit 1
    fi
    echo "✅ Serveur démarré (PID: $SERVER_PID)"
}

# Arrêter serveur
stop_server() {
    echo "Arrêt du serveur..."
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
}

# ========================================
# EXÉCUTION DES TESTS
# ========================================

# Vérifier que les exécutables existent
if [ ! -f "./client" ] || [ ! -f "./server" ]; then
    echo "❌ Erreur: Compilez d'abord avec 'make' ou 'g++'"
    exit 1
fi

# Créer fichiers de test
create_test_files

# Démarrer serveur
start_server

# ========================================
# 2.11.1: Tests TCP
# ========================================
echo -e "\n📋 2.11.1: Tests protocole TCP"

run_test "tcp_file" "TCP" "file" "test_data/simple.txt" "success"
run_test "tcp_keyboard" "TCP" "keyboard" "" "success"

# ========================================
# 2.11.2: Tests UDP
# ========================================
echo -e "\n📋 2.11.2: Tests protocole UDP"

run_test "udp_file" "UDP" "file" "test_data/simple.txt" "success"
run_test "udp_keyboard" "UDP" "keyboard" "" "success"

# Test UDP avec serveur indisponible
test_udp_no_server

# ========================================
# 2.11.3/2.11.4: déjà testés ci-dessus
# ========================================
echo -e "\n📋 2.11.3 & 2.11.4: Entrée clavier/fichier (déjà testé)"

# ========================================
# 2.11.5: Tests limites OДЗ
# ========================================
echo -e "\n📋 2.11.5: Tests limites OДЗ"

run_test "odz_n5" "TCP" "file" "test_data/n5.txt" "failure"
run_test "odz_n6" "TCP" "file" "test_data/n6.txt" "success"
run_test "odz_n19" "TCP" "file" "test_data/n19.txt" "success"
run_test "odz_n20" "TCP" "file" "test_data/n20.txt" "failure"

# ========================================
# 2.11.6: Test milieu OДЗ
# ========================================
echo -e "\n📋 2.11.6: Test milieu OДЗ"

run_test "odz_middle" "TCP" "file" "test_data/middle.txt" "success"

# ========================================
# TESTS SUPPLÉMENTAIRES (bonus)
# ========================================
echo -e "\n📋 Tests supplémentaires"

# Test avec plusieurs clients (simultanés)
echo -e "\n🔧 Test: 3 clients simultanés"
for i in 1 2 3; do
    (timeout 10 ./client $SERVER_IP TCP $PORT << EOF
2
test_data/simple.txt
EOF
    ) > logs/concurrent_$i.log 2>&1 &
done

# Attendre que tous terminent
wait

# Vérifier résultats
concurrent_success=0
for i in 1 2 3; do
    if grep -q "RESULT\|Path length:" logs/concurrent_$i.log; then
        ((concurrent_success++))
    fi
done

if [ $concurrent_success -eq 3 ]; then
    echo "   ✅ 3 clients simultanés: tous réussis"
else
    echo "   ⚠️  3 clients simultanés: $concurrent_success/3 réussis"
fi

# ========================================
# NETTOYAGE ET RAPPORT
# ========================================

stop_server
rm -f input.tmp

echo -e "\n========================================"
echo "RAPPORT FINAL"
echo "========================================"
echo "Tests exécutés: $TOTAL"
echo "Tests réussis:  $PASS"
echo "Tests échoués:  $FAIL"

if [ $FAIL -eq 0 ]; then
    echo -e "\n✅ TOUTES LES EXIGENCES 2.11 SATISFAITES !"
    exit 0
else
    echo -e "\n❌ Certains tests ont échoué"
    echo "Consultez les logs dans: logs/"
    exit 1
fi
