// R1 ablation: neuromod plasticity_scale gates STDP; bypass vs RPE-driven paths must diverge.
// Build: scripts\compile_research_gates.bat

#include "cluster_graph.h"
#include "fpsan_neuromod.h"

#include <cmath>
#include <cstdio>

static float stdp_delta_one_graph(bool ablation_bypass, float prediction_error_01) noexcept {
    if (ablation_bypass)
        fpsan::neuromod_set_plasticity_ablation_bypass(true);
    else {
        fpsan::neuromod_set_plasticity_ablation_bypass(false);
        fpsan::neuromod_update_from_prediction_error(prediction_error_01);
    }

    ClusterGraph g;
    g.init(128);
    int a = g.spawn(), b = g.spawn();
    g.node(a).add_edge(b, 1.0f, EDGE_TEMPORAL);
    float w0 = g.node(a).edges[0].weight;
    g.apply_stdp(a, b, 2.0f);
    float dw = g.node(a).edges[0].weight - w0;

    fpsan::neuromod_set_plasticity_ablation_bypass(false);
    return dw;
}

int main() {
    constexpr int kSeeds = 8;
    float bypass_sum = 0.f;
    float rpe_hi_sum = 0.f;
    float rpe_lo_sum = 0.f;

    for (int s = 0; s < kSeeds; ++s) {
        (void)s;
        bypass_sum += stdp_delta_one_graph(true, 0.5f);
        rpe_hi_sum += stdp_delta_one_graph(false, 0.95f);
        rpe_lo_sum += stdp_delta_one_graph(false, 0.02f);
    }

    float bypass_mean = bypass_sum / (float)kSeeds;
    float rpe_hi_mean = rpe_hi_sum / (float)kSeeds;
    float rpe_lo_mean = rpe_lo_sum / (float)kSeeds;

    printf("R1 ablation (%d seeds): mean_dW bypass=%.6f rpe_hi=%.6f rpe_lo=%.6f\n", kSeeds,
           bypass_mean, rpe_hi_mean, rpe_lo_mean);

    fpsan::neuromod().plasticity_scale.store(1.0f, std::memory_order_relaxed);
    fpsan::neuromod().last_prediction_error.store(0.5f, std::memory_order_relaxed);

    if (bypass_mean <= 0.f || rpe_hi_mean <= 0.f || rpe_lo_mean <= 0.f) {
        puts("FAIL: STDP did not increase weight");
        return 1;
    }
    if (rpe_hi_mean <= rpe_lo_mean * 1.2f) {
        puts("FAIL: high vs low RPE did not separate STDP magnitude");
        return 1;
    }
    if (rpe_lo_mean >= bypass_mean * 0.65f) {
        puts("FAIL: low-RPE plasticity should sit below ablation baseline");
        return 1;
    }
    puts("PASS");
    return 0;
}
