# Engram Core — command reference

*Engine **FP-SAN v17.0** · public release **v1.0.0** (see [README](../README.md)).*

Typed at the **`you >`** prompt unless noted. Slash commands start with **`/`**; motor and shortcuts with **`!`**; generation/query with **`?`**.

---

## Natural language (default)

| Input | What it does |
|--------|----------------|
| **Plain sentence** | Full **reasoning cycle**: ingests triples into the graph, updates drives/neuromod, generates a reply (not a dumb echo). This is the main “teach + converse” path. |

---

## Slash — system

| Command | What it does |
|---------|----------------|
| **`/help`** | Full categorized help (same information as this doc, in the console). |
| **`/status`** | **Telemetry table**: tick, ~uptime, nodes, edges, words, neuromod, **working set (MB)**, artefacts disk size, **worst tick µs**, **worst ShadowBrain check µs**, brain file path. Your best “systems flex” in demos. |
| **`/dashboard`** | R0 pillar telemetry (tiers, edge mix, neuromod counters). |
| **`/save`** | Saves brain via `SynapticMemory::sleep` (`engram_brain.fpsan` or legacy path). Voice line + sleep metaphor. |
| **`/load`** | Loads brain from disk; wipes/resets if missing file. |
| **`/train `** *file* | Ingest a single file (docs/code). |
| **`/train_dir `** *dir* | Recursively ingest text/code from a directory. |
| **`/research `** *goal* | Start **async** background research (plan + fetch pipeline). |
| **`/research_run`** | Run next research step (local). **`/research_run `** *net* — include network fetch when implemented. |
| **`/research_status`** | Plan + progress for research tasks. |
| **`/metamorph `** *token* | Compile and hot-load **`meta_<token>.dll`** (R4 registry). |
| **`/wasm_run `** *path* | Load and exercise **Wasm** from path (resolves vs exe dir, repo root, `fixtures\phase14`). |
| **`/words`** | Dump active vocabulary (word labels from clusters). |
| **`/quit`** / **`/exit`** | **`/save`**, then exit the process. |

### Hidden / advanced (not shown on `/help`)

| Command | What it does |
|---------|----------------|
| **`/reset`** | Deletes current brain file, re-inits graph + cortex, re-ingests **Identity::SELF_KNOWLEDGE**, reapplies **`data/core_directives.txt`** (verbose), clears translation map. **Destructive.** |

---

## Bang — motor, vision, sensors

| Command | What it does |
|---------|----------------|
| **`!open `** *app* | Launch application (e.g. `notepad`). |
| **`!type `** *text* | Send keystrokes to the **foreground** window. |
| **`!focus `** *title* | Focus window by partial title; sets motor target for **`!verify`**. |
| **`!windows`** | List open windows (title lines). |
| **`!run `** *procedure* | Run a **named procedure** from motor bootstrap. |
| **`!speak`** | **Voice test** only (short spoken line). |
| **`!speak `** *word* | Generate from seed word and **type** into focused window. |
| **`!speak`** with only spaces after `speak` | Same as bare **`!speak`**. |
| **`!killswitch`** | Resets the **ESC kill-switch** state so automation can resume after you held ESC. |
| **`!ingest `** *sentence* | Same as typing a bare sentence through **`cmd_ingest`** (explicit teach). |
| **`!ingest`** | Usage hint. |
| **`!see`** | **UIA**: read visible text from foreground window; if text found, **ingests** it; else falls back to ASCII fovea preview. |
| **`!diff`** | Temporal **difference** ASCII (vision tick). |
| **`!verify`** | Check whether **`!focus`** target is foreground + visible. |
| **`!meta`** / **`!confidence`** | Metacognition **confidence report**. |
| **`!drives`** | Print **curiosity / boredom / frustration / engagement** (before boredom reset from “user just typed”). |
| **`!goal `** *text* | Try to bind a **multi-step goal** to semantics; may ask you to teach if confidence zero. |
| **`!goals`** | Current goal planner status. |
| **`!mute`** | Toggle **TTS** on/off. |
| **`!ears`** | Toggle **acoustic cortex** (mic path). |
| **`!ears_status`** | Mic / wake / energy / last error string. |
| **`!ears_listen`** | Force **15 s** listen window (no wake word). |
| **`!contradictions`** | Scan graph for contradictions → **`artefacts/contradictions.csv`**. |
| **`!load_mass`** *[path]* | Load **`knowledge_mass.bin`** (default path if omitted). |

---

## Query / generate prefixes

| Input | What it does |
|--------|----------------|
| **`?`** *word* | **Generate** continuation starting from cluster for *word*. |
| **`??`** *word* | **Query** typed associations for *word* (`cmd_query`). |

---

## CLI (non-interactive)

| Invocation | What it does |
|------------|----------------|
| **`engram.exe --train `** *file* | Ingest file(s); repeat **`--train`** for multiple. |
| **`engram.exe --train_dir `** *dir* | Ingest directory(ies). |
| **`engram.exe `** *anything else* | Treated as **`dispatch()`** once (e.g. a slash command string). |
| After CLI batch | Brief wait for async work, **`cmd_save`**, exit **0**. |

---

## Batch scripts (repo root)

| Script | What it does |
|--------|----------------|
| **`run_engram.bat`** | **`compile_only`** or full **build + run** `build\engram.exe` (wasm3 objs if needed). |
| **`train_engram.bat`** | Runs **`engram.exe`** with **`--train`** on bundled docs (needs prior build). |
| **`train_engram_sources.bat`** | **`--train_dir`** on `src\core` and `src`. |
| **`scripts\compile_research_gates.bat`** | Build **R0–R5, B1, R3 wasm** gate EXEs. |
| **`scripts\r6_eval_matrix.ps1`** | Run gates, append **`artefacts\r6_eval_matrix.txt`**. |
| **`scripts\build_phase_r3_wasm.bat`** | Build **R3** `.wasm` fixtures. |

---

## Environment

| Variable | Effect |
|----------|--------|
| **`AUTO_SAVE_SECONDS`** | If set to **1–86400**, periodic **`SynapticMemory::sleep`** while idle; **`0`/unset** = off. After save: dim **`[auto-save complete]`**. |

---

## Surprise points (demo / audit worthy)

1. **`/status` table** — Single command shows **process working set**, **worst cognitive tick**, and **ShadowBrain veto** latency — rare in hobby AI daemons.
2. **`/reset` exists but is not listed on `/help`** — intentional reduction of foot-guns; power users still use it.
3. **`!contradictions`, `!load_mass`, `!confidence`** — live in **`dispatch`** but omitted from **`cmd_help`** to keep the menu short; they are **real** commands.
4. **Bare sentence ≠ chatbot** — Every line goes through **`fpsan_reasoning_cycle_user_turn`** (graph physics + ingest + drives + PE), not template replies.
5. **ShadowBrain + ESC** — Motor plans can be **vetoed in-process**; **ESC** is the **hard stop** (see **SECURITY.md**).
6. **Acoustic path** — Spoken shortcuts can route **`!`** / **`?`** like the keyboard (see **`dispatch_acoustic_command`**).
7. **~1 kHz loop target** — Main loop aims for **1 ms** ticks; **`/status`** “Worst tick” shows real overrun µs on Windows.
8. **Programmatic mode** — Same binary can **ingest corpora** and exit (CI / training batch friendly).

---

*Generated from `src/fpsan_live_core.cpp` dispatch graph; if something diverges, the source wins.*
