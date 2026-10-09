# BuildPairs cost on the test app's town-sized scene (scene 15), averaged over the stats windows of a run.
#   tools\pairbench.ps1 -Dlls build\d3d9.dll,build\other.dll [-Seconds 20] [-Runs 2]
param([string]$Dlls = "", [int]$Seconds = 20, [int]$Runs = 2)
$ErrorActionPreference = "Stop"
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "bench"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
$list = if ($Dlls) { $Dlls -split '[,\s]+' } else { @("$b\d3d9.dll") }
foreach ($d in $list) {
    $pair = @(); $shown = @(); $rep = @(); $logic = @(); $ph = @{}
    for ($r = 0; $r -lt $Runs; $r++) {
        Copy-Item $d "$w\d3d9.dll" -Force
        Set-Content "$w\mhfz-fps.ini" "[fps]`r`nmode=2`r`ntarget_fps=170`r`npresent_interval=0`r`nstats_interval=2`r`nselfcheck=0`r`nretro=0`r`n"
        Start-Process -NoNewWindow "$w\testapp.exe" -ArgumentList (@("$Seconds") + @('0') * 8 + @('1')) -WorkingDirectory $w -Wait
        $lines = Get-Content "$w\mhfz-fps.log"
        $k = 0
        foreach ($l in $lines) {
            if ($l -match 'stats [\d.]+s: .*shown ([\d.]+) fps.*logic ([\d.]+) ms, pair ([\d.]+) ms, replay ([\d.]+) ms') { $k++; if ($k -gt 1) { $shown += [double]$Matches[1]; $logic += [double]$Matches[2]; $pair += [double]$Matches[3]; $rep += [double]$Matches[4] } }
            elseif ($k -gt 1 -and $l -match 'pair phases \(ms/tick\):(.*)$') {
                foreach ($m in [regex]::Matches($Matches[1], '([\w.+]+) ([\d.]+)')) { $ph[$m.Groups[1].Value] += @([double]$m.Groups[2].Value) }
            }
        }
    }
    $avg = { param($a) if ($a.Count) { ($a | Measure-Object -Average).Average } else { 0 } }
    "{0,-22} logic {4:N2} ms  pair {1:N2} ms  replay {5:N2} ms  shown {2:N1} fps  ({3} windows)" -f (Split-Path $d -Leaf), (& $avg $pair), (& $avg $shown), $pair.Count, (& $avg $logic), (& $avg $rep)
    "    " + (($ph.Keys | Sort-Object { -(& $avg $ph[$_]) } | Where-Object { (& $avg $ph[$_]) -ge 0.03 } | ForEach-Object { "{0} {1:N2}" -f $_, (& $avg $ph[$_]) }) -join "  ")
}
