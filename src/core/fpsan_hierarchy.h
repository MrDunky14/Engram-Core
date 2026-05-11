#pragma once
// FP-SAN Z-Axis Hierarchical Cortex v3
// V1: 25 OVERLAPPING 7x7 receptive fields (stride 4, 5x5 grid)
// V2: Co-occurrence pattern matching over V1 cluster ID vector
//
// Fix from v2: overlapping patches prevent features from being cut
// at patch boundaries. Stride 4 means neighboring patches share
// 3 columns/rows of overlap.

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>

// ============================================================
// V1 LAYER — Overlapping Localized Feature Detectors
// ============================================================
const int V1_PATCH_SIZE = 7;
const int V1_PATCH_DIM = V1_PATCH_SIZE * V1_PATCH_SIZE; // 49
const int V1_STRIDE = 4;                                // Stride 4 → 3 pixel overlap
const int V1_GRID = 5;                                  // (28-7)/4 + 1 = 5+1≈6, but we use 5 to stay in bounds
const int V1_NUM_PATCHES = V1_GRID * V1_GRID;           // 25 patches
const int V1_CLUSTERS_PER_PATCH = 24;                   // Balanced capacity
const float V1_BASE_VIGILANCE = 0.25f;                  // Stricter: force reuse
const int V1_FREEZE_THRESHOLD = 12;

struct V1Patch {
    struct Cluster {
        int8_t weights[V1_PATCH_DIM];
        float accum[V1_PATCH_DIM];
        int sample_count;
        bool frozen;
        bool active;
    };

    Cluster clusters[V1_CLUSTERS_PER_PATCH];

    void init() {
        for (int c = 0; c < V1_CLUSTERS_PER_PATCH; c++) {
            memset(clusters[c].weights, 0, V1_PATCH_DIM);
            memset(clusters[c].accum, 0, sizeof(clusters[c].accum));
            clusters[c].sample_count = 0;
            clusters[c].frozen = false;
            clusters[c].active = false;
        }
    }

    int active_count() {
        int n = 0;
        for (int c = 0; c < V1_CLUSTERS_PER_PATCH; c++) if (clusters[c].active) n++;
        return n;
    }

    int frozen_count() {
        int n = 0;
        for (int c = 0; c < V1_CLUSTERS_PER_PATCH; c++) if (clusters[c].frozen) n++;
        return n;
    }

