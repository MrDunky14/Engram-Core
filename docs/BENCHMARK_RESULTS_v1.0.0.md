# Benchmark results — Engram Core v1.0.0

**Engine:** FP-SAN **v17.0** (see `Identity::VERSION` / `run_engram.bat` build).  
**Recorded:** 2026-05-11 (Asia/Kolkata in raw logs). **Matrix stamp:** `R6 eval matrix - 2026-05-11T18:04:20.4068263+05:30` (all gates PASS).  
**Machine:** **HP EliteBook 850** — Intel Core **i5**, **8 GB** RAM, Windows x64. Compiler: MSVC 19.50 / Visual Studio 2026 (x64 toolset).  
**Purpose:** frozen citation target for the **v1.0.0** public release alongside [`R6_REPORT_OFFICIAL.md`](R6_REPORT_OFFICIAL.md).

### Hardware sovereignty — why this baseline matters

Engram Core was deliberately **engineered, compiled, and benchmarked** on standard **enterprise-class** hardware (the EliteBook configuration above), not on a GPU workstation. That constraint pushed the design toward **native C++** efficiency: there is no accelerator in the loop to mask sloppy hot paths or memory use. Latency and working-set goals were chased at the **implementation** level to support the thesis that a serious **edge** cognitive stack should not depend on the cloud.

Your own reruns will vary by CPU model, power profile, and background load. Use this file as a **repro narrative**; treat [`/status`](../docs/COMMANDS.md) and fresh gate runs on **your** machine as the live truth.

## R6 native gates (matrix)

| Gate | Result |
|------|--------|
| `r0_baseline_gate` | PASS |
| `r1_neuromod_ablation_gate` | PASS |
| `r2_latent_gate` | PASS |
| `r3_world_gate` | PASS |
| `r3_wasm_closed_loop_gate` | PASS |
| `r5_identity_gate` | PASS |
| `b1_retention_gauntlet` | PASS |
| `transformer_baseline` | TBD (offline harness not invoked in-product) |

## B1 retention (NIAH-style structural retrieval)

Summary from `artefacts/b1_retention_niah.json` (regenerate locally; path is gitignored):

| Field | Value |
|-------|--------|
| Haystack rules | 1000 |
| Triples | 9000 |
| Retention first / last | true / true |
| Ingest total | 7221.867800 ms |
| **Verify** | **0.001100 ms** (**~1.1 µs**) — two needle checks over **9000** ingested triples |
| Exit | OK |

**Evaluator note:** On the reference EliteBook run this wall time is the same order of magnitude as **L1/L2 hit** latency on contemporary cores: you are doing a **graph walk**, not a network round-trip. Rerun **`build\b1_retention_gauntlet.exe`** on your machine; `verify_ms` in **`artefacts/b1_retention_niah.json`** is authoritative.

## R0 pillar frozen suite

**30 / 30** tasks passed (`r0_baseline.json` excerpt in [`R6_REPORT_OFFICIAL.md`](R6_REPORT_OFFICIAL.md)).

## Reproduce

From repository root (MSVC environment available):

```bat
scripts\compile_research_gates.bat
powershell -ExecutionPolicy Bypass -File scripts\r6_eval_matrix.ps1
powershell -ExecutionPolicy Bypass -File scripts\generate_r6_report.ps1
```

Then refresh this file and [`R6_REPORT_OFFICIAL.md`](R6_REPORT_OFFICIAL.md) if numbers drift.
