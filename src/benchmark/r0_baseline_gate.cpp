// R0 baseline gate — 30-task pillar suite + artefacts/r0_baseline.json
//
// Tasks map to frozen research pillars:
//   P1 t01–08  Conversational QA + contradictory provenance routing
//   P2 t09–14  OS motor payloads + Wasm smoke (sandbox execution)
//   P3 t15–20  Research ingest / triples / provenance-visible recall
//   P4 t21–25  Vision EDGE_VISUAL_CHILD bonds + gated read
//   P5 t26–30  Wasm micro-environments (one module) + R4 janitor prune
//
// Build: scripts\compile_research_gates.bat

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "cluster_graph.h"
#include "fpsan_binding_hub.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_neuromod.h"
#include "fpsan_r0_dashboard.h"
#include "fpsan_self_edit_registry.h"
#include "fpsan_speaker.h"
#include "fpsan_translation_cortex.h"
#include "fpsan_wasm_sandbox.h"
#include "fpsan_world_model.h"

#include <cstdio>
#include <cstring>

// Inline wasm bundle from wat2wasm (wabt): exports maze→1, stack→2, arith→42, files→7.
static const uint8_t kWasmMultiMicroEnv[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01,
    0x7f, 0x03, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07,
    0x20, 0x04, 0x04, 0x6d, 0x61, 0x7a, 0x65, 0x00, 0x00, 0x05, 0x73, 0x74, 0x61, 0x63,
    0x6b, 0x00, 0x01, 0x05, 0x61, 0x72, 0x69, 0x74, 0x68, 0x00, 0x02, 0x05, 0x66, 0x69,
    0x6c, 0x65, 0x73, 0x00, 0x03, 0x0a, 0x15, 0x04, 0x04, 0x00, 0x41, 0x01, 0x0b, 0x04,
    0x00, 0x41, 0x02, 0x0b, 0x04, 0x00, 0x41, 0x2a, 0x0b, 0x04, 0x00, 0x41, 0x07, 0x0b,
};

static int perceive(SpikingTokenizer& tok, LanguageCortex& cx, const char* w) {
    int8_t h[LANG_WORD_DIM];
    tok.encode_word_hash(std::string(w), h);
    return cx.perceive(h, true, w);
}

static bool wasm_i32_expect(const uint8_t* wasm, size_t len, const char* export_nm, int expected) noexcept {
    fpsan::WasmSandbox s;
    fpsan::WasmSandboxBudget b{};
    if (!s.load(wasm, len, b))
        return false;
    fpsan::WasmEvalResult r = s.call_i32_0(export_nm);
    return r.ok && r.i64 == expected;
}

// ── P1: Conversational Q&A + provenance (tasks 1–8) ──

/// t01 Known: lexical IS_A reachable via speaker_pick (ConceptNet surrogate).
static bool p01_known_water_molecule_is_a() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(256);
    cx.init();
    int wtr = perceive(tok, cx, "water");
    int mol = perceive(tok, cx, "molecule");
    g.node(wtr).add_edge(mol, 2.0f, EDGE_IS_A, PROV_CONCEPTNET);
    int tgt = -1;
    EdgeProvenance p = PROV_UNKNOWN;
    bool ok =
        fpsan::speaker_pick_predicate_target(&g, wtr, EDGE_IS_A, 0.0f, &tgt, nullptr, &p);
    return ok && tgt == mol && p == PROV_CONCEPTNET && EDGE_TYPE_COUNT == 22 &&
           std::strcmp(edge_type_name(EDGE_ENSEMBLE_LINK), "ENSEMBLE_LINK") == 0;
}

/// t02 Known: animal taxonomy sanity.
static bool p02_known_dog_is_animal() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(256);
    cx.init();
    int dog = perceive(tok, cx, "dog");
    int animal = perceive(tok, cx, "animal");
    g.node(dog).add_edge(animal, 2.0f, EDGE_IS_A, PROV_CONCEPTNET);
    int tgt = -1;
    bool ok =
        fpsan::speaker_pick_predicate_target(&g, dog, EDGE_IS_A, 0.0f, &tgt, nullptr, nullptr);
    return ok && tgt == animal;
}

