#!/usr/bin/env pwsh
# Phase 16 — cumulative live gate scaffolding (captures artefacts, operator-led).
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$out  = Join-Path $root "artefacts\phase16"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$p = Join-Path $out "preflight.txt"
Get-Process | Select-Object ProcessName,Id,CPUTime `
  | Out-File -FilePath $p -Encoding UTF8

$pt = Join-Path $out "process_tree_snapshot.txt"
& "$env:SystemRoot\System32\tasklist.exe" /V | Out-File -FilePath $pt -Encoding ASCII

$mods = Join-Path $out "module_health.csv"
@(
  '"tick","module","healthy","notes"'
  "0,phase16_script,PASS,placeholder until live attach"
) | Set-Content -Path $mods -Encoding UTF8

$tx = Join-Path $out "dialog_transcript.csv"
@(
  '"turn","role","utterance"'
  "0,operator,!help"
  "1,engram,graph-first-cycle placeholder — link live fifo here"
) | Set-Content -Path $tx -Encoding UTF8

$readme = Join-Path $out "LIVE_GATE_HINTS.txt"
@"
Phase 16 operator hookup:
 - Run .\build\engram.exe from repo root AFTER compile_only succeeds.
 - Drive 100 scripted turns referencing phase13 cycle (no NL regex intent).
 - Copy console session into artefacts/phase16/dialog_transcript_full.log
"@ | Set-Content -Path $readme -Encoding UTF8

Write-Host "Phase16 artefacts scaffolded -> $out"
