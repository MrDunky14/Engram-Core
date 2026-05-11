@echo off
REM Engram Core — FP-SAN v17.0 (release v1.0.0)
REM Usage:
REM   run_engram.bat              — compile then run build\engram.exe
REM   run_engram.bat compile_only — compile only (no auto-run)
REM LNK1104: close engram.exe / IDE lock on build\engram.exe, then retry.
REM Optional: AUTO_SAVE_SECONDS=60 (1–86400) for periodic save; default off.
setlocal EnableExtensions
cd /d "%~dp0."
call "%~dp0scripts\vcvars_community.bat"
if errorlevel 1 (
  echo COMPILER ENV FAILED — edit scripts\vcvars_community.bat ^(VS_ROOT^)
  exit /b 1
)
if not exist build mkdir build

set WASM3=%CD%\vendor\wasm3\upstream\source
if not exist "%CD%\build\m3_core.obj" (
  echo [engram] wasm3 objects (one-time^)...
  set OPTS=/nologo /O2 /TC /GS- /D_CRT_SECURE_NO_WARNINGS /I"%WASM3%"
  cl %OPTS% /c "%WASM3%\m3_api_libc.c" /Fo"%CD%\build\m3_api_libc.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_bind.c" /Fo"%CD%\build\m3_bind.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_code.c" /Fo"%CD%\build\m3_code.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_compile.c" /Fo"%CD%\build\m3_compile.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_core.c" /Fo"%CD%\build\m3_core.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_env.c" /Fo"%CD%\build\m3_env.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_exec.c" /Fo"%CD%\build\m3_exec.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_function.c" /Fo"%CD%\build\m3_function.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_info.c" /Fo"%CD%\build\m3_info.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_module.c" /Fo"%CD%\build\m3_module.obj" || exit /b 1
  cl %OPTS% /c "%WASM3%\m3_parse.c" /Fo"%CD%\build\m3_parse.obj" || exit /b 1
)

set M3OBJS=build\m3_api_libc.obj build\m3_bind.obj build\m3_code.obj build\m3_compile.obj build\m3_core.obj build\m3_env.obj build\m3_exec.obj build\m3_function.obj build\m3_info.obj build\m3_module.obj build\m3_parse.obj

echo [engram] compiling...
cl /nologo /std:c++17 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /I src\core /c src\core\fpsan_wasm_sandbox.cpp /Fobuild\fpsan_wasm_sandbox.obj || exit /b 1
cl /nologo /std:c++17 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /I src\core ^
  src\fpsan_live_core.cpp build\fpsan_wasm_sandbox.obj %M3OBJS% ^
  /Fobuild\engram.obj /Febuild\engram.exe ^
  /link /STACK:16777216 ^
  user32.lib shell32.lib gdi32.lib ole32.lib oleaut32.lib winmm.lib ^
  UIAutomationCore.lib Psapi.lib Winhttp.lib Ws2_32.lib sapi.lib
if %ERRORLEVEL% NEQ 0 (
    echo COMPILATION FAILED
    exit /b 1
)

if /I "%~1"=="compile_only" (
    echo [engram] build OK ^(compile_only^)
    exit /b 0
)

echo [engram] starting daemon...
build\engram.exe
