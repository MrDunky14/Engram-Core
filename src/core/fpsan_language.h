#pragma once
// FP-SAN Language Layer (Phase 5)
// Converts text into 2D structural spike arrays for neuromorphic processing.
// Implements a Dual-Stream architecture:
// 1. Char-Stream (8x8): Biological character-by-character reading.
// 2. Word-Hash (16x16): Lightning-fast "sight reading" of whole words.

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

// Language Parameters
const int LANG_CHAR_DIM = 64;   // 8x8 spike grid
const int LANG_WORD_DIM = 256;  // 16x16 spike grid
const int LANG_CLUSTERS = 6500;  // Increased for Real ConceptNet 10k Benchmark

struct SpikingTokenizer {

    // Converts a single ASCII character into an 8x8 binary spike grid (64-dim)
    // Uses a simple algorithmic font based on the bits of the character.
    void encode_char(char c, int8_t* out_64) {
        memset(out_64, 0, LANG_CHAR_DIM);
        // Biological mimicry: we use the raw ASCII bits to define structural columns,
        // and its mathematical inverse to create spatial contrast.
        uint8_t val = (uint8_t)c;
        for (int y = 0; y < 8; y++) {
            bool bit = (val & (1 << y)) != 0;
            // Draw a vertical stroke if bit is 1, empty if 0
            for (int x = 0; x < 8; x++) {
                if (bit && (x == 2 || x == 5)) out_64[y * 8 + x] = 1; // Stroke
                if (!bit && (x == 3 || x == 4)) out_64[y * 8 + x] = 1; // Inverse stroke
            }
        }
    }

    // Hashes an entire word string into a dense 16x16 spike grid (256-dim)
    // This allows the AI to "sight-read" familiar words instantly.
    void encode_word_hash(const std::string& word, int8_t* out_256) {
        memset(out_256, 0, LANG_WORD_DIM);
        
        // Simple deterministic spatial hashing
        uint32_t hash1 = 5381;
        uint32_t hash2 = 0;
        for (char c : word) {
            hash1 = ((hash1 << 5) + hash1) + c; // djb2
            hash2 = c + (hash2 << 6) + (hash2 << 16) - hash2; // sdbm
        }

        // Scatter spikes based on hash seeds to create a unique 2D "fingerprint"
        for (int i = 0; i < LANG_WORD_DIM; i++) {
            // Pseudo-random bit derivation from the hashes
            uint32_t mix = (hash1 * (i + 1)) ^ (hash2 >> (i % 16));
            if (mix % 100 < 35) { // 35% spike sparsity
                out_256[i] = 1;
            } else {
                out_256[i] = 0;
            }
        }
    }
};

// ============================================================
// LANGUAGE CORTEX (Modified Perception Module for 256-dim words)
// ============================================================
struct LanguageCortex {
    struct Cluster {
        int8_t weights[LANG_WORD_DIM];
        float accum[LANG_WORD_DIM];
        int sample_count;
        bool frozen;
        bool active;
        char word_label[64]; // Stores the original word for Generative Output
    };

    Cluster clusters[LANG_CLUSTERS];

    void init() {
        for (int c = 0; c < LANG_CLUSTERS; c++) {
            memset(clusters[c].weights, 0, LANG_WORD_DIM);
            memset(clusters[c].accum, 0, sizeof(clusters[c].accum));
            clusters[c].sample_count = 0;
            clusters[c].frozen = false;
            clusters[c].active = false;
            clusters[c].word_label[0] = '\0';
        }
    }

    int active_count() {
        int n = 0;
        for (int c = 0; c < LANG_CLUSTERS; c++) if (clusters[c].active) n++;
        return n;
    }

    const char* get_word(int cluster_id) const {
        if (cluster_id >= 0 && cluster_id < LANG_CLUSTERS && clusters[cluster_id].active) {
            return clusters[cluster_id].word_label;
        }
        return "";
    }

    // Perceive a 256-dim word hash
    int perceive(const int8_t* input, bool learn, const char* word = nullptr) {
        int best = -1, first_empty = -1;
        float best_sim = -9999.0f;

        // Language requires HIGH vigilance (0.85) to distinguish very similar words
        float vigilance = 0.85f;

        for (int c = 0; c < LANG_CLUSTERS; c++) {
            if (!clusters[c].active) {
                if (first_empty == -1) first_empty = c;
                continue;
            }
            int score = 0, act = 0;
            for (int i = 0; i < LANG_WORD_DIM; i++) {
                if (input[i] != 0 || clusters[c].weights[i] != 0) {
                    if (clusters[c].weights[i] == input[i]) score++; else score--;
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

        if (best != -1 && learn && !clusters[best].frozen) {
            clusters[best].sample_count++;
            float lr = std::max(0.1f, 1.0f / (float)clusters[best].sample_count);
            for (int i = 0; i < LANG_WORD_DIM; i++) {
                clusters[best].accum[i] += lr * ((float)input[i] - clusters[best].accum[i]);
                if (clusters[best].accum[i] > 0.4f) clusters[best].weights[i] = 1;
                else if (clusters[best].accum[i] < -0.4f) clusters[best].weights[i] = -1;
                else clusters[best].weights[i] = 0;
            }
            clusters[best].active = true;
            if (word != nullptr && clusters[best].word_label[0] == '\0') {
                strncpy(clusters[best].word_label, word, 63);
                clusters[best].word_label[63] = '\0';
            }
            // Language freezes very fast (5 exposures)
            if (clusters[best].sample_count >= 5)
                clusters[best].frozen = true;
        }
        return best;
    }
};
