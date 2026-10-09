# Label regression: runs scene 10 ("Small EX" glyph quads), saves the rolling recorder and crops the label row
# of consecutive frames into build\label_<tag>.png. Usage: tools\labeltest.ps1 <tag> [dll]
param([string]$Tag = "new", [string]$Dll = "", [string]$AppArgs = "5 0 0 0 1", [string]$Box = "60 140 340 166")
$ErrorActionPreference = "Stop"
$b = Join-Path $PSScriptRoot "..\build"
$saved = $null
if ($Dll) { $saved = Join-Path $b "d3d9.keep.dll"; Copy-Item (Join-Path $b "d3d9.dll") $saved -Force; Copy-Item $Dll (Join-Path $b "d3d9.dll") -Force }
$retro = Join-Path $b "retro"
if (Test-Path $retro) { Get-ChildItem $retro | ForEach-Object { Remove-Item $_.FullName -Recurse -Force } }
$ini = Join-Path $b "mhfz-fps.ini"
$base = "[fps]`r`nmode=2`r`ntarget_fps=$env:REG_TARGET`r`nstats_interval=2`r`nselfcheck=0`r`nretro=1`r`n"
Set-Content $ini ($base + "retro_trigger=0`r`n") -NoNewline
$p = Start-Process -NoNewWindow -FilePath (Join-Path $b "testapp.exe") -ArgumentList ($AppArgs -split " ") -WorkingDirectory $b -PassThru
Start-Sleep -Milliseconds 3000
Set-Content $ini ($base + "retro_trigger=1`r`n") -NoNewline
$p.WaitForExit()
if ($saved) { Copy-Item $saved (Join-Path $b "d3d9.dll") -Force; Remove-Item $saved }
python (Join-Path $PSScriptRoot "strip.py") (Join-Path $retro "001") 100 24 @($Box -split " ") (Join-Path $b "label_$Tag.png") 1

