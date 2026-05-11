// FP-SAN BENCHMARK v3 — PARAMETER TUNING + CLUSTER FREEZING
// Compile: cl /std:c++17 /O2 /EHsc src\benchmark\fpsan_benchmark_v3.cpp /Fe:build\benchmark_v3.exe
// New in v3:
//   - Cluster stiffness: mature clusters freeze their weights
//   - Adaptive vigilance: scales with cluster capacity
//   - Tuned learning rate: faster convergence
//   - Multi-pass training: 2 passes over data for stable prototypes

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

using namespace std;
using namespace std::chrono;

const int INPUT_DIM = 784;
const int CLUSTER_DIM = 100;
const float BASE_VIGILANCE = 0.05f;
const float BINARIZE_THRESH = 0.5f;
const float HOMEO_EXP = 1.5f;
const int FREEZE_THRESHOLD = 15;  // Cluster freezes after 15 samples

// ============================================================
// DATA LOADER
// ============================================================
struct Dataset {
    vector<vector<int8_t>> spikes;
    vector<vector<float>> raw;
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
            vector<float> fv; vector<int8_t> sv;
            while (getline(ss, val, ',')) {
                float fval = stof(val);
                fv.push_back(fval);
                sv.push_back(fval > BINARIZE_THRESH ? (int8_t)1 : (int8_t)0);
            }
            fv.resize(INPUT_DIM, 0.0f); sv.resize(INPUT_DIM, 0);
            raw.push_back(fv); spikes.push_back(sv); count++;
        }
        cout << "[Data] Loaded " << count << " samples from " << path << endl;
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
        cout << "[Data] Loaded " << labels.size() << " labels" << endl;
        return !labels.empty();
    }
};

// ============================================================
// FP-SAN ENGINE v3
// ============================================================
class FPSANEngine {
public:
    struct Cluster {
        int8_t weights[INPUT_DIM];
        float weight_accum[INPUT_DIM];   // Running average for soft learning
        int sample_count;
        bool frozen;                     // Once mature, weights are locked
        float membrane_potential;
        uint64_t last_spike_time;
        int fire_count;
    };

    Cluster cortex[CLUSTER_DIM];
    float expected_energy;
    bool use_homeostasis;
    float homeostasis_ratio;

    void reset(bool homeo = false) {
        use_homeostasis = homeo;
        expected_energy = (float)(INPUT_DIM) * 0.3f;
        homeostasis_ratio = 1.0f;
        for (int c = 0; c < CLUSTER_DIM; c++) {
            memset(cortex[c].weights, 0, INPUT_DIM);
            memset(cortex[c].weight_accum, 0, sizeof(cortex[c].weight_accum));
            cortex[c].sample_count = 0;
            cortex[c].frozen = false;
            cortex[c].membrane_potential = 0.0f;
            cortex[c].last_spike_time = 0;
            cortex[c].fire_count = 0;
        }
    }

    int active_clusters() {
        int n = 0;
        for (int c = 0; c < CLUSTER_DIM; c++)
            if (cortex[c].last_spike_time > 0) n++;
        return n;
    }

    int frozen_clusters() {
        int n = 0;
        for (int c = 0; c < CLUSTER_DIM; c++)
            if (cortex[c].frozen) n++;
        return n;
    }

    size_t memory_bytes_deployed() {
        // Deployed: only ternary weights + metadata
        return CLUSTER_DIM * (INPUT_DIM + sizeof(uint64_t) + sizeof(int));
    }

