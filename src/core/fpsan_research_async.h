#pragma once
// ============================================================
// FP-SAN Phase 6B: ASYNCHRONOUS RESEARCH CORTEX
// fpsan_research_async.h — Background goal decomposition & analysis.
//
// Dedicated std::thread (pinned to separate core) handles disk I/O
// without blocking the 1kHz cognitive clock. Main loop signals via
// lock-free atomics when a research task should run.
//
// Pattern mirrors MotorCortex for consistency.
//
// Phase 6C: INFORMATION DISTILLER
// Filters raw web text to extract only pure definitional sentences.
// Prevents noise pollution of neural graph by isolating semantic core
// of Wikipedia articles before ingestion into language cortex.
// ============================================================

#define NOMINMAX
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <fstream>
#include "fpsan_research.h"
#include "fpsan_winsock_http.h"

// ───────────────────────────────────────────────────────────────
// Forward externs to the global brain instances defined in
// src/fpsan_live_core.cpp.  Types are visible here because the live
// core includes cluster_graph.h, fpsan_language.h and fpsan_lexer.h
// BEFORE this header (verified in src/fpsan_live_core.cpp lines 23-36).
// We need them so the research thread can ingest fetched Wikipedia
// content directly into the graph instead of writing to a file and
// pretending that counts as learning.
// ───────────────────────────────────────────────────────────────
extern ClusterGraph*    g_graph;
extern LanguageCortex*  g_cortex;
extern SpikingTokenizer g_tokenizer;
extern NativeLexer      g_lexer;

struct ResearchCortex {
    ResearchAgent agent;

    // Async execution state
    std::thread research_thread;
    std::atomic<bool> thread_running;
    std::atomic<bool> research_pending;    // Main thread sets: "run next step"
    std::atomic<bool> research_complete;   // Research thread sets: "step done"
    std::atomic<bool> research_busy;       // Research thread sets: "step in progress"
    std::atomic<bool> allow_network;       // Allow network requests in this step
    std::atomic<bool> auto_advance;        // Drain remaining research steps after one request
    std::string result_message;            // What the research step produced
    std::string result_summary;            // Short summary for TTS
    std::string current_step_label;         // Step currently running
    std::vector<std::string> completed_steps; // Steps completed in this plan
    std::mutex progress_lock;

    // Last research goal (for auto-ingestion)
    std::string last_goal;
    int last_step_index;

    // ─── Bug 1: real-ingest accounting ─────────────────────────
    // Counts sentences from the most recent fetch_and_ingest_source()
    // that actually entered the graph via lexer.ingest_sentence().
    // Replaces the previous canned-string fallback in fpsan_live_core.cpp.
    std::atomic<int> last_facts_ingested{0};
    // Total over the lifetime of the process (auditable via /research_status).
    std::atomic<int> total_facts_ingested{0};

    void init() {
        thread_running.store(true, std::memory_order_relaxed);
        research_pending.store(false, std::memory_order_relaxed);
        research_complete.store(false, std::memory_order_relaxed);
        research_busy.store(false, std::memory_order_relaxed);
        allow_network.store(false, std::memory_order_relaxed);
        auto_advance.store(true, std::memory_order_relaxed);
        last_step_index = 0;
        {
            std::lock_guard<std::mutex> lock(progress_lock);
            current_step_label.clear();
            completed_steps.clear();
        }
        
        research_thread = std::thread(&ResearchCortex::research_loop, this);
        printf("[ResearchCortex] Initialized. Async Research Thread running.\n");
    }

    void shutdown() {
        thread_running.store(false, std::memory_order_release);
        if (research_thread.joinable()) {
            research_thread.join();
        }
        printf("[ResearchCortex] Shutdown complete.\n");
    }

    // Main thread calls this to queue a research run
    void request_next_step(bool net = false, bool advance_all = true) {
        allow_network.store(net, std::memory_order_release);
        auto_advance.store(advance_all, std::memory_order_release);
        research_pending.store(true, std::memory_order_release);
    }

