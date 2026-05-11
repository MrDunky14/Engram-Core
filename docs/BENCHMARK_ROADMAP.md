# Benchmark roadmap (evidence-backed “Brutal Benchmarks”)

This document tracks **planned** harnesses that would strengthen claims you might make to labs or on LinkedIn. Nothing here is promised as shipped until it exists under `src/benchmark/` and is wired into [`scripts/r6_eval_matrix.ps1`](../scripts/r6_eval_matrix.ps1) (or a sibling script).

## B2: Veto micro-benchmark gate

**Goal:** A standalone executable (or gated section) that feeds a **known-unsafe** motor / plan string through the same policy path as the live daemon’s ShadowBrain veto, measures wall time over N iterations, and **fails** if any sample exceeds a documented threshold (e.g. 2 ms).

**Acceptance:**

- Deterministic inputs (no real disk side effects).
- Exit `0` only if all checks pass; prints p50/p99 timing to stdout and optionally `artefacts/b2_veto_latency.json`.

**Dependencies:** Factor or duplicate the minimal veto check from [`src/fpsan_live_core.cpp`](../src/fpsan_live_core.cpp) / motor safety helpers so gates do not need the full UI loop.

## B1: Scalable haystack

**Goal:** Make [`src/benchmark/b1_retention_gauntlet.cpp`](../src/benchmark/b1_retention_gauntlet.cpp) haystack size configurable (CLI arg or `#define` / env) so you can scale **1000 → 10000+** rules when desktop RAM allows, still verifying first/last (and optionally random spot-checks).

**Acceptance:**

- Document max tested machine (RAM) beside the JSON artefact.
- Keep the honest label: **structural** graph retrieval, not HF NIAH token count.

## B4: Soak logger

**Goal:** A script or small C++ helper that samples **working set / private bytes** (and optional artefacts folder size) on an interval while `engram.exe` runs, appends CSV locally, and produces a one-page summary for multi-hour or multi-day runs.

**Acceptance:**

- No false claim of “30-day flat 1.2 GB” until a log is committed or released as an artefact with methodology.
- README footprint badges remain **baseline**; soak CSV is the **long-run** story.

## LinkedIn / GitHub messaging

- Prefer: reproducible commands, dated `artefacts/r6_eval_matrix.txt` tail, and `b1_retention_niah.json` fields.
- Avoid: comparing ShadowBrain µs to cloud **TTFT** without an explicit paired experiment; claiming **catastrophic forgetting** immunity from B1 alone.
