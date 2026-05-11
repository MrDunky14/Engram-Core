// PLUGIN_INTERFACE: zero_deps_required
// ============================================================
// FP-SAN Module Interfaces (Pillars 1-5 surfaces)
// Zero external dependencies. C++17 only.
//
// NOTE: These are *interfaces* only. Implementations live elsewhere.
// ============================================================

#pragma once

#include <cstdint>

namespace fpsan {

enum class ModuleKind : uint8_t {
    Sensor = 1,
    Actuator = 2,
    Speaker = 3,
    SpatialEngine = 4,
    Recompiler = 5,
    Cortex = 6,
};

struct ModuleHealth {
    bool ok = true;
    const char* message = nullptr;
};

struct ModuleTickContext {
    uint64_t tick_id = 0;
    uint64_t now_ns = 0;
    uint64_t budget_ns = 0;
};

struct IModule {
    virtual ~IModule() = default;
    virtual const char* name() const noexcept = 0;
    virtual ModuleKind kind() const noexcept = 0;
    virtual uint32_t version() const noexcept = 0;

    virtual bool init() noexcept = 0;
    virtual void tick(const ModuleTickContext& ctx) noexcept = 0;
    virtual void shutdown() noexcept = 0;
    virtual ModuleHealth health() const noexcept = 0;
};

struct ISensor : public IModule {};
struct IActuator : public IModule {};
struct ISpeaker : public IModule {};
struct ISpatialEngine : public IModule {};
struct IRecompiler : public IModule {};

} // namespace fpsan

