# End-to-end test of MHFZ-Plus.exe against simulated MezeLounge / Rain programs (launcher\fakegame.cpp).
# Usage: launcher\simtest.ps1 [meze|rain|all] [-Fresh]   (results: sim\report.txt, sim\plus\MHFZ-Plus-<server>.log)
param([string]$Which = "all", [switch]$Fresh)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$sim = Join-Path $root "sim"
$kit = "$env:MHFZ_KIT"
$cc = "C:\Program Files\Windhawk\Compiler\bin\clang++.exe"
if ($Fresh -and (Test-Path $sim)) { Get-ChildItem $sim | ForEach-Object { Remove-Item $_.FullName -Recurse -Force } }
New-Item -ItemType Directory -Force $sim | Out-Null
function Tree($from, $to) { New-Item -ItemType Directory -Force $to | Out-Null; Copy-Item "$from\*" $to -Recurse -Force }
if (-not (Test-Path "$sim\pristine-meze")) {
    Tree "$kit\backup" "$sim\pristine-meze"; Tree "$kit\patch" "$sim\english-meze"
    Tree "$kit\backup-rain" "$sim\pristine-rain"; Tree "$kit\patch-rain" "$sim\english-rain"
}
& $cc --target=i686-w64-mingw32 -O2 -std=c++20 -static -s -mwindows -municode -o "$sim\fake.exe" "$PSScriptRoot\fakegame.cpp"
if ($LASTEXITCODE) { throw "fakegame build failed" }
# game folders start with the originals
Tree "$sim\pristine-meze" "$sim\meze"; Copy-Item "$sim\fake.exe" "$sim\meze\mhf.exe" -Force
Tree "$sim\pristine-rain" "$sim\rain\Client"; Copy-Item "$sim\fake.exe" "$sim\rain\Client\client.exe" -Force
New-Item -ItemType Directory -Force "$sim\rain\Launcher\Logs" | Out-Null
Copy-Item "$sim\fake.exe" "$sim\rain\Launcher\rain-server-launcher.exe" -Force
Remove-Item "$sim\meze\d3d9.dll", "$sim\rain\Client\d3d9.dll", "$sim\rain\Launcher\Logs\*" -ErrorAction SilentlyContinue
# the package
$plus = "$sim\plus"
New-Item -ItemType Directory -Force "$plus\fps" | Out-Null
Copy-Item "$root\build\MHFZ-Plus.exe" $plus -Force
Copy-Item "$root\build\d3d9.dll" "$plus\fps\" -Force
Set-Content "$plus\fps\mhfz-fps.ini" "[fps]`r`nenabled=1`r`n"
if (-not (Test-Path "$plus\english")) { Tree "$root\dist-work\english" "$plus\english" }
Set-Content "$plus\MHFZ-Plus.ini" @"
[general]
server=meze
[meze]
game=$sim\meze
english=1
fps=1
[rain]
game=$sim\rain\Client
english=1
fps=1
fps_mode=swap
"@
Remove-Item "$sim\report.txt" -ErrorAction SilentlyContinue
foreach ($s in @(if ($Which -eq "all") { "meze", "rain" } else { $Which })) {
    "=== $s"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process "$plus\MHFZ-Plus.exe" -ArgumentList $s -PassThru
    if (-not $p.WaitForExit(240000)) { "launcher did not finish"; $p.Kill() }
    "launcher exit code $($p.ExitCode) after $([int]$sw.Elapsed.TotalSeconds) s"
    Get-Content "$plus\MHFZ-Plus-$s.log"
}
"=== report"
Get-Content "$sim\report.txt"
"=== final state"
python -c @"
import filecmp, os
sim = r'$sim'
def census(game, prist, eng):
    o = e = x = 0
    for r, _, fs in os.walk(os.path.join(prist, 'dat')):
        for n in fs:
            rel = os.path.relpath(os.path.join(r, n), prist)
            g = os.path.join(game, rel)
            if filecmp.cmp(g, os.path.join(prist, rel), shallow=False): o += 1
            elif os.path.exists(os.path.join(eng, rel)) and filecmp.cmp(g, os.path.join(eng, rel), shallow=False): e += 1
            else: x += 1
    return o, e, x
print('meze game folder (original, English, other):', census(os.path.join(sim, 'meze'), os.path.join(sim, 'pristine-meze'), os.path.join(sim, 'english-meze')), 'd3d9.dll', os.path.exists(os.path.join(sim, 'meze', 'd3d9.dll')))
import glob
print('rain client leftovers:', [os.path.basename(p) for p in glob.glob(os.path.join(sim, 'rain', 'Client', 'mhfz-fps*'))], 'state:', sorted(os.listdir(os.path.join(sim, 'plus', 'fps-state', 'rain'))) if os.path.isdir(os.path.join(sim, 'plus', 'fps-state', 'rain')) else None)
print('rain client folder (original, English, other):', census(os.path.join(sim, 'rain', 'Client'), os.path.join(sim, 'pristine-rain'), os.path.join(sim, 'english-rain')), 'd3d9.dll', os.path.exists(os.path.join(sim, 'rain', 'Client', 'd3d9.dll')))
"@
