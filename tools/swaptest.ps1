# Culling-swap regression: runs the test app's swap scene, saves the rolling recorder after ~2.5 s (swaps at ticks 19 and 47) and counts
# ghost foliage in the in-between frames. Usage: tools\swaptest.ps1 [dll to test]
param([string]$Dll = "")
$ErrorActionPreference = "Stop"
$b = Join-Path $PSScriptRoot "..\build"
$saved = $null
if ($Dll) { $saved = Join-Path $b "d3d9.keep.dll"; Copy-Item (Join-Path $b "d3d9.dll") $saved -Force; Copy-Item $Dll (Join-Path $b "d3d9.dll") -Force }
$retro = Join-Path $b "retro"
if (Test-Path $retro) { Get-ChildItem $retro | ForEach-Object { Remove-Item $_.FullName -Recurse -Force } }
$ini = Join-Path $b "mhfz-fps.ini"
$base = "[fps]`r`nmode=2`r`ntarget_fps=$env:REG_TARGET`r`nstats_interval=2`r`nselfcheck=0`r`nretro=1`r`n"
Set-Content $ini ($base + "retro_trigger=0`r`n") -NoNewline
Remove-Item (Join-Path $b "mhfz-fps.log") -ErrorAction SilentlyContinue
$p = Start-Process -NoNewWindow -FilePath (Join-Path $b "testapp.exe") -ArgumentList "7", "0", "1" -WorkingDirectory $b -PassThru
Start-Sleep -Milliseconds 2500
Set-Content $ini ($base + "retro_trigger=1`r`n") -NoNewline
$p.WaitForExit()
if ($saved) { Copy-Item $saved (Join-Path $b "d3d9.dll") -Force; Remove-Item $saved }
Get-Content (Join-Path $b "mhfz-fps.log") | Select-String "timing|remote" | ForEach-Object { $_.Line }
python (Join-Path $PSScriptRoot "ghosts.py") $retro
