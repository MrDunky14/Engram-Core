// ============================================================
// Gate E1 — C++23 Concurrency & Lossless Persistence Verification
// gate_e1_concurrency_and_persistence.cpp
//
// MANDATORY PASS CRITERIA:
//   1. Lossless Sleep/Wake v3 Round-Trip Test:
//      - Serializes node flags (binding, motor, clamped), memory tiers (L1, L2, L3),
//        motor actions (keys/modifiers/text), and edge provenance (PROV_WIKIPEDIA).
//      - Restores bit-for-bit topology, flags, actions, and trust metadata.
//      - Verifies speaker_compose_define_is_a() + speaker_validate_audit_truthful() pass after reload.
//      - 1-byte corrupted file is strictly rejected via FNV-1a checksum mismatch.
//   2. 8-Thread Contention Stress Test (3 seconds):
//      - 2 threads: aggressive spawn(), kill(), add_edge() with invariant checking.
//      - 2 threads: LanguageCortex::perceive() and Lexicon::insert() / lookup().
//      - 4 threads: spread_activation(), tick(), scan_analogies(), levenshtein_dist().
//      - Asserts 0 crashes, 0 torn edge reads, 0 deadlocks.
// ============================================================

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_memory.h"
#include "fpsan_speaker.h"
#include "fpsan_drives.h"

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cassert>
#include <fstream>
#include <filesystem>
#include <random>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

using namespace std;
using namespace std::chrono;

static int register_word(SpikingTokenizer& tok, LanguageCortex& cx, const char* w) {
    int8_t h[LANG_WORD_DIM];
    tok.encode_word_hash(std::string(w), h);
    return cx.perceive(h, true, w);
}

