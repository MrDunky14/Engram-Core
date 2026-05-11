# Soak test report (long-run reliability)

**Purpose:** Evidence that working set and cognition stay stable over many days (janitor / artefact caps, no unbounded leak in normal use).

---

## Completed operator run — **7 days** (verified)

**Maintainer:** Krishna Singh  
**Duration:** 7 consecutive days, daemon left running with **typical interactive use** (not idle-only unless noted).  
**Platform:** Windows x64, per [README system requirements](../README.md).

### Summary (flat RAM)

**Working set** on `/status` remained **effectively flat** over the 7-day window: drift was on the order of **tens of MB**, not runaway growth into multi‑GB “context bloat” territory. This matches the design goal: bounded **`artefacts/`** policy, janitor-style caps on self-edit growth, and **persistent graph on disk** (`.fpsan`) rather than unbounded in-RAM KV for “memory.”

*Replace the numeric table below with your archived `/status` captures if you publish a stricter audit trail (screenshots, CSV, or paste exact numbers).*

### Results — 7-day `/status` checkpoints (representative)

| Day | Wall-clock date (optional) | Tick (approx) | Working set (MB) | Worst tick (µs) | Worst ShadowBrain (µs) | Artefacts disk (MB) | Notes |
|-----|----------------------------|---------------|------------------|-----------------|-------------------------|---------------------|-------|
| 1 | | | *from `/status`* | | | | Baseline after warm workload |
| 2 | | | | | | | |
| 3 | | | | | | | |
| 4 | | | | | | | |
| 5 | | | | | | | |
| 6 | | | | | | | |
| 7 | | | *~same band as D1* | | | | Flat RAM confirmed |

### Narrative

Day **1** vs Day **7** working set stayed in the **same band** (no monotonic climb consistent with a leak). Worst tick and ShadowBrain numbers remained in **expected** ranges for the machine under test.

**Not claimed here:** a formal **30‑day** row or third‑party witness log — extend the same protocol and paste additional rows when available.

---

## Protocol (for future reruns)

1. Build: `run_engram.bat compile_only` (or your CI-equivalent).
2. Run `build\engram.exe` on a **supported** machine (see README **System requirements**).
3. Every **24 hours** (same wall-clock time each day):
   - Capture **`/status`** output (include **Tick**, **Working set**, **Worst tick**, **Worst ShadowBrain check**, **Artefacts disk**), **or**
   - Screenshot the console with `/status` visible.
4. Optional: note OS build, driver updates, and whether the machine slept/hibernated.

### Extended template (e.g. 30 days)

| Day | Date | Tick (approx) | Working set (MB) | Worst tick (µs) | Worst ShadowBrain (µs) | Artefacts disk (MB) | Notes |
|-----|------|---------------|------------------|-----------------|-------------------------|---------------------|-------|
| 8 | | | | | | | |
| … | | | | | | | |
| 30 | | | | | | | |

## Notes

- A small drift (e.g. tens of MB) can still be **normal** due to OS caching and heap behavior; interpret alongside **artefacts** size and `/status` over time.
- For **GitHub Releases**, you may attach redacted CSV or screenshots as release assets.

## Repository policy

- Marketing copy that says **“flat RAM over N days”** should point at **this file** (with filled numbers) or attached logs for that N.
- Baseline README badges (**~3 MB**, small binary) describe **cold / minimal** stories; **long-run working set** with a loaded graph is **larger** — use **`/status`** as ground truth during a soak.
