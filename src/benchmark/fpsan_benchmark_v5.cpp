// FP-SAN v17 — full system benchmark v5 (legacy harness)
// Hierarchy v3 (overlapping patches) + Reasoning Stress Tests
// Compile: cl /std:c++17 /O2 /EHsc /I src\core src\benchmark\fpsan_benchmark_v5.cpp /Fe:build\benchmark_v5.exe

#define NOMINMAX
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <iomanip>
#include <map>
#include <cstring>
#include <cstdint>

#include "fpsan_core.h"
#include "fpsan_hierarchy.h"
#include "cluster_graph.h"

using namespace std;
using namespace std::chrono;

struct Dataset {
    vector<vector<int8_t>> spikes;
    vector<int> labels;
    int count = 0;
    bool load_data(const string& path, int max_samples = -1) {
        ifstream f(path);
        if (!f.is_open()) { cerr << "[ERROR] Cannot open " << path << endl; return false; }
        string line;
        while (getline(f, line)) {
            if (max_samples > 0 && count >= max_samples) break;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            stringstream ss(line); string val;
            vector<int8_t> sv;
            while (getline(ss, val, ',')) {
                float fval = stof(val);
                sv.push_back(fval > 0.5f ? (int8_t)1 : (int8_t)0);
            }
            sv.resize(784, 0);
            spikes.push_back(sv); count++;
        }
        return count > 0;
    }
    bool load_labels(const string& path) {
        ifstream f(path);
        if (!f.is_open()) return false;
        string line;
        while (getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            labels.push_back((int)stof(line));
        }
        return !labels.empty();
    }
};

float compute_purity(const vector<int>& asgn, const vector<int>& labels, int nc) {
    if (asgn.empty()) return 0;
    int correct = 0;
    for (int c = 0; c < nc; c++) {
        map<int,int> lc;
        for (int i = 0; i < (int)asgn.size(); i++) if (asgn[i] == c) lc[labels[i]]++;
        int mx = 0; for (auto& p : lc) mx = max(mx, p.second);
        correct += mx;
    }
    return (float)correct / asgn.size() * 100.0f;
}

