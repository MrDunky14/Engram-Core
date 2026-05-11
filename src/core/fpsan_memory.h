#pragma once
// ============================================================
// FP-SAN Phase 1 — Synaptic Serialization (The Sleep Cycle)
// fpsan_memory.h — Thread-safe binary brain persistence.
//
// GUARANTEES:
//   sleep() acquires std::unique_lock on graph->graph_rw_lock.
//     All four cognitive cortices stall. The snapshot is taken
//     from a quiescent state — bit-identical across reruns from
//     the same logical state. No torn writes possible.
//   wake()  mirrors the unique_lock. Iterates cortex_memory
//     flat array directly — no chunk indirection.
//   Voltages are NOT saved (biological sleep clears working
//   memory). Only topology is persisted.
//
// File Format (version 2 — flat-arena edition):
//   Block 1: FpsanHeader
//   Block 2: CortexRecord[] (active language clusters)
//   Block 3: NodeRecord + EdgeRecord[] per alive node
// ============================================================

#include "cluster_graph.h"
#include "fpsan_language.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <shared_mutex>

// ── Binary format constants ───────────────────────────────────
static constexpr uint32_t FPSAN_MAGIC   = 0x46505341; // "FPSA"
static constexpr uint32_t FPSAN_VERSION = 2;           // v2: flat arena

struct FpsanHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t node_count;
    uint32_t cortex_active_count;
    uint32_t alive_node_count;
};

struct NodeRecord {
    int32_t node_id;
    int32_t edge_count;
    int32_t inverse_edge_count;
};

struct EdgeRecord {
    int32_t target;
    float   weight;
    int32_t co_occur_count;
    uint8_t type;
};

struct CortexRecord {
    int32_t cluster_id;
    int8_t  weights[LANG_WORD_DIM];
    float   accum[LANG_WORD_DIM];
    int32_t sample_count;
    uint8_t frozen;
    char    word_label[64];
};

// ============================================================
// SYNAPTIC MEMORY
// ============================================================
struct SynapticMemory {

