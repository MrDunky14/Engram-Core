#pragma once
// ============================================================
// FP-SAN Phase 16C: METACOGNITION
// fpsan_metacognition.h — JARVIS knows what it doesn't know.
//
// Uses activation entropy at branch points to compute confidence.
// During text generation, every time the autoregressive loop hits
// a hub word (node with multiple NEXT_WORD edges), the margin
// between the best and second-best candidate is recorded.
//
// High margin = high confidence (one clear path)
// Low margin  = low confidence (ambiguous, multiple paths)
// No edges    = zero confidence (unknown topic)
//
// This is the same physics as competitive inhibition — we just
// surface it as a user-visible metric instead of silently halting.
// ============================================================

#include <cstdio>
#include <cmath>

struct MetaCognition {
    float  total_margin;         // Sum of all (best - second_best) margins
    int    branch_points;        // Number of hub nodes encountered
    int    words_generated;      // Total words in output
    int    words_before_stop;    // Words before generation halted
    float  min_margin;           // Smallest margin seen (weakest point)
    float  max_margin;           // Largest margin seen (strongest point)
    bool   stopped_at_hub;       // Did generation halt due to ambiguity?
    bool   unknown_seed;         // Was the seed word unknown?

    void reset() {
        total_margin = 0.0f;
        branch_points = 0;
        words_generated = 0;
        words_before_stop = 0;
        min_margin = 999.0f;
        max_margin = -999.0f;
        stopped_at_hub = false;
        unknown_seed = false;
    }

    // Record a branch point during generation
    // Called at every hub node (>1 NEXT_WORD edge)
    void record_branch(float best_score, float second_best, int word_pos) {
        float margin = best_score - second_best;
        total_margin += margin;
        branch_points++;
        words_before_stop = word_pos;

        if (margin < min_margin) min_margin = margin;
        if (margin > max_margin) max_margin = margin;
    }

    // Compute overall confidence score [0.0, 1.0]
    float compute_confidence() {
        if (unknown_seed) return 0.0f;
        if (words_generated == 0) return 0.0f;
        if (branch_points == 0) return 1.0f; // No ambiguity = full confidence

        float avg_margin = total_margin / (float)branch_points;

        // Map avg_margin to [0, 1]:
        //   margin >= 0.3 → confidence 1.0 (strong single path)
        //   margin ~= 0.1 → confidence ~0.3 (ambiguous)
        //   margin <= 0.0 → confidence 0.0 (total confusion)
        float confidence = avg_margin / 0.3f;
        if (confidence > 1.0f) confidence = 1.0f;
        if (confidence < 0.0f) confidence = 0.0f;

        // Penalize if generation was very short (< 3 words) despite having content
        if (words_generated < 3 && branch_points > 0) {
            confidence *= 0.6f; // 40% penalty for truncated output
        }

        return confidence;
    }

    // Human-readable confidence label
    const char* confidence_label() {
        float c = compute_confidence();
        if (c >= 0.7f) return "HIGH";
        if (c >= 0.4f) return "MEDIUM";
        if (c >= 0.1f) return "LOW";
        return "UNKNOWN";
    }

    // Human-readable confidence color (Windows console color code)
    int confidence_color() {
        float c = compute_confidence();
        if (c >= 0.7f) return 10;  // Green
        if (c >= 0.4f) return 14;  // Yellow
        if (c >= 0.1f) return 12;  // Red
        return 8;                   // Dark Gray
    }

    // Print detailed metacognition report
    void print_report() {
        float c = compute_confidence();
        printf("  [Metacognition]\n");
        printf("    Confidence:    %.2f (%s)\n", c, confidence_label());
        printf("    Words:         %d generated\n", words_generated);
        printf("    Branch points: %d\n", branch_points);
        if (branch_points > 0) {
            printf("    Avg margin:    %.3f\n", total_margin / (float)branch_points);
            printf("    Min margin:    %.3f (weakest link)\n", min_margin);
            printf("    Max margin:    %.3f (strongest link)\n", max_margin);
        }
        if (stopped_at_hub) {
            printf("    Stopped:       at hub word (ambiguous branch)\n");
        }
        if (unknown_seed) {
            printf("    Seed:          UNKNOWN (not in knowledge graph)\n");
        }
    }
};
