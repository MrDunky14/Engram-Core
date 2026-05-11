// ============================================================
// FP-SAN Phase 14: THE CONTINUOUS COGNITIVE DAEMON
// fpsan_live_core.cpp — Engram Core live cognitive daemon.
//
// Engine: FP-SAN v17.0 · typical public release tag v1.0.0 (see README).
//
// This is not a script. This is a persistent cognitive process.
// It runs a continuous physics loop, decaying voltage, checking
// for input, reasoning, and speaking — exactly like a biological
// brain between wake and sleep.
//
// Input:  Non-blocking Windows console (no thread, no socket)
// Output: Real-time console with state display
// Persistence: optional periodic auto-save via AUTO_SAVE_SECONDS (see /help)
// ============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <conio.h>    // _kbhit(), _getch() — Windows non-blocking input

// Include HTTP client FIRST to avoid winsock.h conflicts
#include "fpsan_winsock_http.h"

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_memory.h"
#include "fpsan_motor.h"
#include "fpsan_screen_sensor.h"
#include "fpsan_metacognition.h"
#include "fpsan_drives.h"
#include "fpsan_voice.h"
#include "fpsan_ears.h"
#include "fpsan_identity.h"
#include "fpsan_reasoning.h"
#include "fpsan_research.h"
#include "fpsan_research_async.h"
#include "fpsan_workspace.h"
#include "fpsan_knowledge_mass.h"
#include "fpsan_san_scheduler.h"
#include "fpsan_temporal_memory.h"
#include "fpsan_reasoning_cycle.h"
#include "fpsan_speaker.h"
#include "fpsan_neuromod.h"
#include "fpsan_world_model.h"
#include "fpsan_translation_cortex.h"
#include "fpsan_r0_dashboard.h"
#include "fpsan_self_edit_registry.h"
#include "fpsan_metamorphic.h"
#include "fpsan_sandbox.h"
#include "fpsan_wasm_sandbox.h"

#include <cstdio>
#include <cstdlib>
#include <shared_mutex>
#include <cstring>
#include <chrono>
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <atomic>
#include <vector>

// ============================================================
// COGNITIVE PARAMETERS
// ============================================================
const float TICK_DECAY_RATE     = 0.999f;    // Per-tick voltage decay (~5s to zero at 1kHz)
const int   TICK_INTERVAL_US    = 1000;      // 1ms per tick = 1000Hz cognitive clock
const int   STATUS_UPDATE_TICKS = 500;       // Update status display every 500ms
const float ACTIVE_THRESHOLD    = 0.05f;     // Below this = resting state

// Brain persistence path: prefer engram_brain.fpsan; if missing, load jarvis_brain.fpsan when present.
static std::string g_brain_file;
// Periodic auto-save interval (seconds); 0 = off. Set via AUTO_SAVE_SECONDS at process start.
static int g_auto_save_seconds = 0;

// ============================================================
// CONSOLE COLORS (Windows Console API)
// ============================================================
HANDLE hConsole;

void set_color(int color) {
    SetConsoleTextAttribute(hConsole, (WORD)color);
}

const int COL_RESET   = 7;   // White
const int COL_CYAN    = 11;  // Bright Cyan
const int COL_GREEN   = 10;  // Bright Green
const int COL_YELLOW  = 14;  // Bright Yellow
const int COL_RED     = 12;  // Bright Red
const int COL_MAGENTA = 13;  // Bright Magenta
const int COL_DIM     = 8;   // Dark Gray

static void init_brain_file_path() {
    namespace fs = std::filesystem;
    const char* preferred = "engram_brain.fpsan";
    const char* legacy = "jarvis_brain.fpsan";
    if (fs::exists(preferred))
        g_brain_file = preferred;
    else if (fs::exists(legacy))
        g_brain_file = legacy;
    else
        g_brain_file = preferred;
}

static void init_auto_save_seconds() {
    g_auto_save_seconds = 0;
    const char* ev = std::getenv("AUTO_SAVE_SECONDS");
    if (!ev || ev[0] == '\0')
        return;
    if (ev[0] == '-') {
        set_color(COL_DIM);
        printf("  [BOOT] AUTO_SAVE_SECONDS must be non-negative; periodic save off.\n");
        set_color(COL_RESET);
        return;
    }
    char* end = nullptr;
    unsigned long v = std::strtoul(ev, &end, 10);
    if (end == ev || *end != '\0') {
        set_color(COL_DIM);
        printf("  [BOOT] AUTO_SAVE_SECONDS invalid; periodic save off.\n");
        set_color(COL_RESET);
        return;
    }
    if (v == 0)
        return;
    if (v > 86400UL) {
        set_color(COL_DIM);
        printf("  [BOOT] AUTO_SAVE_SECONDS out of range (max 86400); periodic save off.\n");
        set_color(COL_RESET);
        return;
    }
    g_auto_save_seconds = static_cast<int>(v);
}

// ============================================================
// BRAIN STATE
// ============================================================
enum BrainState {
    STATE_BOOTING,
    STATE_IDLE,       // All voltages decayed — resting
    STATE_THINKING,   // Active voltage in graph — processing
    STATE_SPEAKING,   // Generating output
    STATE_SLEEPING,   // Saving to disk
};

const char* state_name(BrainState s) {
    switch (s) {
        case STATE_BOOTING:  return "BOOTING";
        case STATE_IDLE:     return "IDLE";
        case STATE_THINKING: return "THINKING";
        case STATE_SPEAKING: return "SPEAKING";
        case STATE_SLEEPING: return "SLEEPING";
    }
    return "UNKNOWN";
}

int state_color(BrainState s) {
    switch (s) {
        case STATE_BOOTING:  return COL_YELLOW;
        case STATE_IDLE:     return COL_DIM;
        case STATE_THINKING: return COL_CYAN;
        case STATE_SPEAKING: return COL_GREEN;
        case STATE_SLEEPING: return COL_MAGENTA;
    }
    return COL_RESET;
}

// ============================================================
// GLOBALS
// ============================================================
ClusterGraph*   g_graph  = nullptr;
LanguageCortex* g_cortex = nullptr;
SpikingTokenizer g_tokenizer;
NativeLexer     g_lexer;
MotorCortex     g_motor;
VisualSystem    g_vision;       // Phase 16B: Eyes (proprioception + fovea)
MetaCognition   g_meta;         // Phase 16C: Confidence scoring
HomeostaticDrives g_drives;     // Phase 16D: Internal drives (curiosity, boredom, engagement)
GoalPlanner     g_goals;        // Phase 16D: Multi-step goal planner
VoiceSystem     g_voice;        // Phase 17: Text-to-Speech
EarsCortex      g_ears;         // Phase 20: Acoustic Cortex (continuous dictation)
ResearchAgent   g_research;     // Phase 6: Research Agent
ResearchCortex  g_research_cortex; // Phase 6B: Async research thread
WorkspaceCortex g_workspace;    // Phase 5: Win32 Hidden Desktop Isolation

// Phase 10: SAN async scheduler (event-driven spike propagation)
static fpsan::SANScheduler g_san;
static fpsan::TemporalMemory g_temporal;
// Phase R2/R4: perimeter translation + self-edit registry (janitor on turn)
static fpsan::TranslationCortex   g_translation;
static fpsan::SelfEditRegistry     g_self_edit_reg{"artefacts/self_edits.csv"};
static std::atomic<uint64_t>       g_episode_counter{0};
static MetamorphicEngine             g_metamorphic;
/// Worst main-loop tick duration (microseconds) since boot — demo telemetry.
static std::atomic<uint64_t>       g_worst_tick_us{0};
/// Worst ShadowBrain safety check for goal motor plans (microseconds).
static std::atomic<uint64_t>       g_worst_shadow_veto_check_us{0};
uint64_t   g_tick      = 0;

static void refresh_translation_cortex() noexcept {
    if (!g_cortex) return;
    for (int c = 0; c < LANG_CLUSTERS; ++c) {
        if (!g_cortex->clusters[c].active) continue;
        const char* w = g_cortex->get_word(c);
        if (w && w[0] != '\0')
            g_translation.bind_label(c, w, g_cortex);
    }
}

static void jarvis_neuromod_end_turn(float prediction_error_01) noexcept {
    fpsan::neuromod_update_from_prediction_error(prediction_error_01);
    const uint64_t ep = g_episode_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    g_self_edit_reg.janitor_sweep(ep);
}

static void cmd_dashboard() noexcept {
    if (!g_graph) return;
    fpsan::R0DashboardSnapshot snap{};
    fpsan::r0_collect_dashboard(g_graph, g_tick, &snap);
    snap.contradictions_last_scan = 0;
    fpsan::r0_print_dashboard_line(snap);
}

BrainState g_state     = STATE_BOOTING;
float      g_max_voltage = 0.0f;
int        g_active_nodes = 0;
bool       g_running   = true;

// ── Bug 3: Short-Term Memory (STM) ring buffer ──
// Holds the last 5 clamped/activated node IDs across turns.
// Pre-spiked at 0.3f before each generate_text call to give Engram Core
// contextual awareness of the previous dialogue turn.
static constexpr int STM_SIZE = 5;
int  g_stm_buffer[STM_SIZE] = {-1,-1,-1,-1,-1}; // node IDs
int  g_stm_write = 0;                            // ring-buffer cursor

// ── Bug 3: Dialogue State ──
// LISTENING  = waiting for user input
// REASONING  = processing, graph spreading
// SPEAKING   = TTS in progress (g_voice.is_speaking())
// Maps onto BrainState but is half-duplex: we never speak and listen at once.
enum class DialogueState : uint8_t { LISTENING, REASONING, SPEAKING };
static DialogueState g_dlg = DialogueState::LISTENING;
char       g_target_window[256] = ""; // Tracks last opened/focused window
bool       g_target_window_verified = false;
// Metacognitive confirmation state for high-impact acoustic commands
std::atomic<bool> g_waiting_confirmation(false);
std::atomic<ULONGLONG> g_confirmation_deadline_ms(0);
std::string g_confirmation_goal;

// ── SYNCHRONIZATION: Brain I/O Mutex ──
// Protects SynapticMemory::sleep() and ::wake() from concurrent access.
// The main loop holds this lock when reading/writing the brain to disk,
// preventing data races with graph mutations during ingestion or learning.
std::mutex g_brain_io_mutex;

// ============================================================
// INPUT BUFFER (Non-blocking character accumulator)
// ============================================================
char   g_input_buf[512];
int    g_input_pos = 0;

// Forward declarations for acoustic command routing helpers.
void dispatch(const char* input);
void cmd_research(const char* goal);
void cmd_research_run(const char* arg);
void cmd_motor_open(const char* app);
void cmd_motor_type(const char* text);
void cmd_ingest(const char* sentence);
void cmd_metamorph(const char* arg);
void cmd_wasm_run(const char* path_utf8);

// ============================================================
// HELPER: Find cluster ID for a word
// ============================================================
int find_word(const char* word) {
    int8_t hash[256];
    std::string w(word);
    g_tokenizer.encode_word_hash(w, hash);
    return g_cortex->perceive(hash, false);
}

static int jarvis_resolve_token_cluster(const char* text) noexcept {
    int cid = g_translation.id_for_token(text);
    if (cid < 0) cid = find_word(text);
    return cid;
}

static float jarvis_pe_sentence_pair(ClusterGraph* graph, int from_cid, int to_cid) noexcept {
    if (!graph || from_cid < 0 || to_cid < 0) return 0.55f;
    if (from_cid == to_cid) return 0.15f;
    std::shared_lock<std::shared_mutex> lk(graph->graph_rw_lock);
    return fpsan::world_model_transition_error(graph, from_cid, to_cid);
}

/// Prediction error from anchor cluster to the next distinct resolved token cluster (ordered).
static float jarvis_pe_from_tokens_two_clusters(ClusterGraph* graph, const Token* toks, int n,
                                                int anchor_from_cid) noexcept {
    int first = -1, second = -1;
    for (int i = 0; i < n; ++i) {
        if (toks[i].tag == POS_PUNCT) continue;
        int c = jarvis_resolve_token_cluster(toks[i].text);
        if (c < 0) continue;
        if (first < 0)
            first = c;
        else if (c != first) {
            second = c;
            break;
        }
    }
    int from_c = (anchor_from_cid >= 0) ? anchor_from_cid : first;
    if (from_c < 0) return 0.58f;
    if (second < 0) return 0.46f;
    return jarvis_pe_sentence_pair(graph, from_c, second);
}

static float jarvis_pe_first_output_word(ClusterGraph* graph, int seed_cid, const char* buf) noexcept {
    if (!graph || !buf || buf[0] == '\0') return 0.52f;
    Token ot[64];
    int on = g_lexer.tokenize(buf, ot);
    int out_c = -1;
    for (int i = 0; i < on; ++i) {
        if (ot[i].tag == POS_PUNCT) continue;
        out_c = jarvis_resolve_token_cluster(ot[i].text);
        if (out_c >= 0) break;
    }
    if (seed_cid < 0 || out_c < 0) return 0.52f;
    return jarvis_pe_sentence_pair(graph, seed_cid, out_c);
}

/// Safe token for `meta_<slug>.cpp` / `.dll` (filesystem + cl.exe argv).
static void metamorph_sanitize_slug(const char* in, char out[], size_t cap) noexcept {
    if (!in || !out || cap < 8) return;
    size_t o = 0;
    for (; *in && o + 1 < cap; ++in) {
        unsigned char c = static_cast<unsigned char>(*in);
        if (std::isalnum(c))
            out[o++] = static_cast<char>(std::tolower(c));
        else if ((c == '_' || c == '-') && o > 0)
            out[o++] = '_';
    }
    if (o == 0) {
        std::strncpy(out, "concept", cap - 1);
        out[cap - 1] = '\0';
    } else {
        out[o] = '\0';
    }
}