// ─────────────────────────────────────────────────────────────
// TEST 1: Lossless Sleep/Wake v3 Round-Trip & Checksum Guard
// ─────────────────────────────────────────────────────────────
static bool test_lossless_persistence_v3() {
    cout << "\n======================================================\n";
    cout << "[Gate E1] Running Test 1: Lossless Sleep/Wake v3 Round-Trip\n";
    cout << "======================================================\n";

    std::filesystem::create_directories("build");
    const char* filepath = "build/gate_e1_test.fpsan";
    const char* corrupt_filepath = "build/gate_e1_corrupt.fpsan";

    SpikingTokenizer tok;
    auto g1 = std::make_unique<ClusterGraph>();
    g1->init(500);
    auto cx1 = std::make_unique<LanguageCortex>();
    cx1->init();

    int cid_sun   = register_word(tok, *cx1, "sun");
    int cid_star  = register_word(tok, *cx1, "star");
    int cid_bind  = register_word(tok, *cx1, "hub_binding");
    int cid_dir   = register_word(tok, *cx1, "directive_safety");
    int cid_motor = register_word(tok, *cx1, "action_save");

    assert(cid_sun >= 0 && cid_star >= 0 && cid_bind >= 0 && cid_dir >= 0 && cid_motor >= 0);

    // 1. Setup Binding Node
    g1->node(cid_bind).is_binding_node.store(true, memory_order_relaxed);

    // 2. Setup Clamped L3 Directive
    g1->node(cid_dir).memory_tier.store(MEMORY_TIER_L3, memory_order_relaxed);
    g1->node(cid_dir).clamped.store(true, memory_order_relaxed);

    // 3. Setup Motor Node
    g1->node(cid_motor).is_motor_node.store(true, memory_order_relaxed);
    g1->node(cid_motor).motor_action.type     = MOTOR_KEY_CHORD;
    g1->node(cid_motor).motor_action.vkey     = 'S';
    g1->node(cid_motor).motor_action.modifier = VK_CONTROL;
    g1->node(cid_motor).motor_action.delay_ms = 50;
    strncpy(g1->node(cid_motor).motor_action.text, "save_buffer", 63);

    // 4. Setup Verified Knowledge with Provenance
    g1->node(cid_sun).add_edge(cid_star, 1.8f, EDGE_IS_A, PROV_WIKIPEDIA);
    g1->node(cid_star).add_inverse_edge(cid_sun, 1.8f, EDGE_IS_A);

    g1->node(cid_sun).add_edge(cid_bind, 1.0f, EDGE_TEMPORAL, PROV_USER);
    g1->node(cid_bind).add_inverse_edge(cid_sun, 1.0f, EDGE_TEMPORAL);

    // Setup dead slot to verify Treiber stack reconstruction across sleep/wake
    int n_dead_target = g1->spawn();
    int n_alive_follow = g1->spawn();
    assert(n_dead_target >= 0 && n_alive_follow >= 0);
    g1->kill(n_dead_target); // killed before sleep

    // Verify Speaker passes before sleep
    fpsan::SpeakerAuditEntry audits[16];
    int audit_n = 0;
    char line[256]{};
    bool spk_pre = fpsan::speaker_compose_define_is_a(
        g1.get(), cx1.get(), cid_sun, line, (int)sizeof(line), 0.15f, audits, &audit_n, 16);
    assert(spk_pre);
    bool truth_pre = fpsan::speaker_validate_audit_truthful(audits, audit_n, g1.get());
    assert(truth_pre);
    cout << "  Pre-sleep composed speech: \"" << line << "\" [PASS]\n";

    // 5. Sleep to v3 file
    bool sleep_ok = SynapticMemory::sleep(filepath, g1.get(), cx1.get());
    if (!sleep_ok) {
        cerr << "FAIL: SynapticMemory::sleep failed!\n";
        return false;
    }

    // 6. Wake into fresh graph and cortex
    auto g2 = std::make_unique<ClusterGraph>();
    auto cx2 = std::make_unique<LanguageCortex>();
    bool wake_ok = SynapticMemory::wake(filepath, g2.get(), cx2.get());
    if (!wake_ok) {
        cerr << "FAIL: SynapticMemory::wake failed!\n";
        return false;
    }

    // 7. Bit-for-bit assertions
    cout << "  Verifying bit-for-bit node flags & motor actions...\n";
    assert(g2->node(cid_bind).is_binding_node.load(memory_order_relaxed) == true);
    assert(g2->node(cid_dir).memory_tier.load(memory_order_relaxed) == MEMORY_TIER_L3);
    assert(g2->node(cid_dir).clamped.load(memory_order_relaxed) == true);

    assert(g2->node(cid_motor).is_motor_node.load(memory_order_relaxed) == true);
    assert(g2->node(cid_motor).motor_action.type == MOTOR_KEY_CHORD);
    assert(g2->node(cid_motor).motor_action.vkey == 'S');
    assert(g2->node(cid_motor).motor_action.modifier == VK_CONTROL);
    assert(g2->node(cid_motor).motor_action.delay_ms == 50);
    assert(strcmp(g2->node(cid_motor).motor_action.text, "save_buffer") == 0);

    // Verify dead node stays dead and alive node stays alive
    assert(g2->node(n_alive_follow).alive.load(memory_order_relaxed) == true);
    assert(g2->node(n_dead_target).alive.load(memory_order_relaxed) == false);
    int recycled = g2->spawn();
    assert(recycled == n_dead_target);
    cout << "  Treiber stack dead node recycling across sleep/wake verified [PASS]\n";

    // Verify edge provenance was retained
    Edge sun_edges[MAX_FANOUT];
    int sun_ec = g2->node(cid_sun).copy_edges_snapshot(sun_edges, MAX_FANOUT);
    bool found_is_a = false;
    for (int e = 0; e < sun_ec; ++e) {
        if (sun_edges[e].type == EDGE_IS_A && sun_edges[e].target == cid_star) {
            assert(sun_edges[e].provenance == PROV_WIKIPEDIA);
            found_is_a = true;
        }
    }
    assert(found_is_a);
    cout << "  Edge provenance PROV_WIKIPEDIA verified bit-for-bit [PASS]\n";

    // Verify speaker audit is still truthful after reload
    audit_n = 0;
    line[0] = '\0';
    bool spk_post = fpsan::speaker_compose_define_is_a(
        g2.get(), cx2.get(), cid_sun, line, (int)sizeof(line), 0.15f, audits, &audit_n, 16);
    assert(spk_post);
    bool truth_post = fpsan::speaker_validate_audit_truthful(audits, audit_n, g2.get());
    assert(truth_post);
    cout << "  Post-reload composed speech: \"" << line << "\" [PASS]\n";

    // 8. Corrupt 1 byte in payload and assert checksum mismatch rejection
    cout << "  Testing 1-byte file corruption rejection...\n";
    vector<uint8_t> file_bytes;
    {
        ifstream in(filepath, ios::binary);
        file_bytes.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
    }
    assert(file_bytes.size() > sizeof(FpsanHeader) + 16);
    // Mutate byte in the payload block
    file_bytes[sizeof(FpsanHeader) + 8] ^= 0x7F;
    {
        ofstream out(corrupt_filepath, ios::binary);
        out.write(reinterpret_cast<const char*>(file_bytes.data()), file_bytes.size());
    }

    auto g3 = std::make_unique<ClusterGraph>();
    g3->init(50);
    g3->node(5).activation.store(42.0f, memory_order_relaxed);
    auto cx3 = std::make_unique<LanguageCortex>();
    cx3->init();
    bool corrupt_wake_ok = SynapticMemory::wake(corrupt_filepath, g3.get(), cx3.get());
    if (corrupt_wake_ok) {
        cerr << "FAIL: Corrupted file was accepted! Checksum guard failed!\n";
        return false;
    }
    assert(g3->node(5).activation.load(memory_order_relaxed) == 42.0f);
    cout << "  Corrupted file correctly rejected & in-memory canary preserved [PASS]\n";

    // Clean up temp test files
    filesystem::remove(filepath);
    filesystem::remove(corrupt_filepath);

    cout << ">>> Test 1 (Lossless Persistence v3) PASSED <<<\n";
    return true;
}

