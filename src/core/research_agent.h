#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

// Simple ResearchAgent: decomposes a textual goal into keyword tokens,
// searches local project files for matching lines, and writes incremental
// notes to artefacts/research/<timestamp>.md. Runs asynchronously.

struct ResearchAgent {
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
    std::string current_goal;
    std::string note_path;
    std::mutex mtx;

    void init() {}

    void start_research(const std::string &goal) {
        std::lock_guard<std::mutex> l(mtx);
        if (running.load()) return;
        stop_requested.store(false);
        current_goal = goal;
        // build a timestamped path
        SYSTEMTIME st; GetLocalTime(&st);
        char buf[128];
        snprintf(buf, sizeof(buf), "artefacts/research/%04d%02d%02d_%02d%02d%02d.md",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        note_path = buf;
        // create directory if needed
        std::filesystem::create_directories("artefacts/research");

        running.store(true);
        worker = std::thread([this]() { this->run(); });
    }

    void stop() {
        stop_requested.store(true);
        if (worker.joinable()) worker.join();
        running.store(false);
    }

    bool is_running() { return running.load(); }

    std::string status() {
        if (is_running()) return std::string("Research running: ") + current_goal;
        return std::string("Idle");
    }

    void add_note(const std::string &note) {
        std::lock_guard<std::mutex> l(mtx);
        std::ofstream f(note_path, std::ios::app);
        if (f) {
            f << note << "\n";
        }
    }

    void run() {
        // naive keyword extraction: split on spaces
        std::vector<std::string> keywords;
        {
            std::istringstream iss(current_goal);
            std::string w;
            while (iss >> w) {
                // lowercase
                for (auto &c : w) c = (char)tolower(c);
                keywords.push_back(w);
            }
        }

        std::ofstream out(note_path);
        if (!out) { running.store(false); return; }
        out << "# Research Notes for: " << current_goal << "\n\n";
        out.flush();

        // Search a small set of local files for matching lines
        std::vector<std::string> files = {"README.md", "training/general_knowledge.txt"};
        for (const auto &file : files) {
            if (stop_requested.load()) break;
            std::ifstream f(file);
            if (!f) continue;
            std::string line;
            int line_no = 0;
            while (std::getline(f, line)) {
                line_no++;
                std::string lower = line;
                for (auto &c : lower) c = (char)tolower(c);
                for (const auto &k : keywords) {
                    if (lower.find(k) != std::string::npos) {
                        out << "- [" << file << ":" << line_no << "] " << line << "\n";
                        out.flush();
                        break;
                    }
                }
                if (stop_requested.load()) break;
            }
        }

        // Final summary placeholder
        out << "\n## Summary\nFound candidate lines matching keywords. Next: expand with web research and citations.\n";
        out.close();
        running.store(false);
    }
};

extern ResearchAgent g_research_agent;
