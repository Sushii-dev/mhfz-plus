# Does the test app exit cleanly with a given d3d9.dll? Runs a scene N times, each with a time limit, and reports runs
# that hang (killed at the limit) - a hang at process exit showed up as "process exit" in the log with the process alive.
#   tools\exittest.ps1 -Dll build\d3d9.dll [-Scene "5 0 0 0 9"] [-Runs 5] [-Ini "field_match=1"] [-LimitSec 40]
param([string]$Dll = "", [string]$Scene = "5 0 0 0 9", [int]$Runs = 5, [string]$Ini = "", [int]$LimitSec = 40)
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "exit-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
Copy-Item $(if ($Dll) { $Dll } else { "$b\d3d9.dll" }) "$w\d3d9.dll" -Force
$hung = 0
for ($r = 0; $r -lt $Runs; $r++) {
    Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=120`r`npresent_interval=0`r`nstats_interval=0`r`nretro=0`r`n" + ($Ini -replace ';', "`r`n") + "`r`n")
    $p = Start-Process -NoNewWindow "$w\testapp.exe" -ArgumentList ($Scene -split ' ') -WorkingDirectory $w -PassThru
    if (-not $p.WaitForExit($LimitSec * 1000)) {
        $hung++
        $last = (Get-Content "$w\mhfz-fps.log" -Tail 1)
        "run ${r}: HUNG (log ends: $last)"
        $p | Stop-Process -Force -Confirm:$false -ErrorAction SilentlyContinue
    } else {
        "run ${r}: exit code $($p.ExitCode)"
    }
}
"$hung of $Runs runs hung"
