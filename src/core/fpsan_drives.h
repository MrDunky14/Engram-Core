#pragma once
// ============================================================
// FP-SAN Phase 16D: HOMEOSTATIC DRIVES & SPONTANEOUS BEHAVIOR
// fpsan_drives.h — The engine that makes JARVIS "alive."
//
// A real brain doesn't wait for input. It has internal drives:
//   - CURIOSITY: Increases when unknown words are encountered.
//     When high enough, JARVIS asks about its knowledge gaps.
//   - BOREDOM: Increases with idle time. When high enough,
//     JARVIS spontaneously recalls knowledge or comments.
//   - ENGAGEMENT: Increases with interaction. Decays when idle.
//     Controls how "attentive" JARVIS is.
//
// These are just more voltage dynamics applied to internal
// state variables instead of knowledge nodes. Same physics,
// different substrate.
//
// The Living Loop:
//   Old: wait_for_input() → process() → respond() → wait
//   New: tick_drives() → check_visual() → check_curiosity()
//        → check_boredom() → maybe_speak() → check_input()
// ============================================================

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

struct HomeostaticDrives {
    // ── Drive Levels (0.0 to 1.0) ──
    float curiosity;       // Desire to learn about unknown things
    float boredom;         // Desire for stimulation / activity
    float engagement;      // How focused/attentive JARVIS is
    float frustration;     // Increases on repeated goal failures
    float doubt;           // Phase 8: rises on detected contradictions
    float satisfaction;    // Phase 8: rises on successful recall / confirmed facts

    // ── Timing State ──
    uint64_t last_input_tick;      // Last tick when user typed something
    uint64_t last_spoke_tick;      // Last tick when JARVIS spoke spontaneously
    uint64_t last_visual_tick;     // Last tick when visual change was noted
    uint64_t last_goal_tick;       // Last tick when a goal step executed
    uint64_t ticks_since_input;    // Running counter

    // ── Knowledge Gap Tracking ──
    char unknown_words[8][64];     // Words JARVIS was asked about but didn't know
    int  unknown_word_count;

    // ── Drive Parameters ──
    static constexpr float BOREDOM_RATE    = 0.00002f;  // Per tick (at 1kHz = 0.02/sec)
    static constexpr float CURIOSITY_DECAY = 0.00001f;  // Slow decay
    static constexpr float ENGAGE_DECAY    = 0.00005f;  // Medium decay
    static constexpr float SPEAK_COOLDOWN  = 15000;     // Min ticks between spontaneous speech (15 sec)
    static constexpr float BOREDOM_THRESHOLD  = 0.6f;   // Boredom to trigger spontaneous behavior
    static constexpr float CURIOSITY_THRESHOLD = 0.5f;  // Curiosity to trigger asking

    void init() {
        curiosity    = 0.0f;
        boredom      = 0.0f;
        engagement   = 0.5f;
        frustration  = 0.0f;
        doubt        = 0.0f;
        satisfaction = 0.0f;
        last_input_tick = 0;
        last_spoke_tick = 0;
        last_visual_tick = 0;
        last_goal_tick = 0;
        ticks_since_input = 0;
        unknown_word_count = 0;
        memset(unknown_words, 0, sizeof(unknown_words));
    }

    // Called every cognitive tick (~1kHz)
    void tick(uint64_t current_tick) {
        ticks_since_input = current_tick - last_input_tick;

        // Boredom increases with idle time
        boredom += BOREDOM_RATE;
        if (boredom > 1.0f) boredom = 1.0f;

        // Curiosity decays slowly (so unanswered questions persist)
        curiosity -= CURIOSITY_DECAY;
        if (curiosity < 0.0f) curiosity = 0.0f;

        // Engagement decays when idle
        engagement -= ENGAGE_DECAY;
        if (engagement < 0.0f) engagement = 0.0f;

        // Frustration decays slowly (JARVIS calms down)
        frustration -= CURIOSITY_DECAY;
        if (frustration < 0.0f) frustration = 0.0f;

        // Doubt decays (contradiction signal fades unless reinforced)
        doubt -= CURIOSITY_DECAY * 0.5f;
        if (doubt < 0.0f) doubt = 0.0f;

        // Satisfaction decays (reward signal fades)
        satisfaction -= CURIOSITY_DECAY * 2.0f;
        if (satisfaction < 0.0f) satisfaction = 0.0f;
    }

    // Called when a contradiction is detected (Phase 8)
    void on_contradiction() {
        doubt       = std::min(1.0f, doubt + 0.4f);
        curiosity   = std::min(1.0f, curiosity + 0.2f);
        satisfaction = std::max(0.0f, satisfaction - 0.3f);
    }

    // Called when a fact is confirmed / recalled correctly (Phase 8)
    void on_confirmed() {
        satisfaction = std::min(1.0f, satisfaction + 0.3f);
        doubt        = std::max(0.0f, doubt - 0.1f);
    }

    // Called when user provides input
    void on_user_input(uint64_t current_tick) {
        last_input_tick = current_tick;
        ticks_since_input = 0;
        boredom *= 0.3f;         // Interaction kills boredom
        engagement += 0.3f;      // Boost engagement
        if (engagement > 1.0f) engagement = 1.0f;
    }