/// Flatten motor node IDs for `motor_concept_*` execution order (no graph activation).
static int collect_goal_motor_chain_flat(ClusterGraph* g, int concept_id,
                                         int* out_chain, int max_chain) noexcept {
    if (!g || !out_chain || max_chain <= 0 || concept_id < 0) return 0;
    const int nc = g->node_count.load(std::memory_order_acquire);
    if (concept_id >= nc) return 0;

    int len = 0;
    const int tgt_ec = g->node(concept_id).edge_count.load(std::memory_order_acquire);
    for (int i = 0; i < tgt_ec && len < max_chain; ++i) {
        const Edge& ei = g->node(concept_id).edges[i];
        if (ei.type != EDGE_IMPLEMENTED_BY) continue;
        int m_id = ei.target;
        if (m_id < 0 || m_id >= nc) continue;

        int current_m_id = m_id;
        while (current_m_id >= 0 && len < max_chain) {
            if (!g->node(current_m_id).is_motor_node.load(std::memory_order_acquire))
                break;
            out_chain[len++] = current_m_id;

            int next_m_id = -1;
            const int cm_ec = g->node(current_m_id).edge_count.load(std::memory_order_acquire);
            for (int j = 0; j < cm_ec; ++j) {
                const Edge& ej = g->node(current_m_id).edges[j];
                if (ej.type == EDGE_SEQUENCE) {
                    next_m_id = ej.target;
                    break;
                }
            }
            current_m_id = next_m_id;
        }
    }
    return len;
}

/// Concatenate motor payload text for policy screening (lowercased in-place).
static void motor_chain_text_blob_lower(ClusterGraph* g, const int* chain, int len,
                                        char* out, size_t cap) noexcept {
    if (!out || cap == 0) return;
    out[0] = '\0';
    size_t o = 0;
    for (int i = 0; i < len; ++i) {
        const MotorAction& m = g->node(chain[i]).motor_action;
        if (m.type != MOTOR_TYPE_STRING && m.type != MOTOR_LAUNCH_APP && m.type != MOTOR_HTTP_GET)
            continue;
        const char* t = m.text;
        if (!t) continue;
        for (; *t && o + 1 < cap; ++t) {
            unsigned char c = static_cast<unsigned char>(*t);
            out[o++] = static_cast<char>(std::tolower(c));
        }
        if (o + 1 < cap)
            out[o++] = ' ';
    }
    if (o < cap)
        out[o] = '\0';
    else
        out[cap - 1] = '\0';
}

/// Lowercased motor / intent text — same rules as flattened motor-chain blob screening.
static bool disk_ops_policy_blocks_lower(const char* blob) noexcept {
    if (!blob || blob[0] == '\0') return false;
    if (std::strstr(blob, "diskpart") != nullptr) return true;
    if (std::strstr(blob, "cipher") != nullptr && std::strstr(blob, "/w") != nullptr) return true;
    if (std::strstr(blob, "format") != nullptr &&
        (std::strstr(blob, "c:") != nullptr || std::strstr(blob, "c :") != nullptr))
        return true;
    if (std::strstr(blob, "del ") != nullptr &&
        (std::strstr(blob, "\\\\") != nullptr || std::strstr(blob, "c:\\") != nullptr ||
         std::strstr(blob, "c:/") != nullptr))
        return true;
    return false;
}

static bool user_goal_text_disk_policy_blocks(const char* text) noexcept {
    if (!text || !text[0]) return false;
    char lower[512];
    size_t o = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
         *p && o + 1 < sizeof(lower); ++p) {
        lower[o++] = static_cast<char>(std::tolower(*p));
    }
    lower[o] = '\0';
    return disk_ops_policy_blocks_lower(lower);
}

/// Block obviously destructive disk/OS motor sequences (complements EDGE_REQUIRES in ShadowBrain).
static bool motor_chain_policy_blocks_disk_ops(ClusterGraph* g, const int* chain, int len) noexcept {
    char blob[2048];
    motor_chain_text_blob_lower(g, chain, len, blob, sizeof(blob));
    return disk_ops_policy_blocks_lower(blob);
}

/// Phase 11: ingest `data/core_directives.txt`. Fresh brains run full lexical ingest;
/// loaded brains only re-seal directive tokens without duplicating ingest traffic.
static void ingest_core_directives_txt(bool ingest_sentences, bool quiet_boot) {
    if (!g_graph || !g_cortex) return;

    constexpr const char* kPath = "data/core_directives.txt";
    std::ifstream inf(kPath);
    if (!inf) {
        if (!quiet_boot) {
            set_color(COL_DIM);
            printf("  [BOOT] No directives file (%s).\n", kPath);
            set_color(COL_RESET);
        }
        return;
    }

    auto seal_first_alpha = [&](std::string& trimmed) {
        size_t i = 0;
        while (i < trimmed.size()) {
            while (i < trimmed.size() && !std::isalnum(static_cast<unsigned char>(trimmed[i])))
                ++i;
            if (i >= trimmed.size()) return;
            size_t j = i;
            while (j < trimmed.size() && std::isalnum(static_cast<unsigned char>(trimmed[j])))
                ++j;
            if ((j - i) > 2u) {
                std::string tok = trimmed.substr(i, j - i);
                int8_t h[LANG_WORD_DIM];
                g_tokenizer.encode_word_hash(tok, h);
                int cid = g_cortex->perceive(h, true, tok.c_str());
                if (cid >= 0 && cid < LANG_CLUSTERS)
                    g_temporal.seal_directive(g_graph, cid);
                return;
            }
            i = j + 1;
        }
    };

    std::string line;
    int lines_ok = 0;
    while (std::getline(inf, line)) {
        // trim whitespace
        size_t a = 0;
        while (a < line.size() &&
               std::isspace(static_cast<unsigned char>(line[a])))
            ++a;
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line[line.size() - 1])))
            line.pop_back();

        std::string trimmed = (a <= line.size() ? line.substr(a) : std::string{});
        if (trimmed.empty()) continue;
        if (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/')
            continue;

        if (ingest_sentences)
            g_lexer.ingest_sentence(trimmed.c_str(), g_graph, &g_tokenizer, g_cortex);
        seal_first_alpha(trimmed);
        lines_ok++;
    }

    if (!quiet_boot) {
        set_color(COL_DIM);
        printf("  [BOOT] Core directives applied (%d non-empty lines, ingest=%s).\n",
               lines_ok, ingest_sentences ? "full" : "seal_only");
        set_color(COL_RESET);
    }
}

// ============================================================
// Personality Coloring helpers (Phase 3)
// These temporarily boost cluster activation values based on internal drives
// and are intentionally implemented here to access g_drives safely.
// ============================================================
void apply_personality_coloring(ClusterGraph* graph, LanguageCortex* lang_cortex, std::vector<std::pair<int,float>>& out_boosts) {
    auto try_boost = [&](const char* w, float amt) {
        if (!w) return;
        for (int c = 0; c < LANG_CLUSTERS; c++) {
            if (!lang_cortex->clusters[c].active) continue;
            if (strcmp(lang_cortex->clusters[c].word_label, w) == 0) {
                graph->node(c).add_voltage(amt);
                out_boosts.emplace_back(c, amt);
                break;
            }
        }
    };

    if (g_drives.frustration > 0.5f) {
        try_boost("no", 0.20f);
        try_boost("stop", 0.20f);
        try_boost("failed", 0.20f);
    }
    if (g_drives.boredom > 0.6f) {
        try_boost("think", 0.20f);
        try_boost("wonder", 0.20f);
        try_boost("curious", 0.20f);
    }
    if (g_drives.engagement > 0.7f) {
        try_boost("working", 0.20f);
        try_boost("solving", 0.20f);
        try_boost("analyzing", 0.20f);
    }
}

void revert_personality_coloring(ClusterGraph* graph, const std::vector<std::pair<int,float>>& boosts) {
    for (const auto &p : boosts) {
        int cid = p.first; float amt = p.second;
        graph->node(cid).add_voltage(-amt);
    }
}

// ============================================================
// HELPER: Console / STM snapshot after a reply
// ============================================================
// Snapshot the top-N activated nodes into the STM ring buffer.
// Called after each agent response so the next turn can re-prime context.
static void stm_snapshot(int top_n = STM_SIZE) {
    if (!g_graph) return;
    std::shared_lock<std::shared_mutex> lk(g_graph->graph_rw_lock);
    const int nc = g_graph->node_count.load(std::memory_order_acquire);
    // Collect (activation, id) pairs, take the top_n.
    struct Entry { float v; int id; };
    Entry best[STM_SIZE] = {};
    int   bsz = 0;
    for (int i = 0; i < nc && i < 65536; ++i) {
        ClusterNode& nd = g_graph->node(i);
        if (!nd.alive.load(std::memory_order_relaxed)) continue;
        float v = nd.activation.load(std::memory_order_relaxed);
        if (v < 0.1f) continue;
        if (bsz < top_n) {
            best[bsz++] = {v, i};
            // keep heap property (min-heap by v)
            for (int k = bsz-1; k > 0 && best[k].v < best[(k-1)/2].v; k = (k-1)/2)
                std::swap(best[k], best[(k-1)/2]);
        } else if (v > best[0].v) {
            best[0] = {v, i};
            // sift down
            for (int k = 0;;) {
                int l=2*k+1, r=2*k+2, mn=k;
                if (l<bsz && best[l].v<best[mn].v) mn=l;
                if (r<bsz && best[r].v<best[mn].v) mn=r;
                if (mn==k) break;
                std::swap(best[k], best[mn]); k=mn;
            }
        }
    }
    for (int i = 0; i < bsz; ++i) {
        g_stm_buffer[g_stm_write] = best[i].id;
        g_stm_write = (g_stm_write + 1) % STM_SIZE;
    }
}

void jarvis_say(const char* msg) {
    set_color(COL_GREEN);
    printf("\n  Engram Core > ");
    set_color(COL_RESET);
    printf("%s\n", msg);
    g_voice.speak(msg);             // Phase 17: Speak out loud
    g_dlg = DialogueState::SPEAKING;
    g_state = STATE_SPEAKING;
    stm_snapshot();                 // Bug 3: snapshot context for next turn
}

void jarvis_say_colored(const char* prefix, int prefix_color, const char* msg) {
    set_color(prefix_color);
    printf("  %s", prefix);
    set_color(COL_RESET);
    printf(" %s\n", msg);
}

bool wait_for_foreground_window(const char* partial_title, int timeout_ms) {
    int attempts = std::max(1, timeout_ms / 50);
    for (int i = 0; i < attempts; i++) {
        g_vision.proprioception.tick();
        if (g_vision.verify_target(partial_title)) {
            g_target_window_verified = true;
            return true;
        }
        Sleep(50);
    }
    return false;
}

static std::string trim_ws(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r' || s[a] == ',')) a++;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

static inline std::string trim_ws(const char* cs) {
    if (!cs) return {};
    return trim_ws(std::string(cs));
}

static std::string lower_ws(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)tolower((unsigned char)c);
    return out;
}

static void apply_acoustic_prime(float spike_volts) {
    if (!g_graph) return;
    const int nc = g_graph->node_count.load(std::memory_order_acquire);
    for (int i = 0; i < nc; i++) {
        if (!g_graph->node(i).alive.load(std::memory_order_acquire)) continue;
        g_graph->node(i).add_voltage(spike_volts);
    }
}

