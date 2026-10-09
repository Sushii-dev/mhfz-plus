# Timing of pairbench builds on the retro saves, robust to noise: each build runs each save -Runs times (alternating
# builds), and the minimum per-tick average is reported per save.
#   tools\benchmin.ps1 -Exes build\pairbench-a.exe,build\pairbench.exe [-Saves 1258\001,1322\003] [-Threads 1] [-Runs 5]
param([string]$Exes, [string]$Saves = "1219\001,1258\001,1322\001,1322\003", [int]$Threads = 0, [int]$Runs = 5, [int]$Rep = 4)
$root = Split-Path $PSScriptRoot
$ex = $Exes -split ','
$sv = $Saves -split ','
$best = @{}
for ($r = 0; $r -lt $Runs; $r++) {
    foreach ($s in $sv) {
        foreach ($e in $ex) {
            $line = (& "$root\$e" -seq "$root\build\user-retro-$s\ticks.seq" -rep $Rep -t $Threads 2>&1 | Select-String '^average' | ForEach-Object { $_.Line })
            $ms = ($line | ForEach-Object { if ($_ -match 'average ([\d.]+) ms') { [double]$Matches[1] } } | Measure-Object -Minimum).Minimum
            $tx = ($line | ForEach-Object { if ($_ -match 'text ([\d.]+)\)') { [double]$Matches[1] } } | Measure-Object -Minimum).Minimum
            $k = "$s|$e"
            if (-not $best.ContainsKey($k) -or $ms -lt $best[$k][0]) { $best[$k] = @($ms, $tx) }
        }
    }
}
foreach ($s in $sv) {
    $row = "{0,-10}" -f $s
    foreach ($e in $ex) { $b = $best["$s|$e"]; $row += "  {0}: {1:N2} ms (text {2:N2})" -f (Split-Path $e -Leaf), $b[0], $b[1] }
    $row
}
