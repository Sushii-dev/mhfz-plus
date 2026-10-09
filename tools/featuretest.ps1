# Tests the opt-in features that the test app can exercise:
#  - input_latch: test\inputtest.exe types short F13 taps (12 ms) while a game-like 30 Hz loop reads a DirectInput
#    keyboard once per tick; prints how many it saw with the latch off and on.
#  - backend fallback: a d3d9_backend whose Direct3DCreate9 returns nothing (build\nullbackend.dll) must leave the game on
#    Windows' own Direct3D 9, interpolating.
#  - camera probe: camprobe_test=1 points the hardware write breakpoints at a variable of the mod's own (written every
#    frame), started through camprobe_trigger while the test app runs; the log must name the writing code.
#   tools\featuretest.ps1 [-Dll build\d3d9.dll] [-InputTest build\inputtest.exe] [-Backend ""|dxvk\d3d9.dll]
param([string]$Dll = "", [string]$InputTest = "", [string]$Backend = "", [switch]$WithInput)   # -WithInput: the input_latch test types F13 into whatever window is in front - only when nobody is using the PC
$b = Join-Path (Split-Path $PSScriptRoot) "build"
if (-not $Dll) { $Dll = "$b\d3d9.dll" }
if (-not $InputTest) { $InputTest = "$b\inputtest.exe" }
$w = Join-Path $b "feature-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item $Dll "$w\d3d9.dll" -Force
Copy-Item $InputTest, "$b\testapp.exe" $w -Force
if (-not (Test-Path "$w\dxvk")) { Copy-Item "$b\dxvk" "$w\dxvk" -Recurse -Force }
function Ini($extra) {
    $be = if ($Backend) { "d3d9_backend=$Backend`r`n" } else { "" }
    Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=144`r`npresent_interval=0`r`nstats_interval=4`r`n$be" + ($extra -replace ';', "`r`n") + "`r`n")
}
if ($WithInput) {
"== input_latch (12 ms taps)"
foreach ($on in 0, 1) {
    Ini "input_latch=$on"
    $out = & "$w\inputtest.exe" 10 12 90
    "latch=${on}: $out"
    Get-Content "$w\mhfz-fps.log" | Where-Object { $_ -match 'input' } | Select-Object -Last 3 | ForEach-Object { "   $_" }
}
}
"== camera probe (self-test)"
Ini "camprobe_test=1;camprobe_s=2;camprobe_trigger=0"
$p = Start-Process -NoNewWindow "$w\testapp.exe" -ArgumentList 7 -WorkingDirectory $w -PassThru -RedirectStandardOutput "$w\out.txt"
Start-Sleep -Seconds 2
Ini "camprobe_test=1;camprobe_s=2;camprobe_trigger=1"
$null = $p.WaitForExit(30000)
Get-Content "$w\mhfz-fps.log" | Where-Object { $_ -match 'camera probe|remote: camera|EXCEPTION|WATCHDOG' } | ForEach-Object { "   $_" }
"== backend fallback (a d3d9_backend that loads but can't start, like DXVK without Vulkan 1.3)"
Copy-Item "$b\nullbackend.dll" $w -Force
Set-Content "$w\mhfz-fps.ini" "[fps]`r`nmode=2`r`ntarget_fps=144`r`npresent_interval=0`r`nstats_interval=2`r`nd3d9_backend=$w\nullbackend.dll`r`n"
$p = Start-Process -NoNewWindow "$w\testapp.exe" -ArgumentList 4 -WorkingDirectory $w -PassThru -RedirectStandardOutput "$w\out.txt"
"   exited cleanly: " + ($p.WaitForExit(30000) -and $p.ExitCode -eq 0)
Get-Content "$w\mhfz-fps.log" | Where-Object { $_ -match 'could not start|stats 2s' } | Select-Object -First 2 | ForEach-Object { "   " + $_.Substring(0, [Math]::Min(150, $_.Length)) }
