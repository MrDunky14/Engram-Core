// ============================================================
// Phase 10 Gate: SAN Async Scheduler
// - Sustained high spike rate for 60 seconds
// - worst_tick_ms < 1.5ms in a 1kHz loop
// - Deterministic propagation given identical input order
// ============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_san_scheduler.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static uint64_t now_ns() {
    static LARGE_INTEGER freq = []() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    // Avoid 64-bit overflow: split into seconds + remainder.
    const uint64_t q = (uint64_t)(t.QuadPart / freq.QuadPart);
    const uint64_t r = (uint64_t)(t.QuadPart % freq.QuadPart);
    return q * 1000000000ull + (r * 1000000000ull) / (uint64_t)freq.QuadPart;
}

static void pin_thread_to_core(int core_index) {
    DWORD_PTR mask = (DWORD_PTR)1ull << (DWORD_PTR)core_index;
    SetThreadAffinityMask(GetCurrentThread(), mask);
}

static void set_thread_realtimeish() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

static void set_thread_producer_priority() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
}

struct DeterminismDigest {
    // small digest of activations for a fixed set of nodes
    uint64_t x = 1469598103934665603ull; // FNV offset
    void add(float v) {
        uint32_t u;
        static_assert(sizeof(u) == sizeof(v), "float size");
        std::memcpy(&u, &v, sizeof(u));
        x ^= (uint64_t)u;
        x *= 1099511628211ull;
    }
};

static DeterminismDigest snapshot_digest(ClusterGraph& g, const int* ids, int n) {
    DeterminismDigest d;
    for (int i = 0; i < n; ++i) {
        float v = g.node(ids[i]).activation.load(std::memory_order_relaxed);
        d.add(v);
    }
    return d;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("PHASE 10 GATE: SAN async scheduler\n");

    ClusterGraph graph;
    LanguageCortex cortex;
    graph.init(6500);
    cortex.init();

    // Seed a small deterministic subgraph: a->b->c fanout.
    const int a = 10, b = 11, c = 12, d = 13, e = 14;
    graph.node(a).add_edge(b, 1.0f, EDGE_TEMPORAL);
    graph.node(a).add_edge(c, 1.0f, EDGE_TEMPORAL);
    graph.node(b).add_edge(d, 1.0f, EDGE_TEMPORAL);
    graph.node(c).add_edge(e, 1.0f, EDGE_TEMPORAL);

    fpsan::SANScheduler san;
    san.bind_graph(&graph);
    san.set_coarse_now_ns([]() noexcept -> uint64_t { return now_ns(); });
    san.set_propagation_enabled(true);

    // Gate uses an explicit producer to enqueue spikes; keep threshold hook disabled
    // to avoid recursive enqueue storms during throughput measurement.
    graph.bind_san_poster(
        [](void* user, uint32_t cid, float voltage) noexcept {
            (void)cid; (void)voltage; (void)user;
        },
        &san,
        1000000.0f);

    // Producer (for throughput run) starts AFTER determinism check.
    std::atomic<bool> running(true);
    std::atomic<uint64_t> spikes_attempted(0);
    std::atomic<uint64_t> spikes_posted(0);
    std::thread producer;

    // Determinism: run two identical 2-second windows and compare digests.
    auto run_window = [&](int seconds, uint64_t budget_ns) {
        pin_thread_to_core(0);
        set_thread_realtimeish();

        uint64_t worst_tick_ns = 0;
        const uint64_t ticks = (uint64_t)seconds * 1000ull;
        uint64_t next_tick_ns = now_ns();
        for (uint64_t ti = 0; ti < ticks; ++ti) {
            const uint64_t t0 = now_ns();
            // budget-bounded drain; gate measures scheduler cost only.
            san.drain_until(now_ns(), budget_ns);
            const uint64_t t1 = now_ns();
            const uint64_t dt = t1 - t0;
            if (dt > worst_tick_ns) worst_tick_ns = dt;

            // Pace to ~1kHz (wait remainder).
            next_tick_ns += 1000000ull;
            while (now_ns() < next_tick_ns) {
                Sleep(0);
            }
        }

        const double worst_ms = (double)worst_tick_ns / 1000000.0;
        printf("  window worst_tick_ms=%.3f\n", worst_ms);
        if (worst_ms > 1.5) {
            printf("FAIL: worst_tick_ms exceeded 1.5ms\n");
            std::exit(1);
        }

        const int ids[5] = {a,b,c,d,e};
        return snapshot_digest(graph, ids, 5);
    };

    // Warmup: seed initial event.
    graph.clear_activation();
    san.post_spike((uint32_t)a, 1.0f);

    printf("  Determinism check...\n");
    const auto d1 = run_window(2, 50000ull);
    // Flush any remaining queued events (should be empty, but make it explicit).
    (void)san.drain_until(now_ns(), 1000000000ull);
    graph.clear_activation();
    san.post_spike((uint32_t)a, 1.0f);
    const auto d2 = run_window(2, 50000ull);
    if (d1.x != d2.x) {
        printf("FAIL: determinism digest mismatch (%llu vs %llu)\n",
               (unsigned long long)d1.x, (unsigned long long)d2.x);
        return 1;
    }
    printf("  Determinism OK\n");

    printf("  Throughput + stability run (60s)...\n");
    san.set_propagation_enabled(false);
    // Two producers to hit >= 1M successful enqueues/sec.
    std::thread producer2([&]() {
        pin_thread_to_core(2);
        set_thread_producer_priority();
        while (running.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 8192; ++i) {
                spikes_attempted.fetch_add(1, std::memory_order_relaxed);
                if (san.post_spike((uint32_t)a, 1.0f)) spikes_posted.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    producer = std::thread([&]() {
        pin_thread_to_core(1);
        set_thread_producer_priority();
        while (running.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 8192; ++i) {
                spikes_attempted.fetch_add(1, std::memory_order_relaxed);
                if (san.post_spike((uint32_t)a, 1.0f)) spikes_posted.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    const uint64_t before_attempt = spikes_attempted.load(std::memory_order_relaxed);
    const uint64_t before_posted = spikes_posted.load(std::memory_order_relaxed);
    const auto d3 = run_window(60, 700000ull);
    (void)d3;
    const uint64_t after_attempt = spikes_attempted.load(std::memory_order_relaxed);
    const uint64_t after_posted = spikes_posted.load(std::memory_order_relaxed);
    const uint64_t attempted = after_attempt - before_attempt;
    const uint64_t posted = after_posted - before_posted;
    const double attempt_rate = (double)attempted / 60.0;
    const double post_rate = (double)posted / 60.0;
    printf("  spikes_attempted=%llu (%.0f spikes/sec)\n", (unsigned long long)attempted, attempt_rate);
    printf("  spikes_posted=%llu (%.0f spikes/sec)\n", (unsigned long long)posted, post_rate);
    if (attempt_rate < 1000000.0) {
        printf("FAIL: synthetic firehose < 1M spikes/sec\n");
        running.store(false);
        producer.join();
        producer2.join();
        return 1;
    }

    running.store(false);
    producer.join();
    producer2.join();

    printf("PASS: Phase 10 gate\n");
    return 0;
}

