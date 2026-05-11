#pragma once
// FP-SAN Phase 10B: Native C++ Lexer & Phrase Grouper
// Replaces spaCy entirely. Zero Python dependency at runtime.
// Layer 1: Trie Lexicon — nanosecond POS tag lookups
// Layer 2: Deterministic FSM — groups tagged tokens into phrases

#include "cluster_graph.h"
#include "fpsan_metacognition.h"
#include <cstring>
#include <cstdio>
#include <cctype>
#include <vector>
#include <utility>

// Personality coloring helpers (implemented in fpsan_live_core.cpp)
void apply_personality_coloring(ClusterGraph* graph, LanguageCortex* lang_cortex, std::vector<std::pair<int,float>>& out_boosts);
void revert_personality_coloring(ClusterGraph* graph, const std::vector<std::pair<int,float>>& boosts);

// POS Tag System
enum POSTag : uint8_t {
    POS_UNKNOWN = 0,
    POS_NOUN    = 1,
    POS_VERB    = 2,
    POS_ADJ     = 3,
    POS_DET     = 4,
    POS_PREP    = 5,
    POS_ADV     = 6,
    POS_PRON    = 7,
    POS_CONJ    = 8,
    POS_PUNCT   = 9,
    POS_AUX     = 10,
    POS_CODE    = 11,
    POS_IDENTIFIER = 12,
    POS_COUNT   = 13
};

const char* pos_tag_name(POSTag t) {
    static const char* names[] = {
        "UNK", "NOUN", "VERB", "ADJ", "DET", "PREP",
        "ADV", "PRON", "CONJ", "PUNCT", "AUX", "CODE", "IDENT"
    };
    return (t < POS_COUNT) ? names[t] : "UNK";
}

// ============================================================
// LAYER 1: TRIE LEXICON
// Maps ASCII words to POS tags in ~50ns per lookup.
// Memory: ~50-200KB depending on vocabulary size.
// ============================================================
struct TrieNode {
    TrieNode* children[128]; // ASCII range
    POSTag tag;
    bool is_terminal;

    void init() {
        memset(children, 0, sizeof(children));
        tag = POS_UNKNOWN;
        is_terminal = false;
    }
};

struct Lexicon {
    TrieNode root;
    TrieNode* pool;      // Pre-allocated node pool
    int pool_size;
    int pool_used;
    int word_count;

    void init(int max_nodes = 200000) {
        root.init();
        pool_size = max_nodes;
        pool_used = 0;
        word_count = 0;
        pool = new TrieNode[pool_size];
        for (int i = 0; i < pool_size; i++) pool[i].init();
    }

    ~Lexicon() { /* intentionally leak for static lifetime */ }

    void destroy() {
        if (pool) { delete[] pool; pool = nullptr; }
    }

    TrieNode* alloc_node() {
        if (pool_used >= pool_size) return nullptr;
        return &pool[pool_used++];
    }

    // Insert a word with its POS tag
    void insert(const char* word, POSTag tag) {
        TrieNode* cur = &root;
        for (int i = 0; word[i]; i++) {
            int c = (unsigned char)tolower(word[i]);
            if (c < 0 || c >= 128) continue;
            if (!cur->children[c]) {
                cur->children[c] = alloc_node();
                if (!cur->children[c]) return; // pool exhausted
            }
            cur = cur->children[c];
        }
        cur->is_terminal = true;
        cur->tag = tag;
        word_count++;
    }

    // Lookup a word's POS tag (~50ns)
    POSTag lookup(const char* word) const {
        const TrieNode* cur = &root;
        for (int i = 0; word[i]; i++) {
            int c = (unsigned char)tolower(word[i]);
            if (c < 0 || c >= 128) return POS_UNKNOWN;
            if (!cur->children[c]) return POS_UNKNOWN;
            cur = cur->children[c];
        }
        return cur->is_terminal ? cur->tag : POS_UNKNOWN;
    }

    // Load dictionary from CSV: "word,TAG\n"
    int load_dictionary(const char* path) {
        FILE* f = fopen(path, "r");
        if (!f) return -1;

        char line[256];
        int loaded = 0;
        while (fgets(line, sizeof(line), f)) {
            // Parse "word,TAG"
            char* comma = strchr(line, ',');
            if (!comma) continue;
            *comma = '\0';
            char* tag_str = comma + 1;
            // Strip newline
            char* nl = strchr(tag_str, '\n');
            if (nl) *nl = '\0';
            char* cr = strchr(tag_str, '\r');
            if (cr) *cr = '\0';

            POSTag tag = POS_UNKNOWN;
            if (strcmp(tag_str, "NOUN") == 0) tag = POS_NOUN;
            else if (strcmp(tag_str, "VERB") == 0) tag = POS_VERB;
            else if (strcmp(tag_str, "ADJ") == 0) tag = POS_ADJ;
            else if (strcmp(tag_str, "DET") == 0) tag = POS_DET;
            else if (strcmp(tag_str, "PREP") == 0 || strcmp(tag_str, "ADP") == 0) tag = POS_PREP;
            else if (strcmp(tag_str, "ADV") == 0) tag = POS_ADV;
            else if (strcmp(tag_str, "PRON") == 0) tag = POS_PRON;
            else if (strcmp(tag_str, "CONJ") == 0 || strcmp(tag_str, "CCONJ") == 0 || strcmp(tag_str, "SCONJ") == 0) tag = POS_CONJ;
            else if (strcmp(tag_str, "AUX") == 0) tag = POS_AUX;
            else if (strcmp(tag_str, "PUNCT") == 0) tag = POS_PUNCT;

            if (tag != POS_UNKNOWN) {
                insert(line, tag);
                loaded++;
            }
        }
        fclose(f);
        return loaded;
    }

