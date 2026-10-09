# How often a build hangs while starting with DXVK as the backend (the 4th Present never returning): many short
# test-app runs per build.   tools\starthang.ps1 -Dlls build\a.dll,build\b.dll [-Runs 100] [-Scene "1"] [-Ini "k=v;k=v"]
param([string]$Dlls, [int]$Runs = 100, [string]$Scene = "1", [string]$Ini = "")
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "starthang-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
if (-not (Test-Path "$w\dxvk")) { Copy-Item "$b\dxvk" "$w\dxvk" -Recurse -Force }
foreach ($dll in ($Dlls -split ',')) {
    Copy-Item $dll "$w\d3d9.dll" -Force
    $hung = 0
    $t0 = Get-Date
    for ($i = 1; $i -le $Runs; $i++) {
        Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=144`r`npresent_interval=0`r`nstats_interval=1`r`nd3d9_backend=dxvk\d3d9.dll`r`n" + ($Ini -replace ';', "`r`n") + "`r`n")
        $p = Start-Process "$w\testapp.exe" -ArgumentList ($Scene -split ' ') -WorkingDirectory $w -PassThru -RedirectStandardOutput "$w\out.txt"
        if (-not $p.WaitForExit(10000)) {
            $hung++
            $keep = Join-Path $b ("starthang-" + [IO.Path]::GetFileNameWithoutExtension($dll) + "-$i.log")
            Copy-Item "$w\mhfz-fps.log" $keep -ErrorAction SilentlyContinue
            Get-ChildItem $w -Filter "*_d3d9.log" | ForEach-Object { Copy-Item $_.FullName "$keep.dxvk.txt" }
            $p.Kill(); $null = $p.WaitForExit(5000)
        }
    }
    "{0,-22} {1} of {2} runs hung  ({3:N0} s)" -f (Split-Path $dll -Leaf), $hung, $Runs, ((Get-Date) - $t0).TotalSeconds
}
