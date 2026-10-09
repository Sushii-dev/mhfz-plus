# Everything a build has to pass before it goes into the game, in one go (about 25 minutes; run it with pwsh - in Windows
# PowerShell 5.1 a test program's stderr line stops the regroup test):
#   tools\validate.ps1 -Dll build\d3d9.<name>.dll -Ref build\d3d9.<last deployed>.dll [-RefHash build\ph-<ref>]
# pairing hash identical to the reference (or say why not), clean exits, regroup bit-exact, self-check error per scene
# next to the reference, the stress sweep, and DXVK starts (the 4th-Present hang of n20b/n20c).
param([Parameter(Mandatory)] [string]$Dll, [Parameter(Mandatory)] [string]$Ref, [string]$RefHash = "", [int]$StartRuns = 200)
$t = $PSScriptRoot
$b = Join-Path (Split-Path $t) "build"
$name = [IO.Path]::GetFileNameWithoutExtension($Dll) -replace '^d3d9\.', ''
"== DXVK starts"
& "$t\starthang.ps1" -Dlls $Dll -Runs $StartRuns
"== pairhash vs $(Split-Path $Ref -Leaf)"
if (-not $RefHash) {
    $RefHash = Join-Path $b ("ph-" + ([IO.Path]::GetFileNameWithoutExtension($Ref) -replace '^d3d9\.', ''))
    if (-not (Test-Path $RefHash)) { & "$t\pairhash.ps1" -Dll $Ref -Out $RefHash | Out-Null }
}
& "$t\pairhash.ps1" -Dll $Dll -Out "$b\ph-$name" | Out-Null
& "$t\pairhash.ps1" -A $RefHash -B "$b\ph-$name" | Select-Object -Last 1
"== exit test"
& "$t\exittest.ps1" -Dll $Dll -Runs 6 | Select-Object -Last 1
"== regrouptest"
Copy-Item $Dll "$b\d3d9.dll" -Force
& "$t\regrouptest.ps1" | Select-Object -Last 1
"== selfcheck"
& "$t\selfcheckbench.ps1" -Dlls "$Ref,$Dll"
"== stress"
& "$t\stress.ps1" -Dll $Dll
