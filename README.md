# Engram Core · FP-SAN v17.0 — release v1.0.0

**A sovereign, lock-free C++ cognitive architecture for extreme edge AI.**

[![C++17](https://img.shields.io/badge/C++-17-blue.svg)]()
[![Footprint](https://img.shields.io/badge/RAM-~3MB_baseline-red.svg)]()
[![Binary](https://img.shields.io/badge/Binary-248KB-orange.svg)]()
[![Latency](https://img.shields.io/badge/Reasoning_P99-92μs-blueviolet.svg)]()
[![Dependencies](https://img.shields.io/badge/Python_Dependencies-0-brightgreen.svg)]()
[![Daemon](https://img.shields.io/badge/Mode-Autonomous_Living_Loop-green.svg)]()

Much of the AI stack is built for **datacenter-scale attention**—high latency, context windows, and opaque memory growth. **Engram Core** is different: a **biomimetic cognitive daemon** in **pure C++**, built to show that a **persistent, local, graph-first agent** can run on ordinary Windows laptops with **no cloud inference** and **no runtime Python dependency** for the shipping path.

**FP-SAN** (Fixed-Point Spiking Asynchronous Network) replaces the Transformer for this codebase: **LIF-style spiking physics**, **typed neuromorphic graph memory**, **homeostatic drives**, and **native Windows perception + voice**. It is a **living loop** (target **~1 kHz** tick), not a stateless chat adapter.

---

## Core architecture

Engram Core is **not** a hosted chatbot; it is a **persistent, autonomous process** on your machine.

- **Topological memory (graph “engrams”):** Knowledge lives in explicit nodes and edges (including provenance), not in a frozen model hidden state. Retrieval walks structure you can gate and instrument.
- **ShadowBrain veto:** Before risky motor plans hit the OS, a **fast structural check** can block them—[`/status`](docs/COMMANDS.md) exposes worst veto latency (microseconds-class on reference hardware). See [`SECURITY.md`](SECURITY.md).
- **Neuromodulation & RPE-style signaling:** Drives learning and gating consistent with the FP-SAN design (e.g. bounded behavior in Wasm closed-loop research gates).
- **Foveated vision & SAPI:** Win32 **GDI / UIA** paths for screen context; **asynchronous TTS** so speech does not stall the cognitive tick.
- **Homeostatic drives:** Boredom, curiosity, engagement, and **multi-step goals**—Engram Core can initiate behavior without constant user prompts.

---

## Hardware sovereignty — EliteBook baseline

Engram Core was **engineered, compiled, and gated** on a standard **HP EliteBook 850** (**Intel Core i5**, **8 GB RAM**). That **enterprise-laptop** constraint kept the critical path **GPU-free**: latency and RAM were chased in **native C++**, not hidden behind accelerators. The **v1.0.0** committed matrix is summarized in **[`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)**. **Your** machine may differ—rerun gates and use **`/status`** as the live source of truth.

### The receipts (v1.0.0 — native C++)

All **R6** lines are reproducible from **`scripts/r6_eval_matrix.ps1`** after **`scripts\compile_research_gates.bat`**. The right-hand column is **illustrative orders-of-magnitude** for cloud chat APIs (different workloads—not a symmetrical benchmark).

| Dimension | Engram Core (measured / gated) | Cloud LLM chat (illustrative) |
| :--- | :--- | :--- |
| **Structural retention (B1)** | **PASS:** first & last needle after **1000** haystack SVO rules (**9000** triples); [**not**](docs/BENCHMARK_ROADMAP.md) HF token-NIAH | Context-window needle tests vary by model and harness |
| **B1 verify step** | **~1.3 µs** structural verify on committed EliteBook run ([`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)) | N/A (first-token latency often **100–500 ms+** class) |
| **Live cognitive loop** | **~1 kHz** target; `/status` worst tick & veto µs | Request / streaming bound |
| **Long-run RAM story** | **Flat working set** over **7-day** operator soak — [`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md) | Context growth / KV cache behavior depends on product |

Badges (**~3 MB**, **248 KB**) are **baseline stories**; real demos should cite **`/status`** with a loaded graph.

---

## Evaluator quick start

**Windows 10/11 x64** + **MSVC** (see `scripts\vcvars_community.bat`). No API keys required to build or run the daemon.

**1. Clone and boot the daemon**

```powershell
git clone https://github.com/MrDunky14/Engram-Core.git
cd Engram-Core
.\run_engram.bat
```

Use **`/help`** and **`/status`** in the REPL. Full command reference: [`docs/COMMANDS.md`](docs/COMMANDS.md).

**2. Run B1 (retention gauntlet) and R6 matrix**

```bat
scripts\compile_research_gates.bat
build\b1_retention_gauntlet.exe
powershell -ExecutionPolicy Bypass -File scripts\r6_eval_matrix.ps1
powershell -ExecutionPolicy Bypass -File scripts\generate_r6_report.ps1
```

B1 writes **`artefacts/b1_retention_niah.json`** (gitignored). Committed snapshots: [`docs/R6_REPORT_OFFICIAL.md`](docs/R6_REPORT_OFFICIAL.md), [`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md).

---

## Repository topography

```text
Engram-Core/
├── scripts/                  # MSVC env, compile gates, R6 matrix, reports
│   ├── compile_research_gates.bat
│   ├── r6_eval_matrix.ps1
│   └── generate_r6_report.ps1
├── src/
│   ├── fpsan_live_core.cpp   # Daemon entry · living loop
│   ├── core/                 # FP-SAN engine (graph, drives, voice, Wasm sandbox, …)
│   │   ├── cluster_graph.h
│   │   ├── fpsan_drives.h
│   │   ├── fpsan_identity.h
│   │   └── fpsan_wasm_sandbox.* 
│   └── benchmark/            # R0–R5 + B1 + phase/Wasm gates
├── docs/                     # R6 report, benchmarks, soak, commands
├── data/                     # Smaller fixtures; large dumps gitignored — data/README.md
├── fixtures/                 # Wasm MDP / phase fixtures (e.g. phase_r3)
├── tools/                    # Optional Python dataset helpers — not required for R6
├── vendor/wasm3/             # Wasm3 upstream (MIT) — see CREDITS.md
├── run_engram.bat            # Build + run build\engram.exe
├── train_engram.bat          # Optional batch ingest (after build)
└── train_engram_sources.bat
```

---

## Threat model & capability ceiling

**What we prove (reproducible):** Native **R6** gates; **B1** structural retention receipt at **1000** rules; **R3** Wasm closed-loop gate; **~1 kHz** tick **target** with `/status` telemetry; **7-day** soak **protocol** for flat working set ([`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md)).

**What we do not claim:** **Year-long “life log” retention** or ML-style catastrophic-forgetting immunity from B1 alone; **beating 70B models** on general open-ended generation; **fair TTFT vs ShadowBrain** without a published protocol ([`docs/BENCHMARK_ROADMAP.md`](docs/BENCHMARK_ROADMAP.md)).

**Security:** [`SECURITY.md`](SECURITY.md) — **ShadowBrain veto**, **ESC kill switch**. **Binding the daemon to public network sockets** is **unsupported** and voids the stated safety posture.

---

## System requirements

| | |
|---|---|
| **OS** | **Windows 10 / 11** (x64 only). No Linux/macOS in v1. |
| **CPU** | x86_64; **AVX2** recommended on hot paths. |
| **RAM** | **8 GB** minimum (supported floor). |
| **Storage** | **< 100 MB** typical for toolchain + fixtures; large **`data/`** files excluded from Git — [`data/README.md`](data/README.md). |
| **Runtime** | **UIA** + **SAPI** (ship with Windows). |

**Remote:** [`https://github.com/MrDunky14/Engram-Core`](https://github.com/MrDunky14/Engram-Core)

**License:** [Apache License 2.0](LICENSE) — **Copyright 2026 Krishna Singh.** [NOTICE](NOTICE) · [CREDITS.md](CREDITS.md) (e.g. **Wasm3**, MIT).

### GitHub Releases (v1 — unsigned)

Publish **SHA-256** for `engram.exe` and release `.zip` in release notes:

```powershell
Get-FileHash -Algorithm SHA256 .\build\engram.exe
```

---

## Build, brain file, and REPL essentials

```powershell
.\run_engram.bat              # build + run
.\run_engram.bat compile_only # build only
```

Default brain: **`engram_brain.fpsan`**. Legacy **`jarvis_brain.fpsan`** still loads until you save once. Optional **`AUTO_SAVE_SECONDS`** (1–86400) for periodic save. Optional ingest: [`train_engram.bat`](train_engram.bat), [`train_engram_sources.bat`](train_engram_sources.bat).

| Category | Commands |
|----------|----------|
| **Cognition** | `<sentence>`, `?<word>`, `??<word>` |
| **Living loop** | `!drives`, `!goal <text>` |
| **Vision** | `!windows`, `!verify` |
| **Voice** | `!mute`, `!speak` |
| **System** | `/status`, `/words`, `/save`, `/quit`, `/help` |

---

## Benchmark definitions (repro)

- **Environment:** MSVC via `scripts\vcvars_community.bat`. **v1.0.0** matrix: EliteBook 850 **i5** / **8 GB** — [`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md).
- **Full matrix:** `scripts\compile_research_gates.bat` then `scripts\r6_eval_matrix.ps1` → append-only **`artefacts/r6_eval_matrix.txt`**.
- **R3:** `fixtures/phase_r3/micro_mdp.wasm` (rebuild with `scripts\build_phase_r3_wasm.bat` if needed).
- **Live daemon:** **`/status`** — worst cognitive tick (µs) ≠ LLM TTFT.

**Repository layout:** **`artefacts/`** and **`*.fpsan`** are **gitignored**. Optional Python helpers: [`tools/README.md`](tools/README.md).

### Industry mapping (positioning)

| FP-SAN | Analogue | We measure **today** |
|--------|-----------|----------------------|
| **B1** | NIAH-style [**analogue**](https://github.com/gkamradt/LLMTest_NeedleInAHaystack) | 1000-rule haystack; **first/last** needle **structural** retrieval |
| **R3** | RL / world-model stories (loose) | Wasm MDP gate + bounds — not BabyAI |
| **Soak** | Long-run reliability narrative | `/status` protocol in [`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md) |

---

## Benchmark results — R6 (committed snapshot)

**Date:** 2026-05-11 · **Hardware:** HP EliteBook 850 (i5, 8 GB). Detail: [`docs/R6_REPORT_OFFICIAL.md`](docs/R6_REPORT_OFFICIAL.md).

| Gate | Result |
|------|--------|
| `r0_baseline_gate` | PASS |
| `r1_neuromod_ablation_gate` | PASS |
| `r2_latent_gate` | PASS |
| `r3_world_gate` | PASS |
| `r3_wasm_closed_loop_gate` | PASS |
| `r5_identity_gate` | PASS |
| `b1_retention_gauntlet` | PASS |
| Transformer baseline | TBD (not invoked in-product) |

### Historical dev metrics (autonomy / interaction)

| Metric | Note |
|--------|------|
| **Phase 12 generative gate** | 4/4 PASS |
| **Language stress** | 87% retrieval (100-sentence battery) |
| **Visual temporal diff** | PASS |
| **Metacognition** | PASS |
| **SAPI** | Async; local TTS path ≠ LLM TTFT |

### System snapshot (baseline; measure your build)

| Metric | Typical story |
|--------|----------------|
| **Binary** | ~248 KB (`engram.exe`; flags-dependent) |
| **RAM** | ~3.1 MB baseline story; use **`/status`** live |
| **Tick** | 1 kHz target |

---

## Architecture block diagram

```text
+---------------------------------------------+
|         Engram Core (FP-SAN v17.0)          |
+-------------+-------------------------------+
|  SENSORY    |  Foveated / UIA vision        |
+-------------+-------------------------------+
|  COGNITION  |  Metacognition · drives       |
+-------------+-------------------------------+
|  REASONING  |  Typed ClusterGraph           |
+-------------+-------------------------------+
|  OUTPUT     |  SAPI · motor (SendInput)    |
+-------------+-------------------------------+
|  AGENCY     |  Goal planner · spontaneity   |
+-------------+-------------------------------+
|  MEMORY     |  .fpsan persistence          |
+-------------+-------------------------------+
```

### Key source files

| File | Role |
|------|------|
| `src/fpsan_live_core.cpp` | Daemon · living loop |
| `src/core/cluster_graph.h` | Graph substrate |
| `src/core/fpsan_lexer.h` | Lexicon · generation |
| `src/core/fpsan_screen_sensor.h` | Vision / OS awareness |
| `src/core/fpsan_drives.h` | Drives · goals |
| `src/core/fpsan_voice.h` | TTS |
| `src/core/fpsan_identity.h` | Identity seed |
| `src/core/fpsan_wasm_sandbox.*` | Wasm3 integration |

---

## Soak test runbook

1. `run_engram.bat` (or `compile_only`) — resolve **LNK1104** if `engram.exe` is locked.
2. Run **`/status`** on a cadence; log per [`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md).
3. Before a release tag: rerun matrix + refresh [`docs/R6_REPORT_OFFICIAL.md`](docs/R6_REPORT_OFFICIAL.md).

---

## What’s in v17.0

- **Voice & identity** — SAPI + self-knowledge seed.
- **Living loop** — drives, curiosity, boredom, multi-step goals.
- **Eyes & metacognition** — foveated sampling; confidence from activation entropy.

## Phase history (abbrev.)

Phases **1–14:** foundation, persistence, 1 kHz loop. **16B–17:** vision, metacognition, drives, voice, identity. See source headers for phase tags.

## Roadmap

Measured next steps: [`docs/BENCHMARK_ROADMAP.md`](docs/BENCHMARK_ROADMAP.md) (B2 veto micro-gate, scaled B1, soak logger).

## Pre-flight (before a public post or Release)

- [ ] Clean clone → `run_engram.bat compile_only`.
- [ ] `build\b1_retention_gauntlet.exe` → exit 0; refresh local B1 JSON if needed.
- [ ] Optional: full R6 matrix → update [`docs/R6_REPORT_OFFICIAL.md`](docs/R6_REPORT_OFFICIAL.md).

## Next engineering directions

- **Phase 18:** richer binding / transitive reasoning surfaces.
- **Vision–motor closure:** tighter efference-style verification loops.