int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN v17 | full system benchmark v5" << endl;
    cout << " Overlapping V1/V2 + Reasoning Stress Tests" << endl;
    cout << "================================================================\n" << endl;

    Dataset mnist;
    if (!mnist.load_data("data/mnist_stream.csv", 10000)) return 1;
    if (!mnist.load_labels("data/mnist_labels.csv")) return 1;
    int N = min((int)mnist.spikes.size(), (int)mnist.labels.size());
    cout << "[Data] " << N << " samples loaded.\n" << endl;

    auto wall_start = high_resolution_clock::now();

    // =========================================================
    // 1. FLAT PERCEPTION BASELINE
    // =========================================================
    cout << "[1/8] FLAT PERCEPTION (784-dim)" << endl;
    cout << "---------------------------------------" << endl;
    auto* flat = new CognitiveCore();
    flat->boot(false);
    vector<int> flat_a(N);
    for (int i = 0; i < N; i++) flat_a[i] = flat->perception.perceive(mnist.spikes[i].data(), true);
    for (int i = 0; i < N; i++) flat_a[i] = flat->perception.perceive(mnist.spikes[i].data(), true);
    float flat_pur = compute_purity(flat_a, mnist.labels, CORE_CLUSTER_DIM);
    cout << "  Purity: " << fixed << setprecision(1) << flat_pur << "%" << endl;
    cout << endl;

    // =========================================================
    // 2. HIERARCHICAL PERCEPTION (Overlapping V1/V2)
    // =========================================================
    cout << "[2/8] HIERARCHICAL PERCEPTION (25 overlapping 7x7, stride 4)" << endl;
    cout << "---------------------------------------" << endl;
    auto* hier = new HierarchicalCortex();
    hier->init();

    auto t0 = high_resolution_clock::now();
    vector<int> hier_a(N);
    for (int i = 0; i < N; i++) hier_a[i] = hier->perceive(mnist.spikes[i].data(), true);
    for (int i = 0; i < N; i++) hier_a[i] = hier->perceive(mnist.spikes[i].data(), true);
    auto t1 = high_resolution_clock::now();

    float hier_pur = compute_purity(hier_a, mnist.labels, V2_CLUSTER_DIM);
    double hier_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

    hier->print_stats();
    cout << "  Purity: " << fixed << setprecision(1) << hier_pur << "%" << endl;
    cout << "  Train:  " << fixed << setprecision(0) << hier_ms << " ms" << endl;

    // Latency
    vector<double> hier_lats;
    for (int i = 0; i < 1000; i++) {
        auto s = high_resolution_clock::now();
        hier->perceive(mnist.spikes[i % N].data(), false);
        auto e = high_resolution_clock::now();
        hier_lats.push_back(duration_cast<nanoseconds>(e - s).count() / 1000.0);
    }
    sort(hier_lats.begin(), hier_lats.end());
    double hier_mean = 0; for (auto l : hier_lats) hier_mean += l; hier_mean /= hier_lats.size();
    cout << "  Latency: " << fixed << setprecision(1) << hier_mean << " us (mean)" << endl;
    cout << "  Deploy:  " << hier->deployed_bytes() / 1024 << " KB" << endl;
    cout << endl;

    // =========================================================
    // 3. CATASTROPHIC FORGETTING (Hierarchical)
    // =========================================================
    cout << "[3/8] CATASTROPHIC FORGETTING (Hierarchical)" << endl;
    cout << "---------------------------------------" << endl;
    auto* hf = new HierarchicalCortex();
    hf->init();
    vector<int> gA, gB;
    for (int i = 0; i < N; i++) { if (mnist.labels[i] <= 4) gA.push_back(i); else gB.push_back(i); }

    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < (int)gA.size(); i++) hf->perceive(mnist.spikes[gA[i]].data(), true);

    vector<int> a_orig(gA.size());
    for (int i = 0; i < (int)gA.size(); i++) a_orig[i] = hf->perceive(mnist.spikes[gA[i]].data(), false);

    for (int i = 0; i < (int)gB.size(); i++) hf->perceive(mnist.spikes[gB[i]].data(), true);

    int retained = 0;
    for (int i = 0; i < (int)gA.size(); i++) {
        int c = hf->perceive(mnist.spikes[gA[i]].data(), false);
        if (c == a_orig[i] && c != -1) retained++;
    }
    float retention = (float)retained / gA.size() * 100.0f;
    cout << "  V1 frozen: " << hf->total_v1_frozen() << " | V2 frozen: " << hf->v2_frozen() << endl;
    cout << "  Retention: " << fixed << setprecision(1) << retention << "%" << endl;
    cout << endl;
    delete hf;

    // =========================================================
    // 4. REASONING: LONG CHAIN PREDICTION
    // =========================================================
    cout << "[4/8] REASONING: Long Chain (10 nodes)" << endl;
    cout << "---------------------------------------" << endl;
    ClusterGraph rg;
    rg.init();
    // Build chain 0→1→2→...→9
    for (int rep = 0; rep < 20; rep++)
        for (int n = 0; n < 10; n++) rg.record_fire(n);

    int pred[10];
    int plen = rg.predict_chain(0, pred, 9);
    cout << "  Chain: 0";
    for (int i = 0; i < plen; i++) cout << "->" << pred[i];
    bool chain_correct = (plen >= 9);
    for (int i = 0; i < plen && i < 9; i++) if (pred[i] != i + 1) chain_correct = false;
    cout << (chain_correct ? "  [CORRECT]" : "  [WRONG]") << endl;

    // =========================================================
    // 5. REASONING: COMPETING PATHS (Diamond)
    // =========================================================
    cout << "\n[5/8] REASONING: Competing Paths (Diamond A->B,C->D)" << endl;
    cout << "---------------------------------------" << endl;
    ClusterGraph diamond;
    diamond.init();
    // Build: A(10)->B(11)->D(13) and A(10)->C(12)->D(13)
    for (int rep = 0; rep < 20; rep++) {
        diamond.record_fire(10); diamond.record_fire(11); diamond.record_fire(13);
        diamond.reset_chain();
        diamond.record_fire(10); diamond.record_fire(12); diamond.record_fire(13);
        diamond.reset_chain();
    }

    int plan[10];
    int plan_len = diamond.plan_path(10, 13, plan, 10);
    cout << "  BFS 10->13: ";
    if (plan_len > 0) {
        cout << "10";
        for (int i = 0; i < plan_len; i++) cout << "->" << plan[i];
        cout << "  (length=" << plan_len << ")";
    } else {
        cout << "NO PATH";
    }
    bool diamond_ok = (plan_len == 2); // Should find 2-hop path
    cout << (diamond_ok ? "  [CORRECT]" : "  [CHECK]") << endl;

    // =========================================================
    // 6. REASONING: CREDIT DECAY
    // =========================================================
    cout << "\n[6/8] REASONING: Credit Decay (TD-lambda)" << endl;
    cout << "---------------------------------------" << endl;
    ClusterGraph credit_g;
    credit_g.init();
    // Build chain 0→1→2→3→4
    for (int rep = 0; rep < 10; rep++)
        for (int n = 0; n < 5; n++) credit_g.record_fire(n);

    // Record edge weights before credit
    float w_before_01 = credit_g.node(0).edges[0].weight;
    float w_before_34 = credit_g.node(3).edges[0].weight;

    credit_g.assign_credit(1.0f, 0.9f);

    float w_after_01 = credit_g.node(0).edges[0].weight;
    float w_after_34 = credit_g.node(3).edges[0].weight;

    cout << "  Edge 0->1: " << fixed << setprecision(3) << w_before_01 << " -> " << w_after_01 << endl;
    cout << "  Edge 3->4: " << fixed << setprecision(3) << w_before_34 << " -> " << w_after_34 << endl;
    bool credit_ok = (w_after_34 > w_after_01); // Node 3 closer to reward, should get more credit
    cout << "  Closer nodes get more credit: " << (credit_ok ? "[CORRECT]" : "[WRONG]") << endl;

    // =========================================================
    // 7. REASONING: GRAPH SATURATION
    // =========================================================
    cout << "\n[7/8] REASONING: Graph Saturation (100 nodes, max edges)" << endl;
    cout << "---------------------------------------" << endl;
    ClusterGraph sat;
    sat.init();
    mt19937 rng(42);
    // Fill all 100 nodes with random edges
    for (int rep = 0; rep < 100; rep++) {
        for (int n = 0; n < INITIAL_CLUSTERS; n++) {
            int next = rng() % INITIAL_CLUSTERS;
            sat.record_fire(n);
            sat.record_fire(next);
            sat.reset_chain();
        }
    }
    cout << "  Total edges: " << sat.total_edges() << "/" << (INITIAL_CLUSTERS * MAX_FANOUT) << endl;

    // Stress test: spread from every node, measure worst-case latency
    vector<double> sat_lats;
    for (int n = 0; n < INITIAL_CLUSTERS; n++) {
        sat.clear_activation();
        auto s = high_resolution_clock::now();
        sat.spread_activation(n);
        auto e = high_resolution_clock::now();
        sat_lats.push_back(duration_cast<nanoseconds>(e - s).count() / 1000.0);
    }
    sort(sat_lats.begin(), sat_lats.end());
    double sat_mean = 0; for (auto l : sat_lats) sat_mean += l; sat_mean /= sat_lats.size();
    cout << "  Spread lat: " << fixed << setprecision(2) << sat_mean << " us (mean), "
         << sat_lats[98] << " us (P99)" << endl;
    bool sat_ok = (sat_lats[98] < 10.0); // Must be under 10 us even at saturation
    cout << "  Under 10us at saturation: " << (sat_ok ? "[PASS]" : "[FAIL]") << endl;

    // =========================================================
    // 8. REASONING: ADVERSARIAL NOISE
    // =========================================================
    cout << "\n[8/8] REASONING: Adversarial Noise Injection" << endl;
    cout << "---------------------------------------" << endl;
    ClusterGraph adv;
    adv.init();
    // Build strong chain 0→1→2→3→4 (50 reps)
    for (int rep = 0; rep < 50; rep++)
        for (int n = 0; n < 5; n++) adv.record_fire(n);

    float strong_weight = adv.node(0).edges[0].weight;

    // Inject 200 random noise edges
    for (int rep = 0; rep < 200; rep++) {
        int a = rng() % 5, b = rng() % INITIAL_CLUSTERS;
        adv.record_fire(a); adv.record_fire(b);
        adv.reset_chain();
    }

    // Verify the strong chain still dominates
    int adv_pred[5];
    int adv_plen = adv.predict_chain(0, adv_pred, 4);
    bool adv_chain_ok = (adv_plen >= 4 && adv_pred[0] == 1 && adv_pred[1] == 2 &&
                          adv_pred[2] == 3 && adv_pred[3] == 4);

    cout << "  Strong bond weight: " << fixed << setprecision(3) << strong_weight << endl;
    cout << "  After 200 noise injections:" << endl;
    cout << "  Predicted chain from 0: 0";
    for (int i = 0; i < adv_plen; i++) cout << "->" << adv_pred[i];
    cout << endl;
    cout << "  Strong chain survives noise: " << (adv_chain_ok ? "[PASS]" : "[FAIL]") << endl;
    cout << endl;

    // =========================================================
    // FINAL SCORECARD
    // =========================================================
    auto wall_end = high_resolution_clock::now();
    double total = duration_cast<milliseconds>(wall_end - wall_start).count() / 1000.0;

    cout << "================================================================" << endl;
    cout << " FULL SYSTEM SCORECARD v7" << endl;
    cout << "================================================================" << endl;
    cout << "| PERCEPTION                              |                    |" << endl;
    cout << "|-----------------------------------------|--------------------|" << endl;
    cout << "| Flat Purity (MNIST)                     | " << setw(14) << fixed << setprecision(1) << flat_pur << "%     |" << endl;
    cout << "| Hierarchical Purity (MNIST)             | " << setw(14) << hier_pur << "%     |" << endl;
    cout << "| Hierarchical Retention                  | " << setw(14) << retention << "%     |" << endl;
    cout << "| Hierarchical Latency                    | " << setw(12) << hier_mean << " us     |" << endl;
    cout << "| Hierarchical Deploy                     | " << setw(12) << hier->deployed_bytes()/1024 << " KB     |" << endl;
    cout << "|-----------------------------------------|--------------------|" << endl;
    cout << "| REASONING                               |                    |" << endl;
    cout << "|-----------------------------------------|--------------------|" << endl;
    cout << "| Long Chain (10 nodes)                   | " << (chain_correct ? "    PASS     " : "    FAIL     ") << "      |" << endl;
    cout << "| Competing Paths (Diamond)               | " << (diamond_ok ? "    PASS     " : "    FAIL     ") << "      |" << endl;
    cout << "| Credit Decay (TD-lambda)                | " << (credit_ok ? "    PASS     " : "    FAIL     ") << "      |" << endl;
    cout << "| Graph Saturation (<10us)                | " << (sat_ok ? "    PASS     " : "    FAIL     ") << "      |" << endl;
    cout << "| Adversarial Noise Survival              | " << (adv_chain_ok ? "    PASS     " : "    FAIL     ") << "      |" << endl;
    cout << "================================================================" << endl;
    cout << " Time: " << fixed << setprecision(1) << total << "s" << endl;

    delete flat;
    delete hier;
    return 0;
}
