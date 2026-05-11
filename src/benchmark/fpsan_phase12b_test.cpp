// FP-SAN Phase 12B Benchmark
// Tests: Synaptic Serialization — Sleep/Wake roundtrip integrity
#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_memory.h"
#include <iostream>
#include <cstring>
#include <string>

using namespace std;

int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN v17 — Phase 12B benchmark (synaptic serialization)" << endl;
    cout << " Sleep/Wake Roundtrip | Topology Integrity | Cold Boot" << endl;
    cout << "================================================================" << endl;

    const char* brain_file = "build/jarvis_brain.fpsan";

    // ==============================
    // PHASE A: Build a brain, teach it, then put it to sleep
    // ==============================
    cout << "\n[1/5] BUILDING BRAIN (Training)" << endl;
    cout << "---------------------------------------" << endl;

    ClusterGraph* graph1 = new ClusterGraph();
    graph1->init(6500);

    SpikingTokenizer stok;

    LanguageCortex* cortex1 = new LanguageCortex();
    cortex1->init();

    NativeLexer lexer;
    lexer.init();
    lexer.lexicon.insert("hungry", POS_ADJ);
    lexer.lexicon.insert("hunt", POS_VERB);
    lexer.lexicon.insert("prey", POS_NOUN);
    lexer.lexicon.insert("looks", POS_VERB);
    lexer.lexicon.insert("food", POS_NOUN);
    lexer.lexicon.insert("fast", POS_ADV);
    lexer.lexicon.insert("small", POS_ADJ);
    lexer.lexicon.insert("run", POS_VERB);

    const char* corpus[] = {
        "the eagle can fly over the dark mountain",
        "the eagle can hunt small prey",
        "the dog can run fast",
        "the hungry eagle looks for food",
        nullptr
    };

    int total_triples = 0;
    for (int s = 0; corpus[s]; s++) {
        int t = lexer.ingest_sentence(corpus[s], graph1, &stok, cortex1);
        total_triples += t;
        cout << "  Ingested: \"" << corpus[s] << "\" (" << t << " triples)" << endl;
    }
    cout << "  Total triples: " << total_triples << endl;

    // Generate text BEFORE sleep to get a reference output
    int8_t eagle_hash[256];
    stok.encode_word_hash("eagle", eagle_hash);
    int eagle_cid_1 = cortex1->perceive(eagle_hash, false);

    char pre_sleep_output[1024] = "";
    int pre_sleep_words = 0;
    if (eagle_cid_1 >= 0) {
        graph1->clear_activation();
        pre_sleep_words = lexer.generate_text(eagle_cid_1, graph1, cortex1, pre_sleep_output, 15);
        cout << "  Pre-sleep generation: \"" << pre_sleep_output << "\" (" << pre_sleep_words << " words)" << endl;
    }

    // Count alive nodes and edges for verification
    int pre_alive = 0, pre_edges = 0;
    {
        int nc1 = graph1->node_count.load(std::memory_order_acquire);
        for (int i = 0; i < nc1; i++) {
            if (graph1->node(i).alive.load(std::memory_order_acquire)) {
                pre_alive++;
                pre_edges += graph1->node(i).edge_count.load(std::memory_order_acquire);
                pre_edges += graph1->node(i).inverse_edge_count.load(std::memory_order_acquire);
            }
        }
    }
    int pre_cortex_active = cortex1->active_count();

    // ==============================
    // PHASE B: SLEEP — Save to .fpsan
    // ==============================
    cout << "\n[2/5] SLEEP (Saving to .fpsan)" << endl;
    cout << "---------------------------------------" << endl;

    bool sleep_ok = SynapticMemory::sleep(brain_file, graph1, cortex1);
    cout << "  Sleep result: " << (sleep_ok ? "[PASS]" : "[FAIL]") << endl;

    // Destroy the original brain completely
    delete graph1;
    delete cortex1;
    graph1 = nullptr;
    cortex1 = nullptr;

    // ==============================
    // PHASE C: WAKE — Load from .fpsan into fresh memory
    // ==============================
    cout << "\n[3/5] WAKE (Loading from .fpsan)" << endl;
    cout << "---------------------------------------" << endl;

    ClusterGraph* graph2 = new ClusterGraph();
    LanguageCortex* cortex2 = new LanguageCortex();

    bool wake_ok = SynapticMemory::wake(brain_file, graph2, cortex2);
    cout << "  Wake result: " << (wake_ok ? "[PASS]" : "[FAIL]") << endl;

    // ==============================
    // PHASE D: INTEGRITY CHECK — Compare pre/post
    // ==============================
    cout << "\n[4/5] INTEGRITY CHECK" << endl;
    cout << "---------------------------------------" << endl;

    int post_alive = 0, post_edges = 0;
    {
        int nc2 = graph2->node_count.load(std::memory_order_acquire);
        for (int i = 0; i < nc2; i++) {
            if (graph2->node(i).alive.load(std::memory_order_acquire)) {
                post_alive++;
                post_edges += graph2->node(i).edge_count.load(std::memory_order_acquire);
                post_edges += graph2->node(i).inverse_edge_count.load(std::memory_order_acquire);
            }
        }
    }
    int post_cortex_active = cortex2->active_count();

    bool nodes_match = (pre_alive == post_alive);
    bool edges_match = (pre_edges == post_edges);
    bool cortex_match = (pre_cortex_active == post_cortex_active);

    cout << "  Alive nodes:  pre=" << pre_alive << " post=" << post_alive
         << "  " << (nodes_match ? "[PASS]" : "[FAIL]") << endl;
    cout << "  Total edges:  pre=" << pre_edges << " post=" << post_edges
         << "  " << (edges_match ? "[PASS]" : "[FAIL]") << endl;
    cout << "  Cortex slots: pre=" << pre_cortex_active << " post=" << post_cortex_active
         << "  " << (cortex_match ? "[PASS]" : "[FAIL]") << endl;

    // Verify reverse string lookup survived
    const char* eagle_word = cortex2->get_word(eagle_cid_1);
    bool word_survived = (strcmp(eagle_word, "eagle") == 0);
    cout << "  Word lookup 'eagle': \"" << eagle_word << "\"  " << (word_survived ? "[PASS]" : "[FAIL]") << endl;

    // ==============================
    // PHASE E: COLD BOOT GENERATION — Can the woken brain still speak?
    // ==============================
    cout << "\n[5/5] COLD BOOT GENERATION" << endl;
    cout << "---------------------------------------" << endl;

    // Verify all activations are zero (cold boot)
    bool cold_boot = true;
    {
        int nc2 = graph2->node_count.load(std::memory_order_acquire);
        for (int i = 0; i < nc2; i++) {
            if (graph2->node(i).activation.load(std::memory_order_acquire) != 0.0f) {
                cold_boot = false;
                break;
            }
        }
    }
    cout << "  All activations at 0.0 (cold boot): " << (cold_boot ? "[PASS]" : "[FAIL]") << endl;

    // Now generate from the woken brain
    char post_sleep_output[1024] = "";
    int post_sleep_words = 0;
    if (eagle_cid_1 >= 0) {
        graph2->clear_activation();
        post_sleep_words = lexer.generate_text(eagle_cid_1, graph2, cortex2, post_sleep_output, 15);
        cout << "  Post-wake generation: \"" << post_sleep_output << "\" (" << post_sleep_words << " words)" << endl;
    }

    bool gen_match = (strcmp(pre_sleep_output, post_sleep_output) == 0);
    cout << "  Output matches pre-sleep: " << (gen_match ? "[PASS]" : "[FAIL]") << endl;

    // ==============================
    // SCORECARD
    // ==============================
    cout << "\n================================================================" << endl;
    cout << " PHASE 12B SCORECARD" << endl;
    cout << "================================================================" << endl;
    cout << "| Binary Sleep (.fpsan write)            |  " << (sleep_ok ? "PASS" : "FAIL") << endl;
    cout << "| Binary Wake (.fpsan read)              |  " << (wake_ok ? "PASS" : "FAIL") << endl;
    cout << "| Node count integrity                   |  " << (nodes_match ? "PASS" : "FAIL") << endl;
    cout << "| Edge count integrity                   |  " << (edges_match ? "PASS" : "FAIL") << endl;
    cout << "| Cortex cluster integrity               |  " << (cortex_match ? "PASS" : "FAIL") << endl;
    cout << "| Reverse word lookup survived           |  " << (word_survived ? "PASS" : "FAIL") << endl;
    cout << "| Cold boot (zero activation)            |  " << (cold_boot ? "PASS" : "FAIL") << endl;
    cout << "| Generation output matches pre-sleep    |  " << (gen_match ? "PASS" : "FAIL") << endl;
    cout << "================================================================" << endl;

    lexer.destroy();
    delete graph2;
    delete cortex2;

    return 0;
}
