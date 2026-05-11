# R0 research suite — build research gates and run baseline JSON writer
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$bat  = Join-Path $root "scripts\compile_research_gates.bat"
if (Test-Path $bat) {
  cmd /c "`"$bat`""
  if ($LASTEXITCODE -ne 0) { throw "compile_research_gates.bat failed" }
}
$gates = @(
  "r0_baseline_gate.exe",
  "r1_neuromod_ablation_gate.exe",
  "r2_latent_gate.exe",
  "r3_world_gate.exe",
  "r5_identity_gate.exe"
)
foreach ($g in $gates) {
  $p = Join-Path $root "build\$g"
  if (Test-Path $p) {
    Write-Host "---- $g ----"
    Push-Location $root
    try { & $p; Write-Host "exit $LASTEXITCODE" } finally { Pop-Location }
  } else {
    Write-Host "skip missing $g"
  }
}
Write-Host "R0 suite done. See artefacts/r0_baseline.json"
