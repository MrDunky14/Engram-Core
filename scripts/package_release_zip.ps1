# Package Engram Core v1.0.0 Windows x64 binary drop (requires prior build).
# Run from repo root after: .\run_engram.bat compile_only
param(
    [string]$Version = "v1.0.0"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = Join-Path $root "build\engram.exe"
if (-not (Test-Path $exe)) {
    Write-Error "Missing build\engram.exe - run run_engram.bat compile_only first."
}
$name = "EngramCore_${Version}_win-x64"
$stageRoot = Join-Path $root "artefacts"
$stage = Join-Path $stageRoot $name
$zipPath = Join-Path $stageRoot "$name.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -Force $exe (Join-Path $stage "engram.exe")
foreach ($rel in @(
    "run_engram.bat",
    "LICENSE", "NOTICE", "README.md", "SECURITY.md", "CREDITS.md",
    "data\core_directives.txt", "data\demo_video_axioms.txt", "data\README.md",
    "scripts\vcvars_community.bat",
    "scripts\gen_b1_style_haystack.ps1",
    "scripts\demo_impossible_sequence.bat",
    "scripts\demo_sovereignty_prep.bat",
    "scripts\package_release_zip.ps1",
    "fixtures\phase_r3\micro_mdp.wasm"
)) {
    $src = Join-Path $root $rel
    if (-not (Test-Path $src)) { continue }
    $parent = Split-Path $rel -Parent
    if ($parent) {
        $destDir = Join-Path $stage $parent
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    }
    Copy-Item -Force $src (Join-Path $stage $rel)
}
foreach ($doc in @(
    "COMMANDS.md", "BENCHMARK_RESULTS_v1.0.0.md", "R6_REPORT_OFFICIAL.md",
    "README.md", "SOAK_TEST_REPORT.md", "BENCHMARK_ROADMAP.md"
)) {
    $src = Join-Path $root "docs\$doc"
    if (-not (Test-Path $src)) { continue }
    $destDir = Join-Path $stage "docs"
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -Force $src (Join-Path $stage "docs\$doc")
}
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Compress-Archive -Path $stage -DestinationPath $zipPath -CompressionLevel Optimal
$h = Get-FileHash -Algorithm SHA256 $zipPath
Write-Host ""
Write-Host "Package: $zipPath"
Write-Host "SHA-256: $($h.Hash)"
Write-Host ""
