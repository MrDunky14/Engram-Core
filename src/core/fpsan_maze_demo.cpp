// FP-SAN Cognitive Maze — Reasoning Brain Demo
// Compile: cl /std:c++17 /O2 /EHsc src\core\fpsan_maze_demo.cpp /Fe:build\maze_demo.exe /I src\core
// Two agents: REACTIVE (1-step lookahead) vs REASONING (graph-based planning)

#include <iostream>
#include <cstring>
#include <chrono>
#include <iomanip>
#include "fpsan_core.h"

using namespace std;

// ============================================================
// THE PHYSICAL WORLD
// ============================================================
const int MAP_W = 10;
const int MAP_H = 10;

char world[MAP_H][MAP_W] = {
    {'#','#','#','#','#','#','#','#','#','#'},
    {'#','.','.','.','#','.','.','.','.','#'},
    {'#','.','#','.','#','.','#','#','.','#'},
    {'#','.','#','.','.','.','#','.','.','#'},
    {'#','.','#','#','#','.','#','.','#','#'},
    {'#','.','.','.','#','.','.','.','.','#'},
    {'#','#','#','.','#','#','#','#','.','#'},
    {'#','.','.','.','.','.','.','#','.','#'},
    {'#','.','#','#','#','#','.','.','.','#'},
    {'#','#','#','#','#','#','#','#','#','#'}
};

// 8-direction sensor: Cardinals + Diagonals → {-1=wall, 1=open}
void get_vision(int x, int y, int8_t* out) {
    memset(out, 0, CORE_INPUT_DIM);
    // Pack into first 8 dimensions
    out[0] = (world[y-1][x] == '#') ? -1 : 1; // Up
    out[1] = (world[y+1][x] == '#') ? -1 : 1; // Down
    out[2] = (world[y][x-1] == '#') ? -1 : 1; // Left
    out[3] = (world[y][x+1] == '#') ? -1 : 1; // Right
    out[4] = (world[y-1][x-1] == '#') ? -1 : 1;
    out[5] = (world[y-1][x+1] == '#') ? -1 : 1;
    out[6] = (world[y+1][x-1] == '#') ? -1 : 1;
    out[7] = (world[y+1][x+1] == '#') ? -1 : 1;
}

// Action deltas: 0=Up, 1=Down, 2=Left, 3=Right
int dx[] = {0, 0, -1, 1};
int dy[] = {-1, 1, 0, 0};
const char* action_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};

void print_world(int ax, int ay, int gx, int gy) {
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            if (x == ax && y == ay) cout << "A ";
            else if (x == gx && y == gy) cout << "G ";
            else cout << world[y][x] << " ";
        }
        cout << endl;
    }
}

// ============================================================
// REACTIVE AGENT (1-step pain avoidance — old behavior)
// ============================================================
struct ReactiveAgent {
    int x, y;
    int collisions;
    int steps;
    unsigned int rng;

    void init(int sx, int sy) { x = sx; y = sy; collisions = 0; steps = 0; rng = 12345; }

    void step() {
        rng = rng * 1103515245 + 12345;
        int action = (rng >> 16) % 4;
        int nx = x + dx[action], ny = y + dy[action];
        steps++;
        if (world[ny][nx] == '#') {
            collisions++;
        } else {
            x = nx; y = ny;
        }
    }
};

// ============================================================
// COGNITIVE AGENT (unified core with reasoning)
// ============================================================
struct CognitiveAgent {
    CognitiveCore brain;
    int x, y;
    int collisions;
    int steps;
    int successful_predictions;
    int total_predictions;

    void init(int sx, int sy) {
        brain.boot(false);
        x = sx; y = sy;
        collisions = 0; steps = 0;
        successful_predictions = 0;
        total_predictions = 0;
    }

