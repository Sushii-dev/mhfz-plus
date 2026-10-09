# Records a test-app scene as a tick sequence (ticks.seq, like the game's Delete-key clips) for pairbench -seq:
# the rolling recorder saves itself AtSec seconds in.
#   tools\recordseq.ps1 -Dll build\d3d9.dll -Scene "6 0 0 0 4" -Out build\seq-hardlabel [-AtSec 4] [-Ini "x=1;y=2"]
param([string]$Dll = "", [string]$Scene = "6 0 0 0 4", [string]$Out = "", [int]$AtSec = 4, [string]$Ini = "")
$ErrorActionPreference = "Stop"
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "seq-run"
Remove-Item -Recurse -Force $w -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
Copy-Item $(if ($Dll) { $Dll } else { "$b\d3d9.dll" }) "$w\d3d9.dll" -Force
Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=120`r`npresent_interval=0`r`nstats_interval=0`r`nretro=1`r`nretro_ticks=16`r`nretro_autosave=$AtSec`r`n" + ($Ini -replace ';', "`r`n") + "`r`n")
Start-Process -NoNewWindow "$w\testapp.exe" -ArgumentList ($Scene -split ' ') -WorkingDirectory $w -Wait
$seq = Get-ChildItem "$w\retro" -Recurse -Filter ticks.seq -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $seq) { "no ticks.seq written (see $w\mhfz-fps.log)"; return }
New-Item -ItemType Directory -Force $Out | Out-Null
Copy-Item "$($seq.Directory.FullName)\*" $Out -Force
"saved $((Get-ChildItem $Out).Count) files to $Out"