    // Bootstrap with essential English function words (determiners, prepositions, etc.)
    // These are the ~200 most common words that define sentence structure.
    void bootstrap_core() {
        // Determiners
        const char* dets[] = {"the","a","an","this","that","these","those","my","your",
                              "his","her","its","our","their","some","any","no","every",
                              "each","all","both","few","many","much","several",nullptr};
        for (int i = 0; dets[i]; i++) insert(dets[i], POS_DET);

        // Prepositions
        const char* preps[] = {"in","on","at","to","for","with","by","from","of","about",
                               "into","through","during","before","after","above","below",
                               "between","under","over","near","around","against","upon",
                               "within","without","along","across","behind","beyond",nullptr};
        for (int i = 0; preps[i]; i++) insert(preps[i], POS_PREP);

        // Pronouns
        const char* prons[] = {"i","you","he","she","it","we","they","me","him","us",
                               "them","who","what","which","whom","whose",nullptr};
        for (int i = 0; prons[i]; i++) insert(prons[i], POS_PRON);

        // Conjunctions
        const char* conjs[] = {"and","or","but","nor","yet","so","because","although",
                               "while","if","when","than","that","whether",nullptr};
        for (int i = 0; conjs[i]; i++) insert(conjs[i], POS_CONJ);

        // Auxiliaries
        const char* auxs[] = {"is","am","are","was","were","be","been","being",
                              "have","has","had","do","does","did","will","would",
                              "shall","should","may","might","can","could","must",nullptr};
        for (int i = 0; auxs[i]; i++) insert(auxs[i], POS_AUX);

        // Common adverbs
        const char* advs[] = {"not","very","also","often","never","always","sometimes",
                              "usually","just","already","still","even","now","then",
                              "here","there","where","how","why","when",nullptr};
        for (int i = 0; advs[i]; i++) insert(advs[i], POS_ADV);

        // Common verbs
        const char* verbs[] = {"run","walk","eat","drink","sleep","fly","swim","jump",
                               "sit","stand","go","come","make","take","give","get",
                               "know","think","say","tell","see","hear","feel","find",
                               "want","need","use","try","ask","work","call","move",
                               "live","play","turn","put","keep","let","begin","show",
                               "seem","help","talk","read","write","learn","grow",
                               "open","close","stop","start","kill","die","cause","causes",
                               "requires","monitors","protects","operates","destroys",
                               "violates","compiles","creates","reads","metamorph",nullptr};
        for (int i = 0; verbs[i]; i++) insert(verbs[i], POS_VERB);

        // Common adjectives
        const char* adjs[] = {"big","small","large","little","long","short","old","new",
                              "good","bad","great","high","low","young","hot","cold",
                              "fast","slow","hard","soft","strong","weak","dark","light",
                              "red","blue","green","black","white","true","false",
                              "happy","sad","angry","beautiful","ugly","smart","dumb",nullptr};
        for (int i = 0; adjs[i]; i++) insert(adjs[i], POS_ADJ);

        // Common nouns
        const char* nouns[] = {"dog","cat","bird","fish","eagle","snake","horse","lion",
                               "tree","flower","water","fire","air","earth","sun","moon",
                               "man","woman","child","person","people","house","car",
                               "book","food","time","day","night","year","world","city",
                               "hand","head","eye","heart","mind","body","life","death",
                               "king","queen","way","thing","place","part","word","name",
                               "wing","tail","nest","sky","ocean","river","mountain",
                               "animal","plant","rock","metal","glass","wood","paper",nullptr};
        for (int i = 0; nouns[i]; i++) insert(nouns[i], POS_NOUN);

        // Programming/Code primitives
        const char* codes[] = {"function","variable","loop","print","returns","takes",nullptr};
        for (int i = 0; codes[i]; i++) insert(codes[i], POS_CODE);
    }
};

// ============================================================
// LAYER 2: FSM PHRASE GROUPER
// Deterministic grammar rules that group POS-tagged tokens
// into structural phrases (NP, VP, SVO triples).
// ============================================================
enum PhraseType : uint8_t {
    PHRASE_NP = 0,  // Noun Phrase: [DET? ADJ* NOUN]
    PHRASE_VP = 1,  // Verb Phrase: [AUX? ADV? VERB]
    PHRASE_PP = 2,  // Prepositional Phrase: [PREP NP]
    PHRASE_SVO = 3  // Subject-Verb-Object triple
};

struct Token {
    char text[64];
    POSTag tag;
    int cluster_id;   // Mapped cluster in the graph (-1 if unmapped)
};

