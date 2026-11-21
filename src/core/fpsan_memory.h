#pragma once
// ============================================================
// FP-SAN Phase 1 — Synaptic Serialization (The Sleep Cycle)
// fpsan_memory.h — Lossless v3 binary brain persistence with FNV-1a checksum.
//
// GUARANTEES:
//   sleep() acquires std::unique_lock on graph->graph_rw_lock.
//     All four cognitive cortices stall. Quiescent snapshot.
//   wake()  mirrors unique_lock. Restores all flags, tiers, motor actions,
//     and provenance codes. Rejects corrupted files via FNV-1a payload checksum.
//
// File Format (version 3 — lossless edition):
//   Block 1: FpsanHeader (magic, version=3, node_count, cortex_active_count, alive_node_count)
//   Block 2: CortexRecord[] (active language clusters)
//   Block 3: Per alive node:
//              - NodeRecord (node_id, edge_count, inverse_edge_count, flags, memory_tier, reserved)
//              - [Optional] MotorAction (only if flags & NODE_FLAG_MOTOR)
//              - EdgeRecord[] for forward edges (with provenance)
//              - EdgeRecord[] for inverse edges (with provenance)
//   Block 4: Trailing uint64_t payload_fnv1a checksum
// ============================================================

#include "cluster_graph.h"
#include "fpsan_language.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <shared_mutex>
#include <cstdint>
#include <vector>

// ── Binary format constants ───────────────────────────────────
static constexpr uint32_t FPSAN_MAGIC   = 0x46505341; // "FPSA"
static constexpr uint32_t FPSAN_VERSION = 3;           // v3: lossless with provenance & node flags & checksum

// 64-bit FNV-1a constants and helper
static constexpr uint64_t FNV1A64_OFFSET_BASIS = 14695981039346656037ULL;
static constexpr uint64_t FNV1A64_PRIME        = 1099511628211ULL;

inline uint64_t fnv1a64_update(uint64_t hash, const void* data, size_t len) noexcept {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        hash ^= p[i];
        hash *= FNV1A64_PRIME;
    }
    return hash;
}

// Node flags for serialization
static constexpr uint8_t NODE_FLAG_BINDING = 1 << 0;
static constexpr uint8_t NODE_FLAG_MOTOR   = 1 << 1;
static constexpr uint8_t NODE_FLAG_CLAMPED = 1 << 2;

struct FpsanHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t node_count;
    uint32_t cortex_active_count;
    uint32_t alive_node_count;
};

struct NodeRecord {
    int32_t  node_id;
    int32_t  edge_count;
    int32_t  inverse_edge_count;
    uint8_t  flags;       // NODE_FLAG_*
    uint8_t  memory_tier; // MEMORY_TIER_*
    uint16_t reserved{0};
};

struct EdgeRecord {
    int32_t  target;
    float    weight;
    int32_t  co_occur_count;
    uint8_t  type;
    uint8_t  provenance; // EdgeProvenance
    uint16_t reserved{0};
};

struct CortexRecord {
    int32_t cluster_id;
    int8_t  weights[LANG_WORD_DIM];
    float   accum[LANG_WORD_DIM];
    int32_t sample_count;
    uint8_t frozen;
    char    word_label[64];
};

