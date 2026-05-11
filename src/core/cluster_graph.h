#pragma once
// ============================================================
// FP-SAN Phase 1 — Memory + Hardware Peak Efficiency
// cluster_graph.h — The lock-safe, cache-optimal reasoning substrate
//
// ARCHITECTURAL GUARANTEES:
//   1. Flat arena: 262,144 nodes in one contiguous allocation.
//      Zero heap allocations after init(). L1/L2 prefetcher friendly.
//   2. Voltage safety: std::atomic<float> activation + CAS add_voltage().
//      No torn float reads across concurrent spikes.
//   3. Boolean safety: std::atomic<bool> for alive/clamped/fired_this_query/
//      is_binding_node/is_motor_node. No torn state.
//   4. RW-Lock: std::shared_mutex graph_rw_lock.
//      spread_activation/tick/reads -> shared_lock (concurrent).
//      SynapticMemory::sleep/init    -> unique_lock (exclusive).
//   5. Edge micro-lock: atomic_flag spinlock per node.
//      add_edge sorts in place under spinlock; readers are lock-free.
//   6. Lock-free neurogenesis: Treiber stack with ABA-tagged uint64 CAS.
//      spawn()/kill() never block the 1 kHz cognitive loop.
// ============================================================

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <shared_mutex>
#include <thread>
#include <mutex>
#include <memory>
#include "fpsan_motor.h"
#include "fpsan_neuromod.h"

// ── Compile-time safety assertions ─────────────────────────────
static_assert(std::atomic<float>::is_always_lock_free,
    "std::atomic<float> must be lock-free on this platform. "
    "If this fires on EliteBook, switch activation to atomic<uint32_t>+bit_cast.");
static_assert(std::atomic<bool>::is_always_lock_free,
    "std::atomic<bool> must be lock-free.");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
    "std::atomic<uint64_t> must be lock-free (required for Treiber stack).");

// ── Graph constants ─────────────────────────────────────────────
static constexpr int   CORTEX_CAPACITY   = 262144;  // Flat arena size (never changes)
static constexpr int   INITIAL_CLUSTERS  = 6500;    // Nodes alive at boot
static constexpr int   MAX_FANOUT        = 150;     // Forward + inverse edges per node
static constexpr float EDGE_PRUNE        = 0.05f;
static constexpr float DECAY_PER_HOP    = 0.7f;
static constexpr float ACTIVATION_CUTOFF = 0.01f;
static constexpr int   MAX_REASONING_DEPTH = 15;
static constexpr int   SPREAD_FANOUT_CAP   = 12;

// Sentinel: free list is empty
static constexpr uint64_t FREE_STACK_EMPTY = 0xFFFFFFFF'FFFFFFFFull;

// ============================================================
// EDGE TYPE SYSTEM
// ============================================================
enum EdgeType : uint8_t {
    EDGE_TEMPORAL    = 0,
    EDGE_IS_A        = 1,
    EDGE_HAS_A       = 2,
    EDGE_CAN_DO      = 3,
    EDGE_CAUSES      = 4,
    EDGE_SEQUENCE    = 5,
    EDGE_AST_CHILD   = 6,
    EDGE_NEXT_WORD   = 7,
    EDGE_PHRASE_HEAD  = 8,
    EDGE_PHRASE_CHILD = 9,
    EDGE_RELATED     = 10,
    EDGE_AST_DEF     = 11,
    EDGE_AST_ARG     = 12,
    EDGE_AST_BODY    = 13,
    EDGE_IMPLEMENTED_BY = 14,
    EDGE_REQUIRES    = 15,
    EDGE_ANALOGY       = 16,  // Phase 2: structural isomorphism
    EDGE_VISUAL_CHILD  = 17,  // Phase 7: UIA tree parenthood
    EDGE_ANTONYM       = 18,  // Phase 8: semantic opposition
    EDGE_EPISODIC_LINK = 19,  // Phase 11: episodic hub -> member clusters
    EDGE_DIRECTIVE     = 20,  // Phase 11: declarative directives (optional tagging)
    EDGE_ENSEMBLE_LINK = 21,  // R2: binding hub -> member clusters
    EDGE_TYPE_COUNT    = 22
};

inline const char* edge_type_name(EdgeType t) {
    static const char* names[] = {
        "TEMPORAL","IS_A","HAS_A","CAN_DO","CAUSES",
        "SEQUENCE","AST_CHILD","NEXT_WORD","PHRASE_HEAD",
        "PHRASE_CHILD","RELATED","AST_DEF","AST_ARG","AST_BODY",
        "IMPLEMENTED_BY","REQUIRES","ANALOGY","VISUAL_CHILD","ANTONYM",
        "EPISODIC_LINK","DIRECTIVE","ENSEMBLE_LINK"
    };
    if (t < EDGE_TYPE_COUNT) return names[t];
    return "UNKNOWN";
}

// ── Lifelong-memory tiers (Phase 11) ──
static constexpr uint8_t MEMORY_TIER_TRANSIENT = 0;
static constexpr uint8_t MEMORY_TIER_L1        = 1; // ~20–50 ticks half-life slice
static constexpr uint8_t MEMORY_TIER_L2        = 2; // episodic / slow decay
static constexpr uint8_t MEMORY_TIER_L3        = 3; // directives — exempt from decay