    int process(const vector<int8_t>& input, bool learning) {
        int best = -1, first_empty = -1;
        float best_sim = -9999.0f;

        // Homeostasis
        homeostasis_ratio = 1.0f;
        if (use_homeostasis) {
            float energy = 0;
            for (int i = 0; i < INPUT_DIM; i++) if (input[i] != 0) energy += 1.0f;
            expected_energy = 0.8f * expected_energy + 0.2f * energy;
            if (expected_energy > 0.1f) {
                homeostasis_ratio = energy / expected_energy;
                homeostasis_ratio = powf(homeostasis_ratio, HOMEO_EXP);
            }
            homeostasis_ratio = max(0.05f, min(homeostasis_ratio, 2.0f));
        }

        // ADAPTIVE VIGILANCE: scales with capacity pressure
        int active = active_clusters();
        float capacity_ratio = (float)active / CLUSTER_DIM;
        // As we fill up, become pickier about spawning new clusters
        float vigilance = BASE_VIGILANCE + capacity_ratio * 0.4f;
        vigilance *= homeostasis_ratio;

        // Find best matching cluster
        for (int c = 0; c < CLUSTER_DIM; c++) {
            if (cortex[c].last_spike_time == 0) {
                if (first_empty == -1) first_empty = c;
                continue;
            }
            int score = 0, act = 0;
            for (int i = 0; i < INPUT_DIM; i++) {
                if (input[i] != 0 || cortex[c].weights[i] != 0) {
                    // HOMEOSTASIS-AWARE MATCHING
                    if (use_homeostasis && homeostasis_ratio < 0.5f) {
                        if (input[i] != 0) {
                            if (cortex[c].weights[i] == input[i]) score++;
                            else score--;
                            act++;
                        }
                    } else {
                        if (cortex[c].weights[i] == input[i]) score++;
                        else score--;
                        act++;
                    }
                }
            }
            if (act > 0) {
                float sim = (float)score / act;
                if (sim > best_sim) { best_sim = sim; best = c; }
            }
        }

        // Neurogenesis
        if (best_sim < vigilance && first_empty != -1 && learning) {
            best = first_empty;
        } else if (best_sim < vigilance && !learning) {
            return -1;
        }

        // CLUSTER PROTECTION: refuse destructive overwrites
        if (learning && first_empty == -1 && best_sim < 0.3f) {
            return best;
        }

        // LEARNING: soft Hebbian + stiffness + freezing
        if (best != -1 && learning && !cortex[best].frozen) {
            cortex[best].sample_count++;

            // Decaying learning rate — fast early, slow later
            float lr = max(0.05f, 1.0f / (float)cortex[best].sample_count);

            for (int i = 0; i < INPUT_DIM; i++) {
                // Accumulate running average
                cortex[best].weight_accum[i] += lr * ((float)input[i] - cortex[best].weight_accum[i]);

                // Quantize to ternary with hysteresis
                if (cortex[best].weight_accum[i] > 0.35f) cortex[best].weights[i] = 1;
                else if (cortex[best].weight_accum[i] < -0.35f) cortex[best].weights[i] = -1;
                else cortex[best].weights[i] = 0;
            }

            cortex[best].last_spike_time = 1;
            cortex[best].fire_count++;

            // FREEZE CHECK: if enough samples seen, crystallize
            if (cortex[best].sample_count >= FREEZE_THRESHOLD) {
                cortex[best].frozen = true;
            }
        }
        return best;
    }
};

// ============================================================
// K-MEANS BASELINE
// ============================================================
class KMeans {
public:
    vector<vector<float>> centroids;
    int k;
    void fit(const vector<vector<int8_t>>& data, int num_k, int max_iter = 30) {
        k = num_k; int n = (int)data.size(); mt19937 rng(42);
        centroids.resize(k, vector<float>(INPUT_DIM, 0));
        vector<int> indices(n); iota(indices.begin(), indices.end(), 0);
        shuffle(indices.begin(), indices.end(), rng);
        for (int i = 0; i < k; i++)
            for (int d = 0; d < INPUT_DIM; d++)
                centroids[i][d] = (float)data[indices[i]][d];
        vector<int> assignments(n, 0);
        for (int iter = 0; iter < max_iter; iter++) {
            for (int i = 0; i < n; i++) {
                float bd = 1e18f;
                for (int c = 0; c < k; c++) {
                    float dist = 0;
                    for (int d = 0; d < INPUT_DIM; d++) { float df = (float)data[i][d] - centroids[c][d]; dist += df*df; }
                    if (dist < bd) { bd = dist; assignments[i] = c; }
                }
            }
            vector<vector<float>> sums(k, vector<float>(INPUT_DIM, 0));
            vector<int> counts(k, 0);
            for (int i = 0; i < n; i++) { counts[assignments[i]]++; for (int d = 0; d < INPUT_DIM; d++) sums[assignments[i]][d] += (float)data[i][d]; }
            for (int c = 0; c < k; c++) if (counts[c] > 0) for (int d = 0; d < INPUT_DIM; d++) centroids[c][d] = sums[c][d] / counts[c];
        }
    }
    int predict(const vector<int8_t>& input) {
        float bd = 1e18f; int best = 0;
        for (int c = 0; c < k; c++) { float d = 0; for (int i = 0; i < INPUT_DIM; i++) { float df = (float)input[i] - centroids[c][i]; d += df*df; } if (d < bd) { bd = d; best = c; } }
        return best;
    }
};