    // Main thread checks this to see if research completed
    bool is_research_complete() {
        return research_complete.load(std::memory_order_acquire);
    }

    bool is_research_busy() {
        return research_busy.load(std::memory_order_acquire);
    }

    // Main thread calls this to consume results and reset
    std::string consume_result() {
        std::string msg = result_message;
        result_message.clear();
        result_summary.clear();
        research_complete.store(false, std::memory_order_release);
        return msg;
    }

    std::string get_summary() {
        return result_summary;
    }

    // Expose the plan file path so main loop can read and display actual findings
    std::string get_plan_file_path() {
        return agent.last_plan_path;
    }

    // ─────────────────────────────────────
    // RESEARCH LOOP (runs in background thread)
    // ─────────────────────────────────────
    void research_loop() {
        // Optional: set thread affinity to a separate core (Windows-specific)
        // This keeps research I/O off the main cognitive core.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        while (thread_running.load(std::memory_order_acquire)) {
            if (research_pending.load(std::memory_order_acquire)) {
                research_pending.store(false, std::memory_order_release);
                research_busy.store(true, std::memory_order_release);
                bool use_network = allow_network.load(std::memory_order_acquire);
                bool drain_all = auto_advance.load(std::memory_order_acquire);

                // Execute the research step(s) off the main loop.
                std::vector<std::string> outputs;
                while (agent.remaining() > 0) {
                    std::string step_label = agent.tasks.empty() ? std::string() : agent.tasks.front();
                    {
                        std::lock_guard<std::mutex> lock(progress_lock);
                        current_step_label = step_label;
                    }
                    std::string res;

                    if (use_network && agent.remaining() > 0 &&
                        agent.tasks[0].find("Collect authoritative") != std::string::npos) {
                        res = fetch_and_ingest_source(agent.tasks[0]);
                        agent.next_step();
                    } else {
                        res = agent.run_next_step(false);
                    }

                    {
                        std::lock_guard<std::mutex> lock(progress_lock);
                        if (!step_label.empty()) {
                            completed_steps.push_back(step_label);
                        }
                        current_step_label.clear();
                    }

                    if (!res.empty()) {
                        outputs.push_back(res);
                    }

                    // Only the first step is allowed to use network; remaining steps are local.
                    use_network = false;
                    if (!drain_all) break;
                }

                // Generate a short summary for TTS.
                if (outputs.empty()) {
                    result_summary = "No research steps were executed.";
                    result_message = result_summary;
                } else {
                    std::string combined;
                    for (size_t i = 0; i < outputs.size(); ++i) {
                        if (!combined.empty()) combined += "\n";
                        combined += outputs[i];
                    }

                    if (outputs.size() > 1) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Completed %zu research steps and saved the results.", outputs.size());
                        result_summary = buf;
                    } else if (combined.find("Collected") != std::string::npos || combined.find("fetched") != std::string::npos) {
                        result_summary = "I found authoritative sources on the web and saved them.";
                    } else if (combined.find("Extracted") != std::string::npos) {
                        result_summary = "I extracted key concepts from online sources.";
                    } else if (combined.find("Appended placeholder summary") != std::string::npos) {
                        result_summary = "I created a summary section in the plan.";
                    } else if (combined.find("Appended 3 follow-up experiments") != std::string::npos) {
                        result_summary = "I proposed three follow-up experiments and saved them.";
                    } else if (combined.find("Network fetch") != std::string::npos) {
                        result_summary = "Network research requested but not enabled in this environment.";
                    } else {
                        result_summary = combined;
                    }

                    result_message = combined;
                }

                research_complete.store(true, std::memory_order_release);
                research_busy.store(false, std::memory_order_release);
            }

            // Yield to avoid spinning
            Sleep(10);
        }
    }

