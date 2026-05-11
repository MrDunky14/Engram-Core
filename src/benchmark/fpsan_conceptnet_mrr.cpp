// FP-SAN Phase 5C: Academic Benchmarks
// Computes Hits@10 and Mean Reciprocal Rank (MRR) on a 10,000 edge subset of ConceptNet 5.7.0
// Compile: cl /std:c++17 /O2 /EHsc /I src\core src\benchmark\fpsan_conceptnet_mrr.cpp /Fe:build\conceptnet_mrr.exe

#define NOMINMAX
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <map>
#include <set>
#include <algorithm>
#include <random>

#include "fpsan_core.h"
#include "fpsan_language.h"

using namespace std;
using namespace std::chrono;

struct Fact {
    string subj;
    string rel;
    string obj;
};

int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN PHASE 5C: ACADEMIC KNOWLEDGE GRAPH BENCHMARK" << endl;
    cout << " Dataset: ConceptNet 5.7.0 (10,000 edges)" << endl;
    cout << " Metrics: Hits@10, Mean Reciprocal Rank (MRR)" << endl;
    cout << "================================================================\n" << endl;

    vector<Fact> facts;
    ifstream file("data/conceptnet_10k_real.csv");
    if (!file.is_open()) {
        cerr << "Error: Could not open data/conceptnet_10k_real.csv" << endl;
        return 1;
    }

    string line;
    while (getline(file, line)) {
        stringstream ss(line);
        string s, r, o;
        if (getline(ss, s, ',') && getline(ss, r, ',') && getline(ss, o, ',')) {
            facts.push_back({s, r, o});
        }
    }
    cout << "[Dataset] Loaded " << facts.size() << " real-world ConceptNet facts." << endl;

    // Train/Test Split (90/10)
    mt19937 g(42);
    shuffle(facts.begin(), facts.end(), g);
    
    int split_idx = facts.size() * 0.9;
    vector<Fact> train_facts(facts.begin(), facts.begin() + split_idx);
    vector<Fact> test_facts(facts.begin() + split_idx, facts.end());
    
    cout << "  -> Train set: " << train_facts.size() << " facts" << endl;
    cout << "  -> Test set:  " << test_facts.size() << " facts" << endl;

    // Initialize Engine (Heap allocated due to 5000 cluster scale)
    SpikingTokenizer tokenizer;
    LanguageCortex* cortex = new LanguageCortex();
    ClusterGraph* graph = new ClusterGraph();
    
    cortex->init();
    graph->init();

    cout << "\n[System] Engine initialized. CORTEX_CAPACITY = " << CORTEX_CAPACITY << ", MAX_FANOUT = " << MAX_FANOUT << endl;

    map<string, int> word_to_id;
    auto perceive_word = [&](const string& word, bool learn) -> int {
        int8_t hash[LANG_WORD_DIM];
        tokenizer.encode_word_hash(word, hash);
        int cid = cortex->perceive(hash, learn);
        if (cid >= 0) word_to_id[word] = cid;
        return cid;
    };

    cout << "\n[1/3] Vocabulary Acquisition..." << endl;
    set<string> unique_words;
    for (const auto& f : train_facts) {
        unique_words.insert(f.subj);
        unique_words.insert(f.rel);
        unique_words.insert(f.obj);
    }
    cout << "  -> Extracted " << unique_words.size() << " unique concepts." << endl;
    
    auto t_vocab0 = high_resolution_clock::now();
    for (int rep = 0; rep < 5; rep++) {
        for (const auto& w : unique_words) {
            perceive_word(w, true);
        }
        if (rep == 0) cout << "    -> Epoch 1 done (" << cortex->active_count() << " clusters formed)..." << endl;
    }
    auto t_vocab1 = high_resolution_clock::now();
    double vocab_sec = duration_cast<milliseconds>(t_vocab1 - t_vocab0).count() / 1000.0;
    
    int vocab_size = cortex->active_count();
    cout << "  -> Formed " << vocab_size << " unique vocabulary clusters in " << vocab_sec << "s." << endl;

    cout << "\n[2/3] Knowledge Graph Construction..." << endl;
    auto t0 = high_resolution_clock::now();
    for (const auto& f : train_facts) {
        int id_s = perceive_word(f.subj, false);
        int id_r = perceive_word(f.rel, false);
        int id_o = perceive_word(f.obj, false);
        
        if (id_s >= 0 && id_r >= 0 && id_o >= 0) {
            graph->reset_chain();
            graph->record_fire(id_s);
            graph->record_fire(id_r);
            graph->record_fire(id_o);
        }
    }
    auto t1 = high_resolution_clock::now();
    double build_ms = duration_cast<milliseconds>(t1 - t0).count();
    cout << "  -> Graph built in " << build_ms << " ms." << endl;

    cout << "\n[3/3] Evaluating MRR and Hits@10..." << endl;
    
    int hits_at_10 = 0;
    double mrr_sum = 0.0;
    int evaluated = 0;
    double total_latency_us = 0.0;
    
    // We will evaluate both Forward (Subj+Rel -> ?) and Inverse (? <- Rel+Obj) queries
    for (const auto& f : test_facts) {
        if (word_to_id.count(f.subj) == 0 || word_to_id.count(f.rel) == 0 || word_to_id.count(f.obj) == 0) {
            continue; // Skip facts containing words not seen in train
        }
        
        int s_id = word_to_id[f.subj];
        int r_id = word_to_id[f.rel];
        int o_id = word_to_id[f.obj];
        
        // --- FORWARD QUERY: Subject + Relation -> Predict Object ---
        graph->clear_activation();
        auto t2 = high_resolution_clock::now();
        graph->spread_activation(s_id);
        graph->spread_activation(r_id);
        auto t3 = high_resolution_clock::now();
        total_latency_us += duration_cast<nanoseconds>(t3 - t2).count() / 1000.0;
        
        int top_nodes[150]; float top_vals[150];
        graph->get_top_activated(top_nodes, top_vals, 150);
        
        int rank = -1;
        for (int i = 0; i < 150; i++) {
            if (top_nodes[i] == o_id && top_vals[i] > 0) {
                rank = i + 1;
                break;
            }
        }
        
        if (rank > 0) {
            mrr_sum += 1.0 / rank;
            if (rank <= 10) hits_at_10++;
        }
        evaluated++;
        
        // --- INVERSE QUERY: Object + Relation -> Predict Subject ---
        graph->clear_activation();
        t2 = high_resolution_clock::now();
        graph->spread_activation_inverse(o_id);
        graph->spread_activation_inverse(r_id); // Relation spreads backward too
        t3 = high_resolution_clock::now();
        total_latency_us += duration_cast<nanoseconds>(t3 - t2).count() / 1000.0;
        
        graph->get_top_activated(top_nodes, top_vals, 150);
        
        rank = -1;
        for (int i = 0; i < 150; i++) {
            if (top_nodes[i] == s_id && top_vals[i] > 0) {
                rank = i + 1;
                break;
            }
        }
        
        if (rank > 0) {
            mrr_sum += 1.0 / rank;
            if (rank <= 10) hits_at_10++;
        }
        evaluated++;
        
        if (evaluated % 200 == 0) {
            cout << "  Evaluated " << evaluated << " queries..." << endl;
        }
    }
    
    double mrr = mrr_sum / evaluated;
    double hits_10_pct = (double)hits_at_10 / evaluated * 100.0;
    double avg_latency = total_latency_us / evaluated;

    cout << "\n================================================================" << endl;
    cout << " FINAL METRICS" << endl;
    cout << "================================================================" << endl;
    cout << " Total Queries: " << evaluated << endl;
    cout << " Hits@10:       " << fixed << setprecision(2) << hits_10_pct << " %" << endl;
    cout << " MRR:           " << fixed << setprecision(4) << mrr << endl;
    cout << " Avg Latency:   " << fixed << setprecision(2) << avg_latency << " us / query" << endl;
    cout << "================================================================\n" << endl;

    delete cortex;
    delete graph;

    return 0;
}
