// FP-SAN Phase 10 + 10B Benchmark
// Tests: Typed Edges, Fan-out Capped Saturation, Native Lexer, Sentence Ingestion
#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <string>
using namespace std;
using namespace std::chrono;

int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN v17 — Phase 10 + 10B benchmark" << endl;
    cout << " Typed Edges | Saturation Fix | Native Lexer" << endl;
    cout << "================================================================" << endl;

    // ============================================================
    // TEST 1: Typed Edge Creation & Typed Spreading
    // ============================================================
    cout << "\n[1/6] TYPED EDGES: IS_A + CAN_DO Reasoning" << endl;
    cout << "---------------------------------------" << endl;

    // Heap-allocate all large objects to avoid stack overflow
    ClusterGraph* gp = new ClusterGraph();
    ClusterGraph& g = *gp;
    g.init(100); // Small graph for this test

    // Build a mini knowledge base:
    // eagle IS_A bird, bird CAN_DO fly, bird HAS_A wing
    // dog IS_A animal, animal CAN_DO move
    int eagle = 0, bird = 1, fly_node = 2, wing = 3;
    int dog = 4, animal = 5, move_node = 6;

    g.node(eagle).add_edge(bird, 0.9f, EDGE_IS_A);
    g.node(bird).add_inverse_edge(eagle, 0.9f, EDGE_IS_A);
    g.node(bird).add_edge(fly_node, 0.8f, EDGE_CAN_DO);
    g.node(fly_node).add_inverse_edge(bird, 0.8f, EDGE_CAN_DO);
    g.node(bird).add_edge(wing, 0.7f, EDGE_HAS_A);
    g.node(wing).add_inverse_edge(bird, 0.7f, EDGE_HAS_A);

    g.node(dog).add_edge(animal, 0.9f, EDGE_IS_A);
    g.node(animal).add_inverse_edge(dog, 0.9f, EDGE_IS_A);
    g.node(animal).add_edge(move_node, 0.8f, EDGE_CAN_DO);
    g.node(move_node).add_inverse_edge(animal, 0.8f, EDGE_CAN_DO);

    // Test: spread IS_A from eagle — should reach bird but NOT fly (wrong type)
    g.clear_activation();
    g.spread_typed(eagle, EDGE_IS_A);
    bool bird_reached = g.node(bird).activation.load(std::memory_order_acquire) > ACTIVATION_CUTOFF;
    bool fly_leaked   = g.node(fly_node).activation.load(std::memory_order_acquire) > ACTIVATION_CUTOFF;
    cout << "  eagle --IS_A--> bird: " << (bird_reached ? "REACHED" : "MISSED") << endl;
    cout << "  fly leaked via IS_A:  " << (fly_leaked ? "LEAKED [FAIL]" : "BLOCKED [PASS]") << endl;

    // Test: bind_and_query — "What can eagles do?" (IS_A → CAN_DO)
    int result_ids[10];
    float result_vals[10];
    int n_results = g.bind_and_query(eagle, EDGE_IS_A, EDGE_CAN_DO, result_ids, result_vals, 10);
    cout << "  bind_and_query(eagle, IS_A, CAN_DO): " << n_results << " results" << endl;
    bool found_fly = false;
    for (int i = 0; i < n_results; i++) {
        if (result_ids[i] == fly_node) found_fly = true;
    }
    cout << "  'fly' in results: " << (found_fly ? "[PASS]" : "[FAIL]") << endl;

    // Test edge type counts
    cout << "  IS_A edges: " << g.total_edges_of_type(EDGE_IS_A) << endl;
    cout << "  CAN_DO edges: " << g.total_edges_of_type(EDGE_CAN_DO) << endl;
    cout << "  HAS_A edges: " << g.total_edges_of_type(EDGE_HAS_A) << endl;

    // ============================================================
    // TEST 2: Fan-out Capped Saturation Stress Test
    // ============================================================
    cout << "\n[2/6] SATURATION FIX (Fan-out Cap = " << SPREAD_FANOUT_CAP << ")" << endl;
    cout << "---------------------------------------" << endl;

    ClusterGraph* satp = new ClusterGraph();
    ClusterGraph& sat = *satp;
    sat.init(INITIAL_CLUSTERS);

    mt19937 rng(42);
    for (int rep = 0; rep < 100; rep++) {
        for (int n = 0; n < INITIAL_CLUSTERS; n++) {
            int next = rng() % INITIAL_CLUSTERS;
            sat.record_fire(n);
            sat.record_fire(next);
            sat.reset_chain();
        }
    }
    cout << "  Total edges: " << sat.total_edges() << "/" << (INITIAL_CLUSTERS * MAX_FANOUT) << endl;

    vector<double> sat_lats;
    for (int n = 0; n < INITIAL_CLUSTERS; n++) {
        sat.clear_activation();
        auto s = high_resolution_clock::now();
        sat.spread_activation(n);
        auto e = high_resolution_clock::now();
        sat_lats.push_back(duration_cast<nanoseconds>(e - s).count() / 1000.0);
    }
    sort(sat_lats.begin(), sat_lats.end());
    double mean_lat = 0;
    for (auto v : sat_lats) mean_lat += v;
    mean_lat /= sat_lats.size();
    double p99_lat = sat_lats[(int)(sat_lats.size() * 0.99)];

    cout << "  Spread lat: " << fixed << setprecision(2) << mean_lat << " us (mean), "
         << p99_lat << " us (P99)" << endl;
    cout << "  Under 10us at saturation: " << (p99_lat < 10.0 ? "[PASS]" : (p99_lat < 50.0 ? "[IMPROVED]" : "[FAIL]")) << endl;

    // ============================================================
    // TEST 3: Native Lexer — Trie Lookup Speed
    // ============================================================
    cout << "\n[3/6] TRIE LEXICON: Lookup Speed" << endl;
    cout << "---------------------------------------" << endl;

    NativeLexer lexer;
    lexer.init();
    cout << "  Bootstrap words loaded: " << lexer.lexicon.word_count << endl;

    // Verify correctness
    bool correct = true;
    correct &= (lexer.lexicon.lookup("the") == POS_DET);
    correct &= (lexer.lexicon.lookup("eagle") == POS_NOUN);
    correct &= (lexer.lexicon.lookup("fly") == POS_VERB);
    correct &= (lexer.lexicon.lookup("big") == POS_ADJ);
    correct &= (lexer.lexicon.lookup("in") == POS_PREP);
    correct &= (lexer.lexicon.lookup("xyzzy123") == POS_UNKNOWN);
    cout << "  POS correctness: " << (correct ? "[PASS]" : "[FAIL]") << endl;

    // Speed test: 100K lookups
    auto t1 = high_resolution_clock::now();
    volatile POSTag dummy;
    for (int i = 0; i < 100000; i++) {
        dummy = lexer.lexicon.lookup("eagle");
    }
    auto t2 = high_resolution_clock::now();
    double ns_per = duration_cast<nanoseconds>(t2 - t1).count() / 100000.0;
    cout << "  Lookup latency: " << fixed << setprecision(1) << ns_per << " ns/word" << endl;

    // ============================================================
    // TEST 4: FSM Phrase Grouper
    // ============================================================
    cout << "\n[4/6] FSM PHRASE GROUPER" << endl;
    cout << "---------------------------------------" << endl;

    Token tokens[MAX_TOKENS];
    Phrase phrases[MAX_PHRASES];

    const char* test_sentence = "the big eagle can fly over the dark mountain";
    int n_tokens = lexer.tokenize(test_sentence, tokens);
    cout << "  Sentence: \"" << test_sentence << "\"" << endl;
    cout << "  Tokens: " << n_tokens << endl;
    for (int t = 0; t < n_tokens; t++) {
        cout << "    [" << tokens[t].text << " -> " << pos_tag_name(tokens[t].tag) << "]" << endl;
    }

    int n_phrases = lexer.group_phrases(tokens, n_tokens, phrases);
    cout << "  Phrases: " << n_phrases << endl;
    for (int p = 0; p < n_phrases; p++) {
        const char* type_names[] = {"NP", "VP", "PP", "SVO"};
        cout << "    " << type_names[phrases[p].type] << " head=\""
             << tokens[phrases[p].head_token].text << "\" [" 
             << phrases[p].start_token << ":" << phrases[p].end_token << "]" << endl;
    }

    // ============================================================
    // TEST 5: Full Sentence Ingestion Pipeline
    // ============================================================
    cout << "\n[5/6] SENTENCE INGESTION PIPELINE" << endl;
    cout << "---------------------------------------" << endl;

    ClusterGraph* lang_gp = new ClusterGraph();
    ClusterGraph& lang_g = *lang_gp;
    lang_g.init(6500);
    SpikingTokenizer stok;
    LanguageCortex* lcxp = new LanguageCortex();
    LanguageCortex& lcx = *lcxp;
    lcx.init();

    const char* sentences[] = {
        "the eagle can fly",
        "the dog can run",
        "the bird has a wing",
        "the cat is a small animal",
        "the big fish can swim",
        nullptr
    };

    int total_triples = 0;
    auto ingest_start = high_resolution_clock::now();
    for (int s = 0; sentences[s]; s++) {
        int t = lexer.ingest_sentence(sentences[s], &lang_g, &stok, &lcx);
        cout << "  \"" << sentences[s] << "\" -> " << t << " triples" << endl;
        total_triples += t;
    }
    auto ingest_end = high_resolution_clock::now();
    double ingest_us = duration_cast<microseconds>(ingest_end - ingest_start).count();

    cout << "  Total triples: " << total_triples << endl;
    cout << "  Ingestion time: " << fixed << setprecision(1) << ingest_us << " us" << endl;
    cout << "  NEXT_WORD edges: " << lang_g.total_edges_of_type(EDGE_NEXT_WORD) << endl;
    cout << "  PHRASE_HEAD edges: " << lang_g.total_edges_of_type(EDGE_PHRASE_HEAD) << endl;
    cout << "  PHRASE_CHILD edges: " << lang_g.total_edges_of_type(EDGE_PHRASE_CHILD) << endl;
    cout << "  CAN_DO edges: " << lang_g.total_edges_of_type(EDGE_CAN_DO) << endl;
    cout << "  Active language clusters: " << lcx.active_count() << endl;

    // ============================================================
    // TEST 6: Cross-Modal Query ("What can eagles do?")
    // ============================================================
    cout << "\n[6/6] CROSS-MODAL QUERY" << endl;
    cout << "---------------------------------------" << endl;

    // Find eagle's cluster
    int8_t eagle_hash[256];
    stok.encode_word_hash(string("eagle"), eagle_hash);
    int eagle_cid = lcx.perceive(eagle_hash, false);
    cout << "  'eagle' cluster ID: " << eagle_cid << endl;

    if (eagle_cid >= 0) {
        // Spread along CAN_DO from eagle
        lang_g.clear_activation();
        lang_g.spread_typed(eagle_cid, EDGE_CAN_DO);
        int top_ids[5]; float top_vals[5];
        int n_top = lang_g.get_top_activated(top_ids, top_vals, 5);
        cout << "  CAN_DO spread from eagle: " << n_top << " nodes activated" << endl;

        // Also try NEXT_WORD chain
        lang_g.clear_activation();
        lang_g.spread_typed(eagle_cid, EDGE_NEXT_WORD);
        n_top = lang_g.get_top_activated(top_ids, top_vals, 5);
        cout << "  NEXT_WORD spread from eagle: " << n_top << " nodes activated" << endl;
    }

    // ============================================================
    // SCORECARD
    // ============================================================
    cout << "\n================================================================" << endl;
    cout << " PHASE 10 + 10B SCORECARD" << endl;
    cout << "================================================================" << endl;
    cout << "| Typed Edge IS_A isolation            |  " << (!fly_leaked ? "PASS" : "FAIL") << endl;
    cout << "| Compositional bind_and_query          |  " << (found_fly ? "PASS" : "FAIL") << endl;
    cout << "| Saturation P99 < 50us (capped)        |  " << (p99_lat < 50.0 ? "PASS" : "FAIL")
         << " (" << fixed << setprecision(1) << p99_lat << " us)" << endl;
    cout << "| Trie lookup < 100ns                   |  " << (ns_per < 100.0 ? "PASS" : "FAIL")
         << " (" << fixed << setprecision(1) << ns_per << " ns)" << endl;
    cout << "| POS tag correctness                   |  " << (correct ? "PASS" : "FAIL") << endl;
    cout << "| Sentence ingestion                    |  " << (total_triples > 0 ? "PASS" : "FAIL")
         << " (" << total_triples << " triples)" << endl;
    cout << "================================================================" << endl;

    lexer.destroy();
    delete gp;
    delete satp;
    delete lang_gp;
    delete lcxp;
    return 0;
}
