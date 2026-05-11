#pragma once
// ============================================================
// Phase 12 — Pure deterministic speaker (graph-walked composer)
// No LLM at runtime. Optional binary artefacts: templates.bin, bigrams.bin
// ============================================================

#include "cluster_graph.h"
#include "fpsan_language.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace fpsan {

static constexpr uint32_t SPK_AUDIT_MAX_WORDS = 48;

enum class SpeechIntent : uint8_t {
    DEFINE = 0,
    EXPLAIN = 1,
    REFUSE = 2,
};

struct SpeakerAuditEntry {
    char             word_token[96]{};
    int              source_cluster = -1;
    EdgeType         edge_used      = EDGE_TEMPORAL;
    EdgeProvenance   provenance     = PROV_UNKNOWN;
};

inline float speaker_provenance_trust(EdgeProvenance p) noexcept {
    switch (p) {
        case PROV_USER:       return 1.0f;
        case PROV_WIKIPEDIA:  return 0.9f;
        case PROV_CONCEPTNET: return 0.85f;
        case PROV_INFERRED:   return 0.6f;
        case PROV_VISUAL:     return 0.95f;
        default:               return -1.f; // rejects PROV_UNKNOWN
    }
}

/// Pick highest-trust EDGE_IS_A (or EDGE_HAS_A) outbound edge from `subject`.
inline bool speaker_pick_predicate_target(
    ClusterGraph* graph,
    int subject,
    EdgeType et,
    float min_trust,
    int* out_tgt,
    float* out_score,
    EdgeProvenance* out_prov) noexcept
{
    if (!graph || subject < 0 || !out_tgt) return false;
    std::shared_lock<std::shared_mutex> lk(graph->graph_rw_lock);
    const int nc = graph->node_count.load(std::memory_order_acquire);
    if (subject >= nc) return false;

    const ClusterNode& nd = graph->node(subject);
    const int ec = nd.edge_count.load(std::memory_order_acquire);
    int best = -1;
    float best_s = -1.f;
    EdgeProvenance best_p = PROV_UNKNOWN;

    for (int i = 0; i < ec; ++i) {
        const Edge& e = nd.edges[i];
        if (e.type != et) continue;
        const float tw = speaker_provenance_trust(e.provenance);
        if (tw < 0.f || tw < min_trust) continue;
        float sc = tw * std::max(0.05f, e.weight);
        if (sc > best_s) {
            best_s = sc;
            best = e.target;
            best_p = e.provenance;
        }
    }
    if (best < 0) return false;
    *out_tgt = best;
    if (out_score) *out_score = best_s;
    if (out_prov) *out_prov = best_p;
    return true;
}

inline void speaker_append_audit(
    SpeakerAuditEntry* audits,
    int* audit_fill,
    int audit_cap,
    const char* word,
    int src_c,
    EdgeType et,
    EdgeProvenance prov) noexcept
{
    if (!audits || !audit_fill) return;
    if (*audit_fill >= audit_cap) return;
    SpeakerAuditEntry& e = audits[*audit_fill];
    std::memset(&e, 0, sizeof(e));
    std::strncpy(e.word_token, word, sizeof(e.word_token) - 1);
    e.source_cluster = src_c;
    e.edge_used = et;
    e.provenance = prov;
    ++(*audit_fill);
}

inline bool speaker_flush_audit_csv(const char* path,
                                    const SpeakerAuditEntry* audits,
                                    int audit_n) noexcept
{
    FILE* fp = fopen(path, "ab");
    if (!fp) return false;
    static std::atomic<int> hdr_inited{0};
    bool need_hdr = hdr_inited.load(std::memory_order_relaxed) == 0;
    if (need_hdr) {
        fputs("word,cluster_id,edge_type,provenance_enum\n", fp);
        hdr_inited.store(1, std::memory_order_relaxed);
    }
    for (int i = 0; i < audit_n; ++i) {
        fprintf(fp, "%s,%d,%u,%u\n",
               audits[i].word_token,
               audits[i].source_cluster,
               (unsigned)(uint8_t)audits[i].edge_used,
               (unsigned)(uint8_t)audits[i].provenance);
    }
    fclose(fp);
    return true;
}

/// Compose a short deterministic EXPLAIN utterance anchored at `subject_cid`.
/// Returns false → caller should spike curiosity / emit canonical refusal externally.
inline bool speaker_compose_define_is_a(
    ClusterGraph* graph,
    LanguageCortex* cortex,
    int subject_cid,
    char* buf,
    int buflen,
    float min_prov_trust,
    SpeakerAuditEntry* audits,
    int* audit_n,
    int audit_cap)
{
    if (!graph || !cortex || !buf || buflen < 16) return false;

    const char* subject_label = cortex->get_word(subject_cid);

    int obj = -1;
    float scr = -1.f;
    EdgeProvenance prov = PROV_UNKNOWN;
    if (!speaker_pick_predicate_target(graph, subject_cid, EDGE_IS_A, min_prov_trust,
                                       &obj, &scr, &prov)) {
        std::snprintf(buf, (size_t)buflen, "I do not know.");
        return false;
    }

    const char* object_label = cortex->get_word(obj);

    std::snprintf(buf, (size_t)buflen,
                  "%s is a %s", subject_label && subject_label[0] ? subject_label : "?",
                               object_label && object_label[0] ? object_label : "?");

    if (audit_n) *audit_n = 0;
    if (audits && audit_n && audit_cap > 0) {
        speaker_append_audit(audits, audit_n, audit_cap,
                             subject_label && subject_label[0] ? subject_label : "?",
                             subject_cid, EDGE_NEXT_WORD, PROV_USER);
        speaker_append_audit(audits, audit_n, audit_cap,
                             "is", subject_cid, EDGE_NEXT_WORD, PROV_USER);
        speaker_append_audit(audits, audit_n, audit_cap,
                             "a", subject_cid, EDGE_NEXT_WORD, PROV_USER);
        speaker_append_audit(audits, audit_n, audit_cap,
                             object_label && object_label[0] ? object_label : "?",
                             obj, EDGE_IS_A, prov);
    }
    return true;
}

inline bool speaker_validate_audit_truthful(
    const SpeakerAuditEntry* audits,
    int audit_n,
    ClusterGraph*) noexcept
{
    for (int i = 0; i < audit_n; ++i) {
        const EdgeProvenance p = audits[i].provenance;
        if (p == PROV_UNKNOWN) return false;
        const float tw = speaker_provenance_trust(p);
        if (tw <= 0.f) return false;
    }
    return audit_n > 0;
}

} // namespace fpsan