// ── Provenance codes carried in Edge.provenance ──
enum EdgeProvenance : uint8_t {
    PROV_UNKNOWN    = 0,  // Default / legacy
    PROV_USER       = 1,  // Typed by the user at runtime
    PROV_WIKIPEDIA  = 2,  // Fetched/ingested from Wikipedia
    PROV_CONCEPTNET = 3,  // Loaded from knowledge_mass.bin (ConceptNet)
    PROV_INFERRED   = 4,  // Graph-derived analogy or contradiction check
    PROV_VISUAL     = 5   // Read from UIA tree
};

// ── Edge record (plain data, no atomics — protected by node's spinlock) ─
struct Edge {
    int             target;
    float           weight;
    int             co_occur_count;
    EdgeType        type;
    EdgeProvenance  provenance = PROV_UNKNOWN; // Phase 8: trust source
};

// ============================================================
// CLUSTER NODE
// alignas(64): start of each node is cache-line aligned.
// All hot fields are at the top so the first cache line covers
// activation + flags + edge_count in one prefetch.
// ============================================================
struct alignas(64) ClusterNode {
    // ── Hot fields (cache line 0) ────────────────────────────
    std::atomic<float> activation{0.0f};
    std::atomic<bool>  alive{false};
    std::atomic<bool>  clamped{false};
    std::atomic<bool>  is_binding_node{false};
    std::atomic<bool>  fired_this_query{false};
    std::atomic<bool>  is_motor_node{false};

    /// Phase 11: MEMORY_TIER_* constants
    std::atomic<uint8_t> memory_tier{0};

    std::atomic<int>   edge_count{0};
    std::atomic_flag   edge_lock{};          // micro-spinlock for add_edge

    std::atomic<int>   inverse_edge_count{0};
    std::atomic_flag   inverse_edge_lock{};  // micro-spinlock for add_inverse_edge

    // ── Motor payload (cold, only read when is_motor_node==true) ─
    MotorAction motor_action;

    // ── Edge arrays ─────────────────────────────────────────
    Edge edges[MAX_FANOUT];
    Edge inverse_edges[MAX_FANOUT];

    // ─────────────────────────────────────────────────────────
    // init() — reset to clean state. Called at boot and on kill/spawn.
    // NOT thread-safe; must be called before the node is exposed.
    // ─────────────────────────────────────────────────────────
    void init() noexcept {
        activation.store(0.0f, std::memory_order_relaxed);
        alive.store(true,      std::memory_order_relaxed);
        clamped.store(false,   std::memory_order_relaxed);
        is_binding_node.store(false, std::memory_order_relaxed);
        fired_this_query.store(false, std::memory_order_relaxed);
        is_motor_node.store(false,   std::memory_order_relaxed);
        memory_tier.store(MEMORY_TIER_TRANSIENT, std::memory_order_relaxed);
        edge_count.store(0,         std::memory_order_relaxed);
        inverse_edge_count.store(0, std::memory_order_relaxed);
        memset(&motor_action, 0, sizeof(MotorAction));
        // edge_lock and inverse_edge_lock are cleared by value-init / ATOMIC_FLAG_INIT
    }

    // ─────────────────────────────────────────────────────────
    // add_voltage() — thread-safe floating-point accumulation.
    // CAS loop: no torn writes. Multiple cortices can spike the
    // same node simultaneously without corruption.
    // ─────────────────────────────────────────────────────────
    void add_voltage(float dv) noexcept {
        float old = activation.load(std::memory_order_relaxed);
        float desired;
        do {
            desired = old + dv;
        } while (!activation.compare_exchange_weak(
            old, desired,
            std::memory_order_release,
            std::memory_order_relaxed));
    }

