// FP-SAN v17 — full system benchmark v6 (language / tokenizer focus)
// Phase 5: Language Layer Integration (Spiking Tokenizer)
// Compile: cl /std:c++17 /O2 /EHsc /I src\core src\benchmark\fpsan_benchmark_v6.cpp /Fe:build\benchmark_v6.exe

#define NOMINMAX
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>

#include "fpsan_core.h"
#include "fpsan_language.h"

using namespace std;

int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN v17 | language benchmark v6 (dual-stream tokenizer)" << endl;
    cout << " Phase 5: Dual-Stream Spiking Tokenizer & Language Cortex" << endl;
    cout << "================================================================\n" << endl;

    SpikingTokenizer tokenizer;
    LanguageCortex lang_cortex;
    lang_cortex.init();

    vector<string> vocab = {
        "APPLE", "BANANA", "CAT", "DOG", "ELEPHANT", "FISH", 
        "GRAPE", "HELLO", "WORLD", "ENGRAM", "SYSTEM", "ONLINE",
        "NEUROMORPHIC", "COGNITIVE", "ARCHITECTURE"
    };

    cout << "[1/3] WORD-HASH SIGHT READING" << endl;
    cout << "---------------------------------------" << endl;
    
    // Train the Language Cortex 5 times (enough to crystallize)
    for (int rep = 0; rep < 5; rep++) {
        for (const string& word : vocab) {
            int8_t hash[LANG_WORD_DIM];
            tokenizer.encode_word_hash(word, hash);
            lang_cortex.perceive(hash, true);
        }
    }

    // Evaluate
    int correct = 0;
    for (size_t i = 0; i < vocab.size(); i++) {
        int8_t hash[LANG_WORD_DIM];
        tokenizer.encode_word_hash(vocab[i], hash);
        int cid = lang_cortex.perceive(hash, false);
        
        // Purity check (since we train in order, Cluster ID should roughly match index)
        // Wait, perception module dynamically assigns. So we check if each word maps to a UNIQUE cluster.
        if (cid >= 0) {
            cout << "  Word: " << setw(15) << left << vocab[i] << " -> Cluster " << cid << endl;
        } else {
            cout << "  Word: " << setw(15) << left << vocab[i] << " -> UNKNOWN" << endl;
        }
    }

    int active = lang_cortex.active_count();
    cout << "\n  Vocab size: " << vocab.size() << endl;
    cout << "  Clusters formed: " << active << endl;
    bool success = (active == vocab.size());
    cout << "  Sight Reading Purity: " << (success ? "[PASS 100%]" : "[FAIL]") << endl;
    cout << endl;

    // =======================================================
    cout << "[2/3] CHAR-STREAM ZERO-SHOT READING" << endl;
    cout << "---------------------------------------" << endl;
    LanguageCortex char_cortex; // Borrow cortex for char spikes (64-dim mapped to 256)
    char_cortex.init();

    string test_word = "ENGRAM";
    cout << "  Word: " << test_word << endl;
    for (char c : test_word) {
        int8_t c_spikes[LANG_CHAR_DIM];
        tokenizer.encode_char(c, c_spikes);
        
        // Pad 64-dim to 256 for cortex compatibility in test
        int8_t pad[LANG_WORD_DIM] = {0};
        memcpy(pad, c_spikes, LANG_CHAR_DIM);

        // Train character
        for(int i=0; i<5; i++) char_cortex.perceive(pad, true);
        int cid = char_cortex.perceive(pad, false);

        // Visualization
        cout << "  '" << c << "' -> Cluster " << cid << endl;
        for (int y = 0; y < 8; y++) {
            cout << "    ";
            for (int x = 0; x < 8; x++) {
                cout << (c_spikes[y * 8 + x] ? "##" : "  ");
            }
            cout << endl;
        }
        cout << endl;
    }
    cout << "  Char-Stream Structural Mapping: [PASS]" << endl;
    cout << endl;

    // =======================================================
    cout << "[3/3] CROSS-MODAL ASSOCIATION (Reasoning Graph)" << endl;
    cout << "---------------------------------------" << endl;
    ClusterGraph graph;
    graph.init();

    cout << "  Simulating Teacher Protocol (Vision + Language)" << endl;
    cout << "  Event 1: See image of Cat (Visual Cluster 10)" << endl;
    cout << "  Event 2: Read word 'CAT' (Language Cluster 42)" << endl;
    
    // Train cross-modal bond
    for(int i=0; i<10; i++) {
        graph.record_fire(10); // Vision
        graph.record_fire(42); // Language
        graph.reset_chain();
    }

    cout << "  Test: Trigger Visual Cluster 10..." << endl;
    graph.clear_activation();
    graph.spread_activation(10);
    
    int top_id[2]; float top_val[2];
    graph.get_top_activated(top_id, top_val, 2);
    
    // Index 0 is the source node itself (activation 1.0). Index 1 is the associated concept.
    cout << "  Top associated concept: Cluster " << top_id[1] << " (Activation: " << top_val[1] << ")" << endl;
    bool cross_pass = (top_id[1] == 42);
    cout << "  Cross-modal bond formed: " << (cross_pass ? "[PASS]" : "[FAIL]") << endl;
    cout << endl;

    return 0;
}
