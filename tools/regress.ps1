# All test-app regressions in one go. Usage: tools\regress.ps1 [dll]  (default: build\d3d9.dll)
param([string]$Dll = "")
if (-not $env:REG_TARGET) { $env:REG_TARGET = "120" }
"target fps: $env:REG_TARGET"
$t = $PSScriptRoot
$b = Join-Path $t "..\build"
function Sc($name, $appArgs) {
    $r = & (Join-Path $t "runtest.ps1") $appArgs $Dll | Where-Object { $_ -match "^selfcheck" } | Select-Object -First 1
    "{0,-12} {1}" -f $name, $r
}
Sc "plain" "5"
Sc "billboards" "5 1"
Sc "name-tag" "5 0 0 1"
& (Join-Path $t "swaptest.ps1") $Dll | Out-Null
"swap ghosts  " + ((python (Join-Path $t "ghosts.py") (Join-Path $b "retro") | Select-Object -Skip 1 -First 1) -replace '\s+', ' ')
"fog card     " + ((python (Join-Path $t "cardpop.py") (Join-Path $b "retro\001") (Join-Path $b "card_regress.png") | Select-Object -Skip 1) -join ' | ')
& (Join-Path $t "labeltest.ps1") "regress" $Dll "5 0 0 0 0 1" "150 262 200 297" | Out-Null
"static text  " + (python (Join-Path $t "statictext.py") (Join-Path $b "retro\001") 150 262 200 297)
& (Join-Path $t "labeltest.ps1") "regress" $Dll | Out-Null
"moving label see build\label_regress.png"
& (Join-Path $t "labeltest.ps1") "skip" $Dll "5 0 0 0 0 0 1" "14 4 30 16" | Out-Null
"skipped setups " + ((python (Join-Path $t "regioncolor.py") (Join-Path $b "retro\001") 14 4 30 16) -join ' | ')
& (Join-Path $t "labeltest.ps1") "ramp" $Dll "5 0 0 0 0 0 0 1" "14 4 30 16" | Out-Null
"steady ramp  " + (python (Join-Path $t "rampcheck.py") (Join-Path $b "retro\001") 14 4 30 16)

& (Join-Path $t "labeltest.ps1") "live" $Dll "5 0 0 0 0 0 0 0 1" "14 4 30 16" | Out-Null
"read-live    " + (python (Join-Path $t "rampcheck.py") (Join-Path $b "retro\001") 14 4 30 16)
& (Join-Path $t "labeltest.ps1") "lswap" $Dll "5 0 0 0 2" | Out-Null
"label swaps  see build\label_lswap.png"
