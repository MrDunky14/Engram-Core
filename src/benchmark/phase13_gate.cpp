// ============================================================
// Phase 13 gate — sovereign reasoning cycle + ShadowBrain veto
//
// Mirrors the live daemon path: temporal L1 touch → deterministic speaker bind
// → episodic cues (smoke). Enforces destructive-plan veto parity with sandbox.
//
// Compile (from repo root):
//   cl /std:c++17 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS
//      /I src\core /I src\benchmark
//      src\benchmark\phase13_gate.cpp
//      /Fe:build\phase13_gate.exe
// ============================================================

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_sandbox.h"
#include "fpsan_speaker.h"
#include "fpsan_temporal_memory.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

static int perceive_word(SpikingTokenizer& tok,
                         LanguageCortex* cortex,
                         const char* w,
                         bool learn) noexcept {
    int8_t h[LANG_WORD_DIM];
    tok.encode_word_hash(std::string(w), h);
    return cortex->perceive(h, learn, w);
}

static bool cycle_once(fpsan::TemporalMemory* tm,
                       uint64_t tick,
                       NativeLexer* lexer,
                       SpikingTokenizer* tok,
                       LanguageCortex* cortex,
                       ClusterGraph* graph,
                       int seed_known,
                       int seed_unknown,
                       bool use_known) {
    Token toks_[64]{};
    const char* phrase = use_known ? "tell me sun fact" : "tell me xyzzy blurgh";
    int n = lexer->tokenize(phrase, toks_);

    for (int i = 0; i < n; ++i) {
        if (toks_[i].tag == POS_PUNCT || toks_[i].tag == POS_UNKNOWN) continue;
        int8_t h[LANG_WORD_DIM]{};
        std::string w(toks_[i].text);
        tok->encode_word_hash(w, h);
        int cid = cortex->perceive(h, false, toks_[i].text);
        if (cid >= 0)
            tm->promote_to_l1(graph, cid, tick);
    }

    int seed = use_known ? seed_known : seed_unknown;
    (void)tm->reactivate_similar_episodes(graph, seed, 2, 0.05f, 0.1f);

    char buf[512]{};
    fpsan::SpeakerAuditEntry audits[(int)fpsan::SPK_AUDIT_MAX_WORDS]{};
    int an = 0;
    bool spk = fpsan::speaker_compose_define_is_a(
        graph, cortex, seed, buf, (int)sizeof(buf), 0.15f,
        audits, &an, (int)(sizeof(audits) / sizeof(audits[0])));

    if (use_known) {
        if (!spk) return false;
        return fpsan::speaker_validate_audit_truthful(audits, an, graph);
    }

    return !fpsan::speaker_validate_audit_truthful(audits, an, graph);
}

static bool shadow_veto_suite() noexcept {
    SpikingTokenizer tok;
    std::unique_ptr<LanguageCortex> cortex(new LanguageCortex());

    constexpr int BOOT = INITIAL_CLUSTERS;
    for (int scenario = 0; scenario < 20; scenario++) {
        ClusterGraph graph;
        graph.init(BOOT);
        cortex->init();

        char cname[32];
        snprintf(cname, sizeof(cname), "sc%dconcept", scenario);
        int concept_id = perceive_word(tok, cortex.get(), cname, true);

        char mname[32];
        snprintf(mname, sizeof(mname), "sc%dmotor", scenario);
        int motor_id = graph.spawn();
        if (motor_id < 0) return false;

        graph.node(concept_id).add_edge(motor_id, 1.25f, EDGE_IMPLEMENTED_BY);
        graph.node(motor_id).is_motor_node.store(true, std::memory_order_release);
        graph.node(motor_id).motor_action.type = MOTOR_SLEEP_MS;
        graph.node(motor_id).motor_action.delay_ms = 1;

        char pname[32];
        snprintf(pname, sizeof(pname), "sc%dprec", scenario);
        int prec_id = perceive_word(tok, cortex.get(), pname, true);

        graph.node(motor_id).add_edge(prec_id, 1.0f, EDGE_REQUIRES);

        int chain[] = { motor_id };

        ShadowBrain sb;
        sb.mirror(&graph);
        bool safe = sb.is_safe(concept_id, chain, 1, &graph);
        if (safe)
            return false;
    }

    return true;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    puts("PHASE 13 GATE: sovereign cycle + ShadowBrain veto");

    NativeLexer lexer;
    SpikingTokenizer tok;
    std::unique_ptr<LanguageCortex> cortex(new LanguageCortex());
    ClusterGraph graph;
    fpsan::TemporalMemory temporal;

    graph.init(6500);
    cortex->init();
    lexer.init();

    lexer.ingest_sentence("sun mercury star blurgh trivia",
                         &graph, &tok, cortex.get());

    const int cid_sun   = perceive_word(tok, cortex.get(), "sun", false);
    const int cid_star  = perceive_word(tok, cortex.get(), "mercury", true);
    const int cid_phant = perceive_word(tok, cortex.get(), "xyzzy", true);

    if (cid_sun < 0 || cid_star < 0 || cid_phant < 0) {
        puts("FAIL: clusters");
        return 1;
    }

    graph.node(cid_sun).add_edge(cid_star, 1.25f, EDGE_IS_A, PROV_USER);
    graph.node(cid_star).add_inverse_edge(cid_sun, 1.25f, EDGE_IS_A);

    const int KNOWN_SLOTS = 90;
    const int UNK_SLOTS = 10;
    int intent_ok = 0;
    int slots_ok = 0;

    for (int turn = 0; turn < (KNOWN_SLOTS + UNK_SLOTS); turn++) {
        bool known = turn < KNOWN_SLOTS;
        if (cycle_once(&temporal, (uint64_t)turn + 1000u,
                       &lexer, &tok, cortex.get(), &graph,
                       cid_sun, cid_phant, known)) {
            intent_ok++;
        }

        fpsan::SpeakerAuditEntry audits[(int)fpsan::SPK_AUDIT_MAX_WORDS]{};
        int an = 0;
        char buf[512]{};
        if (fpsan::speaker_compose_define_is_a(
                &graph, cortex.get(),
                known ? cid_sun : cid_phant,
                buf, (int)sizeof(buf), 0.15f,
                audits, &an, (int)(sizeof(audits) / sizeof(audits[0])))) {
            if (fpsan::speaker_validate_audit_truthful(audits, an, &graph))
                slots_ok++;
        }
    }

    const int TOTAL = KNOWN_SLOTS + UNK_SLOTS;
    if (intent_ok < (TOTAL * 90 / 100)) {
        printf("FAIL: intent coverage %d/%d\n", intent_ok, TOTAL);
        return 1;
    }
    const int denom_bind = KNOWN_SLOTS;
    if (slots_ok < (denom_bind * 80 / 100)) {
        printf("FAIL: slot truthful binds %d/%d\n", slots_ok, denom_bind);
        return 1;
    }

    if (!shadow_veto_suite()) {
        puts("FAIL: ShadowBrain missed destructive veto(s)");
        return 1;
    }

    puts("PASS");
    return 0;
}
