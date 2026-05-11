// ============================================================
// FP-SAN Module Registry / Orchestrator (Phase 9 baseline)
// Fixed-size, no heap allocations during runtime.
// ============================================================

#pragma once

#include "fpsan_iface.h"

#include <atomic>
#include <cstdint>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fpsan {

class ModuleRegistry {
public:
    static constexpr uint32_t kMaxModules = 64;

    struct Entry {
        IModule* module = nullptr;
        uint32_t failures = 0;
        bool quarantined = false;
    };

    ModuleRegistry() = default;

    bool register_module(IModule* m) noexcept {
        if (!m) return false;
        const uint32_t idx = module_count_.fetch_add(1, std::memory_order_acq_rel);
        if (idx >= kMaxModules) {
            module_count_.store(kMaxModules, std::memory_order_release);
            return false;
        }
        entries_[idx].module = m;
        entries_[idx].failures = 0;
        entries_[idx].quarantined = false;
        return true;
    }

    uint32_t module_count() const noexcept {
        return module_count_.load(std::memory_order_acquire);
    }

    Entry& entry(uint32_t i) noexcept { return entries_[i]; }
    const Entry& entry(uint32_t i) const noexcept { return entries_[i]; }

    bool init_all() noexcept {
        const uint32_t n = module_count();
        bool ok = true;
        for (uint32_t i = 0; i < n; ++i) {
            if (!entries_[i].module) { ok = false; continue; }
            if (!entries_[i].module->init()) ok = false;
        }
        return ok;
    }

    void tick_all(const ModuleTickContext& ctx) noexcept {
        const uint32_t n = module_count();
        for (uint32_t i = 0; i < n; ++i) {
            auto& e = entries_[i];
            if (!e.module || e.quarantined) continue;
            e.module->tick(ctx);
        }
    }

    /// Prefer this in production: consult ModuleHealth(), quarantine after repeated fault.
    void tick_health_all(const ModuleTickContext& ctx, uint32_t fail_limit = 4) noexcept {
        const uint32_t n = module_count();
        for (uint32_t i = 0; i < n; ++i) {
            auto& e = entries_[i];
            if (!e.module || e.quarantined) continue;

            ModuleHealth mh = e.module->health();
            if (!mh.ok) {
                ++e.failures;
                if (e.failures >= fail_limit)
                    e.quarantined = true;
                continue;
            }
            if (e.failures)
                --e.failures;
            e.module->tick(ctx);
        }
    }

    void shutdown_all() noexcept {
        const uint32_t n = module_count();
        for (uint32_t i = 0; i < n; ++i) {
            if (!entries_[i].module) continue;
            entries_[i].module->shutdown();
        }
    }

private:
    std::atomic<uint32_t> module_count_{0};
    Entry entries_[kMaxModules]{};
};

// ── WASM hot-slot helper (directory notification) ─────────────────────────
inline bool wasm_file_changed_since(const wchar_t* path_utf16,
                                    FILETIME* last_seen_ft) noexcept {
#if defined(_WIN32)
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(path_utf16, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    FindClose(h);

    LONG cmp = CompareFileTime(&fd.ftLastWriteTime, last_seen_ft);
    if (cmp == 0)
        return false;
    *last_seen_ft = fd.ftLastWriteTime;
    return true;
#else
    (void)path_utf16;
    (void)last_seen_ft;
    return false;
#endif
}

// Phase 9 Gate helpers: tiny Null modules compile-time only.
class NullSpatial final : public ISpatialEngine {
public:
    const char* name() const noexcept override { return "NullSpatial"; }
    ModuleKind kind() const noexcept override { return ModuleKind::SpatialEngine; }
    uint32_t version() const noexcept override { return 1; }
    bool init() noexcept override { return true; }
    void tick(const ModuleTickContext&) noexcept override {}
    void shutdown() noexcept override {}
    ModuleHealth health() const noexcept override { return ModuleHealth{true, nullptr}; }
};

class NullRecompiler final : public IRecompiler {
public:
    const char* name() const noexcept override { return "NullRecompiler"; }
    ModuleKind kind() const noexcept override { return ModuleKind::Recompiler; }
    uint32_t version() const noexcept override { return 1; }
    bool init() noexcept override { return true; }
    void tick(const ModuleTickContext&) noexcept override {}
    void shutdown() noexcept override {}
    ModuleHealth health() const noexcept override { return ModuleHealth{true, nullptr}; }
};

class NullSensor final : public ISensor {
public:
    const char* name() const noexcept override { return "NullSensor"; }
    ModuleKind kind() const noexcept override { return ModuleKind::Sensor; }
    uint32_t version() const noexcept override { return 1; }
    bool init() noexcept override { return true; }
    void tick(const ModuleTickContext&) noexcept override {}
    void shutdown() noexcept override {}
    ModuleHealth health() const noexcept override { return ModuleHealth{true, nullptr}; }
};

class NullActuator final : public IActuator {
public:
    const char* name() const noexcept override { return "NullActuator"; }
    ModuleKind kind() const noexcept override { return ModuleKind::Actuator; }
    uint32_t version() const noexcept override { return 1; }
    bool init() noexcept override { return true; }
    void tick(const ModuleTickContext&) noexcept override {}
    void shutdown() noexcept override {}
    ModuleHealth health() const noexcept override { return ModuleHealth{true, nullptr}; }
};

class NullSpeakerIface final : public ISpeaker {
public:
    const char* name() const noexcept override { return "NullSpeaker"; }
    ModuleKind kind() const noexcept override { return ModuleKind::Speaker; }
    uint32_t version() const noexcept override { return 1; }
    bool init() noexcept override { return true; }
    void tick(const ModuleTickContext&) noexcept override {}
    void shutdown() noexcept override {}
    ModuleHealth health() const noexcept override { return ModuleHealth{true, nullptr}; }
};

} // namespace fpsan