struct SynapticMemory {
    static bool sleep(const char* filepath,
                      ClusterGraph* graph,
                      LanguageCortex* cortex)
    {
        std::unique_lock<std::shared_mutex> brain_lock(graph->graph_rw_lock);
        std::unique_lock<std::shared_mutex> cortex_lock(cortex->cortex_lock);

        FILE* f = fopen(filepath, "wb");
        if (!f) {
            fprintf(stderr, "[SynapticMemory] ERROR: Cannot open '%s' for writing.\n", filepath);
            return false;
        }

        auto t_start = std::chrono::high_resolution_clock::now();

        const int nc = graph->node_count.load(std::memory_order_relaxed);

        int alive_count = 0;
        for (int i = 0; i < nc; i++)
            if (graph->cortex_memory[i].alive.load(std::memory_order_relaxed)) alive_count++;

        int cortex_active = 0;
        for (int i = 0; i < LANG_CLUSTERS; i++)
            if (cortex->clusters[i].active) cortex_active++;

        // Block 1: Header
        FpsanHeader header;
        header.magic               = FPSAN_MAGIC;
        header.version             = FPSAN_VERSION;
        header.node_count          = (uint32_t)nc;
        header.cortex_active_count = (uint32_t)cortex_active;
        header.alive_node_count    = (uint32_t)alive_count;
        if (fwrite(&header, sizeof(FpsanHeader), 1, f) != 1) {
            fclose(f); return false;
        }

        // Hash header into running_hash as root of trust
        uint64_t running_hash = fnv1a64_update(FNV1A64_OFFSET_BASIS, &header, sizeof(FpsanHeader));

        // Block 2: Language Cortex
        for (int i = 0; i < LANG_CLUSTERS; i++) {
            if (!cortex->clusters[i].active) continue;
            CortexRecord rec;
            rec.cluster_id   = i;
            memcpy(rec.weights, cortex->clusters[i].weights, LANG_WORD_DIM);
            memcpy(rec.accum,   cortex->clusters[i].accum, sizeof(float) * LANG_WORD_DIM);
            rec.sample_count = cortex->clusters[i].sample_count;
            rec.frozen       = cortex->clusters[i].frozen ? 1 : 0;
            memset(rec.word_label, 0, 64);
            strncpy(rec.word_label, cortex->clusters[i].word_label, 63);
            if (fwrite(&rec, sizeof(CortexRecord), 1, f) != 1) {
                fclose(f); return false;
            }
            running_hash = fnv1a64_update(running_hash, &rec, sizeof(CortexRecord));
        }

        // Block 3: Graph topology (lossless with flags, tiers, motor actions, provenance)
        int total_edges_written = 0;
        for (int i = 0; i < nc; i++) {
            const ClusterNode& n = graph->cortex_memory[i];
            if (!n.alive.load(std::memory_order_relaxed)) continue;

            const int ec  = n.edge_count.load(std::memory_order_relaxed);
            const int iec = n.inverse_edge_count.load(std::memory_order_relaxed);

            uint8_t flags = 0;
            if (n.is_binding_node.load(std::memory_order_relaxed)) flags |= NODE_FLAG_BINDING;
            if (n.is_motor_node.load(std::memory_order_relaxed))   flags |= NODE_FLAG_MOTOR;
            if (n.clamped.load(std::memory_order_relaxed))         flags |= NODE_FLAG_CLAMPED;

            NodeRecord nrec;
            nrec.node_id            = i;
            nrec.edge_count         = ec;
            nrec.inverse_edge_count = iec;
            nrec.flags              = flags;
            nrec.memory_tier        = n.memory_tier.load(std::memory_order_relaxed);
            nrec.reserved           = 0;
            if (fwrite(&nrec, sizeof(NodeRecord), 1, f) != 1) {
                fclose(f); return false;
            }
            running_hash = fnv1a64_update(running_hash, &nrec, sizeof(NodeRecord));

            // Optional MotorAction payload
            if (flags & NODE_FLAG_MOTOR) {
                if (fwrite(&n.motor_action, sizeof(MotorAction), 1, f) != 1) {
                    fclose(f); return false;
                }
                running_hash = fnv1a64_update(running_hash, &n.motor_action, sizeof(MotorAction));
            }

            for (int e = 0; e < ec; e++) {
                EdgeRecord erec;
                erec.target         = n.edges[e].target;
                erec.weight         = n.edges[e].weight;
                erec.co_occur_count = n.edges[e].co_occur_count;
                erec.type           = (uint8_t)n.edges[e].type;
                erec.provenance     = (uint8_t)n.edges[e].provenance;
                erec.reserved       = 0;
                if (fwrite(&erec, sizeof(EdgeRecord), 1, f) != 1) {
                    fclose(f); return false;
                }
                running_hash = fnv1a64_update(running_hash, &erec, sizeof(EdgeRecord));
                total_edges_written++;
            }

            for (int e = 0; e < iec; e++) {
                EdgeRecord erec;
                erec.target         = n.inverse_edges[e].target;
                erec.weight         = n.inverse_edges[e].weight;
                erec.co_occur_count = n.inverse_edges[e].co_occur_count;
                erec.type           = (uint8_t)n.inverse_edges[e].type;
                erec.provenance     = (uint8_t)n.inverse_edges[e].provenance;
                erec.reserved       = 0;
                if (fwrite(&erec, sizeof(EdgeRecord), 1, f) != 1) {
                    fclose(f); return false;
                }
                running_hash = fnv1a64_update(running_hash, &erec, sizeof(EdgeRecord));
                total_edges_written++;
            }
        }

        // Block 4: Trailing FNV-1a checksum
        uint64_t payload_fnv1a = running_hash;
        if (fwrite(&payload_fnv1a, sizeof(uint64_t), 1, f) != 1) {
            fclose(f); return false;
        }

        long file_size = ftell(f);
        fclose(f);

        auto t_end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        printf("[SynapticMemory] SLEEP complete (v%u).\n", FPSAN_VERSION);
        printf("  File: %s\n",              filepath);
        printf("  Nodes: %d alive / %d total\n", alive_count, nc);
        printf("  Cortex: %d active clusters\n", cortex_active);
        printf("  Edges: %d total\n",       total_edges_written);
        printf("  Checksum: 0x%016llX\n",   (unsigned long long)payload_fnv1a);
        printf("  Size: %.2f KB\n",         file_size / 1024.0);
        printf("  Time: %.3f ms\n",         ms);
        return true;
    }

