# FP-SAN Phase R6 — official consolidated report (frozen in Git)

**This file is the committed record** for releases and LinkedIn/GitHub citations. Regenerate from a local run (`scripts\r6_eval_matrix.ps1` → `scripts\generate_r6_report.ps1`), then copy the meaningful tail into this document when you tag a version. Raw append-only logs live only under `artefacts/` (gitignored).


**Aligned with release:** **v1.0.0** (engine **FP-SAN v17.0**) — see also [`BENCHMARK_RESULTS_v1.0.0.md`](BENCHMARK_RESULTS_v1.0.0.md). **Reference hardware** for the frozen matrix: **HP EliteBook 850** (Intel Core **i5**, **8 GB** RAM); rationale in that document (“hardware sovereignty” baseline).

---

# FP-SAN Phase R6 - consolidated report

Generated: 2026-05-11T18:04:28.4604379+05:30

## Eval matrix (latest tail)
``````
r1_neuromod_ablation_gate PASS exit=0
r2_latent_gate PASS exit=0
r3_world_gate PASS exit=0
r3_wasm_closed_loop_gate PASS exit=0
r5_identity_gate PASS exit=0
b1_retention_gauntlet PASS exit=0
R6 eval matrix - 2026-05-11T18:02:22.9670170+05:30
transformer_baseline: TBD (offline harness not invoked in-product)
r0_baseline_gate PASS exit=0
r1_neuromod_ablation_gate PASS exit=0
r2_latent_gate PASS exit=0
r3_world_gate PASS exit=0
r3_wasm_closed_loop_gate PASS exit=0
r5_identity_gate PASS exit=0
b1_retention_gauntlet PASS exit=0
R6 eval matrix - 2026-05-11T18:04:20.4068263+05:30
transformer_baseline: TBD (offline harness not invoked in-product)
r0_baseline_gate PASS exit=0
r1_neuromod_ablation_gate PASS exit=0
r2_latent_gate PASS exit=0
r3_world_gate PASS exit=0
r3_wasm_closed_loop_gate PASS exit=0
r5_identity_gate PASS exit=0
b1_retention_gauntlet PASS exit=0
``````

## JSON artefacts
### b1_retention_niah.json
``````json
{
  "benchmark": "B1_retention_gauntlet",
  "mapping": "NIAH_style_structural_retrieval",
  "haystack_rules": 1000,
  "retention_first": true,
  "retention_last": true,
  "ingest_total_ms": 7221.867800,
  "verify_ms": 0.001300,
  "triples_total": 9000,
  "exit_ok": true
}
``````

### r0_baseline.json
``````json
{
  "suite": "r0_pillar_frozen",
  "tasks": [
    {"id":"p1_t01_known_water_molecule_qa","pass":true},
    {"id":"p1_t02_known_dog_animal_qa","pass":true},
    {"id":"p1_t03_compound_predicate_chain","pass":true},
    {"id":"p1_t04_listen_lexical_spread","pass":true},
    {"id":"p1_t05_conflict_wiki_beats_user","pass":true},
    {"id":"p1_t06_prediction_error_neuromod","pass":true},
    {"id":"p1_t07_equal_weight_stable_provenance","pass":true},
    {"id":"p1_t08_translation_hub_gate","pass":true},
    {"id":"p2_t09_motor_launch_notepad_skeleton","pass":true},
    {"id":"p2_t10_motor_type_payload","pass":true},
    {"id":"p2_t11_motor_sequence_save_chain","pass":true},
    {"id":"p2_t12_focus_loss_abort_counter","pass":true},
    {"id":"p2_t13_wasm_maze_smoke","pass":true},
    {"id":"p2_t14_wasm_stack_smoke","pass":true},
    {"id":"p3_t15_offline_fetch_provenance_wire","pass":true},
    {"id":"p3_t16_triple_distillation_edges","pass":true},
    {"id":"p3_t17_walk_causes_recall","pass":true},
    {"id":"p3_t18_multi_edge_provenance_audit","pass":true},
    {"id":"p3_t19_translation_ingest_recall_map","pass":true},
    {"id":"p3_t20_dashboard_counters_coherent","pass":true},
    {"id":"p4_t21_vision_root_seed","pass":true},
    {"id":"p4_t22_visual_child_bond_single","pass":true},
    {"id":"p4_t23_visual_multi_child_fanout","pass":true},
    {"id":"p4_t24_visual_prov_distinct_from_user","pass":true},
    {"id":"p4_t25_visual_only_answer_path","pass":true},
    {"id":"p5_t26_wasm_arithmetic_microenv","pass":true},
    {"id":"p5_t27_wasm_files_microenv","pass":true},
    {"id":"p5_t28_wasm_reload_stack_lifecycle","pass":true},
    {"id":"p5_t29_stdp_ltp_budget_invariant","pass":true},
    {"id":"p5_t30_janitor_prunes_idle_dll","pass":true}
  ],
  "passed": 30,
  "total": 30
}
``````


