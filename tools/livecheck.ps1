# Per tick of test\regrouptest: the game frame's recording replayed (t = 1, recorded order) against the frame as it was
# shown, in differing pixels (debug_replayhash's "live" figure; -1 = no comparison). Windows' d3d9 and DXVK.
#   tools\livecheck.ps1 [-Dll build\d3d9.dll] [-Frames 40]
param([string]$Dll = "", [int]$Frames = 40)
$ErrorActionPreference = "Stop"
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "rc-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\regrouptest.exe" $w -Force
if (-not (Test-Path "$w\dxvk")) { Copy-Item "$b\dxvk" "$w\dxvk" -Recurse -Force }
Copy-Item $(if ($Dll) { $Dll } else { "$b\d3d9.dll" }) "$w\d3d9.dll" -Force
foreach ($be in '', 'dxvk\d3d9.dll') {
    Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=120`r`npresent_interval=0`r`nstats_interval=0`r`nregroup=2`r`ndebug_replayhash=1`r`n" +
        $(if ($be) { "d3d9_backend=$be`r`n" } else { '' }))
    Push-Location $w
    & "$w\regrouptest.exe" $Frames 25 *> $null
    Pop-Location
    $r = @(Select-String -Path "$w\mhfz-fps.log" -Pattern 'replayhash (\d+) .* live (-?\d+)' | ForEach-Object { "$($_.Matches[0].Groups[1].Value):$($_.Matches[0].Groups[2].Value)" })
    "{0}: {1}" -f $(if ($be) { 'DXVK' } else { 'Windows d3d9' }), ($r -join ' ')
}