struct Phrase {
    PhraseType type;
    int head_token;       // Index of the head word in the token array
    int start_token;      // First token index
    int end_token;        // Last token index (exclusive)
    int cluster_id;       // Graph node representing this phrase (-1 if not created)
};

const int MAX_TOKENS = 128;
const int MAX_PHRASES = 32;

struct NativeLexer {
    Lexicon lexicon;

    void init() {
        lexicon.init();
        lexicon.bootstrap_core();
    }

    void destroy() {
        lexicon.destroy();
    }

    int load_dictionary(const char* path) {
        return lexicon.load_dictionary(path);
    }

    // Tokenize a sentence into tagged tokens
    int tokenize(const char* sentence, Token* out_tokens) {
        int count = 0;
        int i = 0;
        int len = (int)strlen(sentence);

        while (i < len && count < MAX_TOKENS) {
            // Skip whitespace
            while (i < len && isspace((unsigned char)sentence[i])) i++;
            if (i >= len) break;

            const unsigned char c0 = static_cast<unsigned char>(sentence[i]);
            // Single-char punctuation (never split identifiers: '_' '\'' '-' stay inside words)
            if (ispunct(c0) && c0 != '\'' && c0 != '-' && c0 != '_') {
                out_tokens[count].text[0] = sentence[i];
                out_tokens[count].text[1] = '\0';
                out_tokens[count].tag = POS_PUNCT;
                out_tokens[count].cluster_id = -1;
                count++;
                i++;
                continue;
            }

            // Extract word (alnum + underscore + in-word apostrophe/hyphen)
            int                ws = 0;
            while (i < len && ws < 63) {
                const unsigned char c = static_cast<unsigned char>(sentence[i]);
                if (isspace(c)) break;
                if (isalnum(c) || c == '_' || c == '\'' || c == '-') {
                    out_tokens[count].text[ws++] = sentence[i++];
                } else
                    break;
            }
            if (ws == 0) {
                out_tokens[count].text[0] = sentence[i++];
                out_tokens[count].text[1] = '\0';
                out_tokens[count].tag      = POS_PUNCT;
                out_tokens[count].cluster_id = -1;
                count++;
                continue;
            }
            out_tokens[count].text[ws] = '\0';

            // Lookup POS tag
            out_tokens[count].tag = lexicon.lookup(out_tokens[count].text);
            out_tokens[count].cluster_id = -1;
            count++;
        }
        return count;
    }

    // Group tokens into phrases using deterministic FSM rules
    int group_phrases(Token* tokens, int n_tokens, Phrase* out_phrases) {
        int n_phrases = 0;
        int i = 0;

        while (i < n_tokens && n_phrases < MAX_PHRASES) {
            // Skip punctuation
            if (tokens[i].tag == POS_PUNCT) { i++; continue; }

            // Try to match Noun Phrase: [DET? ADJ* NOUN/PRON]
            if (tokens[i].tag == POS_DET || tokens[i].tag == POS_ADJ ||
                tokens[i].tag == POS_NOUN || tokens[i].tag == POS_PRON) {
                int start = i;
                // Optional DET
                if (tokens[i].tag == POS_DET) i++;
                // Optional ADJ chain
                while (i < n_tokens && tokens[i].tag == POS_ADJ) i++;
                // Must end with NOUN or PRON (or we already have PRON at start)
                if (i < n_tokens && (tokens[i].tag == POS_NOUN || tokens[i].tag == POS_PRON)) {
                    out_phrases[n_phrases].type = PHRASE_NP;
                    out_phrases[n_phrases].start_token = start;
                    out_phrases[n_phrases].head_token = i; // Head = the noun
                    out_phrases[n_phrases].end_token = i + 1;
                    out_phrases[n_phrases].cluster_id = -1;
                    n_phrases++;
                    i++;
                    continue;
                } else if (start < i) {
                    // We consumed DET/ADJ but no noun follows — treat first as NP anyway
                    // (handles "the big" edge case, or rollback)
                    i = start + 1; // rollback, skip this token
                    continue;
                } else {
                    // Single PRON already matched at start
                    if (tokens[start].tag == POS_PRON) {
                        out_phrases[n_phrases].type = PHRASE_NP;
                        out_phrases[n_phrases].start_token = start;
                        out_phrases[n_phrases].head_token = start;
                        out_phrases[n_phrases].end_token = start + 1;
                        out_phrases[n_phrases].cluster_id = -1;
                        n_phrases++;
                        i = start + 1;
                        continue;
                    }
                    i = start + 1;
                    continue;
                }
            }

            // Try to match Verb Phrase: [AUX? ADV? VERB]
            if (tokens[i].tag == POS_AUX || tokens[i].tag == POS_ADV || tokens[i].tag == POS_VERB) {
                int start = i;
                if (tokens[i].tag == POS_AUX) i++;
                while (i < n_tokens && tokens[i].tag == POS_ADV) i++;
                if (i < n_tokens && tokens[i].tag == POS_VERB) {
                    out_phrases[n_phrases].type = PHRASE_VP;
                    out_phrases[n_phrases].start_token = start;
                    out_phrases[n_phrases].head_token = i;
                    out_phrases[n_phrases].end_token = i + 1;
                    out_phrases[n_phrases].cluster_id = -1;
                    n_phrases++;
                    i++;
                    continue;
                }
                // Bare verb
                if (tokens[start].tag == POS_VERB) {
                    out_phrases[n_phrases].type = PHRASE_VP;
                    out_phrases[n_phrases].start_token = start;
                    out_phrases[n_phrases].head_token = start;
                    out_phrases[n_phrases].end_token = start + 1;
                    out_phrases[n_phrases].cluster_id = -1;
                    n_phrases++;
                    i = start + 1;
                    continue;
                }
                // Bare AUX as copula VP ("is", "was", "are" used as main verb)
                if (tokens[start].tag == POS_AUX) {
                    out_phrases[n_phrases].type = PHRASE_VP;
                    out_phrases[n_phrases].start_token = start;
                    out_phrases[n_phrases].head_token = start;
                    out_phrases[n_phrases].end_token = start + 1;
                    out_phrases[n_phrases].cluster_id = -1;
                    n_phrases++;
                    i = start + 1;
                    continue;
                }
                i = start + 1;
                continue;
            }

            // Skip unrecognized tokens
            i++;
        }
        return n_phrases;
    }

