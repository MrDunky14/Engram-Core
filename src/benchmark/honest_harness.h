#pragma once
// ============================================================
// FP-SAN Honest Benchmark Harness
// Neutral CSV reporter. No victory language. Numbers only.
//
// Usage:
//   HonestHarness h;
//   h.run("spread_activation_1k", 100, [&]() -> bool {
//       auto t0 = hclock::now();
//       graph.spread_activation(seed, 1.0f);
//       auto dt = hclock::now() - t0;
//       // return false to count as a failure trial
//       return true;
//   }, 1.0 /*pass_threshold_ms p99*/);
//   h.print_csv();
//   return h.all_passed() ? 0 : 1;
// ============================================================

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <functional>
#include <vector>
#include <string>
#include <atomic>

using hclock = std::chrono::high_resolution_clock;

struct BenchResult {
    char   name[128];
    double mean_ms;
    double p50_ms;
    double p95_ms;
    double p99_ms;
    double worst_ms;      // absolute worst single trial (max latency ever seen)
    double ops_per_sec;
    int    fail_count;
    int    total_trials;
    bool   passed;        // against threshold
    double threshold_ms;  // p99 must be <= this to pass
    double worst_threshold_ms; // worst_ms must be <= this (0 = skip)
};

struct HonestHarness {
    std::vector<BenchResult> results;
    bool _all_passed = true;

    // ──────────────────────────────────────────────────────────
    // run() — executes `n_trials` calls to `fn`.
    // fn returns bool: true = success, false = logic failure.
    // threshold_p99_ms:   pass criterion for p99 latency (<=). 0 = skip.
    // threshold_worst_ms: pass criterion for worst single tick (<=). 0 = skip.
    //   The worst_tick gate catches thermal throttle / scheduling spikes.
    // ──────────────────────────────────────────────────────────
    void run(const char* name,
             int n_trials,
             std::function<bool()> fn,
             double threshold_p99_ms   = 0.0,
             double threshold_worst_ms = 0.0)
    {
        std::vector<double> times;
        times.reserve(n_trials);
        int fails = 0;

        for (int i = 0; i < n_trials; i++) {
            auto t0 = hclock::now();
            bool ok = fn();
            double ms = std::chrono::duration<double, std::milli>(
                hclock::now() - t0).count();
            times.push_back(ms);
            if (!ok) fails++;
        }

        std::sort(times.begin(), times.end());

        BenchResult r{};
        strncpy(r.name, name, 127);
        r.total_trials       = n_trials;
        r.fail_count         = fails;
        r.threshold_ms       = threshold_p99_ms;
        r.worst_threshold_ms = threshold_worst_ms;

        double sum = 0.0;
        for (double t : times) sum += t;
        r.mean_ms = sum / n_trials;

        r.p50_ms   = times[static_cast<int>(n_trials * 0.50)];
        r.p95_ms   = times[static_cast<int>(n_trials * 0.95)];
        r.p99_ms   = times[static_cast<int>(n_trials * 0.99)];
        r.worst_ms = times.back();  // absolute max — catches thermal spikes
        r.ops_per_sec = (r.mean_ms > 0.0) ? (1000.0 / r.mean_ms) : 0.0;

        bool latency_pass = (threshold_p99_ms   <= 0.0) || (r.p99_ms   <= threshold_p99_ms);
        bool worst_pass   = (threshold_worst_ms <= 0.0) || (r.worst_ms <= threshold_worst_ms);
        r.passed = (fails == 0) && latency_pass && worst_pass;

        if (!r.passed) _all_passed = false;
        results.push_back(r);
    }

    // ──────────────────────────────────────────────────────────
    // assert_metric() — record a scalar gate (no timing).
    //   condition: true = pass.
    // ──────────────────────────────────────────────────────────
    void assert_metric(const char* name, double value, double threshold,
                       bool greater_or_equal = true)
    {
        BenchResult r{};
        strncpy(r.name, name, 127);
        r.total_trials = 1;
        r.fail_count   = 0;
        r.mean_ms      = value;
        r.p50_ms       = value;
        r.p95_ms       = value;
        r.p99_ms       = value;
        r.ops_per_sec  = 0.0;
        r.threshold_ms = threshold;
        r.passed = greater_or_equal ? (value >= threshold) : (value <= threshold);
        if (!r.passed) { _all_passed = false; r.fail_count = 1; }
        results.push_back(r);
    }

    // ──────────────────────────────────────────────────────────
    // print_csv() — emit results to stdout in CSV format.
    // NO victory language. Numbers only.
    // ──────────────────────────────────────────────────────────
    void print_csv() const {
        printf("name,mean_ms,p50_ms,p95_ms,p99_ms,worst_ms,ops_per_sec,"
               "fail_count,total_trials,threshold_ms,worst_threshold_ms,status\n");
        for (const auto& r : results) {
            printf("%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%d,%d,%.4f,%.4f,%s\n",
                r.name,
                r.mean_ms, r.p50_ms, r.p95_ms, r.p99_ms, r.worst_ms,
                r.ops_per_sec,
                r.fail_count, r.total_trials,
                r.threshold_ms, r.worst_threshold_ms,
                r.passed ? "PASS" : "FAIL");
        }
    }

    // ──────────────────────────────────────────────────────────
    // print_summary() — one-line per result with PASS/FAIL.
    // ──────────────────────────────────────────────────────────
    void print_summary() const {
        printf("\n=== GATE SUMMARY ===\n");
        int passed = 0, total = (int)results.size();
        for (const auto& r : results) {
            printf("  [%s] %s  p99=%.3fms worst=%.3fms fails=%d/%d\n",
                r.passed ? "PASS" : "FAIL",
                r.name,
                r.p99_ms, r.worst_ms,
                r.fail_count, r.total_trials);
            if (r.passed) passed++;
        }
        printf("=== %d/%d PASSED ===\n", passed, total);
        if (!_all_passed) {
            printf("=== HARD STOP: gate failed, do not proceed to next phase ===\n");
        }
    }

    bool all_passed() const { return _all_passed; }

    // ──────────────────────────────────────────────────────────
    // Gate runner: call with argc/argv from main.
    // Exits non-zero if any criterion fails.
    // ──────────────────────────────────────────────────────────
    static int gate_exit(const HonestHarness& h) {
        h.print_csv();
        h.print_summary();
        return h.all_passed() ? 0 : 1;
    }
};