    // Called when an unknown word is encountered
    void on_unknown_word(const char* word) {
        curiosity += 0.25f;
        if (curiosity > 1.0f) curiosity = 1.0f;

        // Check for duplicates before storing
        for (int i = 0; i < unknown_word_count; i++) {
            if (strcmp(unknown_words[i], word) == 0) return; // Already tracked
        }

        // Store the unknown word for later questioning
        if (unknown_word_count < 8) {
            strncpy(unknown_words[unknown_word_count], word, 63);
            unknown_words[unknown_word_count][63] = '\0';
            unknown_word_count++;
        }
    }

    // Called when JARVIS successfully learns something
    void on_learned() {
        curiosity *= 0.5f;  // Partially satisfied
        engagement += 0.1f;
        if (engagement > 1.0f) engagement = 1.0f;
    }

    // Called when visual environment changes
    void on_visual_change(uint64_t current_tick) {
        last_visual_tick = current_tick;
        boredom *= 0.7f;  // Something happened — less bored
    }

    // Called when a goal fails
    void on_goal_failed() {
        frustration += 0.3f;
        if (frustration > 1.0f) frustration = 1.0f;
    }

    // ── Spontaneous Behavior Checks ──

    // Should JARVIS ask about something it doesn't know?
    bool should_ask_question(uint64_t current_tick) {
        if (curiosity < CURIOSITY_THRESHOLD) return false;
        if (unknown_word_count == 0) return false;
        if ((current_tick - last_spoke_tick) < SPEAK_COOLDOWN) return false;
        return true;
    }

    // Should JARVIS spontaneously recall/comment?
    bool should_spontaneous_speak(uint64_t current_tick) {
        if (boredom < BOREDOM_THRESHOLD) return false;
        if ((current_tick - last_spoke_tick) < SPEAK_COOLDOWN) return false;
        return true;
    }

    // Get a question to ask (pops from unknown words)
    const char* pop_question() {
        if (unknown_word_count == 0) return nullptr;
        unknown_word_count--;
        return unknown_words[unknown_word_count];
    }

    // Mark that JARVIS just spoke spontaneously
    void on_spoke(uint64_t current_tick) {
        last_spoke_tick = current_tick;
        boredom *= 0.5f;  // Speaking reduces boredom
    }

    // ── Phase 2: Active Epistemology ─────────────────────────
    // Trigger condition: high curiosity AND low confidence (< 0.05).
    // When true, the caller should asynchronously fetch + distill + ingest
    // the unknown word via the research pipeline.
    static constexpr float CONFIDENCE_CURIOSITY_THRESHOLD = 0.05f;

    bool should_fetch_knowledge(float current_confidence) const noexcept {
        return (curiosity >= CURIOSITY_THRESHOLD) &&
               (current_confidence < CONFIDENCE_CURIOSITY_THRESHOLD) &&
               (unknown_word_count > 0);
    }

    // Returns the most curiosity-urgent topic and pops it.
    // Call this after should_fetch_knowledge() returns true.
    const char* pop_fetch_topic() noexcept {
        if (unknown_word_count == 0) return nullptr;
        // Always pop index 0 (FIFO — oldest curiosity item first)
        static char topic_buf[64];
        strncpy(topic_buf, unknown_words[0], 63);
        topic_buf[63] = '\0';
        // Shift remaining
        for (int i = 0; i + 1 < unknown_word_count; i++)
            memcpy(unknown_words[i], unknown_words[i+1], 64);
        unknown_word_count--;
        return topic_buf;
    }

    // Called when the research pipeline successfully ingested new knowledge.
    void on_knowledge_ingested(int facts_learned) noexcept {
        curiosity    -= 0.3f * (float)facts_learned;
        if (curiosity < 0.0f) curiosity = 0.0f;
        engagement   += 0.15f * (float)facts_learned;
        if (engagement > 1.0f) engagement = 1.0f;
    }

    // Print drive state
    void print_status() {
        printf("  [Drives]\n");
        printf("    Curiosity:    %.2f%s\n", curiosity,
               curiosity >= CURIOSITY_THRESHOLD ? " (wants to ask)" : "");
        printf("    Boredom:      %.2f%s\n", boredom,
               boredom >= BOREDOM_THRESHOLD ? " (restless)" : "");
        printf("    Engagement:   %.2f\n", engagement);
        printf("    Frustration:  %.2f%s\n", frustration,
               frustration > 0.5f ? " (struggling)" : "");
        printf("    Doubt:        %.2f%s\n", doubt,
               doubt > 0.5f ? " (contradictions detected — trusting verified sources)" : "");
        printf("    Satisfaction: %.2f%s\n", satisfaction,
               satisfaction > 0.5f ? " (confirmed)" : "");
        printf("    Idle:         %.1f sec\n", ticks_since_input / 1000.0f);
        if (unknown_word_count > 0) {
            printf("    Curious about: ");
            for (int i = 0; i < unknown_word_count; i++) {
                printf("\"%s\" ", unknown_words[i]);
            }
            printf("\n");
        }
    }
};

