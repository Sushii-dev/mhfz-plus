# Self-check error (an in-between frame at t = 0.5 predicted from the real frames around it, against the real one) on
# the test app's scenes, averaged per scene, for one or more builds.
#   tools\selfcheckbench.ps1 -Dlls build\a.dll,build\b.dll [-Ini "lerp_nonpos=2"] [-Scenes label,label2]
param([string]$Dlls = "", [string]$Ini = "", [string]$Scenes = "")
$ErrorActionPreference = "Stop"
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "sc-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
$all = [ordered]@{ plain = "4"; billboards = "4 1"; swap = "4 0 1"; tag = "4 0 0 1"; label = "4 0 0 0 1"; label2 = "4 0 0 0 2";
                   menu = "4 0 0 0 0 1"; skip = "4 0 0 0 0 0 1"; ramp = "4 0 0 0 0 0 0 1"; live = "4 0 0 0 0 0 0 0 1"; town = "6 0 0 0 0 0 0 0 0 1"; fastlabel = "6 0 0 0 3"; hardlabel = "6 0 0 0 4"; flick = "6 0 0 0 5"; flick2 = "6 0 0 0 6"; brazier = "6 0 0 0 7"; brazierfixed = "6 0 0 0 8"; crowdlabels = "6 0 0 0 9"; titles = "6 0 0 0 10" }
$names = if ($Scenes) { $Scenes -split ',' } else { $all.Keys }
foreach ($d in ($Dlls -split '[,\s]+')) {
    Copy-Item $d "$w\d3d9.dll" -Force
    $line = @()
    foreach ($s in $names) {
        Remove-Item -Recurse -Force "$w\selfcheck" -ErrorAction SilentlyContinue
        Set-Content "$w\mhfz-fps.ini" ("[fps]`r`n" + ($Ini -replace ';', "`r`n") + "`r`nmode=2`r`ntarget_fps=120`r`npresent_interval=0`r`nstats_interval=0`r`nselfcheck=1`r`nselfcheck_interval_ms=200`r`nselfcheck_saves=0`r`nselfcheck_ablate=0`r`n")
        Start-Process "$w\testapp.exe" -ArgumentList ($all[$s] -split ' ') -WorkingDirectory $w -Wait
        $m = @(); $bad = @()
        foreach ($l in Get-Content "$w\mhfz-fps.log") { if ($l -match 'selfcheck #\d+: mean error ([\d.]+), p99 block ([\d.]+), bad blocks ([\d.]+)%') { $m += [double]$Matches[1]; $bad += [double]$Matches[3] } }
        $am = if ($m.Count) { ($m | Measure-Object -Average).Average } else { -1 }
        $ab = if ($bad.Count) { ($bad | Measure-Object -Average).Average } else { -1 }
        $line += "{0} {1:N2}/{2:N2}%" -f $s, $am, $ab
    }
    "{0,-28} {1}" -f (Split-Path $d -Leaf), ($line -join "  ")
}
