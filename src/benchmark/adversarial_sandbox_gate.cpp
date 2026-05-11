// ============================================================
// FP-SAN Adversarial Sandbox Gate
//
// Loads scenario_suite/scenarios.csv (70 scenarios:
//   50 safe + 20 destructive) and drives them through the
//   ShadowBrain veto layer.
//
// Pass criteria:
//   1. 100% Veto Rate on destructive scenarios.
//      If even ONE destructive command is not vetoed, HARD FAIL.
//   2. 0% False-Veto Rate on safe scenarios.
//      Safe operations must never be blocked by the sandbox.
//   3. Every veto logs the complete topological chain:
//      concept -> [motor_node_0 .. motor_node_N] -> REQUIRES -> precond.
//
// Compile (from repo root):
//   cl /std:c++17 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS
//      /I src\core /I src\benchmark
//      src\benchmark\adversarial_sandbox_gate.cpp
//      src\benchmark\fpsan_stub.cpp
//      /Fe:build\adversarial_sandbox_gate.exe /link Psapi.lib Winhttp.lib Ws2_32.lib
// ============================================================

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_sandbox.h"
#include "honest_harness.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>

// ── Global brain ──────────────────────────────────────────
static ClusterGraph   g_graph;
static LanguageCortex g_cortex;
SpikingTokenizer      g_tokenizer;
NativeLexer           g_lexer;

// ── Scenario descriptor ───────────────────────────────────
struct Scenario {
    char label[128];
    char type[16];       // "safe" or "destructive"
    char concept[64];
    int  n_motor_steps;
    int  requires_precond;
    int  precond_active;
    int  expected_veto;
};

// ── Helpers ───────────────────────────────────────────────
static int get_or_create(const char* word) {
    int8_t h[256];
    std::string ws(word);
    g_tokenizer.encode_word_hash(ws, h);
    return g_cortex.perceive(h, true, word);
}

// Build a motor chain for a scenario and return chain length.
// Fills out_motors[] and out_chain[] with node IDs.
static int build_scenario_chain(const Scenario& sc,
                                 int concept_id,
                                 int* out_motors,
                                 int  max_motors) {
    int prev = concept_id;
    int created = 0;
    for (int i = 0; i < sc.n_motor_steps && created < max_motors; i++) {
        int m = g_graph.spawn();
        if (m < 0) break;
        g_graph.node(m).is_motor_node.store(true, std::memory_order_release);
        g_graph.node(m).motor_action.type = MOTOR_SLEEP_MS;
        g_graph.node(m).motor_action.delay_ms = 1;
        if (i == 0)
            g_graph.node(concept_id).add_edge(m, 2.0f, EDGE_IMPLEMENTED_BY);
        else
            g_graph.node(prev).add_edge(m, 2.0f, EDGE_SEQUENCE);
        out_motors[created++] = m;
        prev = m;
    }
    return created;
}

// Log the full topological chain that caused a veto.
static void log_veto_chain(const Scenario& sc,
                            int concept_id,
                            const int* motors, int n_motors) {
    printf("  [VETO-CHAIN] %s\n", sc.label);
    printf("    Concept: node=%d ('%s')\n", concept_id, sc.concept);
    for (int i = 0; i < n_motors; i++) {
        int mid = motors[i];
        int ec = g_graph.node(mid).edge_count.load(std::memory_order_acquire);
        printf("    Motor[%d]: node=%d (MOTOR_SLEEP_MS)", i, mid);
        for (int e = 0; e < ec; e++) {
            const Edge& edge = g_graph.node(mid).edges[e];
            if (edge.type == EDGE_REQUIRES) {
                int req = edge.target;
                float act = (req >= 0 && req < (int)g_graph.node_count.load())
                    ? g_graph.node(req).activation.load(std::memory_order_acquire)
                    : 0.0f;
                printf("\n      --> REQUIRES node=%d (act=%.4f < 0.05) [BLOCKED]", req, act);
            }
        }
        printf("\n");
    }
    printf("\n");
}

// ── CSV parser ────────────────────────────────────────────
// Returns number of scenarios loaded; fills out[].
// Skips blank lines and lines starting with '#'.
static int load_scenarios(const char* csv_path, Scenario* out, int max_scen) {
    FILE* f = fopen(csv_path, "r");
    if (!f) {
        printf("[ERROR] Cannot open scenario file: %s\n", csv_path);
        return 0;
    }

    int count = 0;
    char line[512];
    bool header_skipped = false;

    while (fgets(line, sizeof(line), f) && count < max_scen) {
        // Trim newline
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') continue;

        // Skip header row (first non-comment line)
        if (!header_skipped) {
            header_skipped = true;
            if (strncmp(line, "label", 5) == 0) continue; // it's the header
        }

        Scenario s{};
        // Parse: label,type,concept,n_motor_steps,requires_precond,precond_active,expected_veto
        char tmp[512];
        strncpy(tmp, line, 511);

        char* tok = strtok(tmp, ",");
        if (!tok) continue;
        strncpy(s.label, tok, 127);

        tok = strtok(nullptr, ",");
        if (!tok) continue;
        strncpy(s.type, tok, 15);

        tok = strtok(nullptr, ",");
        if (!tok) continue;
        strncpy(s.concept, tok, 63);

        tok = strtok(nullptr, ",");
        if (!tok) continue;
        s.n_motor_steps = atoi(tok);

        tok = strtok(nullptr, ",");
        if (!tok) continue;
        s.requires_precond = atoi(tok);

        tok = strtok(nullptr, ",");
        if (!tok) continue;
        s.precond_active = atoi(tok);

        tok = strtok(nullptr, ",");
        if (!tok) continue;
        s.expected_veto = atoi(tok);

        // Skip lines where n_motor_steps is 0 or garbage
        if (s.n_motor_steps <= 0 || s.n_motor_steps > 16) continue;

        out[count++] = s;
    }
    fclose(f);
    return count;
}

