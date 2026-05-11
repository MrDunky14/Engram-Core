// ============================================================
// FP-SAN SAN Scheduler (Phase 10)
// Lock-free bounded MPMC spike event queue + propagation.
// Zero heap allocations during runtime.
// ============================================================

#pragma once

#include "cluster_graph.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <malloc.h> // _aligned_malloc/_aligned_free

namespace fpsan {

struct SpikeEvent {
    uint32_t src_cid = 0;
    float    voltage = 0.0f;
    uint8_t  hop = 0;
    uint8_t  reserved0 = 0;
    uint16_t reserved1 = 0;
    uint64_t deadline_ns = 0; // optional scheduling hint
};

// Dmitry Vyukov bounded MPMC queue (power-of-two capacity).
// Storage is allocated once at construction (boot-time), not per tick.
class alignas(64) MpmcQueue {
    struct alignas(64) Cell {
        std::atomic<uint32_t> seq;
        SpikeEvent data;
    };

public:
    explicit MpmcQueue(uint32_t capacity_pow2) noexcept : capacity_(capacity_pow2) {
        // Capacity must be power-of-two and reasonably sized.
        if (capacity_ < 1024u) capacity_ = 1024u;
        if ((capacity_ & (capacity_ - 1)) != 0u) {
            // round down to nearest power-of-two
            uint32_t p = 1;
            while ((p << 1) <= capacity_) p <<= 1;
            capacity_ = p;
        }
        mask_ = capacity_ - 1u;

        cells_ = static_cast<Cell*>(_aligned_malloc(sizeof(Cell) * capacity_, 64));
        if (!cells_) {
            capacity_ = 0;
            mask_ = 0;
            return;
        }
        for (uint32_t i = 0; i < capacity_; ++i) {
            new (&cells_[i]) Cell();
            cells_[i].seq.store(i, std::memory_order_relaxed);
            std::memset(&cells_[i].data, 0, sizeof(SpikeEvent));
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }

    ~MpmcQueue() {
        if (cells_) {
            // Cells contain atomics; no need for explicit destruction beyond free.
            _aligned_free(cells_);
            cells_ = nullptr;
        }
    }

    bool try_push(const SpikeEvent& ev) noexcept {
        if (!cells_) return false;
        uint32_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = cells_[pos & mask_];
            uint32_t seq = c.seq.load(std::memory_order_acquire);
            int32_t diff = (int32_t)seq - (int32_t)pos;
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    c.data = ev;
                    c.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    bool try_pop(SpikeEvent& out) noexcept {
        if (!cells_) return false;
        uint32_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = cells_[pos & mask_];
            uint32_t seq = c.seq.load(std::memory_order_acquire);
            int32_t diff = (int32_t)seq - (int32_t)(pos + 1);
            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    out = c.data;
                    c.seq.store(pos + capacity_, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // empty
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

private:
    uint32_t capacity_{0};
    uint32_t mask_{0};
    Cell* cells_{nullptr};
    std::atomic<uint32_t> enqueue_pos_{0};
    std::atomic<uint32_t> dequeue_pos_{0};
};

// Scheduler: owns the queue and drives spike propagation budget-bounded.
class SANScheduler {
public:
    static constexpr uint32_t kQueueCapacity = 1u << 20; // 1,048,576 events
    static constexpr uint8_t  kMaxHop = 3;

    SANScheduler() : q_(kQueueCapacity) {}

    void bind_graph(ClusterGraph* g) noexcept { graph_ = g; }
    void set_propagation_enabled(bool on) noexcept { propagate_enabled_ = on; }

    bool post_spike(uint32_t cid, float voltage, uint64_t deadline_ns = 0, uint8_t hop = 0) noexcept {
        SpikeEvent ev;
        ev.src_cid = cid;
        ev.voltage = voltage;
        ev.hop = hop;
        ev.deadline_ns = deadline_ns;
        return q_.try_push(ev);
    }

    // Drain until budget is exhausted or queue empties.
    // Returns number of processed events.
    uint32_t drain_until(uint64_t now_ns, uint64_t budget_ns) noexcept {
        if (!graph_) return 0;
        const uint64_t end_ns = now_ns + budget_ns;
        uint32_t processed = 0;
        // Hard cap to guarantee bounded work even without a clock function.
        const uint32_t max_events = (uint32_t)(budget_ns / 1000ull) + 32u;

        // One shared lock for the whole drain window (reduces overhead).
        std::shared_lock<std::shared_mutex> lk(graph_->graph_rw_lock);

        SpikeEvent ev;
        while (q_.try_pop(ev)) {
            // Optional scheduling hint: skip until deadline.
            if (ev.deadline_ns != 0 && ev.deadline_ns > now_ns) {
                // push back (best-effort) and stop draining this cycle
                q_.try_push(ev);
                break;
            }
            if (propagate_enabled_) propagate_one_nolock(ev);
            processed++;

            if (processed >= max_events) break;

            // budget check (coarse): sample every 8 events
            if ((processed & 7u) == 0u && coarse_now_ns_ != &default_now_ns) {
                now_ns = coarse_now_ns_();
                if (now_ns >= end_ns) break;
            }
        }
        return processed;
    }

    // Caller can provide a fast coarse clock to avoid QueryPerformanceCounter costs.
    void set_coarse_now_ns(uint64_t (*fn)() noexcept) noexcept { coarse_now_ns_ = fn; }

private:
    void propagate_one_nolock(const SpikeEvent& ev) noexcept {
        if (!graph_) return;
        if (ev.hop >= kMaxHop) return;

        const int nc = graph_->node_count.load(std::memory_order_acquire);
        const int src = (int)ev.src_cid;
        if (src < 0 || src >= nc) return;

        const ClusterNode& nd = graph_->node(src);
        const int ec = nd.edge_count.load(std::memory_order_acquire);

        // Deterministic, bounded propagation: only forward edges, capped by MAX_FANOUT.
        for (int i = 0; i < ec; ++i) {
            const int tgt = nd.edges[i].target;
            const float w = nd.edges[i].weight;
            if (tgt < 0 || tgt >= nc) continue;

            // Conservative scaling: keep legacy activation stable while enabling async spikes.
            const float dv = ev.voltage * w * 0.10f;
            if (dv <= 0.0f) continue;
            graph_->add_voltage_to(tgt, dv);
        }
    }

    // Default coarse clock: constant 0 (budget checks become no-op).
    static uint64_t default_now_ns() noexcept { return 0; }

    ClusterGraph* graph_{nullptr};
    MpmcQueue q_;
    uint64_t (*coarse_now_ns_)() noexcept = &default_now_ns;
    bool propagate_enabled_{true};
};

} // namespace fpsan

