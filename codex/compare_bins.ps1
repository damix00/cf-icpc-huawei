param(
    [string]$Glob,
    [string]$Base,
    [string]$Candidate
)

$ErrorActionPreference = "Stop"
$sum = 0.0
$count = 0
foreach ($file in Get-ChildItem $Glob | Sort-Object Name) {
    $baseLine = & $Base $file.FullName | Select-Object -First 1
    $candidateLine = & $Candidate $file.FullName | Select-Object -First 1
    if ($baseLine -notmatch 'score=([0-9.]+)') { throw "$Base failed on $($file.Name)" }
    $baseScore = [double]$Matches[1]
    if ($candidateLine -notmatch 'score=([0-9.]+)') { throw "$Candidate failed on $($file.Name)" }
    $candidateScore = [double]$Matches[1]
    $delta = $candidateScore - $baseScore
    $sum += $delta
    ++$count
    if ([math]::Abs($delta) -ge 0.01) {
        Write-Output ("{0,-18} {1,10:+0.000;-0.000;0.000}" -f $file.Name, $delta)
    }
}
Write-Output ("mean delta {0:+0.000;-0.000;0.000}" -f ($sum / [math]::Max(1, $count)))
