# Runs the test app with the self-check on and prints the self-check / matching summary.
# Usage: tools\runtest.ps1 "<testapp args>" [dll to test instead of build\d3d9.dll]
param([string]$AppArgs = "5", [string]$Dll = "")
$ErrorActionPreference = "Stop"
$b = Join-Path $PSScriptRoot "..\build"
$saved = $null
if ($Dll) { $saved = Join-Path $b "d3d9.keep.dll"; Copy-Item (Join-Path $b "d3d9.dll") $saved -Force; Copy-Item $Dll (Join-Path $b "d3d9.dll") -Force }
Set-Content (Join-Path $b "mhfz-fps.ini") "[fps]`r`nmode=2`r`ntarget_fps=$env:REG_TARGET`r`nstats_interval=2`r`nselfcheck=1`r`nselfcheck_interval_ms=300`r`nselfcheck_saves=0`r`nretro=0`r`n" -NoNewline
Remove-Item (Join-Path $b "mhfz-fps.log") -ErrorAction SilentlyContinue
Start-Process -NoNewWindow -FilePath (Join-Path $b "testapp.exe") -ArgumentList ($AppArgs -split ' ') -WorkingDirectory $b -Wait
if ($saved) { Copy-Item $saved (Join-Path $b "d3d9.dll") -Force; Remove-Item $saved }
Get-Content (Join-Path $b "mhfz-fps.log") | Select-String "selfcheck:|timing" | ForEach-Object {
    $_.Line -replace '^.*(selfcheck:)', '$1' -replace '^.*(rejected pairs)', '$1' }
