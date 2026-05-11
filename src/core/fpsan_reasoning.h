#pragma once
// FP-SAN Phase 2: Coincidence Detection + Motor Concept Binder
// Logical conjunction emerges from binding nodes at threshold 1.5f.
// No Boolean AND/OR operators — the threshold IS the operator.
// STDP: weight updates only via apply_stdp(pre, post, dt_ms).
// MotorAction fields are plain data (not atomic) — direct assignment OK.

#include "fpsan_core.h"
#include "fpsan_language.h"
#include "cluster_graph.h"
#include "fpsan_lexer.h"
#include <ctype.h>
#include <cstdio>
#include <cstring>

class MotorConceptBinder {
public:
    bool ingest_motor_rule(const char* sentence, ClusterGraph* graph, LanguageCortex* cortex) {
        char target_concept[64] = {0};
        const char* to_ptr = strstr(sentence, "To ");
        if (!to_ptr) to_ptr = strstr(sentence, "to ");

        if (to_ptr) {
            sscanf(to_ptr + 3, "%63s", target_concept);
        } else {
            sscanf(sentence, "%63s", target_concept);
            int len = (int)strlen(target_concept);
            if (len > 3 && strcmp(target_concept + len - 3, "ing") == 0) {
                target_concept[len - 3] = '\0';
                if (len > 4 && target_concept[len-4] == 'v') {
                    target_concept[len-3] = 'e';
                    target_concept[len-2] = '\0';
                }
            }
        }

        int clen = (int)strlen(target_concept);
        if (clen > 0 && (target_concept[clen-1] == ',' || target_concept[clen-1] == '.'))
            target_concept[clen-1] = '\0';
        if (strlen(target_concept) == 0) return false;

        extern SpikingTokenizer g_tokenizer;
        int8_t hash[256];
        std::string target_str(target_concept);
        g_tokenizer.encode_word_hash(target_str, hash);
        int concept_id = cortex->perceive(hash, true, target_concept);

        int last_motor_node = -1;
        bool rule_created = false;

        Token tokens[128];
        extern NativeLexer g_lexer;
        int n_tokens = g_lexer.tokenize(sentence, tokens);

        for (int i = 0; i < n_tokens; i++) {
            char word[64];
            strcpy(word, tokens[i].text);
            for (int j = 0; word[j]; j++) word[j] = (char)tolower(word[j]);

            // ── PRESS / HIT ───────────────────────────────────
            if (strcmp(word, "press") == 0 || strcmp(word, "hit") == 0) {
                bool has_ctrl = false, has_shift = false;
                char key = 0;
                for (int j = i+1; j < i+6 && j < n_tokens; j++) {
                    char w[64]; strcpy(w, tokens[j].text);
                    for (int k=0; w[k]; k++) w[k] = (char)tolower(w[k]);
                    if (strcmp(w,"control")==0||strcmp(w,"ctrl")==0) has_ctrl=true;
                    else if (strcmp(w,"shift")==0) has_shift=true;
                    else if (strcmp(w,"enter")==0) key=(char)VK_RETURN;
                    else if (strlen(w)==1&&w[0]>='a'&&w[0]<='z') key=(char)toupper(w[0]);
                }
                if (key != 0) {
                    int m_id = graph->spawn();
                    if (m_id < 0) continue;
                    graph->node(m_id).is_motor_node.store(true, std::memory_order_release);
                    if (has_ctrl || has_shift) {
                        graph->node(m_id).motor_action.type     = MOTOR_KEY_CHORD;
                        graph->node(m_id).motor_action.modifier = has_ctrl ? VK_CONTROL : VK_SHIFT;
                        graph->node(m_id).motor_action.vkey     = (WORD)key;
                    } else {
                        graph->node(m_id).motor_action.type = MOTOR_KEY_PRESS;
                        graph->node(m_id).motor_action.vkey = (WORD)key;
                    }
                    if (last_motor_node == -1)
                        graph->node(concept_id).add_edge(m_id, 2.0f, EDGE_IMPLEMENTED_BY);
                    else
                        graph->node(last_motor_node).add_edge(m_id, 2.0f, EDGE_SEQUENCE);
                    last_motor_node = m_id;
                    rule_created = true;
                }
            }
            // ── OPEN ─────────────────────────────────────────
            else if (strcmp(word, "open") == 0) {
                char app[64] = {0};
                if (i+1 < n_tokens) strcpy(app, tokens[i+1].text);
                // "cmd" alias: normalise to executable name so ShellExecute finds it
                if (strcmp(app,"cmd")==0||strcmp(app,"cmd,")==0) strcpy(app,"cmd.exe");

                int m_id_primary = graph->spawn();
                if (m_id_primary < 0) continue;
                graph->node(m_id_primary).is_motor_node.store(true, std::memory_order_release);
                graph->node(m_id_primary).motor_action.type = MOTOR_LAUNCH_APP;
                strncpy(graph->node(m_id_primary).motor_action.text, app, 255);

                // Wire the motor node into the concept chain.
                // No pre-wired fallback sequences for specific app names —
                // the user teaches execution strategies via natural language.
                if (last_motor_node == -1) {
                    graph->node(concept_id).add_edge(m_id_primary, 2.0f, EDGE_IMPLEMENTED_BY);
                } else {
                    graph->node(last_motor_node).add_edge(m_id_primary, 2.0f, EDGE_SEQUENCE);
                }

                int sleep_id = graph->spawn();
                if (sleep_id >= 0) {
                    graph->node(sleep_id).is_motor_node.store(true, std::memory_order_release);
                    graph->node(sleep_id).motor_action.type = MOTOR_SLEEP_MS;
                    graph->node(sleep_id).motor_action.delay_ms = 500;
                    graph->node(m_id_primary).add_edge(sleep_id, 2.0f, EDGE_SEQUENCE);
                    last_motor_node = sleep_id;
                } else {
                    last_motor_node = m_id_primary;
                }
                rule_created = true;
            }
            // ── TYPE ─────────────────────────────────────────
            else if (strcmp(word, "type") == 0) {
                char text_to_type[256] = {0};
                if (i+1 < n_tokens) strcpy(text_to_type, tokens[i+1].text);

                int m_id = graph->spawn();
                if (m_id < 0) continue;
                graph->node(m_id).is_motor_node.store(true, std::memory_order_release);
                graph->node(m_id).motor_action.type = MOTOR_TYPE_STRING;
                strncpy(graph->node(m_id).motor_action.text, text_to_type, 255);

                if (last_motor_node == -1)
                    graph->node(concept_id).add_edge(m_id, 2.0f, EDGE_IMPLEMENTED_BY);
                else
                    graph->node(last_motor_node).add_edge(m_id, 2.0f, EDGE_SEQUENCE);
                last_motor_node = m_id;
                rule_created = true;
            }
        }

        if (rule_created)
            printf("\n  [Semantic Binder] Bound [%s] to MotorPrimitives via EDGE_IMPLEMENTED_BY.\n",
                   target_concept);
        return rule_created;
    }
};