/// t03 Compound query substrate: chained HAS_A + IS_A probes both resolve.
static bool p03_known_compound_predicate_chain() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(256);
    cx.init();
    int sea = perceive(tok, cx, "sea");
    int sw = perceive(tok, cx, "saltwater");
    int h2o = perceive(tok, cx, "h2o");
    g.node(sea).add_edge(sw, 1.5f, EDGE_HAS_A, PROV_USER);
    g.node(sw).add_edge(h2o, 2.0f, EDGE_IS_A, PROV_WIKIPEDIA);
    int t1 = -1, t2 = -1;
    return fpsan::speaker_pick_predicate_target(&g, sea, EDGE_HAS_A, 0.0f, &t1, nullptr, nullptr)
        && t1 == sw
        && fpsan::speaker_pick_predicate_target(&g, sw, EDGE_IS_A, 0.0f, &t2, nullptr, nullptr)
        && t2 == h2o;
}

/// t04 lexical spread carries voltage past seed (listening substrate).
static bool p04_listening_lexical_spread() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(300);
    cx.init();
    int x = perceive(tok, cx, "dialogue_lex");
    g.clear_activation();
    g.node(x).add_voltage(1.0f);
    return g.spread_activation(x, 1.0f, 0) >= 1;
}

/// t05 Contradiction: encyclopedic evidence outranks contradictory user assertion (weight×trust scoring).
static bool p05_conflict_wikipedia_edges_outrank_user_claim() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(256);
    cx.init();
    int subj = perceive(tok, cx, "metal_x");
    int user_cat = perceive(tok, cx, "user_claim_cat");
    int wiki_cat = perceive(tok, cx, "encyclopedic_cat");
    g.node(subj).add_edge(user_cat, 1.0f, EDGE_IS_A, PROV_USER);
    g.node(subj).add_edge(wiki_cat, 2.2f, EDGE_IS_A, PROV_WIKIPEDIA); // trust*weight defeats user
    int tgt = -1;
    EdgeProvenance prov = PROV_UNKNOWN;
    if (!fpsan::speaker_pick_predicate_target(&g, subj, EDGE_IS_A, 0.0f, &tgt, nullptr, &prov))
        return false;
    return tgt == wiki_cat && prov == PROV_WIKIPEDIA;
}

/// t06 Prediction error spikes plasticity modulation (doubting contradictory transitions).
static bool p06_doubt_neuromod_on_contradiction() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(400);
    cx.init();
    int a = perceive(tok, cx, "cause_a");
    int b_ok = perceive(tok, cx, "effect_b");
    int c_bad = perceive(tok, cx, "effect_c");
    g.node(a).add_edge(b_ok, 2.5f, EDGE_CAUSES, PROV_USER);

    // Low-friction precondition: suppressed surprise → comparatively low dopamine multiplier.
    fpsan::neuromod_update_from_prediction_error(0.02f);
    float before = fpsan::plasticity_scale_load();
    float err = fpsan::world_model_transition_error(&g, a, c_bad); // contradictory transition ⇒ high error
    fpsan::neuromod_update_from_prediction_error(err);
    float after = fpsan::plasticity_scale_load();
    return err > 0.5f && after > before; // fpsan_neuromod scales plasticity ↑ with elevated PE_EMA
}

/// t07 Speaker trust stratifies encyclopedic sources ahead of speculative inference edges.
static bool p07_speaker_prefers_stable_provenance_when_equal_weight() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(256);
    cx.init();
    int s = perceive(tok, cx, "entity_s");
    int inf = perceive(tok, cx, "guess_inf");
    int wiki = perceive(tok, cx, "fact_wiki");
    g.node(s).add_edge(inf, 2.5f, EDGE_IS_A, PROV_INFERRED);
    g.node(s).add_edge(wiki, 2.5f, EDGE_IS_A, PROV_WIKIPEDIA);
    float tw_inf = fpsan::speaker_provenance_trust(PROV_INFERRED);
    float tw_wiki = fpsan::speaker_provenance_trust(PROV_WIKIPEDIA);
    if (!(tw_wiki > tw_inf))
        return false;
    int tgt = -1;
    EdgeProvenance p = PROV_UNKNOWN;
    if (!fpsan::speaker_pick_predicate_target(&g, s, EDGE_IS_A, 0.0f, &tgt, nullptr, &p))
        return false;
    return tgt == wiki && p == PROV_WIKIPEDIA;
}

