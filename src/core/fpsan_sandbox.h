#pragma once
// ============================================================
// FP-SAN Phase 3: Semantic Planner + Counterfactual Sandbox
//
// SemanticPlanner:
//   Given a goal node, spreads typed activation along
//   EDGE_IMPLEMENTED_BY edges to discover motor primitives.
//   Collects them in EDGE_SEQUENCE order and queues them through
//   MotorCortex. Zero hardcoded trigger keywords — pure traversal.
//
// ShadowBrain:
//   A second ClusterGraph under the same shared_mutex discipline.
//   Runs spread at 0.1× voltage to evaluate consequences before
//   committing. EDGE_REQUIRES violation → veto. Safe → proceed.
// ============================================================

#include "cluster_graph.h"
#include "fpsan_language.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <shared_mutex>

// ============================================================
// SEMANTIC PLANNER — Pure graph-traversal motor assembly
// ============================================================
struct SemanticPlanner {
    static constexpr int MAX_MOTOR_CHAIN = 64;
    static constexpr float IMPL_SPREAD_STRENGTH = 1.0f;

    // Collect motor nodes reachable from goal_id via EDGE_IMPLEMENTED_BY.
    // Returns the number of motor nodes found; fills out_chain[] in
    // EDGE_SEQUENCE order (BFS respects sequence links).
    static int collect_motor_chain(int goal_id, ClusterGraph* graph,
                                   int* out_chain, int max_chain) noexcept {
        if (goal_id < 0 || !graph) return 0;

        // Spread typed activation from the goal along EDGE_IMPLEMENTED_BY
        {
            std::shared_lock<std::shared_mutex> lk(graph->graph_rw_lock);
            graph->clear_activation();
            graph->spread_typed(goal_id, EDGE_IMPLEMENTED_BY, IMPL_SPREAD_STRENGTH);
        }

        // Walk the activated motor nodes in EDGE_SEQUENCE order.
        // Starting point: most-activated motor node connected to goal_id.
        const int nc = graph->node_count.load(std::memory_order_acquire);
        int chain_len = 0;

        // Find the head motor node (connected to goal via EDGE_IMPLEMENTED_BY)
        int head = -1;
        float best_act = 0.0f;
        {
            int ec = graph->node(goal_id).edge_count.load(std::memory_order_acquire);
            for (int i = 0; i < ec && chain_len < max_chain; i++) {
                const Edge& e = graph->node(goal_id).edges[i];
                if (e.type != EDGE_IMPLEMENTED_BY) continue;
                int t = e.target;
                if (t < 0 || t >= nc) continue;
                if (!graph->node(t).is_motor_node.load(std::memory_order_relaxed)) continue;
                float act = graph->node(t).activation.load(std::memory_order_relaxed);
                if (act > best_act) { best_act = act; head = t; }
            }
        }

        if (head < 0) return 0;

        // Follow EDGE_SEQUENCE chain from head
        int current = head;
        bool visited[MAX_MOTOR_CHAIN] = {};
        while (current >= 0 && chain_len < max_chain) {
            // Simple cycle guard using slot index in out_chain
            bool already_in = false;
            for (int k = 0; k < chain_len; k++) {
                if (out_chain[k] == current) { already_in = true; break; }
            }
            if (already_in) break;

            out_chain[chain_len++] = current;

            // Find next in EDGE_SEQUENCE — pick highest weight
            int next = -1;
            float best_w = 0.0f;
            int ec = graph->node(current).edge_count.load(std::memory_order_acquire);
            for (int i = 0; i < ec; i++) {
                const Edge& e = graph->node(current).edges[i];
                if (e.type != EDGE_SEQUENCE) continue;
                if (e.target < 0 || e.target >= nc) continue;
                if (e.weight > best_w) { best_w = e.weight; next = e.target; }
            }
            current = next;
        }
        return chain_len;
    }

    // Resolve a goal node and print the motor plan (without executing).
    static void print_motor_plan(int goal_id, ClusterGraph* graph,
                                  LanguageCortex* cortex) noexcept {
        int chain[MAX_MOTOR_CHAIN];
        int len = collect_motor_chain(goal_id, graph, chain, MAX_MOTOR_CHAIN);
        printf("[SemanticPlanner] Goal node %d → %d motor primitives:\n", goal_id, len);
        for (int i = 0; i < len; i++) {
            const ClusterNode& n = graph->node(chain[i]);
            const char* label = (chain[i] < 6500) ? cortex->get_word(chain[i]) : "";
            printf("  [%d] node=%d label='%s' type=%d\n",
                   i, chain[i], label,
                   (int)n.motor_action.type);
        }
    }
};

// ============================================================
// SHADOW BRAIN — Counterfactual safety veto layer
// ============================================================
struct ShadowBrain {
    ClusterGraph shadow;

