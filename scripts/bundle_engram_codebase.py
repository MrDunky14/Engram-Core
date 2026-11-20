import os
import sys

def main():
    root = r'c:\Users\Krishna\Documents\mygithub\Engram-Core'
    out_path = os.path.join(root, 'ENGRAM_CORE_CONSOLIDATED_CODEBASE.md')
    brain_out = r'C:\Users\Krishna\.gemini\antigravity\brain\f4966cf1-bdc4-4134-9ed4-4064d4f31985\ENGRAM_CORE_CONSOLIDATED_CODEBASE.md'

    manifest = [
        # 1. Overview & Teardown Audit
        ("Architecture & Audit", "README.md", "markdown", "Project overview, systems architecture & empirical benchmark receipts"),
        ("Architecture & Audit", "ENGRAM_CORE_TEARDOWN_AND_AUDIT.md", "markdown", "Comprehensive ground-truth teardown audit: claims vs. C++ implementation reality"),
        ("Architecture & Audit", "FP-SAN Architecture.md", "markdown", "Original architectural blueprint & neuromorphic network specification"),
        ("Architecture & Audit", "docs/BENCHMARK_RESULTS_v1.0.0.md", "markdown", "Frozen citation benchmark results & R6 matrix verification"),
        ("Architecture & Audit", "docs/COMMANDS.md", "markdown", "Interactive operator console command reference & control syntax"),

        # 2. Build Tooling
        ("Build Tooling", "Makefile", "makefile", "Modern MSVC build targets for systems benchmarks, gates, and tests"),
        ("Build Tooling", "run_engram.bat", "bat", "Daemon compilation & launch driver script"),
        ("Build Tooling", "scripts/vcvars_community.bat", "bat", "Dynamic Visual Studio toolset environment discovery script"),
        ("Build Tooling", "scripts/compile_research_gates.bat", "bat", "Batch compiler for R0, R1, R3, R5 and B1 gauntlet gates"),

        # 3. Core Graph & Memory Substrate
        ("Memory Substrate", "src/core/cluster_graph.h", "cpp", "Contiguous flat arena (262k nodes), Treiber stack neurogenesis, CAS voltage, spinlock edges"),
        ("Memory Substrate", "src/core/fpsan_language.h", "cpp", "SpikingTokenizer, LanguageCortex, 256-bit Hamming perception & O(1) word_cache"),
        ("Memory Substrate", "src/core/fpsan_memory.h", "cpp", "SynapticMemory: exclusive RW-lock sleep/wake binary serialization for brain state"),
        ("Memory Substrate", "src/core/fpsan_cortex_harness.h", "cpp", "Four-cortex thread harness with hybrid high-res waitable timer + microsecond spinwait tail"),
        ("Memory Substrate", "src/core/fpsan_temporal_memory.h", "cpp", "Temporal chain recording, episodic hub links & multi-tier decay"),

        # 4. Perception, Lexicon & Safety
        ("Perception & Safety", "src/core/fpsan_lexer.h", "cpp", "Trie lexicon POS lookup, deterministic FSM phrase grouper, SVO triple ingest"),
        ("Perception & Safety", "src/core/fpsan_sandbox.h", "cpp", "ShadowBrain counterfactual safety veto & HTTP tool primitive registration"),
        ("Perception & Safety", "src/core/fpsan_motor.h", "cpp", "Motor action structures, UIAutomation OS input injection, action queues"),
        ("Perception & Safety", "src/core/fpsan_drives.h", "cpp", "Homeostatic drives (boredom, curiosity, engagement) & autonomous goal triggers"),
        ("Perception & Safety", "src/core/fpsan_neuromod.h", "cpp", "Global neuromodulation: atomic plasticity_scale, arousal, and prediction error EMA"),
        ("Perception & Safety", "src/core/fpsan_world_model.h", "cpp", "Graph-walked transition prior: greedy next-step prediction via CAUSES/TEMPORAL edges"),
        ("Perception & Safety", "src/core/fpsan_metacognition.h", "cpp", "Metacognitive monitoring, confidence tracking, and doubt/frustration telemetry"),
        ("Perception & Safety", "src/core/fpsan_metamorphic.h", "cpp", "Metamorphic agency: dynamic C++ template generation, cl.exe invocation & DLL hot-load"),
        ("Perception & Safety", "src/core/fpsan_san_scheduler.h", "cpp", "Spiking Asynchronous Network event queue & discrete spike dispatcher"),
        ("Perception & Safety", "src/core/fpsan_wasm_sandbox.h", "cpp", "Wasm3 sandboxed execution interface with instruction & memory gas budgets"),
        ("Perception & Safety", "src/core/fpsan_wasm_sandbox.cpp", "cpp", "Wasm3 sandbox implementation: runtime lifecycle, function dispatch, isolation"),
        ("Perception & Safety", "src/core/fpsan_reasoning.h", "cpp", "BFS path reasoning, hypothesis testing, and contradiction detection"),
        ("Perception & Safety", "src/core/fpsan_identity.h", "cpp", "Self-knowledge axioms, identity verification, and core system directives"),
        ("Perception & Safety", "src/core/fpsan_speaker.h", "cpp", "Predicate target extraction and natural language response assembly"),
        ("Perception & Safety", "src/core/fpsan_hierarchy.h", "cpp", "Hierarchical cortex scheduling and cross-cortex voltage synchronization"),
        ("Perception & Safety", "src/core/fpsan_voice.h", "cpp", "Windows SAPI text-to-speech bridge"),
        ("Perception & Safety", "src/core/fpsan_ears.h", "cpp", "Continuous acoustic cortex dictation using Windows Speech Recognition (SAPI)"),
        ("Perception & Safety", "src/core/fpsan_screen_sensor.h", "cpp", "Windows UIAutomation accessibility tree scanner and visual hierarchy ingest"),
        ("Perception & Safety", "src/core/fpsan_winsock_http.h", "cpp", "WinSock2 non-blocking HTTP client for external knowledge ingest"),
        ("Perception & Safety", "src/core/fpsan_research_async.h", "cpp", "Background worker thread for asynchronous Wikipedia / ConceptNet fetches"),
        ("Perception & Safety", "src/core/fpsan_research.h", "cpp", "Synchronous research pipeline and knowledge distillation interface"),
        ("Perception & Safety", "src/core/fpsan_self_edit_registry.h", "cpp", "Registry for tracking runtime code modifications and metamorphic patches"),
        ("Perception & Safety", "src/core/fpsan_r0_dashboard.h", "cpp", "Telemetry dashboard tracking active edges, tiers, plasticity, and arousal"),
        ("Perception & Safety", "src/core/fpsan_translation_cortex.h", "cpp", "Translation cortex mapping symbolic directives into graph activation priors"),
        ("Perception & Safety", "src/core/fpsan_workspace.h", "cpp", "Windows desktop workspace manager and active window tracker"),

        # 5. Live Cognitive Daemon
        ("Live Engine", "src/fpsan_live_core.cpp", "cpp", "Living cognitive daemon entrypoint: non-blocking input, hybrid 1kHz loop, drive ticks"),

        # 6. Systems Benchmarks & Research Gates
        ("Benchmarks & Gates", "src/benchmark/systems_benchmark.cpp", "cpp", "4-part systems benchmark: Engram vs SQLite vs FlatGraph, spreading latency, timer jitter"),
        ("Benchmarks & Gates", "src/benchmark/b1_retention_gauntlet.cpp", "cpp", "B1 NIAH retention receipt test: 1,000-rule SVO haystack and needle verification"),
        ("Benchmarks & Gates", "src/benchmark/heartbeat_gate.cpp", "cpp", "Thread pinning and cognitive tick frequency gate (1,000 Hz validation)"),
        ("Benchmarks & Gates", "src/benchmark/r0_baseline_gate.cpp", "cpp", "30-task frozen research pillar baseline gate suite"),
        ("Benchmarks & Gates", "src/benchmark/r1_neuromod_ablation_gate.cpp", "cpp", "Neuromodulation ablation gate: tests learning with/without RPE dopamine gating"),
        ("Benchmarks & Gates", "src/benchmark/r2_latent_gate.cpp", "cpp", "Latent concept binding gate: tests ensemble linking and hub activation"),
        ("Benchmarks & Gates", "src/benchmark/r3_world_gate.cpp", "cpp", "World model transition gate: validates CAUSES/TEMPORAL edge prediction priors"),
        ("Benchmarks & Gates", "src/benchmark/r3_wasm_closed_loop_gate.cpp", "cpp", "Wasm3 sandboxed closed-loop agent execution gate"),
        ("Benchmarks & Gates", "src/benchmark/r5_identity_gate.cpp", "cpp", "Self-knowledge integrity and identity persistence gate"),
        ("Benchmarks & Gates", "src/benchmark/honest_harness.h", "cpp", "High-resolution latency statistics harness with percentiles and pass thresholds"),
        ("Benchmarks & Gates", "src/benchmark/phase13_gate.cpp", "cpp", "Phase 13 sovereign reasoning cycle & ShadowBrain safety veto gate"),
        ("Benchmarks & Gates", "src/benchmark/adversarial_sandbox_gate.cpp", "cpp", "Adversarial destructive scenario suite (50 safe vs. 20 destructive test cases)"),
    ]

    print(f"Bundling {len(manifest)} files...")

    lines = []
    lines.append("# 📦 Engram-Core: Complete Consolidated Codebase Dossier")
    lines.append("")
    lines.append("**Author:** Krishna Singh ([@MrDunky14](https://github.com/MrDunky14))  ")
    lines.append("**Project:** Engram-Core (FP-SAN v17.0 / Release v1.0.0)  ")
    lines.append("**Architecture:** Cache-Conscious Spreading-Activation Graph Store & Real-Time Edge Runtime in C++17  ")
    lines.append("**Purpose:** Single all-in-one consolidated codebase dossier prepared for technical peer review, architectural audit, and systems engineering evaluation.  ")
    lines.append("")
    lines.append("---")
    lines.append("")
    lines.append("## 📑 Codebase File Manifest")
    lines.append("")
    lines.append("| Category | File Path | Language | Lines | Size | Primary Architectural Responsibility |")
    lines.append("|---|---|---|---|---|---|")

    file_contents = []
    total_lines = 0
    total_bytes = 0

    for cat, rel, lang, desc in manifest:
        full_p = os.path.join(root, rel.replace('/', os.sep))
        if not os.path.exists(full_p):
            print(f"WARNING: File missing: {rel}")
            continue
        with open(full_p, 'r', encoding='utf-8', errors='replace') as f:
            content = f.read()
        n_lines = len(content.splitlines())
        n_bytes = len(content.encode('utf-8'))
        total_lines += n_lines
        total_bytes += n_bytes
        lines.append(f"| **{cat}** | `{rel}` | {lang} | {n_lines:,} | {n_bytes / 1024:.1f} KB | {desc} |")
        file_contents.append((cat, rel, lang, desc, content, n_lines, n_bytes))

    lines.append("")
    lines.append(f"**Total Scope:** **{len(file_contents)} files** | **{total_lines:,} lines of code** | **{total_bytes / 1024:.1f} KB** uncompressed text.")
    lines.append("")
    lines.append("---")
    lines.append("")

    current_cat = None
    for cat, rel, lang, desc, content, n_lines, n_bytes in file_contents:
        if cat != current_cat:
            current_cat = cat
            lines.append(f"## 📁 {current_cat}")
            lines.append("")

        lines.append(f"### File: `{rel}`")
        lines.append(f"> **Lines:** {n_lines:,} | **Size:** {n_bytes / 1024:.1f} KB | **Role:** {desc}")
        lines.append("")
        lines.append(f"```{lang}")
        lines.append(content)
        lines.append("```")
        lines.append("")
        lines.append("---")
        lines.append("")

    full_text = "\n".join(lines)
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(full_text)
    print(f"Wrote local consolidated codebase: {out_path} ({len(full_text):,} chars, {total_lines:,} lines)")

    with open(brain_out, 'w', encoding='utf-8') as f:
        f.write(full_text)
    print(f"Wrote brain artifact copy: {brain_out}")

if __name__ == '__main__':
    main()