// ============================================================
// GOAL PLANNER — Proactive multi-step task execution
// ============================================================
struct GoalPlanner {
    struct Goal {
        char description[256];   // Original goal text
        char steps[8][128];      // Decomposed action steps
        int  step_count;         // Total steps
        int  current_step;       // Current step index
        bool active;             // Is a goal being pursued?
    };

    Goal current;

    void init() {
        current.active = false;
        current.step_count = 0;
        current.current_step = 0;
        current.description[0] = '\0';
    }

    // Set a new explicit goal
    // Decomposes simple goal descriptions into motor sequences or semantic primitives
    bool set_goal(const char* goal_text, ClusterGraph* graph = nullptr, LanguageCortex* cortex = nullptr) {
        strncpy(current.description, goal_text, 255);
        current.description[255] = '\0';
        current.step_count = 0;
        current.current_step = 0;

        // Phase 18B: Semantic Motor Binding (Zero-Shot)
        if (graph && cortex) {
            char target_concept[64] = {0};
            // Simplistic extraction: find first verb in the goal text
            extern NativeLexer g_lexer;
            extern int find_word(const char* word);
            
            Token tokens[64];
            int n_tokens = g_lexer.tokenize(goal_text, tokens);
            int target_id = -1;
            for (int i = 0; i < n_tokens; i++) {
                int cid = find_word(tokens[i].text);
                if (cid >= 0 && g_lexer.lexicon.lookup(tokens[i].text) == POS_VERB) {
                    target_id = cid;
                    strcpy(target_concept, tokens[i].text);
                    break;
                }
            }

            if (target_id >= 0) {
                // Check if the graph has an EDGE_IMPLEMENTED_BY for this concept
                bool has_motor = false;
                for (int i = 0; i < graph->node(target_id).edge_count; i++) {
                    if (graph->node(target_id).edges[i].type == EDGE_IMPLEMENTED_BY) {
                        has_motor = true;
                        break;
                    }
                }

                if (has_motor) {
                    char step[128];
                    snprintf(step, sizeof(step), "motor_concept_%d", target_id);
                    add_step(step);
                    current.active = true;
                    return true;
                } else {
                    printf("\n  [GoalPlanner] Confidence 0.0: Concept '%s' is not bound to any motor primitive.\n", target_concept);
                    printf("  [GoalPlanner] Teach me: \"To %s, press ...\" or \"To %s, open ...\"\n", target_concept, target_concept);
                    return false; // Force Zero-Shot failure
                }
            }

            // No verb found with semantic binding. Try to find ANY noun
            // that has motor bindings (e.g., "notepad" might have EDGE_IMPLEMENTED_BY
            // from a prior "To open, open notepad" teaching).
            for (int i = 0; i < n_tokens; i++) {
                int cid = find_word(tokens[i].text);
                if (cid >= 0) {
                    bool has_motor = false;
                    for (int j = 0; j < graph->node(cid).edge_count; j++) {
                        if (graph->node(cid).edges[j].type == EDGE_IMPLEMENTED_BY) {
                            has_motor = true;
                            break;
                        }
                    }
                    if (has_motor) {
                        char step[128];
                        snprintf(step, sizeof(step), "motor_concept_%d", cid);
                        add_step(step);
                        current.active = true;
                        return true;
                    }
                }
            }
        }

        // No semantic bindings found for any word in the goal.
        // JARVIS doesn't know how to do this — ask to be taught.
        printf("\n  [GoalPlanner] No semantic motor bindings found for this goal.\n");
        printf("  [GoalPlanner] Teach me actions using natural language:\n");
        printf("  [GoalPlanner]   \"To save, press Control and S\"\n");
        printf("  [GoalPlanner]   \"To compile, open cmd\"\n");
        return false; // Unknown goal — zero confidence
    }

    void add_step(const char* step) {
        if (current.step_count < 8) {
            strncpy(current.steps[current.step_count], step, 127);
            current.steps[current.step_count][127] = '\0';
            current.step_count++;
        }
    }

    // Get the next action to execute (or nullptr if done)
    const char* next_action() {
        if (!current.active) return nullptr;
        if (current.current_step >= current.step_count) {
            current.active = false;
            return nullptr;
        }
        return current.steps[current.current_step];
    }

    // Mark current step as complete
    void complete_step() {
        current.current_step++;
        if (current.current_step >= current.step_count) {
            current.active = false;
        }
    }

    bool is_active() { return current.active; }

    void cancel() {
        current.active = false;
        printf("  [GoalPlanner] Goal cancelled.\n");
    }

    void print_status() {
        if (!current.active) {
            printf("  [GoalPlanner] No active goal.\n");
            return;
        }
        printf("  [GoalPlanner] Goal: \"%s\"\n", current.description);
        for (int i = 0; i < current.step_count; i++) {
            printf("    %s [%d] %s\n",
                   i < current.current_step ? "[x]" :
                   i == current.current_step ? "[>]" : "[ ]",
                   i, current.steps[i]);
        }
    }
};
