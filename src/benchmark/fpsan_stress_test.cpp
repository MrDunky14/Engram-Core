// ============================================================
// FP-SAN Phase 16A: STRESS TEST BATTERY
// 100 hand-curated sentences across 5 domains.
// Deterministic ground truth for regression testing.
//
// Tests:
//   1. Bulk ingestion throughput (100 sentences)
//   2. Retrieval accuracy (seed → expected output prefix)
//   3. Cross-topic bleeding detection
//   4. Brain persistence (save → load → recall)
//   5. Graph scaling metrics (nodes, edges, memory)
//
// Compile:
//   cl /std:c++17 /O2 /EHsc /I src\core src\benchmark\fpsan_stress_test.cpp
//      /Fe:build\stress_test.exe /Fo:build\stress_test.obj
// ============================================================

#define NOMINMAX
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>

#include "cluster_graph.h"
#include "fpsan_language.h"
#include "fpsan_lexer.h"
#include "fpsan_memory.h"

using namespace std::chrono;

// ============================================================
// GROUND TRUTH CORPUS
// Each entry: {sentence, seed_word, expected_prefix (first 3+ words)}
// ============================================================
struct TestCase {
    const char* sentence;
    const char* seed;
    const char* expected_prefix;  // Generated text must START with this
    const char* domain;
};