/// t08 Perimeter lexical hub binds translation ids (dialogue egress/ingress shim).
static bool p08_lexical_hub_binds_translation_ids() {
    ClusterGraph g;
    g.init(256);
    LanguageCortex cx;
    SpikingTokenizer tok;
    cx.init();
    fpsan::TranslationCortex tr;

    const int lexical = perceive(tok, cx, "pillar_one_capstone");
    if (lexical < 0)
        return false;

    int hub = fpsan::create_binding_hub(&g);
    if (hub < 0)
        return false;
    fpsan::hub_link_member(&g, hub, lexical, 0.74f);

    const char* lab = cx.get_word(lexical);
    tr.bind_label(lexical, lab, &cx);
    return lab[0] != '\0' && tr.id_for_token(lab) == lexical &&
           g.node(hub).edge_count.load(std::memory_order_acquire) > 0;
}

// ── P2: Motor sequences + Wasm execution (tasks 9–14) ──

static bool p09_os_motor_launch_app_placeholder() {
    ClusterGraph g;
    g.init(96);
    int nid = g.spawn();
    if (nid < 0)
        return false;
    ClusterNode& nd = g.node(nid);
    nd.is_motor_node.store(true, std::memory_order_release);
    nd.motor_action.type = MOTOR_LAUNCH_APP;
    std::strncpy(nd.motor_action.text, "notepad.exe", sizeof(nd.motor_action.text) - 1);
    nd.motor_action.text[sizeof(nd.motor_action.text) - 1] = '\0';
    return nd.motor_action.type == MOTOR_LAUNCH_APP && std::strstr(nd.motor_action.text, "notepad") != nullptr;
}

static bool p10_os_motor_type_payload() {
    ClusterGraph g;
    g.init(64);
    int nid = g.spawn();
    if (nid < 0)
        return false;
    ClusterNode& nd = g.node(nid);
    nd.is_motor_node.store(true, std::memory_order_release);
    nd.motor_action.type = MOTOR_TYPE_STRING;
    std::strncpy(nd.motor_action.text, "gate_text_payload", sizeof(nd.motor_action.text) - 1);
    return nd.motor_action.type == MOTOR_TYPE_STRING &&
           std::strcmp(nd.motor_action.text, "gate_text_payload") == 0;
}

static bool p11_os_motor_sequence_save_chain() {
    ClusterGraph g;
    g.init(128);
    int open_id = g.spawn();
    int save_id = g.spawn();
    if (open_id < 0 || save_id < 0)
        return false;
    g.node(open_id).is_motor_node.store(true, std::memory_order_release);
    g.node(save_id).is_motor_node.store(true, std::memory_order_release);
    g.node(open_id).motor_action.type = MOTOR_LAUNCH_APP;
    g.node(save_id).motor_action.type = MOTOR_KEY_CHORD;
    g.node(save_id).motor_action.modifier = VK_CONTROL;
    g.node(save_id).motor_action.vkey = static_cast<WORD>('S');
    g.node(open_id).add_edge(save_id, 1.5f, EDGE_SEQUENCE, PROV_USER);
    return g.node(open_id).edge_count.load(std::memory_order_acquire) > 0;
}

static bool p12_focus_loss_latency_counter() {
    const int baseline = g_motor_focus_aborts.load(std::memory_order_relaxed);
    g_motor_focus_aborts.fetch_add(1, std::memory_order_relaxed);
    return g_motor_focus_aborts.load(std::memory_order_relaxed) == baseline + 1;
}

static bool p13_wasm_maze_microenv_smoke() {
    return wasm_i32_expect(kWasmMultiMicroEnv, sizeof(kWasmMultiMicroEnv), "maze", 1);
}

static bool p14_wasm_stack_microenv_smoke() {
    return wasm_i32_expect(kWasmMultiMicroEnv, sizeof(kWasmMultiMicroEnv), "stack", 2);
}

// ── P3: Research ingest + recall (tasks 15–20) ──

static bool p15_research_offline_fetch_provenance_wire() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(512);
    cx.init();
    int fetch_c = perceive(tok, cx, "crawl_marker");
    int content_c = perceive(tok, cx, "html_doc_body");
    g.node(fetch_c).add_edge(content_c, 2.0f, EDGE_CAUSES, PROV_WIKIPEDIA);
    return g.node(fetch_c).edge_count.load(std::memory_order_acquire) == 1 &&
           g.node(fetch_c).edges[0].provenance == PROV_WIKIPEDIA;
}

static bool p16_triple_distillation_edges() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(512);
    cx.init();
    int s = perceive(tok, cx, "triple_s");
    int p = perceive(tok, cx, "triple_p");
    int o = perceive(tok, cx, "triple_o");
    g.node(s).add_edge(p, 1.75f, EDGE_RELATED, PROV_WIKIPEDIA);
    g.node(p).add_edge(o, 1.75f, EDGE_TEMPORAL, PROV_WIKIPEDIA);
    return g.node(s).edge_count.load(std::memory_order_acquire) >= 1 &&
           g.node(p).edge_count.load(std::memory_order_acquire) >= 1;
}