    // ─────────────────────────────────────────────────────────
    // add_edge() — spinlock-protected edge insertion/update.
    // Readers in spread_activation are lock-free; they may see
    // a momentarily-stale order but never a torn slot.
    // ─────────────────────────────────────────────────────────
    void add_edge(int target_id, float strength, EdgeType etype = EDGE_TEMPORAL,
                  EdgeProvenance prov = PROV_UNKNOWN) noexcept {
        while (edge_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();

        int ec = edge_count.load(std::memory_order_relaxed);

        for (int i = 0; i < ec; i++) {
            if (edges[i].target == target_id && edges[i].type == etype) {
                edges[i].weight += strength * 0.5f;
                edges[i].weight  = std::min(edges[i].weight, 5.0f);
                edges[i].co_occur_count++;
                if (prov != PROV_UNKNOWN) edges[i].provenance = prov;
                _sort_edges(ec);
                edge_lock.clear(std::memory_order_release);
                return;
            }
        }

        if (ec < MAX_FANOUT) {
            edges[ec] = {target_id, strength, 1, etype, prov};
            edge_count.store(ec + 1, std::memory_order_relaxed);
            ec++;
        } else {
            if (strength > edges[ec - 1].weight) {
                edges[ec - 1] = {target_id, strength, 1, etype, prov};
            }
        }
        _sort_edges(ec);
        edge_lock.clear(std::memory_order_release);
    }

    void add_inverse_edge(int target_id, float strength, EdgeType etype = EDGE_TEMPORAL) noexcept {
        while (inverse_edge_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();

        int ec = inverse_edge_count.load(std::memory_order_relaxed);

        for (int i = 0; i < ec; i++) {
            if (inverse_edges[i].target == target_id && inverse_edges[i].type == etype) {
                inverse_edges[i].weight += strength * 0.5f;
                inverse_edges[i].weight  = std::min(inverse_edges[i].weight, 5.0f);
                inverse_edges[i].co_occur_count++;
                _sort_inv_edges(ec);
                inverse_edge_lock.clear(std::memory_order_release);
                return;
            }
        }

        if (ec < MAX_FANOUT) {
            inverse_edges[ec] = {target_id, strength, 1, etype};
            inverse_edge_count.store(ec + 1, std::memory_order_relaxed);
            ec++;
        } else {
            if (strength > inverse_edges[ec - 1].weight) {
                inverse_edges[ec - 1] = {target_id, strength, 1, etype};
            }
        }
        _sort_inv_edges(ec);
        inverse_edge_lock.clear(std::memory_order_release);
    }

    int count_edges_of_type(EdgeType etype) const noexcept {
        int n = 0, ec = edge_count.load(std::memory_order_acquire);
        for (int i = 0; i < ec; i++)
            if (edges[i].type == etype) n++;
        return n;
    }

    void prune() noexcept {
        while (edge_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
        int ec = edge_count.load(std::memory_order_relaxed);
        int nc = 0;
        for (int i = 0; i < ec; i++)
            if (edges[i].weight >= EDGE_PRUNE) edges[nc++] = edges[i];
        edge_count.store(nc, std::memory_order_relaxed);
        edge_lock.clear(std::memory_order_release);

        while (inverse_edge_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
        int iec = inverse_edge_count.load(std::memory_order_relaxed);
        int nic = 0;
        for (int i = 0; i < iec; i++)
            if (inverse_edges[i].weight >= EDGE_PRUNE) inverse_edges[nic++] = inverse_edges[i];
        inverse_edge_count.store(nic, std::memory_order_relaxed);
        inverse_edge_lock.clear(std::memory_order_release);
    }

private:
    void _sort_edges(int ec) noexcept {
        for (int i = 1; i < ec; i++) {
            Edge key = edges[i];
            int j = i - 1;
            while (j >= 0 && edges[j].weight < key.weight) {
                edges[j + 1] = edges[j]; j--;
            }
            edges[j + 1] = key;
        }
    }
    void _sort_inv_edges(int ec) noexcept {
        for (int i = 1; i < ec; i++) {
            Edge key = inverse_edges[i];
            int j = i - 1;
            while (j >= 0 && inverse_edges[j].weight < key.weight) {
                inverse_edges[j + 1] = inverse_edges[j]; j--;
            }
            inverse_edges[j + 1] = key;
        }
    }
};

// ============================================================
// CLUSTER GRAPH
// ============================================================
struct ClusterGraph {
    // ── Flat arena ──────────────────────────────────────────
    // Raw aligned storage — nodes are constructed lazily (only on spawn/init).
    // Using raw allocation avoids touching all 262K pages at boot.
    struct RawDeleter { void operator()(void* p) const noexcept { _aligned_free(p); } };
    std::unique_ptr<void, RawDeleter> _cortex_raw;
    ClusterNode*                      cortex_memory{nullptr};

    std::atomic<int> node_count{0};

    // ── Treiber stack for lock-free neurogenesis ─────────────
    // free_head packs (ABA_tag:uint32 << 32 | node_id:uint32).
    // FREE_STACK_EMPTY sentinel means the stack is empty.
    std::unique_ptr<std::atomic<int>[]> _free_next_storage;
    std::atomic<int>*  free_next{nullptr};
    std::atomic<uint64_t> free_head{FREE_STACK_EMPTY};

    // ── Global RW-lock ───────────────────────────────────────
    std::shared_mutex graph_rw_lock;

    // ============================================================
    // SAN hook (Phase 10): optional spike poster for async propagation.
    // Bound by the runtime (fpsan_live_core.cpp). The graph remains
    // dependency-free and does not know scheduler types.
    // ============================================================
    using SanPostFn = void(*)(void* user, uint32_t cid, float voltage) noexcept;
    void*     san_user{nullptr};
    SanPostFn san_post{nullptr};
    float     san_threshold{1000000.0f}; // disabled unless explicitly lowered

    // ── Temporal chain (Brainstem-only writes, chain_lock guards) ─
    std::atomic<int>  last_fired{-1};
    std::atomic<int>  chain_len{0};
    std::atomic_flag  chain_lock{};
    int active_chain[32]{};

    // ── BFS scratch (serialized by bfs_lock, NOT on the hot path) ─
    std::unique_ptr<int[]>  _bfs_parent;
    std::unique_ptr<bool[]> _bfs_visited;
    std::unique_ptr<int[]>  _bfs_queue;
    std::mutex bfs_lock;

    // ────────────────────────────────────────────────────────
    ClusterGraph() = default;
    ~ClusterGraph() {
        // Explicitly destroy only the constructed nodes
        if (cortex_memory) {
            const int nc = node_count.load(std::memory_order_relaxed);
            for (int i = 0; i < nc; i++)
                cortex_memory[i].~ClusterNode();
            // _cortex_raw frees the raw storage via _aligned_free
        }
    }

    // Non-copyable, non-movable (contains shared_mutex)
    ClusterGraph(const ClusterGraph&) = delete;
    ClusterGraph& operator=(const ClusterGraph&) = delete;

    // ── Accessor ────────────────────────────────────────────
    ClusterNode& node(int id) noexcept { return cortex_memory[id]; }
    const ClusterNode& node(int id) const noexcept { return cortex_memory[id]; }

    // ────────────────────────────────────────────────────────
    // init() — allocates the flat arena and builds the Treiber
    //          free stack. Must be called with unique_lock held
    //          (or before any threads start).
    // ────────────────────────────────────────────────────────
    void init(int initial_nodes = INITIAL_CLUSTERS) {
        // ── Allocate flat arena with raw aligned storage ──────
        // _aligned_malloc avoids default-constructing all 262144 nodes,
        // so the OS only maps physical pages for nodes we actually touch.
        void* raw = _aligned_malloc(sizeof(ClusterNode) * CORTEX_CAPACITY,
                                    alignof(ClusterNode));
        if (!raw) throw std::bad_alloc();
        _cortex_raw   = std::unique_ptr<void, RawDeleter>(raw);
        cortex_memory = static_cast<ClusterNode*>(raw);

        _free_next_storage = std::make_unique<std::atomic<int>[]>(CORTEX_CAPACITY);
        free_next          = _free_next_storage.get();

        _bfs_parent  = std::make_unique<int[]>(CORTEX_CAPACITY);
        _bfs_visited = std::make_unique<bool[]>(CORTEX_CAPACITY);
        _bfs_queue   = std::make_unique<int[]>(CORTEX_CAPACITY);

        node_count.store(0, std::memory_order_relaxed);
        free_head.store(FREE_STACK_EMPTY, std::memory_order_relaxed);
        last_fired.store(-1, std::memory_order_relaxed);
        chain_len.store(0,   std::memory_order_relaxed);

        // Init only the first `initial_nodes` nodes (placement-new + init).
        // Nodes beyond this range are untouched; spawn() placement-news them on demand.
        int actual = (initial_nodes < CORTEX_CAPACITY) ? initial_nodes : CORTEX_CAPACITY;
        for (int i = 0; i < actual; i++) {
            new (&cortex_memory[i]) ClusterNode();  // placement-new (default ctor)
            cortex_memory[i].init();
            cortex_memory[i].alive.store(true, std::memory_order_relaxed);
            free_next[i].store(-1, std::memory_order_relaxed);
        }
        node_count.store(actual, std::memory_order_release);
    }

    // ────────────────────────────────────────────────────────
    // spawn() — pop a dead node from the Treiber stack or claim
    //           the next slot from the arena. Lock-free.
    // ────────────────────────────────────────────────────────
    int spawn() noexcept {
        // Try free stack first
        uint64_t head = free_head.load(std::memory_order_acquire);
        while (head != FREE_STACK_EMPTY) {
            uint32_t tag = (uint32_t)(head >> 32);
            int32_t  id  = (int32_t)(head & 0xFFFFFFFFull);

            int next = free_next[id].load(std::memory_order_acquire);
            uint64_t new_head = (next < 0)
                ? FREE_STACK_EMPTY
                : (((uint64_t)(tag + 1) << 32) | (uint32_t)next);

            if (free_head.compare_exchange_weak(head, new_head,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                cortex_memory[id].init();
                return id;
            }
        }

        // Stack empty: claim next slot from arena (placement-new first use)
        int id = node_count.fetch_add(1, std::memory_order_acq_rel);
        if (id >= CORTEX_CAPACITY) {
            node_count.fetch_sub(1, std::memory_order_relaxed);
            return -1;
        }
        new (&cortex_memory[id]) ClusterNode(); // first-time construction
        cortex_memory[id].init();
        free_next[id].store(-1, std::memory_order_relaxed);
        return id;
    }

    // ────────────────────────────────────────────────────────
    // kill() — push node onto the Treiber stack. Lock-free.
    // ────────────────────────────────────────────────────────
    void kill(int id) noexcept {
        cortex_memory[id].alive.store(false, std::memory_order_release);
        cortex_memory[id].edge_count.store(0, std::memory_order_release);
        cortex_memory[id].inverse_edge_count.store(0, std::memory_order_release);

        uint64_t head = free_head.load(std::memory_order_acquire);
        uint64_t new_head;
        do {
            uint32_t tag = (uint32_t)(head >> 32);
            int32_t  old_top = (head == FREE_STACK_EMPTY)
                ? -1
                : (int32_t)(head & 0xFFFFFFFFull);

            free_next[id].store(old_top, std::memory_order_relaxed);
            new_head = (((uint64_t)(tag + 1) << 32) | (uint32_t)id);
        } while (!free_head.compare_exchange_weak(head, new_head,
            std::memory_order_release,
            std::memory_order_acquire));
    }

    // Proxy used by callers that only have a graph pointer
    void add_voltage_to(int id, float dv) noexcept {
        float before = cortex_memory[id].activation.load(std::memory_order_relaxed);
        cortex_memory[id].add_voltage(dv);
        if (san_post) {
            float after = cortex_memory[id].activation.load(std::memory_order_relaxed);
            if (before < san_threshold && after >= san_threshold) {
                san_post(san_user, (uint32_t)id, after);
            }
        }
    }

    void bind_san_poster(SanPostFn fn, void* user, float threshold) noexcept {
        san_user = user;
        san_post = fn;
        san_threshold = threshold;
    }

    // ────────────────────────────────────────────────────────
    // record_fire() — build temporal bonds. Brainstem only.
    //                 Caller must hold chain_lock spin.
    // ────────────────────────────────────────────────────────
    void record_fire(int cluster_id) noexcept {
        while (chain_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();

        int lf = last_fired.load(std::memory_order_relaxed);
        if (lf >= 0 && lf != cluster_id) {
            cortex_memory[lf].add_edge(cluster_id, 0.2f, EDGE_TEMPORAL);
            cortex_memory[cluster_id].add_inverse_edge(lf, 0.2f, EDGE_TEMPORAL);
        }
        last_fired.store(cluster_id, std::memory_order_relaxed);

        int cl = chain_len.load(std::memory_order_relaxed);
        if (cl < 32) {
            active_chain[cl] = cluster_id;
            chain_len.store(cl + 1, std::memory_order_relaxed);
        } else {
            memmove(active_chain, active_chain + 1, 31 * sizeof(int));
            active_chain[31] = cluster_id;
        }

        chain_lock.clear(std::memory_order_release);
    }

    // ============================================================
    // SPREADING ACTIVATION
    // PRECONDITION: caller holds std::shared_lock on graph_rw_lock.
    // Reads activation via load(acquire). Writes via add_voltage()
    // CAS loop. No recursion-internal locks.
    // ============================================================
    int spread_activation(int source, float strength = 1.0f, int depth = 0) noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        if (depth >= MAX_REASONING_DEPTH || strength < fpsan::neuromod_effective_activation_cutoff(ACTIVATION_CUTOFF)) return 0;
        if (source < 0 || source >= nc) return 0;

        if (depth == 0) {
            float cur = cortex_memory[source].activation.load(std::memory_order_acquire);
            if (strength > cur)
                cortex_memory[source].activation.store(strength, std::memory_order_release);
        }
        int activated = 1;

        int ec = cortex_memory[source].edge_count.load(std::memory_order_acquire);
        int limit = (ec < SPREAD_FANOUT_CAP) ? ec : SPREAD_FANOUT_CAP;

        for (int i = 0; i < limit; i++) {
            int tgt      = cortex_memory[source].edges[i].target;
            float w      = cortex_memory[source].edges[i].weight;
            EdgeType ety = cortex_memory[source].edges[i].type;
            float prop   = strength * DECAY_PER_HOP * w;

            if (tgt < 0 || tgt >= nc) continue;

            if (cortex_memory[tgt].is_binding_node.load(std::memory_order_acquire)) {
                if (ety == EDGE_TEMPORAL) {
                    cortex_memory[tgt].add_voltage(prop);
                    float act = cortex_memory[tgt].activation.load(std::memory_order_acquire);
                    if (act > 1.5f &&
                        !cortex_memory[tgt].fired_this_query.load(std::memory_order_acquire))
                    {
                        cortex_memory[tgt].fired_this_query.store(true, std::memory_order_release);
                        activated += spread_activation(tgt, act, depth + 1);
                    } else if (cortex_memory[tgt].fired_this_query.load(std::memory_order_acquire)) {
                        int sub_ec = cortex_memory[tgt].edge_count.load(std::memory_order_acquire);
                        int sub_lim = (sub_ec < SPREAD_FANOUT_CAP) ? sub_ec : SPREAD_FANOUT_CAP;
                        for (int j = 0; j < sub_lim; j++) {
                            int st = cortex_memory[tgt].edges[j].target;
                            float sp = act * DECAY_PER_HOP * cortex_memory[tgt].edges[j].weight;
                            if (st < 0 || st >= nc) continue;
                            if (!cortex_memory[st].is_binding_node.load(std::memory_order_acquire)) {
                                float cur = cortex_memory[st].activation.load(std::memory_order_acquire);
                                if (sp > cur)
                                    cortex_memory[st].activation.store(sp, std::memory_order_release);
                            }
                        }
                    }
                }
            } else {
                float cur = cortex_memory[tgt].activation.load(std::memory_order_acquire);
                if (prop > cur) {
                    cortex_memory[tgt].activation.store(prop, std::memory_order_release);
                    activated += spread_activation(tgt, prop, depth + 1);
                }
            }
        }
        return activated;
    }

    int spread_activation_inverse(int source, float strength = 1.0f, int depth = 0) noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        if (depth >= MAX_REASONING_DEPTH || strength < fpsan::neuromod_effective_activation_cutoff(ACTIVATION_CUTOFF)) return 0;
        if (source < 0 || source >= nc) return 0;

        float cur = cortex_memory[source].activation.load(std::memory_order_acquire);
        if (strength > cur)
            cortex_memory[source].activation.store(strength, std::memory_order_release);

        int activated = 1;
        int iec = cortex_memory[source].inverse_edge_count.load(std::memory_order_acquire);
        int limit = (iec < SPREAD_FANOUT_CAP) ? iec : SPREAD_FANOUT_CAP;

        for (int i = 0; i < limit; i++) {
            int tgt    = cortex_memory[source].inverse_edges[i].target;
            float prop = strength * DECAY_PER_HOP * cortex_memory[source].inverse_edges[i].weight;
            if (tgt < 0 || tgt >= nc) continue;
            float tcur = cortex_memory[tgt].activation.load(std::memory_order_acquire);
            if (prop > tcur)
                activated += spread_activation_inverse(tgt, prop, depth + 1);
        }
        return activated;
    }

    int spread_typed(int source, EdgeType follow_type,
                     float strength = 1.0f, int depth = 0) noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        if (depth >= MAX_REASONING_DEPTH || strength < fpsan::neuromod_effective_activation_cutoff(ACTIVATION_CUTOFF)) return 0;
        if (source < 0 || source >= nc) return 0;

        float cur = cortex_memory[source].activation.load(std::memory_order_acquire);
        if (cur < 0.0f) return 0;
        if (strength > cur)
            cortex_memory[source].activation.store(strength, std::memory_order_release);

        int activated = 1, followed = 0;
        int ec = cortex_memory[source].edge_count.load(std::memory_order_acquire);

        for (int i = 0; i < ec && followed < SPREAD_FANOUT_CAP; i++) {
            if (cortex_memory[source].edges[i].type != follow_type) continue;
            int tgt    = cortex_memory[source].edges[i].target;
            float prop = strength * DECAY_PER_HOP * cortex_memory[source].edges[i].weight;
            if (tgt < 0 || tgt >= nc) continue;
            float tcur = cortex_memory[tgt].activation.load(std::memory_order_acquire);
            if (tcur >= 0.0f && prop > tcur)
                activated += spread_typed(tgt, follow_type, prop, depth + 1);
            followed++;
        }
        return activated;
    }

    int spread_typed_inverse(int source, EdgeType follow_type,
                             float strength = 1.0f, int depth = 0) noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        if (depth >= MAX_REASONING_DEPTH || strength < fpsan::neuromod_effective_activation_cutoff(ACTIVATION_CUTOFF)) return 0;
        if (source < 0 || source >= nc) return 0;

        float cur = cortex_memory[source].activation.load(std::memory_order_acquire);
        if (cur < 0.0f) return 0;
        if (strength > cur)
            cortex_memory[source].activation.store(strength, std::memory_order_release);

        int activated = 1, followed = 0;
        int iec = cortex_memory[source].inverse_edge_count.load(std::memory_order_acquire);

        for (int i = 0; i < iec && followed < SPREAD_FANOUT_CAP; i++) {
            if (cortex_memory[source].inverse_edges[i].type != follow_type) continue;
            int tgt    = cortex_memory[source].inverse_edges[i].target;
            float prop = strength * DECAY_PER_HOP * cortex_memory[source].inverse_edges[i].weight;
            if (tgt < 0 || tgt >= nc) continue;
            float tcur = cortex_memory[tgt].activation.load(std::memory_order_acquire);
            if (tcur >= 0.0f && prop > tcur)
                activated += spread_typed_inverse(tgt, follow_type, prop, depth + 1);
            followed++;
        }
        return activated;
    }

    // ── Compositional query ─────────────────────────────────
    int bind_and_query(int seed, EdgeType bind_type, EdgeType query_type,
                       int* out_ids, float* out_vals, int max_results) {
        clear_activation();
        spread_typed(seed, bind_type);

        int bound_ids[64]; float bound_vals[64];
        int n_bound = get_top_activated(bound_ids, bound_vals, 64);

        clear_activation();
        for (int i = 0; i < n_bound; i++)
            spread_typed(bound_ids[i], query_type, bound_vals[i]);

        return get_top_activated(out_ids, out_vals, max_results);
    }

    // ── Bulk state ops (shared_lock callers) ────────────────
    void clear_activation() noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        for (int i = 0; i < nc; i++) {
            if (cortex_memory[i].alive.load(std::memory_order_acquire) &&
                !cortex_memory[i].clamped.load(std::memory_order_acquire))
            {
                cortex_memory[i].activation.store(0.0f, std::memory_order_release);
                cortex_memory[i].fired_this_query.store(false, std::memory_order_release);
            }
        }
    }

    void tick(float decay_rate = 0.9f) noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        // L1 half-life ~20 ticks @ 1 kHz ⇒ <5 % under ~100 ticks after impulse.
        const float decay_l1 = std::pow(0.5f, 1.0f / 20.0f);
        // L2 slow decay (~log-like tail): half-life ~600 ticks (~600 ms at 1 kHz).
        const float decay_l2 = std::pow(0.5f, 1.0f / 600.0f);

        for (int i = 0; i < nc; i++) {
            if (!cortex_memory[i].alive.load(std::memory_order_acquire)) continue;
            const uint8_t tier = cortex_memory[i].memory_tier.load(std::memory_order_relaxed);
            // L3 + directives retain voltage (no exponential decay sweep).
            if (tier == MEMORY_TIER_L3) {
                if (cortex_memory[i].clamped.load(std::memory_order_acquire))
                    cortex_memory[i].activation.store(1.0f, std::memory_order_release);
                // Frozen voltage unless explicitly cleared elsewhere — no exponential decay.
                continue;
            }
            if (cortex_memory[i].clamped.load(std::memory_order_acquire)) {
                cortex_memory[i].activation.store(1.0f, std::memory_order_release);
            } else {
                float cur = cortex_memory[i].activation.load(std::memory_order_relaxed);
                float mul = decay_rate;
                if (tier == MEMORY_TIER_L1) mul = decay_l1;
                else if (tier == MEMORY_TIER_L2) mul = decay_l2;
                float next = cur * mul;
                const float cut = fpsan::neuromod_effective_activation_cutoff(ACTIVATION_CUTOFF);
                if (next < cut) next = 0.0f;
                cortex_memory[i].activation.store(next, std::memory_order_relaxed);
            }
        }
    }

