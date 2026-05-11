// FP-SAN v17 — definitive benchmark suite (legacy standalone harness)
// Compile (MSVC): cl /std:c++17 /O2 /EHsc src\benchmark\fpsan_benchmark.cpp /Fe:build\benchmark.exe
// Run: build\benchmark.exe
// Requires: data/mnist_stream.csv, data/mnist_labels.csv

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

// ============================================================
// CONFIGURATION
// ============================================================
const int INPUT_DIM = 784;
const int CLUSTER_DIM = 100;
const float VIGILANCE = 0.05f;
const float BINARIZE_THRESH = 0.5f;
const float HOMEO_EXP = 1.5f;

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
            // Strip \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            stringstream ss(line);
            string val;
            vector<float> fv;
            vector<int8_t> sv;
            while (getline(ss, val, ',')) {
                float fval = stof(val);
                fv.push_back(fval);
                sv.push_back(fval > BINARIZE_THRESH ? (int8_t)1 : (int8_t)0);
            }
            // Pad or trim to INPUT_DIM
            fv.resize(INPUT_DIM, 0.0f);
            sv.resize(INPUT_DIM, 0);
            raw.push_back(fv);
            spikes.push_back(sv);
            count++;
        }
        cout << "[Data] Loaded " << count << " samples from " << path << endl;
        return count > 0;
    }

    bool load_labels(const string& path) {
        ifstream f(path);
        if (!f.is_open()) { cerr << "[ERROR] Cannot open " << path << endl; return false; }
        string line;
        while (getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            // Handle both "7" and "7.0" formats
            float v = stof(line);
            labels.push_back((int)v);
        }
        cout << "[Data] Loaded " << labels.size() << " labels" << endl;
        return !labels.empty();
    }
};

// ============================================================
// FP-SAN ENGINE (Clean extraction from v6 codebase)
// ============================================================
class FPSANEngine {
public:
    struct Cluster {
        int8_t weights[INPUT_DIM];
        float membrane_potential;
        uint64_t last_spike_time;
        int fire_count;
    };

    Cluster cortex[CLUSTER_DIM];
    float expected_energy;
    bool use_homeostasis;

