# Summarize artefacts/*.json + r6_eval_matrix into artefacts/R6_report.md
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$art = Join-Path $root "artefacts"
$matrix = Join-Path $art "r6_eval_matrix.txt"
$out = Join-Path $art "R6_report.md"
$ts = Get-Date -Format o

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("# FP-SAN Phase R6 - consolidated report")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("Generated: $ts")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("## Eval matrix (latest tail)")
if (Test-Path -LiteralPath $matrix) {
    $tail = Get-Content -LiteralPath $matrix -Tail 24 -ErrorAction SilentlyContinue
    [void]$sb.AppendLine('``````')
    foreach ($line in $tail) { [void]$sb.AppendLine($line) }
    [void]$sb.AppendLine('``````')
} else {
    [void]$sb.AppendLine("(r6_eval_matrix.txt not found - run scripts/r6_eval_matrix.ps1)")
}
[void]$sb.AppendLine("")
[void]$sb.AppendLine("## JSON artefacts")
if (Test-Path -LiteralPath $art) {
    Get-ChildItem -LiteralPath $art -Filter "*.json" -File | ForEach-Object {
        $name = $_.Name
        [void]$sb.AppendLine("### $name")
        $content = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        if ($null -eq $content) { $content = "" }
        [void]$sb.AppendLine('``````json')
        [void]$sb.AppendLine($content.TrimEnd())
        [void]$sb.AppendLine('``````')
        [void]$sb.AppendLine("")
    }
} else {
    [void]$sb.AppendLine("(artefacts directory missing)")
}

Set-Content -LiteralPath $out -Value $sb.ToString() -Encoding UTF8
Write-Host "Wrote $out"
