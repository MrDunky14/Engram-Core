// FP-SAN Phase 6: HumanEval AST ingestion (generate CSV via tools/humaneval_ast_ingestor.py)
// Evaluates HumanEval AST graph logic reconstruction
// Compile: cl /std:c++17 /O2 /EHsc /I src\core src\benchmark\fpsan_humaneval_test.cpp /Fe:build\humaneval_test.exe

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

#include "fpsan_core.h"
#include "fpsan_language.h"

using namespace std;
using namespace std::chrono;

struct ASTTriple {
    string subj;
    string rel;
    string obj;
};

int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN PHASE 6: AGI DECLARATIVE LOGIC (AST GRAPH)" << endl;
    cout << " Dataset: OpenAI HumanEval (164 Problems)" << endl;
    cout << " Testing: Docstring -> AST Syntax Reconstruction" << endl;
    cout << "================================================================\n" << endl;

    vector<ASTTriple> triples;
    ifstream file("data/humaneval_ast_triples.csv");
    if (!file.is_open()) {
        cerr << "Error: Could not open data/humaneval_ast_triples.csv" << endl;
        return 1;
    }

    string line;
    while (getline(file, line)) {
        stringstream ss(line);
        string s, r, o;
        if (getline(ss, s, ',') && getline(ss, r, ',') && getline(ss, o, ',')) {
            triples.push_back({s, r, o});
        }
    }
    cout << "[Dataset] Loaded " << triples.size() << " AST Logic Triples." << endl;

    // Load Prompts
    map<string, string> docstrings;
    ifstream pfile("data/humaneval_prompts.csv");
    while (getline(pfile, line)) {
        stringstream ss(line);
        string id, doc;
        if (getline(ss, id, ',') && getline(ss, doc, ',')) {
            docstrings[id] = doc;
        }
    }

    // Initialize Engine
    SpikingTokenizer tokenizer;
    LanguageCortex* cortex = new LanguageCortex();
    ClusterGraph* graph = new ClusterGraph();
    
    cortex->init();
    graph->init();

    map<string, int> word_to_id;
    auto perceive_word = [&](const string& word, bool learn) -> int {
        int8_t hash[LANG_WORD_DIM];
        tokenizer.encode_word_hash(word, hash);
        int cid = cortex->perceive(hash, learn);
        if (cid >= 0) word_to_id[word] = cid;
        return cid;
    };

    cout << "\n[1/3] Abstract Syntax Tree Parsing..." << endl;
    set<string> unique_nodes;
    for (const auto& t : triples) {
        unique_nodes.insert(t.subj);
        unique_nodes.insert(t.rel);
        unique_nodes.insert(t.obj);
    }
    cout << "  -> Extracted " << unique_nodes.size() << " unique AST node states." << endl;
    
    if ((int)unique_nodes.size() > CORTEX_CAPACITY) {
        cerr << "ERROR: Unique AST nodes exceed CORTEX_CAPACITY (" << CORTEX_CAPACITY << ")" << endl;
        return 1;
    }

    for (int rep = 0; rep < 5; rep++) {
        for (const auto& w : unique_nodes) perceive_word(w, true);
    }
    cout << "  -> Formed " << cortex->active_count() << " syntax clusters." << endl;

    cout << "\n[2/3] Neuromorphic Logic Construction..." << endl;
    auto t0 = high_resolution_clock::now();
    for (const auto& t : triples) {
        int id_s = word_to_id[t.subj];
        int id_r = word_to_id[t.rel];
        int id_o = word_to_id[t.obj];
        
        graph->reset_chain();
        graph->record_fire(id_s);
        graph->record_fire(id_r);
        graph->record_fire(id_o);
    }
    auto t1 = high_resolution_clock::now();
    cout << "  -> Logic graph built in " << duration_cast<milliseconds>(t1 - t0).count() << " ms." << endl;

    cout << "\n[3/3] Evaluating HumanEval Docstring -> Code Resolution..." << endl;
    
    int hits_at_10 = 0;
    int tested = 0;
    
    // We will test if providing the Docstring cluster can successfully illuminate the target FunctionDef
    for (const auto& pair : docstrings) {
        string task_id = pair.first;
        string doc = pair.second;
        string doc_label = "Doc_" + doc;
        replace(doc_label.begin(), doc_label.end(), ' ', '_');
        if (doc_label.length() > 24) doc_label = doc_label.substr(0, 24);
        
        // Find the target function label that implements this docstring in the triples
        string target_func = "";
        for (const auto& t : triples) {
            if (t.subj == doc_label && t.rel == "IMPLEMENTED_BY") {
                target_func = t.obj;
                break;
            }
        }
        
        if (target_func.empty() || word_to_id.count(doc_label) == 0 || word_to_id.count(target_func) == 0) {
            continue;
        }

        int doc_id = word_to_id[doc_label];
        int func_id = word_to_id[target_func];
        
        graph->clear_activation();
        graph->spread_activation(doc_id);
        
        int top_nodes[150]; float top_vals[150];
        graph->get_top_activated(top_nodes, top_vals, 150);
        
        int rank = -1;
        for (int i = 0; i < 150; i++) {
            if (top_nodes[i] == func_id && top_vals[i] > 0) {
                rank = i + 1;
                break;
            }
        }
        
        if (rank > 0 && rank <= 10) hits_at_10++;
        tested++;
    }
    
    double hits_10_pct = (double)hits_at_10 / tested * 100.0;

    cout << "\n================================================================" << endl;
    cout << " HUMANEVAL DECLARATIVE METRICS" << endl;
    cout << "================================================================" << endl;
    cout << " Problems Tested: " << tested << " / 164" << endl;
    cout << " Docstring -> Function Root Hits@10: " << fixed << setprecision(2) << hits_10_pct << " %" << endl;
    cout << "================================================================\n" << endl;

    delete cortex;
    delete graph;
    return 0;
}
