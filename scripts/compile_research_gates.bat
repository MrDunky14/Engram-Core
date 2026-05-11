@echo off
REM Compile research program gates (R0,R1,R3,R5) + B1 retention gauntlet
REM R0 links wasm3 static objects — built once alongside phase14 artefacts.
setlocal EnableExtensions
pushd "%~dp0.."
set ROOT=%CD%
popd
call "%~dp0vcvars_community.bat"
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build" mkdir "%ROOT%\build"

set COREINC=/I "%ROOT%\src\core" /I "%ROOT%\src\benchmark"
set FLAGS=/std:c++17 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS
set WASM3_SRC=%ROOT%\vendor\wasm3\upstream\source

if not exist "%ROOT%\build\m3_core.obj" call :build_wasm3_objs
if errorlevel 1 exit /b 1

if not exist "%ROOT%\fixtures\phase_r3\micro_mdp.wasm" (
  echo [research_gates] building phase_r3 wasm fixtures...
  call "%ROOT%\scripts\build_phase_r3_wasm.bat"
  if errorlevel 1 exit /b 1
)

set M3OBJS="%ROOT%\build\m3_api_libc.obj" "%ROOT%\build\m3_bind.obj" "%ROOT%\build\m3_code.obj" "%ROOT%\build\m3_compile.obj" "%ROOT%\build\m3_core.obj" "%ROOT%\build\m3_env.obj" "%ROOT%\build\m3_exec.obj" "%ROOT%\build\m3_function.obj" "%ROOT%\build\m3_info.obj" "%ROOT%\build\m3_module.obj" "%ROOT%\build\m3_parse.obj"

REM LanguageCortex is ~1.6MB on stack per test frame — default 1MB stack overflows.
cl %FLAGS% %COREINC% "%ROOT%\src\benchmark\r0_baseline_gate.cpp" "%ROOT%\src\core\fpsan_wasm_sandbox.cpp" %M3OBJS% /Fe"%ROOT%\build\r0_baseline_gate.exe" ^
  /link /STACK:16777216 user32.lib || exit /b 1

cl %FLAGS% %COREINC% "%ROOT%\src\benchmark\r2_latent_gate.cpp" /Fe"%ROOT%\build\r2_latent_gate.exe" /link /STACK:16777216 || exit /b 1
cl %FLAGS% %COREINC% "%ROOT%\src\benchmark\r1_neuromod_ablation_gate.cpp" /Fe"%ROOT%\build\r1_neuromod_ablation_gate.exe" /link /STACK:16777216 || exit /b 1
cl %FLAGS% %COREINC% "%ROOT%\src\benchmark\r3_world_gate.cpp" /Fe"%ROOT%\build\r3_world_gate.exe" /link /STACK:16777216 || exit /b 1
cl %FLAGS% %COREINC% "%ROOT%\src\benchmark\r5_identity_gate.cpp" /Fe"%ROOT%\build\r5_identity_gate.exe" /link /STACK:16777216 || exit /b 1

cl %FLAGS% %COREINC% "%ROOT%\src\benchmark\b1_retention_gauntlet.cpp" /Fe"%ROOT%\build\b1_retention_gauntlet.exe" /link /STACK:16777216 || exit /b 1

cl %FLAGS% %COREINC% "%ROOT%\src\benchmark\r3_wasm_closed_loop_gate.cpp" "%ROOT%\src\core\fpsan_wasm_sandbox.cpp" %M3OBJS% /Fe"%ROOT%\build\r3_wasm_closed_loop_gate.exe" ^
  /link /STACK:16777216 user32.lib || exit /b 1

echo OK research gates in %ROOT%\build
exit /b 0

:build_wasm3_objs
echo [research_gates] building wasm3 objects (one-time)...
set OPTS=/nologo /O2 /TC /GS- /D_CRT_SECURE_NO_WARNINGS /I"%WASM3_SRC%"
cl %OPTS% /c "%WASM3_SRC%\m3_api_libc.c" /Fo"%ROOT%\build\m3_api_libc.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_bind.c" /Fo"%ROOT%\build\m3_bind.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_code.c" /Fo"%ROOT%\build\m3_code.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_compile.c" /Fo"%ROOT%\build\m3_compile.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_core.c" /Fo"%ROOT%\build\m3_core.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_env.c" /Fo"%ROOT%\build\m3_env.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_exec.c" /Fo"%ROOT%\build\m3_exec.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_function.c" /Fo"%ROOT%\build\m3_function.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_info.c" /Fo"%ROOT%\build\m3_info.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_module.c" /Fo"%ROOT%\build\m3_module.obj" || exit /b 1
cl %OPTS% /c "%WASM3_SRC%\m3_parse.c" /Fo"%ROOT%\build\m3_parse.obj" || exit /b 1
exit /b 0
