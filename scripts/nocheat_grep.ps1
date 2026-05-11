# =============================================================
# FP-SAN No-Cheat Grep Gate  (scripts/nocheat_grep.ps1)
#
# The ONLY oracle for FP-SAN intelligence is the neural graph.
# This script enforces three hard rules at every commit / CI run:
#
#  Rule 1 — STDP integrity
#    Direct synaptic weight assignment (.weight =, .weight +=, etc.)
#    is forbidden outside the graph core files where add_edge() and
#    apply_stdp() live.  All plasticity MUST flow through apply_stdp().
#
#  Rule 2 — No keyword-routed input matching
#    if/else chains that compare raw input against literal strings
#    (if (input == "hello") …) are banned in every source file.
#    Routing must come from spreading activation, not C++ switch
#    statements pretending to be intelligence.
#
#  Rule 3 — Banned symbol list
#    Functions that were deleted during The Purge must never be
#    re-introduced.  Any file that references them fails the gate.
#
# Usage:
#   cd <repo-root>
#   .\scripts\nocheat_grep.ps1
#
# Exit code 0 = clean.  Exit code 1 = violations found.
# =============================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ROOT   = Split-Path -Parent $PSScriptRoot
$SRC    = Join-Path $ROOT "src"
$FAILED = $false

# --- Helpers -------------------------------------------------
function Fail($label, $matches_list) {
    Write-Host "`nFAIL [$label]" -ForegroundColor Red
    foreach ($m in $matches_list) {
        $rel = $m.Path.Replace($ROOT + "\", "")
        Write-Host ("  {0}:{1}  {2}" -f $rel, $m.LineNumber, $m.Line.Trim()) -ForegroundColor Yellow
    }
    $script:FAILED = $true
}

# --- Rule 1: Direct weight assignment outside graph core -----
#
# Allowed files:
#   cluster_graph.h  - contains add_edge() and apply_stdp() definitions
#   fpsan_memory.h   - serialises/deserialises edge records (I/O, not plasticity)
#   fpsan_stub.cpp   - no-op stubs for linker
$WEIGHT_ALLOW = @(
    (Join-Path $SRC "core\cluster_graph.h"),
    (Join-Path $SRC "core\fpsan_memory.h"),
    (Join-Path $SRC "benchmark\fpsan_stub.cpp"),
    # Benchmark gate crafts synthetic knowledge_mass records.
    (Join-Path $SRC "benchmark\phase5_gate.cpp")
)

$all_sources = Get-ChildItem -Recurse -Path $SRC -Include "*.h","*.cpp"
$weight_targets = $all_sources | Where-Object {
    $path = $_.FullName
    -not ($WEIGHT_ALLOW | Where-Object { $_ -eq $path })
}

$weight_hits = $weight_targets |
    Select-String -Pattern '\.weight\s*[+\-\*\/]?=' -AllMatches

if ($weight_hits) { Fail "Rule 1 - Direct weight assignment (use apply_stdp)" $weight_hits }

# --- Rule 2: Hardcoded input keyword routing -----------------
#
# Banned: comparing raw 'input' against a non-slash literal string.
#   if (strcmp(input, "hello") == 0)   <- banned: conversation keyword match
#   if (input == "who are you")        <- banned: conversation keyword match
#
# Allowed: REPL /slash and !bang CLI commands (they start with "/" or "!"):
#   if (strcmp(input, "/quit") == 0)   <- OK: REPL dispatcher
#   if (strcmp(input, "!open") == 0)   <- OK: motor command dispatcher
#
# The regex excludes literals whose first char is / or ! by requiring
# the opening quote to be followed by a non-slash/non-bang character.
$routing_hits = $all_sources | Select-String -Pattern `
    '(if\s*\(.*\binput\b.*==\s*"[^/!]|\bstrcmp\s*\(\s*(input|\*input),\s*"[^/!])' -AllMatches

if ($routing_hits) { Fail "Rule 2 - Hardcoded input keyword routing" $routing_hits }

# --- Rule 4: No runtime LLM / transformer code ----------------
#
# Hard fail if these substrings appear anywhere under src/**
# (We are a pure deterministic runtime; LLMs are offline-only.)
$LLM_BANNED = @(
    "llama",
    "transformer",
    "torch",
    "ggml",
    "sentencepiece",
    "tiktoken"
)

foreach ($sym in $LLM_BANNED) {
    $hits = $all_sources | Select-String -Pattern $sym -AllMatches
    if ($hits) { Fail ("Rule 4 - Banned runtime AI substring '" + $sym + "'") $hits }
}

# --- Rule 3: Banned symbol list ------------------------------
#
# These functions were deleted in The Purge and must never return.
$BANNED_SYMBOLS = @(
    "is_social_input",
    "get_social_seed",
    "has_fallback"
)

foreach ($sym in $BANNED_SYMBOLS) {
    $hits = $all_sources | Select-String -Pattern "\b$sym\b" -AllMatches
    if ($hits) { Fail ("Rule 3 - Banned symbol '" + $sym + "' re-introduced") $hits }
}

# --- Final verdict -------------------------------------------
Write-Host ""
if ($FAILED) {
    Write-Host "NO-CHEAT GATE: FAILED - see violations above." -ForegroundColor Red
    Write-Host "The graph is the only oracle.  Fix before committing." -ForegroundColor Red
    exit 1
} else {
    Write-Host "NO-CHEAT GATE: PASSED - the graph is the only oracle." -ForegroundColor Green
    exit 0
}
