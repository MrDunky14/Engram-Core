#pragma once
// Simple Research Agent (Phase 6) — lightweight goal decomposition + persistence

#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <map>
#include <algorithm>
#include <cctype>

struct ResearchAgent {
    std::vector<std::string> tasks;
    std::string last_plan_path;

    void decompose_goal(const char* goal) {
        tasks.clear();
        std::string s(goal);
        // Very lightweight decomposition heuristics (placeholder for research logic)
        tasks.push_back(std::string("Collect authoritative sources for: ") + s);
        tasks.push_back(std::string("Extract key concepts and definitions for: ") + s);
        tasks.push_back(std::string("Summarize findings in 3 bullets for: ") + s);
        tasks.push_back(std::string("Propose 3 follow-up experiments for: ") + s);
    }

    // Persist tasks to artefacts/research/<timestamp>_research.txt and return path
    std::string persist_plan() {
        std::filesystem::path dir("artefacts/research");
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#if defined(_MSC_VER)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        std::string filename = ss.str() + std::string("_research.txt");
        std::filesystem::path outpath = dir / filename;

        std::ofstream out(outpath.string());
        if (!out.is_open()) return "";
        out << "# Research Plan: " << filename << "\n\n";
        for (size_t i = 0; i < tasks.size(); ++i) {
            out << "- [ ] " << tasks[i] << "\n";
        }
        out.close();
        last_plan_path = outpath.string();
        return outpath.string();
    }

    // Return next step (removes it from the list)
    std::string next_step() {
        if (tasks.empty()) return std::string();
        std::string s = tasks.front();
        tasks.erase(tasks.begin());
        return s;
    }

    // Return number of remaining tasks
    int remaining() const {
        return (int)tasks.size();
    }

    // Run the next step. If allow_network is true, external fetchers may be invoked.
    // Returns a textual result or empty on failure.
    std::string run_next_step(bool allow_network=false) {
        if (tasks.empty()) return std::string();
        std::string step = next_step();

        // Ensure we have a plan file to append results to
        if (last_plan_path.empty()) persist_plan();

        std::ofstream out(last_plan_path, std::ios::app);
        if (!out.is_open()) return std::string();

        if (allow_network) {
            out << "\n## Network Fetch Requested\n";
            out << "- Note: Network fetcher requested but disabled in this environment.\n\n";
            out.close();
            return std::string("Network fetch requested but not performed in this environment.");
        }

        // Simple dispatch based on heuristics in step text
        if (step.find("Collect authoritative sources") != std::string::npos) {
            out << "\n## Sources (local scan)\n";
            // scan common local doc locations
            const std::vector<std::string> scan_dirs = {"training", "artefacts", "."};
            for (const auto &d : scan_dirs) {
                std::error_code ec;
                for (auto &p : std::filesystem::directory_iterator(d, ec)) {
                    if (ec) break;
                    if (!p.is_regular_file()) continue;
                    std::string ext = p.path().extension().string();
                    if (ext == ".txt" || ext == ".md" ) {
                        std::ifstream f(p.path().string());
                        if (!f.is_open()) continue;
                        std::string first;
                        std::getline(f, first);
                        out << "- " << p.path().generic_string() << ": " << first << "\n";
                    }
                }
            }
            out << "\n";
            out.close();
            return std::string("Collected local sources and appended to plan.");
        }

        if (step.find("Extract key concepts") != std::string::npos) {
            out << "\n## Key Concepts (local extraction)\n";
            // naive keyword frequency across training + artefacts
            std::map<std::string,int> freq;
            const std::vector<std::string> files = {"training/general_knowledge.txt", "README.md"};
            for (const auto &fpath : files) {
                std::error_code ec;
                if (!std::filesystem::exists(fpath, ec)) continue;
                std::ifstream f(fpath);
                std::string w;
                while (f >> w) {
                    // normalize simple punctuation
                    while (!w.empty() && ispunct((unsigned char)w.back())) w.pop_back();
                    while (!w.empty() && ispunct((unsigned char)w.front())) w.erase(w.begin());
                    if (w.size() < 3) continue;
                    for (auto &c : w) c = tolower((unsigned char)c);
                    freq[w]++;
                }
            }
            // pick top 8
            std::vector<std::pair<int,std::string>> vec;
            for (auto &p : freq) vec.emplace_back(p.second, p.first);
            sort(vec.begin(), vec.end(), std::greater<>());
            int limit = std::min((size_t)8, vec.size());
            for (int i = 0; i < limit; ++i) {
                out << "- " << vec[i].second << " (count=" << vec[i].first << ")\n";
            }
            out << "\n";
            out.close();
            return std::string("Extracted local key concepts and appended to plan.");
        }

        if (step.find("Summarize findings") != std::string::npos) {
            out << "\n## Summary\n";
            out << "- Local-only summary: repository notes and benchmark artifacts were scanned without network access.\n";
            out << "- Sources and key concepts were collected from local docs, training text, and README files.\n";
            out << "- Follow-up work should validate the benchmark metrics against reproducible runs.\n\n";
            out.close();
            return std::string("Appended local summary to plan.");
        }

        if (step.find("Propose 3 follow-up experiments") != std::string::npos) {
            out << "\n## Follow-up Experiments\n";
            out << "- (1) Reproduce key benchmark on local dataset.\n";
            out << "- (2) Cross-validate architecture variations with ablation.\n";
            out << "- (3) Collect small corpus of domain-specific texts and retrain.\n\n";
            out.close();
            return std::string("Appended 3 follow-up experiments to plan.");
        }

        // Fallback: append the step as a note
        out << "\n## Note\n- " << step << "\n\n";
        out.close();
        return std::string("Recorded step: " ) + step;
    }
};
 