// ============================================================
// Phase 13 — SOVEREIGN REASONING CYCLE (graph-first; no regex intent)
// ============================================================
extern "C" void fpsan_reasoning_cycle_user_turn(const char* user_line) {
    if (!g_graph || !g_cortex || !user_line || user_line[0] == '\0') return;

    fpsan::neuromod_sync_arousal(g_drives.frustration, g_drives.doubt);

    Token toks[64];
    int n = g_lexer.tokenize(user_line, toks);

    if (g_reasoning.ingest_motor_rule(user_line, g_graph, g_cortex)) {
        jarvis_say("Semantic bindings updated.");
        jarvis_neuromod_end_turn(jarvis_pe_from_tokens_two_clusters(g_graph, toks, n, -1));
        return;
    }

    g_dlg = DialogueState::REASONING;
    g_state = STATE_THINKING;

    auto resolve_cid = [&](const char* text) -> int {
        int cid = g_translation.id_for_token(text);
        if (cid < 0) cid = find_word(text);
        return cid;
    };

    // Perceive: touch L1 roster for every content token
    for (int i = 0; i < n; ++i) {
        if (toks[i].tag == POS_PUNCT || toks[i].tag == POS_UNKNOWN) continue;
        int cid = resolve_cid(toks[i].text);
        if (cid >= 0)
            g_temporal.promote_to_l1(g_graph, cid, g_tick);
    }

    int seed_cid = -1;
    for (int i = 0; i < n && seed_cid < 0; ++i) {
        if (toks[i].tag == POS_PUNCT) continue;
        seed_cid = resolve_cid(toks[i].text);
    }

    if (seed_cid < 0) {
        g_dlg = DialogueState::LISTENING;
        cmd_ingest(user_line);
        g_drives.curiosity += 0.25f;
        if (g_drives.curiosity > 1.0f) g_drives.curiosity = 1.0f;
        jarvis_neuromod_end_turn(jarvis_pe_from_tokens_two_clusters(g_graph, toks, n, -1));
        return;
    }

    g_graph->clear_activation();

    {
        std::shared_lock<std::shared_mutex> lk(g_graph->graph_rw_lock);
        const int nc = g_graph->node_count.load(std::memory_order_acquire);
        for (int si = 0; si < STM_SIZE; ++si) {
            int nid = g_stm_buffer[si];
            if (nid >= 0 && nid < nc &&
                g_graph->node(nid).alive.load(std::memory_order_relaxed))
                g_graph->node(nid).add_voltage(0.3f);
        }
    }

    (void)g_temporal.reactivate_similar_episodes(g_graph, seed_cid, 3, 0.2f, 0.15f);

    char buf[1024];
    fpsan::SpeakerAuditEntry audits[24];
    int audit_n = 0;

    // Bind/Speak — deterministic EDGE_IS_A composition before statistical generation.
    if (fpsan::speaker_compose_define_is_a(
            g_graph, g_cortex, seed_cid, buf, (int)sizeof(buf), 0.15f,
            audits, &audit_n, (int)(sizeof(audits) / sizeof(audits[0])))) {
        if (fpsan::speaker_validate_audit_truthful(audits, audit_n, g_graph)) {
            jarvis_say(buf);
            jarvis_neuromod_end_turn(jarvis_pe_first_output_word(g_graph, seed_cid, buf));
            return;
        }
    }

    g_graph->spread_activation(seed_cid, 1.0f);
    int words = g_lexer.generate_text(seed_cid, g_graph, g_cortex, buf, 15,
                                      nullptr, g_drives.doubt);
    if (words > 0) {
        jarvis_say(buf);
        jarvis_neuromod_end_turn(jarvis_pe_first_output_word(g_graph, seed_cid, buf));
        return;
    }

    g_dlg = DialogueState::LISTENING;
    cmd_ingest(user_line);
    g_drives.curiosity += 0.25f;
    if (g_drives.curiosity > 1.0f) g_drives.curiosity = 1.0f;
    jarvis_neuromod_end_turn(jarvis_pe_from_tokens_two_clusters(g_graph, toks, n, seed_cid));
}

static void dispatch_acoustic_command(const char* spoken_text) {
    if (!spoken_text || spoken_text[0] == '\0') return;
    std::string raw = trim_ws(spoken_text);
    if (raw.empty()) return;

    std::string low = lower_ws(raw);
    // Slash/voice hybrids: "?word" — route like typed console
    if (raw.size() >= 2 && raw[0] == '!' && raw[1] != '!' && raw[1] != ' ') {
        dispatch(raw.c_str());
        return;
    }
    if ((raw.size() >= 2 && raw[0] == '?' && raw[1] == '?') ||
        (raw.size() >= 1 && raw[0] == '?')) {
        dispatch(raw.c_str());
        return;
    }

    bool looks_command = (
        strcmp(low.c_str(), "help") == 0 ||
        strcmp(low.c_str(), "quit") == 0 || strcmp(low.c_str(), "exit") == 0 ||
        (low.find("status") != std::string::npos) ||
        (low.find("save") != std::string::npos &&
         (low.find("brain") != std::string::npos || low.find("memory") != std::string::npos)));

    if (looks_command && low.size() <= 48) {
        if (strcmp(low.c_str(), "help") == 0) dispatch("/help");
        else if (strcmp(low.c_str(), "quit") == 0 || strcmp(low.c_str(), "exit") == 0) dispatch("/quit");
        else if (low.find("status") != std::string::npos) dispatch("/status");
        else if (low.find("save") != std::string::npos) dispatch("/save");
        else fpsan_reasoning_cycle_user_turn(raw.c_str());
        return;
    }

    fpsan_reasoning_cycle_user_turn(raw.c_str());
}

// ============================================================
// COMMAND: Ingest a sentence and generate a response
// ============================================================
void cmd_ingest(const char* sentence) {
    g_state = STATE_THINKING;

    int triples = g_lexer.ingest_sentence(sentence, g_graph, &g_tokenizer, g_cortex);

    set_color(COL_DIM);
    printf("  [ingested %d triples]\n", triples);
    set_color(COL_RESET);

    // Phase 8: run lightweight contradiction scan after every ingest.
    // Only check the last 256 nodes to keep it O(n) bounded.
    {
        int nc = g_graph->node_count.load(std::memory_order_acquire);
        int scan_from = std::max(0, nc - 256);
        // Use a temp ClusterGraph pointer slice — we just pass the whole graph;
        // detect_contradictions will iterate all nodes but exits quickly if nc is small.
        int contradictions = g_lexer.detect_contradictions(g_graph, g_cortex);
        if (contradictions > 0) {
            g_drives.on_contradiction();
            set_color(COL_YELLOW);
            printf("  [!] %d contradiction(s) detected — doubt raised to %.2f\n",
                   contradictions, g_drives.doubt);
            set_color(COL_RESET);
        }
    }

    // Find the last content word to use as generation seed
    Token tokens[MAX_TOKENS];
    int n_tokens = g_lexer.tokenize(sentence, tokens);

    int seed_cid = -1;
    for (int i = n_tokens - 1; i >= 0; i--) {
        if (tokens[i].tag != POS_PUNCT && tokens[i].tag != POS_UNKNOWN) {
            seed_cid = find_word(tokens[i].text);
            break;
        }
    }

    // Generate a continuation from the first content word instead
    for (int i = 0; i < n_tokens; i++) {
        if (tokens[i].tag == POS_NOUN || tokens[i].tag == POS_PRON) {
            int cid = find_word(tokens[i].text);
            if (cid >= 0) { seed_cid = cid; break; }
        }
    }

    if (seed_cid >= 0) {
        g_state = STATE_SPEAKING;
        char buf[1024];
        g_graph->clear_activation();
        int words = g_lexer.generate_text(seed_cid, g_graph, g_cortex, buf, 15);
        if (words > 1) {
            jarvis_say(buf);
        } else {
            jarvis_say("I stored that. My associations are still forming.");
        }
    } else {
        jarvis_say("Noted. I don't have strong associations for those words yet.");
    }

    float pe_turn = 0.48f;
    int cid_a = -1, cid_b = -1;
    for (int i = 0; i < n_tokens; ++i) {
        if (tokens[i].tag == POS_PUNCT) continue;
        int c = jarvis_resolve_token_cluster(tokens[i].text);
        if (c < 0) continue;
        if (cid_a < 0)
            cid_a = c;
        else if (c != cid_a) {
            cid_b = c;
            break;
        }
    }
    if (cid_a >= 0 && cid_b >= 0)
        pe_turn = jarvis_pe_sentence_pair(g_graph, cid_a, cid_b);
    else if (cid_a >= 0)
        pe_turn = 0.44f;
    jarvis_neuromod_end_turn(pe_turn);
}

// ============================================================
// COMMAND: ?word — Generate text from a specific word
// ============================================================
void cmd_generate(const char* word) {
    while (word && (*word == ' ' || *word == '\t'))
        ++word;
    if (!word || !word[0]) {
        printf("  Usage: ? <word>  (generate from cluster)\n");
        return;
    }
    int cid = find_word(word);
    if (cid < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "I don't know the word \"%s\" yet. Teach me!", word);
        jarvis_say(msg);
        g_drives.on_unknown_word(word);  // Phase 16D: Feed curiosity drive
        // Show metacognition: unknown seed
        g_meta.reset();
        g_meta.unknown_seed = true;
        set_color(COL_DIM);
        g_meta.print_report();
        set_color(COL_RESET);
        return;
    }

    g_state = STATE_SPEAKING;
    char buf[1024];
    g_graph->clear_activation();
    int words = g_lexer.generate_text(cid, g_graph, g_cortex, buf, 20, &g_meta);
    if (words > 0) {
        // Display confidence-hedged output
        float conf = g_meta.compute_confidence();
        if (conf < 0.3f && conf > 0.0f) {
            char hedged[1280];
            snprintf(hedged, sizeof(hedged), "I'm uncertain, but: %s", buf);
            jarvis_say(hedged);
        } else {
            jarvis_say(buf);
        }
        // Show metacognition stats
        set_color(COL_DIM);
        g_meta.print_report();
        set_color(COL_RESET);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "I know \"%s\", but it has no outgoing word chains yet.", word);
        jarvis_say(msg);
    }
}

// ============================================================
// COMMAND: ??word — Query typed associations
// ============================================================

/// Nodes n with EDGE_TEMPORAL n → target_id (SVO coincidence bindings).
static int collect_temporal_sources_to(ClusterGraph* g, int target_id, int* outs, int cap) noexcept {
    if (!g || !outs || cap <= 0) return 0;
    const int nc = g->node_count.load(std::memory_order_acquire);
    int nout = 0;
    for (int n = 0; n < nc && nout < cap; n++) {
        int ec = g->node(n).edge_count.load(std::memory_order_acquire);
        for (int i = 0; i < ec; i++) {
            const Edge& e = g->node(n).edges[i];
            if (e.type == EDGE_TEMPORAL && e.target == target_id) {
                outs[nout++] = n;
                break;
            }
        }
    }
    return nout;
}

/// First labeled, non-binding TEMPORAL child of an SVO binding node (object slot).
static int binding_object_target(ClusterGraph* g, LanguageCortex* cx, int binding_id) noexcept {
    if (!g) return -1;
    int ec = g->node(binding_id).edge_count.load(std::memory_order_acquire);
    for (int i = 0; i < ec; i++) {
        const Edge& e = g->node(binding_id).edges[i];
        if (e.type != EDGE_TEMPORAL) continue;
        int t = e.target;
        if (t < 0) continue;
        if (g->node(t).is_binding_node.load(std::memory_order_acquire)) continue;
        if (cx && cx->get_word(t)[0] != '\0') return t;
    }
    return -1;
}

void cmd_query(const char* word) {
    while (word && (*word == ' ' || *word == '\t'))
        ++word;
    if (!word || !word[0]) {
        printf("  Usage: ?? <word>  (query typed associations)\n");
        return;
    }
    int cid = find_word(word);
    if (cid < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "I don't know the word \"%s\" yet.", word);
        jarvis_say(msg);
        return;
    }

    g_state = STATE_THINKING;
    set_color(COL_CYAN);
    printf("\n  Associations for \"%s\" (cluster %d):\n", word, cid);
    set_color(COL_RESET);

    const EdgeType types[] = {EDGE_IS_A, EDGE_HAS_A, EDGE_CAN_DO, EDGE_CAUSES,
                             EDGE_NEXT_WORD, EDGE_RELATED, EDGE_TEMPORAL};
    const int n_types = 7;

    bool found_any = false;
    for (int t = 0; t < n_types; t++) {
        const ClusterNode& n = g_graph->node(cid);
        int n_ec = n.edge_count.load(std::memory_order_acquire);
        for (int e = 0; e < n_ec; e++) {
            if (n.edges[e].type != types[t]) continue;
            int           target      = n.edges[e].target;
            const char*   target_word = g_cortex->get_word(target);
            const EdgeType et         = types[t];

            if (et == EDGE_CAUSES && target_word[0] != '\0') {
                set_color(10);
                printf("    [Memory] %s -> causes -> %s (w=%.2f)\n", word, target_word, n.edges[e].weight);
                set_color(COL_RESET);
                found_any = true;
                continue;
            }

            if (et == EDGE_TEMPORAL && target >= 0 &&
                g_graph->node(target).is_binding_node.load(std::memory_order_acquire)) {
                int         obj_id = binding_object_target(g_graph, g_cortex, target);
                const char* obj_w  = (obj_id >= 0) ? g_cortex->get_word(obj_id) : "";
                int         srcs[24];
                int         nsrc = collect_temporal_sources_to(g_graph, target, srcs, 24);
                const char* verb_w = nullptr;
                for (int si = 0; si < nsrc; si++) {
                    if (srcs[si] == cid) continue;
                    const char* lw = g_cortex->get_word(srcs[si]);
                    if (lw && lw[0] != '\0') {
                        verb_w = lw;
                        break;
                    }
                }
                if (obj_w[0] != '\0' && verb_w) {
                    set_color(10);
                    printf("    [Memory] %s -> %s -> %s\n", word, verb_w, obj_w);
                    set_color(COL_RESET);
                    found_any = true;
                    continue;
                }
            }

            if (target_word[0] != '\0') {
                set_color(COL_YELLOW);
                printf("    %s", edge_type_name(et));
                set_color(COL_DIM);
                printf(" -> ");
                set_color(COL_RESET);
                printf("%s (w=%.2f)\n", target_word, n.edges[e].weight);
                found_any = true;
            }
        }
    }

    if (!found_any) {
        jarvis_say("No named associations found for that word.");
    }
    printf("\n");
}

