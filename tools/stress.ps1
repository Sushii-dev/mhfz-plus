# Runs every test-app scene and the regroup test under several option sets and reports exceptions, crashes and
# whether stats kept coming (a robustness sweep before handing a build over).
#   tools\stress.ps1 [-Dll build\d3d9.dll]
param([string]$Dll = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$b = Join-Path $root "build"
$w = Join-Path $b "stress-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe", "$b\regrouptest.exe" $w -Force
if (-not (Test-Path "$w\dxvk")) { Copy-Item "$b\dxvk" "$w\dxvk" -Recurse -Force }
Copy-Item $(if ($Dll) { $Dll } else { "$b\d3d9.dll" }) "$w\d3d9.dll" -Force
$sets = [ordered]@{
    default = ""; retro = "retro=1;retro_frames=48;retro_ticks=12"; noasync = "pair_async=0;precompute=0"; nonpos1 = "lerp_nonpos=1;cam_paths=0;text_runs=0"
    selfcheck = "selfcheck=1;selfcheck_interval_ms=300;selfcheck_saves=0"; regroupcheck = "regroup_check=1"; dxvk = "d3d9_backend=dxvk\d3d9.dll"; field = "field_match=1"
}
$scenes = @("3", "3 1", "3 0 1", "3 0 0 1", "3 0 0 0 1", "3 0 0 0 2", "3 0 0 0 3", "3 0 0 0 4", "3 0 0 0 5", "3 0 0 0 0 1", "3 0 0 0 0 0 1",
            "3 0 0 0 0 0 0 1", "3 0 0 0 0 0 0 0 1", "4 0 0 0 0 0 0 0 0 1", "4 1 1 1 2 1 1 1 1 1")
$bad = 0
foreach ($k in $sets.Keys) {
    $line = "{0,-13}" -f $k
    foreach ($s in $scenes) {
        Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=144`r`npresent_interval=0`r`nstats_interval=1`r`n" + ($sets[$k] -replace ';', "`r`n") + "`r`n")
        $p = Start-Process "$w\testapp.exe" -ArgumentList ($s -split ' ') -WorkingDirectory $w -PassThru -RedirectStandardOutput "$w\out.txt"
        if (-not $p.WaitForExit(30000)) {
            $keep = Join-Path $b ("hang-" + (Get-Date -Format HHmmss) + "-$k.log")
            Copy-Item "$w\mhfz-fps.log" $keep -ErrorAction SilentlyContinue
            Copy-Item "$w\out.txt" "$keep.out.txt" -ErrorAction SilentlyContinue  # the app's exit markers
            $p.Kill(); $null = $p.WaitForExit(5000); $line +=" HANG[$s -> $keep]"; $bad++; continue
        }
        $log = Get-Content "$w\mhfz-fps.log" -ErrorAction SilentlyContinue
        if (-not ($log | Select-Object -First 1 | Where-Object { $_ -match "\(pid $($p.Id)\)" })) {
            # the log is an earlier run's: this process never loaded the mod (e.g. 0xC0000142, Windows failing to start
            # a console process under load) - not the mod's doing, but say so
            $line += " [${s}: did not start, exit $('{0:X8}' -f $p.ExitCode)]"; $bad++; continue
        }
        $exc = @($log | Where-Object { $_ -match 'EXCEPTION' }).Count
        $st = @($log | Where-Object { $_ -match 'stats 1s' }).Count
        if ($p.ExitCode -ne 0 -or $exc -or $st -lt 1) { $line += " [${s}: exit $($p.ExitCode) exc $exc stats $st]"; $bad++ } else { $line += " ." }
    }
    Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=144`r`npresent_interval=0`r`nstats_interval=1`r`n" + ($sets[$k] -replace ';', "`r`n") + "`r`n")
    Push-Location $w
    & "$w\regrouptest.exe" 60 -33 -1 0 0 2 *> $null
    $code = $LASTEXITCODE
    Pop-Location
    $exc = @(Get-Content "$w\mhfz-fps.log" | Where-Object { $_ -match 'EXCEPTION' }).Count
    if ($code -ne 0 -or $exc) { $line += " [regrouptest: exit $code exc $exc]"; $bad++ } else { $line += " r" }
    $line
}
if ($bad) { "PROBLEMS: $bad" } else { "all clean" }
