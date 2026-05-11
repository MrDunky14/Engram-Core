// Phase 14 gate — wasm3 sandbox harness + orchestration hooks exercise.
//
// Build: scripts\compile_phase14_gate.bat  (needs MSVC wasm3 objs)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "fpsan_module_registry.h"
#include "fpsan_wasm_sandbox.h"

#include <cstdio>
#include <fstream>
#include <vector>

static std::vector<uint8_t> read_file_blob(const char* path) {
    std::vector<uint8_t> out;
    std::ifstream f(path, std::ios::binary);
    if (!f) return out;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz <= 0) return out;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(out.data()), sz))
        out.clear();
    return out;
}

static bool gate_hello() {
    puts("[GATE A] hello_sensor.wasm saturation");
    auto wasm = read_file_blob("fixtures/phase14/hello_sensor.wasm");
    if (wasm.empty()) {
        puts("FAIL missing fixtures/phase14/hello_sensor.wasm");
        return false;
    }
    fpsan::WasmSandbox s;
    fpsan::WasmSandboxBudget cfg;
    cfg.wasm_stack_bytes  = 32768;
    cfg.host_gate_budget  = (uint32_t)-1;

    if (!s.load(wasm.data(), wasm.size(), cfg)) {
        printf("FAIL load hello %s\n", s.last_trap() ? s.last_trap() : "?");
        return false;
    }
    LARGE_INTEGER fq{}, t0{}, t1{};
    QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&t0);
    const int iterations = 200000;
    for (int i = 0; i < iterations; ++i) {
        auto rv = s.call_i32_0("sensor_read");
        if (!rv.ok || rv.i64 != 42) {
            printf("FAIL hello iter %d err=%s val=%lld\n", i,
                   rv.err ? rv.err : "?", (long long)rv.i64);
            return false;
        }
    }
    QueryPerformanceCounter(&t1);
    double secs = double(t1.QuadPart - t0.QuadPart) / double(fq.QuadPart);
    double per_us = secs * 1e6 / double(iterations);
    printf("  ok %d iterations, %.4f µs avg per invoke\n", iterations, per_us);
    if (per_us > 500.0) {
        puts("WARN high per-invoke latency"); // heuristic only
    }
    return true;
}

static bool gate_runaway_trap() {
    puts("[GATE B] runaway.wasm traps under host gate budget");
    auto wasm = read_file_blob("fixtures/phase14/runaway.wasm");
    if (wasm.empty()) {
        puts("FAIL missing runaway wasm");
        return false;
    }
    fpsan::WasmSandbox s;
    fpsan::WasmSandboxBudget b;
    b.wasm_stack_bytes = 32768;
    b.host_gate_budget = 800; // tighter than wasm loop amortization noise

    if (!s.load(wasm.data(), wasm.size(), b)) {
        printf("FAIL load runaway %s\n", s.last_trap() ? s.last_trap() : "?");
        return false;
    }
    LARGE_INTEGER fq{}, ta{}, tb{};
    QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&ta);
    auto rv = s.call_void("_start");
    QueryPerformanceCounter(&tb);
    double ms = double(tb.QuadPart - ta.QuadPart) / double(fq.QuadPart) * 1000.0;

    // Expect trap signal (budget exceeded)
    if (rv.ok) {
        puts("FAIL runaway did not terminate with trap/path");
        return false;
    }
    printf("  trapped as expected in %.4f ms (message=%s)\n", ms,
           rv.err ? rv.err : "(null)");
    if (ms > 5.0) {
        puts("WARN runaway exceeded 5 ms budget goal");
        // soft warning — environment dependent
    }
    return true;
}

static bool gate_hotswap() {
    puts("[GATE C] sequential loads v1->v2 (atomic binary swap surrogate)");
    auto v1 = read_file_blob("fixtures/phase14/hello_sensor.wasm");
    auto v2 = read_file_blob("fixtures/phase14/hello_sensor_v2.wasm");
    if (v1.empty() || v2.empty()) {
        puts("FAIL missing wasm fixtures");
        return false;
    }

    fpsan::WasmSandbox s;
    fpsan::WasmSandboxBudget cfg;
    if (!s.load(v1.data(), v1.size(), cfg)) return false;
    auto a = s.call_i32_0("sensor_read");
    if (!a.ok || a.i64 != 42)
        return false;

    // simulate hot swap by reloading new binary wholesale
    if (!s.load(v2.data(), v2.size(), cfg)) return false;
    auto b = s.call_i32_0("sensor_read");
    if (!b.ok || b.i64 != 77)
        return false;

    puts("  hot swap surrogate OK");
    return true;
}

static bool gate_orchestrator_stub() {
    puts("[GATE D] ModuleRegistry health tick smoke");
    fpsan::ModuleRegistry reg;
    fpsan::NullSpatial spatial;
    fpsan::NullRecompiler recomp;
    reg.register_module(&spatial);
    reg.register_module(&recomp);

    fpsan::ModuleTickContext ctx{1, 0, 500000};
    reg.tick_health_all(ctx, /*fail_limit*/ 2);

    FILETIME ft{};
    wchar_t path[] = L"fixtures/phase14/hello_sensor.wasm";
    (void)fpsan::wasm_file_changed_since(path, &ft);
    bool again = fpsan::wasm_file_changed_since(path, &ft);
    if (again) {
        puts("FAIL file change false positive");
        return false;
    }
    puts("  registry tick OK");
    return true;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    puts("PHASE14 GATE wasm3 sandbox + orchestrator smoke\n");

    if (!gate_hello()) return 1;
    if (!gate_runaway_trap()) return 1;
    if (!gate_hotswap()) return 1;
    if (!gate_orchestrator_stub()) return 1;

    puts("\nPASS");
    return 0;
}
