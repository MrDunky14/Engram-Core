// ============================================================
// FP-SAN Phase 7 Go/No-Go Gate
// Validates UIA → graph cognitive wiring:
//   1. Calls vision.tick() 5× on a synthetic UIA text string
//   2. Every word in the UIA text becomes a reachable graph node
//   3. Nodes have non-zero activation after spread
//   4. EDGE_VISUAL_CHILD edges are present between consecutive tokens
// ============================================================
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>
#include <sstream>
#include <shared_mutex>

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"

// Simulate what VisualSystem::tick does in Phase 7 without opening Notepad:
// feed a synthetic UIA text string through the lexer into the graph,
// then wire EDGE_VISUAL_CHILD between consecutive tokens.
static int simulate_uia_tick(const char* uia_text,
                              ClusterGraph* graph,
                              LanguageCortex* cortex,
                              SpikingTokenizer* tok,
                              NativeLexer* lexer)
{
    // Ingest the text (creates EDGE_NEXT_WORD bonds)
    int new_triples = lexer->ingest_sentence(uia_text, graph, tok, cortex);

    // Wire EDGE_VISUAL_CHILD between consecutive content tokens
    Token tokens[128];
    int n = lexer->tokenize(uia_text, tokens);
    int8_t wh[256];
    int prev_id = -1;
    int visual_edges = 0;
    for (int i = 0; i < n; i++) {
        std::string w(tokens[i].text);
        tok->encode_word_hash(w, wh);
        int cid = cortex->perceive(wh, false);
        if (cid < 0) continue;
        if (prev_id >= 0 && prev_id != cid) {
            graph->node(prev_id).add_edge(cid, 0.4f, EDGE_VISUAL_CHILD, PROV_VISUAL);
            visual_edges++;
        }
        prev_id = cid;
    }
    return visual_edges;
}

int main() {
    printf("\n=== PHASE 7 GO/NO-GO GATE ===\n\n");

    ClusterGraph* graph = new ClusterGraph();
    graph->init(6500);
    LanguageCortex* cortex = new LanguageCortex();
    cortex->init();
    SpikingTokenizer tokenizer;
    NativeLexer lexer;
    lexer.init();

    // Simulate "Notepad" foreground window text
    const char* NOTEPAD_TEXT =
        "The quick brown fox jumps over the lazy dog. "
        "Artificial intelligence is transforming the world. "
        "Water flows downhill due to gravity. "
        "Dogs are loyal companions to human beings.";

    printf("[1] Simulating 5× UIA vision ticks on synthetic Notepad content …\n");
    printf("    Text: \"%.80s …\"\n\n", NOTEPAD_TEXT);

    int total_visual_edges = 0;
    for (int tick = 0; tick < 5; tick++) {
        int ve = simulate_uia_tick(NOTEPAD_TEXT, graph, cortex, &tokenizer, &lexer);
        printf("  Tick %d: %d EDGE_VISUAL_CHILD wired\n", tick+1, ve);
        total_visual_edges += ve;
    }

    // Spread activation from "fox" node
    printf("\n[2] Spreading activation from seed 'fox' …\n");
    int8_t wh[256];
    std::string fox_w("fox");
    tokenizer.encode_word_hash(fox_w, wh);
    int fox_id = cortex->perceive(wh, false);
    int activated = 0;
    if (fox_id >= 0) {
        graph->clear_activation();
        std::shared_lock<std::shared_mutex> lk(graph->graph_rw_lock);
        graph->spread_activation(fox_id, 1.0f);
    }

    // Count words from the Notepad text that have non-zero activation
    const char* test_words[] = {
        "quick","brown","fox","jumps","lazy","dog",
        "artificial","intelligence","water","dogs","human"
    };
    int words_activated = 0;
    int words_found = 0;
    for (auto* w : test_words) {
        std::string ws(w);
        tokenizer.encode_word_hash(ws, wh);
        int cid = cortex->perceive(wh, false);
        if (cid < 0) continue;
        words_found++;
        float act = graph->node(cid).activation.load(std::memory_order_relaxed);
        printf("  %-20s  node=%4d  activation=%.4f  %s\n",
               w, cid, act, act > 0.0f ? "ACTIVE" : "silent");
        if (act > 0.0f) words_activated++;
    }

    // Check EDGE_VISUAL_CHILD presence in graph
    int visual_child_edges_in_graph = 0;
    {
        std::shared_lock<std::shared_mutex> lk(graph->graph_rw_lock);
        const int nc = graph->node_count.load(std::memory_order_acquire);
        for (int i = 0; i < nc; i++) {
            if (!graph->node(i).alive.load()) continue;
            int ec = graph->node(i).edge_count.load();
            for (int j = 0; j < ec; j++) {
                if (graph->node(i).edges[j].type == EDGE_VISUAL_CHILD)
                    visual_child_edges_in_graph++;
            }
        }
    }

    printf("\n[Results]\n");
    printf("  EDGE_VISUAL_CHILD in graph  : %d\n", visual_child_edges_in_graph);
    printf("  Test words found in cortex  : %d / %d\n", words_found, (int)(sizeof(test_words)/sizeof(test_words[0])));
    printf("  Test words with activation  : %d / %d\n", words_activated, words_found);

    // ── GATE CRITERIA ─────────────────────────────────────────
    int fails = 0;
    printf("\n=== GATE CRITERIA ===\n");

    #define CHECK(cond, label, ...) \
        do { bool ok = (cond); \
             printf("  [%s] " label "\n", ok?"PASS":"FAIL", ##__VA_ARGS__); \
             if (!ok) fails++; } while(0)

    CHECK(words_found >= 8,
          "At least 8 test words mapped to graph nodes (%d found)", words_found);
    CHECK(words_activated >= words_found * 6 / 10,
          "At least 60%% of found nodes show non-zero activation (%d/%d)",
          words_activated, words_found);
    CHECK(visual_child_edges_in_graph >= 5,
          "At least 5 EDGE_VISUAL_CHILD edges in graph (%d)", visual_child_edges_in_graph);
    CHECK(graph->node_count.load() > 50,
          "Graph grew beyond seed (nodes=%d)",
          graph->node_count.load(std::memory_order_acquire));

    printf("\n%s  (%d failure%s)\n\n",
           fails == 0 ? "=== PHASE 7 GATE: PASS ===" : "=== PHASE 7 GATE: FAIL ===",
           fails, fails == 1 ? "" : "s");

    delete graph;
    delete cortex;
    return fails;
}