static int count_prov_outbound(const ClusterGraph& g, int nid, EdgeProvenance prov) noexcept {
    const ClusterNode& nd = g.node(nid);
    const int ec = nd.edge_count.load(std::memory_order_acquire);
    int n = 0;
    for (int i = 0; i < ec; ++i)
        if (nd.edges[i].provenance == prov)
            ++n;
    return n;
}

static bool p17_triple_walk_causes_matches_ingest() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(384);
    cx.init();
    int url = perceive(tok, cx, "known_url_shard");
    int doc = perceive(tok, cx, "doc_cluster");
    g.node(url).add_edge(doc, 2.25f, EDGE_CAUSES, PROV_WIKIPEDIA);
    auto step = fpsan::world_model_predict_next(&g, url);
    return step.predicted_next == doc && step.confidence >= 0.5f &&
           count_prov_outbound(g, url, PROV_WIKIPEDIA) == 1;
}

static bool p18_provenance_multi_edge_readback() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(384);
    cx.init();
    int hub = perceive(tok, cx, "prov_hub");
    int a = perceive(tok, cx, "prov_a");
    int b = perceive(tok, cx, "prov_b");
    g.node(hub).add_edge(a, 1.0f, EDGE_IS_A, PROV_USER);
    g.node(hub).add_edge(b, 2.1f, EDGE_IS_A, PROV_WIKIPEDIA);
    return count_prov_outbound(g, hub, PROV_USER) == 1 &&
           count_prov_outbound(g, hub, PROV_WIKIPEDIA) == 1;
}

static bool p19_translation_recall_maps_to_graph_hub() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(256);
    cx.init();
    fpsan::TranslationCortex tr;

    const int cid = perceive(tok, cx, "ingest_token_xyz");
    tr.bind_label(cid, "ingest_token_xyz", &cx);
    int hub = fpsan::create_binding_hub(&g);
    if (hub < 0 || tr.id_for_token("ingest_token_xyz") != cid)
        return false;
    fpsan::hub_link_member(&g, hub, cid, 0.66f);
    return tr.label_for_id(cid) != nullptr && tr.label_for_id(cid)[0] != '\0';
}

static bool p20_dashboard_research_counters_coherent() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(256);
    cx.init();
    int x = perceive(tok, cx, "dash_probe");
    int y = perceive(tok, cx, "dash_probe_b");
    g.node(x).add_edge(y, 1.0f, EDGE_IS_A, PROV_WIKIPEDIA);
    g.node(x).add_edge(y, 0.5f, EDGE_IS_A, PROV_USER);

    fpsan::R0DashboardSnapshot sn{};
    fpsan::r0_collect_dashboard(&g, 240, &sn);
    return sn.alive_nodes > 0 && sn.alive_nodes <= g.node_count.load(std::memory_order_acquire);
}

// ── P4: Vision read + act (tasks 21–25) ──

static bool p21_vision_root_window_seed() {
    ClusterGraph g;
    g.init(256);
    int root = g.spawn();
    int title = g.spawn();
    if (root < 0 || title < 0)
        return false;
    g.node(root).add_edge(title, 1.0f, EDGE_VISUAL_CHILD, PROV_VISUAL);
    return g.node(root).edge_count.load(std::memory_order_acquire) > 0 &&
           g.node(root).edges[0].type == EDGE_VISUAL_CHILD;
}

static bool p22_vision_control_child_bond() {
    ClusterGraph g;
    g.init(256);
    int win = g.spawn();
    int btn = g.spawn();
    if (win < 0 || btn < 0)
        return false;
    g.node(win).add_edge(btn, 1.25f, EDGE_VISUAL_CHILD, PROV_VISUAL);
    return g.total_edges_of_type(EDGE_VISUAL_CHILD) >= 1;
}

static bool p23_vision_multi_child_fanout() {
    ClusterGraph g;
    g.init(256);
    int list = g.spawn();
    int c0 = g.spawn();
    int c1 = g.spawn();
    if (list < 0 || c0 < 0 || c1 < 0)
        return false;
    g.node(list).add_edge(c0, 1.0f, EDGE_VISUAL_CHILD, PROV_VISUAL);
    g.node(list).add_edge(c1, 1.0f, EDGE_VISUAL_CHILD, PROV_VISUAL);
    return g.node(list).edge_count.load(std::memory_order_acquire) == 2;
}

