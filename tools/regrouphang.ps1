# How often regrouptest hangs with DXVK in the deferred-draw mode (regroup=2, the pairing on the render thread), per
# build - the case that froze in n20d's validation inside FlushDeferred.
#   tools\regrouphang.ps1 -Dlls build\a.dll,build\b.dll [-Runs 60] [-Ini "pair_async=0;debug_pairhash=1"]
param([string]$Dlls, [int]$Runs = 60, [string]$Ini = "pair_async=0;debug_pairhash=1;regroup=2")
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "rghang-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\regrouptest.exe" $w -Force
if (-not (Test-Path "$w\dxvk")) { Copy-Item "$b\dxvk" "$w\dxvk" -Recurse -Force }
foreach ($dll in ($Dlls -split ',')) {
    Copy-Item $dll "$w\d3d9.dll" -Force
    $hung = 0
    $t0 = Get-Date
    for ($i = 1; $i -le $Runs; $i++) {
        Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=170`r`npresent_interval=0`r`nstats_interval=1`r`nd3d9_backend=dxvk\d3d9.dll`r`n" + ($Ini -replace ';', "`r`n") + "`r`n")
        $p = Start-Process -NoNewWindow "$w\regrouptest.exe" -ArgumentList 40, 25 -WorkingDirectory $w -PassThru -RedirectStandardOutput "$w\out.txt" -RedirectStandardError "$w\err.txt"
        if (-not $p.WaitForExit(15000)) {
            $hung++
            Copy-Item "$w\mhfz-fps.log" (Join-Path $b ("rghang-" + [IO.Path]::GetFileNameWithoutExtension($dll) + "-$i.log")) -ErrorAction SilentlyContinue
            $p.Kill(); $null = $p.WaitForExit(5000)
        }
    }
    "{0,-22} {1} of {2} runs hung  ({3:N0} s)" -f (Split-Path $dll -Leaf), $hung, $Runs, ((Get-Date) - $t0).TotalSeconds
}