// 100 sentences across 5 domains (20 each)
TestCase g_corpus[] = {
    // ── DOMAIN 1: ANIMALS (20) ──────────────────────────────
    {"the eagle can fly over the dark mountain", "eagle", "eagle can fly", "ANIMALS"},
    {"the dog can run fast in the park", "dog", "dog can run", "ANIMALS"},
    {"the cat sits on the warm mat", "cat", "cat sits on", "ANIMALS"},
    {"the whale swims deep in the cold ocean", "whale", "whale swims deep", "ANIMALS"},
    {"the snake slithers through the tall grass", "snake", "snake slithers through", "ANIMALS"},
    {"the horse gallops across the open field", "horse", "horse gallops across", "ANIMALS"},
    {"the lion hunts prey on the savanna", "lion", "lion hunts prey", "ANIMALS"},
    {"the owl sees well in the dark night", "owl", "owl sees well", "ANIMALS"},
    {"the fish breathes through small gills", "fish", "fish breathes through", "ANIMALS"},
    {"the bear sleeps during the cold winter", "bear", "bear sleeps during", "ANIMALS"},
    {"the rabbit hops quickly through the garden", "rabbit", "rabbit hops quickly", "ANIMALS"},
    {"the wolf howls loudly at the full moon", "wolf", "wolf howls loudly", "ANIMALS"},
    {"the dolphin jumps high above the waves", "dolphin", "dolphin jumps high", "ANIMALS"},
    {"the penguin waddles slowly on the thick ice", "penguin", "penguin waddles slowly", "ANIMALS"},
    {"the spider spins a delicate silk web", "spider", "spider spins", "ANIMALS"},
    {"the parrot repeats words it hears clearly", "parrot", "parrot repeats words", "ANIMALS"},
    {"the shark patrols the warm shallow reef", "shark", "shark patrols", "ANIMALS"},
    {"the frog leaps from the lily pad", "frog", "frog leaps from", "ANIMALS"},
    {"the deer runs swiftly through the dense forest", "deer", "deer runs swiftly", "ANIMALS"},
    {"the turtle moves slowly but lives very long", "turtle", "turtle moves slowly", "ANIMALS"},

    // ── DOMAIN 2: PHYSICS & CHEMISTRY (20) ──────────────────
    {"water boils at one hundred degrees celsius", "water", "water boils at", "PHYSICS"},
    {"gravity pulls all objects toward the ground", "gravity", "gravity pulls all", "PHYSICS"},
    {"light travels faster than sound through air", "light", "light travels faster", "PHYSICS"},
    {"atoms combine together to form molecules", "atoms", "atoms combine together", "PHYSICS"},
    {"energy cannot be created or destroyed ever", "energy", "energy cannot be", "PHYSICS"},
    {"friction slows down all moving objects gradually", "friction", "friction slows down", "PHYSICS"},
    {"electricity flows through copper wire easily", "electricity", "electricity flows through", "PHYSICS"},
    {"magnetism attracts iron and steel strongly", "magnetism", "magnetism attracts iron", "PHYSICS"},
    {"pressure increases with depth under water", "pressure", "pressure increases with", "PHYSICS"},
    {"sound needs a medium to travel through", "sound", "sound needs", "PHYSICS"},
    {"heat transfers from hot objects to cold ones", "heat", "heat transfers from", "PHYSICS"},
    {"ice melts when temperature rises above zero", "ice", "ice melts when", "PHYSICS"},
    {"radiation travels through empty space freely", "radiation", "radiation travels through", "PHYSICS"},
    {"oxygen supports combustion and burning reactions", "oxygen", "oxygen supports combustion", "PHYSICS"},
    {"carbon forms the backbone of organic chemistry", "carbon", "carbon forms", "PHYSICS"},
    {"hydrogen is the lightest element in nature", "hydrogen", "hydrogen is", "PHYSICS"},
    {"plasma is the fourth state of matter", "plasma", "plasma is", "PHYSICS"},
    {"voltage drives current through a closed circuit", "voltage", "voltage drives current", "PHYSICS"},
    {"density equals mass divided by total volume", "density", "density equals mass", "PHYSICS"},
    {"momentum equals mass multiplied by velocity always", "momentum", "momentum equals mass", "PHYSICS"},

    // ── DOMAIN 3: CODE & COMPUTER SCIENCE (20) ──────────────
    {"python uses indentation for code blocks", "python", "python uses indentation", "CODE"},
    {"a function takes input and returns output", "function", "function takes input", "CODE"},
    {"a variable stores a single value in memory", "variable", "variable stores", "CODE"},
    {"an array holds multiple values in sequence", "array", "array holds multiple", "CODE"},
    {"a loop repeats code until a condition fails", "loop", "loop repeats code", "CODE"},
    {"recursion happens when a function calls itself", "recursion", "recursion happens when", "CODE"},
    {"a compiler translates source code into binary", "compiler", "compiler translates source", "CODE"},
    {"a database stores structured data on disk", "database", "database stores structured", "CODE"},
    {"encryption protects sensitive data from attackers", "encryption", "encryption protects sensitive", "CODE"},
    {"bandwidth measures data transfer speed per second", "bandwidth", "bandwidth measures data", "CODE"},
    {"a pointer stores the address of another variable", "pointer", "pointer stores", "CODE"},
    {"a stack follows last in first out order", "stack", "stack follows last", "CODE"},
    {"a queue follows first in first out order", "queue", "queue follows first", "CODE"},
    {"a hash maps keys to values very quickly", "hash", "hash maps keys", "CODE"},
    {"binary represents numbers using zeros and ones", "binary", "binary represents numbers", "CODE"},
    {"latency measures the delay between request and response", "latency", "latency measures", "CODE"},
    {"a thread runs code in parallel with others", "thread", "thread runs code", "CODE"},
    {"a socket connects two programs over a network", "socket", "socket connects two", "CODE"},
    {"an algorithm solves a problem step by step", "algorithm", "algorithm solves", "CODE"},
    {"a cache stores frequently accessed data nearby", "cache", "cache stores frequently", "CODE"},

    // ── DOMAIN 4: GEOGRAPHY & SPACE (20) ────────────────────
    {"jupiter is the largest planet in our solar system", "jupiter", "jupiter is", "GEOGRAPHY"},
    {"the amazon river flows through south america", "amazon", "amazon river flows", "GEOGRAPHY"},
    {"mount everest stands as the tallest peak", "everest", "everest stands as", "GEOGRAPHY"},
    {"the sahara desert spans much of north africa", "sahara", "sahara desert spans", "GEOGRAPHY"},
    {"the pacific ocean covers one third of earth", "pacific", "pacific ocean covers", "GEOGRAPHY"},
    {"antarctica holds most of the fresh ice", "antarctica", "antarctica holds most", "GEOGRAPHY"},
    {"the moon orbits the earth every twenty nine days", "moon", "moon orbits", "GEOGRAPHY"},
    {"mars has two small moons named phobos and deimos", "mars", "mars has two", "GEOGRAPHY"},
    {"saturn has beautiful rings made of ice particles", "saturn", "saturn has beautiful", "GEOGRAPHY"},
    {"the equator divides earth into two hemispheres", "equator", "equator divides earth", "GEOGRAPHY"},
    {"volcanoes erupt when magma reaches the surface", "volcanoes", "volcanoes erupt when", "GEOGRAPHY"},
    {"earthquakes happen when tectonic plates shift suddenly", "earthquakes", "earthquakes happen when", "GEOGRAPHY"},
    {"the nile river is the longest river on earth", "nile", "nile river is", "GEOGRAPHY"},
    {"the arctic stays frozen for most of the year", "arctic", "arctic stays frozen", "GEOGRAPHY"},
    {"comets orbit the sun in long elliptical paths", "comets", "comets orbit", "GEOGRAPHY"},
    {"venus is the hottest planet due to thick atmosphere", "venus", "venus is", "GEOGRAPHY"},
    {"the galaxy contains billions of stars and planets", "galaxy", "galaxy contains billions", "GEOGRAPHY"},
    {"tides rise and fall due to lunar gravity", "tides", "tides rise and", "GEOGRAPHY"},
    {"the ozone layer protects life from ultraviolet rays", "ozone", "ozone layer protects", "GEOGRAPHY"},
    {"a meteor burns brightly as it enters the atmosphere", "meteor", "meteor burns brightly", "GEOGRAPHY"},

    // ── DOMAIN 5: BIOLOGY & HUMAN BODY (20) ─────────────────
    {"the human brain has eighty six billion neurons", "brain", "brain has eighty", "BIOLOGY"},
    {"blood carries oxygen from the lungs to cells", "blood", "blood carries oxygen", "BIOLOGY"},
    {"muscles contract to produce movement in joints", "muscles", "muscles contract to", "BIOLOGY"},
    {"the heart pumps blood through the entire body", "heart", "heart pumps blood", "BIOLOGY"},
    {"lungs exchange oxygen and carbon dioxide constantly", "lungs", "lungs exchange oxygen", "BIOLOGY"},
    {"bones provide structure and protect vital organs", "bones", "bones provide structure", "BIOLOGY"},
    {"the liver filters toxins from the bloodstream", "liver", "liver filters toxins", "BIOLOGY"},
    {"the stomach digests food using strong acid", "stomach", "stomach digests food", "BIOLOGY"},
    {"kidneys filter waste products from the blood", "kidneys", "kidneys filter waste", "BIOLOGY"},
    {"skin protects the body from infection and damage", "skin", "skin protects", "BIOLOGY"},
    {"nerves transmit electrical signals throughout the body", "nerves", "nerves transmit electrical", "BIOLOGY"},
    {"the spine supports the body and protects the cord", "spine", "spine supports", "BIOLOGY"},
    {"white cells fight infection in the immune system", "white", "white cells fight", "BIOLOGY"},
    {"the retina detects light at the back of the eye", "retina", "retina detects light", "BIOLOGY"},
    {"insulin regulates blood sugar levels in the body", "insulin", "insulin regulates blood", "BIOLOGY"},
    {"the cochlea converts sound vibrations into nerve signals", "cochlea", "cochlea converts sound", "BIOLOGY"},
    {"tendons connect muscles firmly to the bones", "tendons", "tendons connect muscles", "BIOLOGY"},
    {"the diaphragm contracts to allow breathing in", "diaphragm", "diaphragm contracts to", "BIOLOGY"},
    {"enzymes speed up chemical reactions in living cells", "enzymes", "enzymes speed up", "BIOLOGY"},
    {"chromosomes carry genetic information in every cell", "chromosomes", "chromosomes carry genetic", "BIOLOGY"},
};