    void step() {
        int8_t vision[CORE_INPUT_DIM];
        get_vision(x, y, vision);

        // Cognitive tick: perceive → reason → act
        int action = brain.tick(vision, true);
        steps++;

        // Execute action
        int nx = x + dx[action], ny = y + dy[action];

        if (world[ny][nx] == '#') {
            // PAIN — wall collision
            collisions++;
            brain.reward(-1.0f);
        } else {
            x = nx; y = ny;
            // Small reward for surviving
            brain.reward(0.1f);
        }

        // Check temporal prediction accuracy
        if (brain.predicted_next >= 0) {
            total_predictions++;
            // Get vision at new position and check if prediction matches
            int8_t new_vision[CORE_INPUT_DIM];
            get_vision(x, y, new_vision);
            int actual = brain.perception.perceive(new_vision, false);
            if (actual == brain.predicted_next)
                successful_predictions++;
        }
    }
};

// ============================================================
// MAIN — BENCHMARK: REACTIVE vs COGNITIVE
// ============================================================
int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN COGNITIVE MAZE DEMO" << endl;
    cout << " Reactive Agent (random) vs Cognitive Agent (reasoning brain)" << endl;
    cout << "================================================================\n" << endl;

    const int TOTAL_STEPS = 500;
    const int START_X = 1, START_Y = 1;

    cout << "--- WORLD MAP ---" << endl;
    print_world(START_X, START_Y, -1, -1);
    cout << endl;

    // =========================================================
    // PHASE 1: REACTIVE AGENT
    // =========================================================
    cout << "[Phase 1] Reactive Agent (random walk, no brain)" << endl;
    cout << "---------------------------------------" << endl;

    ReactiveAgent reactive;
    reactive.init(START_X, START_Y);
    for (int i = 0; i < TOTAL_STEPS; i++) reactive.step();

    cout << "  Steps: " << reactive.steps << endl;
    cout << "  Collisions: " << reactive.collisions << endl;
    cout << "  Collision Rate: " << fixed << setprecision(1)
         << (float)reactive.collisions / reactive.steps * 100 << "%" << endl;
    cout << "  Final Position: (" << reactive.x << ", " << reactive.y << ")" << endl;
    cout << endl;

    // =========================================================
    // PHASE 2: COGNITIVE AGENT — TRAINING
    // =========================================================
    cout << "[Phase 2] Cognitive Agent — Training Phase (learning the maze)" << endl;
    cout << "---------------------------------------" << endl;

    auto* cognitive = new CognitiveAgent();
    cognitive->init(START_X, START_Y);

    auto t0 = chrono::high_resolution_clock::now();

    // Training: 3 epochs of exploration
    for (int epoch = 0; epoch < 3; epoch++) {
        cognitive->x = START_X;
        cognitive->y = START_Y;
        cognitive->brain.graph.reset_chain();
        for (int i = 0; i < TOTAL_STEPS; i++) {
            cognitive->step();
        }
        cout << "  Epoch " << epoch + 1 << ": Collisions=" << cognitive->collisions
             << " | Clusters=" << cognitive->brain.perception.active_count()
             << " | Graph Edges=" << cognitive->brain.graph.total_edges()
             << " | Exploration=" << fixed << setprecision(3)
             << cognitive->brain.agency.exploration_rate << endl;
    }

    auto t1 = chrono::high_resolution_clock::now();
    double train_ms = chrono::duration_cast<chrono::microseconds>(t1 - t0).count() / 1000.0;

    cout << "  Training time: " << fixed << setprecision(1) << train_ms << " ms" << endl;
    cout << endl;

    // =========================================================
    // PHASE 3: COGNITIVE AGENT — TESTING (exploitation)
    // =========================================================
    cout << "[Phase 3] Cognitive Agent — Test Phase (exploiting learned graph)" << endl;
    cout << "---------------------------------------" << endl;

    // Reset position and collision counter, keep learned brain
    cognitive->x = START_X;
    cognitive->y = START_Y;
    int test_collisions = 0;
    cognitive->brain.agency.exploration_rate = 0.05f; // Low exploration
    cognitive->brain.graph.reset_chain();

    for (int i = 0; i < TOTAL_STEPS; i++) {
        int prev_col = cognitive->collisions;
        cognitive->step();
        if (cognitive->collisions > prev_col) test_collisions++;
    }

    cout << "  Steps: " << TOTAL_STEPS << endl;
    cout << "  Test Collisions: " << test_collisions << endl;
    cout << "  Test Collision Rate: " << fixed << setprecision(1)
         << (float)test_collisions / TOTAL_STEPS * 100 << "%" << endl;
    cout << "  Final Position: (" << cognitive->x << ", " << cognitive->y << ")" << endl;

    // Prediction accuracy
    if (cognitive->total_predictions > 0) {
        cout << "  Temporal Predictions: " << cognitive->successful_predictions
             << "/" << cognitive->total_predictions << " ("
             << fixed << setprecision(1)
             << (float)cognitive->successful_predictions / cognitive->total_predictions * 100
             << "% accurate)" << endl;
    }
    cout << endl;

    // =========================================================
    // PHASE 4: REASONING DEMO — ASSOCIATION SPREADING
    // =========================================================
    cout << "[Phase 4] Reasoning Demo — Associative Memory" << endl;
    cout << "---------------------------------------" << endl;

    // Pick a cluster and see what the brain associates with it
    int test_cluster = cognitive->brain.current_cluster;
    if (test_cluster >= 0) {
        cognitive->brain.graph.clear_activation();
        int activated = cognitive->brain.graph.spread_activation(test_cluster);
        cout << "  Stimulus: Cluster " << test_cluster << endl;
        cout << "  Activated nodes: " << activated << endl;

        int top_ids[5]; float top_vals[5];
        memset(top_ids, 0, sizeof(top_ids));
        memset(top_vals, 0, sizeof(top_vals));
        int found = cognitive->brain.graph.get_top_activated(top_ids, top_vals, 5);

        cout << "  Top associations:" << endl;
        for (int i = 0; i < found; i++) {
            cout << "    -> Cluster " << top_ids[i] << " (activation: "
                 << fixed << setprecision(3) << top_vals[i] << ")" << endl;
        }

        // Chain prediction
        int chain[5];
        int chain_len = cognitive->brain.graph.predict_chain(test_cluster, chain, 5);
        if (chain_len > 0) {
            cout << "  Predicted chain: " << test_cluster;
            for (int i = 0; i < chain_len; i++) cout << " -> " << chain[i];
            cout << endl;
        }
    }
    cout << endl;

    // =========================================================
    // FINAL SCORECARD
    // =========================================================
    cout << "================================================================" << endl;
    cout << " SCORECARD: REACTIVE vs COGNITIVE" << endl;
    cout << "================================================================" << endl;
    cout << "| Metric              | Reactive  | Cognitive | Winner     |" << endl;
    cout << "|---------------------|-----------|-----------|------------|" << endl;

    float react_rate = (float)reactive.collisions / reactive.steps * 100;
    float cog_rate = (float)test_collisions / TOTAL_STEPS * 100;

    cout << "| Collision Rate      | " << setw(7) << fixed << setprecision(1) << react_rate
         << "%  | " << setw(7) << cog_rate << "%  | "
         << (cog_rate < react_rate ? "COGNITIVE " : "REACTIVE  ") << " |" << endl;
    cout << "| Has Memory          |    NO     |   YES     | COGNITIVE  |" << endl;
    cout << "| Temporal Prediction |    NO     |   YES     | COGNITIVE  |" << endl;
    cout << "| Multi-step Credit   |    NO     |   YES     | COGNITIVE  |" << endl;
    cout << "| Graph Reasoning     |    NO     |   YES     | COGNITIVE  |" << endl;
    cout << "================================================================" << endl;

    cognitive->brain.print_stats();

    delete cognitive;
    return 0;
}