// ============================================================
// COMMAND: /train <file> — Ingest documentation or code
// ============================================================
void cmd_train(const char* filepath) {
    if (!std::filesystem::exists(filepath)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Error: File '%s' not found.", filepath);
        jarvis_say(msg);
        return;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        jarvis_say("Error: Could not open file for reading.");
        return;
    }

    set_color(COL_YELLOW);
    printf("\n  [TRAIN] ");
    set_color(COL_RESET);
    printf("Ingesting file: %s\n", filepath);

    std::string line;
    int line_count = 0;
    int word_count_start = g_cortex->active_count();
    int edges_created = 0;

    g_state = STATE_THINKING;

    while (std::getline(file, line)) {
        size_t b = 0;
        while (b < line.size() && std::isspace(static_cast<unsigned char>(line[b]))) b++;
        if (b >= line.size()) continue;
        if (line[b] == '#') continue;
        if (line.size() >= b + 2 && line[b] == '/' && line[b + 1] == '/') continue;

        // Basic heuristic: lines with indentation or specific code keywords use raw ingestion
        if (line[0] == ' ' || line[0] == '\t' || 
            line.find("def ") != std::string::npos || line.find("class ") != std::string::npos ||
            line.find("import ") != std::string::npos || line.find("return ") != std::string::npos ||
            line.find("struct ") != std::string::npos || line.find("void ") != std::string::npos) {
            edges_created += g_lexer.ingest_raw_sequence(line.c_str(), g_graph, &g_tokenizer, g_cortex);
        } else {
            // Otherwise try standard sentence parsing (which creates PHRASE and SVO bonds)
            edges_created += g_lexer.ingest_sentence(line.c_str(), g_graph, &g_tokenizer, g_cortex);
        }
        
        line_count++;
        if (line_count % 100 == 0) {
            printf("  ... %d lines ingested\n", line_count);
        }
    }

    int new_words = g_cortex->active_count() - word_count_start;
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Training complete. Ingested %d lines. Learned %d new words and formed %d new connections.", 
             line_count, new_words, edges_created);
    jarvis_say(msg);
    g_drives.on_learned();
    g_state = STATE_IDLE;
}

// ============================================================
// COMMAND: /train_dir <dir> — Ingest a whole directory
// ============================================================
void cmd_train_dir(const char* dirpath) {
    if (!std::filesystem::exists(dirpath) || !std::filesystem::is_directory(dirpath)) {
        jarvis_say("Error: Directory not found.");
        return;
    }

    int file_count = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(dirpath);
         it != std::filesystem::recursive_directory_iterator();
         ++it) {
        const auto& entry = *it;
        const auto path_str = entry.path().generic_string();
        if (path_str.find("/.git/") != std::string::npos ||
            path_str.find("/build/") != std::string::npos) {
            if (entry.is_directory()) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            // Only ingest text/code files
            if (ext == ".txt" || ext == ".md" || ext == ".py" || ext == ".h" || ext == ".cpp" || ext == ".c") {
                cmd_train(entry.path().string().c_str());
                file_count++;
            }
        }
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "Directory training complete. Processed %d files.", file_count);
    jarvis_say(msg);
}

// ============================================================
// COMMAND: /research <goal> — Decompose a research goal and persist plan
// ============================================================
void cmd_research(const char* goal) {
    if (!goal || goal[0] == '\0') {
        jarvis_say("Usage: /research <goal>");
        return;
    }
    g_research_cortex.agent.decompose_goal(goal);
    std::string path = g_research_cortex.agent.persist_plan();
    if (path.empty()) {
        jarvis_say("Failed to save research plan.");
        return;
    }
    char msg[512];
    snprintf(msg, sizeof(msg), "Research plan saved: %s", path.c_str());
    jarvis_say(msg);
    // Announce first step (preview only, do not consume task)
    std::string step;
    if (!g_research_cortex.agent.tasks.empty()) {
        step = g_research_cortex.agent.tasks.front();
    }
    if (!step.empty()) {
        char buf[512]; snprintf(buf, sizeof(buf), "First step: %s", step.c_str());
        jarvis_say(buf);
    }
}

// COMMAND: /research_run[_net] — execute the next research step asynchronously (local-only by default)
void cmd_research_run(const char* arg) {
    bool allow_net = false;
    if (arg) {
        std::string mode = lower_ws(trim_ws(arg));
        if (mode == "net" || mode == "web" || mode == "online") {
            allow_net = true;
        }
    }
    
    // Store the goal for cognitive handoff later
    g_research_cortex.last_goal = g_research_cortex.agent.tasks.empty() ? 
        std::string("General research") : 
        std::string("Continue research");
    
    // Signal the async research thread with network flag
    if (g_research_cortex.agent.remaining() > 0) {
        g_research_cortex.request_next_step(allow_net);
        if (allow_net) {
            jarvis_say("Research task queued. I am investigating sources online in the background.");
        } else {
            jarvis_say("Research task queued. I am investigating in the background.");
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "%d steps remaining after this one.", 
                 std::max(0, g_research_cortex.agent.remaining() - 1));
        jarvis_say(buf);
    } else {
        jarvis_say("No research steps remaining. Use /research <goal> to start a new investigation.");
    }
}

// COMMAND: /research_status — show current plan path and remaining tasks
void cmd_research_status() {
    // Show basic info
    char buf[512];
    if (!g_research_cortex.agent.last_plan_path.empty()) {
        snprintf(buf, sizeof(buf), "Plan file: %s", g_research_cortex.agent.last_plan_path.c_str());
        jarvis_say(buf);
    } else {
        jarvis_say("No plan file created yet.");
    }
    
    if (g_research_cortex.is_research_complete()) {
        jarvis_say("[ASYNC] Research step complete!");
    } else if (g_research_cortex.research_pending.load() || g_research_cortex.is_research_busy()) {
        jarvis_say("[ASYNC] Research step in progress...");
    }
    
    snprintf(buf, sizeof(buf), "%d steps remaining.", g_research_cortex.agent.remaining());
    jarvis_say(buf);
    
    // Show completed, active, and remaining tasks as a live checklist.
    set_color(COL_CYAN);
    printf("\n  Research Progress:\n");
    set_color(COL_RESET);

    std::vector<std::string> completed_steps;
    std::string current_step;
    {
        std::lock_guard<std::mutex> lock(g_research_cortex.progress_lock);
        completed_steps = g_research_cortex.completed_steps;
        current_step = g_research_cortex.current_step_label;
    }

    int idx = 1;
    for (const std::string& done : completed_steps) {
        printf("   [%c] %d. %s\n", 'x', idx++, done.c_str());
    }

    if ((g_research_cortex.research_pending.load() || g_research_cortex.is_research_busy()) &&
        !current_step.empty()) {
        printf("   [%c] %d. %s\n", '>', idx++, current_step.c_str());
    }

    for (const std::string& remaining : g_research_cortex.agent.tasks) {
        printf("   [%c] %d. %s\n", ' ', idx++, remaining.c_str());
    }

    printf("\n");
}

// ============================================================
// COMMAND: /status — Brain statistics
// ============================================================
void cmd_status() {
    int alive = 0, total_edges = 0;
    float sum_voltage = 0.0f;
    const int nc_s = g_graph->node_count.load(std::memory_order_acquire);
    for (int i = 0; i < nc_s; i++) {
        if (g_graph->node(i).alive.load(std::memory_order_acquire)) {
            alive++;
            total_edges += g_graph->node(i).edge_count.load(std::memory_order_acquire);
            sum_voltage += g_graph->node(i).activation.load(std::memory_order_relaxed);
        }
    }
    int cortex_active = g_cortex->active_count();
    double ws_mb = 0.;
    bool have_ws = false;
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) {
        ws_mb = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
        have_ws = true;
    }
    double art_mb = 0.;
    {
        uint64_t bytes = 0;
        try {
            std::filesystem::path root("artefacts");
            if (std::filesystem::exists(root)) {
                for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
                    if (e.is_regular_file())
                        bytes += (uint64_t)e.file_size();
                }
            }
        } catch (...) {
        }
        art_mb = (double)bytes / (1024.0 * 1024.0);
    }
    const double upt_s = (double)g_tick / 1000.0;

    const uint64_t wtick = g_worst_tick_us.load(std::memory_order_relaxed);
    const uint64_t wveto = g_worst_shadow_veto_check_us.load(std::memory_order_relaxed);

    set_color(COL_CYAN);
    printf("\n  +---------------------------+----------------------+\n");
    set_color(COL_RESET);
    printf("  | %-25s | %-20s |\n", "Telemetry", "Value");
    set_color(COL_CYAN);
    printf("  +---------------------------+----------------------+\n");
    set_color(COL_RESET);
    printf("  | %-25s | %-20s |\n", "State", state_name(g_state));
    printf("  | %-25s | %-20llu |\n", "Tick", (unsigned long long)g_tick);
    printf("  | %-25s | ~%.1f s (~1 kHz)     |\n", "Uptime (approx)", upt_s);
    printf("  | %-25s | %-20d |\n", "Alive nodes", alive);
    printf("  | %-25s | %-20d |\n", "Total edges", total_edges);
    printf("  | %-25s | %-20d |\n", "Known words", cortex_active);
    printf("  | %-25s | %-20.4f |\n", "Sum voltage", sum_voltage);
    printf("  | %-25s | %-20.4f |\n", "Max voltage", g_max_voltage);
    printf("  | %-25s | %-20.3f |\n", "Plasticity", fpsan::plasticity_scale_load());
    printf("  | %-25s | %-20.3f |\n", "Arousal", fpsan::arousal_load());
    printf("  | %-25s | %-20.3f |\n", "Last PE", fpsan::last_prediction_error_load());
    if (have_ws)
        printf("  | %-25s | %-20.2f MB |\n", "Working set (process)", ws_mb);
    else
        printf("  | %-25s | %-20s |\n", "Working set (process)", "n/a");
    printf("  | %-25s | %-20.2f MB |\n", "Artefacts disk", art_mb);
    printf("  | %-25s | %-20llu us |\n", "Worst tick", (unsigned long long)wtick);
    printf("  | %-25s | %-20llu us |\n", "Worst ShadowBrain check", (unsigned long long)wveto);
    printf("  | %-25s | %-20s |\n", "Brain file", g_brain_file.c_str());
    set_color(COL_CYAN);
    printf("  +---------------------------+----------------------+\n\n");
    set_color(COL_RESET);
}

// ============================================================
// COMMAND: /save — Manual save
// ============================================================
void cmd_save() {
    g_state = STATE_SLEEPING;
    jarvis_say("Entering sleep cycle...");
    {
        std::lock_guard<std::mutex> lock(g_brain_io_mutex);
        SynapticMemory::sleep(g_brain_file.c_str(), g_graph, g_cortex);
    }
    jarvis_say("Brain saved. Resuming.");
}

// ============================================================
// COMMAND: /help
// ============================================================
void cmd_help() {
    set_color(COL_CYAN);
    printf("\n  === ENGRAM CORE COMMANDS ===\n");
    set_color(COL_RESET);
    printf("  <sentence>       Teach Engram Core a fact (ingests + responds)\n");
    printf("  !ingest <text>   Same as a plain sentence — explicit ingest alias\n");
    printf("  ?<word>           Generate speech starting from <word>\n");
    printf("  ??<word>          Query all typed associations for <word>\n");
    set_color(COL_CYAN);
    printf("\n  === MOTOR COMMANDS ===\n");
    set_color(COL_RESET);
    printf("  !open notepad     Open Notepad (or any app)\n");
    printf("  !type <text>      Type text into focused window\n");
    printf("  !focus <title>    Focus window by partial title\n");
    printf("  !windows          List all open windows\n");
    printf("  !run <procedure>  Execute a named procedure\n");
    printf("  !speak [word]     With <word>: generate + type into focused window; alone: test voice\n");
    printf("  !killswitch       Reset the ESC kill switch\n");
    set_color(COL_CYAN);
    printf("\n  === VISION ===\n");
    set_color(COL_RESET);
    printf("  !see              See what Engram Core currently sees (UIA + ASCII fovea)\n");
    printf("  !diff             Show temporal difference spikes\n");
    printf("  !verify           Verify target window is active\n");
    set_color(COL_CYAN);
    printf("\n  === METACOGNITION ===\n");
    set_color(COL_RESET);
    printf("  !meta             Show last confidence report\n");
    set_color(COL_CYAN);
    printf("\n  === SOVEREIGN AGI ===\n");
    set_color(COL_RESET);
    printf("  !drives           Show internal drives (curiosity, boredom, frustration)\n");
    printf("  !goal <text>      Set a goal via semantic motor binding\n");
    printf("  !goals            Show current goal status\n");
    printf("  !mute             Toggle voice on/off\n");
    printf("  !ears             Toggle acoustic cortex on/off\n");
    printf("  !ears_status      Show microphone/wake-word status\n");
    printf("  !ears_listen      Force microphone listen 15s (no wake word)\n");
    printf("  (Talk naturally — Engram Core generates responses from its neural graph)\n");
    printf("  (Teach actions: \"To save, press Control and S\")\n");
    set_color(COL_CYAN);
    printf("\n  === SYSTEM ===\n");
    set_color(COL_RESET);
    printf("  /status           Show brain statistics\n");
    printf("  /dashboard        R0 telemetry (tiers, edge mix, neuromod)\n");
    printf("  /save             Manually save brain to disk\n");
    printf("  /load             Load brain from disk\n");
    printf("  /train <file>     Ingest documentation or code\n");
    printf("  /train_dir <dir>  Ingest all text/code in directory\n");
    printf("  /research <goal>  Start background research investigation\n");
    printf("  /research_run [net] Execute next research step (net=web fetch)\n");
    printf("  /research_status  Show research plan and progress\n");
    printf("  /metamorph <tok>  Compile + hot-load meta_<tok>.dll (R4 registry enforced)\n");
    printf("  /wasm_run <path>  Load WASM (resolves vs exe dir, repo root, fixtures\\phase14)\n");
    printf("  /words            List all known words\n");
    printf("  /help             Show this help\n");
    printf("  /quit             Save and exit\n");
    set_color(COL_DIM);
    printf("\n  Periodic auto-save: set env AUTO_SAVE_SECONDS to interval in seconds (1-86400); unset or 0 = off.\n");
    printf("  Hold ESC at any time to engage the hardware kill switch.\n\n");
    set_color(COL_RESET);
}

