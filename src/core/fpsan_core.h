#pragma once
// FP-SAN Unified Cognitive Core
// One brain. One loop. Sense → Perceive → Remember → Predict → Reason → Act → Learn.

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include "cluster_graph.h"
#include "fpsan_language.h"

// ============================================================
// PERCEPTION MODULE (FP-SAN v17 — crystallization + soft Hebbian)
// ============================================================
const int CORE_INPUT_DIM = 784;
const int CORE_CLUSTER_DIM = 200;
const float CORE_BASE_VIGILANCE = 0.03f;
const int CORE_FREEZE_THRESHOLD = 25;

struct PerceptionModule {
    struct Cluster {
        int8_t weights[CORE_INPUT_DIM];
        float accum[CORE_INPUT_DIM];
        int sample_count;
        bool frozen;
        uint64_t last_spike;
    };

    Cluster cortex[CORE_CLUSTER_DIM];
    float expected_energy;

    void init() {
        expected_energy = CORE_INPUT_DIM * 0.3f;
        for (int c = 0; c < CORE_CLUSTER_DIM; c++) {
            memset(cortex[c].weights, 0, CORE_INPUT_DIM);
            memset(cortex[c].accum, 0, sizeof(cortex[c].accum));
            cortex[c].sample_count = 0;
            cortex[c].frozen = false;
            cortex[c].last_spike = 0;
        }
    }

    int active_count() {
        int n = 0;
        for (int c = 0; c < CORE_CLUSTER_DIM; c++) if (cortex[c].last_spike > 0) n++;
        return n;
    }

    // Returns winning cluster ID
    int perceive(const int8_t* input, bool learn) {
        int best = -1, first_empty = -1;
        float best_sim = -9999.0f;

        int active = active_count();
        float cap = (float)active / CORE_CLUSTER_DIM;
        float vigilance = CORE_BASE_VIGILANCE + cap * 0.4f;

        for (int c = 0; c < CORE_CLUSTER_DIM; c++) {
            if (cortex[c].last_spike == 0) {
                if (first_empty == -1) first_empty = c;
                continue;
            }
            int score = 0, act = 0;
            for (int i = 0; i < CORE_INPUT_DIM; i++) {
                if (input[i] != 0 || cortex[c].weights[i] != 0) {
                    if (cortex[c].weights[i] == input[i]) score++; else score--;
                    act++;
                }
            }
            if (act > 0) {
                float sim = (float)score / act;
                if (sim > best_sim) { best_sim = sim; best = c; }
            }
        }

        if (best_sim < vigilance && first_empty != -1 && learn) best = first_empty;
        else if (best_sim < vigilance && !learn) return -1;

        if (learn && first_empty == -1 && best_sim < 0.3f) return best;

        if (best != -1 && learn && !cortex[best].frozen) {
            cortex[best].sample_count++;
            float lr = std::max(0.05f, 1.0f / (float)cortex[best].sample_count);
            for (int i = 0; i < CORE_INPUT_DIM; i++) {
                cortex[best].accum[i] += lr * ((float)input[i] - cortex[best].accum[i]);
                if (cortex[best].accum[i] > 0.30f) cortex[best].weights[i] = 1;
                else if (cortex[best].accum[i] < -0.30f) cortex[best].weights[i] = -1;
                else cortex[best].weights[i] = 0;
            }
            cortex[best].last_spike = 1;
            if (cortex[best].sample_count >= CORE_FREEZE_THRESHOLD)
                cortex[best].frozen = true;
        }
        return best;
    }
};

// ============================================================
// AGENCY MODULE (dopamine RL with eligibility traces)
// ============================================================
const int NUM_ACTIONS = 4; // Up, Down, Left, Right

struct AgencyModule {
    // Action weights per cluster (learned via dopamine)
    float action_weights[CORE_CLUSTER_DIM][NUM_ACTIONS];
    float exploration_rate;

    void init() {
        memset(action_weights, 0, sizeof(action_weights));
        exploration_rate = 0.3f;
    }

    // Select action for a given cluster state
    int select_action(int cluster_id, unsigned int* rng_state) {
        if (cluster_id < 0) return *rng_state % NUM_ACTIONS;

        // Epsilon-greedy with linear RNG
        *rng_state = *rng_state * 1103515245 + 12345;
        float roll = (float)((*rng_state >> 16) % 1000) / 1000.0f;

        if (roll < exploration_rate) {
            *rng_state = *rng_state * 1103515245 + 12345;
            return (*rng_state >> 16) % NUM_ACTIONS;
        }

        // Exploit: pick best action
        int best = 0;
        float best_w = action_weights[cluster_id][0];
        for (int a = 1; a < NUM_ACTIONS; a++) {
            if (action_weights[cluster_id][a] > best_w) {
                best_w = action_weights[cluster_id][a];
                best = a;
            }
        }
        return best;
    }

    // Apply dopamine reward to cluster-action pair
    void learn(int cluster_id, int action, float dopamine) {
        if (cluster_id >= 0)
            action_weights[cluster_id][action] += dopamine;
    }

    void decay_exploration(float factor = 0.995f) {
        exploration_rate *= factor;
        if (exploration_rate < 0.05f) exploration_rate = 0.05f;
    }
};

// ============================================================
// COGNITIVE CORE — The unified brain
// ============================================================
struct CognitiveCore {
    PerceptionModule perception;
    LanguageCortex language;
    AgencyModule agency;
    ClusterGraph graph;

    int current_cluster;    // What we perceive right now
    int predicted_next;     // What we expect to happen next
    int last_action;        // What we did
    uint64_t tick_count;
    unsigned int rng_state;

    bool verbose;

    void boot(bool v = false) {
        verbose = v;
        perception.init();
        language.init();
        agency.init();
        graph.init();
        current_cluster = -1;
        predicted_next = -1;
        last_action = -1;
        tick_count = 0;
        rng_state = 42;
        if (verbose) std::cout << "[Core] Cognitive Core booted." << std::endl;
    }

    // One complete cognitive cycle
    // Returns: action taken
    int tick(const int8_t* sensor_input, bool learn = true) {
        tick_count++;

        // 1. PERCEIVE — cluster the input
        current_cluster = perception.perceive(sensor_input, learn);

        // 2. REMEMBER — record temporal bond in graph
        if (current_cluster >= 0) {
            graph.record_fire(current_cluster);
        }

        // 3. PREDICT — what comes next based on temporal bonds
        predicted_next = -1;
        if (current_cluster >= 0 &&
            graph.node(current_cluster).edge_count.load(std::memory_order_acquire) > 0) {
            predicted_next = graph.node(current_cluster).edges[0].target;
        }

        // 4. REASON — spread activation to find associations
        graph.clear_activation();
        if (current_cluster >= 0) {
            graph.spread_activation(current_cluster);
        }

        // 5. ACT — select action
        last_action = agency.select_action(current_cluster, &rng_state);

        return last_action;
    }

    // Apply environmental feedback
    void reward(float dopamine) {
        // Direct: reinforce last cluster-action pair
        agency.learn(current_cluster, last_action, dopamine);

        // Multi-step: propagate credit through active chain
        if (dopamine > 0) {
            graph.assign_credit(dopamine);
        }

        agency.decay_exploration();
    }

    void print_stats() {
        std::cout << "[Core] Tick: " << tick_count
                  << " | Clusters: " << perception.active_count()
                  << " | Graph Edges: " << graph.total_edges()
                  << " | Exploration: " << agency.exploration_rate
                  << std::endl;
    }
};