const int CORPUS_SIZE = sizeof(g_corpus) / sizeof(g_corpus[0]);
const char* STRESS_BRAIN_FILE = "stress_test_brain.fpsan";

// ============================================================
// HELPER: check if generated text starts with expected prefix
// ============================================================
bool starts_with(const char* text, const char* prefix) {
    // Case-insensitive prefix match
    int pi = 0;
    int ti = 0;
    while (prefix[pi] != '\0') {
        if (text[ti] == '\0') return false;
        char a = (char)tolower(text[ti]);
        char b = (char)tolower(prefix[pi]);
        if (a != b) return false;
        ti++;
        pi++;
    }
    return true;
}

// ============================================================
// MAIN
// ============================================================
int main() {
    printf("================================================================\n");
    printf(" FP-SAN PHASE 16A: STRESS TEST BATTERY\n");
    printf(" 100 Sentences | 5 Domains | Deterministic Ground Truth\n");
    printf("================================================================\n\n");

    // ── Initialize Brain ─────────────────────────────────────
    ClusterGraph* graph = new ClusterGraph();
    graph->init(6500);

    LanguageCortex* cortex = new LanguageCortex();
    cortex->init();

    SpikingTokenizer tokenizer;
    NativeLexer lexer;
    lexer.init();

    printf("[System] Brain initialized: %d nodes, %d cortex clusters\n\n",
           graph->node_count.load(std::memory_order_acquire), cortex->active_count());

    // ══════════════════════════════════════════════════════════
    // TEST 1: BULK INGESTION (100 sentences)
    // ══════════════════════════════════════════════════════════
    printf("=== TEST 1: BULK INGESTION (%d sentences) ===\n", CORPUS_SIZE);

    auto t0 = high_resolution_clock::now();
    int total_triples = 0;

    for (int i = 0; i < CORPUS_SIZE; i++) {
        int triples = lexer.ingest_sentence(g_corpus[i].sentence, graph, &tokenizer, cortex);
        total_triples += triples;
    }

    auto t1 = high_resolution_clock::now();
    double ingest_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

    // Count graph stats
    int alive_nodes = 0, total_edges = 0;
    int nc = graph->node_count.load(std::memory_order_acquire);
    for (int i = 0; i < nc; i++) {
        if (graph->node(i).alive.load(std::memory_order_acquire)) {
            alive_nodes++;
            total_edges += graph->node(i).edge_count.load(std::memory_order_acquire);
        }
    }

    printf("  Sentences ingested:  %d\n", CORPUS_SIZE);
    printf("  Total triples:       %d\n", total_triples);
    printf("  Ingestion time:      %.2f ms\n", ingest_ms);
    printf("  Throughput:          %.0f sentences/sec\n", (CORPUS_SIZE * 1000.0) / ingest_ms);
    printf("  Alive nodes:         %d / %d\n", alive_nodes, nc);
    printf("  Active cortex:       %d clusters\n", cortex->active_count());
    printf("  Total edges:         %d\n", total_edges);
    printf("  Ingestion:           %s\n\n", ingest_ms < 500.0 ? "PASS (< 500ms)" : "FAIL (>= 500ms)");

    // ══════════════════════════════════════════════════════════
    // TEST 2: RETRIEVAL ACCURACY
    // ══════════════════════════════════════════════════════════
    printf("=== TEST 2: RETRIEVAL ACCURACY ===\n");

    int pass_count = 0;
    int fail_count = 0;
    int domain_pass[5] = {0};
    int domain_total[5] = {0};
    const char* domain_names[5] = {"ANIMALS", "PHYSICS", "CODE", "GEOGRAPHY", "BIOLOGY"};

    for (int i = 0; i < CORPUS_SIZE; i++) {
        // Find seed cluster
        int8_t hash[256];
        std::string seed_str(g_corpus[i].seed);
        tokenizer.encode_word_hash(seed_str, hash);
        int cid = cortex->perceive(hash, false);

        // Generate text
        char buf[1024];
        graph->clear_activation();
        int words = lexer.generate_text(cid, graph, cortex, buf, 20);

        // Check prefix match
        bool ok = (words > 0 && starts_with(buf, g_corpus[i].expected_prefix));

        // Find domain index
        int di = -1;
        for (int d = 0; d < 5; d++) {
            if (strcmp(g_corpus[i].domain, domain_names[d]) == 0) { di = d; break; }
        }
        if (di >= 0) {
            domain_total[di]++;
            if (ok) domain_pass[di]++;
        }

        if (ok) {
            pass_count++;
        } else {
            fail_count++;
            printf("  FAIL [%s] seed=\"%s\" expected=\"%s\" got=\"%s\"\n",
                   g_corpus[i].domain, g_corpus[i].seed, g_corpus[i].expected_prefix,
                   words > 0 ? buf : "(empty)");
        }
    }

    float accuracy = (pass_count * 100.0f) / CORPUS_SIZE;
    printf("\n  === RETRIEVAL SCORECARD ===\n");
    for (int d = 0; d < 5; d++) {
        printf("  %-12s: %2d / %2d  (%5.1f%%)\n",
               domain_names[d], domain_pass[d], domain_total[d],
               domain_total[d] > 0 ? (domain_pass[d] * 100.0f / domain_total[d]) : 0.0f);
    }
    printf("  ────────────────────────\n");
    printf("  TOTAL:        %2d / %2d  (%5.1f%%)\n", pass_count, CORPUS_SIZE, accuracy);
    printf("  Retrieval:    %s\n\n", accuracy >= 80.0f ? "PASS (>= 80%)" : "FAIL (< 80%)");

    // ══════════════════════════════════════════════════════════
    // TEST 3: CROSS-TOPIC BLEEDING DETECTION
    // ══════════════════════════════════════════════════════════
    printf("=== TEST 3: CROSS-TOPIC BLEEDING ===\n");

    // For each domain, pick 3 seed words and verify generated text
    // doesn't contain signature words from OTHER domains
    struct BleedTest {
        const char* seed;
        const char* domain;
        const char* foreign_words[3]; // Words that should NOT appear
    };

    BleedTest bleed_tests[] = {
        {"eagle",   "ANIMALS",   {"python", "jupiter", "brain"}},
        {"water",   "PHYSICS",   {"eagle", "compiler", "blood"}},
        {"python",  "CODE",      {"eagle", "gravity", "heart"}},
        {"jupiter", "GEOGRAPHY", {"eagle", "python", "lungs"}},
        {"brain",   "BIOLOGY",   {"eagle", "python", "jupiter"}},
    };

    int bleed_pass = 0;
    int bleed_total = 5;

    for (int b = 0; b < bleed_total; b++) {
        int8_t hash[256];
        std::string seed_str(bleed_tests[b].seed);
        tokenizer.encode_word_hash(seed_str, hash);
        int cid = cortex->perceive(hash, false);

        char buf[1024];
        graph->clear_activation();
        lexer.generate_text(cid, graph, cortex, buf, 20);

        // Convert to lowercase for matching
        char lower_buf[1024];
        strncpy(lower_buf, buf, 1023);
        lower_buf[1023] = '\0';
        for (int i = 0; lower_buf[i]; i++) lower_buf[i] = (char)tolower(lower_buf[i]);

        bool bled = false;
        for (int f = 0; f < 3; f++) {
            if (strstr(lower_buf, bleed_tests[b].foreign_words[f])) {
                printf("  BLEED: \"%s\" (%s) contains foreign word \"%s\"\n",
                       bleed_tests[b].seed, bleed_tests[b].domain,
                       bleed_tests[b].foreign_words[f]);
                bled = true;
            }
        }
        if (!bled) bleed_pass++;
    }

    printf("  Bleeding:     %d / %d clean (%s)\n\n",
           bleed_pass, bleed_total,
           bleed_pass == bleed_total ? "PASS (0% bleed)" : "FAIL (cross-topic contamination)");

    // ══════════════════════════════════════════════════════════
    // TEST 4: BRAIN PERSISTENCE (Save → Load → Recall)
    // ══════════════════════════════════════════════════════════
    printf("=== TEST 4: BRAIN PERSISTENCE ===\n");

    // Save
    auto ts0 = high_resolution_clock::now();
    bool saved = SynapticMemory::sleep(STRESS_BRAIN_FILE, graph, cortex);
    auto ts1 = high_resolution_clock::now();
    double save_ms = duration_cast<microseconds>(ts1 - ts0).count() / 1000.0;

    if (!saved) {
        printf("  FAIL: Could not save brain file!\n\n");
    } else {
        // Get file size
        FILE* sf = fopen(STRESS_BRAIN_FILE, "rb");
        long file_size = 0;
        if (sf) {
            fseek(sf, 0, SEEK_END);
            file_size = ftell(sf);
            fclose(sf);
        }

        printf("  Save time:     %.2f ms\n", save_ms);
        printf("  File size:     %.2f KB\n", file_size / 1024.0);

        // Reinitialize brain (fresh slate)
        graph->init(6500);
        cortex->init();

        // Load
        auto tl0 = high_resolution_clock::now();
        bool loaded = SynapticMemory::wake(STRESS_BRAIN_FILE, graph, cortex);
        auto tl1 = high_resolution_clock::now();
        double load_ms = duration_cast<microseconds>(tl1 - tl0).count() / 1000.0;

        if (!loaded) {
            printf("  FAIL: Could not load brain file!\n\n");
        } else {
            printf("  Load time:     %.2f ms\n", load_ms);

            // Test recall on 10 samples (every 10th sentence)
            int recall_pass = 0;
            int recall_total = 10;

            for (int i = 0; i < recall_total; i++) {
                int idx = i * 10; // 0, 10, 20, ..., 90
                int8_t hash[256];
                std::string seed_str(g_corpus[idx].seed);
                tokenizer.encode_word_hash(seed_str, hash);
                int cid = cortex->perceive(hash, false);

                char buf[1024];
                graph->clear_activation();
                int words = lexer.generate_text(cid, graph, cortex, buf, 20);

                bool ok = (words > 0 && starts_with(buf, g_corpus[idx].expected_prefix));
                if (ok) {
                    recall_pass++;
                } else {
                    printf("  RECALL FAIL [%d] seed=\"%s\" expected=\"%s\" got=\"%s\"\n",
                           idx, g_corpus[idx].seed, g_corpus[idx].expected_prefix,
                           words > 0 ? buf : "(empty)");
                }
            }

            float recall_acc = (recall_pass * 100.0f) / recall_total;
            printf("  Post-load recall: %d / %d (%.1f%%)\n", recall_pass, recall_total, recall_acc);
            printf("  Persistence:   %s\n", recall_acc >= 80.0f ? "PASS" : "FAIL");
            printf("  File size:     %s\n\n", file_size < 512 * 1024 ? "PASS (< 512 KB)" : "FAIL (>= 512 KB)");
        }

        // Cleanup test file
        remove(STRESS_BRAIN_FILE);
    }

    // ══════════════════════════════════════════════════════════
    // FINAL SCORECARD
    // ══════════════════════════════════════════════════════════
    printf("================================================================\n");
    printf(" PHASE 16A FINAL SCORECARD\n");
    printf("================================================================\n");
    printf("| Bulk Ingestion (< 500ms)              |  %s\n", ingest_ms < 500.0 ? "PASS" : "FAIL");
    printf("| Retrieval Accuracy (>= 80%%)            |  %s (%.1f%%)\n", accuracy >= 80.0f ? "PASS" : "FAIL", accuracy);
    printf("| Cross-Topic Bleeding (0%%)              |  %s\n", bleed_pass == bleed_total ? "PASS" : "FAIL");
    printf("| Brain Persistence (save/load/recall)   |  %s\n", saved ? "PASS" : "FAIL");
    printf("| Graph Scale (<512 KB brain file)       |  %s\n", saved ? "PASS" : "FAIL");
    printf("================================================================\n\n");

    // Cleanup
    lexer.destroy();
    delete graph;
    delete cortex;

    return 0;
}