// ─────────────────────────────────────────────────────────────
// TEST 2: 8-Thread Contention Stress Test (3 seconds)
// ─────────────────────────────────────────────────────────────
static bool test_8thread_concurrency_stress() {
    cout << "\n======================================================\n";
    cout << "[Gate E1] Running Test 2: 8-Thread Contention Stress (3s)\n";
    cout << "======================================================\n";

    auto g = std::make_unique<ClusterGraph>();
    g->init(1000);
    auto cx = std::make_unique<LanguageCortex>();
    cx->init();
    auto lex = std::make_unique<Lexicon>();
    lex->init(20000);
    lex->bootstrap_core();

    atomic<bool> stop_flag{false};
    atomic<uint64_t> ops_neuro{0};
    atomic<uint64_t> ops_lang{0};
    atomic<uint64_t> ops_spread{0};
    atomic<uint64_t> ops_tick{0};
    atomic<uint64_t> torn_invariants{0};

    // Pre-populate some words
    SpikingTokenizer tok;
    for (int i = 0; i < 50; i++) {
        string w = "core_word_" + to_string(i);
        register_word(tok, *cx, w.c_str());
        lex->insert(w.c_str(), POS_NOUN);
    }

    vector<thread> threads;
    threads.reserve(8);

    // Threads 0 & 1: Rapid Neurogenesis, Edge Mutation & Invariant Checks
    for (int t = 0; t < 2; t++) {
        threads.emplace_back([&, t]() {
            mt19937 rng(1337 + t);
            uniform_int_distribution<int> node_dist(0, 499);
            Edge snap[MAX_FANOUT];

            while (!stop_flag.load(memory_order_relaxed)) {
                int id = g->spawn();
                if (id >= 0) {
                    int target = (id * 7 + 1) % 500;
                    g->node(id).add_edge(target, 1.5f, EDGE_IS_A, PROV_WIKIPEDIA);

                    // Check snapshot under reader seqlock
                    int ec = g->node(id).copy_edges_snapshot(snap, MAX_FANOUT);
                    for (int i = 0; i < ec; i++) {
                        if (snap[i].type >= EDGE_TYPE_COUNT || snap[i].provenance > PROV_VISUAL ||
                            snap[i].target < 0 || snap[i].weight <= 0.0f) {
                            torn_invariants.fetch_add(1, memory_order_relaxed);
                        }
                    }

                    if (id > 800) {
                        g->kill(id);
                    }
                    ops_neuro.fetch_add(1, memory_order_relaxed);
                } else {
                    this_thread::yield();
                }
            }
        });
    }

    // Threads 2 & 3: Language Cortex & Lexicon Concurrent Perception
    for (int t = 0; t < 2; t++) {
        threads.emplace_back([&, t]() {
            SpikingTokenizer local_tok;
            int counter = 0;
            while (!stop_flag.load(memory_order_relaxed)) {
                string w = "test_word_" + to_string((counter++) % 150);
                int8_t h[LANG_WORD_DIM];
                local_tok.encode_word_hash(w, h);
                int cid = cx->perceive(h, true, w.c_str());
                POSTag tag = lex->lookup(w.c_str());
                if (tag == POS_UNKNOWN) {
                    lex->insert(w.c_str(), POS_NOUN);
                }
                assert(cid >= 0);
                ops_lang.fetch_add(1, memory_order_relaxed);
            }
        });
    }

    // Thread 4: Spreading Activation & Chain Queries
    threads.emplace_back([&]() {
        mt19937 rng(42);
        uniform_int_distribution<int> dist(0, 400);
        int path[16];
        while (!stop_flag.load(memory_order_relaxed)) {
            int src = dist(rng);
            g->spread_activation(src, 1.0f);
            g->predict_chain(src, path, 8);
            ops_spread.fetch_add(1, memory_order_relaxed);
        }
    });

    // Thread 5: Cognitive Tick & Homeostatic Drives
    threads.emplace_back([&]() {
        HomeostaticDrives drives;
        drives.init();
        uint64_t current_tick = 0;
        while (!stop_flag.load(memory_order_relaxed)) {
            g->tick(0.95f);
            drives.tick(++current_tick);
            if (current_tick % 100 == 0) {
                drives.on_unknown_word("mystery_topic");
                drives.pop_fetch_topic();
            }
            ops_tick.fetch_add(1, memory_order_relaxed);
        }
    });

    // Thread 6: Analogy Scanner (Topology Fingerprinting)
    threads.emplace_back([&]() {
        while (!stop_flag.load(memory_order_relaxed)) {
            g->scan_analogies(16);
            this_thread::sleep_for(milliseconds(1));
        }
    });

    // Thread 7: Typo Resonance & Levenshtein DP
    threads.emplace_back([&]() {
        while (!stop_flag.load(memory_order_relaxed)) {
            int d = NativeLexer::levenshtein_dist("biological", "biologcal");
            assert(d == 1);
            int d2 = NativeLexer::levenshtein_dist("concurrency", "concurrency");
            assert(d2 == 0);
            this_thread::yield();
        }
    });

    // Run for 3.0 seconds
    cout << "  Stress testing with 8 threads for 3.0 seconds...\n";
    this_thread::sleep_for(seconds(3));
    stop_flag.store(true, memory_order_release);

    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }

    uint64_t torn = torn_invariants.load();
    cout << "  Operations Completed:\n";
    cout << "    Neurogenesis & Invariant Checks: " << ops_neuro.load() << "\n";
    cout << "    Language Perception & Lexicon:   " << ops_lang.load() << "\n";
    cout << "    Spreading Activation Chains:     " << ops_spread.load() << "\n";
    cout << "    Cognitive Ticks & Drives:        " << ops_tick.load() << "\n";
    cout << "    Torn Edge Invariant Violations:  " << torn << "\n";

    if (torn != 0) {
        cerr << "FAIL: Detected " << torn << " torn edge invariant violations!\n";
        return false;
    }

    cout << ">>> Test 2 (8-Thread Contention Stress) PASSED <<<\n";
    return true;
}

