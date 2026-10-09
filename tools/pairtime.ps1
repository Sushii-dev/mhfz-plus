# Matching time per tick as the mod itself measures it (the stats line's "pair X ms"), per test-app scene and build:
# a like-for-like speed comparison of d3d9.dll builds.
#   tools\pairtime.ps1 -Dlls build\d3d9.a.dll,build\d3d9.b.dll [-Scenes town,crowdlabels] [-Runs 2]
param([string]$Dlls, [string]$Scenes = "town,crowdlabels,titles,heavy", [int]$Runs = 2)
$ErrorActionPreference = "Stop"
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "pt-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
$all = [ordered]@{ town = "6 0 0 0 0 0 0 0 0 1"; crowdlabels = "6 0 0 0 9"; titles = "6 0 0 0 10"; heavy = "8 0 0 0 0 0 0 0 0 1"; label = "6 0 0 0 1"; flick2 = "6 0 0 0 6" }
$res = @{}
for ($r = 0; $r -lt $Runs; $r++) {
    foreach ($s in $Scenes -split ',') {
        foreach ($d in $Dlls -split ',') {
            Copy-Item (Join-Path (Split-Path $b) $d) "$w\d3d9.dll" -Force
            Set-Content "$w\mhfz-fps.ini" "[fps]`r`nmode=2`r`ntarget_fps=170`r`npresent_interval=0`r`nstats_interval=2`r`nselfcheck=0`r`nretro=0`r`n"
            Start-Process "$w\testapp.exe" -ArgumentList ($all[$s] -split ' ') -WorkingDirectory $w -Wait
            $v = @(Get-Content "$w\mhfz-fps.log" | ForEach-Object { if ($_ -match 'stats [\d.]+s: .* pair ([\d.]+) ms') { [double]$Matches[1] } })
            if ($v.Count -gt 1) { $v = $v[1..($v.Count - 1)] }  # (the first interval includes the warm-up)
            $k = "$s|$d"
            if (-not $res.ContainsKey($k)) { $res[$k] = @() }
            $res[$k] += $v
        }
    }
}
foreach ($s in $Scenes -split ',') {
    $row = "{0,-12}" -f $s
    foreach ($d in $Dlls -split ',') {
        $v = $res["$s|$d"]
        $row += "  {0}: {1:N2} ms ({2} intervals)" -f (Split-Path $d -Leaf), (($v | Measure-Object -Average).Average), $v.Count
    }
    $row
}
