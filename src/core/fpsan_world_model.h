#pragma once
// ============================================================
// Research program R3 — Graph-walked one-step transition prior
// Predicts next cluster from EDGE_CAUSES / EDGE_TEMPORAL only (no transformers).
// ============================================================

#include "cluster_graph.h"

#include <cmath>

namespace fpsan {

struct WorldModelStep {
    int predicted_next = -1;
    float confidence = 0.f;
};

/// Greedy one-step: strongest typed edge from `state_id` among CAUSES then TEMPORAL.
inline WorldModelStep world_model_predict_next(ClusterGraph* graph, int state_id) noexcept {
    WorldModelStep out{};
    if (!graph || state_id < 0) return out;
    const int nc = graph->node_count.load(std::memory_order_acquire);
    if (state_id >= nc) return out;

    auto best_of = [&](EdgeType et) {
        int best = -1;
        float bw = 0.f;
        const ClusterNode& nd = graph->node(state_id);
        int ec = nd.edge_count.load(std::memory_order_acquire);
        for (int e = 0; e < ec; ++e) {
            if (nd.edges[e].type != et) continue;
            if (nd.edges[e].weight > bw) {
                bw = nd.edges[e].weight;
                best = nd.edges[e].target;
            }
        }
        return std::make_pair(best, bw);
    };

    auto ca = best_of(EDGE_CAUSES);
    if (ca.first >= 0) {
        out.predicted_next = ca.first;
        out.confidence = std::min(1.0f, ca.second / 3.0f);
        return out;
    }
    auto te = best_of(EDGE_TEMPORAL);
    if (te.first >= 0) {
        out.predicted_next = te.first;
        out.confidence = std::min(1.0f, te.second / 3.0f);
    }
    return out;
}

/// Scalar error in [0,1]: 0 if match, ~1 if mismatch or unknown.
inline float world_model_transition_error(ClusterGraph* graph,
                                          int from_id, int actual_next_id) noexcept {
    auto pr = world_model_predict_next(graph, from_id);
    if (pr.predicted_next < 0) return 1.0f;
    if (pr.predicted_next == actual_next_id) return 1.0f - pr.confidence; // residual uncertainty
    return 1.0f;
}

} // namespace fpsan
