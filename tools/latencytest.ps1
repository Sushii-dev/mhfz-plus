# latency_trim A/B on a test-app scene: tick rate (the game's own), shown frames, and the hold it found.
#   tools\latencytest.ps1 -Dll build\d3d9.dll [-Scene "10 0 0 0 0 0 0 0 0 1"] [-Ini "x=1"]
param([string]$Dll = "", [string]$Scene = "10 0 0 0 0 0 0 0 0 1", [string]$Ini = "")
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "lt-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
Copy-Item $(if ($Dll) { $Dll } else { "$b\d3d9.dll" }) "$w\d3d9.dll" -Force
foreach ($lt in 0, 1) {
    Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=0`r`npresent_interval=0`r`nstats_interval=4`r`nlatency_trim=$lt`r`n" + ($Ini -replace ';', "`r`n") + "`r`n")
    Start-Process -NoNewWindow "$w\testapp.exe" -ArgumentList ($Scene -split ' ') -WorkingDirectory $w -Wait -RedirectStandardOutput "$w\out$lt.txt" | Out-Null
    "== latency_trim=$lt"
    Get-Content "$w\out$lt.txt" | Where-Object { $_ -match 'ticks:' }
    Get-Content "$w\mhfz-fps.log" | Where-Object { $_ -match '\] stats |lower input latency' } | Select-Object -Last 2 | ForEach-Object { $_.Substring(0, [Math]::Min(230, $_.Length)) }
}