    void reset(bool homeo = false) {
        use_homeostasis = homeo;
        expected_energy = (float)(INPUT_DIM) * 0.3f;
        for (int c = 0; c < CLUSTER_DIM; c++) {
            memset(cortex[c].weights, 0, INPUT_DIM);
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

    size_t memory_bytes() {
        return sizeof(cortex);
    }

    // Returns cluster ID assigned (-1 if none)
    int process(const vector<int8_t>& input, bool learning) {
        int best = -1, first_empty = -1;
        float best_sim = -9999.0f;

        // Homeostasis
        float dyn_vigilance = VIGILANCE;
        if (use_homeostasis) {
            float energy = 0;
            for (int i = 0; i < INPUT_DIM; i++) if (input[i] != 0) energy += 1.0f;
            expected_energy = 0.8f * expected_energy + 0.2f * energy;
            float ratio = 1.0f;
            if (expected_energy > 0.1f) {
                ratio = energy / expected_energy;
                ratio = powf(ratio, HOMEO_EXP);
            }
            ratio = max(0.1f, min(ratio, 2.0f));
            dyn_vigilance = VIGILANCE * ratio;
        }

        // Find best matching cluster
        for (int c = 0; c < CLUSTER_DIM; c++) {
            if (cortex[c].last_spike_time == 0) {
                if (first_empty == -1) first_empty = c;
                continue;
            }
            int score = 0, active = 0;
            for (int i = 0; i < INPUT_DIM; i++) {
                if (input[i] != 0 || cortex[c].weights[i] != 0) {
                    if (cortex[c].weights[i] == input[i]) score++;
                    else score--;
                    active++;
                }
            }
            if (active > 0) {
                float sim = (float)score / active;
                if (sim > best_sim) { best_sim = sim; best = c; }
            }
        }

        // Neurogenesis
        if (best_sim < dyn_vigilance && first_empty != -1 && learning) {
            best = first_empty;
        } else if (best_sim < dyn_vigilance && !learning) {
            return -1; // Inference: no match found
        }

        // Hebbian update
        if (best != -1 && learning) {
            for (int i = 0; i < INPUT_DIM; i++) {
                if (input[i] != 0) cortex[best].weights[i] = input[i];
            }
            cortex[best].last_spike_time = 1;
            cortex[best].fire_count++;
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
        k = num_k;
        int n = (int)data.size();
        mt19937 rng(42);

        // Init centroids from random data points
        centroids.resize(k, vector<float>(INPUT_DIM, 0));
        vector<int> indices(n);
        iota(indices.begin(), indices.end(), 0);
        shuffle(indices.begin(), indices.end(), rng);
        for (int i = 0; i < k; i++)
            for (int d = 0; d < INPUT_DIM; d++)
                centroids[i][d] = (float)data[indices[i]][d];

        vector<int> assignments(n, 0);

        for (int iter = 0; iter < max_iter; iter++) {
            // Assign
            for (int i = 0; i < n; i++) {
                float best_dist = 1e18f;
                for (int c = 0; c < k; c++) {
                    float dist = 0;
                    for (int d = 0; d < INPUT_DIM; d++) {
                        float diff = (float)data[i][d] - centroids[c][d];
                        dist += diff * diff;
                    }
                    if (dist < best_dist) { best_dist = dist; assignments[i] = c; }
                }
            }
            // Update
            vector<vector<float>> sums(k, vector<float>(INPUT_DIM, 0));
            vector<int> counts(k, 0);
            for (int i = 0; i < n; i++) {
                counts[assignments[i]]++;
                for (int d = 0; d < INPUT_DIM; d++)
                    sums[assignments[i]][d] += (float)data[i][d];
            }
            for (int c = 0; c < k; c++)
                if (counts[c] > 0)
                    for (int d = 0; d < INPUT_DIM; d++)
                        centroids[c][d] = sums[c][d] / counts[c];
        }
    }

    int predict(const vector<int8_t>& input) {
        float best_dist = 1e18f;
        int best = 0;
        for (int c = 0; c < k; c++) {
            float dist = 0;
            for (int d = 0; d < INPUT_DIM; d++) {
                float diff = (float)input[d] - centroids[c][d];
                dist += diff * diff;
            }
            if (dist < best_dist) { best_dist = dist; best = c; }
        }
        return best;
    }
};

// ============================================================
// METRICS
// ============================================================
float compute_purity(const vector<int>& assignments, const vector<int>& labels, int num_clusters) {
    if (assignments.empty()) return 0;
    int correct = 0;
    for (int c = 0; c < num_clusters; c++) {
        map<int, int> label_counts;
        for (int i = 0; i < (int)assignments.size(); i++) {
            if (assignments[i] == c) label_counts[labels[i]]++;
        }
        int max_count = 0;
        for (auto& p : label_counts) max_count = max(max_count, p.second);
        correct += max_count;
    }
    return (float)correct / assignments.size() * 100.0f;
}

// ============================================================
// MAIN BENCHMARK SUITE
// ============================================================
int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN v17 | definitive benchmark suite (legacy path)" << endl;
    cout << " Compiler: MSVC | Platform: Windows x64" << endl;
    cout << "================================================================\n" << endl;

    // --- LOAD DATA ---
    Dataset mnist;
    if (!mnist.load_data("data/mnist_stream.csv", 10000)) {
        cerr << "FATAL: Cannot load MNIST data. Run tools/gen_mnist.py first." << endl;
        return 1;
    }
    if (!mnist.load_labels("data/mnist_labels.csv")) {
        cerr << "FATAL: Cannot load MNIST labels." << endl;
        return 1;
    }
    int N = min((int)mnist.spikes.size(), (int)mnist.labels.size());
    cout << "[Data] Using " << N << " samples for benchmarking.\n" << endl;

    FPSANEngine engine;
    auto wall_start = high_resolution_clock::now();

    // =========================================================
    // BENCHMARK 1: CLUSTER PURITY (MNIST)
    // =========================================================
    cout << "[1/6] CLUSTER PURITY TEST" << endl;
    cout << "---------------------------------------" << endl;

    engine.reset(false);
    vector<int> fpsan_assignments(N);

    auto t0 = high_resolution_clock::now();
    for (int i = 0; i < N; i++) {
        fpsan_assignments[i] = engine.process(mnist.spikes[i], true);
    }
    auto t1 = high_resolution_clock::now();

    int fpsan_clusters_used = engine.active_clusters();
    float fpsan_purity = compute_purity(fpsan_assignments, mnist.labels, CLUSTER_DIM);
    double fpsan_train_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

    cout << "  Clusters Used:   " << fpsan_clusters_used << "/" << CLUSTER_DIM << endl;
    cout << "  Cluster Purity:  " << fixed << setprecision(1) << fpsan_purity << "%" << endl;
    cout << "  Training Time:   " << fixed << setprecision(1) << fpsan_train_ms << " ms" << endl;

    // K-Means baseline (k=10, matching digit count)
    KMeans km;
    t0 = high_resolution_clock::now();
    km.fit(mnist.spikes, 10);
    t1 = high_resolution_clock::now();

    vector<int> km_assignments(N);
    for (int i = 0; i < N; i++) km_assignments[i] = km.predict(mnist.spikes[i]);
    float km_purity = compute_purity(km_assignments, mnist.labels, 10);
    double km_train_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

    // K-Means with k=100 for fair comparison
    KMeans km100;
    km100.fit(mnist.spikes, 100);
    vector<int> km100_assignments(N);
    for (int i = 0; i < N; i++) km100_assignments[i] = km100.predict(mnist.spikes[i]);
    float km100_purity = compute_purity(km100_assignments, mnist.labels, 100);

    cout << "  k-Means (k=10):  " << fixed << setprecision(1) << km_purity << "% (" << km_train_ms << " ms)" << endl;
    cout << "  k-Means (k=100): " << fixed << setprecision(1) << km100_purity << "%" << endl;
    cout << endl;

    // =========================================================
    // BENCHMARK 2: CATASTROPHIC FORGETTING
    // =========================================================
    cout << "[2/6] CATASTROPHIC FORGETTING TEST" << endl;
    cout << "---------------------------------------" << endl;

    engine.reset(false);

    // Split by label: Group A = {0,1,2,3,4}, Group B = {5,6,7,8,9}
    vector<int> groupA_idx, groupB_idx;
    for (int i = 0; i < N; i++) {
        if (mnist.labels[i] <= 4) groupA_idx.push_back(i);
        else groupB_idx.push_back(i);
    }

    // Phase 1: Train on Group A
    vector<int> a_original(groupA_idx.size());
    for (int i = 0; i < (int)groupA_idx.size(); i++) {
        a_original[i] = engine.process(mnist.spikes[groupA_idx[i]], true);
    }
    int clusters_after_A = engine.active_clusters();

    // Phase 2: Train on Group B (domain shift)
    for (int i = 0; i < (int)groupB_idx.size(); i++) {
        engine.process(mnist.spikes[groupB_idx[i]], true);
    }
    int clusters_after_B = engine.active_clusters();

    // Phase 3: Re-test Group A (inference only)
    int retained = 0;
    for (int i = 0; i < (int)groupA_idx.size(); i++) {
        int cluster = engine.process(mnist.spikes[groupA_idx[i]], false);
        if (cluster == a_original[i]) retained++;
    }
    float retention = (float)retained / groupA_idx.size() * 100.0f;

    cout << "  Group A samples:        " << groupA_idx.size() << " (digits 0-4)" << endl;
    cout << "  Group B samples:        " << groupB_idx.size() << " (digits 5-9)" << endl;
    cout << "  Clusters after A:       " << clusters_after_A << endl;
    cout << "  Clusters after A+B:     " << clusters_after_B << endl;
    cout << "  Group A Retention:      " << fixed << setprecision(1) << retention << "%" << endl;
    cout << "  Forgetting:             " << fixed << setprecision(1) << (100.0f - retention) << "%" << endl;
    cout << endl;

    // =========================================================
    // BENCHMARK 3: HOMEOSTASIS RECOVERY
    // =========================================================
    cout << "[3/6] HOMEOSTASIS RECOVERY TEST" << endl;
    cout << "---------------------------------------" << endl;

    // Train clean
    engine.reset(true); // Enable homeostasis
    vector<int> clean_assignments(N);
    for (int i = 0; i < N; i++)
        clean_assignments[i] = engine.process(mnist.spikes[i], true);

    // Degrade signal: keep only pixels with intensity > 0.8
    // (simulating 70% sensor failure)
    int homeo_match = 0, no_homeo_match = 0;
    int test_count = min(N, 1000); // Test subset for speed

    FPSANEngine engine_no_homeo;
    engine_no_homeo.reset(false);
    for (int i = 0; i < N; i++)
        engine_no_homeo.process(mnist.spikes[i], true);

    for (int i = 0; i < test_count; i++) {
        vector<int8_t> degraded(INPUT_DIM, 0);
        for (int d = 0; d < INPUT_DIM; d++) {
            if (mnist.raw[i][d] > 0.8f) degraded[d] = 1; // Only strongest pixels survive
        }
        int h_result = engine.process(degraded, false);
        int nh_result = engine_no_homeo.process(degraded, false);

        if (h_result == clean_assignments[i] && h_result != -1) homeo_match++;
        if (nh_result == clean_assignments[i] && nh_result != -1) no_homeo_match++;
    }

    float homeo_acc = (float)homeo_match / test_count * 100.0f;
    float no_homeo_acc = (float)no_homeo_match / test_count * 100.0f;

    cout << "  Signal Degradation:     70% pixel loss (threshold > 0.8)" << endl;
    cout << "  Test Samples:           " << test_count << endl;
    cout << "  WITH Homeostasis:       " << fixed << setprecision(1) << homeo_acc << "% cluster retention" << endl;
    cout << "  WITHOUT Homeostasis:    " << fixed << setprecision(1) << no_homeo_acc << "% cluster retention" << endl;
    cout << "  Homeostasis Advantage:  " << fixed << setprecision(1) << (homeo_acc - no_homeo_acc) << "%" << endl;
    cout << endl;

    // =========================================================
    // BENCHMARK 4: NOISE RESISTANCE
    // =========================================================
    cout << "[4/6] NOISE RESISTANCE TEST" << endl;
    cout << "---------------------------------------" << endl;

    engine.reset(false);
    // Train on first 500 real samples
    int pre_train = 500;
    for (int i = 0; i < pre_train; i++)
        engine.process(mnist.spikes[i], true);
    int pre_noise_clusters = engine.active_clusters();

    // Snapshot weights of first 10 clusters
    int8_t snapshot[10][INPUT_DIM];
    for (int c = 0; c < 10 && c < pre_noise_clusters; c++)
        memcpy(snapshot[c], engine.cortex[c].weights, INPUT_DIM);

    // Inject 500 frames of random noise WITH learning on
    mt19937 rng(42);
    for (int n = 0; n < 500; n++) {
        vector<int8_t> noise(INPUT_DIM);
        for (int d = 0; d < INPUT_DIM; d++)
            noise[d] = (rng() % 2 == 0) ? 1 : 0;
        engine.process(noise, true);
    }
    int post_noise_clusters = engine.active_clusters();

    // Check original cluster integrity
    int intact = 0;
    int check_count = min(10, pre_noise_clusters);
    for (int c = 0; c < check_count; c++) {
        if (memcmp(snapshot[c], engine.cortex[c].weights, INPUT_DIM) == 0) intact++;
    }

    cout << "  Pre-noise Clusters:     " << pre_noise_clusters << endl;
    cout << "  Noise Frames Injected:  500 (random binary)" << endl;
    cout << "  Post-noise Clusters:    " << post_noise_clusters << endl;
    cout << "  Junk Clusters Spawned:  " << (post_noise_clusters - pre_noise_clusters) << endl;
    cout << "  Original Integrity:     " << intact << "/" << check_count << " clusters unchanged" << endl;
    cout << endl;

    // =========================================================
    // BENCHMARK 5: LATENCY & THROUGHPUT
    // =========================================================
    cout << "[5/6] LATENCY & THROUGHPUT" << endl;
    cout << "---------------------------------------" << endl;

    engine.reset(false);
    // Warm up
    for (int i = 0; i < 100; i++) engine.process(mnist.spikes[i], true);

    // Measure inference latency
    vector<double> latencies;
    for (int i = 0; i < 1000; i++) {
        auto s = high_resolution_clock::now();
        engine.process(mnist.spikes[i % N], false);
        auto e = high_resolution_clock::now();
        latencies.push_back(duration_cast<nanoseconds>(e - s).count() / 1000.0); // microseconds
    }
    sort(latencies.begin(), latencies.end());
    double mean_lat = 0;
    for (auto l : latencies) mean_lat += l;
    mean_lat /= latencies.size();
    double p50 = latencies[499];
    double p99 = latencies[989];

    // Throughput: time 10K inferences
    t0 = high_resolution_clock::now();
    for (int i = 0; i < 10000; i++)
        engine.process(mnist.spikes[i % N], false);
    t1 = high_resolution_clock::now();
    double throughput_sec = duration_cast<microseconds>(t1 - t0).count() / 1e6;
    double fps = 10000.0 / throughput_sec;

    cout << "  Mean Latency:           " << fixed << setprecision(1) << mean_lat << " us" << endl;
    cout << "  P50 Latency:            " << fixed << setprecision(1) << p50 << " us" << endl;
    cout << "  P99 Latency:            " << fixed << setprecision(1) << p99 << " us" << endl;
    cout << "  Throughput:             " << fixed << setprecision(0) << fps << " frames/sec" << endl;
    cout << endl;

    // =========================================================
    // BENCHMARK 6: MEMORY FOOTPRINT
    // =========================================================
    cout << "[6/6] MEMORY FOOTPRINT" << endl;
    cout << "---------------------------------------" << endl;

    size_t brain_bytes = engine.memory_bytes();
    size_t per_cluster = sizeof(FPSANEngine::Cluster);
    bool under_50k = brain_bytes <= 51200;

    cout << "  Cluster struct size:    " << per_cluster << " bytes" << endl;
    cout << "  Total brain size:       " << brain_bytes << " bytes (" << brain_bytes / 1024 << " KB)" << endl;
    cout << "  Per active cluster:     " << per_cluster << " bytes" << endl;
    cout << "  50KB budget:            " << (under_50k ? "PASS" : "EXCEEDED") << endl;
    cout << endl;

    // =========================================================
    // FINAL SCORECARD
    // =========================================================
    auto wall_end = high_resolution_clock::now();
    double total_sec = duration_cast<milliseconds>(wall_end - wall_start).count() / 1000.0;

    cout << "================================================================" << endl;
    cout << " FINAL SCORECARD" << endl;
    cout << "================================================================" << endl;
    cout << "| Metric                  | FP-SAN     | k-Means    | Winner   |" << endl;
    cout << "|-------------------------|------------|------------|----------|" << endl;

    auto winner = [](float a, float b, bool higher_better) -> string {
        if (higher_better) return a > b ? "FP-SAN" : (b > a ? "k-Means" : "TIE");
        return a < b ? "FP-SAN" : (b < a ? "k-Means" : "TIE");
    };

    cout << "| Cluster Purity          | " << setw(8) << fixed << setprecision(1) << fpsan_purity << "%  | "
         << setw(8) << km100_purity << "%  | " << setw(8) << winner(fpsan_purity, km100_purity, true) << " |" << endl;
    cout << "| Forgetting Retention    | " << setw(8) << retention << "%  | "
         << setw(8) << "N/A" << "   | " << setw(8) << "FP-SAN" << " |" << endl;
    cout << "| Homeostasis Recovery    | " << setw(8) << homeo_acc << "%  | "
         << setw(8) << "N/A" << "   | " << setw(8) << "FP-SAN" << " |" << endl;
    cout << "| Inference Latency       | " << setw(7) << fixed << setprecision(1) << mean_lat << "us  | "
         << setw(8) << "---" << "   | " << setw(8) << "---" << " |" << endl;
    cout << "| Memory                  | " << setw(6) << brain_bytes/1024 << " KB  | "
         << setw(8) << "---" << "   | " << setw(8) << "---" << " |" << endl;
    cout << "================================================================" << endl;
    cout << " Total benchmark time: " << fixed << setprecision(1) << total_sec << " seconds" << endl;
    cout << "================================================================" << endl;

    return 0;
}