private:
    // ─────────────────────────────────────
    // INFORMATION DISTILLER
    // Prevents noise pollution in neural graph by filtering raw web text
    // ─────────────────────────────────────
    
    // Split raw text into sentences using simple heuristics
    std::vector<std::string> split_into_sentences(const std::string& text) {
        std::vector<std::string> sentences;
        std::string current;
        
        for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];
            current += c;
            
            // Check for sentence boundaries: . ? ! followed by space
            if ((c == '.' || c == '?' || c == '!') && 
                i + 1 < text.length() && text[i+1] == ' ') {
                
                // Skip over multiple spaces
                while (i + 1 < text.length() && text[i+1] == ' ') i++;
                
                // Trim and store if non-empty
                if (!current.empty()) {
                    sentences.push_back(current);
                    current.clear();
                }
            }
        }
        
        // Capture any trailing text
        if (!current.empty() && current.find_first_not_of(" \t\n\r") != std::string::npos) {
            sentences.push_back(current);
        }
        
        return sentences;
    }
    
    // Check if a sentence is a definitional sentence
    // Looks for target concept + definitive verbs: is, are, means, refers to, allows, enables, involves
    bool is_definitional_sentence(const std::string& sentence, const std::string& target) {
        // Must contain target concept (case-insensitive)
        std::string lower_sentence = sentence;
        std::string lower_target = target;
        
        // Simple lowercase conversion
        for (auto& c : lower_sentence) c = tolower(c);
        for (auto& c : lower_target) c = tolower(c);
        
        if (lower_sentence.find(lower_target) == std::string::npos) {
            return false;
        }
        
        // Must contain a definitive verb
        const char* definitive_verbs[] = {
            " is ", " are ", " means ", " refers to ", " allows ",
            " enables ", " involves ", " represents ", " consists of ",
            " can be ", " could be ", " defined as "
        };
        
        for (const auto* verb : definitive_verbs) {
            if (lower_sentence.find(verb) != std::string::npos) {
                return true;
            }
        }
        
        return false;
    }
    
    // Score sentence by how close it is to pure definition
    int score_definition_quality(const std::string& sentence) {
        int score = 0;
        
        // Prefer shorter sentences (more concise definitions)
        if (sentence.length() < 150) score += 10;
        else if (sentence.length() < 250) score += 5;
        
        // Penalize for noise indicators: parentheses, dates, commas with conjunctions
        if (sentence.find('(') != std::string::npos) score -= 3;
        if (sentence.find(')') != std::string::npos) score -= 3;
        
        // Look for years (4 consecutive digits)
        for (size_t i = 0; i + 3 < sentence.length(); i++) {
            if (isdigit(sentence[i]) && isdigit(sentence[i+1]) && 
                isdigit(sentence[i+2]) && isdigit(sentence[i+3])) {
                score -= 5;
                break;
            }
        }
        
        // Prefer "is/are" over other verbs (most atomic definitions)
        std::string lower = sentence;
        for (auto& c : lower) c = tolower(c);
        if (lower.find(" is ") != std::string::npos || lower.find(" are ") != std::string::npos) {
            score += 5;
        }
        
        return score;
    }
    
    // Main filtering function: extract only pure definitional sentences
    // Returns top 3 definitional sentences distilled from raw web text
    std::string distill_to_definitions(const std::string& raw_text, const std::string& target_concept) {
        // Split into sentences
        auto sentences = split_into_sentences(raw_text);
        
        // Score each sentence
        std::vector<std::pair<int, std::string>> scored_sentences;
        for (const auto& sent : sentences) {
            if (is_definitional_sentence(sent, target_concept)) {
                int score = score_definition_quality(sent);
                scored_sentences.push_back({score, sent});
            }
        }
        
        // Sort by score (highest first)
        std::sort(scored_sentences.begin(), scored_sentences.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        
        // Collect top 3
        std::string result;
        int count = 0;
        for (const auto& [score, sent] : scored_sentences) {
            if (count >= 3) break;
            result += sent;
            if (!result.empty() && result.back() != '\n') result += " ";
            count++;
        }
        
        return result.empty() ? "No definitional content found." : result;
    }
    
    // ─── Bug 1: native sentence cleaner ───────────────────────────
    // Drops banned symbols and control characters that would pollute the
    // graph (HTML/wiki residue: < > { } [ ] | \, plus ASCII < 32 except
    // tab/newline). Returns true if the cleaned sentence is substantive
    // enough to ingest (at least 3 alphabetic chars and 2 word boundaries).
    bool clean_for_ingest(std::string& s) {
        // Strip banned symbols in place
        std::string out;
        out.reserve(s.size());
        int alpha = 0, spaces = 0;
        for (char c : s) {
            if (c == '<' || c == '>' || c == '{' || c == '}' ||
                c == '[' || c == ']' || c == '|' || c == '\\') {
                out += ' ';   // collapse to whitespace, don't drop the boundary
                continue;
            }
            if ((unsigned char)c < 32 && c != '\t' && c != '\n' && c != '\r') {
                continue;     // drop other control chars
            }
            out += c;
            if (isalpha((unsigned char)c)) ++alpha;
            else if (c == ' ') ++spaces;
        }
        s = std::move(out);
        // Trim leading/trailing whitespace
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) { s.clear(); return false; }
        s = s.substr(a, b - a + 1);
        // Substantive enough to be a fact?
        return (alpha >= 3) && (spaces >= 1) && (s.size() <= 1024);
    }

    // ─── Bug 1: REAL Wikipedia ingest ─────────────────────────────
    // Fetches a summary from Wikipedia, splits into ALL sentences,
    // cleans each, and feeds each one directly into lexer.ingest_sentence().
    // The graph physically grows from the live fetch.  Distillation is
    // still performed for the human-readable plan file (audit trail), but
    // it no longer gates whether the brain learns.
    std::string fetch_and_ingest_source(const std::string& task_description) {
        last_facts_ingested.store(0, std::memory_order_relaxed);

        // Extract goal from task (e.g., "Collect authoritative sources for: AGI")
        size_t pos = task_description.rfind(": ");
        if (pos == std::string::npos) pos = task_description.rfind("for ");
        std::string goal = pos != std::string::npos ? task_description.substr(pos + 2) : "research";

        printf("[Research] Fetching Wikipedia article: %s\n", goal.c_str());

        std::string raw_summary = HTTPClient::fetch_wikipedia_summary(goal);
        if (raw_summary.empty()) {
            printf("[Research] Failed to fetch from Wikipedia. Network error or topic not found.\n");
            return std::string("Network fetch failed or topic not found: ") + goal;
        }
        printf("[Research] Raw fetch: %zu chars.\n", raw_summary.length());

        // ── PRIMARY PATH: ingest every clean sentence into the graph ──
        std::vector<std::string> sentences = split_into_sentences(raw_summary);
        int ingested = 0;
        if (g_graph != nullptr && g_cortex != nullptr) {
            for (auto& sent : sentences) {
                if (!clean_for_ingest(sent)) continue;
                int n = g_lexer.ingest_sentence(sent.c_str(), g_graph,
                                                &g_tokenizer, g_cortex);
                if (n > 0) ++ingested;
            }
            last_facts_ingested.store(ingested, std::memory_order_release);
            total_facts_ingested.fetch_add(ingested, std::memory_order_relaxed);
        } else {
            printf("[Research] WARNING: globals not bound; ingestion skipped.\n");
        }

        // ── SECONDARY PATH: distillation -> plan file (human audit only) ──
        std::string distilled = distill_to_definitions(raw_summary, goal);
        if (agent.last_plan_path.empty()) agent.persist_plan();
        std::ofstream out(agent.last_plan_path, std::ios::app);
        if (out.is_open()) {
            out << "\n## Web Sources (Wikipedia)\n";
            out << "- Topic: " << goal << "\n";
            out << "- Sentences ingested into graph: " << ingested << "\n";
            out << "- Distilled definitions (audit only):\n  " << distilled << "\n\n";
            out.close();
        }

        printf("[Research] Ingested %d sentences from '%s' into the graph.\n",
               ingested, goal.c_str());

        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Fetched and ingested %d facts about %s into the graph.",
                 ingested, goal.c_str());
        return std::string(buf);
    }
};

extern ResearchCortex g_research_cortex;
