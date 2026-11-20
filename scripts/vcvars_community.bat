@echo off
REM Dynamically discover Visual Studio vcvars64.bat via vswhere, or fallback to known paths.
if defined VCVARS_INITIALIZED exit /b 0
if defined VSINSTALLDIR (
  if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" %*
    set "VCVARS_INITIALIZED=1"
    exit /b 0
  )
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
      call "%%i\VC\Auxiliary\Build\vcvars64.bat" %*
      set "VCVARS_INITIALIZED=1"
      exit /b 0
    )
  )
)

REM Fallbacks for specific environments
for %%P in (
  "C:\Program Files\Microsoft Visual Studio\18\Community"
  "C:\Program Files\Microsoft Visual Studio\2022\Community"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
  "D:\VS\community"
) do (
  if exist "%%~P\VC\Auxiliary\Build\vcvars64.bat" (
    call "%%~P\VC\Auxiliary\Build\vcvars64.bat" %*
    set "VCVARS_INITIALIZED=1"
    exit /b 0
  )
)

echo ERROR: vcvars64.bat not found via vswhere or standard install locations.
exit /b 1

