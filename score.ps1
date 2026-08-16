# Score a strategy source across a test suite and print the mean (PowerShell port of score.sh).
# Usage:  ./score.ps1 [claude/sol.cpp] ["judge/*.txt"]
param(
    [string]$Src = "ref.cpp",
    [string]$Glob = "tests/ex1.txt tests/ex2.txt tests/g_*.txt"
)
$ErrorActionPreference = "Stop"
$tag = ($Src -replace '\.cpp$','') -replace '[/\\]','_'
$bin = "tmp/sim_$tag.exe"
New-Item -ItemType Directory -Force -Path "tmp" | Out-Null
$def = '-DSOL_SRC=\"' + $Src + '\"'
g++ -O2 -std=gnu++17 $def -o $bin sim.cpp
if ($LASTEXITCODE -ne 0) { Write-Error "compile failed" }

$files = $Glob.Split(' ') | ForEach-Object {
    Get-ChildItem -Path $_ -File -ErrorAction SilentlyContinue
} | Select-Object -ExpandProperty FullName -Unique

$lines = @()
foreach ($f in $files) {
    $out = & $bin $f 2>&1 | Out-String
    $lines += $out
}
$lines | Set-Content "tmp/scores_$tag.txt"

$scores = $lines | ForEach-Object {
    if ($_ -match "score=([0-9.]+)") { [double]$matches[1] }
}
$fails = ($lines | Where-Object { $_ -match "FAILED" }).Count
$mean = if ($scores.Count) { ($scores | Measure-Object -Average).Average } else { 0 }
$worst = if ($scores.Count) { ($scores | Measure-Object -Minimum).Minimum } else { 0 }
$wname = ""
if ($scores.Count) {
    $mi = 0
    for ($i = 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "score=([0-9.]+)") {
            if ([double]$matches[1] -eq $worst) { $wname = ($lines[$i] -replace "^.*test=(\S+).*", '$1') }
        }
    }
}
Write-Output ("{0}: tests={1} failures={2} MEAN={3:F3}  worst={4:F1} ({5})" -f $Src, $scores.Count, $fails, $mean, $worst, $wname)