// ============================================================
// METRICS
// ============================================================
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

// ============================================================
// MAIN
// ============================================================
int main() {
    ofstream csv("build/benchmark_v3.csv");
    if (csv.is_open()) {
        csv << "section,metric,value\n";
    }

    cout << "[BENCH] FP-SAN v17 — benchmark v3 (flat perception path)" << endl;

    Dataset mnist;
    if (!mnist.load_data("data/mnist_stream.csv", 10000)) return 1;
    if (!mnist.load_labels("data/mnist_labels.csv")) return 1;
    int N = min((int)mnist.spikes.size(), (int)mnist.labels.size());

    auto write_metric = [&](const string& section, const string& metric, const string& value) {
        if (csv.is_open()) {
            csv << section << "," << metric << "," << value << "\n";
        }
    };

    auto write_metric_num = [&](const string& section, const string& metric, double value, int precision = 1) {
        if (csv.is_open()) {
            ostringstream ss;
            ss << fixed << setprecision(precision) << value;
            write_metric(section, metric, ss.str());
        }
    };

    auto* engine_ptr = new FPSANEngine();
    FPSANEngine& engine = *engine_ptr;
    auto wall_start = high_resolution_clock::now();

    // =========================================================
    // BENCHMARK 1: CLUSTER PURITY (with multi-pass)
    // =========================================================
    cout << "[1/6] CLUSTER PURITY (2-pass training)" << endl;
    cout << "---------------------------------------" << endl;
    engine.reset(false);
    vector<int> fpsan_a(N);

    // Pass 1: form clusters
    for (int i = 0; i < N; i++)
        fpsan_a[i] = engine.process(mnist.spikes[i], true);
    float pass1_purity = compute_purity(fpsan_a, mnist.labels, CLUSTER_DIM);

    // Pass 2: refine (frozen clusters won't change)
    for (int i = 0; i < N; i++)
        fpsan_a[i] = engine.process(mnist.spikes[i], true);
    float pass2_purity = compute_purity(fpsan_a, mnist.labels, CLUSTER_DIM);

    cout << "  Clusters:    " << engine.active_clusters() << " (" << engine.frozen_clusters() << " frozen)" << endl;
    cout << "  Pass 1:      " << fixed << setprecision(1) << pass1_purity << "%" << endl;
    cout << "  Pass 2:      " << fixed << setprecision(1) << pass2_purity << "%" << endl;
    write_metric_num("cluster_purity", "pass1_pct", pass1_purity);
    write_metric_num("cluster_purity", "pass2_pct", pass2_purity);
    write_metric("cluster_purity", "active_clusters", to_string(engine.active_clusters()));
    write_metric("cluster_purity", "frozen_clusters", to_string(engine.frozen_clusters()));

    KMeans km100; km100.fit(mnist.spikes, 100);
    vector<int> km_a(N); for (int i = 0; i < N; i++) km_a[i] = km100.predict(mnist.spikes[i]);
    float km_p = compute_purity(km_a, mnist.labels, 100);
    cout << "  k-Means:     " << fixed << setprecision(1) << km_p << "%" << endl;
    write_metric_num("cluster_purity", "kmeans_pct", km_p);
    cout << endl;

    // =========================================================
    // BENCHMARK 2: CATASTROPHIC FORGETTING
    // =========================================================
    cout << "[2/6] CATASTROPHIC FORGETTING" << endl;
    cout << "---------------------------------------" << endl;
    engine.reset(false);
    vector<int> gA, gB;
    for (int i = 0; i < N; i++) { if (mnist.labels[i] <= 4) gA.push_back(i); else gB.push_back(i); }

    vector<int> a_orig(gA.size());
    for (int i = 0; i < (int)gA.size(); i++) a_orig[i] = engine.process(mnist.spikes[gA[i]], true);
    // Second pass on A to crystallize
    for (int i = 0; i < (int)gA.size(); i++) engine.process(mnist.spikes[gA[i]], true);
    int frozen_after_A = engine.frozen_clusters();
    // Re-record after crystallization
    for (int i = 0; i < (int)gA.size(); i++) a_orig[i] = engine.process(mnist.spikes[gA[i]], false);

    // Inject Group B
    for (int i = 0; i < (int)gB.size(); i++) engine.process(mnist.spikes[gB[i]], true);

    // Re-test Group A
    int retained = 0;
    for (int i = 0; i < (int)gA.size(); i++) {
        int c = engine.process(mnist.spikes[gA[i]], false);
        if (c == a_orig[i]) retained++;
    }
    float retention = (float)retained / gA.size() * 100.0f;

    cout << "  Frozen after A (2-pass): " << frozen_after_A << " clusters" << endl;
    cout << "  Retention after B:       " << fixed << setprecision(1) << retention << "%" << endl;
    cout << "  Forgetting:              " << fixed << setprecision(1) << (100-retention) << "%" << endl;
    write_metric("forgetting", "frozen_after_a", to_string(frozen_after_A));
    write_metric_num("forgetting", "retention_pct", retention);
    write_metric_num("forgetting", "forgetting_pct", 100.0 - retention);
    cout << endl;

    // =========================================================
    // BENCHMARK 3: HOMEOSTASIS
    // =========================================================
    cout << "[3/6] HOMEOSTASIS RECOVERY" << endl;
    cout << "---------------------------------------" << endl;
    engine.reset(true);
    vector<int> clean_a(N);
    for (int i = 0; i < N; i++) clean_a[i] = engine.process(mnist.spikes[i], true);
    for (int i = 0; i < N; i++) engine.process(mnist.spikes[i], true); // Pass 2

    auto* engine_nh_ptr = new FPSANEngine();
    FPSANEngine& engine_nh = *engine_nh_ptr;
    engine_nh.reset(false);
    for (int i = 0; i < N; i++) engine_nh.process(mnist.spikes[i], true);
    for (int i = 0; i < N; i++) engine_nh.process(mnist.spikes[i], true);

    int tc = min(N, 1000), hm = 0, nhm = 0;
    for (int i = 0; i < tc; i++) {
        vector<int8_t> deg(INPUT_DIM, 0);
        for (int d = 0; d < INPUT_DIM; d++) if (mnist.raw[i][d] > 0.8f) deg[d] = 1;
        int hr = engine.process(deg, false);
        int nr = engine_nh.process(deg, false);
        if (hr == clean_a[i] && hr != -1) hm++;
        if (nr != -1 && nr == clean_a[i]) nhm++;
    }
    float ha = (float)hm/tc*100, na = (float)nhm/tc*100;
    cout << "  WITH Homeostasis:    " << fixed << setprecision(1) << ha << "%" << endl;
    cout << "  WITHOUT:             " << fixed << setprecision(1) << na << "%" << endl;
    cout << "  Advantage:           " << fixed << setprecision(1) << (ha-na) << "%" << endl;
    write_metric_num("homeostasis", "with_pct", ha);
    write_metric_num("homeostasis", "without_pct", na);
    write_metric_num("homeostasis", "advantage_pct", ha - na);
    cout << endl;

    // =========================================================
    // BENCHMARK 4: NOISE RESISTANCE
    // =========================================================
    cout << "[4/6] NOISE RESISTANCE" << endl;
    cout << "---------------------------------------" << endl;
    engine.reset(false);
    for (int i = 0; i < 500; i++) engine.process(mnist.spikes[i], true);
    for (int i = 0; i < 500; i++) engine.process(mnist.spikes[i], true); // Crystallize
    int pre = engine.active_clusters(), frz = engine.frozen_clusters();

    int8_t snap[10][INPUT_DIM];
    int sc = min(10, pre);
    for (int c = 0; c < sc; c++) memcpy(snap[c], engine.cortex[c].weights, INPUT_DIM);

    mt19937 rng(42);
    for (int n = 0; n < 500; n++) {
        vector<int8_t> noise(INPUT_DIM);
        for (int d = 0; d < INPUT_DIM; d++) noise[d] = (rng()%2==0) ? 1 : 0;
        engine.process(noise, true);
    }
    int intact = 0;
    for (int c = 0; c < sc; c++) if (memcmp(snap[c], engine.cortex[c].weights, INPUT_DIM) == 0) intact++;

    cout << "  Pre-noise: " << pre << " clusters (" << frz << " frozen)" << endl;
    cout << "  Post-noise: " << engine.active_clusters() << " clusters" << endl;
    cout << "  Integrity: " << intact << "/" << sc << " unchanged" << endl;
    write_metric("noise", "pre_clusters", to_string(pre));
    write_metric("noise", "frozen_clusters", to_string(frz));
    write_metric("noise", "post_clusters", to_string(engine.active_clusters()));
    write_metric("noise", "integrity_unchanged", to_string(intact));
    write_metric("noise", "integrity_checked", to_string(sc));
    cout << endl;

    // =========================================================
    // BENCHMARK 5: LATENCY
    // =========================================================
    cout << "[5/6] LATENCY & THROUGHPUT" << endl;
    cout << "---------------------------------------" << endl;
    engine.reset(false);
    for (int i = 0; i < 100; i++) engine.process(mnist.spikes[i], true);
    vector<double> lats;
    for (int i = 0; i < 1000; i++) {
        auto s = high_resolution_clock::now();
        engine.process(mnist.spikes[i%N], false);
        auto e = high_resolution_clock::now();
        lats.push_back(duration_cast<nanoseconds>(e-s).count()/1000.0);
    }
    sort(lats.begin(), lats.end());
    double ml = 0; for (auto l : lats) ml += l; ml /= lats.size();
    auto t0 = high_resolution_clock::now();
    for (int i = 0; i < 10000; i++) engine.process(mnist.spikes[i%N], false);
    auto t1 = high_resolution_clock::now();
    double fps = 10000.0/(duration_cast<microseconds>(t1-t0).count()/1e6);

    cout << "  Mean: " << fixed << setprecision(1) << ml << " us | P99: " << lats[989] << " us" << endl;
    cout << "  Throughput: " << fixed << setprecision(0) << fps << " fps" << endl;
    write_metric_num("latency", "mean_us", ml);
    write_metric_num("latency", "p99_us", lats[989]);
    write_metric_num("latency", "throughput_fps", fps, 0);
    cout << endl;

    // =========================================================
    // BENCHMARK 6: MEMORY
    // =========================================================
    cout << "[6/6] MEMORY" << endl;
    cout << "---------------------------------------" << endl;
    size_t deploy = engine.memory_bytes_deployed();
    cout << "  Deployed size: " << deploy/1024 << " KB" << endl;
    cout << "  50KB budget:   " << (deploy<=51200 ? "PASS" : "EXCEEDED") << endl;
    write_metric("memory", "deployed_kb", to_string(deploy / 1024));
    write_metric("memory", "budget_50kb", deploy <= 51200 ? "PASS" : "EXCEEDED");
    cout << endl;

    // =========================================================
    // FINAL COMPARISON
    // =========================================================
    auto wall_end = high_resolution_clock::now();
    double total = duration_cast<milliseconds>(wall_end-wall_start).count()/1000.0;

    write_metric_num("summary", "runtime_s", total);
    write_metric_num("summary", "final_cluster_purity_pct", pass2_purity);
    write_metric_num("summary", "final_retention_pct", retention);
    write_metric_num("summary", "final_homeostasis_pct", ha);
    write_metric_num("summary", "final_throughput_fps", fps, 0);

    cout << "[BENCH] CSV report written to build/benchmark_v3.csv" << endl;
    cout << "[BENCH] Runtime: " << fixed << setprecision(1) << total << " s" << endl;

    delete engine_nh_ptr;
    delete engine_ptr;
    return 0;
}
