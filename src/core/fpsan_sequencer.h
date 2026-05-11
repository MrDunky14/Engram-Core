#pragma once
// FP-SAN Phase 7: Temporal Sequencer & Working Memory
// Implements the JARVIS procedural loop via Persistent Goal Voltage.
// Phase 1 migration: all ClusterNode field accesses use atomic API.

#include "cluster_graph.h"
#include <shared_mutex>
#include <iostream>

struct Sequencer {
    ClusterGraph* graph;

    void init(ClusterGraph* g) { graph = g; }

    void set_goal(int cluster_id) {
        const int nc = graph->node_count.load(std::memory_order_acquire);
        if (cluster_id >= 0 && cluster_id < nc) {
            graph->node(cluster_id).clamped.store(true,  std::memory_order_release);
            graph->node(cluster_id).activation.store(1.0f, std::memory_order_release);
            std::cout << "[Working Memory] Goal Clamped: Node " << cluster_id << std::endl;
        }
    }

    void clear_goal(int cluster_id) {
        const int nc = graph->node_count.load(std::memory_order_acquire);
        if (cluster_id >= 0 && cluster_id < nc) {
            graph->node(cluster_id).clamped.store(false, std::memory_order_release);
        }
    }

    void tick(int sensory_spike_id, float spike_voltage = 1.5f) {
        std::shared_lock<std::shared_mutex> lk(graph->graph_rw_lock);

        if (sensory_spike_id >= 0)
            graph->spread_activation(sensory_spike_id, spike_voltage);

        const int nc = graph->node_count.load(std::memory_order_acquire);
        for (int i = 0; i < nc; i++) {
            if (graph->node(i).clamped.load(std::memory_order_acquire))
                graph->spread_activation(i, 1.0f);
        }
        graph->tick(0.9f);
    }

    int select_action(int* action_candidates, int num_candidates) {
        int best_action = -1;
        float best_voltage = 0.0f;
        for (int i = 0; i < num_candidates; i++) {
            int cand = action_candidates[i];
            float act = graph->node(cand).activation.load(std::memory_order_acquire);
            if (act > best_voltage) {
                best_voltage = act;
                best_action  = cand;
            }
        }
        return (best_voltage > 0.5f) ? best_action : -1;
    }
};
