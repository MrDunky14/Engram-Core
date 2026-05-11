# Emit 1000 B1-style SVO lines for /train (same pattern as b1_retention_gauntlet).
# Run from repo root:  powershell -ExecutionPolicy Bypass -File scripts\gen_b1_style_haystack.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$outPath = Join-Path $root "data\b1_style_haystack_1000.txt"

New-Item -ItemType Directory -Force -Path (Split-Path $outPath) | Out-Null
$sw = New-Object System.IO.StreamWriter($outPath, $false, [System.Text.UTF8Encoding]::new($false))
try {
    for ($i = 0; $i -lt 1000; $i++) {
        $srv = "srv{0:D4}" -f $i
        $prt = "p{0:D6}" -f (100000 + $i)
        $sw.WriteLine("$srv need $prt")
    }
}
finally {
    $sw.Close()
}

Write-Host "Wrote $outPath (1000 lines). In engram: /train data\b1_style_haystack_1000.txt"