    // Full pipeline: parse sentence → create graph bonds
    // Returns number of triples injected into the graph
    int ingest_sentence(const char* sentence, ClusterGraph* graph,
                        SpikingTokenizer* tokenizer, LanguageCortex* lang_cortex) {
        Token tokens[MAX_TOKENS];
        Phrase phrases[MAX_PHRASES];

        int n_tokens = tokenize(sentence, tokens);
        if (n_tokens == 0) return 0;
        // Map each word to a cluster via the LanguageCortex
        // Open-class assumption: unknown words are treated as nouns.
        // This enables JARVIS to learn proper nouns, numbers, and
        // domain-specific vocabulary not in the bootstrap trie.
        int8_t word_hash[256];
        for (int t = 0; t < n_tokens; t++) {
            // SYNTACTIC BOOTSTRAPPING (Contextual Guesser)
            // If previous word within 3 tokens was POS_CODE and this is unknown, it's an IDENTIFIER
            if (tokens[t].tag == POS_UNKNOWN) {
                bool near_code = false;
                for (int back = 1; back <= 3 && t - back >= 0; back++) {
                    if (tokens[t-back].tag == POS_CODE) near_code = true;
                }
                if (near_code) {
                    tokens[t].tag = POS_IDENTIFIER;
                    lexicon.insert(tokens[t].text, POS_IDENTIFIER);
                } else {
                    tokens[t].tag = POS_NOUN;
                    lexicon.insert(tokens[t].text, POS_NOUN);
                }
            }
            if (tokens[t].tag == POS_PUNCT) continue;
            std::string word_str(tokens[t].text);
            tokenizer->encode_word_hash(word_str, word_hash);
            tokens[t].cluster_id = lang_cortex->perceive(word_hash, true, tokens[t].text);

            // Add EDGE_AST_DEF bond if this is an identifier following POS_CODE
            if (tokens[t].tag == POS_IDENTIFIER && t > 0 && tokens[t-1].tag == POS_CODE) {
                if (tokens[t-1].cluster_id >= 0 && tokens[t].cluster_id >= 0) {
                    graph->node(tokens[t-1].cluster_id).add_edge(tokens[t].cluster_id, 1.0f, EDGE_AST_DEF);
                }
            }
        }

        // Group into phrases
        int n_phrases = group_phrases(tokens, n_tokens, phrases);

        int triples = 0;

        // Create NEXT_WORD bonds between consecutive content words
        // AND skip-gram bonds (A→C for trigram A→B→C) to bridge hub words
        int prev_cluster = -1;
        int prev_prev_cluster = -1;
        for (int t = 0; t < n_tokens; t++) {
            if (tokens[t].cluster_id < 0) continue;
            if (prev_cluster >= 0 && prev_cluster != tokens[t].cluster_id) {
                graph->node(prev_cluster).add_edge(tokens[t].cluster_id, 0.3f, EDGE_NEXT_WORD);
                graph->node(tokens[t].cluster_id).add_inverse_edge(prev_cluster, 0.3f, EDGE_NEXT_WORD);
                triples++;

                // Skip-gram: create A→C edge across hub words (trigram context)
                // This allows generation to disambiguate at shared words like "the"
                if (prev_prev_cluster >= 0 && prev_prev_cluster != tokens[t].cluster_id) {
                    graph->node(prev_prev_cluster).add_edge(tokens[t].cluster_id, 0.15f, EDGE_TEMPORAL);
                    triples++;
                }
            }
            prev_prev_cluster = prev_cluster;
            prev_cluster = tokens[t].cluster_id;
        }

        // Create PHRASE_HEAD and PHRASE_CHILD bonds
        for (int p = 0; p < n_phrases; p++) {
            int head_cid = tokens[phrases[p].head_token].cluster_id;
            if (head_cid < 0) continue;

            // Spawn a phrase node in the graph
            int phrase_node = graph->spawn();
            phrases[p].cluster_id = phrase_node;

            // PHRASE_HEAD: phrase → head word
            graph->node(phrase_node).add_edge(head_cid, 0.8f, EDGE_PHRASE_HEAD);
            graph->node(head_cid).add_inverse_edge(phrase_node, 0.8f, EDGE_PHRASE_HEAD);
            triples++;

            // PHRASE_CHILD: phrase → each constituent
            for (int t = phrases[p].start_token; t < phrases[p].end_token; t++) {
                if (tokens[t].cluster_id >= 0 && tokens[t].cluster_id != head_cid) {
                    graph->node(phrase_node).add_edge(tokens[t].cluster_id, 0.5f, EDGE_PHRASE_CHILD);
                    graph->node(tokens[t].cluster_id).add_inverse_edge(phrase_node, 0.5f, EDGE_PHRASE_CHILD);
                    triples++;
                }
            }
        }

        // Detect SVO triples: NP VP NP → Subject-Verb-Object
        for (int p = 0; p + 2 < n_phrases; p++) {
            if (phrases[p].type == PHRASE_NP &&
                phrases[p+1].type == PHRASE_VP &&
                phrases[p+2].type == PHRASE_NP) {
                int subj = tokens[phrases[p].head_token].cluster_id;
                int verb = tokens[phrases[p+1].head_token].cluster_id;
                int obj  = tokens[phrases[p+2].head_token].cluster_id;
                if (subj >= 0 && verb >= 0 && obj >= 0) {
                    const char* vtxt = tokens[phrases[p + 1].head_token].text;
                    // Causal SVO: "fire causes smoke" → direct EDGE_CAUSES (world-model prior)
                    if (vtxt && strcmp(vtxt, "causes") == 0) {
                        graph->node(subj).add_edge(obj, 2.0f, EDGE_CAUSES, PROV_USER);
                        triples += 1;
                        continue;
                    }

                    // Create Coincidence Binding Node
                    int b_node = graph->spawn();
                    graph->node(b_node).is_binding_node.store(true, std::memory_order_release);

                    // Temporal Momentum: Find the LAST (most recent) binding node for this subject
                    int last_b_node = -1;
                    {
                        int subj_ec = graph->node(subj).edge_count.load(std::memory_order_acquire);
                        for (int i = 0; i < subj_ec; i++) {
                            int old_tgt = graph->node(subj).edges[i].target;
                            if (graph->node(old_tgt).is_binding_node.load(std::memory_order_acquire)
                                && old_tgt != b_node) {
                                last_b_node = old_tgt;
                            }
                        }
                    }

                    // Bind Subj and Verb into the Binding Node
                    // Weight 1.1: ensures prop = 0.7*1.1 = 0.77 per input
                    // Spatial summation of 2 inputs: 1.54 > 1.5 threshold → fires
                    // Single input alone (0.77) cannot fire → true coincidence detection
                    graph->node(subj).add_edge(b_node, 1.1f, EDGE_TEMPORAL);
                    graph->node(verb).add_edge(b_node, 1.1f, EDGE_TEMPORAL);
                    
                    // Binding Node triggers the Object
                    graph->node(b_node).add_edge(obj, 1.0f, EDGE_TEMPORAL);

                    // Add Temporal Momentum Avalanche link
                    if (last_b_node >= 0) {
                        graph->node(last_b_node).add_edge(b_node, 0.9f, EDGE_TEMPORAL);
                    }
                    triples += 3;
                }
            }
        }

        return triples;
    }

