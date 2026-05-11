@echo off
REM Ingest bundled docs into the graph (requires build\engram.exe — run run_engram.bat compile_only first).
setlocal EnableExtensions
cd /d "%~dp0."
if not exist "build\engram.exe" (
  echo [engram] build\engram.exe missing — run run_engram.bat compile_only first.
  exit /b 1
)
echo [engram] training — docs bundle...
build\engram.exe --train training\general_knowledge.txt --train README.md --train "FP-SAN Architecture.md"
if errorlevel 1 exit /b 1
echo [engram] training complete.
echo [engram] brain files in repo root:
dir /b *.fpsan 2>nul
exit /b 0
