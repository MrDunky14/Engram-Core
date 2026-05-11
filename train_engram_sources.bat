@echo off
REM Ingest src\ and src\core\ (requires build\engram.exe — run run_engram.bat compile_only first).
setlocal EnableExtensions
cd /d "%~dp0."
if not exist "build\engram.exe" (
  echo [engram] build\engram.exe missing — run run_engram.bat compile_only first.
  exit /b 1
)
echo [engram] training — source trees...
build\engram.exe --train_dir src\core --train_dir src
if errorlevel 1 exit /b 1
echo [engram] training complete.
echo [engram] brain files in repo root:
dir /b *.fpsan 2>nul
exit /b 0
