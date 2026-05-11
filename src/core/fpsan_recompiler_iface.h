#pragma once
// ============================================================
// Pillar / Phase 15 — Hot recompiler façade
//
// Wraps MetamorphicEngine (Phase 4) as an fpsan::IRecompiler pillar module.
// The heavy lifting remains in fpsan_metamorphic.h — this adaptor only binds
// the orchestrator contract (init/tick/shutdown/health probes).
// ============================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "fpsan_iface.h"
#include "fpsan_metamorphic.h"

namespace fpsan {

class MetamorphicRecompilerAdapter final : public IRecompiler {
public:
    explicit MetamorphicRecompilerAdapter(MetamorphicEngine* engine) noexcept
        : engine_(engine) {}

    const char* name() const noexcept override { return "MetamorphicRecompiler"; }
    ModuleKind kind() const noexcept override { return ModuleKind::Recompiler; }
    uint32_t version() const noexcept override { return 19; }

    bool init() noexcept override { return engine_ != nullptr; }

    void tick(const ModuleTickContext&) noexcept override {
        // Metamorphic work is episodic — nothing to amortize each ms tick yet.
    }

    void shutdown() noexcept override {}

    ModuleHealth health() const noexcept override {
        if (!engine_)
            return ModuleHealth{false, "no metamorphic backend"};
        return ModuleHealth{true, nullptr};
    }

private:
    MetamorphicEngine* engine_{nullptr};
};

} // namespace fpsan