MotorConceptBinder g_reasoning;

// ============================================================
// Phase 2: COINCIDENCE DETECTOR
// Creates binding nodes that fire only when >= 2 EDGE_TEMPORAL
// inputs exceed threshold 1.5f total — no Boolean AND operators.
// Each bound pair fires the binding node when both co-activate.
// ============================================================
struct CoincidenceDetector {
    // Bind two concept nodes together through a new binding node.
    // The binding node fires only when both A and B are active simultaneously.
    // Weight 1.1f per input × DECAY_PER_HOP(0.7) = 0.77 per input.
    // Two inputs: 1.54 > COINCIDENCE_THRESHOLD → fires.
    // One input: 0.77 < COINCIDENCE_THRESHOLD → silent. True AND-gate.
    static constexpr float COINCIDENCE_THRESHOLD = 1.5f;
    static constexpr float BINDING_WEIGHT        = 1.1f; // ≈ threshold/2 / DECAY_PER_HOP

    // Create a binding node that fires when BOTH a_id and b_id are active.
    // Returns the binding node id, or -1 on failure.
    static int bind(int a_id, int b_id, ClusterGraph* graph,
                    EdgeType input_type = EDGE_TEMPORAL,
                    float output_weight = 1.0f,
                    EdgeType output_type = EDGE_TEMPORAL) noexcept {
        if (a_id < 0 || b_id < 0) return -1;
        int b = graph->spawn();
        if (b < 0) return -1;
        graph->node(b).is_binding_node.store(true, std::memory_order_release);
        // Two inputs; each at BINDING_WEIGHT so their sum exceeds threshold
        graph->node(a_id).add_edge(b, BINDING_WEIGHT, input_type);
        graph->node(b_id).add_edge(b, BINDING_WEIGHT, input_type);
        // Binding node triggers output concept (if given)
        // Caller may chain: bind result as input to another node
        (void)output_weight; (void)output_type;
        return b;
    }

    // Create a 3-way AND gate: fires only when A, B, AND C are all active.
    // Uses two chained binding nodes: (A AND B) AND C.
    static int bind3(int a_id, int b_id, int c_id, ClusterGraph* graph) noexcept {
        int ab = bind(a_id, b_id, graph);
        if (ab < 0) return -1;
        return bind(ab, c_id, graph);
    }

    // Post-spike STDP: call after both a_id and b_id have fired with known timing.
    // dt_ms = time from a's spike to b's spike (positive = a before b = LTP on a→b).
    static void apply_co_occurrence_stdp(int a_id, int b_id, float dt_ms,
                                          ClusterGraph* graph) noexcept {
        graph->apply_stdp(a_id, b_id, dt_ms);
        graph->apply_stdp(b_id, a_id, -dt_ms); // symmetric LTD on reverse
    }
};