    // Phase 19: Raw Sequence Ingestion (Bulk Training)
    // Tokenizes raw text and builds EDGE_NEXT_WORD and EDGE_TEMPORAL bonds
    // without requiring strict NP-VP-NP grammar. Auto-tags code tokens.
    int ingest_raw_sequence(const char* text, ClusterGraph* graph,
                            SpikingTokenizer* tokenizer, LanguageCortex* lang_cortex) {
        Token tokens[MAX_TOKENS];
        int n_tokens = tokenize(text, tokens);
        if (n_tokens == 0) return 0;

        int8_t word_hash[256];
        int bonds_created = 0;

        for (int t = 0; t < n_tokens; t++) {
            // Auto-detect code tokens based on basic heuristics
            if (tokens[t].tag == POS_UNKNOWN) {
                if (strcmp(tokens[t].text, "def") == 0 || strcmp(tokens[t].text, "class") == 0 ||
                    strcmp(tokens[t].text, "import") == 0 || strcmp(tokens[t].text, "return") == 0) {
                    tokens[t].tag = POS_CODE;
                    lexicon.insert(tokens[t].text, POS_CODE);
                } else if (tokens[t].text[0] == '(' || tokens[t].text[0] == ')' || 
                           tokens[t].text[0] == '{' || tokens[t].text[0] == '}' || 
                           tokens[t].text[0] == '=' || tokens[t].text[0] == ':' ||
                           tokens[t].text[0] == '*' || tokens[t].text[0] == '&' ||
                           tokens[t].text[0] == ';' || tokens[t].text[0] == '<' ||
                           tokens[t].text[0] == '>' || strstr(tokens[t].text, "->") ||
                           strstr(tokens[t].text, "::")) {
                    tokens[t].tag = POS_PUNCT;
                } else {
                    // C++ syntax filter to avoid noise pollution
                    const char* w = tokens[t].text;
                    if (strcmp(w, "int") == 0 || strcmp(w, "void") == 0 || strcmp(w, "const") == 0 ||
                        strcmp(w, "bool") == 0 || strcmp(w, "char") == 0 || strcmp(w, "float") == 0 ||
                        strcmp(w, "auto") == 0 || strcmp(w, "nullptr") == 0 || strcmp(w, "true") == 0 ||
                        strcmp(w, "false") == 0 || strcmp(w, "if") == 0 || strcmp(w, "else") == 0 ||
                        strcmp(w, "for") == 0 || strcmp(w, "while") == 0 || strcmp(w, "struct") == 0) {
                        tokens[t].tag = POS_CODE; // Treat as code to avoid treating as open-class NOUN
                    } else {
                        tokens[t].tag = POS_NOUN; // Default open class
                        lexicon.insert(tokens[t].text, POS_NOUN);
                    }
                }
            }

            // Skip punctuation and pure syntax code tokens
            if (tokens[t].tag == POS_PUNCT || tokens[t].tag == POS_CODE) continue;

            std::string word_str(tokens[t].text);
            tokenizer->encode_word_hash(word_str, word_hash);
            tokens[t].cluster_id = lang_cortex->perceive(word_hash, true, tokens[t].text);

            // Phase 2: Levenshtein resonance — if not found, also spike neighbors at 0.8×
            if (tokens[t].cluster_id < 0) {
                spike_levenshtein_neighbors(tokens[t].text, graph, lang_cortex, 0.8f, 3);
            }
        }

        // Build Sequence Bonds
        int prev_cluster = -1;
        int prev_prev_cluster = -1;
        for (int t = 0; t < n_tokens; t++) {
            if (tokens[t].cluster_id < 0) continue;
            if (prev_cluster >= 0 && prev_cluster != tokens[t].cluster_id) {
                graph->node(prev_cluster).add_edge(tokens[t].cluster_id, 0.3f, EDGE_NEXT_WORD);
                graph->node(tokens[t].cluster_id).add_inverse_edge(prev_cluster, 0.3f, EDGE_NEXT_WORD);
                bonds_created++;

                // Skip-gram
                if (prev_prev_cluster >= 0 && prev_prev_cluster != tokens[t].cluster_id) {
                    graph->node(prev_prev_cluster).add_edge(tokens[t].cluster_id, 0.15f, EDGE_TEMPORAL);
                    bonds_created++;
                }
            }
            prev_prev_cluster = prev_cluster;
            prev_cluster = tokens[t].cluster_id;
        }

        return bonds_created;
    }

