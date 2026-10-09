# Hashes the in-between frames at fixed points (debug_replayhash) on every tick of the test app's scenes and of
# test\regrouptest (Windows' d3d9 and DXVK), so two builds can be compared: an optimisation of the replay must give
# the same images on every tick.
#   tools\replayhash.ps1 -Dll build\d3d9.dll -Out build\rh-new        (records)
#   tools\replayhash.ps1 -A build\rh-old -B build\rh-new               (compares)
param([string]$Dll = "", [string]$Out = "", [string]$A = "", [string]$B = "", [string]$Ini = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if ($A) {
    $tot = 0; $diff = 0
    foreach ($f in Get-ChildItem "$A\*.txt") {
        $ha = @{}; foreach ($l in Get-Content $f.FullName) { $p = $l -split ' ', 2; $ha[$p[0]] = $p[1] }
        $other = Join-Path $B $f.Name
        if (-not (Test-Path $other)) { "{0,-14} missing in {1}" -f $f.BaseName, $B; continue }
        $hb = @{}; foreach ($l in Get-Content $other) { $p = $l -split ' ', 2; $hb[$p[0]] = $p[1] }
        $common = @($ha.Keys | Where-Object { $hb.ContainsKey($_) })
        $d = @($common | Where-Object { $ha[$_] -ne $hb[$_] })
        $zero = @($common | Where-Object { $ha[$_] -match '0000000000000000' })
        $tot += $common.Count; $diff += $d.Count
        "{0,-14} {1,4} ticks compared, {2,3} differ{3}{4}" -f $f.BaseName, $common.Count, $d.Count,
            $(if ($d) { " (first: tick $(($d | ForEach-Object { [int]$_ } | Sort-Object)[0]))" } else { "" }),
            $(if ($zero) { " ($($zero.Count) without an image)" } else { "" })
    }
    "TOTAL: $tot ticks, $diff differ"
    return
}
$b = Join-Path $root "build"
$work = Join-Path $b "rh-run"
New-Item -ItemType Directory -Force $work, $Out | Out-Null
Copy-Item "$b\testapp.exe", "$b\regrouptest.exe" $work -Force
if (-not (Test-Path "$work\dxvk")) { Copy-Item "$b\dxvk" "$work\dxvk" -Recurse -Force }
Copy-Item $(if ($Dll) { $Dll } else { "$b\d3d9.dll" }) "$work\d3d9.dll" -Force
$base = "[fps]`r`nmode=2`r`ntarget_fps=120`r`npresent_interval=0`r`nstats_interval=0`r`nselfcheck=0`r`nretro=0`r`ndebug_replayhash=1`r`n" + ($Ini -replace ';', "`r`n") + "`r`n"
$scenes = [ordered]@{ plain = "5"; billboards = "5 1"; swap = "5 0 1"; tag = "5 0 0 1"; label = "5 0 0 0 1"; menu = "5 0 0 0 0 1";
                      skip = "5 0 0 0 0 0 1"; ramp = "5 0 0 0 0 0 0 1"; live = "5 0 0 0 0 0 0 0 1"; heavy = "8 0 0 0 0 0 0 0 0 1";
 all = "6 1 1 1 2 1 1 1 1 1" }
foreach ($s in $scenes.Keys) {
    Set-Content "$work\mhfz-fps.ini" $base
    Start-Process -NoNewWindow "$work\testapp.exe" -ArgumentList ($scenes[$s] -split ' ') -WorkingDirectory $work -Wait
    Get-Content "$work\mhfz-fps.log" | Where-Object { $_ -match 'replayhash (\d+) (.*)$' } | ForEach-Object { "$($Matches[1]) $($Matches[2])" } |
        Set-Content "$Out\$s.txt"
    "{0,-14} {1} ticks" -f $s, (Get-Content "$Out\$s.txt").Count
}
foreach ($be in '', 'dxvk\d3d9.dll') {
    foreach ($rg in 0, 2) {
        $name = "cascades-" + $(if ($be) { 'dxvk' } else { 'win' }) + "-rg$rg"
        Set-Content "$work\mhfz-fps.ini" ($base + "regroup=$rg`r`n" + $(if ($be) { "d3d9_backend=$be`r`n" } else { "" }))
        Push-Location $work
        & "$work\regrouptest.exe" 40 25 *> $null
        Pop-Location
        Get-Content "$work\mhfz-fps.log" | Where-Object { $_ -match 'replayhash (\d+) (.*)$' } | ForEach-Object { "$($Matches[1]) $($Matches[2])" } |
            Set-Content "$Out\$name.txt"
        "{0,-14} {1} ticks" -f $name, (Get-Content "$Out\$name.txt").Count
    }
}
