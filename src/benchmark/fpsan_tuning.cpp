// FP-SAN PARAMETER SWEEP — Finding the optimal config for >=80% purity
// Tests multiple parameter combinations systematically
// Compile: cl /std:c++17 /O2 /EHsc /I src\core src\benchmark\fpsan_tuning.cpp /Fe:build\tuning.exe

#define NOMINMAX
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <map>
#include <cstring>
#include <cstdint>

using namespace std;

const int INPUT_DIM = 784;

struct Dataset {
    vector<vector<int8_t>> spikes;
    vector<int> labels;
    int count = 0;
    bool load_data(const string& path, int max_samples = -1) {
        ifstream f(path); if (!f.is_open()) return false;
        string line;
        while (getline(f, line)) {
            if (max_samples > 0 && count >= max_samples) break;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            stringstream ss(line); string val;
            vector<int8_t> sv;
            while (getline(ss, val, ',')) { float fv = stof(val); sv.push_back(fv > 0.5f ? 1 : 0); }
            sv.resize(INPUT_DIM, 0); spikes.push_back(sv); count++;
        }
        return count > 0;
    }
    bool load_labels(const string& path) {
        ifstream f(path); if (!f.is_open()) return false;
        string line;
        while (getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            labels.push_back((int)stof(line));
        }
        return !labels.empty();
    }
};

// Parameterized engine for sweep
struct TuneEngine {
    static const int MAX_C = 256;
    struct Cluster {
        int8_t weights[INPUT_DIM];
        float accum[INPUT_DIM];
        int sample_count;
        bool frozen;
        uint64_t last_spike;
    };

    Cluster* cortex;
    int num_clusters;
    float base_vig;
    float quant_thresh;
    int freeze_thresh;
    float cap_scale;

    TuneEngine() { cortex = new Cluster[MAX_C]; }
    ~TuneEngine() { delete[] cortex; }

    void init(int nc, float bv, float qt, int ft, float cs) {
        num_clusters = nc; base_vig = bv; quant_thresh = qt; freeze_thresh = ft; cap_scale = cs;
        for (int c = 0; c < num_clusters; c++) {
            memset(cortex[c].weights, 0, INPUT_DIM);
            memset(cortex[c].accum, 0, sizeof(cortex[c].accum));
            cortex[c].sample_count = 0;
            cortex[c].frozen = false;
            cortex[c].last_spike = 0;
        }
    }

    int active_count() {
        int n = 0;
        for (int c = 0; c < num_clusters; c++) if (cortex[c].last_spike > 0) n++;
        return n;
    }

