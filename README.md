# 🧠 Engram Core
**A Sovereign, Lock-Free C++ Cognitive Architecture for Extreme Edge AI**

[![Release v1.0.0](https://img.shields.io/badge/Release-v1.0.0-blue.svg)](#) [![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE) [![Hardware Baseline](https://img.shields.io/badge/Target-Intel_i5_%7C_8GB_RAM-darkred.svg)](#)
[![Footprint](https://img.shields.io/badge/RAM-~3MB_baseline-red.svg)](#) [![Binary Size](https://img.shields.io/badge/Binary-248KB-orange.svg)](#) [![Python Dependencies](https://img.shields.io/badge/Python_Dependencies-0-brightgreen.svg)](#)

**Architected by Krishna Singh**

Engram Core (**FP-SAN v17.0**) is a bare-metal biomimetic daemon designed to challenge datacenter-scale attention models. It runs entirely offline on standard Windows edge hardware, executing a continuous, living cognitive loop. By replacing the ephemeral "Context Window" with a purely deterministic, 1,000 Hz topological graph, it achieves zero-loss memory retention, microsecond safety vetos, and autonomous, multi-step goal execution without the latency, bloat, or cloud dependency of traditional LLMs.

---

## ⚡ Core Architecture: The Sovereign Engine

Engram Core is a persistent process, not a hosted chatbot. It is built on an **FP-SAN (Fixed-Point Spiking Asynchronous Network)** featuring LIF-style spiking physics, typed neuromorphic graph memory, and native Windows perception.

* **Zero-Loss Topological Memory:** Knowledge is stored in explicit `EDGE_PROVENANCE` physical graph structures. Retrieval relies on structured, deterministic walks—not a frozen, probabilistic transformer state.
* **The ShadowBrain Veto:** An air-gapped cognitive firewall. It structurally traverses potential motor-chain actions and intercepts risky OS-level commands (e.g., `format C:`) before they execute, achieving µs-class latency on reference hardware.
* **RPE Neuromodulation:** Employs biologically aligned Reward Prediction Error (RPE) signaling. STDP learning is gated by "Dopamine"—the system only commits energy to memory when its World Model prediction fails, driving true sample efficiency.
* **Autonomous Agency & Homeostasis:** Operates continuously via internal drives (boredom, curiosity, engagement) to form multi-step goals without requiring constant user prompting.
* **Metamorphic Hot-Loading:** Generates native C++ templates, invokes `cl.exe`, and dynamically links payload DLLs directly into its motor cortex in real-time, managed by a Janitor GC.

---

## 💻 Hardware Sovereignty: The EliteBook Baseline

**Reference Environment:** Engineered, gated, and measured on an **HP EliteBook 850** (Intel Core i5, 8GB RAM, GPU-free).

Engram Core was forged under strict enterprise hardware constraints. There is no GPU acceleration masking inefficient code. Every microsecond of tick latency and every megabyte of RAM was optimized in pure C++ to prove that high-level reasoning and zero-loss memory can execute entirely on standard edge devices.

---

## 📊 Brutal Benchmarks (The Receipts)

Below is the verified performance snapshot from the R6 automated research suite on the reference hardware. 
*(Full details: [`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md))*

| Metric | Engram Core Result | Traditional LLM Comparison |
| :--- | :--- | :--- |
| **B1 Topological Retention** | **PASS (100% Structural Recall)** | Context-bound degradation (~85-95%) |
| **B1 Retrieval Latency** | **~1.3 µs (Verify Step)** | Streaming TTFT Bound (500ms+) |
| **Cognitive Loop Rate** | **~1,000 Hz Target** | High-latency turn-based API |
| **Long-Run Reliability** | **7-Day Soak Protocol (Stable)** | Context bloat / OOM risk |

> **B1 NIAH Gauntlet Validation:** 1,000 Haystack SVO Rules Ingested | First & Last Needle Verified | Exit OK

---

## 🚀 Evaluator's Quick Start Guide

Engram Core is designed for rigorous, reproducible evaluation. No cloud inference. No Python runtime.

**1. Clone & Build (Windows 10/11 x64 + MSVC required)**
Ensure your MSVC environment is active (`scripts\vcvars_community.bat`):
```cmd
git clone [https://github.com/MrDunky14/Engram-Core.git](https://github.com/MrDunky14/Engram-Core.git)
cd Engram-Core
run_engram.bat compile_only

```

**2. Verify the R6 Matrix & B1 Gauntlet**
Execute the native benchmark suite to validate the 100% retention and retrieval metrics on your machine:

```cmd
scripts\compile_research_gates.bat
build\b1_retention_gauntlet.exe
powershell -ExecutionPolicy Bypass -File scripts\r6_eval_matrix.ps1

```

**3. Run the Living Daemon**
Boot the cognitive loop, prime the graph, and test the telemetry:

```cmd
run_engram.bat
> /train data/demo_video_axioms.txt
> /status 
> !goal format C:

```

*Expected Veto: `[ShadowBrain] VETO (check took X us)*`

---

## 🛡️ Threat Model & Security Posture

Engram Core executes with OS-level sovereignty.

* **Intended Use:** Single-user, air-gapped desktop intelligence.
* **Out of Scope:** Binding the daemon to public sockets is strictly unsupported and voids the safety posture.
* **Safeguards:** Microsecond structural **ShadowBrain** policy interception and a global **ESC-key kill switch** to sever the motor thread instantly.
*(Review [`SECURITY.md`](SECURITY.md) before deployment.)*

---

## 🏗️ Repository Layout & Documentation

* **Operator Reference:** [`docs/COMMANDS.md`](https://www.google.com/search?q=docs/COMMANDS.md)
* **Architecture Deep Dive:** [`FP-SAN Architecture.md`](FP-SAN Architecture.md)
* **Soak Test Evidence:** [`docs/SOAK_TEST_REPORT.md`](https://www.google.com/search?q=docs/SOAK_TEST_REPORT.md)

---

**Copyright © 2026 Krishna Singh. Released under the Apache 2.0 License.**
*See [`NOTICE`](https://www.google.com/search?q=NOTICE) and [`CREDITS.md`](https://www.google.com/search?q=CREDITS.md) for third-party attributions (e.g., Wasm3).*

```

```
