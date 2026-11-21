# 🧠 Engram-Core

**A Cache-Conscious Spreading-Activation Graph Store & Real-Time Edge Runtime in C++23**

[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Compiler](https://img.shields.io/badge/Compiler-MSVC_19.52_%7C_C%2B%2B23-blue.svg)](#)
[![Graph Ingest](https://img.shields.io/badge/Ingest_Throughput-22.9M_triples%2Fs-brightgreen.svg)](#)
[![2-Hop Traversal](https://img.shields.io/badge/2--Hop_Traversal-100_ns-darkblue.svg)](#)
[![Memory Working Set](https://img.shields.io/badge/Working_Set_%28RSS%29-47_MB-brightgreen.svg)](#)
[![Loop Clock](https://img.shields.io/badge/Loop_Clock-1%2C000_Hz_Hybrid-orange.svg)](#)
[![Python Dependencies](https://img.shields.io/badge/Python_Dependencies-0-brightgreen.svg)](#)

**Architected by Krishna Singh**

Engram-Core is a bare-metal, dependency-free C++23 knowledge-graph storage engine and deterministic spreading-activation substrate designed for low-latency edge AI systems. Rather than treating knowledge retrieval as a high-latency external service or relying on heavy database engines, Engram-Core implements a contiguous demand-paged flat arena with lock-free node allocation, atomic compare-and-swap (CAS) voltage accumulation, seqlock-protected edge snapshots, and a hybrid microsecond timer scheduler achieving **22.9M triples/sec ingestion**, **100 ns 2-hop relational traversal**, and **47 MB process working set** on standard edge hardware.

---

## ⚡ Core Systems Architecture

Engram-Core is built from the hardware level up for predictable memory access and sub-microsecond latency:

* **Contiguous Demand-Paged Arena (`ClusterGraph`):** Pre-allocates up to 262,144 nodes in a single cache-aligned region via Windows `VirtualAlloc` (`MEM_RESERVE | MEM_COMMIT`) / POSIX `mmap`. By utilizing OS virtual memory demand-paging without eager physical page-faulting, boot working set (RSS) remains bounded at **47 MB** across 6,500 active clusters, eliminating heap indirection while keeping hot graph structures resident in CPU L1/L2 caches.
* **Lock-Free Treiber Stack with Reader-Quiesced Node Reclamation:** Dynamic node allocation (`spawn()`) executes without global mutex locks via 64-bit tagged pointer CAS loops (`free_head`). Node retirement (`kill()`) performs an exclusive Read-Quiescence Synchronization (RQS) drain on `graph_rw_lock` to prevent ABA dangling edge reads, ensuring memory reclamation without race conditions.
* **C++23 Atomic Floating-Point Voltage Accumulation:** Spreading activation updates per-node floating-point activation (`std::atomic<float> activation`) via native C++20/C++23 `fetch_add(..., std::memory_order_acq_rel)` and `std::bit_cast` for bit-level topology hashing without pointer-aliasing undefined behavior.
* **Torn-Read Proof Seqlock & Snapshot Traversal:** Outbound edges and weights are mutated under per-node sequence counters (`edge_seq`). Readers acquire bounded local snapshots (`copy_edges_snapshot()`) without blocking graph mutators or observing torn 16-byte `Edge` structs during concurrent in-place edge sorting or STDP reinforcement.
* **Lossless Synaptic Persistence v3 (`.fpsan`):** High-speed binary persistence engine with full-file pre-verification, 64-bit FNV-1a checksum validation, and automatic dead slot recycling onto the Treiber stack. Corrupted files are rejected cleanly before mutating in-memory graphs.
* **Compact Flat Lexicon:** Replaced legacy multi-megabyte pointer structures with a cache-friendly, thread-safe `std::unordered_map` lexicon protected by `std::shared_mutex` and snapshot string accessors, cutting text subsystem RAM to `< 1 MB`.
* **Hybrid High-Resolution Windows Scheduler:** Bypasses Windows scheduler quantum limitations (which throttle standard `Sleep(1)` to ~64–100 Hz) by combining RAII-managed `CreateWaitableTimerExW` and `timeBeginPeriod(1)` with microsecond spin-wait tails (`YieldProcessor()`) to achieve true **1,000 Hz** tick loops with low scheduling jitter.

---

## 📊 Empirical Microarchitectural Benchmarks

All benchmarks were compiled with **MSVC 19.52.37029 (`/O2 /std:c++23`)** and executed natively on an **Intel Core i7-14700HX (28 threads)** running **Windows 11 x64**. Raw machine-readable receipts are generated in [`artefacts/systems_benchmark_results.json`](artefacts/systems_benchmark_results.json).

### 1. Ingestion Throughput & Relational Traversal (10,000 Triples)

Comparison of Engram-Core's contiguous arena against a dynamic Flat Graph (CSR-style), `std::unordered_map`, and embedded **SQLite 3.47.2 In-Memory (`:memory:`)**:

| Storage Engine | Ingestion Time | Ingestion Throughput | 1-Hop Lookup (p50) | 2-Hop Traversal (p50) | 2-Hop Traversal (p99) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Engram-Core `ClusterGraph`** | **0.88 ms** | **22,851,920 op/s** | **100.0 ns** | **100.0 ns** | **300.0 ns** |
| **FlatGraph (Dynamic CSR-like)** | 0.53 ms | 37,814,332 op/s | < 10.0 ns | < 10.0 ns | 100.0 ns |
| **`std::unordered_map`** | 1.56 ms | 12,828,736 op/s | 100.0 ns | 100.0 ns | 200.0 ns |
| **SQLite3 In-Memory (`:memory:`)** | 7.47 ms | 2,676,015 op/s | 400.0 ns | 700.0 ns | 800.0 ns |

> **Systems Takeaway:** Engram-Core's flat arena achieves **7.0× lower traversal latency** (`100 ns` vs. `700 ns`) and **8.5× higher ingestion throughput** than in-memory SQLite by eliminating SQL query compilation, B-tree rebalancing, and virtual machine dispatch overhead.

---

### 2. Spreading Activation Depth Scaling

Empirical measurement of voltage propagation across a balanced tree with fanout 4:

| Traversal Depth | Active Nodes Activated | Total Latency (p50) | Effective Latency per Activated Node |
| :---: | :---: | :---: | :---: |
| **Depth 1** | 1 | 100.0 ns | 100.00 ns |
| **Depth 2** | 5 | 200.0 ns | 40.00 ns |
| **Depth 3** | 21 | 600.0 ns | 28.57 ns |
| **Depth 4** | 85 | 2,200.0 ns | 25.88 ns |
| **Depth 5** | 341 | 8,700.0 ns | **25.51 ns** |

> **Microarchitectural Profile:** As activation spreads across deeper graph neighborhoods, cache locality stabilizes at an asymptotically flat **`~25.5 ns` per activated node**, demonstrating sustained L1/L2 prefetch efficiency.

---

### 3. Windows Scheduling Quantum & Timer Resolution (1,000 µs Target)

Windows thread scheduling defaults to a `15.625 ms` quantum, preventing standard POSIX/Win32 sleep calls from achieving high-frequency loops. Below is the empirical comparison of 500 loop iterations targeting a 1,000 Hz (1,000 µs) tick rate:

| Scheduling Strategy | Mean Tick | p50 Tick | p99 Tick | Effective Loop Frequency |
| :--- | :---: | :---: | :---: | :---: |
| **1. Default `Sleep(1)`** | 10,242.8 µs | 7,806.2 µs | 19,983.4 µs | **97.6 Hz** |
| **2. `timeBeginPeriod(1)` + `Sleep(1)`** | 1,844.2 µs | 1,996.7 µs | 2,524.4 µs | **542.2 Hz** |
| **3. High-Resolution Waitable Timer** | 1,360.7 µs | 1,510.6 µs | 1,605.4 µs | **734.8 Hz** |
| **4. Hybrid Timer + Spin-Wait Tail** | **1,050.0 µs** | **1,025.2 µs** | **1,313.7 µs** | **~952 – 1,000 Hz** |

> **Real-Time Loop Finding:** Coarse sleep alone cannot achieve 1,000 Hz on Windows due to context-switch scheduling penalties. Engram-Core implements a **Hybrid Timer Strategy** (`precise_sleep_us`): coarse sleep for `interval - 200 µs` via high-resolution waitable timers, followed by a fine-grained `YieldProcessor()` spin-wait tail to absorb thread quantum jitter.

---

### 4. Perception Bottleneck: Linear Vector Scan vs. Direct Hash Index

Engram-Core incorporates a dual perception pipeline: an experimental 256-bit spiking Hamming similarity matcher alongside an exact string-label index:

| Perception Mechanism | Mean Latency | p50 Latency | Throughput |
| :--- | :---: | :---: | :---: |
| **`LanguageCortex` (3k Linear 256-bit Scan)** | 1,996.5 µs | 1,980.2 µs | 501 words/s |
| **Direct Hash Index (`word_cache`)** | **11.5 ns** | **< 10.0 ns** | **86,956,522 words/s** |

> **Optimization:** Introducing an $O(1)$ fast-path direct hash cache alongside the continuous neuromorphic cluster representations reduced repeated vocabulary resolution from `2.0 ms` down to sub-microsecond latency, accelerating sentence ingestion by **> 170,000×**.

---

### 5. Multi-Threaded Concurrency, Invariant Integrity & Working Set (Gate E1)

Gate E1 subjects Engram-Core to an 8-thread concurrent contention gauntlet (4 reader threads executing continuous spreading activation and lexicon perception, and 4 mutator threads executing neurogenesis `spawn()`, node recycling `kill()`, edge mutation, and STDP weight updates):

| Microarchitectural Metric | Invariant / Target Ceiling | Empirical Measurement | Verdict |
| :--- | :---: | :---: | :---: |
| **Active Process Working Set (RSS)** | < 50.0 MB | **47.1 MB** (VirtualAlloc Demand-Paged) | **PASS** |
| **8-Thread Contention Throughput (3.0s)** | > 5,000,000 op/s | **9,524,529 op/s** (28,573,589 total ops) | **PASS** |
| **Torn Edge Invariant Violations** | 0 violations | **0 observed** (Seqlock + Snapshot readers) | **PASS** |
| **Data Races & Memory Corruption** | 0 races | **0 data races** (`/std:c++23` atomics) | **PASS** |
| **Persistence Round-Trip Integrity (v3)** | Bit-for-bit lossless | **100% Canary, Provenance & Flags Preserved** | **PASS** |
| **Corrupted File Rejection** | Fail loudly & preserve memory | **Checksum Mismatch Caught** (Zero state wipe) | **PASS** |

> **Concurrency Takeaway:** Replacing lock-free torn reads with versioned seqlock snapshots and upgrading floating-point voltage accumulation to native C++23 atomic arithmetic completely eliminates concurrency data races and torn struct reads under extreme multi-threaded contention while sustaining **~9.5M ops/sec**.

---

## 🚀 Quick Start Guide

Engram-Core requires **Windows 10/11 x64** and **Visual Studio 2022/2026 (MSVC x64)**. Zero external package managers or Python runtimes are required.

### 1. Build and Run Gate E1 Concurrency & Persistence Suite

Verify multi-threaded contention, seqlock reader snapshots, memory RSS, and lossless v3 persistence:

```cmd
git clone https://github.com/MrDunky14/Engram-Core.git
cd Engram-Core
call scripts\vcvars_community.bat
nmake gate_e1
build\gate_e1_concurrency_and_persistence.exe
```

### 2. Build and Run the Systems Benchmark Suite

The systems benchmark compiles natively and validates all microarchitectural findings against in-memory SQLite:

```cmd
nmake systems_benchmark
build\systems_benchmark.exe
```

### 3. Verify Research Gates & Heartbeat Jitter

Compile and run the research verification matrix and multi-thread heartbeat gate:

```cmd
scripts\compile_research_gates.bat
build\b1_retention_gauntlet.exe
build\heartbeat_gate.exe
```

### 4. Build & Launch the Interactive Daemon

Build the standalone living engine executable:

```cmd
run_engram.bat compile_only
build\engram.exe
```

Inside the console:
```text
> /train data/demo_video_axioms.txt
> /status
> !goal format C:
```
*Expected Veto: `[ShadowBrain] VETO — disk/OS safety policy blocked destructive motor sequence.`*

---

## 🛡️ Safety & Execution Boundary

* **Thread Priority:** Elevates to `HIGH_PRIORITY_CLASS` during real-time cognitive cycles to maintain predictable tick latency.
* **Air-Gapped Operation:** Designed for isolated, offline edge execution. Public socket binding is unsupported.
* **Deterministic Veto Layer:** Intercepts high-risk OS motor commands (`format`, `diskpart`, `cipher /w`) via structural dependency checks (`EDGE_REQUIRES`) and string safety filters before OS dispatch.
* **Emergency Sever Switch:** Global ESC-key handler severs active motor execution threads immediately.

---

## 🏗️ Repository Layout & Documentation

* **Operator Commands:** [`docs/COMMANDS.md`](docs/COMMANDS.md)
* **Architecture Deep Dive:** [`FP-SAN Architecture.md`](FP-SAN Architecture.md)
* **Verified Benchmark Snapshot:** [`docs/BENCHMARK_RESULTS_v1.0.0.md`](docs/BENCHMARK_RESULTS_v1.0.0.md)
* **Soak Test Evidence:** [`docs/SOAK_TEST_REPORT.md`](docs/SOAK_TEST_REPORT.md)
* **Security & Vulnerability Reporting:** [`SECURITY.md`](SECURITY.md)

---

**Copyright © 2026 Krishna Singh. Released under the Apache 2.0 License.**  
*Third-party acknowledgments: Wasm3 ([`CREDITS.md`](CREDITS.md)) and SQLite ([`NOTICE`](NOTICE)).*
