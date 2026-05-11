# Engram Core · FP-SAN v17.0 (v1.0.0)

**Sovereign Windows cognitive daemon — C++17, graph-first memory, no cloud inference on the shipping path.**

[![C++17](https://img.shields.io/badge/C++-17-blue.svg)]()
[![Dependencies](https://img.shields.io/badge/Python_(build_path)-optional-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/OS-Windows%2010%2F11%20x64-lightgrey.svg)]()

Engram Core is a **persistent local process**: spiking-style dynamics, a **typed cluster graph** for knowledge, **homeostatic drives**, **vision (GDI/UIA)**, **SAPI voice**, and **motor output** with **ShadowBrain** safety screening. It runs a **~1 kHz target** cognitive loop; use **`/status`** for live telemetry (tick, working set, veto latency).

**Not** a hosted chatbot — see **[`SECURITY.md`](SECURITY.md)** for threat model and **[`docs/COMMANDS.md`](docs/COMMANDS.md)** for the full REPL.

---

## Quick start

**Requirements:** Windows 10/11 **x64**, **MSVC** (configure paths in [`scripts/vcvars_community.bat`](scripts/vcvars_community.bat)).

```powershell
git clone <your-repo-url>
cd <repo>
.\run_engram.bat
```

In the REPL: **`/help`**, **`/status`**, **`/save`** / **`/quit`**.

| | |
|--|--|
| **Build only** | `.\run_engram.bat compile_only` |
| **Brain file** | `engram_brain.fpsan` (persists graph + cortex; legacy `jarvis_brain.fpsan` still loads) |
| **Demo prep** | [`scripts/demo_impossible_sequence.bat`](scripts/demo_impossible_sequence.bat), [`scripts/demo_sovereignty_prep.bat`](scripts/demo_sovereignty_prep.bat) |
| **Train from file** | `/train data/demo_video_axioms.txt` — generate haystack with `scripts\gen_b1_style_haystack.ps1` first |

---

## Repository layout

```text
├── src/                 # Daemon (fpsan_live_core.cpp) + core/ engine + benchmark/
├── scripts/             # MSVC env, gates, R6 matrix, demo helpers
├── docs/                # Operator & benchmark reference ([docs/README.md](docs/README.md))
├── data/                # Small fixtures; large assets gitignored — [data/README.md](data/README.md)
├── fixtures/            # Wasm and other test fixtures
├── run_engram.bat       # Build + run build\engram.exe
├── SECURITY.md
├── LICENSE / NOTICE / CREDITS.md
└── FP-SAN Architecture.md   # Deeper design overview
```

---

## Verification & benchmarks

Native **R6** gates and **B1** retention harness are built with **`scripts\compile_research_gates.bat`**. Full matrix: **`scripts\r6_eval_matrix.ps1`**. Committed summaries: **[`docs/R6_REPORT_OFFICIAL.md`](docs/R6_REPORT_OFFICIAL.md)**, **[`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)**. Long-run methodology: **[`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md)**.

Figures and badges are **reference** stories; **your machine is authoritative** — always cite **`/status`** and fresh gate output for releases.

---

## Claims (honest scope)

**Reproducible today:** R6 gate executables, B1 structural retention receipt (1000-rule haystack in harness), R3 Wasm closed-loop gate, live **`/status`** telemetry.

**Out of scope for this README:** year-long “life log” guarantees, comparisons to LLM TTFT without a defined protocol, treating B1 as token-length NIAH. Details: **[`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)**.

---

## License & attribution

**License:** [Apache-2.0](LICENSE) — see [NOTICE](NOTICE). Third-party: **[`CREDITS.md`](CREDITS.md)** (e.g. **Wasm3**).
