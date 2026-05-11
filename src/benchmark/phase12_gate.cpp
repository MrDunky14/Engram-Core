// Phase 12 gate: pure deterministic speaker.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_speaker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static uint64_t now_ns_qpc() {
    static LARGE_INTEGER fq = []() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    const uint64_t q = (uint64_t)(t.QuadPart / fq.QuadPart);
    const uint64_t r = (uint64_t)(t.QuadPart % fq.QuadPart);
    return q * 1000000000ull + (r * 1000000000ull) / (uint64_t)fq.QuadPart;
}

static int perceive_word(SpikingTokenizer& tok,
                         LanguageCortex* cortex,
                         const char* w,
                         bool learn) noexcept
{
    int8_t h[LANG_WORD_DIM];
    tok.encode_word_hash(std::string(w), h);
    return cortex->perceive(h, learn, w);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    puts("PHASE 12 GATE: pure deterministic speaker");

    remove("artefacts/speaker_audit_gate12.csv");

    ClusterGraph graph;
    std::unique_ptr<LanguageCortex> cortex(new LanguageCortex());
    NativeLexer lexer;
    SpikingTokenizer tok;
    lexer.init();

    graph.init(6500);
    cortex->init();

    lexer.ingest_sentence("sun mercury astronomy vector",
                         &graph, &tok, cortex.get());

    const int cid_sun   = perceive_word(tok, cortex.get(), "sun", false);
    const int cid_star  = perceive_word(tok, cortex.get(), "mercury", true);
    const int cid_noise = perceive_word(tok, cortex.get(), "astronomy", true);

    if (cid_sun < 0 || cid_star < 0 || cid_noise < 0) {
        puts("FAIL: seed clusters");
        return 1;
    }

    graph.node(cid_sun).add_edge(cid_star, 1.25f, EDGE_IS_A, PROV_WIKIPEDIA);
    graph.node(cid_star).add_inverse_edge(cid_sun, 1.25f, EDGE_IS_A);

    graph.node(cid_sun).add_edge(cid_noise, 99.f, EDGE_IS_A, PROV_UNKNOWN);
    graph.node(cid_noise).add_inverse_edge(cid_sun, 99.f, EDGE_IS_A);

    fpsan::SpeakerAuditEntry audits[(int)fpsan::SPK_AUDIT_MAX_WORDS];

    constexpr int KNOWN_CYCLES = 30;
    constexpr int UNKNOWN_CYCLES = 170;

    std::vector<uint64_t> lat_ns;
    lat_ns.reserve((size_t)(KNOWN_CYCLES + UNKNOWN_CYCLES));

    for (int i = 0; i < KNOWN_CYCLES; ++i) {
        char line[320];
        int an = 0;
        memset(audits, 0, sizeof(audits));

        uint64_t t0 = now_ns_qpc();
        bool ok = fpsan::speaker_compose_define_is_a(
            &graph, cortex.get(), cid_sun, line, (int)sizeof(line),
            0.15f,
            audits, &an, (int)(sizeof(audits) / sizeof(audits[0])));
        uint64_t t1 = now_ns_qpc();
        lat_ns.push_back(t1 - t0);

        if (!ok) {
            puts("FAIL: known subject refused");
            return 1;
        }
        if (!fpsan::speaker_validate_audit_truthful(audits, an, &graph)) {
            puts("FAIL: known answer audit dishonest");
            return 1;
        }
    }

    int unknown_ok = 0;
    for (int i = 0; i < UNKNOWN_CYCLES; ++i) {
        std::string w = std::string("qx") + std::to_string(100000 + i);
        const int ph = perceive_word(tok, cortex.get(), w.c_str(), true);

        char line[320];
        int an = 0;
        memset(audits, 0, sizeof(audits));

        uint64_t t0 = now_ns_qpc();
        bool ok = fpsan::speaker_compose_define_is_a(
            &graph, cortex.get(), ph, line, (int)sizeof(line),
            0.80f,
            audits, &an, (int)(sizeof(audits) / sizeof(audits[0])));
        uint64_t t1 = now_ns_qpc();
        lat_ns.push_back(t1 - t0);

        if (!ok)
            unknown_ok++;
    }

    const float frac = unknown_ok / (float)UNKNOWN_CYCLES;
    printf("  unknown_refusal_frac=%.4f (%d/%d)\n",
           frac, unknown_ok, UNKNOWN_CYCLES);
    if (frac < 0.95f) {
        puts("FAIL: phantom refusal quota");
        return 1;
    }

    const size_t n = lat_ns.size();
    const size_t pk = (size_t)std::floor(0.99 * double(n <= 1 ? 0 : n - 1));
    std::nth_element(lat_ns.begin(), lat_ns.begin() + (ptrdiff_t)pk, lat_ns.end());
    double p99_ms = (double)lat_ns[pk] / 1000000.0;
    printf("  compose_latency_p99_ms=%.6f (%zu samples)\n", p99_ms, n);
    if (p99_ms > 8.0) {
        puts("FAIL: latency budget");
        return 1;
    }

    puts("PASS: Phase 12 gate");
    return 0;
}