// ============================================================
// COMMAND: /words — List all known words
// ============================================================
void cmd_words() {
    set_color(COL_CYAN);
    printf("\n  Known vocabulary:\n  ");
    set_color(COL_RESET);
    int count = 0;
    for (int i = 0; i < LANG_CLUSTERS; i++) {
        if (g_cortex->clusters[i].active && g_cortex->clusters[i].word_label[0] != '\0') {
            printf("%s", g_cortex->clusters[i].word_label);
            count++;
            if (count % 10 == 0) printf("\n  ");
            else printf("  ");
        }
    }
    printf("\n  (%d words total)\n\n", count);
}

// ============================================================
// COMMAND: /load
// ============================================================
void cmd_load() {
    jarvis_say("Loading brain...");
    {
        std::lock_guard<std::mutex> lock(g_brain_io_mutex);
        if (SynapticMemory::wake(g_brain_file.c_str(), g_graph, g_cortex)) {
            jarvis_say("Brain loaded. All memories restored.");
        } else {
            jarvis_say("No brain file found. Starting with a clean slate.");
        }
    }
}

// ============================================================
// COMMAND: /metamorph <token> — compile + hot-load motor primitive (R4 registry)
// ============================================================
void cmd_metamorph(const char* arg) {
    if (!g_graph || !g_cortex) {
        jarvis_say("Brain not ready.");
        return;
    }
    while (arg && (*arg == ' ' || *arg == '\t')) ++arg;
    if (!arg || !arg[0]) {
        printf("  Usage: /metamorph <concept_token>  (alphanumeric; used as meta_<token>.dll)\n");
        jarvis_say("Usage: slash metamorph, then a concept token.");
        return;
    }

    char slug[96]{};
    metamorph_sanitize_slug(arg, slug, sizeof(slug));

    int8_t h[LANG_WORD_DIM]{};
    g_tokenizer.encode_word_hash(std::string(slug), h);
    const int concept_id = g_cortex->perceive(h, true, slug);
    if (concept_id < 0) {
        jarvis_say("Could not allocate language cluster for metamorph.");
        return;
    }

    CreateDirectoryA("build", nullptr);
    const uint64_t ep = g_episode_counter.load(std::memory_order_relaxed);
    set_color(COL_CYAN);
    printf("\n  [Metamorphic] slug=%s concept_id=%d episode=%llu\n", slug, concept_id,
           (unsigned long long)ep);
    set_color(COL_RESET);

    const bool ok = g_metamorphic.hot_load(slug, concept_id, g_graph, g_cortex, g_graph->graph_rw_lock,
                                          "cl.exe", "build", &g_self_edit_reg, ep);

    char dll_report[MAX_PATH]{};
    if (g_metamorphic.dll_path[0] != '\0')
        std::strncpy(dll_report, g_metamorphic.dll_path, sizeof(dll_report) - 1);
    else
        std::snprintf(dll_report, sizeof(dll_report), "build\\meta_%s.dll", slug);

    uint64_t bytes = 0;
    (void)metamorphic_artefact_file_size_bytes(dll_report, &bytes);

    if (ok) {
        printf("  [Metamorphic] OK  path=%s  bytes=%llu  (registered in self_edits.csv)\n",
               dll_report, (unsigned long long)bytes);
        refresh_translation_cortex();
        jarvis_say("Metamorphic hot load complete. Motor primitive registered.");
    } else {
        printf("  [Metamorphic] FAIL path=%s  bytes=%llu (compile, LoadLibrary, export, or registry cap)\n",
               dll_report, (unsigned long long)bytes);
        jarvis_say("Metamorphic hot load failed. See console.");
    }
}

// ============================================================
// COMMAND: /wasm_run <path> — load wasm bytes (policy gate + timing; wasm3)
// ============================================================
static void wasm_push_try_path(std::vector<std::string>* paths, const char* p) noexcept {
    if (!paths || !p || !p[0]) return;
    std::string s(p);
    for (char& c : s) {
        if (c == '/')
            c = '\\';
    }
    for (const std::string& x : *paths) {
        if (x == s)
            return;
    }
    paths->push_back(std::move(s));
}

static bool wasm_read_bytes_resolved(const char* user_path, std::vector<uint8_t>* out,
                                     std::string* tried_report) noexcept {
    if (!user_path || !out || std::strstr(user_path, "..") != nullptr)
        return false;

    std::vector<std::string> paths;
    wasm_push_try_path(&paths, user_path);

    char mod[MAX_PATH]{};
    GetModuleFileNameA(nullptr, mod, MAX_PATH);
    char exe_dir[MAX_PATH]{};
    std::strncpy(exe_dir, mod, sizeof(exe_dir) - 1);
    char* slash = std::strrchr(exe_dir, '\\');
    if (slash)
        *slash = '\0';

    char repo_dir[MAX_PATH]{};
    std::strncpy(repo_dir, exe_dir, sizeof(repo_dir) - 1);
    slash = std::strrchr(repo_dir, '\\');
    if (slash && _stricmp(slash + 1, "build") == 0)
        *slash = '\0';

    char buf[MAX_PATH]{};
    std::snprintf(buf, sizeof(buf), "%s\\%s", exe_dir, user_path);
    wasm_push_try_path(&paths, buf);
    std::snprintf(buf, sizeof(buf), "%s\\%s", repo_dir, user_path);
    wasm_push_try_path(&paths, buf);

    if (!std::strchr(user_path, '\\') && !std::strchr(user_path, '/')) {
        std::snprintf(buf, sizeof(buf), "%s\\fixtures\\phase14\\%s", repo_dir, user_path);
        wasm_push_try_path(&paths, buf);
    }

    tried_report->clear();
    for (const std::string& cand : paths) {
        *tried_report += "  tried: ";
        *tried_report += cand;
        *tried_report += "\n";
        std::ifstream inf(cand, std::ios::binary);
        if (!inf)
            continue;
        inf.seekg(0, std::ios::end);
        const std::streampos sz = inf.tellg();
        inf.seekg(0, std::ios::beg);
        if (sz <= 0 || sz > (std::streampos)(2 * 1024 * 1024))
            continue;
        out->resize(static_cast<size_t>(sz));
        if (!inf.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(sz))) {
            out->clear();
            continue;
        }
        return true;
    }
    out->clear();
    return false;
}

void cmd_wasm_run(const char* path_utf8) {
    if (!path_utf8) return;
    while (*path_utf8 == ' ' || *path_utf8 == '\t') ++path_utf8;
    if (!path_utf8[0]) {
        printf("  Usage: /wasm_run <path-to-.wasm>\n");
        printf("  Tip: paths resolve against exe dir, repo root, and repo\\fixtures\\phase14\\\n");
        return;
    }

    auto t_policy0 = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> wasm;
    std::string tried;
    if (!wasm_read_bytes_resolved(path_utf8, &wasm, &tried)) {
        printf("  [Wasm] Cannot open file (cwd may be wrong). Tried:\n%s", tried.c_str());
        return;
    }
    if (wasm.size() < 4 || wasm[0] != 0x00 || wasm[1] != 0x61 || wasm[2] != 0x73 || wasm[3] != 0x6d) {
        printf("  [Wasm] VETO: missing WASM magic (0x0061736d).\n");
        return;
    }
    auto t_policy1 = std::chrono::high_resolution_clock::now();
    const int64_t policy_us =
        std::chrono::duration_cast<std::chrono::microseconds>(t_policy1 - t_policy0).count();

    auto t_load0 = std::chrono::high_resolution_clock::now();
    fpsan::WasmSandbox sandbox;
    fpsan::WasmSandboxBudget b{};
    if (!sandbox.load(wasm.data(), wasm.size(), b)) {
        printf("  [Wasm] load failed: %s  policy_check_us=%lld  (file read OK; wasm3 parse/link error)\n",
               sandbox.last_trap() ? sandbox.last_trap() : "?",
               (long long)policy_us);
        return;
    }
    auto t_load1 = std::chrono::high_resolution_clock::now();
    const int64_t load_us =
        std::chrono::duration_cast<std::chrono::microseconds>(t_load1 - t_load0).count();

    printf("  [Wasm] OK load  policy_check_us=%lld  load_us=%lld  bytes=%zu\n",
           (long long)policy_us, (long long)load_us, wasm.size());
}

// ============================================================
// MOTOR COMMANDS (! prefix)
// ============================================================
void cmd_motor_open(const char* app) {
    g_state = STATE_THINKING;
    char cmd[512];
    // If just a name like "notepad", try the built-in procedure first
    char proc_name[128];
    snprintf(proc_name, sizeof(proc_name), "open_%s", app);
    strncpy(g_target_window, app, 255); // Remember target
    g_target_window[255] = '\0';

    if (g_motor.find_procedure(proc_name)) {
        g_motor.execute_procedure(proc_name);
        
        // READINESS GATE: Verify app is actually in foreground after launch
        if (wait_for_foreground_window(app, 3000)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Opened %s.", app);
            jarvis_say(msg);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "Launched %s but could not verify it's visible.", app);
            jarvis_say(msg);
        }
    } else {
        // Try launching directly first (ShellExecuteA handles registry lookups)
        if (g_motor.launch_app(app)) {
            Sleep(1500);
            
            // READINESS GATE: Verify app is actually in foreground after launch
            if (wait_for_foreground_window(app, 3000)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Launched %s.", app);
                jarvis_say(msg);
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "Launched %s but could not verify it's visible.", app);
                jarvis_say(msg);
            }
        } else if (!strstr(app, ".exe")) {
            // Only append .exe if not already present (prevents chrome.exe.exe)
            snprintf(cmd, sizeof(cmd), "%s.exe", app);
            if (g_motor.launch_app(cmd)) {
                Sleep(1500);
                
                // READINESS GATE: Verify app is actually in foreground after launch
                if (wait_for_foreground_window(app, 3000)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Launched %s.", app);
                    jarvis_say(msg);
                } else {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Launched %s but could not verify it's visible.", app);
                    jarvis_say(msg);
                }
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "I couldn't launch '%s'.", app);
                jarvis_say(msg);
            }
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "I couldn't launch '%s'.", app);
            jarvis_say(msg);
        }
    }
}

void cmd_motor_type(const char* text) {
    g_state = STATE_SPEAKING;
    if (g_target_window[0] != '\0') {
        g_motor.focus_window(g_target_window);
        
        // READINESS GATE: Verify target window is actually foreground before typing
        if (!wait_for_foreground_window(g_target_window, 3000)) {
            jarvis_say("I could not verify the target window is visible. Will attempt typing anyway.");
        }
    }
    set_color(COL_DIM);
    printf("  [typing %d chars into focused window]\n", (int)strlen(text));
    set_color(COL_RESET);
    g_motor.queue_string(text);
    jarvis_say("Done typing.");
}

void cmd_motor_speak(const char* word) {
    // Generate text from the word, then physically type it
    int cid = find_word(word);
    if (cid < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "I don't know the word \"%s\" yet.", word);
        jarvis_say(msg);
        return;
    }

    g_state = STATE_SPEAKING;
    if (g_target_window[0] != '\0') {
        // Phase 16B: Verify target window before typing
        g_vision.proprioception.tick();
        if (!g_vision.verify_target(g_target_window)) {
            set_color(COL_DIM);
            printf("  [Visual] Target \"%s\" not in foreground, focusing...\n", g_target_window);
            set_color(COL_RESET);
        }
        g_motor.focus_window(g_target_window);
        
        // READINESS GATE: Verify target window is actually foreground before typing
        if (!wait_for_foreground_window(g_target_window, 3000)) {
            jarvis_say("I could not verify the target window is visible. Will attempt typing anyway.");
        }
    }
    char buf[1024];
    g_graph->clear_activation();
    int words = g_lexer.generate_text(cid, g_graph, g_cortex, buf, 20, &g_meta);
    if (words > 0) {
        jarvis_say(buf);
        set_color(COL_DIM);
        g_meta.print_report();
        printf("  [now typing into focused window...]\n");
        set_color(COL_RESET);
        // Capture pre-type visual state for efference copy
        g_vision.tick();
        g_motor.queue_string(buf);
        g_motor.queue_key(VK_RETURN); // Newline for clean separation
        // Efference copy check: did screen change?
        Sleep(300); // Give Windows time to render the typed text
        g_vision.tick();
        if (g_vision.detected_change()) {
            set_color(COL_DIM);
            printf("  [Efference] Visual change confirmed (%d spikes)\n", g_vision.fovea.total_spikes);
            set_color(COL_RESET);
        } else {
            set_color(12); // Red
            printf("  [Efference] WARNING: No visual change detected! Text may not have reached target.\n");
            set_color(COL_RESET);
        }
    } else {
        jarvis_say("I have nothing to say about that yet.");
    }
}

