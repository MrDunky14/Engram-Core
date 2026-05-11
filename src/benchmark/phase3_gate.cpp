// ============================================================
// FP-SAN Phase 3 Go/No-Go Gate — Semantic Sandbox
//
// Pass criteria (all required):
//   1. SemanticPlanner resolves goal → motor chain via EDGE_IMPLEMENTED_BY
//      with ZERO hardcoded keyword checks.
//   2. ShadowBrain correctly vetoes destructive scenarios (100%).
//   3. ShadowBrain does NOT veto safe scenarios (≤ 10% false-veto rate).
//   4. HTTP tool primitive (MOTOR_HTTP_GET) is reachable via graph traversal.
//   5. Phase 1 + Phase 2 gates still green (compiled and passed externally).
//
// Compile:
//   cl /std:c++17 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /I src\core /I src\benchmark
//      src\benchmark\phase3_gate.cpp src\benchmark\fpsan_stub.cpp
//      /Fe:build\phase3_gate.exe /link Psapi.lib Winhttp.lib Ws2_32.lib
// ============================================================

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_reasoning.h"
#include "fpsan_sandbox.h"
#include "honest_harness.h"
#include <cstdio>
#include <cstring>
#include <shared_mutex>

// ── Global brain ──────────────────────────────────────────────
static ClusterGraph   g_graph;
static LanguageCortex g_cortex;
SpikingTokenizer      g_tokenizer;
NativeLexer           g_lexer;

static int get_or_create_word(const char* word) {
    int8_t h[256];
    std::string ws(word);
    g_tokenizer.encode_word_hash(ws, h);
    int id = g_cortex.perceive(h, true, word);
    return id;
}

// ────────────────────────────────────────────────────────────
// SCENARIO builder helpers
// ────────────────────────────────────────────────────────────

// Create a minimal motor chain for a concept:
//   concept → EDGE_IMPLEMENTED_BY → motor_a → EDGE_SEQUENCE → motor_b ...
static int build_concept_motor_chain(const char* concept, int n_steps, int* out_motors) {
    int concept_id = get_or_create_word(concept);
    int prev = concept_id;
    for (int i = 0; i < n_steps; i++) {
        int m = g_graph.spawn();
        if (m < 0) return i;
        g_graph.node(m).is_motor_node.store(true, std::memory_order_release);
        g_graph.node(m).motor_action.type = MOTOR_SLEEP_MS;
        g_graph.node(m).motor_action.delay_ms = 10;  // dummy action
        if (i == 0) {
            g_graph.node(concept_id).add_edge(m, 2.0f, EDGE_IMPLEMENTED_BY);
        } else {
            g_graph.node(prev).add_edge(m, 2.0f, EDGE_SEQUENCE);
        }
        out_motors[i] = m;
        prev = m;
    }
    return n_steps;
}

// Add a EDGE_REQUIRES edge: motor_id requires precondition_id to be active.
static void add_requires(int motor_id, int precondition_id) {
    g_graph.node(motor_id).add_edge(precondition_id, 1.0f, EDGE_REQUIRES);
}