    // Mirrors the main graph topology (shared_lock on main, then copies alive
    // nodes + edges into shadow). max_nodes=0 means "all current nodes".
    void mirror(ClusterGraph* main, int max_nodes = 0) noexcept {
        const int mnc = main->node_count.load(std::memory_order_acquire);
        const int nc  = (max_nodes > 0) ? std::min(mnc, max_nodes) : mnc;
        if (nc <= 0) return;

        if (shadow.cortex_memory == nullptr) {
            shadow.init(nc);  // placement-news exactly nc nodes
        }

        std::shared_lock<std::shared_mutex> lk(main->graph_rw_lock);
        int shadow_nc = shadow.node_count.load(std::memory_order_acquire);
        for (int i = 0; i < nc && i < shadow_nc; i++) {
            ClusterNode& src = main->node(i);
            if (!src.alive.load(std::memory_order_relaxed)) continue;
            ClusterNode& dst = shadow.node(i);
            dst.is_motor_node.store(src.is_motor_node.load(), std::memory_order_relaxed);
            dst.is_binding_node.store(src.is_binding_node.load(), std::memory_order_relaxed);
            dst.activation.store(0.0f, std::memory_order_relaxed);
            // Copy edges (weight × 0.1 to model low-impact sandbox spread)
            int ec = src.edge_count.load(std::memory_order_relaxed);
            for (int e = 0; e < ec; e++) {
                int tgt = src.edges[e].target;
                if (tgt < 0 || tgt >= shadow_nc) continue; // skip out-of-range
                dst.add_edge(tgt, src.edges[e].weight * 0.1f, src.edges[e].type);
            }
        }
    }

    // Veto check: evaluate preconditions against what's activated in the shadow.
    // Checks EDGE_REQUIRES on each motor node from the MAIN graph topology
    // (the shadow may not contain nodes spawned after last mirror() call).
    // Returns true (SAFE) if all EDGE_REQUIRES preconditions are met.
    // Returns false (VETO) if any required precondition is not active in shadow.
    bool is_safe(int goal_id, const int* motor_chain, int chain_len,
                 ClusterGraph* main_ref = nullptr) noexcept {
        if (chain_len == 0) return true;

        // Spread goal in shadow at 0.1× strength (if shadow has the goal node)
        int shadow_nc = shadow.node_count.load(std::memory_order_acquire);
        if (goal_id >= 0 && goal_id < shadow_nc) {
            std::shared_lock<std::shared_mutex> lk(shadow.graph_rw_lock);
            shadow.clear_activation();
            shadow.spread_typed(goal_id, EDGE_IMPLEMENTED_BY, 0.1f);
        }

        // Use main_ref for nodes beyond shadow (recently spawned motor nodes).
        // Check EDGE_REQUIRES edges from each motor node in the chain.
        ClusterGraph* source = main_ref ? main_ref : &shadow;
        const int snc = source->node_count.load(std::memory_order_acquire);

        for (int i = 0; i < chain_len; i++) {
            int mid = motor_chain[i];
            if (mid < 0 || mid >= snc) continue;
            int ec = source->node(mid).edge_count.load(std::memory_order_acquire);
            for (int e = 0; e < ec; e++) {
                if (source->node(mid).edges[e].type != EDGE_REQUIRES) continue;
                int req_id = source->node(mid).edges[e].target;
                if (req_id < 0) continue;
                // Preconditions checked against REAL WORLD activation (main_ref).
                // The shadow models hypothetical consequences; preconditions are
                // facts about the current state. If no main_ref, use shadow.
                float req_act = 0.0f;
                int main_nc = main_ref
                    ? (int)main_ref->node_count.load(std::memory_order_acquire) : 0;
                if (main_ref && req_id < main_nc) {
                    req_act = main_ref->node(req_id).activation.load(std::memory_order_acquire);
                } else if (req_id < shadow_nc) {
                    req_act = shadow.node(req_id).activation.load(std::memory_order_acquire);
                }
                if (req_act < 0.05f) {
                    printf("[ShadowBrain] VETO: motor node %d requires node %d (act=%.3f < 0.05)\n",
                           mid, req_id, req_act);
                    return false;
                }
            }
        }
        return true;
    }
};

// ============================================================
// HTTP TOOL PRIMITIVE — Wire fetch_url via EDGE_IMPLEMENTED_BY
// ============================================================
// Call once at boot to register the HTTP-get motor primitive.
// The concept node "fetch" → EDGE_IMPLEMENTED_BY → [motor: MOTOR_HTTP_GET].
// No hardcoded string matching — the planner activates fetch_url by voltage.
inline int register_http_tool_primitive(const char* concept_word,
                                         const char* url,
                                         ClusterGraph* graph,
                                         LanguageCortex* lang_cortex,
                                         SpikingTokenizer* tokenizer) {
    int8_t h[256];
    std::string ws(concept_word);
    tokenizer->encode_word_hash(ws, h);
    int concept_id = lang_cortex->perceive(h, true, concept_word);
    if (concept_id < 0) return -1;

    int m_id = graph->spawn();
    if (m_id < 0) return -1;
    graph->node(m_id).is_motor_node.store(true, std::memory_order_release);
    graph->node(m_id).motor_action.type = MOTOR_HTTP_GET;
    strncpy(graph->node(m_id).motor_action.text, url, 255);
    graph->node(m_id).motor_action.text[255] = '\0';

    graph->node(concept_id).add_edge(m_id, 2.0f, EDGE_IMPLEMENTED_BY);
    return m_id;
}