    static bool wake(const char* filepath,
                     ClusterGraph* graph,
                     LanguageCortex* cortex)
    {
        std::unique_lock<std::shared_mutex> brain_lock(graph->graph_rw_lock);
        std::unique_lock<std::shared_mutex> cortex_lock(cortex->cortex_lock);

        FILE* f = fopen(filepath, "rb");
        if (!f) {
            fprintf(stderr, "[SynapticMemory] ERROR: Cannot open '%s' for reading.\n", filepath);
            return false;
        }

        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f); return false;
        }
        long file_size = ftell(f);
        if (file_size < (long)(sizeof(FpsanHeader) + sizeof(uint64_t))) {
            fprintf(stderr, "[SynapticMemory] ERROR: File '%s' too small (%ld bytes).\n", filepath, file_size);
            fclose(f); return false;
        }
        if (fseek(f, 0, SEEK_SET) != 0) {
            fclose(f); return false;
        }

        std::vector<uint8_t> buf(static_cast<size_t>(file_size));
        if (fread(buf.data(), 1, static_cast<size_t>(file_size), f) != static_cast<size_t>(file_size)) {
            fprintf(stderr, "[SynapticMemory] ERROR: Incomplete read of '%s'.\n", filepath);
            fclose(f); return false;
        }
        fclose(f);

        auto t_start = std::chrono::high_resolution_clock::now();

        // Check trailing checksum first
        size_t payload_len = static_cast<size_t>(file_size) - sizeof(uint64_t);
        uint64_t expected_hash = 0;
        memcpy(&expected_hash, buf.data() + payload_len, sizeof(uint64_t));

        uint64_t computed_hash = fnv1a64_update(FNV1A64_OFFSET_BASIS, buf.data(), payload_len);
        if (computed_hash != expected_hash) {
            fprintf(stderr, "[SynapticMemory] ERROR: Checksum mismatch in '%s': expected 0x%016llX, got 0x%016llX\n",
                    filepath, (unsigned long long)expected_hash, (unsigned long long)computed_hash);
            return false;
        }

        // Header validation
        FpsanHeader header;
        memcpy(&header, buf.data(), sizeof(FpsanHeader));
        if (header.magic != FPSAN_MAGIC) {
            fprintf(stderr, "[SynapticMemory] ERROR: Bad magic 0x%08X\n", header.magic);
            return false;
        }
        if (header.version != FPSAN_VERSION) {
            fprintf(stderr, "[SynapticMemory] ERROR: Unsupported version (file=%u engine=%u)\n",
                    header.version, FPSAN_VERSION);
            return false;
        }
        if (header.node_count == 0 || header.node_count > (uint32_t)CORTEX_CAPACITY) {
            fprintf(stderr, "[SynapticMemory] ERROR: Illegal node_count %u (max %d)\n",
                    header.node_count, CORTEX_CAPACITY);
            return false;
        }
        if (header.cortex_active_count > (uint32_t)LANG_CLUSTERS) {
            fprintf(stderr, "[SynapticMemory] ERROR: Illegal cortex_active_count %u (max %d)\n",
                    header.cortex_active_count, LANG_CLUSTERS);
            return false;
        }
        if (header.alive_node_count > header.node_count) {
            fprintf(stderr, "[SynapticMemory] ERROR: alive_node_count %u > node_count %u\n",
                    header.alive_node_count, header.node_count);
            return false;
        }

        // Structural and bounds pre-verification pass
        const uint8_t* ptr = buf.data() + sizeof(FpsanHeader);
        const uint8_t* const payload_end = buf.data() + payload_len;

        for (uint32_t i = 0; i < header.cortex_active_count; i++) {
            if (ptr + sizeof(CortexRecord) > payload_end) {
                fprintf(stderr, "[SynapticMemory] ERROR: Truncated cortex record at index %u\n", i);
                return false;
            }
            CortexRecord rec;
            memcpy(&rec, ptr, sizeof(CortexRecord));
            if (rec.cluster_id < 0 || rec.cluster_id >= LANG_CLUSTERS) {
                fprintf(stderr, "[SynapticMemory] ERROR: Bad cortex cluster_id %d\n", rec.cluster_id);
                return false;
            }
            ptr += sizeof(CortexRecord);
        }

        for (uint32_t i = 0; i < header.alive_node_count; i++) {
            if (ptr + sizeof(NodeRecord) > payload_end) {
                fprintf(stderr, "[SynapticMemory] ERROR: Truncated node record at alive index %u\n", i);
                return false;
            }
            NodeRecord nrec;
            memcpy(&nrec, ptr, sizeof(NodeRecord));
            if (nrec.node_id < 0 || nrec.node_id >= (int32_t)header.node_count) {
                fprintf(stderr, "[SynapticMemory] ERROR: Bad node_id %d (node_count=%u)\n",
                        nrec.node_id, header.node_count);
                return false;
            }
            if (nrec.edge_count < 0 || nrec.edge_count > MAX_FANOUT) {
                fprintf(stderr, "[SynapticMemory] ERROR: Bad edge_count %d at node %d (max %d)\n",
                        nrec.edge_count, nrec.node_id, MAX_FANOUT);
                return false;
            }
            if (nrec.inverse_edge_count < 0 || nrec.inverse_edge_count > MAX_FANOUT) {
                fprintf(stderr, "[SynapticMemory] ERROR: Bad inverse_edge_count %d at node %d (max %d)\n",
                        nrec.inverse_edge_count, nrec.node_id, MAX_FANOUT);
                return false;
            }
            ptr += sizeof(NodeRecord);

            if (nrec.flags & NODE_FLAG_MOTOR) {
                if (ptr + sizeof(MotorAction) > payload_end) {
                    fprintf(stderr, "[SynapticMemory] ERROR: Truncated motor action at node %d\n", nrec.node_id);
                    return false;
                }
                ptr += sizeof(MotorAction);
            }

            size_t fwd_bytes = static_cast<size_t>(nrec.edge_count) * sizeof(EdgeRecord);
            if (ptr + fwd_bytes > payload_end) {
                fprintf(stderr, "[SynapticMemory] ERROR: Truncated forward edges at node %d\n", nrec.node_id);
                return false;
            }
            ptr += fwd_bytes;

            size_t inv_bytes = static_cast<size_t>(nrec.inverse_edge_count) * sizeof(EdgeRecord);
            if (ptr + inv_bytes > payload_end) {
                fprintf(stderr, "[SynapticMemory] ERROR: Truncated inverse edges at node %d\n", nrec.node_id);
                return false;
            }
            ptr += inv_bytes;
        }

        if (ptr != payload_end) {
            fprintf(stderr, "[SynapticMemory] ERROR: Payload size mismatch: parsed %zu bytes, expected %zu\n",
                    (size_t)(ptr - buf.data()), payload_len);
            return false;
        }

        // ========================================================
        // Verification succeeded! Now safely instantiate and populate.
        // ========================================================
        graph->init(0);
        cortex->init_nolock();

        graph->arena_alloc.store((int)header.node_count, std::memory_order_relaxed);
        graph->node_count.store((int)header.node_count, std::memory_order_relaxed);

        ptr = buf.data() + sizeof(FpsanHeader);

        // Block 2: Populate Language Cortex
        for (uint32_t i = 0; i < header.cortex_active_count; i++) {
            CortexRecord rec;
            memcpy(&rec, ptr, sizeof(CortexRecord));
            ptr += sizeof(CortexRecord);

            int cid = rec.cluster_id;
            memcpy(cortex->clusters[cid].weights, rec.weights, LANG_WORD_DIM);
            memcpy(cortex->clusters[cid].accum,   rec.accum, sizeof(float) * LANG_WORD_DIM);
            cortex->clusters[cid].sample_count = rec.sample_count;
            cortex->clusters[cid].frozen       = (rec.frozen != 0);
            cortex->clusters[cid].active       = true;
            strncpy(cortex->clusters[cid].word_label, rec.word_label, 63);
            cortex->clusters[cid].word_label[63] = '\0';
        }
        cortex->rebuild_cache_nolock();

        // Block 3: Populate Nodes and Edges
        std::vector<bool> node_is_alive(header.node_count, false);
        int total_edges_read = 0;

        for (uint32_t i = 0; i < header.alive_node_count; i++) {
            NodeRecord nrec;
            memcpy(&nrec, ptr, sizeof(NodeRecord));
            ptr += sizeof(NodeRecord);

            int nid = nrec.node_id;
            node_is_alive[nid] = true;

            ClusterNode& n = graph->cortex_memory[nid];
            new (&n) ClusterNode();
            n.init();
            n.constructed.store(true, std::memory_order_relaxed);
            n.alive.store(true, std::memory_order_relaxed);
            n.activation.store(0.0f, std::memory_order_relaxed);
            n.clamped.store((nrec.flags & NODE_FLAG_CLAMPED) != 0, std::memory_order_relaxed);
            n.is_binding_node.store((nrec.flags & NODE_FLAG_BINDING) != 0, std::memory_order_relaxed);
            n.is_motor_node.store((nrec.flags & NODE_FLAG_MOTOR) != 0, std::memory_order_relaxed);
            n.memory_tier.store(nrec.memory_tier, std::memory_order_relaxed);

            if (nrec.flags & NODE_FLAG_MOTOR) {
                MotorAction act;
                memcpy(&act, ptr, sizeof(MotorAction));
                ptr += sizeof(MotorAction);
                n.motor_action = act;
            }

            n.edge_count.store(nrec.edge_count, std::memory_order_relaxed);
            for (int e = 0; e < nrec.edge_count; e++) {
                EdgeRecord erec;
                memcpy(&erec, ptr, sizeof(EdgeRecord));
                ptr += sizeof(EdgeRecord);

                n.edges[e].target         = erec.target;
                n.edges[e].weight         = erec.weight;
                n.edges[e].co_occur_count = erec.co_occur_count;
                n.edges[e].type           = static_cast<EdgeType>(erec.type);
                n.edges[e].provenance     = static_cast<EdgeProvenance>(erec.provenance);
                total_edges_read++;
            }

            n.inverse_edge_count.store(nrec.inverse_edge_count, std::memory_order_relaxed);
            for (int e = 0; e < nrec.inverse_edge_count; e++) {
                EdgeRecord erec;
                memcpy(&erec, ptr, sizeof(EdgeRecord));
                ptr += sizeof(EdgeRecord);

                n.inverse_edges[e].target         = erec.target;
                n.inverse_edges[e].weight         = erec.weight;
                n.inverse_edges[e].co_occur_count = erec.co_occur_count;
                n.inverse_edges[e].type           = static_cast<EdgeType>(erec.type);
                n.inverse_edges[e].provenance     = static_cast<EdgeProvenance>(erec.provenance);
                total_edges_read++;
            }
        }

        // Reconstruct Treiber stack for all dead nodes in [0, header.node_count)
        for (int id = static_cast<int>(header.node_count) - 1; id >= 0; id--) {
            if (!node_is_alive[id]) {
                ClusterNode& n = graph->cortex_memory[id];
                new (&n) ClusterNode();
                n.init();
                n.constructed.store(true, std::memory_order_relaxed);
                n.alive.store(false, std::memory_order_relaxed);

                uint64_t head = graph->free_head.load(std::memory_order_relaxed);
                uint32_t tag = static_cast<uint32_t>(head >> 32);
                int32_t  old_top = (head == FREE_STACK_EMPTY)
                    ? -1
                    : static_cast<int32_t>(head & 0xFFFFFFFFull);

                graph->free_next[id].store(old_top, std::memory_order_relaxed);
                uint64_t new_head = (((uint64_t)(tag + 1) << 32) | static_cast<uint32_t>(id));
                graph->free_head.store(new_head, std::memory_order_relaxed);
            }
        }

        auto t_end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        printf("[SynapticMemory] WAKE complete (v%u).\n", header.version);
        printf("  File: %s\n",                   filepath);
        printf("  Nodes: %u alive / %u total\n", header.alive_node_count, header.node_count);
        printf("  Cortex: %u active clusters\n", header.cortex_active_count);
        printf("  Edges: %d total\n",             total_edges_read);
        printf("  Checksum: 0x%016llX [VALID]\n", (unsigned long long)expected_hash);
        printf("  Time: %.3f ms\n",               ms);
        printf("  All activations reset to 0.0 (cold boot).\n");
        return true;
    }
};
