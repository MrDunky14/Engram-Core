// R2 gate: translation cortex + binding hub (perimeter / ensemble wiring)
#include "cluster_graph.h"
#include "fpsan_binding_hub.h"
#include "fpsan_language.h"
#include "fpsan_translation_cortex.h"

#include <cstdio>
#include <cstring>

int main() {
    fpsan::TranslationCortex tr;
    LanguageCortex cx;
    cx.init();
    tr.bind_label(7, "alpha", &cx);
    if (tr.id_for_token("alpha") != 7 || strcmp(tr.label_for_id(7), "alpha") != 0) {
        puts("FAIL translation round-trip");
        return 1;
    }
    ClusterGraph g;
    g.init(200);
    int m = g.spawn();
    int h = fpsan::create_binding_hub(&g);
    fpsan::hub_link_member(&g, h, m, 0.55f);
    bool ok = false;
    int ec = g.node(h).edge_count.load();
    for (int e = 0; e < ec; ++e) {
        if (g.node(h).edges[e].type == EDGE_ENSEMBLE_LINK && g.node(h).edges[e].target == m) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        puts("FAIL ensemble edge");
        return 1;
    }
    puts("PASS");
    return 0;
}
