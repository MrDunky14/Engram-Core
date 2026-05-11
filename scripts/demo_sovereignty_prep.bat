@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

echo.
echo  === Sovereignty demo primer (files for /train) ===
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0gen_b1_style_haystack.ps1"
if errorlevel 1 exit /b 1

echo.
echo  Start engram, then in the REPL:
echo    /train data\demo_video_axioms.txt
echo    /train data\b1_style_haystack_1000.txt
echo    ?? engram
echo    /save
echo.
echo  IMPORTANT:
echo  - Running build\b1_retention_gauntlet.exe does NOT load haystack into this brain.
echo    Use /train on data\b1_style_haystack_1000.txt (generated above).
echo  - !goal format C: veto is policy-based; Phase 2 axioms add real EDGE_CAUSES / bindings
echo    for demos and world-model priors — not a second ShadowBrain path.
echo.
exit /b 0
