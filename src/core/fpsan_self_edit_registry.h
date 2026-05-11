#pragma once
// ============================================================
// Research program R4 — Self-authored artefact registry + episodic GC
// HARD: total cap default 50MB, per-artefact 4MB, prune if idle 7d OR 50 episodes.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fpsan {

struct SelfEditRecord {
    char     artefact_path[MAX_PATH]{};
    char     kind[16]{}; // "wasm" | "dll"
    uint64_t created_episode = 0;
    uint64_t last_invocation_episode = 0;
    FILETIME last_invocation_ft{};
    uint64_t bytes_disk = 0;
    uint32_t bytes_rss_estimate = 0;
    uint64_t pruned_episode = 0;
    bool     l3_pinned = false;
};

struct SelfEditRegistryConfig {
    static constexpr uint64_t kDefaultTotalCap = 50u * 1024u * 1024u;
    static constexpr uint32_t kPerArtefactCap = 4u * 1024u * 1024u;
    static constexpr int       kIdleEpisodes   = 50;
    // 7 days in 100-ns intervals (FILETIME)
    static constexpr uint64_t kSevenDays100ns = 7ull * 24ull * 3600ull * 10000000ull;
};

class SelfEditRegistry {
public:
    explicit SelfEditRegistry(const char* csv_path = "artefacts/self_edits.csv") noexcept
        : csv_path_(csv_path ? csv_path : "artefacts/self_edits.csv") {}

    void set_csv_path(const char* p) noexcept { csv_path_ = p ? p : ""; }

    [[nodiscard]] uint64_t total_bytes_disk() const noexcept {
        uint64_t s = 0;
        for (const auto& r : records_)
            if (r.pruned_episode == 0) s += r.bytes_disk;
        return s;
    }

    /// Register or touch an artefact. Returns false if per-artefact cap exceeded or total cap (no prune inside add — caller runs janitor first).
    bool register_or_touch(const char* path_utf8, const char* kind, uint64_t episode_now,
                          uint64_t bytes_disk, bool l3_pinned) noexcept {
        if (!path_utf8 || !kind) return false;
        if (bytes_disk > SelfEditRegistryConfig::kPerArtefactCap) return false;

        FILETIME now{};
        SYSTEMTIME st{};
        GetSystemTime(&st);
        SystemTimeToFileTime(&st, &now);

        for (auto& r : records_) {
            if (r.pruned_episode != 0) continue;
            if (strcmp(r.artefact_path, path_utf8) == 0) {
                r.last_invocation_episode = episode_now;
                r.last_invocation_ft = now;
                r.l3_pinned = l3_pinned;
                r.bytes_disk = bytes_disk;
                append_csv_row(r, "touch");
                return true;
            }
        }

        if (total_bytes_disk() + bytes_disk > SelfEditRegistryConfig::kDefaultTotalCap)
            return false;

        SelfEditRecord nr{};
        strncpy(nr.artefact_path, path_utf8, MAX_PATH - 1);
        strncpy(nr.kind, kind, 15);
        nr.created_episode = episode_now;
        nr.last_invocation_episode = episode_now;
        nr.last_invocation_ft = now;
        nr.bytes_disk = bytes_disk;
        nr.l3_pinned = l3_pinned;
        records_.push_back(nr);
        append_csv_row(nr, "create");
        return true;
    }

    /// Call on L2 episode rollover or boot. `episode_now` monotonic episode id.
    void janitor_sweep(uint64_t episode_now) noexcept {
        FILETIME now{};
        SYSTEMTIME st{};
        GetSystemTime(&st);
        SystemTimeToFileTime(&st, &now);

        for (auto& r : records_) {
            if (r.pruned_episode != 0) continue;
            if (r.l3_pinned) continue;

            bool old_ep = (episode_now > r.last_invocation_episode) &&
                          (episode_now - r.last_invocation_episode >= (uint64_t)SelfEditRegistryConfig::kIdleEpisodes);

            ULARGE_INTEGER ul_now{}, ul_last{};
            ul_now.LowPart = now.dwLowDateTime;
            ul_now.HighPart = now.dwHighDateTime;
            ul_last.LowPart = r.last_invocation_ft.dwLowDateTime;
            ul_last.HighPart = r.last_invocation_ft.dwHighDateTime;
            uint64_t delta = ul_now.QuadPart - ul_last.QuadPart;
            bool old_wall = delta >= SelfEditRegistryConfig::kSevenDays100ns;

            if (old_ep || old_wall) {
                DeleteFileA(r.artefact_path);
                r.pruned_episode = episode_now;
                append_csv_row(r, "pruned");
            }
        }
    }

private:
    void append_csv_row(const SelfEditRecord& r, const char* op) noexcept {
        CreateDirectoryA("artefacts", nullptr);
        FILE* fp = fopen(csv_path_.c_str(), "a+b");
        if (!fp) return;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        if (sz <= 0) {
            fprintf(fp,
                    "op,path,kind,created_ep,last_inv_ep,bytes_disk,bytes_rss,l3_pin,pruned_ep\n");
        }
        fprintf(fp, "%s,%s,%s,%llu,%llu,%llu,%u,%d,%llu\n",
                op, r.artefact_path, r.kind,
                (unsigned long long)r.created_episode,
                (unsigned long long)r.last_invocation_episode,
                (unsigned long long)r.bytes_disk,
                (unsigned)r.bytes_rss_estimate,
                r.l3_pinned ? 1 : 0,
                (unsigned long long)r.pruned_episode);
        fclose(fp);
    }

    std::string csv_path_;
    std::vector<SelfEditRecord> records_;
};

} // namespace fpsan