    // ── Phase 8: Contradiction Detection ────────────────────────────────────
    // Scans the graph for nodes that have TWO outgoing EDGE_IS_A edges where
    // the two target nodes are connected by an EDGE_ANTONYM relationship.
    // Logs every contradiction to contradictions.csv and returns the count.
    //
    // Also detects simple "sky IS_A blue" then "sky IS_A red" scenarios by
    // checking IS_A edges from the same source that reach ANTONYM-linked targets.
    // ────────────────────────────────────────────────────────────────────────
    int detect_contradictions(ClusterGraph* graph,
                              LanguageCortex* lang_cortex,
                              const char* csv_path = "contradictions.csv") {
        int found = 0;
        FILE* csv = fopen(csv_path, "a");
        // Header if file is empty
        if (csv) {
            fseek(csv, 0, SEEK_END);
            if (ftell(csv) == 0)
                fprintf(csv, "subject,object_a,object_b,edge_type,prov_a,prov_b\n");
        }

        std::shared_lock<std::shared_mutex> lk(graph->graph_rw_lock);
        const int nc = graph->node_count.load(std::memory_order_acquire);

        for (int src = 0; src < nc; src++) {
            ClusterNode& sn = graph->node(src);
            if (!sn.alive.load(std::memory_order_relaxed)) continue;
            int ec = sn.edge_count.load(std::memory_order_relaxed);

            // Collect all IS_A and HAS_A targets
            for (int i = 0; i < ec; i++) {
                if (sn.edges[i].type != EDGE_IS_A && sn.edges[i].type != EDGE_HAS_A) continue;
                int tgt_a = sn.edges[i].target;
                EdgeProvenance prov_a = sn.edges[i].provenance;

                for (int j = i+1; j < ec; j++) {
                    if (sn.edges[j].type != sn.edges[i].type) continue;
                    int tgt_b = sn.edges[j].target;
                    if (tgt_a == tgt_b) continue;
                    EdgeProvenance prov_b = sn.edges[j].provenance;

                    // Check if tgt_a ↔ tgt_b have an ANTONYM edge
                    bool antonym_ab = false;
                    ClusterNode& ta = graph->node(tgt_a);
                    int tec = ta.edge_count.load(std::memory_order_relaxed);
                    for (int k = 0; k < tec; k++) {
                        if (ta.edges[k].target == tgt_b &&
                            ta.edges[k].type == EDGE_ANTONYM) {
                            antonym_ab = true; break;
                        }
                    }
                    if (!antonym_ab) continue;

                    // Contradiction found
                    found++;
                    const char* subj_label  = lang_cortex->get_word(src);
                    const char* obj_a_label = lang_cortex->get_word(tgt_a);
                    const char* obj_b_label = lang_cortex->get_word(tgt_b);
                    printf("[Contradiction] '%s' IS_A '%s' AND '%s' (antonyms!) prov=%d/%d\n",
                           subj_label ? subj_label : "?",
                           obj_a_label ? obj_a_label : "?",
                           obj_b_label ? obj_b_label : "?",
                           (int)prov_a, (int)prov_b);
                    if (csv)
                        fprintf(csv, "%s,%s,%s,%s,%d,%d\n",
                                subj_label  ? subj_label  : "?",
                                obj_a_label ? obj_a_label : "?",
                                obj_b_label ? obj_b_label : "?",
                                edge_type_name(sn.edges[i].type),
                                (int)prov_a, (int)prov_b);
                }
            }
        }

        if (csv) fclose(csv);
        return found;
    }