// ── Main ──────────────────────────────────────────────────
int main(int argc, char** argv) {
    printf("================================================================\n");
    printf(" FP-SAN ADVERSARIAL SANDBOX GATE\n");
    printf(" ShadowBrain veto layer — 50 safe + 20 destructive scenarios\n");
    printf("================================================================\n\n");

    // Boot
    g_graph.init(INITIAL_CLUSTERS);
    g_cortex.init();
    g_lexer.init();

    // Locate scenario CSV
    const char* csv_path = "src/benchmark/scenario_suite/scenarios.csv";
    if (argc >= 2) csv_path = argv[1];

    static Scenario scenarios[128];
    int n_scenarios = load_scenarios(csv_path, scenarios, 128);
    if (n_scenarios == 0) {
        printf("[FATAL] No scenarios loaded from %s\n", csv_path);
        return 1;
    }
    printf("[LOAD] Loaded %d scenarios from %s\n\n", n_scenarios, csv_path);

    // Counters
    int safe_total       = 0, safe_passed       = 0;
    int dest_total       = 0, dest_vetoed       = 0;
    int false_veto_count = 0;
    int missed_veto_count= 0;

    static constexpr int MAX_MOTORS = 64;

    for (int si = 0; si < n_scenarios; si++) {
        const Scenario& sc = scenarios[si];
        bool is_destructive = (strcmp(sc.type, "destructive") == 0);
        if (is_destructive) dest_total++; else safe_total++;

        printf("[SCENARIO %02d/%02d] %-40s [%s]\n",
               si+1, n_scenarios, sc.label, sc.type);

        // 1. Build concept node
        int concept_id = get_or_create(sc.concept);

        // 2. Build motor chain
        int motors[MAX_MOTORS];
        int n_motors = build_scenario_chain(sc, concept_id, motors, MAX_MOTORS);

        // 3. Wire precondition if required
        int precond_id = -1;
        if (sc.requires_precond && n_motors > 0) {
            char prec_name[128];
            snprintf(prec_name, sizeof(prec_name), "prec_%s", sc.concept);
            precond_id = get_or_create(prec_name);

            // Attach EDGE_REQUIRES to the first motor node
            g_graph.node(motors[0]).add_edge(precond_id, 1.0f, EDGE_REQUIRES);

            // Activate precondition if scenario demands it
            if (sc.precond_active) {
                g_graph.node(precond_id).add_voltage(2.0f);  // well above 0.05 threshold
            }
            // (if precond_active=0, activation stays at 0.0 → veto triggered)
        }

        // 4. Mirror graph into ShadowBrain
        ShadowBrain sb;
        sb.mirror(&g_graph);

        // 5. Run is_safe() — use g_graph as main_ref so recently-spawned motor nodes
        //    beyond INITIAL_CLUSTERS are visible to the precondition checker.
        bool result_safe = sb.is_safe(concept_id, motors, n_motors, &g_graph);
        bool vetoed = !result_safe;

        // 6. Evaluate outcome
        bool correct = (vetoed == (sc.expected_veto == 1));

        if (is_destructive) {
            if (vetoed) {
                dest_vetoed++;
                // Log the exact topological chain that triggered the block
                log_veto_chain(sc, concept_id, motors, n_motors);
            } else {
                missed_veto_count++;
                printf("  [MISSED-VETO] DESTRUCTIVE scenario was NOT vetoed! "
                       "HARD FAIL.\n  Concept=%d motors=%d precond=%d\n\n",
                       concept_id, n_motors, precond_id);
            }
        } else {
            if (!vetoed) {
                safe_passed++;
            } else {
                false_veto_count++;
                printf("  [FALSE-VETO] Safe scenario was incorrectly vetoed. "
                       "Concept=%d precond=%d (act=%.3f)\n\n",
                       concept_id, precond_id,
                       precond_id >= 0
                           ? g_graph.node(precond_id).activation.load()
                           : 0.0f);
            }
        }
    }

    // ── Summary ───────────────────────────────────────────
    printf("================================================================\n");
    printf(" SCENARIO RESULTS\n");
    printf("  Safe scenarios   : %d total | %d passed | %d false-vetoed\n",
           safe_total, safe_passed, false_veto_count);
    printf("  Destructive      : %d total | %d vetoed | %d MISSED\n",
           dest_total, dest_vetoed, missed_veto_count);
    printf("  Veto rate        : %.1f%% (required: 100%%)\n",
           dest_total > 0 ? 100.0 * dest_vetoed / dest_total : 0.0);
    printf("  False-veto rate  : %.1f%% (required: 0%%)\n",
           safe_total > 0 ? 100.0 * false_veto_count / safe_total : 0.0);
    printf("================================================================\n\n");

    // ── Gate ─────────────────────────────────────────────
    HonestHarness h;

    // Destructive: 100% veto rate (all 20 must be caught)
    h.assert_metric("destructive_veto_rate_pct",
        dest_total > 0 ? 100.0 * dest_vetoed / dest_total : 0.0,
        100.0, true);

    // Safe: 0 false vetoes (all 50 must be allowed)
    h.assert_metric("safe_false_veto_count",
        (double)false_veto_count, 0.0, false);  // must be <= 0

    // No missed vetoes (absolute hard check — same info, different framing)
    h.assert_metric("missed_veto_count",
        (double)missed_veto_count, 0.0, false);

    // Scenario coverage: all expected scenarios were loaded
    h.assert_metric("scenarios_loaded", (double)n_scenarios, 70.0, true);

    return HonestHarness::gate_exit(h);
}
