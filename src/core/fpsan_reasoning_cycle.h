#pragma once
// ============================================================
// Phase 13 — Sovereign reasoning cycle (graph-first)
//
// Single entry point for conversational turns:
// Perceive → L1 temporal touch → episodic cue → deterministic speaker bind
// → activation spread → lexer generation → ingest-on-unknown.
//
// Heavy dependencies live in fpsan_live_core.cpp (globals). This header only
// publishes the callable surface for benchmarks/tools that link the daemon TU.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

/// Graph-first cognitive turn using live brain globals (`g_*` in fpsan_live_core.cpp).
/// Does not handle `!foo` / `?bar` slash commands — `dispatch()` routes those first.
void fpsan_reasoning_cycle_user_turn(const char* user_line);

#ifdef __cplusplus
}
#endif
