@echo off
setlocal
pushd "%~dp0..\fixtures\phase_r3"
if errorlevel 1 exit /b 1
for %%F in (micro_mdp add1 double parity clamp3) do (
  echo [phase_r3] wat2wasm %%F.wat
  call npx -y -p wabt wat2wasm "%%F.wat" -o "%%F.wasm" || exit /b 1
)
popd
echo OK phase_r3 wasm fixtures built
exit /b 0
