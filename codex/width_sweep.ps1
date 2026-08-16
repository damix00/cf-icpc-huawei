param([string]$Glob = "judge/*.txt")

$ErrorActionPreference = "Stop"
$files = Get-ChildItem $Glob | Sort-Object Name
$bins = @("codex/sim_v0.exe") + (1..8 | ForEach-Object { "codex/sim_fixed_w$_.exe" })

foreach ($file in $files) {
    $scores = @()
    foreach ($bin in $bins) {
        $line = & $bin $file.FullName | Select-Object -First 1
        if ($line -notmatch 'score=([0-9.]+)') { throw "$bin failed on $($file.Name): $line" }
        $scores += [double]$Matches[1]
    }
    $bestIndex = 1
    for ($i = 2; $i -lt $scores.Count; ++$i) {
        if ($scores[$i] -gt $scores[$bestIndex]) { $bestIndex = $i }
    }
    $bestDelta = $scores[$bestIndex] - $scores[0]
    if ([math]::Abs($bestDelta) -ge 0.01) {
        Write-Output ("{0,-16} current={1,8:F3} bestW={2} gain={3,9:+0.000;-0.000;0.000}" -f
                      $file.Name, $scores[0], $bestIndex, $bestDelta)
    }
}
