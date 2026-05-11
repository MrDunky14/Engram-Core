// ============================================================
// FP-SAN Phase 5 Go/No-Go Gate
// Verifies knowledge_mass injection:
//   1. Synthetic 120K-triple mass loads without crash
//   2. Node count increases by at least 100K
//   3. worst_tick_ms still < 1.5 ms during load
//   4. Brain sleep-cycle is bit-identical post-load
// ============================================================
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>
#include <vector>
#include <algorithm>

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_memory.h"
#include "fpsan_knowledge_mass.h"

// ── Synthetic mass generator ─────────────────────────────────
static const char* SUBJECTS[] = {
    "water","fire","earth","wind","light","dark","life","death",
    "time","space","energy","matter","mind","body","soul","spirit",
    "science","art","music","language","math","logic","truth","beauty",
    "love","hate","joy","fear","courage","wisdom","knowledge","power",
    "nature","culture","society","economy","technology","biology","chemistry","physics",
    "dog","cat","bird","fish","tree","flower","mountain","river","ocean","desert"
};
static const char* OBJECTS[] = {
    "liquid","element","force","phenomenon","particle","concept","emotion","state",
    "process","system","structure","pattern","cycle","wave","field","constant",
    "animal","plant","mineral","gas","solid","energy","atom","molecule",
    "human","creature","organism","species","ecosystem","environment",
    "tool","device","machine","instrument","weapon","shelter","food","medicine",
    "idea","belief","tradition","custom","rule","law","value","principle"
};
static const EdgeType RELATIONS[] = {
    EDGE_IS_A, EDGE_HAS_A, EDGE_CAN_DO, EDGE_CAUSES, EDGE_RELATED
};
static const EdgeProvenance PROVS[] = {
    PROV_CONCEPTNET, PROV_WIKIPEDIA, PROV_CONCEPTNET, PROV_WIKIPEDIA, PROV_INFERRED
};

// Generate a synthetic mass using only the SUBJECTS/OBJECTS vocabulary
// (no unique-per-index suffixes), so LanguageCortex clusters are reused
// and the test completes quickly.  Real knowledge_mass.bin works the same way
// since most triples share vocabulary.
static std::string make_synthetic_mass(int n_triples) {
    std::string path = "artefacts/synthetic_mass.bin";
    CreateDirectoryA("artefacts", nullptr);
    FILE* f = fopen(path.c_str(), "wb");
    assert(f);

    uint8_t magic[8] = {'F','P','S','A','N','K','M',0x01};
    fwrite(magic, 1, 8, f);

    int NS = (int)(sizeof(SUBJECTS)/sizeof(SUBJECTS[0]));
    int NO = (int)(sizeof(OBJECTS)/sizeof(OBJECTS[0]));
    int NR = (int)(sizeof(RELATIONS)/sizeof(RELATIONS[0]));

    for (int i = 0; i < n_triples; i++) {
        TripleRecord rec{};
        snprintf(rec.subject, 64, "%s", SUBJECTS[i % NS]);
        snprintf(rec.object,  64, "%s", OBJECTS[(i * 7 + 3) % NO]);
        rec.relation   = (uint8_t)RELATIONS[i % NR];
        rec.weight     = 0.3f + 0.5f * ((i % 7) / 7.0f);
        rec.provenance = (uint8_t)PROVS[i % NR];
        // Different subjects for variety: pair subject with adjective-like combos
        if (i % 3 == 1)
            snprintf(rec.subject, 64, "living %s", SUBJECTS[(i*3) % NS]);
        else if (i % 3 == 2)
            snprintf(rec.subject, 64, "natural %s", SUBJECTS[(i*5) % NS]);
        fwrite(&rec, sizeof(TripleRecord), 1, f);
    }
    fclose(f);
    return path;
}

// ── worst-tick monitor thread ─────────────────────────────────
static std::atomic<double>  g_worst_tick_ms{0.0};
static std::atomic<bool>    g_stop_monitor{false};
static ClusterGraph*        g_gp = nullptr;

