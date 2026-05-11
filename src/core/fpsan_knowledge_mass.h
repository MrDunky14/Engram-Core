#pragma once
// ============================================================
// FP-SAN Phase 5B: KNOWLEDGE MASS LOADER
// fpsan_knowledge_mass.h — Bare-metal binary triple injector.
//
// Reads a knowledge_mass.bin file produced by training/kb_extractor.py
// and injects every triple directly into the lock-free 262K-node arena
// using the same spawn() / add_edge() atomics built in Phase 1.
//
// Binary format (8-byte magic header + N×TripleRecord):
//   Header: "FPSANKM\x01"
//   Per record (134 bytes):
//     char[64]  subject   — null-terminated UTF-8
//     char[64]  object    — null-terminated UTF-8
//     uint8     relation  — EdgeType enum value
//     float     weight    — confidence [0,1]
//     uint8     provenance— EdgeProvenance enum value
//
// Thread-safety: holds graph_rw_lock unique_lock in 512-triple batches
// so the 1 kHz loop only stalls for ~0.5 ms bursts.
// ============================================================

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <shared_mutex>

#pragma pack(push,1)
struct TripleRecord {
    char    subject[64];
    char    object[64];
    uint8_t relation;
    float   weight;
    uint8_t provenance;
};
#pragma pack(pop)
static_assert(sizeof(TripleRecord) == 134, "TripleRecord size mismatch");

static constexpr uint8_t KM_MAGIC[8] = {'F','P','S','A','N','K','M',0x01};
static constexpr int     KM_BATCH    = 32;  // triples per unique_lock window (keeps worst_tick < 1.5ms)

struct KnowledgeMass {
    uint64_t triples_loaded  = 0;
    uint64_t nodes_created   = 0;
    uint64_t edges_created   = 0;
    bool     last_load_ok    = false;
    char     last_error[128] = {};

    // ── load() ──────────────────────────────────────────────
    // Reads path, injects all triples.  Returns triples loaded or -1 on error.
    // lang_cortex / tokenizer / lexer pointers must be live (same as main runtime).
    int load(const char*      path,
             ClusterGraph*    graph,
             LanguageCortex*  cortex,
             SpikingTokenizer* tokenizer,
             NativeLexer*     lexer)
    {
        last_load_ok = false;
        FILE* f = fopen(path, "rb");
        if (!f) {
            snprintf(last_error, sizeof(last_error),
                     "Cannot open '%s': %s", path, strerror(errno));
            return -1;
        }

        // Verify magic header
        uint8_t magic[8];
        if (fread(magic, 1, 8, f) != 8 ||
            memcmp(magic, KM_MAGIC, 8) != 0) {
            snprintf(last_error, sizeof(last_error),
                     "'%s' is not a valid knowledge_mass.bin (bad magic)", path);
            fclose(f);
            return -1;
        }

        int8_t word_hash[256];
        TripleRecord rec;
        int loaded = 0;
        int batch_count = 0;

        // Acquire unique_lock for first batch
        std::unique_lock<std::shared_mutex> lk(graph->graph_rw_lock);

        while (fread(&rec, sizeof(TripleRecord), 1, f) == 1) {
            // Null-safety
            rec.subject[63] = '\0';
            rec.object[63]  = '\0';
            if (rec.subject[0] == '\0' || rec.object[0] == '\0') continue;

            EdgeType   etype = (rec.relation < EDGE_TYPE_COUNT)
                               ? static_cast<EdgeType>(rec.relation) : EDGE_RELATED;
            EdgeProvenance prov = (rec.provenance <= PROV_VISUAL)
                               ? static_cast<EdgeProvenance>(rec.provenance) : PROV_UNKNOWN;
            float weight = (rec.weight > 0.0f && rec.weight <= 1.0f)
                           ? rec.weight : 0.3f;

            // Perceive / spawn subject cluster
            std::string subj_s(rec.subject);
            tokenizer->encode_word_hash(subj_s, word_hash);
            int sid = cortex->perceive(word_hash, true, rec.subject);
            if (sid < 0) continue;

            // Perceive / spawn object cluster
            std::string obj_s(rec.object);
            tokenizer->encode_word_hash(obj_s, word_hash);
            int oid = cortex->perceive(word_hash, true, rec.object);
            if (oid < 0) continue;

            // Wire the edge (spinlock inside, safe under unique_lock)
            graph->node(sid).add_edge(oid, weight, etype, prov);

            edges_created++;
            loaded++;
            batch_count++;

            // Release unique_lock every KM_BATCH triples so the 1 kHz loop
            // can breathe; re-acquire for the next batch.
            if (batch_count >= KM_BATCH) {
                lk.unlock();
                batch_count = 0;
                // Yield to let spread_activation run
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                lk.lock();
            }
        }

        fclose(f);
        triples_loaded += loaded;
        nodes_created   = static_cast<uint64_t>(
            graph->node_count.load(std::memory_order_acquire));
        last_load_ok = true;
        return loaded;
    }

    void print_stats() const {
        printf("\n  [KnowledgeMass] Load result:\n");
        printf("    Triples loaded : %llu\n", (unsigned long long)triples_loaded);
        printf("    Total nodes    : %llu\n", (unsigned long long)nodes_created);
        printf("    Edges created  : %llu\n", (unsigned long long)edges_created);
        if (!last_load_ok)
            printf("    Last error     : %s\n", last_error);
    }
};

// ── Singleton accessor ──
inline KnowledgeMass& get_knowledge_mass() {
    static KnowledgeMass km;
    return km;
}
