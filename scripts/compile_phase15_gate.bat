@echo off
REM Phase 15 gate — header-only stubs + metamorphic façade ( MSVC )

setlocal
pushd "%~dp0.."
set ROOT=%CD%
popd

call "%~dp0vcvars_community.bat"
if errorlevel 1 exit /b 1

cl /std:c++17 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS ^
  /I "%ROOT%\src\core" /I "%ROOT%\src\benchmark" ^
  "%ROOT%\src\benchmark\phase15_gate.cpp" ^
  /Fe"%ROOT%\build\phase15_gate.exe" ^
  user32.lib
if errorlevel 1 exit /b 1

echo OK "%ROOT%\build\phase15_gate.exe"
exit /b 0
