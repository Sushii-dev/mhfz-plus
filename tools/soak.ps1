# Long run of a test-app scene with everything a user might turn on: does it stay up, exit cleanly, and keep its memory?
#   tools\soak.ps1 -Dll build\d3d9.dll [-Seconds 600] [-Scene "0 0 0 0 0 0 0 0 1"] [-Ini "overlay=2;retro=1"]
param([string]$Dll = "", [int]$Seconds = 600, [string]$Scene = "0 0 0 0 0 0 0 0 1", [string]$Ini = "overlay=2;retro=1;latency_trim=1")
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "soak-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
Copy-Item $(if ($Dll) { $Dll } else { "$b\d3d9.dll" }) "$w\d3d9.dll" -Force
Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=0`r`npresent_interval=0`r`nstats_interval=60`r`n" + ($Ini -replace ';', "`r`n") + "`r`n")
$p = Start-Process -NoNewWindow "$w\testapp.exe" -ArgumentList (@("$Seconds") + ($Scene -split ' ')) -WorkingDirectory $w -PassThru -RedirectStandardOutput "$w\out.txt"
$peak = 0
while (-not $p.WaitForExit(15000)) {
    try { $p.Refresh(); $peak = [Math]::Max($peak, $p.PrivateMemorySize64 / 1MB) } catch {}
    if ((Get-Date) - $p.StartTime -gt [TimeSpan]::FromSeconds($Seconds + 90)) { "HUNG (killed)"; $p | Stop-Process -Force -Confirm:$false; break }
}
"exit code $($p.ExitCode), peak private memory {0:F0} MB" -f $peak
Get-Content "$w\out.txt" | Where-Object { $_ -match 'ticks:|released' }
Get-Content "$w\mhfz-fps.log" | Where-Object { $_ -match 'memory:|\] stats ' } | ForEach-Object { $_.Substring(0, [Math]::Min(200, $_.Length)) }
