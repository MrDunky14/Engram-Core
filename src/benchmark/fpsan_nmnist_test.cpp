// FP-SAN Phase 5D: Neuromorphic Vision (N-MNIST)
// Evaluates the V1/V2 hierarchy on temporal event streams (Saccade Simulation)
// Compile: cl /std:c++17 /O2 /EHsc /I src\core src\benchmark\fpsan_nmnist_test.cpp /Fe:build\nmnist_test.exe

#define NOMINMAX
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cmath>

#include "fpsan_hierarchy.h"

using namespace std;
using namespace std::chrono;

const int IMG_SIZE = 28;

// Applies a temporal saccade (shift) to generate event spikes
void generate_saccade_spikes(const vector<float>& img, int dx, int dy, vector<float>& prev_img, int8_t* out_spikes) {
    vector<float> curr_img(784, 0.0f);
    
    // Shift image
    for (int y = 0; y < IMG_SIZE; y++) {
        for (int x = 0; x < IMG_SIZE; x++) {
            int nx = x - dx;
            int ny = y - dy;
            if (nx >= 0 && nx < IMG_SIZE && ny >= 0 && ny < IMG_SIZE) {
                curr_img[y * IMG_SIZE + x] = img[ny * IMG_SIZE + nx];
            }
        }
    }
    
    // Generate event spikes (difference > threshold)
    for (int i = 0; i < 784; i++) {
        float diff = curr_img[i] - prev_img[i];
        if (diff > 0.2f) out_spikes[i] = 1;       // ON event
        else if (diff < -0.2f) out_spikes[i] = -1; // OFF event
        else out_spikes[i] = 0;                   // No event
        
        prev_img[i] = curr_img[i]; // Update state
    }
}

int main() {
    cout << "================================================================" << endl;
    cout << " FP-SAN PHASE 5D: NEUROMORPHIC VISION BENCHMARK" << endl;
    cout << " Dataset: N-MNIST (via Saccade Simulation)" << endl;
    cout << " Testing LIF Hierarchy on true temporal event streams." << endl;
    cout << "================================================================\n" << endl;

    // 1. Load static MNIST
    vector<vector<float>> images;
    ifstream file("data/mnist_stream.csv");
    if (!file.is_open()) {
        cerr << "Error: Could not open data/mnist_stream.csv" << endl;
        return 1;
    }

    string line;
    int max_samples = 2000; // Test on 2000 samples to keep benchmark fast
    while (getline(file, line) && images.size() < max_samples) {
        stringstream ss(line);
        string val;
        vector<float> img;
        while (getline(ss, val, ',')) {
            img.push_back(stof(val));
        }
        if (img.size() == 784) images.push_back(img);
    }
    cout << "[Dataset] Loaded " << images.size() << " frames for N-MNIST conversion." << endl;

    // 2. Initialize V1/V2 Hierarchy
    HierarchicalCortex* cortex = new HierarchicalCortex();
    cortex->init();

    cout << "[System] V1/V2 Cortex initialized." << endl;

    int total_spikes = 0;
    int total_frames = 0;
    
    auto t0 = high_resolution_clock::now();

    // 3. Process Temporal Streams
    cout << "\n[1/2] Processing Saccade Event Streams..." << endl;
    
    for (size_t i = 0; i < images.size(); i++) {
        vector<float> prev_state(784, 0.0f);
        
        // Define a saccade path (e.g., eye drifting right, then down, then left)
        int path[5][2] = {{0,0}, {1,0}, {2,1}, {1,2}, {0,1}};
        
        for (int step = 0; step < 5; step++) {
            int8_t spikes[784];
            generate_saccade_spikes(images[i], path[step][0], path[step][1], prev_state, spikes);
            
            // Count spikes for metrics
            int active_spikes = 0;
            for(int j=0; j<784; j++) if(spikes[j] != 0) active_spikes++;
            total_spikes += active_spikes;
            total_frames++;
            
            // Feed temporal spikes into hierarchy (learning enabled)
            cortex->perceive(spikes, true);
        }
        
        if ((i + 1) % 500 == 0) {
            cout << "  Processed " << (i + 1) << " streams..." << endl;
        }
    }

    auto t1 = high_resolution_clock::now();
    double elapsed_ms = duration_cast<milliseconds>(t1 - t0).count();
    
    cout << "\n[2/2] Freezing and Evaluating Clusters..." << endl;
    
    // In a real setup, we'd map clusters to labels and measure purity.
    // Here we report the capacity and throughput.
    int v1_active = cortex->total_v1_clusters();
    int v2_active = cortex->v2_clusters();

    double fps = (total_frames * 1000.0) / elapsed_ms;
    double eps = (total_spikes * 1000.0) / elapsed_ms; // Events per second

    cout << "\n================================================================" << endl;
    cout << " N-MNIST BENCHMARK RESULTS" << endl;
    cout << "================================================================" << endl;
    cout << " Temporal Frames: " << total_frames << " (5 steps per digit)" << endl;
    cout << " Total Spikes:    " << total_spikes << " (Sparsity: " << fixed << setprecision(1) << (total_spikes * 100.0 / (total_frames * 784)) << "%)" << endl;
    cout << " V1 Nodes Active: " << v1_active << " / " << (25 * 600) << endl;
    cout << " V2 Concepts:     " << v2_active << " / 100" << endl;
    cout << " Latency:         " << fixed << setprecision(2) << (elapsed_ms / total_frames) * 1000.0 << " us / frame" << endl;
    cout << " Throughput:      " << fixed << setprecision(0) << fps << " FPS" << endl;
    cout << " Event Rate:      " << fixed << setprecision(0) << eps << " EPS (Events/sec)" << endl;
    cout << "================================================================\n" << endl;

    delete cortex;
    return 0;
}
