// B1 Retention Gauntlet — NIAH-style receipt harness (1000 SVO haystack + first/last needle verify)
//
// Ingests 1000 unique "srv0000 need p100000" sentences (no underscores: '_' is punctuation and
// splits tokens on MSVC, which breaks NP–VP–NP). Single-token server/port ids preserve SVO binding.
// (NP–VP–NP binding topology), then checks the first and last rules are still reachable through
// subject -> binding_node -> object EDGE_TEMPORAL chains.
//
// Build: scripts\compile_research_gates.bat

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

static constexpr int kHaystackRules = 1000;

// Re-resolve cluster id immediately after ingest (learn=true: same path as lexer, always returns id).
static int capture_cluster_id(SpikingTokenizer& tok, LanguageCortex& cx, const char* w) noexcept {
    int8_t h[LANG_WORD_DIM];
    tok.encode_word_hash(std::string(w), h);
    return cx.perceive(h, true, w);
}

// True iff some binding node reached from subj by EDGE_TEMPORAL has EDGE_TEMPORAL to obj.
static bool svo_binding_reaches_obj(const ClusterGraph& g, int subj, int obj) noexcept {
    if (subj < 0 || obj < 0) return false;
    const ClusterNode& sn = g.node(subj);
    int ec = sn.edge_count.load(std::memory_order_acquire);
    for (int i = 0; i < ec; ++i) {
        if (sn.edges[i].type != EDGE_TEMPORAL) continue;
        int b = sn.edges[i].target;
        if (!g.node(b).is_binding_node.load(std::memory_order_acquire)) continue;
        const ClusterNode& bn = g.node(b);
        int bec = bn.edge_count.load(std::memory_order_acquire);
        for (int j = 0; j < bec; ++j) {
            if (bn.edges[j].type == EDGE_TEMPORAL && bn.edges[j].target == obj)
                return true;
        }
    }
    return false;
}

static void fmt_pair(int i, char* srv, size_t srv_sz, char* port, size_t port_sz) noexcept {
    snprintf(srv, srv_sz, "srv%04d", i);
    snprintf(port, port_sz, "p%06d", 100000 + i);
}

int main() {
    std::filesystem::create_directories("artefacts");

    ClusterGraph g;
    g.init();
    LanguageCortex cx;
    cx.init();
    SpikingTokenizer tok;
    NativeLexer lex;
    lex.init();

    char line[192];

    int64_t triples_total = 0;
    int needle0_subj = -1, needle0_obj = -1;
    int needleL_subj = -1, needleL_obj = -1;
    auto t_ingest0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kHaystackRules; ++i) {
        char srv[32], prt[32];
        fmt_pair(i, srv, sizeof srv, prt, sizeof prt);
        snprintf(line, sizeof line, "%s need %s", srv, prt);
        triples_total += lex.ingest_sentence(line, &g, &tok, &cx);
        if (i == 0) {
            needle0_subj = capture_cluster_id(tok, cx, srv);
            needle0_obj = capture_cluster_id(tok, cx, prt);
        } else if (i == kHaystackRules - 1) {
            needleL_subj = capture_cluster_id(tok, cx, srv);
            needleL_obj = capture_cluster_id(tok, cx, prt);
        }
    }
    auto t_ingest1 = std::chrono::high_resolution_clock::now();
    double ingest_total_ms =
        std::chrono::duration<double, std::milli>(t_ingest1 - t_ingest0).count();

    auto t_v0 = std::chrono::high_resolution_clock::now();
    bool retention_first =
        needle0_subj >= 0 && needle0_obj >= 0 && svo_binding_reaches_obj(g, needle0_subj, needle0_obj);
    bool retention_last =
        needleL_subj >= 0 && needleL_obj >= 0 && svo_binding_reaches_obj(g, needleL_subj, needleL_obj);
    auto t_v1 = std::chrono::high_resolution_clock::now();
    double verify_ms = std::chrono::duration<double, std::milli>(t_v1 - t_v0).count();

    bool exit_ok = retention_first && retention_last;

    const char* json_path = "artefacts/b1_retention_niah.json";
    if (FILE* fp = fopen(json_path, "wb")) {
        fprintf(fp,
                "{\n"
                "  \"benchmark\": \"B1_retention_gauntlet\",\n"
                "  \"mapping\": \"NIAH_style_structural_retrieval\",\n"
                "  \"haystack_rules\": %d,\n"
                "  \"retention_first\": %s,\n"
                "  \"retention_last\": %s,\n"
                "  \"ingest_total_ms\": %.6f,\n"
                "  \"verify_ms\": %.6f,\n"
                "  \"triples_total\": %lld,\n"
                "  \"exit_ok\": %s\n"
                "}\n",
                kHaystackRules,
                retention_first ? "true" : "false",
                retention_last ? "true" : "false",
                ingest_total_ms,
                verify_ms,
                (long long)triples_total,
                exit_ok ? "true" : "false");
        fclose(fp);
    }

    printf(
        "B1 NIAH gauntlet: haystack=%d retention_first=%s retention_last=%s ingest_ms=%.3f "
        "verify_ms=%.6f triples=%lld exit_ok=%s\n",
        kHaystackRules,
        retention_first ? "true" : "false",
        retention_last ? "true" : "false",
        ingest_total_ms,
        verify_ms,
        (long long)triples_total,
        exit_ok ? "true" : "false");

    return exit_ok ? 0 : 1;
}
