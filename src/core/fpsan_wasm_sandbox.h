#pragma once
// ============================================================
// Phase 14 — wasm3-hosted sandbox wrapper (budgeted imports)
// Depends on wasm3 object files linked into the TU / executable.
// ============================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fpsan {

struct WasmSandboxBudget {
    /// Runtime stack for wasm3 (bytes).
    uint32_t wasm_stack_bytes = 65536;

    /// For runawayfixture: trap after N host.`gate()` calls (pseudo cycle cap).
    uint32_t host_gate_budget = 1024;

    /// Optional WASM memory ceiling — forwarded to wasm3 runtime allocator heuristics
    /// (stack sizing only here; WASM currently has zero pages until module grows).
    uint32_t max_memory_bytes_hint = 256 * 1024;
};

struct WasmEvalResult {
    bool        ok = false;
    const char* err = nullptr;
    int64_t     i64 = 0;
};

/// RAII wasm3 interpreter with optional `host.gate` budget shim for metering loops.
class WasmSandbox {
public:
    WasmSandbox() = default;
    WasmSandbox(const WasmSandbox&)            = delete;
    WasmSandbox& operator=(const WasmSandbox&) = delete;

    ~WasmSandbox() { teardown(); }

    [[nodiscard]] bool load(const uint8_t* wasm, size_t len, const WasmSandboxBudget& b = WasmSandboxBudget());

    /// Call an exported WASM function expecting i32 retval, zero args (after compile).
    [[nodiscard]] WasmEvalResult call_i32_0(const char* export_name) noexcept;

    /// Call export (i32) -> i32 with one i32 argument (e.g. MDP / env step).
    [[nodiscard]] WasmEvalResult call_i32_1(const char* export_name, int32_t a0) noexcept;

    /// Run exported void () entry (typically `_start`) after CompileModule / RunStart hooks.
    [[nodiscard]] WasmEvalResult call_void(const char* export_name) noexcept;

    [[nodiscard]] bool                 ok() const noexcept { return !!rt_; }
    [[nodiscard]] const char*          last_trap() const noexcept { return trap_; }

    [[nodiscard]] const std::vector<uint8_t>& binary() const noexcept { return wasm_copy_; }

    void teardown() noexcept;

private:
    WasmEvalResult finalize_call(void* func) noexcept;

    void*            env_{nullptr}; // opaque IM3Environment
    void*            rt_{nullptr};  // opaque IM3Runtime
    void*            gate_heap_{nullptr}; // HostGateCtx* when runaway import linked
    std::vector<uint8_t> wasm_copy_;
    const char*      trap_ = nullptr;
    WasmSandboxBudget budget_{};
};

} // namespace fpsan
