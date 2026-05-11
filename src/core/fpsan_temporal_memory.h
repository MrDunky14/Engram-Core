#pragma once
// ============================================================
// FP-SAN Phase 11 — L1/L2/L3 temporal memory scaffolding
// Fixed-size LRU L1 roster, episodic hubs, directive sealing.
// ============================================================

#include "cluster_graph.h"

#include <algorithm>
#include <cstdint>

namespace fpsan {

class TemporalMemory {
public:
    static constexpr int L1_CAP = 64;

    void promote_to_l1(ClusterGraph* graph, int cluster_id, uint64_t logical_tick) noexcept {
        if (!graph || cluster_id < 0) return;

        graph->node(cluster_id).memory_tier.store(MEMORY_TIER_L1, std::memory_order_release);

        int slot = find_slot_(cluster_id);
        if (slot >= 0) {
            l1_seen_[slot] = logical_tick;
            return;
        }
        if (l1_sz_ < L1_CAP) {
            l1_nodes_[l1_sz_] = cluster_id;
            l1_seen_[l1_sz_] = logical_tick;
            ++l1_sz_;
            return;
        }
        const int idx = evict_lru_(graph);
        l1_nodes_[idx] = cluster_id;
        l1_seen_[idx] = logical_tick;
    }

    /// Episodic binding hub (tier L2).
    [[nodiscard]] int spawn_episode(ClusterGraph* graph, uint64_t turn_id) {
        (void)turn_id;
        if (!graph) return -1;

        const int epis = graph->spawn();
        if (epis < 0) return -1;

        ClusterNode& hub = graph->node(epis);
        hub.is_binding_node.store(true, std::memory_order_release);
        hub.memory_tier.store(MEMORY_TIER_L2, std::memory_order_release);
        hub.activation.store(0.05f, std::memory_order_relaxed);

        for (int i = 0; i < l1_sz_; ++i) {
            const int cid = l1_nodes_[i];
            if (cid < 0) continue;
            hub.add_edge(cid, 0.5f, EDGE_EPISODIC_LINK, PROV_USER);
            graph->node(cid).add_inverse_edge(epis, 0.5f, EDGE_EPISODIC_LINK);
        }
        return epis;
    }

    void seal_directive(ClusterGraph* graph, int cluster_id) {
        if (!graph || cluster_id < 0) return;
        ClusterNode& nd = graph->node(cluster_id);
        nd.memory_tier.store(MEMORY_TIER_L3, std::memory_order_release);
        nd.clamped.store(true, std::memory_order_release);
        nd.activation.store(1.0f, std::memory_order_release);
    }

    [[nodiscard]] int l1_roster_count() const noexcept { return l1_sz_; }

    /// Clears LRU roster entries (demotes L1 cortical slots back to transient).
    void clear_working_slots(ClusterGraph* graph) noexcept;

    [[nodiscard]] int reactivate_similar_episodes(
        ClusterGraph* graph,
        int cue_cluster_id,
        int top_k,
        float min_overlap_frac,
        float spike_amp) noexcept
    {
        if (!graph || cue_cluster_id < 0) return 0;

        auto overlap_frac = [&](int epis_hub) noexcept -> float {
            const ClusterNode& hub = graph->node(epis_hub);
            const int ec = hub.edge_count.load(std::memory_order_acquire);
            if (ec == 0) return 0.f;
            int members = 0;
            int hits = 0;
            for (int i = 0; i < ec; ++i) {
                const Edge& e = hub.edges[i];
                if (e.type != EDGE_EPISODIC_LINK) continue;
                ++members;
                const float act = graph->node(e.target).activation.load(std::memory_order_relaxed);
                if (act > ACTIVATION_CUTOFF) ++hits;
            }
            if (members == 0) return 0.f;
            return (float)hits / (float)members;
        };

        std::shared_lock<std::shared_mutex> lk(graph->graph_rw_lock);

        const ClusterNode& cue = graph->node(cue_cluster_id);
        const int iec = cue.inverse_edge_count.load(std::memory_order_acquire);

        struct Cand { int epi; float overlap; };
        Cand buf[128];
        int nbuf = 0;

        for (int i = 0; i < iec && nbuf < 128; ++i) {
            const Edge& ie = cue.inverse_edges[i];
            if (ie.type != EDGE_EPISODIC_LINK) continue;
            const int epis = ie.target;
            if (epis < 0 || epis >= graph->node_count.load(std::memory_order_acquire)) continue;
            const float frac = overlap_frac(epis);
            if (frac < min_overlap_frac) continue;
            buf[nbuf++] = Cand{ epis, frac };
        }

        if (nbuf == 0) return 0;

        if (top_k <= 0) top_k = 4;
        const int nk_sel = std::min(nbuf, top_k);
        std::partial_sort(buf, buf + nk_sel, buf + nbuf,
            [](const Cand& a, const Cand& b) { return a.overlap > b.overlap; });

        const int nk = nk_sel;
        int reactivated = 0;
        for (int k = 0; k < nk; ++k) {
            graph->node(buf[k].epi).add_voltage(spike_amp * buf[k].overlap);

            ClusterNode& hub = graph->node(buf[k].epi);
            const int ec = hub.edge_count.load(std::memory_order_acquire);
            for (int ei = 0; ei < ec; ++ei) {
                const Edge& e = hub.edges[ei];
                if (e.type != EDGE_EPISODIC_LINK) continue;
                graph->add_voltage_to(e.target, spike_amp * buf[k].overlap * e.weight / 5.0f);
            }
            ++reactivated;
        }
        return reactivated;
    }

private:
    int find_slot_(int cid) const noexcept {
        for (int i = 0; i < l1_sz_; ++i)
            if (l1_nodes_[i] == cid) return i;
        return -1;
    }

    int evict_lru_(ClusterGraph* graph) noexcept {
        int idx = 0;
        uint64_t oldest = l1_seen_[0];
        for (int i = 1; i < L1_CAP; ++i) {
            if (l1_seen_[i] < oldest) {
                oldest = l1_seen_[i];
                idx = i;
            }
        }
        const int evict_id = l1_nodes_[idx];
        if (graph && evict_id >= 0 &&
            graph->node(evict_id).memory_tier.load(std::memory_order_relaxed) == MEMORY_TIER_L1) {
            graph->node(evict_id).memory_tier.store(MEMORY_TIER_TRANSIENT, std::memory_order_release);
        }
        return idx;
    }

    int      l1_nodes_[L1_CAP]{};
    uint64_t l1_seen_[L1_CAP]{};
    int      l1_sz_{0};
};

inline void TemporalMemory::clear_working_slots(ClusterGraph* graph) noexcept {
    for (int i = 0; i < l1_sz_; ++i) {
        const int cid = l1_nodes_[i];
        if (!graph || cid < 0) continue;
        if (graph->node(cid).memory_tier.load(std::memory_order_relaxed) == MEMORY_TIER_L1)
            graph->node(cid).memory_tier.store(MEMORY_TIER_TRANSIENT, std::memory_order_release);
        l1_nodes_[i] = 0;
        l1_seen_[i] = 0;
    }
    l1_sz_ = 0;
}

} // namespace fpsan
