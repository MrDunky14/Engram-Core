@echo off
setlocal
pushd "%~dp0.."
set ROOT=%CD%
popd
call "%~dp0vcvars_community.bat"
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build" mkdir "%ROOT%\build"

cl /std:c++17 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS ^
  /I "%ROOT%\src\core" /I "%ROOT%\src\benchmark" ^
  "%ROOT%\src\benchmark\phase13_gate.cpp" ^
  /Fe"%ROOT%\build\phase13_gate.exe"

if errorlevel 1 exit /b 1
echo OK "%ROOT%\build\phase13_gate.exe"
exit /b 0