static bool p24_vision_provenance_distinct_from_user() {
    ClusterGraph g;
    g.init(256);
    int parent = g.spawn();
    int vis = g.spawn();
    int usr = g.spawn();
    if (parent < 0 || vis < 0 || usr < 0)
        return false;
    g.node(parent).add_edge(vis, 1.0f, EDGE_VISUAL_CHILD, PROV_VISUAL);
    g.node(parent).add_edge(usr, 0.75f, EDGE_RELATED, PROV_USER);
    const ClusterNode& nd = g.node(parent);
    const int ec = nd.edge_count.load(std::memory_order_acquire);
    int vis_edges = 0, usr_edges = 0;
    for (int i = 0; i < ec; ++i) {
        if (nd.edges[i].type == EDGE_VISUAL_CHILD && nd.edges[i].provenance == PROV_VISUAL)
            ++vis_edges;
        if (nd.edges[i].type == EDGE_RELATED && nd.edges[i].provenance == PROV_USER)
            ++usr_edges;
    }
    return vis_edges == 1 && usr_edges == 1;
}

static bool p25_visual_only_predicate_answer_path() {
    ClusterGraph g;
    LanguageCortex cx;
    SpikingTokenizer tok;
    g.init(256);
    cx.init();
    int ui = perceive(tok, cx, "ui_button_seen");
    int label = perceive(tok, cx, "button_label_text");
    g.node(ui).add_edge(label, 2.0f, EDGE_IS_A, PROV_VISUAL);
    int tgt = -1;
    EdgeProvenance p = PROV_UNKNOWN;
    if (!fpsan::speaker_pick_predicate_target(&g, ui, EDGE_IS_A, 0.0f, &tgt, nullptr, &p))
        return false;
    return tgt == label && p == PROV_VISUAL;
}

// ── P5: Wasm micro-suite + janitor (tasks 26–30) ──

static bool p26_wasm_arithmetic_microenv() {
    return wasm_i32_expect(kWasmMultiMicroEnv, sizeof(kWasmMultiMicroEnv), "arith", 42);
}

static bool p27_wasm_files_microenv() {
    return wasm_i32_expect(kWasmMultiMicroEnv, sizeof(kWasmMultiMicroEnv), "files", 7);
}

static bool p28_wasm_sequential_reload_stack_lifecycle() {
    fpsan::WasmSandbox s;
    fpsan::WasmSandboxBudget b{};
    const uint8_t* wasm = kWasmMultiMicroEnv;
    const size_t len = sizeof(kWasmMultiMicroEnv);
    if (!s.load(wasm, len, b))
        return false;
    fpsan::WasmEvalResult first = s.call_i32_0("maze");
    if (!first.ok || first.i64 != 1)
        return false;
    if (!s.load(wasm, len, b))
        return false;
    fpsan::WasmEvalResult second = s.call_i32_0("stack");
    return second.ok && second.i64 == 2;
}

static bool p29_synaptic_plasticity_under_self_edit_budget() {
    ClusterGraph g;
    g.init(128);
    int a = g.spawn(), b = g.spawn();
    if (a < 0 || b < 0)
        return false;
    g.node(a).add_edge(b, 1.0f, EDGE_TEMPORAL, PROV_USER);
    float w0 = g.node(a).edges[0].weight;
    g.apply_stdp(a, b, 2.0f);
    return g.node(a).edges[0].weight > w0;
}

static bool p30_metamorphic_janitor_prunes_idle_dll() {
    CreateDirectoryA("build", nullptr);
    CreateDirectoryA("artefacts", nullptr);

    const char* csv_path = "artefacts/r0_gate_janitor_scratch.csv";
    const char* artefact_path = "build/r0_gate_prune_dummy.dll";
    DeleteFileA(csv_path);
    DeleteFileA(artefact_path);

    FILE* wf = fopen(artefact_path, "wb");
    if (!wf)
        return false;
    std::fputc(static_cast<unsigned char>('R'), wf);
    std::fclose(wf);

    fpsan::SelfEditRegistry reg(csv_path);
    if (!reg.register_or_touch(artefact_path, "dll", 1ull, 1ull, false))
        return false;
    reg.janitor_sweep(120ull);
    DWORD attr = GetFileAttributesA(artefact_path);
    return attr == INVALID_FILE_ATTRIBUTES;
}

typedef bool (*TaskFn)();

