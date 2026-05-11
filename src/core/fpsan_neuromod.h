#pragma once
// ============================================================
// Research program R1 — Global neuromodulation (cache-friendly)
// Plasticity multiplier (dopamine-shaped) + arousal-driven spread cutoff.
// No per-node receptor floats.
// ============================================================

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace fpsan {

struct NeuromodState {
    /// Multiplies STDP Δw (and assign_credit path). RPE-shaped driver updates this.
    std::atomic<float> plasticity_scale{1.0f};
    /// [0,1] global arousal — high → lower effective activation cutoff (wide spread).
    std::atomic<float> arousal{0.25f};
    /// Last committed prediction error in [0,1] (for telemetry / R1 ablation gates).
    std::atomic<float> last_prediction_error{0.5f};
    /// EMA of prediction error for smoother dopamine (optional smoothing).
    std::atomic<float> pe_ema{0.5f};
    static constexpr float pe_ema_alpha = 0.2f;
};

inline NeuromodState& neuromod() noexcept {
    static NeuromodState g;
    return g;
}

inline float plasticity_scale_load() noexcept {
    float v = neuromod().plasticity_scale.load(std::memory_order_relaxed);
    return std::max(0.0f, std::min(3.0f, v));
}

inline float arousal_load() noexcept {
    float a = neuromod().arousal.load(std::memory_order_relaxed);
    return std::max(0.0f, std::min(1.0f, a));
}

inline float last_prediction_error_load() noexcept {
    return neuromod().last_prediction_error.load(std::memory_order_relaxed);
}

/// Maps frustration / doubt from HomeostaticDrives into arousal (merge, do not add a third knob).
inline void neuromod_sync_arousal(float frustration_01, float doubt_01) noexcept {
    float fr = std::max(0.0f, std::min(1.0f, frustration_01));
    float db = std::max(0.0f, std::min(1.0f, doubt_01));
    float a = 0.35f * fr + 0.25f * db + 0.05f;
    neuromod().arousal.store(std::min(1.0f, a), std::memory_order_relaxed);
}

/// `prediction_error` in [0,1] — high when model is surprised (RPE-shaped plasticity gate).
inline void neuromod_update_from_prediction_error(float prediction_error) noexcept {
    float pe = std::max(0.0f, std::min(1.0f, prediction_error));
    auto& g = neuromod();
    g.last_prediction_error.store(pe, std::memory_order_relaxed);

    float prev = g.pe_ema.load(std::memory_order_relaxed);
    float ema = NeuromodState::pe_ema_alpha * pe + (1.0f - NeuromodState::pe_ema_alpha) * prev;
    g.pe_ema.store(ema, std::memory_order_relaxed);

    // Plasticity high when surprise high; freeze-ish when ema ~ 0
    float scale = std::max(0.05f, std::min(1.5f, ema * 1.25f));
    g.plasticity_scale.store(scale, std::memory_order_relaxed);
}

/// Effective minimum propagation strength for spread_activation (modulated by arousal).
inline float neuromod_effective_activation_cutoff(float base_cutoff) noexcept {
    float a = arousal_load();
    // Low arousal → stricter (higher cutoff); high arousal → permissive.
    return base_cutoff * (1.25f - 0.75f * a);
}

/// R1 ablation: force full plasticity (disables dopamine gate).
inline void neuromod_set_plasticity_ablation_bypass(bool on) noexcept {
    if (on)
        neuromod().plasticity_scale.store(1.0f, std::memory_order_relaxed);
}

} // namespace fpsan
