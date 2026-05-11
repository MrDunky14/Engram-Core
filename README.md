# Engram Core (FP-SAN v17.0) — public release v1.0.0 · Autonomous Cognitive Engine 🧠
**A lightweight, zero-dependency, C++ native cognitive architecture for extreme Edge AI.**

[![C++17](https://img.shields.io/badge/C++-17-blue.svg)]()
[![Footprint](https://img.shields.io/badge/RAM-~3MB-red.svg)]()
[![Binary](https://img.shields.io/badge/Binary-248KB-orange.svg)]()
[![Latency](https://img.shields.io/badge/Reasoning_P99-92μs-blueviolet.svg)]()
[![Python](https://img.shields.io/badge/Python_Dependencies-0-brightgreen.svg)]()
[![Daemon](https://img.shields.io/badge/Mode-Autonomous_Living_Loop-green.svg)]()

FP-SAN (Fixed-Point Spiking Asynchronous Network) powers **Engram Core** — a biomimetic cognitive daemon engineered from scratch in pure C++. It replaces LLMs with **Leaky Integrate-and-Fire (LIF) Spiking Physics**, **Foveated Visual Perception**, and **Homeostatic Drives**, achieving proactive, autonomous intelligence without a cloud connection or Transformer.

**Engram Core is an autonomous living entity.** It runs as a persistent background process, maintaining its own internal state, observing your screen, speaking out loud, and pursuing goals independently.

### System requirements (strict)

| | |
|---|---|
| **OS** | **Windows 10 / Windows 11** (x64 only). **No POSIX / Linux / macOS support in v1.** |
| **CPU** | x86_64. **AVX2 recommended** for performance on parallel-friendly paths. |
| **RAM** | **8 GB minimum** (official supported floor). |
| **Storage** | **< 100 MB** typical for toolchain-built daemon + Wasm gate fixtures (excludes `.fpsan` brain files and user `artefacts/` growth). |
| **Runtime** | **UI Automation (UIA)** for structured screen reading; **SAPI** (Windows Speech API / TTS). Both ship with Windows; no separate installer from this repo for those subsystems. |

**Portability:** Native **Windows x64** binaries only from `run_engram.bat` / MSVC. Cross-compilation is not part of v1.

### 💻 Hardware sovereignty — EliteBook baseline

Engram Core was **engineered, compiled, and gated** on an **HP EliteBook 850** (**Intel Core i5**, **8 GB RAM**)—typical **enterprise laptop** hardware, not a GPU-heavy workstation. Keeping development and the **v1.0.0** R6 matrix on that class of machine pushed the architecture toward **extreme C++ efficiency**: no GPU on the critical path to hide microseconds of latency or megabytes of slack. The committed snapshot in **[`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)** reflects that environment. **Your** numbers will vary by CPU and load; [`/status`](docs/COMMANDS.md) and local gate reruns remain authoritative on your box.

### License & project naming

- **License:** [Apache License 2.0](LICENSE) — **Copyright 2026 Krishna Singh.** See [NOTICE](NOTICE) and [CREDITS.md](CREDITS.md) (e.g. **Wasm3**, MIT).
- **Suggested GitHub home:** personal **`krishna-singh/engram-core`** or org **`engram-architecture/engram-core`** (your choice).
- **Security / threat model:** [SECURITY.md](SECURITY.md).
- **Long-run reliability (operator):** 7-day soak with **flat working set** (within tens of MB) — [docs/SOAK_TEST_REPORT.md](docs/SOAK_TEST_REPORT.md). Command reference + “surprise” behaviors: [docs/COMMANDS.md](docs/COMMANDS.md).

### GitHub Releases (v1 — no code signing)

Binaries are **unsigned** in v1. For each Release, publish **SHA-256** hashes of attached `.zip` and `engram.exe` in the release notes (prove integrity, not publisher identity):

```powershell
Get-FileHash -Algorithm SHA256 .\build\engram.exe
```

---

## What we prove vs. what we don’t claim (release honesty)

**We prove (reproducible on Windows + MSVC):**

- Native **R6 research gates** — run [`scripts/r6_eval_matrix.ps1`](scripts/r6_eval_matrix.ps1) locally; outputs go to **`artefacts/`** (gitignored). The last committed snapshots are **[`docs/R6_REPORT_OFFICIAL.md`](docs/R6_REPORT_OFFICIAL.md)** and **[`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)** (refresh when you cut a release).
- **B1 retention gauntlet**: after **1000** haystack SVO ingests, **first and last** needles remain reachable via the graph’s structural binding topology (not HuggingFace token-NIAH). Local output: `artefacts/b1_retention_niah.json` after you run the gate.
- **R3 Wasm closed-loop gate**: short **Wasm MDP** steps with world-model transition error and neuromod bounds checked ([`src/benchmark/r3_wasm_closed_loop_gate.cpp`](src/benchmark/r3_wasm_closed_loop_gate.cpp)).
- Live daemon targets a **~1 kHz** cognitive tick (`TICK_INTERVAL_US`); `/status` reports worst tick and ShadowBrain veto check durations (see [Benchmark definitions](#benchmark-definitions-repro)).
- **7-day soak (operator):** stable **working set** band over a week — documented in **[`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md)** (not a third-party audit; reproducible via the protocol there).

**We do not claim yet (without additional runs / baselines):**

- **“100% life-log retention”** or immunity to catastrophic forgetting in the ML sense—B1 is a **fixed structural receipt** at 1000 rules, not year-long PDF logs.
- **Beating 70B models on NIAH** or 10k+ haystack scale—see roadmap for scaling.
- **Cloud TTFT vs local veto** as a fair head-to-head—different workloads; any comparison needs an explicit protocol ([`docs/BENCHMARK_ROADMAP.md`](docs/BENCHMARK_ROADMAP.md)).
- **30+ day soak** with formal public logs—extend the same **`/status`** protocol in [`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md); **7 days** is recorded there as operator-verified flat RAM.

Badges above (**~3 MB RAM**, **248 KB binary**) describe a **baseline footprint story**, not a long-run maximum with a large graph and OS buffers—treat `/status` working set as the live metric when demoing.

---

## 🌟 What's New in v17.0?

### Phase 17: Voice & Identity (Personification)
Engram Core has a **voice** and a **self-concept**:
- **SAPI Voice**: Integrated Windows Text-to-Speech (Async). Engram Core speaks its thoughts out loud.
- **Identity System**: Bootstrapped with self-knowledge (graph seed; see `src/core/fpsan_identity.h`).
- **Social Response**: Handles greetings and "how are you" queries dynamically based on internal drive levels.

### Phase 16D: The Living Loop (Autonomy)
Engram Core is no longer purely reactive. It is driven by **Homeostatic Drives**:
- **Boredom**: Accumulates when idle, triggering spontaneous musing and random recall.
- **Curiosity**: Increases when unknown words are encountered, triggering Engram Core to ask YOU questions.
- **Engagement**: Modulates attentiveness based on your activity level.
- **Goal Planner**: Autonomous multi-step execution (e.g., "open notepad and type X").

### Phase 16B/C: Eyes & Metacognition
- **Foveated Visual Cortex**: Uses Win32 GDI to "see" the screen. Detects window switches and text entry via 8-level quantized temporal diffs.
- **Confidence Scoring**: Uses activation entropy at branching hub-nodes to calculate real-time confidence (0.00 to 1.00) in its own generated speech.

---

## 🎮 Quick Start: Talk to Engram Core

### Build & Run (Windows MSVC)
```powershell
# Launch Engram Core (builds then runs build\engram.exe)
run_engram.bat
```

Optional **batch ingest** (after `compile_only` or a full build): [`train_engram.bat`](train_engram.bat) (README + architecture + `training/general_knowledge.txt`), [`train_engram_sources.bat`](train_engram_sources.bat) (`src/`, `src/core/`). Both assume `build\engram.exe` exists and use repo-relative paths (no hardcoded VS install). Full **REPL command list**: [`docs/COMMANDS.md`](docs/COMMANDS.md).

Default brain file is **`engram_brain.fpsan`**. If you still have **`jarvis_brain.fpsan`** from an older build, the daemon loads it automatically until you save once (then prefer the new name). Optional periodic auto-save: set environment variable **`AUTO_SAVE_SECONDS`** to an integer **1–86400** before starting; unset or **`0`** keeps periodic save **off** (use `/save` or `/quit` as before).

### Core Commands
| Category | Commands |
|----------|----------|
| **Cognition** | `<sentence>` (Teach), `?<word>` (Generate), `??<word>` (Query associations) |
| **Living Loop** | `!drives` (Show curiosity/boredom), `!goal <text>` (Set autonomous task) |
| **Vision** | `!windows` (List open windows), `!verify` (Check if target window is focused) |
| **Voice** | `!mute` (Toggle speech), `!speak` alone (Test output) |
| **System** | `/status`, `/words`, `/save`, `/quit`, `/help` |

### Social Interaction
- "hello" / "who are you" / "what can you do"
- "how are you" (Engram Core will report boredom/curiosity levels)

### Research gates (R6) — reproduce
From the repo root (Windows, MSVC env via `scripts\vcvars_community.bat`):

```bat
scripts\compile_research_gates.bat
build\b1_retention_gauntlet.exe
build\r3_wasm_closed_loop_gate.exe
powershell -ExecutionPolicy Bypass -File scripts\r6_eval_matrix.ps1
powershell -ExecutionPolicy Bypass -File scripts\generate_r6_report.ps1
```

Optional local LLM TTFT notes: `scripts\baseline_phi3_latency.ps1` (placeholder; use your own Ollama / llama.cpp timings beside `/status` lines).

### Benchmark definitions (repro)

- **Environment**: Windows x64, **MSVC** via `scripts\vcvars_community.bat` (paths in [`scripts/compile_research_gates.bat`](scripts/compile_research_gates.bat)). **v1.0.0** committed R6 numbers were produced on an **HP EliteBook 850** (Core **i5**, **8 GB** RAM)—see **[`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)**. Gates do not require a GPU.
- **Full matrix**: `scripts\compile_research_gates.bat` then `powershell -ExecutionPolicy Bypass -File scripts\r6_eval_matrix.ps1` from repo root (append-only lines in `artefacts/r6_eval_matrix.txt`).
- **B1 only**: `build\b1_retention_gauntlet.exe` → refreshes `artefacts/b1_retention_niah.json`.
- **R3 only**: ensure `fixtures/phase_r3/micro_mdp.wasm` (run `scripts\build_phase_r3_wasm.bat` if missing), then `build\r3_wasm_closed_loop_gate.exe`.
- **Live daemon metrics**: run `build\engram.exe`, use `/status` for worst cognitive tick (µs) and worst ShadowBrain veto check (µs)—these are **not** the same metric as LLM time-to-first-token.

**Repository layout:** `artefacts/` and `*.fpsan` are **gitignored**—reproducible builds should regenerate them. Update **`docs/R6_REPORT_OFFICIAL.md`** when you want a frozen R6 narrative in Git. Optional **Python** helpers that emit files under **`data/`** are documented in [`tools/README.md`](tools/README.md) (not required for R6).

**Artefacts policy:** when you run the matrix script, `artefacts/r6_eval_matrix.txt` grows **append-only**. Rotate locally if needed; keep the README gate table aligned with reality.

### Industry benchmark mapping (positioning)
| FP-SAN phase | Industry analogue | What we measure **today** |
|--------------|-------------------|---------------------------|
| **B1 Retention Gauntlet** | NIAH-style **analogue** ([NeedleInAHaystack](https://github.com/gkamradt/LLMTest_NeedleInAHaystack)) | **1000** haystack SVO rules; **first/last** needle structural retrieval (`artefacts/b1_retention_niah.json`). Not token-for-token HF NIAH. |
| **B2 Veto / tick budget** | Agent safety / latency narratives | Live loop targets **~1 kHz**; ShadowBrain veto timing exposed in `/status`. No standalone “format blocked in &lt;2 ms” gate in-repo yet—see roadmap. |
| **B3 World / Wasm loop** | ARC / RL sample-efficiency stories (loose analogue) | **`r3_wasm_closed_loop_gate`**: few-step Wasm MDP + transition error + neuromod bounds—not BabyAI, not PPO step counts. |
| **B4 Soak / janitor** | Long-run reliability ([SWE-bench](https://github.com/swe-bench/SWE-bench)-style **positioning only**) | Design direction: janitor cap, `/status` working set. **No** committed multi-day soak log; do not claim fixed multi-GB RAM over weeks without data. |
| **Edge ML reference** | MLPerf Tiny ([mlcommons/tiny](https://github.com/mlcommons/tiny)) | Orthogonal stack; footprint badges vs microcontroller suites are **positioning**, not a submitted MLPerf score. |

### Soak test runbook (R4)
1. Build `run_engram.bat` (or `compile_only`) with **no** other process locking `build\engram.exe`. If MSVC reports **LNK1104**, exit `engram.exe` / Task Manager handles, then rebuild (see `run_engram.bat` header).
2. Run `build\engram.exe`; periodically run `/status` — note **Working set** and **Artefacts disk** vs the 50 MB janitor cap. Log multi-day runs using **[`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md)**.
3. After running gates locally: `scripts\r6_eval_matrix.ps1` and `scripts\generate_r6_report.ps1` → refresh **`docs/R6_REPORT_OFFICIAL.md`** before tagging a release.

---

## 📊 Benchmark Results (v17.0, May 2026)

### Native R6 research gates (reproducible)

Last matrix snapshot checked in: **2026-05-11** on an **HP EliteBook 850** (Core **i5**, **8 GB** RAM) — see **[`docs/R6_REPORT_OFFICIAL.md`](docs/R6_REPORT_OFFICIAL.md)** and **[`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)**. Regenerate locally under `artefacts/` with `scripts\r6_eval_matrix.ps1` (ignored in Git).

| Gate | Result |
|------|--------|
| `r0_baseline_gate` | PASS |
| `r1_neuromod_ablation_gate` | PASS |
| `r2_latent_gate` | PASS |
| `r3_world_gate` | PASS |
| `r3_wasm_closed_loop_gate` | PASS |
| `r5_identity_gate` | PASS |
| `b1_retention_gauntlet` | PASS |
| Transformer / cloud baseline | TBD (not invoked in-product; matrix line is honest placeholder) |

Reproduce: `scripts\compile_research_gates.bat` then `powershell -ExecutionPolicy Bypass -File scripts\r6_eval_matrix.ps1` (from repo root with MSVC env).

### Autonomy & Interaction (historical dev metrics)
| Metric | Result |
|--------|--------|
| **Generative Accuracy** | **4/4 PASS** — Phase 12 Benchmarks |
| **Language Stress Test** | **87% Retrieval** — 100-sentence battery |
| **Visual Temporal Diff** | **PASS** — Confirmed text-entry detection |
| **Metacognition Accuracy** | **PASS** — Distinguishes known vs unknown topics |
| **Speech** | **Async SAPI** — “&lt;2ms” in earlier notes referred to **trigger/latency of the local TTS path**, not LLM TTFT or cloud comparison |

### System (baseline snapshot; measure your build)
| Metric | Result |
|--------|--------|
| **Binary Size** | **248 KB** (typical release-ish build; varies with flags) |
| **RAM Footprint** | **~3.1 MB** (baseline story; **not** a long-run cap—use `/status`) |
| **Cognitive Tick Rate** | **1,000 Hz** target in the live loop |
| **CPU Usage** | **&lt;0.1%** (Idle, typical) |

---

## 🧠 Core Architecture

```
+---------------------------------------------+
|         Engram Core (FP-SAN v17.0)          |
+-------------+-------------------------------+
|  SENSORY    |  Foveated Vision (BitBlt)     |
|             |  Proprioceptive OS Tracker    |
+-------------+-------------------------------+
|  COGNITION  |  Metacognition (Entropy)      |
|             |  Homeostatic Drives (Boredom) |
+-------------+-------------------------------+
|  REASONING  |  Typed ClusterGraph (Slab)    |
|             |  Compositional Query Engine   |
+-------------+-------------------------------+
|  OUTPUT     |  Voice System (SAPI TTS)      |
|             |  Motor Cortex (SendInput)     |
+-------------+-------------------------------+
|  AGENCY     |  Goal Planner (Multi-step)    |
|             |  Spontaneous Behavior Engine  |
+-------------+-------------------------------+
|  MEMORY     |  Binary .fpsan Persistence    |
+-------------+-------------------------------+
```

### Key Source Files
| File | Purpose |
|------|---------|
| `src/fpsan_live_core.cpp` | Main daemon & Autonomous Living Loop |
| `src/core/fpsan_lexer.h` | Trie lexicon + Phrase grouping + Generation |
| `src/core/fpsan_screen_sensor.h`| Visual perception and window tracking |
| `src/core/fpsan_drives.h` | Curiosity, Boredom, and Goal Planning |
| `src/core/fpsan_voice.h` | Windows SAPI Text-to-Speech integration |
| `src/core/fpsan_identity.h` | Self-knowledge and social response logic |
| `src/core/cluster_graph.h` | The neuromorphic substrate (Nodes/Edges) |

---

## Future benchmarks (roadmap)

Planned extensions that tighten industry-facing narratives **with measured evidence**: [docs/BENCHMARK_ROADMAP.md](docs/BENCHMARK_ROADMAP.md) (B2 veto micro-gate, scalable B1 haystack, B4 soak logger). For **LinkedIn / lab outreach**, cite **`docs/R6_REPORT_OFFICIAL.md`**, local B1 JSON from a fresh run, and avoid claims not in those artefacts.

---

## 🗺️ Phase History
- **Phase 1-14**: Foundation, native C++ NLP, binary persistence, 1kHz live loop.
- **Phase 16B**: **Foveated Visual Cortex** — Engram Core "sees" the Windows environment.
- **Phase 16C**: **Metacognition** — Internal confidence scoring via entropy.
- **Phase 16D**: **The Living Loop** — Introduction of Homeostatic Drives and Autonomy.
- **Phase 17**: **Identity & Voice** — Personification via SAPI and self-concept.

## Pre-flight (before a public post or Release)

- [ ] **Clean clone** into a new folder; run `run_engram.bat compile_only`.
- [ ] Run `build\b1_retention_gauntlet.exe` — must exit 0; refresh local `artefacts/b1_retention_niah.json` if needed, then update narrative in README / `docs/R6_REPORT_OFFICIAL.md` as appropriate.
- [ ] Optional: `scripts\compile_research_gates.bat` and `scripts\r6_eval_matrix.ps1`, then refresh **`docs/R6_REPORT_OFFICIAL.md`** from the generated report.

## ⏭️ Next Steps
- **Phase 18: Reasoning Engine** — Implementing Binding Nodes for logical deduction and Transitive Inference.
- **Dynamic Goal Execution** — Closing the loop between Vision and Motor actions.