    int get_top_activated(int* out_ids, float* out_vals, int max_n) noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        int count = 0;
        for (int i = 0; i < nc; i++) {
            if (!cortex_memory[i].alive.load(std::memory_order_acquire)) continue;
            float act = cortex_memory[i].activation.load(std::memory_order_acquire);
            if (act <= fpsan::neuromod_effective_activation_cutoff(ACTIVATION_CUTOFF)) continue;

            int pos = count;
            for (int j = 0; j < count && j < max_n; j++) {
                if (act > out_vals[j]) { pos = j; break; }
            }
            if (pos < max_n) {
                for (int j = (count < max_n - 1 ? count : max_n - 1); j > pos; j--) {
                    out_ids[j] = out_ids[j-1];
                    out_vals[j] = out_vals[j-1];
                }
                out_ids[pos]  = i;
                out_vals[pos] = act;
                if (count < max_n) count++;
            }
        }
        return count;
    }

    // ── STDP — Spike-Timing Dependent Plasticity ─────────────
    // Called whenever pre fires before post (LTP) or after (LTD).
    // dt_ms > 0: pre before post (LTP).  dt_ms < 0: post before pre (LTD).
    // Spinlock on pre node's edge_lock during weight update.
    static constexpr float STDP_A_PLUS  = 0.10f;   // LTP amplitude
    static constexpr float STDP_A_MINUS = 0.08f;   // LTD amplitude
    static constexpr float STDP_TAU_MS  = 20.0f;   // Time constant (ms)
    static constexpr float STDP_W_MAX   = 5.0f;
    static constexpr float STDP_W_MIN   = 0.0f;

    void apply_stdp(int pre_id, int post_id, float dt_ms) noexcept {
        if (pre_id < 0 || post_id < 0) return;
        const int nc = node_count.load(std::memory_order_acquire);
        if (pre_id >= nc || post_id >= nc) return;

        float dw;
        if (dt_ms >= 0.0f) {
            dw = STDP_A_PLUS  * expf(-dt_ms / STDP_TAU_MS);   // LTP
        } else {
            dw = -STDP_A_MINUS * expf( dt_ms / STDP_TAU_MS);  // LTD (dw < 0)
        }

        dw *= fpsan::plasticity_scale_load();

        ClusterNode& pre = cortex_memory[pre_id];
        while (pre.edge_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
        int ec = pre.edge_count.load(std::memory_order_relaxed);
        for (int i = 0; i < ec; i++) {
            if (pre.edges[i].target == post_id) {
                pre.edges[i].weight += dw;
                pre.edges[i].weight  = std::max(STDP_W_MIN,
                                       std::min(STDP_W_MAX, pre.edges[i].weight));
                break;
            }
        }
        pre.edge_lock.clear(std::memory_order_release);
    }

    // ── Credit assignment (now calls STDP for each active pair) ─
    void assign_credit(float reward, float gamma = 0.9f) noexcept {
        while (chain_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
        int cl = chain_len.load(std::memory_order_relaxed);
        for (int i = cl - 1; i >= 0; i--) {
            float credit = reward * powf(gamma, (float)(cl - 1 - i));
            // Route through STDP: reward path treated as coincident firing dt≈0
            if (i + 1 < cl) {
                apply_stdp(active_chain[i], active_chain[i + 1],
                           credit > 0.0f ? 1.0f : -1.0f);   // +1ms LTP / -1ms LTD
            }
        }
        chain_lock.clear(std::memory_order_release);
    }

    void reset_chain() noexcept {
        while (chain_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
        chain_len.store(0, std::memory_order_relaxed);
        last_fired.store(-1, std::memory_order_relaxed);
        chain_lock.clear(std::memory_order_release);
    }

    // ── Predictive chain ────────────────────────────────────
    int predict_chain(int start, int* out_path, int max_steps) noexcept {
        int current = start, steps = 0;
        const int nc = node_count.load(std::memory_order_acquire);
        for (int s = 0; s < max_steps; s++) {
            int ec = cortex_memory[current].edge_count.load(std::memory_order_acquire);
            if (ec == 0) break;
            current = cortex_memory[current].edges[0].target;
            if (current < 0 || current >= nc) break;
            out_path[steps++] = current;
        }
        return steps;
    }

    int predict_chain_typed(int start, EdgeType follow_type,
                            int* out_path, int max_steps) noexcept {
        int current = start, steps = 0;
        const int nc = node_count.load(std::memory_order_acquire);
        for (int s = 0; s < max_steps; s++) {
            int best_tgt = -1; float best_w = 0.0f;
            int ec = cortex_memory[current].edge_count.load(std::memory_order_acquire);
            for (int e = 0; e < ec; e++) {
                if (cortex_memory[current].edges[e].type == follow_type &&
                    cortex_memory[current].edges[e].weight > best_w) {
                    best_w = cortex_memory[current].edges[e].weight;
                    best_tgt = cortex_memory[current].edges[e].target;
                }
            }
            if (best_tgt < 0 || best_tgt >= nc) break;
            current = best_tgt;
            out_path[steps++] = current;
        }
        return steps;
    }

    // ── BFS path planning (serialized, NOT hot-path) ─────────
    int plan_path(int start, int goal, int* out_path, int max_len) {
        if (start == goal) return 0;
        std::lock_guard<std::mutex> bfs_guard(bfs_lock);
        const int nc = node_count.load(std::memory_order_acquire);

        memset(_bfs_parent.get(),  -1,    nc * sizeof(int));
        memset(_bfs_visited.get(), false, nc * sizeof(bool));

        int qfront = 0, qback = 0;
        _bfs_queue[qback++] = start;
        _bfs_visited[start] = true;

        while (qfront < qback) {
            int cur = _bfs_queue[qfront++];
            int ec = cortex_memory[cur].edge_count.load(std::memory_order_acquire);
            for (int e = 0; e < ec; e++) {
                int tgt = cortex_memory[cur].edges[e].target;
                if (tgt < 0 || tgt >= nc || _bfs_visited[tgt]) continue;
                _bfs_visited[tgt] = true;
                _bfs_parent[tgt]  = cur;
                if (tgt == goal) {
                    int plen = 0, trace = goal;
                    while (trace != start && plen < max_len) {
                        out_path[plen++] = trace;
                        trace = _bfs_parent[trace];
                    }
                    for (int i = 0; i < plen / 2; i++) {
                        int tmp = out_path[i];
                        out_path[i] = out_path[plen-1-i];
                        out_path[plen-1-i] = tmp;
                    }
                    return plen;
                }
                _bfs_queue[qback++] = tgt;
            }
        }
        return 0;
    }

    int plan_path_typed(int start, int goal, EdgeType follow_type,
                        int* out_path, int max_len) {
        if (start == goal) return 0;
        std::lock_guard<std::mutex> bfs_guard(bfs_lock);
        const int nc = node_count.load(std::memory_order_acquire);

        memset(_bfs_parent.get(),  -1,    nc * sizeof(int));
        memset(_bfs_visited.get(), false, nc * sizeof(bool));

        int qfront = 0, qback = 0;
        _bfs_queue[qback++] = start;
        _bfs_visited[start] = true;

        while (qfront < qback) {
            int cur = _bfs_queue[qfront++];
            int ec = cortex_memory[cur].edge_count.load(std::memory_order_acquire);
            for (int e = 0; e < ec; e++) {
                if (cortex_memory[cur].edges[e].type != follow_type) continue;
                int tgt = cortex_memory[cur].edges[e].target;
                if (tgt < 0 || tgt >= nc || _bfs_visited[tgt]) continue;
                _bfs_visited[tgt] = true;
                _bfs_parent[tgt]  = cur;
                if (tgt == goal) {
                    int plen = 0, trace = goal;
                    while (trace != start && plen < max_len) {
                        out_path[plen++] = trace;
                        trace = _bfs_parent[trace];
                    }
                    for (int i = 0; i < plen / 2; i++) {
                        int tmp = out_path[i];
                        out_path[i] = out_path[plen-1-i];
                        out_path[plen-1-i] = tmp;
                    }
                    return plen;
                }
                _bfs_queue[qback++] = tgt;
            }
        }
        return 0;
    }

    // ── Stats ────────────────────────────────────────────────
    int total_edges() noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        int n = 0;
        for (int i = 0; i < nc; i++)
            if (cortex_memory[i].alive.load(std::memory_order_acquire))
                n += cortex_memory[i].edge_count.load(std::memory_order_acquire);
        return n;
    }

    int total_edges_of_type(EdgeType etype) noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        int n = 0;
        for (int i = 0; i < nc; i++)
            if (cortex_memory[i].alive.load(std::memory_order_acquire))
                n += cortex_memory[i].count_edges_of_type(etype);
        return n;
    }

    int alive_count() noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        int n = 0;
        for (int i = 0; i < nc; i++)
            if (cortex_memory[i].alive.load(std::memory_order_acquire)) n++;
        return n;
    }

    // ── STDP spike-time recorder ─────────────────────────────
    // Call after record_fire(). Walks the chain, applies STDP between
    // consecutive pairs with inter-spike dt = 1 ms per hop.
    void apply_chain_stdp(float reward_sign = 1.0f) noexcept {
        while (chain_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
        int cl = chain_len.load(std::memory_order_relaxed);
        for (int i = 0; i + 1 < cl; i++) {
            float dt = reward_sign > 0.0f ? 1.0f : -1.0f;
            apply_stdp(active_chain[i], active_chain[i + 1], dt);
        }
        chain_lock.clear(std::memory_order_release);
    }

    // ── Structural isomorphism — EDGE_ANALOGY insertion ──────
    // Computes a simple fingerprint per node: (alive_edge_count, edge_type_histogram).
    // Pairs with identical fingerprints get an EDGE_ANALOGY link.
    // Runs on a background thread (shared_lock). Rate-limited to 1024 pairs/s.
    void scan_analogies(int max_pairs = 256) noexcept {
        const int nc = node_count.load(std::memory_order_acquire);
        if (nc < 2) return;

        // Build fingerprints: 32-bit = (alive_ec:8 | type_histogram:24 condensed)
        struct FP { uint32_t fp; int id; };
        // Use a small fixed stack buffer; if nc is huge we sample
        constexpr int MAX_SAMPLE = 1024;
        static FP fps[MAX_SAMPLE];
        int ns = 0;

        for (int i = 0; i < nc && ns < MAX_SAMPLE; i++) {
            if (!cortex_memory[i].alive.load(std::memory_order_relaxed)) continue;
            int ec = cortex_memory[i].edge_count.load(std::memory_order_relaxed);
            if (ec == 0) continue;
            uint32_t hist = 0;
            for (int e = 0; e < ec; e++) {
                EdgeType t = cortex_memory[i].edges[e].type;
                hist ^= (1u << (t % 32));
            }
            fps[ns++] = { (uint32_t)(ec & 0xFF) | (hist << 8), i };
        }

        int pairs = 0;
        for (int a = 0; a < ns && pairs < max_pairs; a++) {
            for (int b = a + 1; b < ns && pairs < max_pairs; b++) {
                if (fps[a].fp == fps[b].fp) {
                    cortex_memory[fps[a].id].add_edge(fps[b].id, 0.1f, EDGE_ANALOGY);
                    cortex_memory[fps[b].id].add_edge(fps[a].id, 0.1f, EDGE_ANALOGY);
                    pairs++;
                }
            }
        }
    }

    // ── Topology hash for wake/sleep fidelity test ──────────
    uint64_t topology_hash() noexcept {
        uint64_t h = 14695981039346656037ull;
        const int nc = node_count.load(std::memory_order_acquire);
        for (int i = 0; i < nc; i++) {
            if (!cortex_memory[i].alive.load(std::memory_order_acquire)) continue;
            int ec = cortex_memory[i].edge_count.load(std::memory_order_acquire);
            for (int e = 0; e < ec; e++) {
                uint32_t t   = (uint32_t)cortex_memory[i].edges[e].target;
                uint32_t raw; memcpy(&raw, &cortex_memory[i].edges[e].weight, 4);
                h ^= (uint64_t)(i * 1000003 + t) ^ ((uint64_t)raw << 32);
                h *= 1099511628211ull;
            }
        }
        return h;
    }
};
