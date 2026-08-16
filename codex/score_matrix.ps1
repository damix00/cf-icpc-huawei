param(
    [string]$Glob = "judge/*.txt",
    [string[]]$Bins = @(
        "codex/sim_ref_wrapper.exe",
        "codex/sim_v0.exe",
        "codex/sim_widthbase.exe",
        "codex/sim_width_no_pressure.exe",
        "codex/sim_width_analytic_pressure.exe",
        "codex/sim_width_weight_gate.exe"
    )
)

$ErrorActionPreference = "Stop"
$files = Get-ChildItem $Glob | Sort-Object Name
$all = @{}
foreach ($bin in $Bins) {
    $rows = @()
    foreach ($file in $files) {
        $line = & $bin $file.FullName | Select-Object -First 1
        if ($line -notmatch 'score=([0-9.]+)') {
            throw "$bin failed on $($file.FullName): $line"
        }
        $rows += [pscustomobject]@{ Name = $file.Name; Score = [double]$Matches[1] }
    }
    $all[$bin] = $rows
    $mean = ($rows.Score | Measure-Object -Average).Average
    $reference = $all[$Bins[0]]
    $delta = 0.0
    if ($reference) {
        for ($i = 0; $i -lt $rows.Count; ++$i) { $delta += $rows[$i].Score - $reference[$i].Score }
        $delta /= [math]::Max(1, $rows.Count)
    }
    Write-Output ("{0,-43} mean={1,9:F3} dref={2,9:+0.000;-0.000;0.000}" -f $bin, $mean, $delta)
}

if ($Bins.Count -gt 2) {
    $base = $all[$Bins[1]]
    foreach ($bin in $Bins[2..($Bins.Count - 1)]) {
        $rows = $all[$bin]
        $diff = for ($i = 0; $i -lt $rows.Count; ++$i) {
            [pscustomobject]@{ Name = $rows[$i].Name; Delta = $rows[$i].Score - $base[$i].Score }
        }
        $moved = $diff | Where-Object { [math]::Abs($_.Delta) -ge 0.01 } |
            Sort-Object { [math]::Abs($_.Delta) } -Descending | Select-Object -First 8
        Write-Output "  $bin versus current:"
        foreach ($row in $moved) { Write-Output ("    {0,-16} {1,10:+0.000;-0.000;0.000}" -f $row.Name, $row.Delta) }
    }
}
