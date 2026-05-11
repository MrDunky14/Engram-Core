// R3 closed-loop gate: graph world-model vs Wasm MDP step + neuromod RPE wiring.
// Loads fixtures/phase_r3/micro_mdp.wasm (run scripts\build_phase_r3_wasm.bat).
// Build: scripts\compile_research_gates.bat

#include "cluster_graph.h"
#include "fpsan_neuromod.h"
#include "fpsan_wasm_sandbox.h"
#include "fpsan_world_model.h"

#include <cstdio>
#include <fstream>
#include <vector>

static std::vector<uint8_t> read_wasm_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf((size_t)sz);
    if (sz > 0)
        f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

int main(int argc, char** argv) {
    const char* path = (argc > 1 && argv[1]) ? argv[1] : "fixtures/phase_r3/micro_mdp.wasm";
    std::vector<uint8_t> wasm = read_wasm_file(path);
    if (wasm.empty()) {
        printf("FAIL: could not read %s (run scripts\\build_phase_r3_wasm.bat)\n", path);
        return 1;
    }

    fpsan::WasmSandbox sandbox;
    fpsan::WasmSandboxBudget b{};
    if (!sandbox.load(wasm.data(), wasm.size(), b)) {
        printf("FAIL: wasm load: %s\n", sandbox.last_trap() ? sandbox.last_trap() : "?");
        return 1;
    }

    ClusterGraph g;
    g.init(64);
    int s0 = g.spawn();
    int s1 = g.spawn();
    int s2 = g.spawn();
    g.node(s0).add_edge(s1, 2.0f, EDGE_CAUSES, PROV_USER);
    g.node(s1).add_edge(s2, 2.0f, EDGE_CAUSES, PROV_USER);
    // Absorbing state: Wasm stays at 2 — self-loop so transition error is not a hard mismatch.
    g.node(s2).add_edge(s2, 2.0f, EDGE_CAUSES, PROV_USER);

    const int map[3] = {s0, s1, s2};
    int wasm_state = 0;
    float sum_err = 0.f;
    int steps = 0;

    for (int t = 0; t < 5; ++t) {
        int cur_c = map[wasm_state];
        fpsan::WasmEvalResult wr = sandbox.call_i32_1("step", (int32_t)wasm_state);
        if (!wr.ok) {
            printf("FAIL: wasm step trap %s\n", wr.err ? wr.err : "?");
            return 1;
        }
        int next_ws = (int)wr.i64;
        if (next_ws < 0 || next_ws > 2)
            next_ws = 2;
        int next_c = map[next_ws];

        float err = fpsan::world_model_transition_error(&g, cur_c, next_c);
        sum_err += err;
        fpsan::neuromod_update_from_prediction_error(err);
        steps++;

        wasm_state = next_ws;
        if (wasm_state == 2 && t >= 2)
            break;
    }

    float ps = fpsan::plasticity_scale_load();
    fpsan::neuromod().plasticity_scale.store(1.0f, std::memory_order_relaxed);
    fpsan::neuromod().last_prediction_error.store(0.5f, std::memory_order_relaxed);

    printf("R3 wasm loop: steps=%d mean_transition_err=%.4f plasticity_scale=%.4f\n", steps,
           (steps > 0) ? (sum_err / (float)steps) : 0.f, ps);

    float mean_err = (steps > 0) ? (sum_err / (float)steps) : 99.f;
    // Matched transitions yield residual error (1 - confidence), not 0 — expect mean well below mismatch (1.0).
    if (steps < 1 || mean_err > 0.60f) {
        puts("FAIL: world model should track Wasm MDP (mean transition error too high)");
        return 1;
    }
    if (ps < 0.05f || ps > 2.0f) {
        puts("FAIL: neuromod scale out of sane range after RPE");
        return 1;
    }
    puts("PASS");
    return 0;
}