    // ──────────────────────────────────────────────────────────
    // sleep() — snapshot the brain to a .fpsan file.
    //
    // Opens with std::unique_lock: all cognitive threads stall
    // for the duration of the write. The lock is released when
    // this function returns, resuming all cortices.
    // ──────────────────────────────────────────────────────────
    static bool sleep(const char* filepath,
                      ClusterGraph* graph,
                      LanguageCortex* cortex)
    {
        // Acquire exclusive brain lock — cognitive threads stall here
        std::unique_lock<std::shared_mutex> brain_lock(graph->graph_rw_lock);

        FILE* f = fopen(filepath, "wb");
        if (!f) {
            fprintf(stderr, "[SynapticMemory] ERROR: Cannot open '%s' for writing.\n", filepath);
            return false;
        }

        auto t_start = std::chrono::high_resolution_clock::now();

        const int nc = graph->node_count.load(std::memory_order_relaxed);

        // Count alive nodes
        int alive_count = 0;
        for (int i = 0; i < nc; i++)
            if (graph->cortex_memory[i].alive.load(std::memory_order_relaxed)) alive_count++;

        // Count active cortex clusters
        int cortex_active = 0;
        for (int i = 0; i < LANG_CLUSTERS; i++)
            if (cortex->clusters[i].active) cortex_active++;

        // Block 1: Header
        FpsanHeader header;
        header.magic              = FPSAN_MAGIC;
        header.version            = FPSAN_VERSION;
        header.node_count         = (uint32_t)nc;
        header.cortex_active_count = (uint32_t)cortex_active;
        header.alive_node_count   = (uint32_t)alive_count;
        fwrite(&header, sizeof(FpsanHeader), 1, f);

        // Block 2: Language Cortex
        for (int i = 0; i < LANG_CLUSTERS; i++) {
            if (!cortex->clusters[i].active) continue;
            CortexRecord rec;
            rec.cluster_id   = i;
            memcpy(rec.weights, cortex->clusters[i].weights, LANG_WORD_DIM);
            memcpy(rec.accum,   cortex->clusters[i].accum,
                   sizeof(float) * LANG_WORD_DIM);
            rec.sample_count = cortex->clusters[i].sample_count;
            rec.frozen       = cortex->clusters[i].frozen ? 1 : 0;
            memset(rec.word_label, 0, 64);
            strncpy(rec.word_label, cortex->clusters[i].word_label, 63);
            fwrite(&rec, sizeof(CortexRecord), 1, f);
        }

        // Block 3: Graph topology (flat iteration — no chunk indirection)
        int total_edges_written = 0;
        for (int i = 0; i < nc; i++) {
            const ClusterNode& n = graph->cortex_memory[i];
            if (!n.alive.load(std::memory_order_relaxed)) continue;

            const int ec  = n.edge_count.load(std::memory_order_relaxed);
            const int iec = n.inverse_edge_count.load(std::memory_order_relaxed);

            NodeRecord nrec;
            nrec.node_id            = i;
            nrec.edge_count         = ec;
            nrec.inverse_edge_count = iec;
            fwrite(&nrec, sizeof(NodeRecord), 1, f);

            for (int e = 0; e < ec; e++) {
                EdgeRecord erec;
                erec.target        = n.edges[e].target;
                erec.weight        = n.edges[e].weight;
                erec.co_occur_count = n.edges[e].co_occur_count;
                erec.type          = (uint8_t)n.edges[e].type;
                fwrite(&erec, sizeof(EdgeRecord), 1, f);
                total_edges_written++;
            }

            for (int e = 0; e < iec; e++) {
                EdgeRecord erec;
                erec.target        = n.inverse_edges[e].target;
                erec.weight        = n.inverse_edges[e].weight;
                erec.co_occur_count = n.inverse_edges[e].co_occur_count;
                erec.type          = (uint8_t)n.inverse_edges[e].type;
                fwrite(&erec, sizeof(EdgeRecord), 1, f);
                total_edges_written++;
            }
        }

        long file_size = ftell(f);
        fclose(f);

        // brain_lock releases here, resuming all cognitive threads

        auto t_end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        printf("[SynapticMemory] SLEEP complete.\n");
        printf("  File: %s\n",              filepath);
        printf("  Nodes: %d alive / %d total\n", alive_count, nc);
        printf("  Cortex: %d active clusters\n", cortex_active);
        printf("  Edges: %d total\n",       total_edges_written);
        printf("  Size: %.2f KB\n",         file_size / 1024.0);
        printf("  Time: %.3f ms\n",         ms);
        return true;
    }

