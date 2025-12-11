# verify_path_simple.py

# --- Define your graph ---
# Example graph with 4 vertices, 4 edges
graph = {
    0: [(1,3),(3,10)],
    1: [(2,1)],
    2: [(3,2)],
    3: []
}

start = 0
end = 3

# --- Read client output ---
with open("output_client.txt","r") as f:
    lines = f.readlines()

# Extract path length
for line in lines:
    if "Path length" in line:
        returned_length = int(line.strip().split(":")[1])
    if "Path:" in line:
        path = list(map(int,line.strip().split(":")[1].strip().split("->")))

# --- Check validity of path ---
valid = True
computed_length = 0

for i in range(len(path)-1):
    u = path[i]
    v = path[i+1]
    found = False
    for w, weight in graph.get(u, []):
        if w == v:
            computed_length += weight
            found = True
            break
    if not found:
        valid = False
        print(f"❌ Invalid edge: {u} -> {v}")

# --- Report ---
if valid:
    print("✔ All edges exist in graph")
else:
    print("❌ Path invalid!")

if computed_length == returned_length:
    print(f"✔ Path length correct: {computed_length}")
else:
    print(f"❌ Length mismatch: returned {returned_length}, computed {computed_length}")
