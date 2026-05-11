@echo off
REM Visual Studio 2022 Community — change VS_ROOT if your layout differs.
set "VS_ROOT=D:\VS\community"
if not exist "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" (
  echo ERROR: vcvars64.bat not found:
  echo   "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat"
  exit /b 1
)
call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" %*