    // Phase 12: Generative Output
    // Autonomously generates text by following NEXT_WORD edges deterministically.
    // Uses direct edge weight + residual voltage for scoring (NOT recursive spread).
    // Relies on Contextual Priming (residual voltages) for tie-breaking,
    // Chain-Context Priming (prev_node neighborhood) for hub-word disambiguation,
    // and Refractory Inhibition to prevent infinite loops.
    // doubt_level: 0.0=no bias, 1.0=strongly prefer verified (Wikipedia/ConceptNet) edges
    int generate_text(int start_cluster_id, ClusterGraph* graph, LanguageCortex* lang_cortex, char* output_buffer, int max_words = 20, MetaCognition* meta = nullptr, float doubt_level = 0.0f) {
        if (start_cluster_id < 0) {
            output_buffer[0] = '\0';
            if (meta) { meta->unknown_seed = true; }
            return 0;
        }
        if (meta) { meta->reset(); }

        int current_node = start_cluster_id;
        int prev_node = -1;  // Chain-context: track where we came from
        int word_count = 0;
        output_buffer[0] = '\0';

        // Personality Coloring: apply temporary boosts (implemented in .cpp)
        std::vector<std::pair<int,float>> _boosts;
        apply_personality_coloring(graph, lang_cortex, _boosts);

        // 1. Initial word
        const char* word = lang_cortex->get_word(current_node);
        if (word[0] != '\0') {
            strcat(output_buffer, word);
            word_count++;
        }

        // 2. Autoregressive Loop
        while (word_count < max_words) {
            // Score each direct NEXT_WORD neighbor using:
            //   score = edge_weight + residual_voltage + chain_context_bonus
            int best_neighbor = -1;
            float best_score = -999.0f;
                int next_word_count = 0;

            const ClusterNode& n = graph->node(current_node);
            {
            int n_ec = n.edge_count.load(std::memory_order_acquire);
            for (int i = 0; i < n_ec; i++) {
                if (n.edges[i].type == EDGE_NEXT_WORD) {
                    next_word_count++;
                    int target = n.edges[i].target;
                    float tgt_act = graph->node(target).activation.load(std::memory_order_acquire);
                    if (tgt_act < 0.0f) continue;

                    float score = n.edges[i].weight + tgt_act;

                    // Phase 8 trust bias: when doubt is high, prefer verified sources.
                    // Wikipedia (PROV_WIKIPEDIA=2) and ConceptNet (PROV_CONCEPTNET=3)
                    // get a bonus; user/unknown provenance gets a slight penalty.
                    if (doubt_level > 0.0f) {
                        EdgeProvenance prov = n.edges[i].provenance;
                        float trust_bonus = 0.0f;
                        if (prov == PROV_WIKIPEDIA || prov == PROV_CONCEPTNET)
                            trust_bonus = +0.3f * doubt_level;
                        else if (prov == PROV_UNKNOWN || prov == PROV_USER)
                            trust_bonus = -0.1f * doubt_level;
                        score += trust_bonus;
                    }

                    // Chain-Context Priming via Skip-Gram edges:
                    // During ingestion, we created TEMPORAL edges for trigrams (A→C).
                    // If prev_node has a skip-gram edge directly to this candidate,
                    // it means prev→current→candidate was in the original sentence.
                    // Boost the score to disambiguate at hub words like "the".
                    if (prev_node >= 0 && next_word_count > 1) {
                        const ClusterNode& pn = graph->node(prev_node);
                        int pn_ec = pn.edge_count.load(std::memory_order_acquire);
                        for (int j = 0; j < pn_ec; j++) {
                            if (pn.edges[j].target == target && pn.edges[j].type == EDGE_TEMPORAL) {
                                score += pn.edges[j].weight; // +0.15f skip-gram bonus
                                break;
                            }
                        }
                    }

                    if (score > best_score) {
                        best_score = score;
                        best_neighbor = target;
                    }
                }
            }

            } // end edge scan block

            // Stop condition: no neighbor or all neighbors in refractory
            if (best_neighbor == -1 || best_score < 0.05f) {
                // Only mark as unknown if we haven't generated beyond the seed
                if (meta && next_word_count == 0 && word_count <= 1) {
                    meta->unknown_seed = true;
                }
                break;
            }

            // Hub-word competitive inhibition: at multi-edge branch points,
            // stop ONLY if the choice is extremely ambiguous and no contextual
            // priming helps. If it's ambiguous but we have a clear path, just pick it.
            if (next_word_count > 1 && word_count >= 2) {
                float second_best = -999.0f;
                int n_ec2 = n.edge_count.load(std::memory_order_acquire);
                for (int i = 0; i < n_ec2; i++) {
                    if (n.edges[i].type == EDGE_NEXT_WORD) {
                        int target = n.edges[i].target;
                        if (target == best_neighbor) continue;
                        float tact = graph->node(target).activation.load(std::memory_order_acquire);
                        if (tact < 0.0f) continue;
                        float score = n.edges[i].weight + tact;
                        // Apply same skip-gram bonus for fair comparison
                        if (prev_node >= 0) {
                            const ClusterNode& pn = graph->node(prev_node);
                            int pn_ec2 = pn.edge_count.load(std::memory_order_acquire);
                            for (int j = 0; j < pn_ec2; j++) {
                                if (pn.edges[j].target == target && pn.edges[j].type == EDGE_TEMPORAL) {
                                    score += pn.edges[j].weight;
                                    break;
                                }
                            }
                        }
                        if (score > second_best) second_best = score;
                    }
                }
                // Record branch point for metacognition
                if (meta && second_best > -999.0f) {
                    meta->record_branch(best_score, second_best, word_count);
                }
                // Only stop if the scores are exactly identical (e.g., brand new un-reinforced bonds)
                // and the graph is extremely sparse. In a dense graph, we just take the first best_neighbor.
                if (second_best > -999.0f && (best_score - second_best) < 0.01f) {
                    if (meta) { meta->stopped_at_hub = true; }
                    break;
                }
            }

            // Output the word
            const char* next_word = lang_cortex->get_word(best_neighbor);
            if (next_word[0] != '\0') {
                strcat(output_buffer, " ");
                strcat(output_buffer, next_word);
                word_count++;
                
                // Stop gracefully at sentence-ending punctuation
                if (strcmp(next_word, ".") == 0 || strcmp(next_word, "!") == 0 || strcmp(next_word, "?") == 0) {
                    break;
                }
            } else {
                break;
            }

            // 3. Refractory Inhibition (store negative to signal refractory period)
            graph->node(current_node).activation.store(-0.5f, std::memory_order_release);

            prev_node = current_node;
            current_node = best_neighbor;
        }

        if (meta) { meta->words_generated = word_count; }

        // Revert temporary boosts applied earlier
        revert_personality_coloring(graph, _boosts);
        return word_count;
    }

