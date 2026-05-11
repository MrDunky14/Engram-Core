@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

echo.
echo  === Engram Core — "Impossible" demo prep ===
echo  Run this once before recording so B1 receipt exists.
echo.

if not exist "build\b1_retention_gauntlet.exe" (
  echo [demo] Building b1_retention_gauntlet.exe ...
  call "%~dp0compile_research_gates.bat"
  if errorlevel 1 exit /b 1
)

if not exist "artefacts" mkdir artefacts
echo [demo] Running B1 gauntlet — writes artefacts\b1_retention_niah.json
build\b1_retention_gauntlet.exe
if errorlevel 1 (
  echo [demo] B1 failed — fix build and retry.
  exit /b 1
)

echo.
echo  --- On-camera checklist (interactive; start recording first) ---
echo.
echo  1. BOOT   From repo root, in a fresh console:
echo            run_engram.bat
echo           Show compile lines if first run; narrate instant process start vs LLM weight load.
echo           RAM: narrate **Working set** from /status — not a fixed number in code.
echo.
echo  2. Wait ~3 seconds, then:
echo            /status
echo           Point at: Tick counter, **Worst tick (us)**, **Working set (MB)**,
echo           **Worst ShadowBrain check (us)** — sub-ms tick is the claim, machine-dependent.
echo.
echo  3. PROOF OF LIFE (after /train demo axiom file if desired):
echo            ?? engram
echo           Expect green [Memory] lines for SVO and causes edges.
echo.
echo  4. METAMORPH
echo            /metamorph system_scan_tool
echo           Requires cl.exe on PATH (see scripts\vcvars_community.bat). Emits meta_* .cpp/.dll in build\.
echo.
echo  5. SAFETY
echo            !goal format C:
echo           Expect red **ShadowBrain VETO** + TTS — structural / policy block, intent level.
echo.
echo  6. RECEIPT  (new console or after exit)
echo            type artefacts\b1_retention_niah.json
echo           haystack_rules: 1000 — verify_ms ~0.0011 ms = **~1.1 µs** on fast reference runs.
echo.
echo  To start the daemon now:
echo            run_engram.bat
echo.
exit /b 0
