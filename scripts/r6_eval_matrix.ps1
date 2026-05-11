# R6 evaluation - run research gates from build\ and append results to artefacts\r6_eval_matrix.txt
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$build = Join-Path $root "build"
$out = Join-Path $root "artefacts\r6_eval_matrix.txt"
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null

function Run-Gate {
    param([string]$Name, [string]$ExeRel)
    $exe = Join-Path $root $ExeRel
    if (-not (Test-Path -LiteralPath $exe)) {
        return "$Name SKIP (missing $ExeRel)"
    }
    Push-Location $root
    try {
        & $exe 2>&1 | Out-Null
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($code -eq 0) { return "$Name PASS exit=0" }
    return "$Name FAIL exit=$code"
}

$ts = Get-Date -Format o
$lines = @()
$lines += "R6 eval matrix - $ts"
$lines += "transformer_baseline: TBD (offline harness not invoked in-product)"
$lines += (Run-Gate "r0_baseline_gate" "build\r0_baseline_gate.exe")
$lines += (Run-Gate "r1_neuromod_ablation_gate" "build\r1_neuromod_ablation_gate.exe")
$lines += (Run-Gate "r2_latent_gate" "build\r2_latent_gate.exe")
$lines += (Run-Gate "r3_world_gate" "build\r3_world_gate.exe")
$lines += (Run-Gate "r3_wasm_closed_loop_gate" "build\r3_wasm_closed_loop_gate.exe")
$lines += (Run-Gate "r5_identity_gate" "build\r5_identity_gate.exe")
$lines += (Run-Gate "b1_retention_gauntlet" "build\b1_retention_gauntlet.exe")

Add-Content -Path $out -Value ($lines -join "`n") -Encoding UTF8
Write-Host ($lines -join "`n")
Write-Host "Appended to $out"

$reportScript = Join-Path $PSScriptRoot "generate_r6_report.ps1"
if (Test-Path -LiteralPath $reportScript) {
    & $reportScript
}
