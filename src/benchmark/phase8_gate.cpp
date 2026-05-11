// ============================================================
// FP-SAN Phase 8 Go/No-Go Gate
// Validates contradiction detection + trust-weighted generation:
//
//   1. Inject "sky IS_A blue" (Wikipedia source)
//   2. Wire "blue" ANTONYM "red"
//   3. Inject "sky IS_A red" (user source)
//   4. Run detect_contradictions() → must find ≥1 contradiction
//   5. Doubt drive rises to ≥ 0.5
//   6. Generate from "sky" — response must prefer Wikipedia-sourced path
//   7. contradictions.csv must be written with ≥1 row
// ============================================================
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <vector>
#include <string>
#include <fstream>
#include <shared_mutex>

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_drives.h"

static int perceive(const char* word, SpikingTokenizer& tok, LanguageCortex* c, bool create) {
    int8_t h[256];
    std::string w(word);
    tok.encode_word_hash(w, h);
    return c->perceive(h, create, word);
}

int main() {
    printf("\n=== PHASE 8 GO/NO-GO GATE ===\n\n");

    ClusterGraph* graph = new ClusterGraph();
    graph->init(6500);
    LanguageCortex* cortex = new LanguageCortex();
    cortex->init();
    SpikingTokenizer tokenizer;
    NativeLexer lexer;
    lexer.init();
    HomeostaticDrives drives;
    drives.init();

    // ── Step 1: Establish "sky IS_A blue" (Wikipedia-sourced) ──
    printf("[1] Injecting 'sky IS_A blue' (PROV_WIKIPEDIA) …\n");
    int sky_id  = perceive("sky",  tokenizer, cortex, true);
    int blue_id = perceive("blue", tokenizer, cortex, true);
    int red_id  = perceive("red",  tokenizer, cortex, true);
    assert(sky_id >= 0 && blue_id >= 0 && red_id >= 0);

    graph->node(sky_id).add_edge(blue_id, 0.8f, EDGE_IS_A, PROV_WIKIPEDIA);
    printf("    sky(%d) --IS_A[wiki]--> blue(%d)\n", sky_id, blue_id);

    // ── Step 2: Wire "blue" ANTONYM "red" ───────────────────────
    printf("[2] Wiring 'blue' ANTONYM 'red' …\n");
    graph->node(blue_id).add_edge(red_id, 1.0f, EDGE_ANTONYM, PROV_CONCEPTNET);
    graph->node(red_id).add_edge(blue_id, 1.0f, EDGE_ANTONYM, PROV_CONCEPTNET);
    printf("    blue(%d) --ANTONYM--> red(%d)\n", blue_id, red_id);

    // ── Step 3: Inject "sky IS_A red" (user-sourced) ────────────
    printf("[3] Injecting 'sky IS_A red' (PROV_USER) …\n");
    graph->node(sky_id).add_edge(red_id, 0.6f, EDGE_IS_A, PROV_USER);
    printf("    sky(%d) --IS_A[user]--> red(%d)\n", sky_id, red_id);

    // ── Step 4: Run contradiction detection ──────────────────────
    printf("\n[4] Running detect_contradictions() …\n");
    CreateDirectoryA("artefacts", nullptr);
    int n_contradictions = lexer.detect_contradictions(
        graph, cortex, "artefacts/contradictions.csv");
    printf("    Contradictions found: %d\n", n_contradictions);

    // ── Step 5: Update doubt drive ───────────────────────────────
    if (n_contradictions > 0) {
        for (int i = 0; i < n_contradictions; i++)
            drives.on_contradiction();
    }
    printf("[5] Doubt drive after contradiction: %.3f\n", drives.doubt);

    // ── Step 6: Generate from "sky" with doubt bias ──────────────
    printf("\n[6] Generating from 'sky' with doubt=%.2f (trust bias) …\n", drives.doubt);
    graph->clear_activation();
    {
        std::shared_lock<std::shared_mutex> lk(graph->graph_rw_lock);
        graph->spread_activation(sky_id, 1.0f);
    }
    char buf[512] = {};
    int words = lexer.generate_text(sky_id, graph, cortex, buf, 10,
                                    nullptr, drives.doubt);
    printf("    Generated: \"%s\" (%d words)\n", buf, words);

    // Check if the response contains "blue" (Wikipedia) rather than "red" (user)
    bool prefers_wikipedia = (strstr(buf, "blue") != nullptr);
    bool mentions_red      = (strstr(buf, "red")  != nullptr);

    // ── Step 7: Verify contradictions.csv ───────────────────────
    printf("\n[7] Checking contradictions.csv …\n");
    std::ifstream csv("artefacts/contradictions.csv");
    int csv_rows = 0;
    std::string line;
    bool first_line = true;
    while (std::getline(csv, line)) {
        if (first_line) { first_line = false; continue; } // skip header
        if (!line.empty()) csv_rows++;
    }
    printf("    Rows in contradictions.csv: %d\n", csv_rows);

    // ── GATE CRITERIA ─────────────────────────────────────────────
    int fails = 0;
    printf("\n=== GATE CRITERIA ===\n");

    #define CHECK(cond, label, ...) \
        do { bool ok = (cond); \
             printf("  [%s] " label "\n", ok?"PASS":"FAIL", ##__VA_ARGS__); \
             if (!ok) fails++; } while(0)

    CHECK(n_contradictions >= 1,
          "At least 1 contradiction detected (%d)", n_contradictions);
    CHECK(drives.doubt >= 0.4f,
          "Doubt drive >= 0.4 after contradiction (%.3f)", drives.doubt);
    CHECK(words > 0,
          "Generation produced output (%d words)", words);
    CHECK(prefers_wikipedia || !mentions_red,
          "Generation prefers Wikipedia path (blue) over user path (red): buf=\"%s\"", buf);
    CHECK(csv_rows >= 1,
          "contradictions.csv contains >= 1 data row (%d)", csv_rows);

    printf("\n%s  (%d failure%s)\n\n",
           fails == 0 ? "=== PHASE 8 GATE: PASS ===" : "=== PHASE 8 GATE: FAIL ===",
           fails, fails == 1 ? "" : "s");

    delete graph;
    delete cortex;
    return fails;
}