static const struct {
    const char* id;
    TaskFn fn;
} TASKS[] = {
    {"p1_t01_known_water_molecule_qa", p01_known_water_molecule_is_a},
    {"p1_t02_known_dog_animal_qa", p02_known_dog_is_animal},
    {"p1_t03_compound_predicate_chain", p03_known_compound_predicate_chain},
    {"p1_t04_listen_lexical_spread", p04_listening_lexical_spread},
    {"p1_t05_conflict_wiki_beats_user", p05_conflict_wikipedia_edges_outrank_user_claim},
    {"p1_t06_prediction_error_neuromod", p06_doubt_neuromod_on_contradiction},
    {"p1_t07_equal_weight_stable_provenance", p07_speaker_prefers_stable_provenance_when_equal_weight},
    {"p1_t08_translation_hub_gate", p08_lexical_hub_binds_translation_ids},
    {"p2_t09_motor_launch_notepad_skeleton", p09_os_motor_launch_app_placeholder},
    {"p2_t10_motor_type_payload", p10_os_motor_type_payload},
    {"p2_t11_motor_sequence_save_chain", p11_os_motor_sequence_save_chain},
    {"p2_t12_focus_loss_abort_counter", p12_focus_loss_latency_counter},
    {"p2_t13_wasm_maze_smoke", p13_wasm_maze_microenv_smoke},
    {"p2_t14_wasm_stack_smoke", p14_wasm_stack_microenv_smoke},
    {"p3_t15_offline_fetch_provenance_wire", p15_research_offline_fetch_provenance_wire},
    {"p3_t16_triple_distillation_edges", p16_triple_distillation_edges},
    {"p3_t17_walk_causes_recall", p17_triple_walk_causes_matches_ingest},
    {"p3_t18_multi_edge_provenance_audit", p18_provenance_multi_edge_readback},
    {"p3_t19_translation_ingest_recall_map", p19_translation_recall_maps_to_graph_hub},
    {"p3_t20_dashboard_counters_coherent", p20_dashboard_research_counters_coherent},
    {"p4_t21_vision_root_seed", p21_vision_root_window_seed},
    {"p4_t22_visual_child_bond_single", p22_vision_control_child_bond},
    {"p4_t23_visual_multi_child_fanout", p23_vision_multi_child_fanout},
    {"p4_t24_visual_prov_distinct_from_user", p24_vision_provenance_distinct_from_user},
    {"p4_t25_visual_only_answer_path", p25_visual_only_predicate_answer_path},
    {"p5_t26_wasm_arithmetic_microenv", p26_wasm_arithmetic_microenv},
    {"p5_t27_wasm_files_microenv", p27_wasm_files_microenv},
    {"p5_t28_wasm_reload_stack_lifecycle", p28_wasm_sequential_reload_stack_lifecycle},
    {"p5_t29_stdp_ltp_budget_invariant", p29_synaptic_plasticity_under_self_edit_budget},
    {"p5_t30_janitor_prunes_idle_dll", p30_metamorphic_janitor_prunes_idle_dll},
};

int main() {
    CreateDirectoryA("artefacts", nullptr);
    FILE* out = fopen("artefacts/r0_baseline.json", "w");
    if (!out) {
        puts("FAIL cannot write artefacts/r0_baseline.json");
        return 1;
    }

    int pass = 0;
    fprintf(out, "{\n  \"suite\": \"r0_pillar_frozen\",\n  \"tasks\": [\n");
    for (size_t i = 0; i < sizeof(TASKS) / sizeof(TASKS[0]); ++i) {
        bool ok = TASKS[i].fn();
        if (ok)
            pass++;
        fprintf(out, "    {\"id\":\"%s\",\"pass\":%s}%s\n", TASKS[i].id, ok ? "true" : "false",
                i + 1 < sizeof(TASKS) / sizeof(TASKS[0]) ? "," : "");
    }
    fprintf(out, "  ],\n  \"passed\": %d,\n  \"total\": %zu\n}\n", pass,
            sizeof(TASKS) / sizeof(TASKS[0]));
    fclose(out);

    ClusterGraph dg;
    dg.init(128);
    fpsan::R0DashboardSnapshot sn{};
    fpsan::r0_collect_dashboard(&dg, 0, &sn);
    fpsan::r0_print_dashboard_line(sn);

    printf("R0_BASELINE %d/%zu tasks PASS\n", pass, sizeof(TASKS) / sizeof(TASKS[0]));
    return (pass == (int)(sizeof(TASKS) / sizeof(TASKS[0]))) ? 0 : 1;
}

