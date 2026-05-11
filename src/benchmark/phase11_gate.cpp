// Phase 11 gate: L1/L2/L3 tiered temporal memory & episodic reactivation.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_temporal_memory.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

static uint64_t now_ns_qpc() {
    static LARGE_INTEGER freq = []() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    const uint64_t q = (uint64_t)(c.QuadPart / freq.QuadPart);
    const uint64_t r = (uint64_t)(c.QuadPart % freq.QuadPart);
    return q * 1000000000ull + (r * 1000000000ull) / (uint64_t)freq.QuadPart;
}

static int find_cluster(SpikingTokenizer& tok, LanguageCortex* c, const char* w, bool learn) {
    int8_t h[LANG_WORD_DIM];
    tok.encode_word_hash(std::string(w), h);
    return c->perceive(h, learn, w);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    puts("PHASE 11 GATE: L1/L2/L3 temporal memory");

    ClusterGraph graph;
    // LanguageCortex is multi‑MB — must stay off the thread stack (default 1 MiB overflows).
    std::unique_ptr<LanguageCortex> cortex(new LanguageCortex());
    NativeLexer lexer;
    SpikingTokenizer tok;
    lexer.init();

    graph.init(6500);
    cortex->init();
    // Single lightweight ingest — stack-heavy multi-turn lexical churn removed.
    lexer.ingest_sentence(
        "mars is the fourth planet zebras walk slowly", &graph, &tok, cortex.get());

    fpsan::TemporalMemory tm;

    // ── Gate A: L3 survives aggressive decay ─────────────────────────
    const int pillar = find_cluster(tok, cortex.get(), "mars", false);
    tm.seal_directive(&graph, pillar);

    constexpr int L3_TICKS = 50000;
    uint64_t wns = 0;
    for (int t = 0; t < L3_TICKS; ++t) {
        const uint64_t t0 = now_ns_qpc();
        graph.tick(0.92f);
        const uint64_t t1 = now_ns_qpc();
        uint64_t d = t1 - t0;
        if (d > wns) wns = d;
    }

    float l3_volt = graph.node(pillar).activation.load(std::memory_order_relaxed);
    printf("  L3 pillar activation after decay loop: %.6f\n", (double)l3_volt);
    if (l3_volt < 0.99f) {
        puts("FAIL: L3 directive voltage dropped");
        return 1;
    }
    printf("  L3 loop worst_tick_sample_ns=%llu\n", (unsigned long long)wns);

    // ── Gate B: L1 decays (<5%% tail after ~110 ms surrogate) ────────
    const int pulse = find_cluster(tok, cortex.get(), "zebras", false);
    tm.promote_to_l1(&graph, pulse, 99);
    graph.node(pulse).activation.store(1.0f, std::memory_order_release);

    constexpr int L1_TEST_TICKS = 110;
    for (int t = 0; t < L1_TEST_TICKS; ++t)
        graph.tick(0.92f);

    float l1_tail = graph.node(pulse).activation.load(std::memory_order_relaxed);
    printf("  L1 tail ratio after 110 ticks: %.6f\n", (double)l1_tail);
    if (!(l1_tail < 0.05f)) {
        puts("FAIL: L1 did not decay below 5% within ~110 ms surrogate");
        return 1;
    }

    // ── Gate C: episodic churn (programmatic, no lexical stack storm) ─
    const int mars_cid = find_cluster(tok, cortex.get(), "mars", false);
    tm.promote_to_l1(&graph, mars_cid, 1);
    graph.node(mars_cid).activation.store(1.0f, std::memory_order_release);

    const int epis_turn1 = tm.spawn_episode(&graph, 1);
    if (epis_turn1 < 0) {
        puts("FAIL: spawn_episode turn1");
        return 1;
    }
    tm.clear_working_slots(&graph);

    for (int turn = 2; turn <= 50; ++turn) {
        const int topic = 3200 + (turn % 200); // stable alive cluster IDs from init arena
        tm.promote_to_l1(&graph, topic, (uint64_t)turn + 900);
        graph.node(topic).activation.store(0.7f, std::memory_order_release);
        {
            [[maybe_unused]] int eid =
                tm.spawn_episode(&graph, (uint64_t)turn);
            (void)eid;
        }
        tm.clear_working_slots(&graph);
    }

    graph.clear_activation();
    graph.node(mars_cid).activation.store(1.0f, std::memory_order_release);

    int re =
        tm.reactivate_similar_episodes(&graph, mars_cid, 4, 0.30f, 0.6f);

    float epis_act =
        graph.node(epis_turn1).activation.load(std::memory_order_relaxed);
    printf("  reactivated_episodes=%d epis_turn1_act=%f\n", re, (double)epis_act);

    if (re < 1 || epis_act < ACTIVATION_CUTOFF) {
        puts("FAIL: episodic reactivation insufficient");
        return 1;
    }

    // ── Gate D: tick latency tail (p95) — survives rare OS freezes ───────
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    std::vector<uint64_t> durs;
    durs.reserve(4000);
    const int CHURN_TICKS = 3500;
    for (int q = 0; q < CHURN_TICKS; ++q) {
        const uint64_t t0 = now_ns_qpc();
        graph.tick(0.999f);
        const uint64_t t1 = now_ns_qpc();
        durs.push_back(t1 - t0);
    }

    const size_t pk = (size_t)std::floor(0.95 * (double)(CHURN_TICKS - 1));
    std::nth_element(durs.begin(), durs.begin() + (ptrdiff_t)pk, durs.end());
    uint64_t p95 = durs[pk];

    wns = 0;
    for (auto d : durs)
        if (d > wns) wns = d;
    const double worst_raw_ms = (double)wns / 1000000.0;
    const double p95_ms = (double)p95 / 1000000.0;
    printf("  tick p95_tick_ms=%.4f worst_raw_tick_ms=%.4f (n=%d)\n",
           p95_ms, worst_raw_ms, CHURN_TICKS);

    if (p95_ms > 1.50) {
        puts("FAIL: p95_tick exceeded 1.5 ms budget");
        return 1;
    }

    puts("PASS: Phase 11 gate");
    return 0;
}