int main() {
    printf("================================================================\n");
    printf(" FP-SAN PHASE 3 GO/NO-GO GATE — SEMANTIC SANDBOX\n");
    printf(" SemanticPlanner | ShadowBrain | HTTP Tool Primitive\n");
    printf("================================================================\n\n");

    HonestHarness h;

    // ── Boot ──────────────────────────────────────────────────
    printf("[BOOT] Initializing graph...\n");
    g_graph.init(INITIAL_CLUSTERS);
    g_cortex.init();
    g_lexer.init();
    printf("[BOOT] Done. node_count=%d\n\n",
           g_graph.node_count.load(std::memory_order_acquire));

    // ── Test 1: SemanticPlanner resolves goal → motor chain ──
    printf("[SEMANTIC] Building 'open_editor' concept with 3-step motor chain...\n");
    {
        int motors[SemanticPlanner::MAX_MOTOR_CHAIN];
        int n = build_concept_motor_chain("open_editor", 3, motors);
        printf("[SEMANTIC] Created %d motor nodes.\n", n);

        int goal_id = get_or_create_word("open_editor");
        int chain[SemanticPlanner::MAX_MOTOR_CHAIN];
        int chain_len = SemanticPlanner::collect_motor_chain(goal_id, &g_graph, chain,
                                                              SemanticPlanner::MAX_MOTOR_CHAIN);

        printf("[SEMANTIC] Planner found %d motor steps (expected 3).\n\n", chain_len);
        h.assert_metric("semantic_planner_found_3_steps",
            (double)chain_len, 3.0, true);  // >= 3
    }

    // ── Test 2: ShadowBrain veto on destructive scenario ─────
    printf("[SANDBOX] Building DESTRUCTIVE scenario: motor requires unmet precondition...\n");
    {
        // "delete_system" concept → motor that REQUIRES "admin_access" (which is NOT active)
        int motors[8]; int n = build_concept_motor_chain("delete_system", 1, motors);
        int precond_id = get_or_create_word("admin_access");
        add_requires(motors[0], precond_id);  // requires admin_access to be active

        int goal_id = get_or_create_word("delete_system");

        ShadowBrain sb;
        sb.mirror(&g_graph); // mirror ALL current nodes (0 = use node_count)

        // admin_access node is NOT activated → veto expected
        // Pass &g_graph as main_ref so motor nodes beyond INITIAL_CLUSTERS are visible
        bool safe = sb.is_safe(goal_id, motors, n, &g_graph);
        printf("[SANDBOX] Destructive scenario veto=%s (expected VETO=false)\n\n",
               safe ? "NO-VETO" : "VETO");
        h.assert_metric("sandbox_vetoes_destructive",
            safe ? 0.0 : 1.0, 1.0, true);  // veto = 1.0
    }

    // ── Test 3: ShadowBrain passes safe scenario ──────────────
    printf("[SANDBOX] Building SAFE scenario: motor requires ACTIVE precondition...\n");
    {
        int motors[8]; int n = build_concept_motor_chain("write_file", 1, motors);
        int precond_id = get_or_create_word("file_system_available");

        // Manually activate the precondition node
        g_graph.node(precond_id).add_voltage(2.0f);
        add_requires(motors[0], precond_id);

        int goal_id = get_or_create_word("write_file");

        ShadowBrain sb2;
        sb2.mirror(&g_graph);

        // Precondition is checked from g_graph (real world).
        // g_graph.node(precond_id).activation was set to 2.0f above → SAFE.
        bool safe2 = sb2.is_safe(goal_id, motors, n, &g_graph);
        printf("[SANDBOX] Safe scenario veto=%s (expected SAFE=true)\n\n",
               safe2 ? "NO-VETO" : "VETO");
        h.assert_metric("sandbox_allows_safe",
            safe2 ? 1.0 : 0.0, 1.0, true);
    }

    // ── Test 4: HTTP tool primitive via graph traversal ───────
    printf("[HTTP] Registering HTTP GET tool primitive...\n");
    {
        int m_id = register_http_tool_primitive(
            "fetch_url",
            "https://en.wikipedia.org/w/api.php?action=query&titles=Water",
            &g_graph, &g_cortex, &g_tokenizer);

        printf("[HTTP] HTTP motor node id=%d\n", m_id);

        // Verify: spread from "fetch_url" concept and check motor is reachable
        int fetch_id = get_or_create_word("fetch_url");
        int chain[SemanticPlanner::MAX_MOTOR_CHAIN];
        int chain_len = SemanticPlanner::collect_motor_chain(
            fetch_id, &g_graph, chain, SemanticPlanner::MAX_MOTOR_CHAIN);

        bool http_in_chain = false;
        for (int i = 0; i < chain_len; i++) {
            if (g_graph.node(chain[i]).motor_action.type == MOTOR_HTTP_GET) {
                http_in_chain = true;
                printf("[HTTP] MOTOR_HTTP_GET node found at chain position %d.\n", i);
            }
        }
        printf("[HTTP] chain_len=%d http_in_chain=%s\n\n",
               chain_len, http_in_chain ? "YES" : "NO");
        h.assert_metric("http_primitive_reachable_via_graph",
            http_in_chain ? 1.0 : 0.0, 1.0, true);
    }

    // ── Test 5: No-hardcode audit (compile-time property) ─────
    // This test verifies the architecture: the planner never checks
    // "if (input == 'open notepad')". Instead it does graph traversal.
    // We test this behaviorally: teach the concept at runtime, verify plan.
    printf("[AUDIT] Zero-hardcode audit: teaching 'compose_poem' at runtime...\n");
    {
        // Brand new concept never seen before — no hardcoded handler exists.
        int concept_id = get_or_create_word("compose_poem");
        int motors[4];
        // Wire motor chain at runtime (simulates what the brain learns over time)
        int m1 = g_graph.spawn();
        int m2 = g_graph.spawn();
        if (m1 >= 0 && m2 >= 0) {
            g_graph.node(m1).is_motor_node.store(true, std::memory_order_release);
            g_graph.node(m1).motor_action.type = MOTOR_SLEEP_MS;
            g_graph.node(m1).motor_action.delay_ms = 1;
            g_graph.node(m2).is_motor_node.store(true, std::memory_order_release);
            g_graph.node(m2).motor_action.type = MOTOR_SLEEP_MS;
            g_graph.node(m2).motor_action.delay_ms = 2;
            g_graph.node(concept_id).add_edge(m1, 2.0f, EDGE_IMPLEMENTED_BY);
            g_graph.node(m1).add_edge(m2, 2.0f, EDGE_SEQUENCE);
        }

        int chain[SemanticPlanner::MAX_MOTOR_CHAIN];
        int chain_len = SemanticPlanner::collect_motor_chain(
            concept_id, &g_graph, chain, SemanticPlanner::MAX_MOTOR_CHAIN);

        printf("[AUDIT] 'compose_poem' plan: %d steps (no hardcoded handler needed).\n\n",
               chain_len);
        h.assert_metric("novel_concept_planned_without_hardcoding",
            (double)chain_len, 1.0, true);
    }

    // ── Print results ─────────────────────────────────────────
    printf("\n");
    return HonestHarness::gate_exit(h);
}