    int perceive(const int8_t* patch_input, bool learn) {
        int best = -1, first_empty = -1;
        float best_sim = -9999.0f;

        int active = active_count();
        float cap = (float)active / V1_CLUSTERS_PER_PATCH;
        float vigilance = V1_BASE_VIGILANCE + cap * 0.45f;

        for (int c = 0; c < V1_CLUSTERS_PER_PATCH; c++) {
            if (!clusters[c].active) {
                if (first_empty == -1) first_empty = c;
                continue;
            }
            int score = 0, act = 0;
            for (int i = 0; i < V1_PATCH_DIM; i++) {
                if (patch_input[i] != 0 || clusters[c].weights[i] != 0) {
                    if (clusters[c].weights[i] == patch_input[i]) score++; else score--;
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

        if (best != -1 && learn && !clusters[best].frozen) {
            clusters[best].sample_count++;
            float lr = std::max(0.05f, 1.0f / (float)clusters[best].sample_count);
            for (int i = 0; i < V1_PATCH_DIM; i++) {
                clusters[best].accum[i] += lr * ((float)patch_input[i] - clusters[best].accum[i]);
                if (clusters[best].accum[i] > 0.35f) clusters[best].weights[i] = 1;
                else if (clusters[best].accum[i] < -0.35f) clusters[best].weights[i] = -1;
                else clusters[best].weights[i] = 0;
            }
            clusters[best].active = true;
            if (clusters[best].sample_count >= V1_FREEZE_THRESHOLD)
                clusters[best].frozen = true;
        }
        return best;
    }
};

// ============================================================
// V2 LAYER — Co-occurrence Pattern Matching
// ============================================================
const int V2_CLUSTER_DIM = 100;
const float V2_BASE_VIGILANCE_FRAC = 0.55f;
const int V2_FREEZE_THRESHOLD = 20;

struct V2Layer {
    struct Cluster {
        int8_t pattern[V1_NUM_PATCHES]; // Stored V1 cluster IDs
        float accum[V1_NUM_PATCHES];
        int sample_count;
        bool frozen;
        bool active;
    };

    Cluster clusters[V2_CLUSTER_DIM];

    void init() {
        for (int c = 0; c < V2_CLUSTER_DIM; c++) {
            memset(clusters[c].pattern, -1, V1_NUM_PATCHES);
            memset(clusters[c].accum, 0, sizeof(clusters[c].accum));
            clusters[c].sample_count = 0;
            clusters[c].frozen = false;
            clusters[c].active = false;
        }
    }

    int active_count() {
        int n = 0;
        for (int c = 0; c < V2_CLUSTER_DIM; c++) if (clusters[c].active) n++;
        return n;
    }

    int frozen_count() {
        int n = 0;
        for (int c = 0; c < V2_CLUSTER_DIM; c++) if (clusters[c].frozen) n++;
        return n;
    }

    int perceive(const int* v1_ids, bool learn) {
        int best = -1, first_empty = -1;
        float best_sim = -9999.0f;

        int active = active_count();
        float cap = (float)active / V2_CLUSTER_DIM;
        float vigilance = V2_BASE_VIGILANCE_FRAC + cap * 0.35f;
        vigilance = std::min(vigilance, 0.95f);

        for (int c = 0; c < V2_CLUSTER_DIM; c++) {
            if (!clusters[c].active) {
                if (first_empty == -1) first_empty = c;
                continue;
            }
            int matches = 0, valid = 0;
            for (int p = 0; p < V1_NUM_PATCHES; p++) {
                if (v1_ids[p] >= 0 && clusters[c].pattern[p] >= 0) {
                    valid++;
                    if (v1_ids[p] == clusters[c].pattern[p]) matches++;
                }
            }
            if (valid > 0) {
                float sim = (float)matches / valid;
                if (sim > best_sim) { best_sim = sim; best = c; }
            }
        }

        if (best_sim < vigilance && first_empty != -1 && learn) best = first_empty;
        else if (best_sim < vigilance && !learn) return -1;

        if (learn && first_empty == -1 && best_sim < 0.4f) return best;

        if (best != -1 && learn && !clusters[best].frozen) {
            clusters[best].sample_count++;
            float lr = std::max(0.05f, 1.0f / (float)clusters[best].sample_count);

            for (int p = 0; p < V1_NUM_PATCHES; p++) {
                if (v1_ids[p] >= 0) {
                    clusters[best].accum[p] += lr * ((float)v1_ids[p] - clusters[best].accum[p]);
                    clusters[best].pattern[p] = (int8_t)std::max(0.0f,
                        std::min((float)(V1_CLUSTERS_PER_PATCH - 1), roundf(clusters[best].accum[p])));
                }
            }
            clusters[best].active = true;
            if (clusters[best].sample_count >= V2_FREEZE_THRESHOLD)
                clusters[best].frozen = true;
        }
        return best;
    }
};

// ============================================================
// HIERARCHICAL CORTEX v3
// ============================================================
struct HierarchicalCortex {
    V1Patch v1[V1_NUM_PATCHES];
    V2Layer v2;

    int last_v1_spikes[V1_NUM_PATCHES];
    int last_v2_cluster;

    void init() {
        for (int p = 0; p < V1_NUM_PATCHES; p++) v1[p].init();
        v2.init();
        memset(last_v1_spikes, -1, sizeof(last_v1_spikes));
        last_v2_cluster = -1;
    }

    // Extract overlapping patch with stride
    void extract_patch(const int8_t* image_784, int patch_idx, int8_t* out_49) {
        int grid_row = patch_idx / V1_GRID;
        int grid_col = patch_idx % V1_GRID;
        int start_y = grid_row * V1_STRIDE;
        int start_x = grid_col * V1_STRIDE;

        for (int dy = 0; dy < V1_PATCH_SIZE; dy++) {
            for (int dx = 0; dx < V1_PATCH_SIZE; dx++) {
                int sy = start_y + dy;
                int sx = start_x + dx;
                int dst_idx = dy * V1_PATCH_SIZE + dx;
                if (sy < 28 && sx < 28)
                    out_49[dst_idx] = image_784[sy * 28 + sx];
                else
                    out_49[dst_idx] = 0;
            }
        }
    }

    int perceive(const int8_t* image_784, bool learn) {
        int8_t patch_buf[V1_PATCH_DIM];

        for (int p = 0; p < V1_NUM_PATCHES; p++) {
            extract_patch(image_784, p, patch_buf);
            last_v1_spikes[p] = v1[p].perceive(patch_buf, learn);
        }

        last_v2_cluster = v2.perceive(last_v1_spikes, learn);
        return last_v2_cluster;
    }

    int total_v1_clusters() {
        int n = 0;
        for (int p = 0; p < V1_NUM_PATCHES; p++) n += v1[p].active_count();
        return n;
    }
    int total_v1_frozen() {
        int n = 0;
        for (int p = 0; p < V1_NUM_PATCHES; p++) n += v1[p].frozen_count();
        return n;
    }
    int v2_clusters() { return v2.active_count(); }
    int v2_frozen() { return v2.frozen_count(); }

    void print_stats() {
        std::cout << "  V1: " << total_v1_clusters()
                  << " clusters (" << total_v1_frozen() << " frozen)"
                  << " across " << V1_NUM_PATCHES << " patches" << std::endl;
        std::cout << "  V2: " << v2_clusters()
                  << " concepts (" << v2_frozen() << " frozen)"
                  << "/" << V2_CLUSTER_DIM << std::endl;
    }

    size_t memory_bytes() {
        size_t v1_size = V1_NUM_PATCHES * V1_CLUSTERS_PER_PATCH *
                         (V1_PATCH_DIM + V1_PATCH_DIM * sizeof(float) + 8);
        size_t v2_size = V2_CLUSTER_DIM *
                         (V1_NUM_PATCHES + V1_NUM_PATCHES * sizeof(float) + 8);
        return v1_size + v2_size;
    }

    size_t deployed_bytes() {
        size_t v1_size = V1_NUM_PATCHES * V1_CLUSTERS_PER_PATCH * V1_PATCH_DIM;
        size_t v2_size = V2_CLUSTER_DIM * V1_NUM_PATCHES;
        return v1_size + v2_size;
    }
};
