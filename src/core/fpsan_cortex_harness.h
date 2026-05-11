#pragma once
// ============================================================
// FP-SAN Phase 0: Four-Cortex Threading Harness Skeleton
// fpsan_cortex_harness.h
//
// Provides the canonical threading infrastructure that all
// cognitive cortices attach to. Brainstem, Fovea, Language,
// and Motor each get their own std::thread gated by a shared
// g_clock_running atomic flag.
//
// This file is the ONLY place threads are spawned. Cortex
// implementations register a tick function and the harness
// manages lifecycle. All cognitive threads hold a shared_lock
// on graph_rw_lock during their work; only SynapticMemory::sleep
// escalates to unique_lock.
// ============================================================

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <functional>
#include <cstring>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "Winmm.lib")

// ── High-resolution sleep ──────────────────────────────────────
// Uses CreateWaitableTimerEx with TIMER_HIGH_RESOLUTION (Win10 1803+).
// Falls back to timeBeginPeriod(1) + sleep_for on older builds.
//
// Windows 11 22H2+ broke per-process timeBeginPeriod for worker threads;
// each thread must call timeBeginPeriod(1) itself OR use the hi-res timer.
// The hi-res timer approach is preferred — no global resolution change needed.
inline void precise_sleep_us(long long us) {
    if (us <= 0) return;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002UL
#endif

    // Cache the waitable timer handle per-thread to avoid repeated CreateHandle.
    static thread_local HANDLE s_timer = nullptr;
    if (s_timer == nullptr) {
        s_timer = CreateWaitableTimerExW(nullptr, nullptr,
                      CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        // Fallback: standard waitable timer (ms-resolution)
        if (s_timer == nullptr)
            s_timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    }

    if (s_timer != nullptr) {
        LARGE_INTEGER ft;
        ft.QuadPart = -10LL * us;  // 100-ns units, negative = relative
        SetWaitableTimerEx(s_timer, &ft, 0, nullptr, nullptr, nullptr, 0);
        WaitForSingleObject(s_timer, INFINITE);
    } else {
        // Last resort: system sleep_for (limited by timer granularity)
        std::this_thread::sleep_for(std::chrono::microseconds(us));
    }
}

// Global cognitive clock flag. Set to false to gracefully stop all cortices.
inline std::atomic<bool> g_clock_running{false};

// ──────────────────────────────────────────────────────────────
// CortexThread — wraps one cognitive thread with a fixed-rate
// tick loop. Rate is specified in microseconds per tick.
// ──────────────────────────────────────────────────────────────
struct CortexThread {
    char   name[32];
    int    tick_interval_us;   // e.g. 1000 = 1 kHz
    std::atomic<uint64_t> tick_count{0};
    std::atomic<bool>     paused{false};
    std::thread           thread_handle;

    // worst_tick_us: maximum tick execution time recorded (single writer: the cortex thread itself).
    // Readers (gate) may read after stop_and_join(). Relaxed ordering is correct here.
    std::atomic<int64_t>  worst_tick_us{0};
    DWORD_PTR             affinity_mask{0};  // 0 = let OS decide; non-zero = pin to core(s)

    using TickFn = std::function<void(uint64_t tick)>;

    CortexThread() {
        memset(name, 0, sizeof(name));
        worst_tick_us.store(0, std::memory_order_relaxed);
    }

    // affinity: bitmask of logical cores — e.g. 0x1=Core0, 0x2=Core1, 0x4=Core2, 0x8=Core3.
    //           Pass 0 to skip pinning.
    // priority: Windows thread priority. THREAD_PRIORITY_HIGHEST is the recommended
    //           floor for 1kHz cognitive threads — pre-empts background OS work without
    //           the system-stability risks of THREAD_PRIORITY_TIME_CRITICAL.
    void start(const char* thread_name, int interval_us, TickFn fn,
               DWORD_PTR affinity  = 0,
               int        priority = THREAD_PRIORITY_HIGHEST) {
        strncpy(name, thread_name, 31);
        tick_interval_us = interval_us;
        affinity_mask    = affinity;

        thread_handle = std::thread([this, fn, priority]() {
            using namespace std::chrono;

            // Elevate priority so cognitive ticks pre-empt normal background OS work.
            SetThreadPriority(GetCurrentThread(), priority);

            // Per-thread timer resolution (Win11 22H2+ requires each thread
            // to call timeBeginPeriod individually; belt-and-suspenders alongside
            // the hi-res waitable timer used in precise_sleep_us).
            timeBeginPeriod(1);

            // Pin this thread to the designated logical core(s).
            if (affinity_mask != 0) {
                HANDLE h = GetCurrentThread();
                DWORD_PTR prev = SetThreadAffinityMask(h, affinity_mask);
                if (prev == 0) {
                    printf("[CortexHarness] WARNING: %s affinity pin failed (mask=0x%llx)\n",
                           name, (unsigned long long)affinity_mask);
                } else {
                    printf("[CortexHarness] %s pinned to core mask 0x%llx (prev=0x%llx)\n",
                           name, (unsigned long long)affinity_mask, (unsigned long long)prev);
                }
            }

            printf("[CortexHarness] %s thread started (interval=%d us)\n",
                   name, tick_interval_us);

            while (g_clock_running.load(std::memory_order_acquire)) {
                if (paused.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(microseconds(tick_interval_us));
                    continue;
                }

                auto t_start = high_resolution_clock::now();

                fn(tick_count.load(std::memory_order_relaxed));
                tick_count.fetch_add(1, std::memory_order_relaxed);

                int64_t elapsed = (int64_t)duration_cast<microseconds>(
                    high_resolution_clock::now() - t_start).count();

                // Track worst tick (single writer — relaxed is safe).
                if (elapsed > worst_tick_us.load(std::memory_order_relaxed))
                    worst_tick_us.store(elapsed, std::memory_order_relaxed);

                long long sleep_us = (long long)tick_interval_us - elapsed;
                precise_sleep_us(sleep_us);
            }

            printf("[CortexHarness] %s stopped after %llu ticks | worst_tick=%.3fms\n",
                   name,
                   (unsigned long long)tick_count.load(),
                   worst_tick_ms());
        });
    }

    void pause()  { paused.store(true,  std::memory_order_release); }
    void resume() { paused.store(false, std::memory_order_release); }

    void stop_and_join() {
        if (thread_handle.joinable())
            thread_handle.join();
    }

    uint64_t ticks()          const { return tick_count.load(std::memory_order_relaxed); }
    double   worst_tick_ms()  const {
        return worst_tick_us.load(std::memory_order_relaxed) / 1000.0;
    }
};

// ──────────────────────────────────────────────────────────────
// CognitiveClock — manages all four cortex threads.
// ──────────────────────────────────────────────────────────────
struct CognitiveClock {
    CortexThread brainstem;
    CortexThread fovea;
    CortexThread language;
    CortexThread motor;

    // Core assignment (default, overridable per-call):
    //   Core 0 (0x1) — Brainstem:  1 kHz spread loop (highest priority)
    //   Core 1 (0x2) — Language:   1 kHz language processing
    //   Core 2 (0x4) — Motor:      1 kHz motor execution
    //   Core 3 (0x8) — Fovea/EARS: 30 Hz visual + acoustic ring buffer
    void start_all(
        CortexThread::TickFn brainstem_fn,
        CortexThread::TickFn fovea_fn,
        CortexThread::TickFn language_fn,
        CortexThread::TickFn motor_fn,
        DWORD_PTR brainstem_affinity = 0x01,
        DWORD_PTR language_affinity  = 0x02,
        DWORD_PTR motor_affinity     = 0x04,
        DWORD_PTR fovea_affinity     = 0x08,
        int       cognitive_priority = THREAD_PRIORITY_HIGHEST)
    {
        g_clock_running.store(true, std::memory_order_release);
        brainstem.start("Brainstem", 1000,  brainstem_fn, brainstem_affinity, cognitive_priority);
        language.start ("Language",  1000,  language_fn,  language_affinity,  cognitive_priority);
        motor.start    ("Motor",     1000,  motor_fn,     motor_affinity,     cognitive_priority);
        fovea.start    ("Fovea",     33333, fovea_fn,     fovea_affinity,     cognitive_priority);
    }

    void pause_cognitive() {
        brainstem.pause();
        fovea.pause();
        language.pause();
        motor.pause();
    }

    void resume_cognitive() {
        brainstem.resume();
        fovea.resume();
        language.resume();
        motor.resume();
    }

    void shutdown() {
        g_clock_running.store(false, std::memory_order_release);
        brainstem.stop_and_join();
        fovea.stop_and_join();
        language.stop_and_join();
        motor.stop_and_join();
        printf("[CognitiveClock] All cortices halted.\n");
    }
};
