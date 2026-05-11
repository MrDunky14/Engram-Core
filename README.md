# Engram Core

**FP-SAN v17.0 · C++17 · Windows x64**

Sparse, lock-free neuromorphic runtime: explicit **typed graph memory**, **structural motor policy**, **native MSVC codegen path**. No Python in the execution envelope. No cloud inference on the shipping path.

The dominant stack ties cognitive behavior to **GPU prefill**, **kilobyte-scale attention masks**, and **unbounded KV growth** in long sessions. Failures present as nondeterministic text, context overflow, and opaque address-space expansion. **Engram Core** is built for the opposite constraint class: **bounded RAM**, **deterministic hot paths**, **edge-local** execution, and **provenance-carrying edges** instead of frozen transformer state.

---

## Hardware sovereignty (EliteBook baseline)

All reference compilation, gates, and cited timings were executed on a **stock HP EliteBook 850**: **Intel Core i5**, **8 GB RAM**, **Windows x64**, **MSVC** toolchains. No discrete GPU in the reference profile. The design target is **laptop-class silicon**, not datacenter accelerators.

---

## Core architecture and features

**Zero-loss topological memory.** Persistent engrams via **EDGE_TEMPORAL** (SVO coincidence binding) and **EDGE_CAUSES** (forward causal priors). Addressing is **structural**, not a sliding context window; overflow is **out-of-band** (arena / fanout limits), not silent token eviction.

**Metamorphic hot-loading.** Host-authored **C++** → **`cl.exe`** → **`LoadLibrary`**; motor primitives wired through **EDGE_IMPLEMENTED_BY**. **Janitor GC** (R4 registry) enforces a **hard 50 MB** aggregate artefact ceiling (**4 MB**/object default), with episodic prune rules. Implementation: `src/core/fpsan_self_edit_registry.h`, `src/core/fpsan_metamorphic.h`.

**ShadowBrain firewall.** Air-gapped from network inference by construction: **motor-chain text policy** + **EDGE_REQUIRES** checks over a mirrored shadow graph before **`SendInput` / shell** dispatch. Latency class: **microseconds** on reference silicon (`/status` → worst ShadowBrain check). Specification: `SECURITY.md`.

**RPE neuromodulation.** **Reward prediction error** gates plasticity; **STDP-class** updates under neuromod parameters support **sample-efficient** behavior on **Wasm MDP** gates in the **R6** matrix (`r3_wasm_closed_loop_gate`, related harnesses under `src/benchmark/`).

---

## Brutal benchmarks (reference matrix)

Numbers below are **frozen EliteBook v1.0.0 narrative targets**. Re-verify on your host before contractual claims.

| Metric | Result |
|--------|--------|
| **Cognitive loop** | **1 kHz** target; **sub-1.5 ms** worst-case cognitive tick (see live **`/status`** worst-tick column). |
| **B1 NIAH gauntlet (memory)** | **1000** rules / **9000** triples ingested in **7.2 s**. **100%** retention (first + last needle, structural verification). **Retrieval latency: 0.0013 ms (1.3 µs)**. |
| **RAM footprint** | **~1.2 GB** stable working set under reference benchmark load (no KV-cache growth curve; measure **`/status`** working set during your run). |

**Contrast (operational LLM inference):** First-token latency is typically **10²–10³ ms** class on managed APIs; resident memory scales with **context + batch**, not with a fixed graph arena; output is **stochastic** under temperature. Engram Core trades generative breadth for **deterministic structure**, **fixed-budget persistence**, and **microsecond-class structural checks** on the paths that touch the OS.

Artifacts: `artefacts/b1_retention_niah.json` (gitignored), committed snapshots `docs/BENCHMARK_RESULTS_v1.0.0.md`, `docs/R6_REPORT_OFFICIAL.md`.

---

## Evaluator quick start

```powershell
git clone https://github.com/MrDunky14/Engram-Core.git
cd Engram-Core
.\run_engram.bat compile_only
```

Research gates (MSVC environment required; configure `scripts\vcvars_community.bat`):

```bat
scripts\compile_research_gates.bat
build\b1_retention_gauntlet.exe
```

Operator REPL reference: `docs/COMMANDS.md`. Threat model: `SECURITY.md`. Generated haystack for bulk ingest: `scripts\gen_b1_style_haystack.ps1`.

---

## License

**Apache License 2.0** — see `LICENSE`, `NOTICE`. Third-party: `CREDITS.md`.
