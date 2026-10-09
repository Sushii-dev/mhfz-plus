# The per-tick costs on a town-crowd-sized MHF-like frame (test\regrouptest at scale 7: about 7,500 draws into three
# shadow cascades, two depth targets and the main pass), averaged over the stats windows of a run.
#   tools\crowdbench.ps1 -Dlls build\d3d9.dll,build\base\d3d9.dll [-Frames 360] [-Scale 7] [-Runs 2] [-Dxvk]
param([string]$Dlls = "", [int]$Frames = 360, [int]$Scale = 7, [int]$Runs = 2, [switch]$Dxvk, [string]$Ini = "", [int]$Sleep = -33)   # -33: MHF's limiter
$ErrorActionPreference = "Stop"
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "crowd"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\regrouptest.exe" $w -Force
if ($Dxvk -and -not (Test-Path "$w\dxvk")) { Copy-Item "$b\dxvk" "$w\dxvk" -Recurse -Force }
$list = if ($Dlls) { $Dlls -split '[,\s]+' } else { @("$b\d3d9.dll") }
$avg = { param($a) if ($a.Count) { ($a | Measure-Object -Average).Average } else { 0 } }
foreach ($d in $list) {
    $v = @{ logic = @(); pair = @(); replay = @(); shown = @(); extra = @(); real = @() }; $ph = @{}
    for ($r = 0; $r -lt $Runs; $r++) {
        Copy-Item $d "$w\d3d9.dll" -Force
        $lines = "[fps]`r`nmode=2`r`ntarget_fps=240`r`npresent_interval=0`r`nstats_interval=2`r`nselfcheck=0`r`nretro=0`r`n"
        if ($Dxvk) { $lines += "d3d9_backend=dxvk\d3d9.dll`r`n" }
        Set-Content "$w\mhfz-fps.ini" ($lines + ($Ini -replace ';', "`r`n") + "`r`n")
        Push-Location $w
        & "$w\regrouptest.exe" $Frames $Sleep -1 0 0 $Scale *> $null
        Pop-Location
        $k = 0
        foreach ($l in Get-Content "$w\mhfz-fps.log") {
            if ($l -match 'stats [\d.]+s: real ([\d.]+) fps, shown ([\d.]+) fps \| .*extra/tick ([\d.]+).*logic ([\d.]+) ms, pair ([\d.]+) ms, replay ([\d.]+) ms') {
                $k++
                if ($k -gt 1) { $v.real += [double]$Matches[1]; $v.shown += [double]$Matches[2]; $v.extra += [double]$Matches[3]; $v.logic += [double]$Matches[4]; $v.pair += [double]$Matches[5]; $v.replay += [double]$Matches[6] }
            } elseif ($k -gt 1 -and $l -match 'pair phases \(ms/tick\):(.*)$') {
                foreach ($m in [regex]::Matches($Matches[1], '([\w.+]+) ([\d.]+)')) { $ph[$m.Groups[1].Value] += @([double]$m.Groups[2].Value) }
            } elseif ($k -gt 1 -and $l -match 'regroup lists: [\d.]+ built/frame, ([\d.]+) ms/frame.*in-between frame runs ([\d.]+) commands') {
                $ph['lists'] += @([double]$Matches[1]); $ph['cmds/k'] += @([double]$Matches[2] / 1000)
            }
        }
    }
    $name = (Split-Path (Split-Path $d) -Leaf) + '\' + (Split-Path $d -Leaf)
    "{0,-26} real {7:N2}  logic {1:N2}  pair {2:N2}  replay {3:N2} ms  extra/tick {4:N2}  shown {5:N1} fps  ({6} windows)" -f $name,
        (& $avg $v.logic), (& $avg $v.pair), (& $avg $v.replay), (& $avg $v.extra), (& $avg $v.shown), $v.shown.Count, (& $avg $v.real)
    "    " + (($ph.Keys | Sort-Object { -(& $avg $ph[$_]) } | Where-Object { (& $avg $ph[$_]) -ge 0.03 } | ForEach-Object { "{0} {1:N2}" -f $_, (& $avg $ph[$_]) }) -join "  ")
}
