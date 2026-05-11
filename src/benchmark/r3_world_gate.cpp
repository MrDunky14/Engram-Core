// R3 gate: world-model transition error + neuromod hook
#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_neuromod.h"
#include "fpsan_world_model.h"

#include <cstdio>

static int p(SpikingTokenizer& t, LanguageCortex& c, const char* w) {
    int8_t h[LANG_WORD_DIM];
    t.encode_word_hash(std::string(w), h);
    return c.perceive(h, true, w);
}

int main() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(400);
    cx.init();
    int a = p(tok, cx, "s_a");
    int b = p(tok, cx, "s_b");
    int c_id = p(tok, cx, "s_c");
    g.node(a).add_edge(b, 2.0f, EDGE_CAUSES, PROV_USER);
    float err_match = fpsan::world_model_transition_error(&g, a, b);
    float err_mismatch = fpsan::world_model_transition_error(&g, a, c_id);
    if (err_match >= err_mismatch) {
        puts("FAIL: mismatch should look worse");
        return 1;
    }
    fpsan::neuromod_update_from_prediction_error(err_mismatch);
    if (fpsan::plasticity_scale_load() < 0.01f) {
        puts("FAIL: plasticity collapsed");
        return 1;
    }
    puts("PASS");
    return 0;
}