    // ──────────────────────────────────────────────────────────
    // wake() — restore topology from a .fpsan file.
    //
    // Opens with std::unique_lock. Must be called before any
    // cognitive threads are started, or after they are paused.
    // Activations all start at 0.0 (cold boot).
    // ──────────────────────────────────────────────────────────
    static bool wake(const char* filepath,
                     ClusterGraph* graph,
                     LanguageCortex* cortex)
    {
        std::unique_lock<std::shared_mutex> brain_lock(graph->graph_rw_lock);

        FILE* f = fopen(filepath, "rb");
        if (!f) {
            fprintf(stderr, "[SynapticMemory] ERROR: Cannot open '%s' for reading.\n", filepath);
            return false;
        }

        auto t_start = std::chrono::high_resolution_clock::now();

        // Block 1: Header
        FpsanHeader header;
        if (fread(&header, sizeof(FpsanHeader), 1, f) != 1) {
            fprintf(stderr, "[SynapticMemory] ERROR: Failed to read header.\n");
            fclose(f); return false;
        }
        if (header.magic != FPSAN_MAGIC) {
            fprintf(stderr, "[SynapticMemory] ERROR: Bad magic 0x%08X\n", header.magic);
            fclose(f); return false;
        }
        if (header.version != FPSAN_VERSION) {
            fprintf(stderr, "[SynapticMemory] WARNING: Version mismatch (file=%u engine=%u)\n",
                header.version, FPSAN_VERSION);
        }

        // Initialise fresh brain
        graph->init((int)header.node_count);
        cortex->init();

        // Block 2: Language Cortex
        for (uint32_t i = 0; i < header.cortex_active_count; i++) {
            CortexRecord rec;
            if (fread(&rec, sizeof(CortexRecord), 1, f) != 1) {
                fprintf(stderr, "[SynapticMemory] ERROR: Truncated cortex at %u\n", i);
                fclose(f); return false;
            }
            if (rec.cluster_id < 0 || rec.cluster_id >= LANG_CLUSTERS) continue;
            memcpy(cortex->clusters[rec.cluster_id].weights, rec.weights, LANG_WORD_DIM);
            memcpy(cortex->clusters[rec.cluster_id].accum,   rec.accum,
                   sizeof(float) * LANG_WORD_DIM);
            cortex->clusters[rec.cluster_id].sample_count = rec.sample_count;
            cortex->clusters[rec.cluster_id].frozen       = (rec.frozen != 0);
            cortex->clusters[rec.cluster_id].active       = true;
            strncpy(cortex->clusters[rec.cluster_id].word_label, rec.word_label, 63);
            cortex->clusters[rec.cluster_id].word_label[63] = '\0';
        }

        // Block 3: Topology (flat iteration)
        int total_edges_read = 0;
        for (uint32_t i = 0; i < header.alive_node_count; i++) {
            NodeRecord nrec;
            if (fread(&nrec, sizeof(NodeRecord), 1, f) != 1) {
                fprintf(stderr, "[SynapticMemory] ERROR: Truncated node at %u\n", i);
                fclose(f); return false;
            }
            if (nrec.node_id < 0 || nrec.node_id >= (int)header.node_count) {
                fprintf(stderr, "[SynapticMemory] ERROR: Bad node_id %d\n", nrec.node_id);
                fclose(f); return false;
            }

            ClusterNode& n = graph->cortex_memory[nrec.node_id];
            n.alive.store(true,   std::memory_order_relaxed);
            n.activation.store(0.0f, std::memory_order_relaxed);
            n.clamped.store(false,   std::memory_order_relaxed);

            n.edge_count.store(nrec.edge_count, std::memory_order_relaxed);
            for (int e = 0; e < nrec.edge_count; e++) {
                EdgeRecord erec;
                if (fread(&erec, sizeof(EdgeRecord), 1, f) != 1) {
                    fprintf(stderr, "[SynapticMemory] ERROR: Truncated forward edge.\n");
                    fclose(f); return false;
                }
                n.edges[e].target        = erec.target;
                n.edges[e].weight        = erec.weight;
                n.edges[e].co_occur_count = erec.co_occur_count;
                n.edges[e].type          = (EdgeType)erec.type;
                total_edges_read++;
            }

            n.inverse_edge_count.store(nrec.inverse_edge_count, std::memory_order_relaxed);
            for (int e = 0; e < nrec.inverse_edge_count; e++) {
                EdgeRecord erec;
                if (fread(&erec, sizeof(EdgeRecord), 1, f) != 1) {
                    fprintf(stderr, "[SynapticMemory] ERROR: Truncated inverse edge.\n");
                    fclose(f); return false;
                }
                n.inverse_edges[e].target        = erec.target;
                n.inverse_edges[e].weight        = erec.weight;
                n.inverse_edges[e].co_occur_count = erec.co_occur_count;
                n.inverse_edges[e].type          = (EdgeType)erec.type;
                total_edges_read++;
            }
        }

        fclose(f);
        // brain_lock releases here

        auto t_end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        printf("[SynapticMemory] WAKE complete.\n");
        printf("  File: %s\n",                   filepath);
        printf("  Nodes: %u alive / %u total\n", header.alive_node_count, header.node_count);
        printf("  Cortex: %u active clusters\n", header.cortex_active_count);
        printf("  Edges: %d total\n",             total_edges_read);
        printf("  Time: %.3f ms\n",               ms);
        printf("  All activations reset to 0.0 (cold boot).\n");
        return true;
    }
};