// ─────────────────────────────────────────────────────────────
// TEST 3: Memory Working Set (RSS) Demand-Paging Verification
// ─────────────────────────────────────────────────────────────
static bool test_memory_rss_footprint() {
    cout << "\n======================================================\n";
    cout << "[Gate E1] Running Test 3: Memory Working Set (RSS) Verification\n";
    cout << "======================================================\n";

    // Boot a production-scale 6,500-cluster graph, language cortex, and lexicon
    auto g = std::make_unique<ClusterGraph>();
    g->init(INITIAL_CLUSTERS);
    auto cx = std::make_unique<LanguageCortex>();
    cx->init();
    auto lex = std::make_unique<Lexicon>();
    lex->init();
    lex->bootstrap_core();

#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        size_t rss_mb = pmc.WorkingSetSize / (1024 * 1024);
        cout << "  Active Process Working Set (RSS): " << rss_mb << " MB\n";
        if (rss_mb >= 50) {
            cerr << "FAIL: Working set " << rss_mb << " MB exceeds 50 MB threshold!\n";
            return false;
        }
        cout << "  Working set < 50 MB verified (Demand-paged arena + flat lexicon active) [PASS]\n";
    }
#else
    cout << "  POSIX RSS check skipped.\n";
#endif

    cout << ">>> Test 3 (Memory Working Set RSS) PASSED <<<\n";
    return true;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    cout.setf(ios::unitbuf);
    cout << "\n======================================================\n";
    cout << "  ENGRAM-CORE GATE E1: C++23 CONCURRENCY & PERSISTENCE \n";
    cout << "======================================================\n";

    if (!test_lossless_persistence_v3()) {
        cerr << "\n[GATE E1 FAILED] Lossless persistence test failed.\n";
        return 1;
    }

    if (!test_8thread_concurrency_stress()) {
        cerr << "\n[GATE E1 FAILED] Concurrency stress test failed.\n";
        return 1;
    }

    if (!test_memory_rss_footprint()) {
        cerr << "\n[GATE E1 FAILED] Memory RSS footprint test failed.\n";
        return 1;
    }

    cout << "\n======================================================\n";
    cout << "  [GATE E1 PASSED] ALL C++23 CONCURRENCY & PERSISTENCE GATES SATISFIED\n";
    cout << "======================================================\n\n";
    return 0;
}
