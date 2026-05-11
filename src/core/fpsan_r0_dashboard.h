#pragma once
// ============================================================
// Research program R0 — Telemetry snapshot for dashboard / baselines
// ============================================================

#include "cluster_graph.h"
#include "fpsan_neuromod.h"

#include <cstdint>
#include <cstdio>

namespace fpsan {

struct R0DashboardSnapshot {
    uint64_t tick_id = 0;
    int      alive_nodes = 0;
    int      total_edges = 0;
    int      contradictions_last_scan = 0;
    uint32_t tier_l1 = 0, tier_l2 = 0, tier_l3 = 0, tier_tr = 0;
    uint32_t edge_hist[EDGE_TYPE_COUNT]{};
    float    neuromod_plasticity = 1.f;
    float    neuromod_arousal = 0.f;
    float    neuromod_last_pe = 0.f;
};

inline void r0_collect_dashboard(ClusterGraph* graph, uint64_t tick,
                                 R0DashboardSnapshot* out) noexcept {
    if (!graph || !out) return;
    out->tick_id = tick;
    out->alive_nodes = 0;
    out->total_edges = 0;
    for (int t = 0; t < EDGE_TYPE_COUNT; ++t) out->edge_hist[t] = 0;

    const int nc = graph->node_count.load(std::memory_order_acquire);
    for (int i = 0; i < nc; ++i) {
        if (!graph->node(i).alive.load(std::memory_order_relaxed)) continue;
        out->alive_nodes++;
        int ec = graph->node(i).edge_count.load(std::memory_order_relaxed);
        out->total_edges += ec;
        for (int e = 0; e < ec; ++e) {
            EdgeType ty = graph->node(i).edges[e].type;
            if (ty < EDGE_TYPE_COUNT) out->edge_hist[(int)ty]++;
        }
        uint8_t tier = graph->node(i).memory_tier.load(std::memory_order_relaxed);
        if (tier == MEMORY_TIER_L1) out->tier_l1++;
        else if (tier == MEMORY_TIER_L2) out->tier_l2++;
        else if (tier == MEMORY_TIER_L3) out->tier_l3++;
        else out->tier_tr++;
    }
    out->neuromod_plasticity = plasticity_scale_load();
    out->neuromod_arousal = arousal_load();
    out->neuromod_last_pe = last_prediction_error_load();
}

inline void r0_print_dashboard_line(const R0DashboardSnapshot& s) noexcept {
    printf("[R0_DASH] tick=%llu alive=%d edges=%d L1=%u L2=%u L3=%u tr=%u "
           "plasticity=%.3f arousal=%.3f pe=%.3f edge0=%u edge_is_a=%u edge_next=%u\n",
           (unsigned long long)s.tick_id, s.alive_nodes, s.total_edges,
           s.tier_l1, s.tier_l2, s.tier_l3, s.tier_tr,
           s.neuromod_plasticity, s.neuromod_arousal, s.neuromod_last_pe,
           s.edge_hist[0], s.edge_hist[1], s.edge_hist[7]);
}

} // namespace fpsan
