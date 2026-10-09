# Records the per-tick pairing hash (debug_pairhash) of every test-app scene for one d3d9.dll, so two builds can be
# compared: an optimisation must give the same hash on every tick.
#   tools\pairhash.ps1 -Dll build\d3d9.dll -Out build\ph-new        (records)
#   tools\pairhash.ps1 -A build\ph-old -B build\ph-new               (compares)
param([string]$Dll = "", [string]$Out = "", [string]$A = "", [string]$B = "", [string]$Ini = "", [string]$Only = "")   # -Ini "overlay=1;x=y": extra settings; -Only "atlas,label": those scenes
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if ($A) {
    $Compare = @($A, $B)
    $tot = 0; $diff = 0
    foreach ($f in Get-ChildItem "$($Compare[0])\*.txt") {
        $ha = @{}; foreach ($l in Get-Content $f.FullName) { $p = $l -split ' '; $ha[$p[0]] = $p[1] }
        $hb = @{}; $other = Join-Path $Compare[1] $f.Name
        if (-not (Test-Path $other)) { "{0,-10} missing in {1}" -f $f.BaseName, $Compare[1]; continue }
        foreach ($l in Get-Content $other) { $p = $l -split ' '; $hb[$p[0]] = $p[1] }
        $common = @($ha.Keys | Where-Object { $hb.ContainsKey($_) })
        $d = @($common | Where-Object { $ha[$_] -ne $hb[$_] })
        $tot += $common.Count; $diff += $d.Count
        "{0,-10} {1,4} ticks compared, {2,3} differ{3}" -f $f.BaseName, $common.Count, $d.Count, $(if ($d) { " (first: tick $(($d | ForEach-Object { [int]$_ } | Sort-Object)[0]))" } else { "" })
    }
    "TOTAL: $tot ticks, $diff differ"
    return
}
$b = Join-Path $root "build"
$work = Join-Path $b "ph-run"
New-Item -ItemType Directory -Force $work, $Out | Out-Null
Copy-Item "$b\testapp.exe" $work -Force
Copy-Item $(if ($Dll) { $Dll } else { "$b\d3d9.dll" }) "$work\d3d9.dll" -Force
$scenes = [ordered]@{ plain = "5"; billboards = "5 1"; swap = "5 0 1"; tag = "5 0 0 1"; label = "5 0 0 0 1"; label2 = "5 0 0 0 2";
                      menu = "5 0 0 0 0 1"; skip = "5 0 0 0 0 0 1"; ramp = "5 0 0 0 0 0 0 1"; live = "5 0 0 0 0 0 0 0 1";
                      heavy = "8 0 0 0 0 0 0 0 0 1"; all = "6 1 1 1 2 1 1 1 1 1"; atlas = "5 0 0 0 14"; titles = "5 0 0 0 10"; crowd = "5 0 0 0 9" }
foreach ($s in $scenes.Keys) {
    if ($Only -and ($Only -split ',') -notcontains $s) { continue }
    Set-Content "$work\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=120`r`npresent_interval=0`r`nstats_interval=0`r`nselfcheck=0`r`nretro=0`r`ndebug_pairhash=1`r`n" + ($Ini -replace ';', "`r`n") + "`r`n")
    Start-Process -NoNewWindow "$work\testapp.exe" -ArgumentList ($scenes[$s] -split ' ') -WorkingDirectory $work -Wait
    Get-Content "$work\mhfz-fps.log" | Where-Object { $_ -match 'pairhash (\d+) (\w+) (\d+)' } | ForEach-Object { "$($Matches[1]) $($Matches[2]) $($Matches[3])" } |
        Set-Content "$Out\$s.txt"
    "{0,-10} {1} ticks" -f $s, (Get-Content "$Out\$s.txt").Count
}
