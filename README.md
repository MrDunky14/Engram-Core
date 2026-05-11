# Engram Core · FP-SAN v17.0 — release v1.0.0

**A sovereign, lock-free C++ cognitive architecture for extreme edge AI.**

[![C++17](https://img.shields.io/badge/C++-17-blue.svg)]()
[![Footprint](https://img.shields.io/badge/RAM-~3MB_baseline-red.svg)]()
[![Binary](https://img.shields.io/badge/Binary-248KB-orange.svg)]()
[![Latency](https://img.shields.io/badge/Reasoning_P99-92μs-blueviolet.svg)]()
[![Dependencies](https://img.shields.io/badge/Python_Dependencies-0-brightgreen.svg)]()
[![Daemon](https://img.shields.io/badge/Mode-Autonomous_Living_Loop-green.svg)]()

Much of the AI stack targets **datacenter-scale attention**—high latency, fixed context, opaque memory growth. **Engram Core** is different: a **biomimetic cognitive daemon** in **pure C++**, built to show that a **persistent, local, graph-first agent** can run on ordinary **Windows** laptops with **no cloud inference** on the shipping path and **no runtime Python** for build + run.

**FP-SAN** (Fixed-Point Spiking Asynchronous Network): **LIF-style spiking physics**, **typed neuromorphic graph memory**, **homeostatic drives**, and **native Windows perception + voice**. **Living loop** with **~1 kHz** tick *target* — use **`/status`** for real numbers on your machine.

---

## Core architecture

Engram Core is **not** a hosted chatbot; it is a **persistent process** on your machine.

| Capability | Notes |
|------------|--------|
| **Topological memory** | Knowledge in explicit nodes/edges (provenance). Retrieval is structured walks, not a frozen transformer state. |
| **ShadowBrain** | Fast checks before risky motor plans hit the OS — see [`SECURITY.md`](SECURITY.md); **`/status`** shows worst veto latency (µs-class on reference HW). |
| **Neuromodulation & RPE-style signaling** | Drives learning/gating; Wasm closed-loop gates in R6. |
| **Vision & SAPI** | GDI/UIA screen context; asynchronous **Windows TTS** so speech does not stall the tick. |
| **Homeostatic drives** | Boredom, curiosity, engagement, **multi-step goals** — behavior without constant prompting. |

Deeper design: **[`FP-SAN Architecture.md`](FP-SAN Architecture.md)** · Operator reference: **[`docs/COMMANDS.md`](docs/COMMANDS.md)**

---

## Hardware reference (v1.0.0 snapshot)

Engineering and gate snapshots used an **HP EliteBook 850** (**Intel i5**, **8 GB RAM**) as the **GPU-free** baseline. **Your** machine may differ — rerun gates and treat **`/status`** as ground truth. Committed numbers: **[`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)**.

| Dimension | Engram Core (gated / measured) | Cloud LLM chat (illustrative) |
| :--- | :--- | :--- |
| **Structural retention (B1)** | PASS: first & last needle after **1000** haystack SVO rules; **structural** retrieval — not token-length NIAH | Varies by model and harness |
| **B1 verify step** | **~1.3 µs** class on reference run — see benchmark doc | N/A |
| **Live cognitive loop** | **~1 kHz** target; worst tick & veto from **`/status`** | Request / streaming bound |
| **Long-run RAM** | **7-day** soak *protocol* — [`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md) | Product-dependent |

Badge RAM/binary/latency figures are **baseline stories**; demos should show **`/status`** with a realistic graph load.

---

## Quick start

**Windows 10/11 x64** + **MSVC** — set **`scripts\vcvars_community.bat`** to your VS install.

```powershell
git clone https://github.com/MrDunky14/Engram-Core.git
cd Engram-Core
.\run_engram.bat
```

| | |
|--|--|
| **Build only** | `.\run_engram.bat compile_only` |
| **Brain file** | **`engram_brain.fpsan`** · legacy **`jarvis_brain.fpsan`** still loads until you **`/save`** once |
| **Optional autosave** | Env **`AUTO_SAVE_SECONDS`** (1–86400) |
| **Demo scripts** | [`scripts/demo_impossible_sequence.bat`](scripts/demo_impossible_sequence.bat) · [`scripts/demo_sovereignty_prep.bat`](scripts/demo_sovereignty_prep.bat) |
| **Bulk ingest** | **`/train <file>`** — e.g. `data/demo_video_axioms.txt`; generate **1000-line haystack** with **`scripts\gen_b1_style_haystack.ps1`** before **`/train data/b1_style_haystack_1000.txt`** |

**Documentation index:** [`docs/README.md`](docs/README.md)

---

## Verification (R6 + B1)

```bat
scripts\compile_research_gates.bat
build\b1_retention_gauntlet.exe
powershell -ExecutionPolicy Bypass -File scripts\r6_eval_matrix.ps1
powershell -ExecutionPolicy Bypass -File scripts\generate_r6_report.ps1
```

B1 writes **`artefacts/b1_retention_niah.json`** (gitignored). Committed summaries: **[`docs/R6_REPORT_OFFICIAL.md`](docs/R6_REPORT_OFFICIAL.md)**.

---

## Repository layout

```text
Engram-Core/
├── scripts/                  # MSVC env, compile gates, R6 matrix, reports, demo helpers
├── src/
│   ├── fpsan_live_core.cpp   # Daemon — living loop
│   ├── core/                 # Graph, drives, voice, vision, Wasm sandbox, …
│   └── benchmark/            # R0–R5, B1, Wasm gates
├── docs/                     # Commands, benchmarks, soak — docs/README.md
├── data/                     # Fixtures; large drops gitignored — data/README.md
├── fixtures/                 # e.g. phase_r3 Wasm
├── vendor/wasm3/             # Wasm3 upstream — CREDITS.md
├── run_engram.bat
├── train_engram.bat
└── train_engram_sources.bat
```

---

## Threat model & what we do not claim

**Security:** **[`SECURITY.md`](SECURITY.md)** — ShadowBrain veto, ESC kill switch. **Binding the daemon to public sockets** is **unsupported** for the stated safety posture.

**We do not claim:** multi-year “life log” retention from B1 alone; beating **70B** models on open-ended chat; fair **TTFT vs ShadowBrain** without a published paired protocol. Scope detail: **[`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)**.

---

## System requirements

| | |
|---|---|
| **OS** | Windows **10 / 11** (x64). |
| **CPU** | x86_64; **AVX2** recommended. |
| **RAM** | **8 GB** minimum (supported floor). |
| **Runtime** | **UIA** + **SAPI** (ship with Windows). |

---

## REPL essentials

| Category | Examples |
|----------|----------|
| **Cognition** | `<sentence>`, `?<word>`, `??<word>` |
| **Living loop** | `!drives`, `!goal <text>` |
| **Vision** | `!windows`, `!see`, `!verify` |
| **Voice** | `!mute`, `!speak` |
| **System train** | **`/train`** *file*, **`/save`**, **`/status`**, **`/help`**, **`/quit`** |

---

## Architecture (block)

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
|  OUTPUT     |  SAPI · motor (SendInput)     |
+-------------+-------------------------------+
|  AGENCY     |  Goal planner · spontaneity    |
+-------------+-------------------------------+
|  MEMORY     |  .fpsan persistence           |
+-------------+-------------------------------+
```

| File | Role |
|------|------|
| `src/fpsan_live_core.cpp` | Daemon · main loop |
| `src/core/cluster_graph.h` | Graph substrate |
| `src/core/fpsan_lexer.h` | Lexicon · generation |
| `src/core/fpsan_screen_sensor.h` | Vision |
| `src/core/fpsan_drives.h` | Drives · goals |
| `src/core/fpsan_voice.h` | TTS |
| `src/core/fpsan_wasm_sandbox.*` | Wasm3 |

---

## R6 gate snapshot (committed)

**Hardware:** HP EliteBook 850 (i5, 8 GB). Detail: [`docs/R6_REPORT_OFFICIAL.md`](docs/R6_REPORT_OFFICIAL.md).

| Gate | Result |
|------|--------|
| `r0_baseline_gate` | PASS |
| `r1_neuromod_ablation_gate` | PASS |
| `r2_latent_gate` | PASS |
| `r3_world_gate` | PASS |
| `r3_wasm_closed_loop_gate` | PASS |
| `r5_identity_gate` | PASS |
| `b1_retention_gauntlet` | PASS |

---

## License

**[Apache License 2.0](LICENSE)** — **[NOTICE](NOTICE)** · **[CREDITS.md](CREDITS.md)** (e.g. **Wasm3**, MIT).
