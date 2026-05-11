# Engram Core

**Release v1.0.0 · FP-SAN v17.0 · C++17 · Windows x64**

Initial production drop: a **bare-metal, lock-free** cognitive runtime for **edge sovereignty**. The engine **does not** use attention-based prefill; world state and procedure are carried in a deterministic **EDGE_PROVENANCE** topological graph (`ClusterGraph` + typed edges). Execution envelope: **native C++**, **Windows**, **no Python** on the operator path, **no** cloud inference on the shipping path.

Industry-standard agents bind cognition to **datacenter GPUs**, **Python orchestration**, and **stochastic decoding**. Failure modes: hallucinated tool calls, unbounded **KV / context** growth, nondeterministic latency. Engram Core targets **fixed working-set bands**, **structural recall**, and **microsecond-class** policy on code paths that invoke the OS.

---

## Baseline hardware validation

Engineered, compiled, and benchmarked on an **HP EliteBook 850**: **Intel Core i5**, **8 GB system RAM**, **Windows x64**, **MSVC**. **Discrete/usable graphics budget capped at 4 GB VRAM** on the validation platform (integrated/shared memory profile); the **reference cognitive path is CPU-native**—no GPU weight load in the daemon loop.

---

## Core capabilities (v1.0.0)

- **Metamorphic hot-loading** — Dynamic **C++** payload generation, **`cl.exe`** compile, and **DLL** hot-link at runtime. Artefacts registered under **R4 self-edit** with a **Janitor GC** enforcing a **hard 50 MB** aggregate cap (default **4 MB**/object). See `src/core/fpsan_self_edit_registry.h`, `src/core/fpsan_metamorphic.h`.

- **ShadowBrain veto** — **Structural** interception of unsafe **motor-chain** plans (policy on flattened motor text + **EDGE_REQUIRES** shadow mirroring) before **SendInput** / shell execution. Latency class **microseconds** on reference silicon; telemetry: **`/status`** → worst ShadowBrain check. Specification: `SECURITY.md`.

- **RPE neuromodulation** — **Reward Prediction Error**–aligned gating drives plasticity scaling; **Wasm MDP** closed-loop gate (`r3_wasm_closed_loop_gate`) demonstrates **sample-efficient** world-model tracking in a **low–episode-count** harness (see `src/benchmark/r3_wasm_closed_loop_gate.cpp`).

- **Zero-loss topological memory (B1 class)** — **1000** haystack rules / **9000** triples; retention verified by **first/last needle** structural reachability (not HF token-NIAH). Live recall: **`??`** queries in the REPL; harness: `build\b1_retention_gauntlet.exe`.

---

## Brutal benchmarks (EliteBook reference matrix)

| Metric | Engram Core result |
| :--- | :--- |
| **Benchmark B1 (topological NIAH)** | **1000** haystack rules / **9000** triples |
| **Memory retention** | **100 %** (zero-loss on first/last needle gate) |
| **Cognitive loop** | **1000 Hz** target (**&lt; 1.5 ms** worst-case tick — confirm with live **`/status`**) |
| **Ingest total time** | **7.22 s** (B1 harness, reference run) |
| **Needle retrieval time** | **0.0013 ms** (**1.3 µs**) structural verification interval |
| **System RAM footprint** | **~1.2 GB** flat working set (reference load; measure **`/status`** on your host) |

**Modern LLM inference (contrast):** managed APIs typically exhibit **100–500 ms+** time-to-first-token class behavior on general prompts; resident memory scales with **context length × batch**; safety is often **prompt-layer** policy, not **motor-chain** structural veto. Engram Core optimizes for **deterministic graph walks**, **bounded artefacts**, and **auditable** motor policy.

Committed receipts: `docs/BENCHMARK_RESULTS_v1.0.0.md`, `docs/R6_REPORT_OFFICIAL.md`. Local JSON: `artefacts/b1_retention_niah.json` (gitignored; regenerate with the B1 executable).

---

## Brain persistence (`.fpsan`)

| File | Role |
| :--- | :--- |
| **`engram_brain.fpsan`** | **Default** persistence path (**process working directory** when you start `engram.exe`). Written by **`/save`**, **`/quit`**, and optional `AUTO_SAVE_SECONDS`. |
| **`jarvis_brain.fpsan`** | **Legacy** filename; still **loaded** if present and the preferred file is missing, until you **`/save`** once. |

Both patterns are **gitignored** (`*.fpsan`). **Back up** before OS reinstall; treat as **opaque binary** snapshots of graph + cortex state.

---

## Security and integrity

**Release archive:** `EngramCore_v1.0.0_win-x64.zip`  
**SHA-256:** `B0FF1F6853E929165C15868AC8F651B864A998CB672982A724A2E8AC5632F811`

*Matches `artefacts\EngramCore_v1.0.0_win-x64.zip` produced by `scripts\package_release_zip.ps1` with README at this revision. Re-pack changes timestamps and may change the digest; always verify with `Get-FileHash` on the binary you received.*

Verify before trusting a downloaded zip:

```powershell
Get-FileHash -Algorithm SHA256 .\artefacts\EngramCore_v1.0.0_win-x64.zip
```

Reproduce the package from a **trusted** source tree (after `.\run_engram.bat compile_only`):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_release_zip.ps1
```

The script writes **`artefacts\EngramCore_v1.0.0_win-x64.zip`** and prints **SHA-256**.

---

## Evaluator quick start

```powershell
git clone https://github.com/MrDunky14/Engram-Core.git
cd Engram-Core
.\run_engram.bat compile_only
```

Binary drop (after `compile_only`):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_release_zip.ps1
```

Research gates (MSVC env — edit `scripts\vcvars_community.bat`):

```bat
scripts\compile_research_gates.bat
build\b1_retention_gauntlet.exe
```

Operator reference: `docs/COMMANDS.md`. Threat model: `SECURITY.md`.

---

## License

**Apache License 2.0** — see `LICENSE`, `NOTICE`. Attribution: `CREDITS.md`.
