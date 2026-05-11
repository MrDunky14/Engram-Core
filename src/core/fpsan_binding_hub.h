#pragma once
// ============================================================
// Research program R2 — Binding hub for ensemble / distributed concepts
// Hub node links members via EDGE_ENSEMBLE_LINK.
// ============================================================

#include "cluster_graph.h"

namespace fpsan {

inline int create_binding_hub(ClusterGraph* graph) noexcept {
    if (!graph) return -1;
    int h = graph->spawn();
    if (h < 0) return -1;
    graph->node(h).is_binding_node.store(true, std::memory_order_release);
    graph->node(h).memory_tier.store(MEMORY_TIER_L2, std::memory_order_release);
    return h;
}

inline void hub_link_member(ClusterGraph* graph, int hub_id, int member_id, float w = 0.6f) noexcept {
    if (!graph || hub_id < 0 || member_id < 0) return;
    graph->node(hub_id).add_edge(member_id, w, EDGE_ENSEMBLE_LINK, PROV_USER);
    graph->node(member_id).add_inverse_edge(hub_id, w, EDGE_ENSEMBLE_LINK);
}

} // namespace fpsan