    int perceive(const int8_t* input, bool learn) {
        int best = -1, first_empty = -1;
        float best_sim = -9999.0f;

        int active = active_count();
        float cap = (float)active / num_clusters;
        float vigilance = base_vig + cap * cap_scale;

        for (int c = 0; c < num_clusters; c++) {
            if (cortex[c].last_spike == 0) {
                if (first_empty == -1) first_empty = c;
                continue;
            }
            int score = 0, act = 0;
            for (int i = 0; i < INPUT_DIM; i++) {
                if (input[i] != 0 || cortex[c].weights[i] != 0) {
                    if (cortex[c].weights[i] == input[i]) score++; else score--;
                    act++;
                }
            }
            if (act > 0) {
                float sim = (float)score / act;
                if (sim > best_sim) { best_sim = sim; best = c; }
            }
        }

        if (best_sim < vigilance && first_empty != -1 && learn) best = first_empty;
        else if (best_sim < vigilance && !learn) return -1;

        if (learn && first_empty == -1 && best_sim < 0.3f) return best;

        if (best != -1 && learn && !cortex[best].frozen) {
            cortex[best].sample_count++;
            float lr = max(0.05f, 1.0f / (float)cortex[best].sample_count);
            for (int i = 0; i < INPUT_DIM; i++) {
                cortex[best].accum[i] += lr * ((float)input[i] - cortex[best].accum[i]);
                if (cortex[best].accum[i] > quant_thresh) cortex[best].weights[i] = 1;
                else if (cortex[best].accum[i] < -quant_thresh) cortex[best].weights[i] = -1;
                else cortex[best].weights[i] = 0;
            }
            cortex[best].last_spike = 1;
            if (cortex[best].sample_count >= freeze_thresh)
                cortex[best].frozen = true;
        }
        return best;
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
    cout << " FP-SAN PARAMETER SWEEP — Targeting >=80% Purity" << endl;
    cout << "================================================================\n" << endl;

    Dataset mnist;
    if (!mnist.load_data("data/mnist_stream.csv", 10000)) return 1;
    if (!mnist.load_labels("data/mnist_labels.csv")) return 1;
    int N = min((int)mnist.spikes.size(), (int)mnist.labels.size());
    cout << "[Data] " << N << " samples.\n" << endl;

    struct Config {
        int clusters; float base_vig; float quant_thresh; int freeze_thresh;
        float cap_scale; int passes;
        const char* name;
    };

    Config configs[] = {
        // Baseline
        {100, 0.05f, 0.35f, 15, 0.4f, 2, "Baseline (canonical)"},
        // More clusters
        {150, 0.05f, 0.35f, 15, 0.4f, 2, "150 clusters"},
        {200, 0.05f, 0.35f, 15, 0.4f, 2, "200 clusters"},
        // Lower quant threshold
        {100, 0.05f, 0.25f, 15, 0.4f, 2, "Quant=0.25"},
        {100, 0.05f, 0.30f, 15, 0.4f, 2, "Quant=0.30"},
        {150, 0.05f, 0.25f, 15, 0.4f, 2, "150c+Q0.25"},
        {150, 0.05f, 0.30f, 15, 0.4f, 2, "150c+Q0.30"},
        // More passes
        {100, 0.05f, 0.35f, 15, 0.4f, 3, "3 passes"},
        {100, 0.05f, 0.35f, 15, 0.4f, 4, "4 passes"},
        {150, 0.05f, 0.30f, 15, 0.4f, 3, "150c+Q0.30+3p"},
        {150, 0.05f, 0.25f, 15, 0.4f, 3, "150c+Q0.25+3p"},
        // Higher freeze
        {100, 0.05f, 0.35f, 25, 0.4f, 3, "Freeze=25+3p"},
        {150, 0.05f, 0.30f, 25, 0.4f, 3, "150c+Q0.30+F25+3p"},
        // Different vigilance
        {150, 0.03f, 0.30f, 20, 0.3f, 3, "150c+V0.03+Q0.30+3p"},
        {200, 0.03f, 0.25f, 20, 0.3f, 3, "200c+V0.03+Q0.25+3p"},
        {200, 0.03f, 0.30f, 25, 0.3f, 4, "200c+V0.03+Q0.30+F25+4p"},
    };
    int num_configs = sizeof(configs) / sizeof(configs[0]);

    float best_purity = 0;
    int best_idx = 0;

    cout << "| # | Config                         | Clusters | Purity  |" << endl;
    cout << "|---|--------------------------------|----------|---------|" << endl;

    for (int ci = 0; ci < num_configs; ci++) {
        Config& cfg = configs[ci];
        TuneEngine engine;
        engine.init(cfg.clusters, cfg.base_vig, cfg.quant_thresh, cfg.freeze_thresh, cfg.cap_scale);

        vector<int> assignments(N);
        for (int pass = 0; pass < cfg.passes; pass++) {
            for (int i = 0; i < N; i++)
                assignments[i] = engine.perceive(mnist.spikes[i].data(), true);
        }

        float purity = compute_purity(assignments, mnist.labels, cfg.clusters);
        int active = engine.active_count();

        cout << "| " << setw(1) << ci << " | " << setw(30) << left << cfg.name << " | "
             << setw(8) << right << active << " | "
             << fixed << setprecision(1) << setw(5) << purity << "% |" << endl;

        if (purity > best_purity) { best_purity = purity; best_idx = ci; }
    }

    cout << "\n================================================================" << endl;
    cout << " BEST CONFIG: " << configs[best_idx].name << endl;
    cout << " Purity: " << fixed << setprecision(1) << best_purity << "%" << endl;
    cout << " Clusters=" << configs[best_idx].clusters
         << " Vig=" << configs[best_idx].base_vig
         << " Quant=" << configs[best_idx].quant_thresh
         << " Freeze=" << configs[best_idx].freeze_thresh
         << " Passes=" << configs[best_idx].passes << endl;
    cout << "================================================================" << endl;

    return 0;
}