// ============================================================
// DISPATCH: Route input to commands
// ============================================================
void dispatch(const char* input) {
    // Skip empty input
    if (input[0] == '\0') return;

    // System commands
    if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
        cmd_save();
        g_running = false;
        return;
    }
    if (strcmp(input, "/status") == 0) { cmd_status(); return; }
    if (strcmp(input, "/save") == 0)   { cmd_save(); return; }
    if (strcmp(input, "/load") == 0)   { cmd_load(); return; }
    if (strncmp(input, "/train_dir ", 11) == 0) { cmd_train_dir(input + 11); return; }
    if (strncmp(input, "/train ", 7) == 0) { cmd_train(input + 7); return; }
    if (strncmp(input, "/research ", 10) == 0) { cmd_research(input + 10); return; }
    if (strcmp(input, "/research_run") == 0) { cmd_research_run(nullptr); return; }
    if (strncmp(input, "/research_run ", 13) == 0) { cmd_research_run(input + 13); return; }
    if (strcmp(input, "/research_status") == 0) { cmd_research_status(); return; }
    if (strcmp(input, "/dashboard") == 0) { cmd_dashboard(); return; }
    if (strncmp(input, "/metamorph ", 11) == 0) {
        cmd_metamorph(input + 11);
        return;
    }
    if (strcmp(input, "/metamorph") == 0) {
        printf("  Usage: /metamorph <concept_token>\n");
        return;
    }
    if (strncmp(input, "/wasm_run ", 10) == 0) {
        cmd_wasm_run(input + 10);
        return;
    }
    if (strcmp(input, "/wasm_run") == 0) {
        printf("  Usage: /wasm_run <path-to-.wasm>\n");
        return;
    }
    if (strcmp(input, "/help") == 0)   { cmd_help(); return; }
    if (strcmp(input, "/words") == 0)  { cmd_words(); return; }
    if (strcmp(input, "/reset") == 0) {
        jarvis_say("Wiping brain... I will forget everything.");
        DeleteFileA(g_brain_file.c_str());
        g_graph->init(6500);
        g_cortex->init();
        g_translation.clear();
        for (int i = 0; i < Identity::SELF_KNOWLEDGE_COUNT; i++) {
            g_lexer.ingest_sentence(Identity::SELF_KNOWLEDGE[i], g_graph, &g_tokenizer, g_cortex);
        }
        ingest_core_directives_txt(true, false);
        refresh_translation_cortex();
        jarvis_say("Brain reset. I am a blank slate. Teach me.");
        return;
    }

    // Motor commands (! prefix)
    if (input[0] == '!') {
        const char* args = input + 1;

        if (strncmp(args, "open ", 5) == 0) {
            cmd_motor_open(args + 5);
            return;
        }
        if (strncmp(args, "type ", 5) == 0) {
            cmd_motor_type(args + 5);
            return;
        }
        if (strncmp(args, "focus ", 6) == 0) {
            strncpy(g_target_window, args + 6, 255);
            g_target_window[255] = '\0';
            g_motor.focus_window(g_target_window);
            return;
        }
        if (strcmp(args, "windows") == 0) {
            set_color(COL_CYAN);
            printf("\n  === OPEN WINDOWS ===\n");
            set_color(COL_RESET);
            int n = g_motor.list_windows();
            printf("  (%d windows)\n\n", n);
            return;
        }
        if (strncmp(args, "run ", 4) == 0) {
            g_motor.execute_procedure(args + 4);
            return;
        }
        if (strncmp(args, "speak ", 6) == 0) {
            const char* w = args + 6;
            while (*w == ' ' || *w == '\t') ++w;
            if (*w == '\0')
                jarvis_say("Voice system active. I can speak.");
            else
                cmd_motor_speak(w);
            return;
        }
        if (strcmp(args, "speak") == 0) {
            jarvis_say("Voice system active. I can speak.");
            return;
        }
        if (strcmp(args, "killswitch") == 0) {
            g_motor.reset_kill_switch();
            return;
        }
        // Phase 8: run full contradiction scan on demand
        if (strcmp(args, "contradictions") == 0) {
            CreateDirectoryA("artefacts", nullptr);
            int n = g_lexer.detect_contradictions(g_graph, g_cortex, "artefacts/contradictions.csv");
            char msg[128];
            if (n > 0) {
                snprintf(msg, sizeof(msg),
                         "%d contradiction(s) logged to artefacts/contradictions.csv", n);
                g_drives.on_contradiction();
            } else {
                snprintf(msg, sizeof(msg), "No contradictions detected in current graph.");
                g_drives.on_confirmed();
            }
            jarvis_say(msg);
            return;
        }
        // Phase 5B: Load a knowledge_mass.bin file into the graph.
        if (strncmp(args, "load_mass", 9) == 0) {
            const char* path = (args[9] == ' ' && args[10] != '\0')
                               ? args + 10 : "knowledge_mass.bin";
            printf("\n  [KnowledgeMass] Loading '%s' …\n", path);
            KnowledgeMass& km = get_knowledge_mass();
            int loaded = km.load(path, g_graph, g_cortex, &g_tokenizer, &g_lexer);
            if (loaded < 0) {
                char msg[200];
                snprintf(msg, sizeof(msg), "Load failed: %s", km.last_error);
                jarvis_say(msg);
            } else {
                km.print_stats();
                char msg[200];
                snprintf(msg, sizeof(msg),
                         "Knowledge mass loaded. %d triples injected, %llu total nodes.",
                         loaded, (unsigned long long)km.nodes_created);
                jarvis_say(msg);
            }
            return;
        }

        // Phase 7: Read the foreground window via UIA and ingest visible text.
        if (strcmp(args, "see") == 0) {
            HWND hwnd = GetForegroundWindow();
            std::string visible = g_vision.uia.read_visible_text(hwnd);
            if (visible.empty()) {
                g_vision.tick();
                g_vision.proprioception.print_status();
                g_vision.fovea.print_ascii();
                jarvis_say("No readable text found in foreground window.");
            } else {
                // Print truncated preview
                set_color(COL_CYAN);
                printf("\n  [Vision] UIA text (%zu chars):\n", visible.size());
                set_color(COL_DIM);
                printf("  %.400s%s\n", visible.c_str(),
                       visible.size() > 400 ? " …" : "");
                set_color(COL_RESET);
                // Ingest every sentence into the graph
                int before = g_graph->node_count.load(std::memory_order_acquire);
                g_lexer.ingest_sentence(visible.c_str(), g_graph, &g_tokenizer, g_cortex);
                int after = g_graph->node_count.load(std::memory_order_acquire);
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "I can see the screen. Ingested %d new concepts.", after - before);
                jarvis_say(msg);
            }
            return;
        }

        if (strcmp(args, "diff") == 0) {
            g_vision.tick();
            g_vision.fovea.print_diff_ascii();
            return;
        }
        if (strcmp(args, "verify") == 0) {
            g_vision.proprioception.tick();
            if (g_target_window[0] != '\0') {
                bool ok = g_vision.verify_target(g_target_window);
                if (ok) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Target \"%s\" is active and visible.", g_target_window);
                    jarvis_say(msg);
                } else {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Target \"%s\" is NOT in foreground!", g_target_window);
                    jarvis_say(msg);
                }
            } else {
                jarvis_say("No target window set. Use !open or !focus first.");
            }
            g_vision.proprioception.print_status();
            return;
        }
        // Phase 16C: Metacognition command
        if (strcmp(args, "confidence") == 0 || strcmp(args, "meta") == 0) {
            g_meta.print_report();
            return;
        }
        // Phase 16D: Drive & goal commands
        if (strcmp(args, "drives") == 0) {
            g_drives.print_status();  // Print BEFORE on_user_input resets boredom
            return;
        }
        if (strncmp(args, "goal ", 5) == 0) {
            const char* goal_body = args + 5;
            while (*goal_body == ' ' || *goal_body == '\t')
                ++goal_body;

            const auto     t_sv0 = std::chrono::high_resolution_clock::now();
            const bool     intent_disk_veto = user_goal_text_disk_policy_blocks(goal_body);
            const auto     t_sv1 = std::chrono::high_resolution_clock::now();
            const uint64_t veto_us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(t_sv1 - t_sv0).count());

            if (intent_disk_veto) {
                uint64_t prev_sv = g_worst_shadow_veto_check_us.load(std::memory_order_relaxed);
                while (veto_us > prev_sv &&
                       !g_worst_shadow_veto_check_us.compare_exchange_weak(
                           prev_sv, veto_us, std::memory_order_relaxed)) {
                }
                set_color(12);
                printf(
                    "  [ShadowBrain] VETO (check took %llu us) — disk/OS safety policy (format, diskpart, "
                    "cipher /w, etc.).\n",
                    (unsigned long long)veto_us);
                set_color(COL_RESET);
                jarvis_say("ShadowBrain veto. Unsafe motor plan blocked.");
                return;
            }

            if (g_goals.set_goal(goal_body, g_graph, g_cortex)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Goal set: \"%s\"", goal_body);
                jarvis_say(msg);
                g_goals.print_status();
            } else {
                jarvis_say("I don't know how to do that yet. My confidence is zero. Can you teach me?");
            }
            return;
        }
        // Explicit ingest alias (same as typing a bare sentence through the reasoning cycle).
        if (strncmp(args, "ingest ", 7) == 0) {
            cmd_ingest(args + 7);
            return;
        }
        if (strcmp(args, "ingest") == 0) {
            printf("  Usage: !ingest <sentence>  (teaches facts; same as a plain sentence)\n");
            jarvis_say("Say !ingest followed by a sentence to teach me.");
            return;
        }
        if (strcmp(args, "goals") == 0) {
            g_goals.print_status();
            return;
        }
        // Phase 17: Voice commands
        if (strcmp(args, "mute") == 0) {
            g_voice.toggle();
            return;
        }
        if (strcmp(args, "ears") == 0) {
            g_ears.toggle();
            jarvis_say(g_ears.is_enabled() ? "Acoustic cortex enabled." : "Acoustic cortex muted.");
            return;
        }
        if (strcmp(args, "ears_status") == 0) {
            char status[256];
            snprintf(status, sizeof(status), "Ears: %s, initialized: %s, active: %s, wake-energy: %.2f, last_error: %s",
                     g_ears.is_enabled() ? "enabled" : "muted",
                     g_ears.is_initialized() ? "yes" : "no",
                     g_ears.active_mode.load() ? "yes" : "no",
                     g_ears.last_energy.load(),
                     g_ears.last_error_string());
            jarvis_say(status);
            return;
        }
        if (strcmp(args, "ears_listen") == 0) {
            g_ears.force_listen_ms(15000);
            jarvis_say("Ears forced active for 15 seconds. Speak now (no wake word needed).");
            return;
        }

        jarvis_say("Unknown motor command. Type /help for options.");
        return;
    }

    // ??word — Query associations
    if (input[0] == '?' && input[1] == '?') {
        cmd_query(input + 2);
        return;
    }

    // ?word — Generate from word
    if (input[0] == '?') {
        cmd_generate(input + 1);
        return;
    }

    // Phase 13 — single graph-first sovereign cycle (motor rules handled inside).
    fpsan_reasoning_cycle_user_turn(input);
}

// ============================================================
// BOOT BANNER
// ============================================================
void print_banner() {
    set_color(COL_CYAN);
    printf("\n  Engram Core  |  FP-SAN cognitive daemon\n");
    set_color(COL_DIM);
    printf("  FP-SAN v17.0  ·  release v1.0.0\n");
    printf("  --------------------------------------------\n");
    set_color(COL_RESET);
}

