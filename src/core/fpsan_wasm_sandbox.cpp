// ============================================================
// wasm3-backed sandbox implementation (linked with wasm3 objs)
// ============================================================

#include "fpsan_wasm_sandbox.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif

extern "C" {
#include "../../vendor/wasm3/upstream/source/wasm3.h"
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <cstring>

namespace fpsan {

struct HostGateCtx {
    uint32_t max_pulses;
    uint32_t pulses;
};

static m3ApiRawFunction(fpsan_host_gate_pulse_raw) {
    auto* gb = reinterpret_cast<HostGateCtx*>(_ctx->userdata);
    if (!gb)
        return m3Err_trapUnreachable;
    gb->pulses++;
    if (gb->pulses > gb->max_pulses)
        return m3Err_trapUnreachable;
    return m3Err_none;
}

void WasmSandbox::teardown() noexcept {
    trap_ = nullptr;
    // Free runtime first — it retains raw import userdata until destroyed.
    if (rt_) {
        m3_FreeRuntime(static_cast<IM3Runtime>(rt_));
        rt_ = nullptr;
    }
    if (gate_heap_) {
        delete reinterpret_cast<HostGateCtx*>(gate_heap_);
        gate_heap_ = nullptr;
    }
    if (env_) {
        m3_FreeEnvironment(static_cast<IM3Environment>(env_));
        env_ = nullptr;
    }
    wasm_copy_.clear();
}

bool WasmSandbox::load(const uint8_t* wasm, size_t len, const WasmSandboxBudget& b) {
    teardown();

    wasm_copy_.assign(wasm, wasm + len);
    budget_ = b;

    env_ = m3_NewEnvironment();
    rt_ = m3_NewRuntime(static_cast<IM3Environment>(env_), budget_.wasm_stack_bytes, nullptr);
    if (!rt_) {
        teardown();
        return false;
    }

    IM3Module mod = nullptr;
    M3Result r = m3_ParseModule(static_cast<IM3Environment>(env_), &mod, wasm_copy_.data(),
                                  static_cast<uint32_t>(wasm_copy_.size()));
    if (r) {
        trap_ = r;
        teardown();
        return false;
    }

    r = m3_LoadModule(static_cast<IM3Runtime>(rt_), mod);
    if (r) {
        trap_ = r;
        m3_FreeModule(mod);
        teardown();
        return false;
    }

    // Optional budgeting import for destructive loop fixture (`host.gate`).
    auto* gate = new HostGateCtx{budget_.host_gate_budget, 0};
    r = m3_LinkRawFunctionEx(mod, "host", "gate", "v()", &fpsan_host_gate_pulse_raw,
                             gate);
    if (r == m3Err_functionLookupFailed) {
        delete gate;
    } else if (r) {
        trap_ = r;
        delete gate;
        teardown();
        return false;
    } else {
        gate_heap_ = gate;
    }

    r = m3_CompileModule(mod);
    if (r) {
        trap_ = r;
        teardown();
        return false;
    }

    (void)m3_RunStart(mod);

    trap_ = nullptr;
    return true;
}

WasmEvalResult WasmSandbox::finalize_call(void* vf) noexcept {
    WasmEvalResult out{};
    IM3Function f = static_cast<IM3Function>(vf);
    if (!f) {
        out.err = "missing function";
        return out;
    }
    M3Result r = m3_CallV(f);
    if (r) {
        out.err = r;
        trap_ = r;
        return out;
    }
    int32_t iv = 0;
    r = m3_GetResultsV(f, &iv);
    if (r) {
        out.err = r;
        trap_ = r;
        return out;
    }
    out.ok  = true;
    out.i64 = iv;
    return out;
}

WasmEvalResult WasmSandbox::call_i32_0(const char* export_name) noexcept {
    WasmEvalResult out{};
    if (!rt_) {
        out.err = "sandbox not initialized";
        return out;
    }
    IM3Function f = nullptr;
    M3Result r = m3_FindFunction(&f, static_cast<IM3Runtime>(rt_), export_name);
    if (r || !f) {
        out.err = r ? r : m3Err_functionLookupFailed;
        trap_ = out.err;
        return out;
    }
    return finalize_call(f);
}

WasmEvalResult WasmSandbox::call_i32_1(const char* export_name, int32_t a0) noexcept {
    WasmEvalResult out{};
    if (!rt_) {
        out.err = "sandbox not initialized";
        return out;
    }
    IM3Function f = nullptr;
    M3Result r = m3_FindFunction(&f, static_cast<IM3Runtime>(rt_), export_name);
    if (r || !f) {
        out.err = r ? r : m3Err_functionLookupFailed;
        trap_ = out.err;
        return out;
    }
    r = m3_CallV(f, a0);
    if (r) {
        out.err = r;
        trap_ = r;
        return out;
    }
    int32_t iv = 0;
    r = m3_GetResultsV(f, &iv);
    if (r) {
        out.err = r;
        trap_ = r;
        return out;
    }
    out.ok  = true;
    out.i64 = iv;
    return out;
}

WasmEvalResult WasmSandbox::call_void(const char* export_name) noexcept {
    WasmEvalResult out{};
    if (!rt_) {
        out.err = "sandbox not initialized";
        return out;
    }
    IM3Function f = nullptr;
    M3Result r = m3_FindFunction(&f, static_cast<IM3Runtime>(rt_), export_name);
    if (r || !f) {
        out.err = r ? r : m3Err_functionLookupFailed;
        trap_ = out.err;
        return out;
    }
    r = m3_CallV(f);
    if (r) {
        out.err = r;
        trap_ = r;
        return out;
    }
    out.ok = true;
    return out;
}

} // namespace fpsan
