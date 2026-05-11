@echo off
REM Build phase14 gate + wasm3 static objects (MSVC required).
REM Edit VS path if differs on your workstation.
setlocal EnableExtensions
pushd "%~dp0.."
set ROOT=%CD%
popd

set W=%ROOT%\vendor\wasm3\upstream\source

call "%~dp0vcvars_community.bat"
if errorlevel 1 (
  echo EDIT scripts\vcvars_community.bat — set VS_ROOT to your VS install.
  exit /b 1
)
if not exist "%ROOT%\build" mkdir "%ROOT%\build"

set OPTS=/nologo /O2 /TC /GS- /D_CRT_SECURE_NO_WARNINGS /I"%W%"

for %%F in (
  m3_api_libc
  m3_bind
  m3_code
  m3_compile
  m3_core
  m3_env
  m3_exec
  m3_function
  m3_info
  m3_module
  m3_parse
) do (
  cl %OPTS% /c "%W%\%%F.c" /Fo"%ROOT%\build\%%F.obj" || exit /b 1
)

cl /nologo /std:c++17 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS ^
  /I "%ROOT%\src\core" /I "%ROOT%\src\benchmark" ^
  "%ROOT%\src\benchmark\phase14_gate.cpp" "%ROOT%\src\core\fpsan_wasm_sandbox.cpp" ^
  "%ROOT%\build\m3_api_libc.obj" "%ROOT%\build\m3_bind.obj" ^
  "%ROOT%\build\m3_code.obj" "%ROOT%\build\m3_compile.obj" ^
  "%ROOT%\build\m3_core.obj" "%ROOT%\build\m3_env.obj" ^
  "%ROOT%\build\m3_exec.obj" "%ROOT%\build\m3_function.obj" ^
  "%ROOT%\build\m3_info.obj" "%ROOT%\build\m3_module.obj" ^
  "%ROOT%\build\m3_parse.obj" ^
  /Fe"%ROOT%\build\phase14_gate.exe" ^
  user32.lib
if errorlevel 1 exit /b 1

echo.
echo OK "%ROOT%\build\phase14_gate.exe"
exit /b 0