    // ── Phase 2: Levenshtein-1 Sub-Threshold Resonance ───────────
    // After perceiving a word, spike the top-3 Levenshtein-1 neighbors
    // at 0.8× base voltage. This lets EDGE_NEXT_WORD context push the
    // right node over threshold without any if/else typo logic.
    // Runs in O(n×L) where n=alive clusters, L=word length.
    static int levenshtein_dist(const char* a, const char* b) noexcept {
        int la = (int)strlen(a), lb = (int)strlen(b);
        if (abs(la - lb) > 2) return 99; // fast reject
        // Simple full-matrix DP (small words, so fast)
        static int dp[65][65];
        for (int i = 0; i <= la; i++) dp[i][0] = i;
        for (int j = 0; j <= lb; j++) dp[0][j] = j;
        for (int i = 1; i <= la; i++)
            for (int j = 1; j <= lb; j++) {
                int cost = (a[i-1] == b[j-1]) ? 0 : 1;
                dp[i][j] = std::min({dp[i-1][j]+1, dp[i][j-1]+1, dp[i-1][j-1]+cost});
            }
        return dp[la][lb];
    }

    // Spike up to max_neighbors cortex nodes that are Levenshtein-1 from `word`
    // at voltage = base_voltage. Returns number of nodes spiked.
    int spike_levenshtein_neighbors(const char* word, ClusterGraph* graph,
                                    LanguageCortex* lang_cortex,
                                    float base_voltage = 0.8f,
                                    int max_neighbors = 3) noexcept {
        struct Cand { int id; int dist; };
        static Cand cands[32];
        int nc_lang = 0;

        // Collect candidates with edit distance 1
        for (int c = 0; c < LANG_CLUSTERS && nc_lang < 32; c++) {
            if (!lang_cortex->clusters[c].active) continue;
            const char* label = lang_cortex->clusters[c].word_label;
            if (label[0] == '\0') continue;
            int d = levenshtein_dist(word, label);
            if (d >= 1 && d <= 1) { // exactly Lev-1
                cands[nc_lang++] = {c, d};
            }
        }

        // Take top max_neighbors (they all have dist==1; just cap count)
        int n_spiked = 0;
        for (int i = 0; i < nc_lang && n_spiked < max_neighbors; i++) {
            // Map cortex cluster → graph node via graph node labels
            // The cortex cluster index IS the graph node id (perceive returns graph id)
            int gid = cands[i].id;
            if (gid >= 0 && gid < graph->node_count.load(std::memory_order_acquire)) {
                graph->node(gid).add_voltage(base_voltage);
                n_spiked++;
            }
        }
        return n_spiked;
    }
};