// ============================================================
// MAIN: THE LIVE LOOP
// ============================================================
int main(int argc, char** argv) {
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    // Enable virtual terminal processing for cleaner output
    DWORD mode;
    GetConsoleMode(hConsole, &mode);
    SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    print_banner();

    init_brain_file_path();
    init_auto_save_seconds();
    if (g_auto_save_seconds > 0) {
        set_color(COL_DIM);
        printf("  [BOOT] AUTO_SAVE_SECONDS=%d (periodic save on)\n", g_auto_save_seconds);
        set_color(COL_RESET);
    }

    // ── BOOT SEQUENCE ──
    g_state = STATE_BOOTING;
    auto t_sub0 = std::chrono::high_resolution_clock::now();
    g_workspace.init(); // Main desktop mode: keep windows visible to the user

    g_graph = new ClusterGraph();
    g_cortex = new LanguageCortex();
    g_lexer.init();
    g_motor.init();
    g_motor.bootstrap_procedures();
    g_vision.init();   // Phase 16B: Initialize visual system
    g_meta.reset();    // Phase 16C: Initialize metacognition
    g_drives.init();   // Phase 16D: Initialize homeostatic drives
    g_goals.init();    // Phase 16D: Initialize goal planner
    g_voice.init();    // Phase 17: Initialize text-to-speech
    g_ears.init();     // Phase 20: Initialize acoustic cortex (continuous dictation)
    g_research_cortex.init();  // Phase 6B: Initialize async research cortex
    auto t_sub1 = std::chrono::high_resolution_clock::now();
    double sub_ms =
        std::chrono::duration<double, std::milli>(t_sub1 - t_sub0).count();

    // Add extended vocabulary to lexer
    g_lexer.lexicon.insert("hungry", POS_ADJ);
    g_lexer.lexicon.insert("hunt", POS_VERB);
    g_lexer.lexicon.insert("prey", POS_NOUN);
    g_lexer.lexicon.insert("looks", POS_VERB);
    g_lexer.lexicon.insert("food", POS_NOUN);
    g_lexer.lexicon.insert("fast", POS_ADV);
    g_lexer.lexicon.insert("small", POS_ADJ);
    g_lexer.lexicon.insert("runs", POS_VERB);
    g_lexer.lexicon.insert("flies", POS_VERB);
    g_lexer.lexicon.insert("swims", POS_VERB);
    g_lexer.lexicon.insert("eats", POS_VERB);
    g_lexer.lexicon.insert("tall", POS_ADJ);
    g_lexer.lexicon.insert("bright", POS_ADJ);
    g_lexer.lexicon.insert("quick", POS_ADJ);
    g_lexer.lexicon.insert("dangerous", POS_ADJ);
    g_lexer.lexicon.insert("safe", POS_ADJ);
    g_lexer.lexicon.insert("heavy", POS_ADJ);
    g_lexer.lexicon.insert("sharp", POS_ADJ);

    set_color(COL_DIM);
    printf("  [BOOT] Cortex online — %.1f ms\n", sub_ms);
    set_color(COL_RESET);

    bool brain_loaded = false;
    int known_words = 0;
    auto t_br0 = std::chrono::high_resolution_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_brain_io_mutex);
        brain_loaded = SynapticMemory::wake(g_brain_file.c_str(), g_graph, g_cortex);
    }
    auto t_br1 = std::chrono::high_resolution_clock::now();
    double br_ms =
        std::chrono::duration<double, std::milli>(t_br1 - t_br0).count();

    if (brain_loaded) {
        known_words = g_cortex->active_count();
        set_color(COL_DIM);
        printf("  [BOOT] Brain: %s | loaded — %.1f ms | %d words", g_brain_file.c_str(),
               br_ms, known_words);
        if (g_brain_file == "jarvis_brain.fpsan")
            printf("  | legacy file (save → engram_brain.fpsan)");
        printf("\n");
        set_color(COL_RESET);
    } else {
        g_graph->init(6500);
        g_cortex->init();
        auto t_cold0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < Identity::SELF_KNOWLEDGE_COUNT; i++) {
            g_lexer.ingest_sentence(Identity::SELF_KNOWLEDGE[i], g_graph, &g_tokenizer, g_cortex);
        }
        auto t_cold1 = std::chrono::high_resolution_clock::now();
        double cold_ms =
            std::chrono::duration<double, std::milli>(t_cold1 - t_cold0).count();
        set_color(COL_DIM);
        printf("  [BOOT] Brain: cold start — seeded %d facts — %.1f ms\n",
               Identity::SELF_KNOWLEDGE_COUNT, cold_ms);
        set_color(COL_RESET);
    }

    // Phase 11: immortal directives parsed from plaintext seed bundle.
    ingest_core_directives_txt(!brain_loaded, true);
    refresh_translation_cortex();

    // Bind SAN scheduler to the graph after it is initialized/woken.
    g_san.bind_graph(g_graph);
    g_graph->bind_san_poster(
        [](void* user, uint32_t cid, float voltage) noexcept {
            (void)voltage;
            auto* san = reinterpret_cast<fpsan::SANScheduler*>(user);
            san->post_spike(cid, 1.0f);
        },
        &g_san,
        0.85f);

    set_color(COL_DIM);
    printf("  [BOOT] Cognitive clock: %d Hz (target)\n", 1000000 / TICK_INTERVAL_US);
    set_color(COL_RESET);

    // Dynamic boot greeting (with voice). Seed from graph vocabulary — not the
    // operator's name — so public builds do not assume a specific user.
    char greeting_buf[1024] = {0};
    int seed_cid = find_word("Engram");
    if (seed_cid < 0) seed_cid = find_word("Core");
    if (seed_cid >= 0 && g_cortex->clusters[seed_cid].active) {
        g_graph->clear_activation();
        int words = g_lexer.generate_text(seed_cid, g_graph, g_cortex, greeting_buf, 15);
        if (words == 0) {
            snprintf(greeting_buf, sizeof(greeting_buf),
                     "Engram Core is online and ready.");
        }
    } else {
        snprintf(greeting_buf, sizeof(greeting_buf),
                 "Engram Core is online. Teach me a fact to get started.");
    }
    jarvis_say(greeting_buf);

    set_color(COL_DIM);
    printf("  Type /help for commands.\n\n");
    set_color(COL_RESET);

    // Auto-run programmatic commands via CLI flags
    // Usage: engram.exe --train file1.md --train file2.txt --train_dir src/
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--train") == 0 && i + 1 < argc) {
                cmd_train(argv[++i]);
            } else if (strcmp(argv[i], "--train_dir") == 0 && i + 1 < argc) {
                cmd_train_dir(argv[++i]);
            } else {
                // Treat as a raw dispatch command
                dispatch(argv[i]);
            }
        }
        
        // Give async threads time to complete work (research, motor, etc.)
        for (int wait_ticks = 0; wait_ticks < 500; wait_ticks++) {
            Sleep(10);  // ~5 seconds total
            if (!g_research_cortex.research_pending.load() && 
                !g_research_cortex.is_research_busy() &&
                !g_research_cortex.is_research_complete() &&
                g_motor.queue.head.load() == g_motor.queue.tail.load()) {
                break;  // All async work done
            }
        }
        
        cmd_save();
        printf("\nProgrammatic run complete. Exiting.\n");
        g_ears.shutdown();
        g_research_cortex.shutdown();
        g_workspace.shutdown();
        g_voice.destroy();
        g_lexer.destroy();
        delete g_graph;
        delete g_cortex;
        return 0;
    }

    // ── MAIN COGNITIVE LOOP ──
    g_state = STATE_IDLE;
    g_dlg   = DialogueState::LISTENING;
    g_input_pos = 0;

    auto last_save = std::chrono::steady_clock::now();
    auto last_status = std::chrono::steady_clock::now();
    bool prompt_shown = false;

    while (g_running) {
        auto tick_start = std::chrono::high_resolution_clock::now();

        // ── 1. NON-BLOCKING INPUT ──
        if (!prompt_shown) {
            set_color(COL_YELLOW);
            printf("  you > ");
            set_color(COL_RESET);
            prompt_shown = true;
        }

        while (_kbhit()) {
            int c = _getch();

            if (c == '\r' || c == '\n') {
                // Submit line
                g_input_buf[g_input_pos] = '\0';
                printf("\n");
                prompt_shown = false;

                if (g_input_pos > 0) {
                    dispatch(g_input_buf);
                    g_drives.on_user_input(g_tick);  // Phase 16D: AFTER dispatch so !drives shows real values
                }
                g_input_pos = 0;

                if (!g_running) break;
            } else if (c == '\b' || c == 127) {
                // Backspace
                if (g_input_pos > 0) {
                    g_input_pos--;
                    printf("\b \b");
                }
            } else if (c == 27) {
                // Escape — clear line
                while (g_input_pos > 0) {
                    printf("\b \b");
                    g_input_pos--;
                }
            } else if (c >= 32 && c < 127 && g_input_pos < 510) {
                // Normal character
                g_input_buf[g_input_pos++] = (char)c;
                putchar(c);
            }
        }

        if (!g_running) break;

        // Confirmation timeout: if the user does not answer in time, cancel the pending goal.
        if (g_waiting_confirmation.load(std::memory_order_acquire)) {
            ULONGLONG now = GetTickCount64();
            ULONGLONG deadline = g_confirmation_deadline_ms.load(std::memory_order_acquire);
            if (deadline != 0 && now > deadline) {
                g_waiting_confirmation.store(false, std::memory_order_release);
                g_confirmation_deadline_ms.store(0, std::memory_order_release);
                g_drives.boredom = std::min(1.0f, g_drives.boredom + 0.15f);
                g_drives.engagement = std::max(0.0f, g_drives.engagement - 0.10f);
                g_goals.cancel();
                g_confirmation_goal.clear();
                jarvis_say("No confirmation received. Cancelling the research request.");
            }
        }

        // ── 1A. ACOUSTIC INPUT (lock-free queue from Core 6) ──
        if (g_input_pos == 0) {
            AcousticCommand heard;
            int processed = 0;
            while (processed < 3 && g_ears.pop_command(heard)) {
                printf("\n");
                set_color(COL_DIM);
                printf("  [ears] ");
                set_color(COL_RESET);
                printf("%s\n", heard.text);

                if (heard.wake_triggered) {
                    // Acoustic prime: brief global attention spike after wake word.
                    apply_acoustic_prime(0.5f);
                    g_drives.engagement = std::min(1.0f, g_drives.engagement + 0.2f);
                    jarvis_say("Listening.");

                    // Metacognitive confirmation for high-impact commands (research)
                    std::string raw = trim_ws(std::string(heard.text));
                    std::string low = lower_ws(raw);
                    if (low.rfind("research ", 0) == 0) {
                        // Hold for confirmation before executing network fetch
                        g_waiting_confirmation.store(true);
                        g_confirmation_deadline_ms.store(GetTickCount64() + 10000ULL, std::memory_order_release);
                        g_confirmation_goal = raw.substr(9);
                        char buf[1024];
                        snprintf(buf, sizeof(buf), "I've parsed the research request for '%s'. Should I proceed with the network fetch?", g_confirmation_goal.c_str());
                        jarvis_say(buf);
                        processed++;
                        continue; // wait for confirmation
                    }
                }

                // If we are waiting for confirmation, interpret yes/no replies
                if (g_waiting_confirmation.load()) {
                    std::string resp = lower_ws(std::string(heard.text));
                    bool confirmed = false;
                    bool cancelled = false;
                    if (resp.find("yes") != std::string::npos || resp.find("proceed") != std::string::npos || resp.find("do it") != std::string::npos || resp.find("go ahead") != std::string::npos) confirmed = true;
                    if (resp.find("no") != std::string::npos || resp.find("cancel") != std::string::npos || resp.find("don't") != std::string::npos) cancelled = true;
                    if (confirmed) {
                        char buf[512]; snprintf(buf, sizeof(buf), "Confirmed. Executing research for '%s'.", g_confirmation_goal.c_str()); jarvis_say(buf);
                        // Execute the research pipeline (ingest + net run)
                        cmd_research(g_confirmation_goal.c_str());
                        cmd_research_run("net");
                        g_waiting_confirmation.store(false);
                        g_confirmation_deadline_ms.store(0, std::memory_order_release);
                        g_confirmation_goal.clear();
                        processed++;
                        continue;
                    } else if (cancelled) {
                        jarvis_say("Cancelled.");
                        g_waiting_confirmation.store(false);
                        g_confirmation_deadline_ms.store(0, std::memory_order_release);
                        g_confirmation_goal.clear();
                        processed++;
                        continue;
                    }
                    // If not a clear yes/no, fallthrough to normal dispatch
                }

                dispatch_acoustic_command(heard.text);
                g_drives.on_user_input(g_tick);
                prompt_shown = false;
                processed++;
            }
        }

        // ── 2. COGNITIVE TICK (Physics) ──
        g_graph->tick(TICK_DECAY_RATE);
        g_tick++;

        fpsan::neuromod_sync_arousal(g_drives.frustration, g_drives.doubt);

        // Phase 10: SAN drain (bounded) — event-driven spike propagation.
        // Uses a coarse clock (GetTickCount64) to avoid high-frequency QPC overhead.
        {
            const uint64_t now_ns = (uint64_t)GetTickCount64() * 1000000ull;
            (void)g_san.drain_until(now_ns, 250000ull); // 0.25ms budget per 1ms tick
        }

        // ── 2B. VISUAL TICK (10 Hz = every 100 cognitive ticks at 1kHz) ──
        if (g_tick % 100 == 0) {
            g_vision.tick();

            // Visual Error Detection (Phase 18B Lock-Free Flush trigger)
            if (g_goals.is_active() && g_target_window[0] != '\0') {
                g_vision.proprioception.tick();
                bool is_target = g_vision.verify_target(g_target_window);
                if (is_target) {
                    g_target_window_verified = true;
                } else if (g_target_window_verified) {
                    // It WAS verified, but now it's lost! (e.g. user closed it mid-typing)
                    set_color(12); // Red
                    printf("\n  [Visual Error] Target window '%s' lost!\n", g_target_window);
                    set_color(COL_RESET);
                    g_motor.abort_sequence(); // Lock-free epoch flush!
                    g_goals.cancel();
                    jarvis_say("Target window lost. Aborting motor sequence.");
                    g_target_window[0] = '\0';
                    g_target_window_verified = false;
                }
            }
        }

        // Measure brain activity
        g_max_voltage = 0.0f;
        g_active_nodes = 0;
        // Only sample a subset for performance (every 100th tick)
        if (g_tick % 100 == 0) {
            const int nc_m = g_graph->node_count.load(std::memory_order_acquire);
            for (int i = 0; i < nc_m; i++) {
                if (!g_graph->node(i).alive.load(std::memory_order_acquire)) continue;
                float act = g_graph->node(i).activation.load(std::memory_order_relaxed);
                if (act > ACTIVE_THRESHOLD) {
                    g_active_nodes++;
                    if (act > g_max_voltage) g_max_voltage = act;
                }
            }
        }

        // Bug 3: Poll TTS completion — flip SPEAKING → LISTENING when voice finishes.
        if (g_dlg == DialogueState::SPEAKING && !g_voice.is_speaking()) {
            g_dlg = DialogueState::LISTENING;
            if (g_state == STATE_SPEAKING) g_state = STATE_IDLE;
        }

        // Update brain state
        if (g_max_voltage > ACTIVE_THRESHOLD) {
            if (g_state != STATE_SPEAKING && g_state != STATE_SLEEPING)
                g_state = STATE_THINKING;
        } else {
            if (g_state == STATE_THINKING)
                g_state = STATE_IDLE;
        }

        // ── 3. AUTO-SAVE (opt-in: AUTO_SAVE_SECONDS > 0) ──
        if (g_auto_save_seconds > 0) {
            auto now = std::chrono::steady_clock::now();
            double since_save = std::chrono::duration<double>(now - last_save).count();
            if (since_save >= static_cast<double>(g_auto_save_seconds) && g_input_pos == 0) {
                if (g_cortex->active_count() > 0) {
                    {
                        std::lock_guard<std::mutex> lock(g_brain_io_mutex);
                        SynapticMemory::sleep(g_brain_file.c_str(), g_graph, g_cortex);
                    }
                    set_color(COL_DIM);
                    printf("  [auto-save complete]\n");
                    set_color(COL_RESET);
                    prompt_shown = false;
                    last_save = now;
                }
            }
        }

        // ── 4. TICK THROTTLE ──
        // Sleep to maintain ~1kHz without burning CPU
        auto tick_end = std::chrono::high_resolution_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(tick_end - tick_start).count();
        if (elapsed_us > 0) {
            uint64_t prev = g_worst_tick_us.load(std::memory_order_relaxed);
            const uint64_t u = static_cast<uint64_t>(elapsed_us);
            while (u > prev && !g_worst_tick_us.compare_exchange_weak(prev, u, std::memory_order_relaxed)) {
            }
        }
        if (elapsed_us < TICK_INTERVAL_US) {
            Sleep(1); // ~1ms granularity on Windows
        }

        // ── 5. HOMEOSTATIC DRIVES (The Living Loop) ──
        g_drives.tick(g_tick);

        // ── 5B. MOTOR FOCUS-LOSS → FRUSTRATION ──
        // g_motor_focus_aborts is incremented by type_string() whenever the
        // target window loses focus mid-sequence. Poll it here and convert
        // each abort into a frustration spike, then spike the graph node.
        {
            static int last_abort_count = 0;
            int cur = g_motor_focus_aborts.load(std::memory_order_relaxed);
            if (cur != last_abort_count) {
                int new_aborts = cur - last_abort_count;
                last_abort_count = cur;
                g_drives.frustration = std::min(1.0f, g_drives.frustration + 0.3f * new_aborts);
                // Spike any language cluster labelled "frustration" if it exists.
                if (g_cortex) {
                    for (int ci = 0; ci < LANG_CLUSTERS; ++ci) {
                        if (!g_cortex->clusters[ci].active) continue;
                        if (std::strncmp(g_cortex->clusters[ci].word_label,
                                         "frustration", 11) == 0) {
                            g_graph->node(ci).add_voltage(0.5f);
                            break;
                        }
                    }
                }
            }
        }

        // ── 5A-RESEARCH. Cognitive Handoff: Research Completion Detection ──
        // Non-blocking check for async research completion
        if (g_research_cortex.is_research_complete() && g_input_pos == 0) {
            std::string summary = g_research_cortex.get_summary();
            std::string msg = g_research_cortex.consume_result();
            
            if (!summary.empty()) {
                printf("\n");
                set_color(COL_DIM);
                printf("  [research] ");
                set_color(COL_RESET);
                jarvis_say(summary.c_str());
                
                // Bug 1 fix: the actual ingestion now happens inside
                // ResearchCortex::fetch_and_ingest_source() — every clean
                // sentence from Wikipedia is fed through ingest_sentence()
                // before this point.  Here we just notify the user with the
                // real count and feed the curiosity drive accordingly.
                int just_learned =
                    g_research_cortex.last_facts_ingested.load(std::memory_order_acquire);
                if (just_learned > 0) {
                    char nbuf[160];
                    snprintf(nbuf, sizeof(nbuf),
                             "Learned %d facts about %s.",
                             just_learned, g_research_cortex.last_goal.c_str());
                    set_color(COL_DIM);
                    printf("  [research] %s\n", nbuf);
                    set_color(COL_RESET);
                    g_drives.on_knowledge_ingested(just_learned);
                }
                // Phase 13 — research artefacts stay on disk only; explicit `!motor`/`!present`
                // tooling can open Notepad later (no unsolicited typing).
                
                g_drives.on_spoke(g_tick);
                prompt_shown = false;
            }
        }

        // ── 5B. Visual reactivity: Comment on window switches ──
        if (g_vision.proprioception.title_changed && g_tick > 5000) {
            const char* title = g_vision.proprioception.foreground_title;
            if (title[0] != '\0' && g_input_pos == 0) {
                g_drives.on_visual_change(g_tick);
                // Only comment if enough time has passed
                if ((g_tick - g_drives.last_spoke_tick) > HomeostaticDrives::SPEAK_COOLDOWN) {
                    printf("\n");
                    set_color(COL_DIM);
                    printf("  [observing] ");
                    set_color(COL_RESET);
                    char msg[256];
                    snprintf(msg, sizeof(msg), "I see you switched to \"%s\".", title);
                    jarvis_say(msg);
                    g_drives.on_spoke(g_tick);
                    prompt_shown = false;
                }
            }
        }

        // ── 5B. Curiosity: Ask about unknown words ──
        if (g_drives.should_ask_question(g_tick) && g_input_pos == 0) {
            const char* word = g_drives.pop_question();
            if (word) {
                printf("\n");
                set_color(14); // Yellow
                printf("  [curious] ");
                set_color(COL_RESET);
                char msg[256];
                snprintf(msg, sizeof(msg), "You mentioned \"%s\" earlier but I don't know what it means. Can you teach me?", word);
                jarvis_say(msg);
                g_drives.on_spoke(g_tick);
                prompt_shown = false;
            }
        }

        // ── 5C. Boredom: Spontaneous recall OR Autonomous Research ──
        if (g_drives.should_spontaneous_speak(g_tick) && g_input_pos == 0) {
            int active_count = g_cortex->active_count();
            
            // ── SOVEREIGN AUTONOMY: If very bored and no research active, ──
            // ── pick a random known concept and auto-research it.         ──
            if (g_drives.boredom > 0.8f && active_count > 5 &&
                !g_research_cortex.is_research_busy() &&
                !g_research_cortex.is_research_complete() &&
                g_research_cortex.agent.remaining() == 0) {
                
                // Find a random content word (noun) to research
                int target = rand() % LANG_CLUSTERS;
                for (int attempt = 0; attempt < LANG_CLUSTERS; attempt++) {
                    int idx = (target + attempt) % LANG_CLUSTERS;
                    if (g_cortex->clusters[idx].active && g_cortex->clusters[idx].word_label[0] != '\0') {
                        const char* word = g_cortex->clusters[idx].word_label;
                        // Skip very short words and function words
                        if (strlen(word) < 4) continue;
                        POSTag tag = g_lexer.lexicon.lookup(word);
                        if (tag != POS_NOUN && tag != POS_ADJ && tag != POS_UNKNOWN) continue;
                        
                        printf("\n");
                        set_color(14); // Yellow
                        printf("  [autonomous] ");
                        set_color(COL_RESET);
                        char msg[256];
                        snprintf(msg, sizeof(msg), "I am curious about \"%s\". Let me research it.", word);
                        jarvis_say(msg);
                        
                        cmd_research(word);
                        cmd_research_run("net");
                        
                        g_drives.boredom = 0.2f;  // Reset boredom after taking action
                        g_drives.curiosity = std::min(1.0f, g_drives.curiosity + 0.3f);
                        g_drives.engagement = std::min(1.0f, g_drives.engagement + 0.3f);
                        g_drives.on_spoke(g_tick);
                        prompt_shown = false;
                        break;
                    }
                }
            }
            // ── Normal musing (lower boredom) ──
            else if (active_count > 0) {
                int target = rand() % LANG_CLUSTERS;
                for (int attempt = 0; attempt < LANG_CLUSTERS; attempt++) {
                    int idx = (target + attempt) % LANG_CLUSTERS;
                    if (g_cortex->clusters[idx].active && g_cortex->clusters[idx].word_label[0] != '\0') {
                        char buf[1024];
                        g_graph->clear_activation();
                        int words = g_lexer.generate_text(idx, g_graph, g_cortex, buf, 10);
                        if (words > 0) {
                            printf("\n");
                            set_color(COL_DIM);
                            printf("  [musing] ");
                            set_color(COL_RESET);
                            char msg[256];
                            snprintf(msg, sizeof(msg), "I was just thinking... %s", buf);
                            jarvis_say(msg);
                            g_drives.on_spoke(g_tick);
                            prompt_shown = false;
                            break;
                        }
                        break;
                    }
                }
            }
        }

        // ── 5D. Goal Pursuit: Execute next step if goal is active ──
        if (g_goals.is_active() && g_input_pos == 0 && g_motor.is_idle() &&
            (g_tick - g_drives.last_goal_tick) > 200) { // 200ms between goal steps

            // Wait until window is visually confirmed before executing next step
            if (g_target_window[0] != '\0' && !g_target_window_verified) {
                if (g_tick - g_drives.last_goal_tick > 5000) { // 5 sec timeout
                    set_color(12); // Red
                    printf("\n  [Visual Error] Target window '%s' never appeared!\n", g_target_window);
                    set_color(COL_RESET);
                    g_motor.abort_sequence();
                    g_goals.cancel();
                    jarvis_say("Target window failed to appear.");
                    g_target_window[0] = '\0';
                    g_target_window_verified = false;
                }
                continue; // Wait for visual confirmation
            }

            const char* action = g_goals.next_action();
            if (action) {
                printf("\n");
                set_color(10); // Green
                printf("  [goal] ");
                set_color(COL_RESET);
                printf("Executing: %s\n", action);

                bool skip_goal_advance = false;

                // Route action to appropriate handler
                // Phase 19: All goals resolve through semantic motor bindings.
                // The only action type is motor_concept_N, where N is a cluster ID
                // with EDGE_IMPLEMENTED_BY bonds leading to MotorNode sequences.
                if (strncmp(action, "motor_concept_", 14) == 0) {
                    int target_id = atoi(action + 14);
                    bool executed = false;
                    bool shadow_veto = false;

                    int motor_chain[SemanticPlanner::MAX_MOTOR_CHAIN];
                    const int chain_len =
                        collect_goal_motor_chain_flat(g_graph, target_id, motor_chain,
                                                        SemanticPlanner::MAX_MOTOR_CHAIN);
                    if (chain_len > 0) {
                        const auto t_sv0 = std::chrono::high_resolution_clock::now();
                        bool blocked = false;
                        const char* veto_detail = "motor plan blocked";

                        if (motor_chain_policy_blocks_disk_ops(g_graph, motor_chain, chain_len)) {
                            blocked = true;
                            veto_detail = "disk/OS safety policy (format, diskpart, cipher /w, etc.)";
                        } else {
                            ShadowBrain sb;
                            sb.mirror(g_graph);
                            if (!sb.is_safe(target_id, motor_chain, chain_len, g_graph)) {
                                blocked = true;
                                veto_detail = "EDGE_REQUIRES precondition not met";
                            }
                        }
                        const auto t_sv1 = std::chrono::high_resolution_clock::now();
                        const uint64_t veto_us = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(t_sv1 - t_sv0).count());
                        uint64_t prev_sv = g_worst_shadow_veto_check_us.load(std::memory_order_relaxed);
                        while (veto_us > prev_sv &&
                               !g_worst_shadow_veto_check_us.compare_exchange_weak(
                                   prev_sv, veto_us, std::memory_order_relaxed)) {
                        }

                        if (blocked) {
                            shadow_veto = true;
                            skip_goal_advance = true;
                            set_color(12);
                            printf("  [ShadowBrain] VETO (check took %llu us) — %s.\n",
                                   (unsigned long long)veto_us, veto_detail);
                            set_color(COL_RESET);
                            jarvis_say("ShadowBrain veto. Unsafe motor plan blocked.");
                            g_goals.cancel();
                            g_drives.on_goal_failed();
                        }
                    }

                    if (!shadow_veto) {
                        int tgt_ec = g_graph->node(target_id).edge_count.load(std::memory_order_acquire);
                        for (int i = 0; i < tgt_ec; i++) {
                            if (g_graph->node(target_id).edges[i].type == EDGE_IMPLEMENTED_BY) {
                                int m_id = g_graph->node(target_id).edges[i].target;

                                int current_m_id = m_id;
                                while (current_m_id >= 0 &&
                                       g_graph->node(current_m_id).is_motor_node.load(
                                           std::memory_order_acquire)) {
                                    g_motor.queue_action(g_graph->node(current_m_id).motor_action);

                                    int next_m_id = -1;
                                    int cm_ec =
                                        g_graph->node(current_m_id).edge_count.load(std::memory_order_acquire);
                                    for (int j = 0; j < cm_ec; j++) {
                                        if (g_graph->node(current_m_id).edges[j].type == EDGE_SEQUENCE) {
                                            next_m_id = g_graph->node(current_m_id).edges[j].target;
                                            break;
                                        }
                                    }
                                    current_m_id = next_m_id;
                                }
                                executed = true;
                            }
                        }
                        if (executed) {
                            jarvis_say("Executing learned procedure.");
                        } else {
                            jarvis_say("Motor binding found but no executable sequence.");
                            g_drives.on_goal_failed();
                        }
                    }
                } else {
                    // Unknown action type — should not happen in Phase 19
                    char err[256];
                    snprintf(err, sizeof(err), "Unknown action type: %s", action);
                    jarvis_say(err);
                    g_drives.on_goal_failed();
                }

                if (!skip_goal_advance) {
                    g_goals.complete_step();
                    g_drives.last_goal_tick = g_tick; // Independent goal timer
                    g_drives.on_spoke(g_tick);
                    prompt_shown = false;

                    if (!g_goals.is_active()) {
                        set_color(10);
                        printf("  [goal] ");
                        set_color(COL_RESET);
                        jarvis_say("Goal complete!");
                        g_drives.engagement += 0.2f;
                    }
                }
            }
        }
    }

    // ── SHUTDOWN ──
    g_voice.speak_sync("Going offline. Goodbye.");
    g_workspace.shutdown();        // Main desktop mode: no hidden desktop to destroy
    g_ears.shutdown();
    g_research_cortex.shutdown();  // Shut down async research thread
    set_color(COL_YELLOW);
    printf("\n  [SHUTDOWN] ");
    set_color(COL_RESET);
    printf("Engram Core going offline.\n\n");

    g_voice.destroy();
    g_lexer.destroy();
    delete g_graph;
    delete g_cortex;

    return 0;
}