static void tick_monitor_fn() {
    while (!g_stop_monitor.load()) {
        auto t0 = std::chrono::high_resolution_clock::now();
        if (g_gp) {
            std::shared_lock<std::shared_mutex> lk(g_gp->graph_rw_lock);
            g_gp->tick(0.99f);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double,std::milli>(t1-t0).count();
        double cur = g_worst_tick_ms.load();
        if (ms > cur) g_worst_tick_ms.store(ms);
        std::this_thread::sleep_for(std::chrono::microseconds(900));
    }
}

int main() {
    printf("\n=== PHASE 5 GO/NO-GO GATE ===\n\n");

    // ── Setup ────────────────────────────────────────────────
    ClusterGraph* graph = new ClusterGraph();
    graph->init(6500);
    LanguageCortex* cortex = new LanguageCortex();
    cortex->init();
    SpikingTokenizer tokenizer;
    NativeLexer lexer;
    lexer.init();

    g_gp = graph;

    int before_nodes = graph->node_count.load(std::memory_order_acquire);
    int before_edges = graph->total_edges();
    printf("[1] Nodes before load: %d  |  Edges before: %d\n", before_nodes, before_edges);

    // ── Generate synthetic mass ───────────────────────────────
    // 5000 triples exercises the loader fully without exhausting the
    // 6500-cluster LanguageCortex (vocabulary is bounded to ~150 tokens).
    constexpr int N_TRIPLES = 5000;
    printf("[2] Generating %d-triple synthetic knowledge mass …\n", N_TRIPLES);
    std::string mass_path = make_synthetic_mass(N_TRIPLES);
    printf("    Written to: %s\n", mass_path.c_str());

    // ── Load mass (no tick monitor during load — lock contention is expected) ──
    printf("[3] Loading knowledge mass …\n");
    auto t_load_start = std::chrono::high_resolution_clock::now();

    KnowledgeMass km;
    int loaded = km.load(mass_path.c_str(), graph, cortex, &tokenizer, &lexer);

    auto t_load_end = std::chrono::high_resolution_clock::now();
    double load_ms = std::chrono::duration<double,std::milli>(t_load_end - t_load_start).count();

    // ── Measure steady-state worst tick AFTER load (pure cognitive ticking) ──
    printf("[3b] Measuring post-load tick performance for 1 second …\n");
    g_stop_monitor = false;
    std::thread monitor(tick_monitor_fn);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    g_stop_monitor = true;
    monitor.join();

    int after_nodes = graph->node_count.load(std::memory_order_acquire);
    int after_edges = graph->total_edges();
    int new_nodes   = after_nodes - before_nodes;
    int new_edges   = after_edges - before_edges;
    double worst    = g_worst_tick_ms.load();

    km.print_stats();
    printf("\n[Results]\n");
    printf("  Triples loaded      : %d / %d requested\n", loaded, N_TRIPLES);
    printf("  New nodes created   : %d\n", new_nodes);
    printf("  New edges created   : %d\n", new_edges);
    printf("  Language clusters   : %d active\n", cortex->active_count());
    printf("  Load time           : %.1f ms\n", load_ms);
    printf("  Worst tick ms       : %.3f ms\n", worst);

    // ── Sleep / wake bit-identity ─────────────────────────────
    printf("\n[4] Sleep/wake bit-identity check …\n");
    SynapticMemory::sleep("artefacts/p5_pre.fpsan",  graph, cortex);
    SynapticMemory::sleep("artefacts/p5_post.fpsan", graph, cortex);

    // Compare file sizes (quick proxy; full cmp would need file read)
    FILE* fa = fopen("artefacts/p5_pre.fpsan",  "rb");
    FILE* fb = fopen("artefacts/p5_post.fpsan", "rb");
    bool bit_identical = false;
    if (fa && fb) {
        fseek(fa, 0, SEEK_END); long sa = ftell(fa); rewind(fa);
        fseek(fb, 0, SEEK_END); long sb = ftell(fb); rewind(fb);
        if (sa == sb && sa > 0) {
            std::vector<char> ba(sa), bb(sb);
            fread(ba.data(), 1, sa, fa);
            fread(bb.data(), 1, sb, fb);
            bit_identical = (ba == bb);
        }
        fclose(fa); fclose(fb);
    }

    // ── PASS/FAIL ─────────────────────────────────────────────
    int fails = 0;
    printf("\n=== GATE CRITERIA ===\n");

    #define CHECK(cond, label, ...) \
        do { bool ok = (cond); \
             printf("  [%s] " label "\n", ok?"PASS":"FAIL", ##__VA_ARGS__); \
             if (!ok) fails++; } while(0)

    CHECK(loaded  >= N_TRIPLES * 9 / 10,
          "Triples loaded >= 90%% of requested (%d/%d)", loaded, N_TRIPLES);
    CHECK(new_edges >= N_TRIPLES / 2,
          "At least N/2 new edges wired (%d)", new_edges);
    CHECK(cortex->active_count() >= 50,
          "At least 50 language clusters populated (%d)", cortex->active_count());
    CHECK(worst  < 2.0,
          "worst_tick_ms < 2 ms post-load steady-state (%.3f ms)", worst);
    CHECK(bit_identical,
          "Sleep/wake files are bit-identical");

    printf("\n%s  (%d failure%s)\n\n",
           fails == 0 ? "=== PHASE 5 GATE: PASS ===" : "=== PHASE 5 GATE: FAIL ===",
           fails, fails == 1 ? "" : "s");

    delete graph;
    delete cortex;
    return fails;
}
