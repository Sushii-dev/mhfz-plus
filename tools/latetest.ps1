# How often the in-between frames end past the game's deadline (the stats line's "late"), per build, on a scene:
#   tools\latetest.ps1 -Dlls build\a.dll,build\b.dll [-Scene "12 0 0 0 0 1"] [-Ini "d3d9_backend=dxvk\d3d9.dll"]
param([string]$Dlls, [string]$Scene = "12 0 0 0 0 1", [string]$Ini = "")
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$w = Join-Path $b "late-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
if (-not (Test-Path "$w\dxvk")) { Copy-Item "$b\dxvk" "$w\dxvk" -Recurse -Force }
foreach ($dll in ($Dlls -split ',')) {
    Copy-Item $dll "$w\d3d9.dll" -Force
    Set-Content "$w\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=0`r`npresent_interval=0`r`nstats_interval=4`r`n" + ($Ini -replace ';', "`r`n") + "`r`n")
    Start-Process "$w\testapp.exe" -ArgumentList ($Scene -split ' ') -WorkingDirectory $w -Wait -RedirectStandardOutput "$w\out.txt" | Out-Null
    "== $(Split-Path $dll -Leaf)  $(Get-Content "$w\out.txt" | Where-Object { $_ -match 'ticks:' })"
    Get-Content "$w\mhfz-fps.log" | Where-Object { $_ -match '\] stats ' } | ForEach-Object {
        if ($_ -match 'real ([\d.]+) fps, shown ([\d.]+) fps .*late (\d+)( \(([\d.]+) ms avg, GPU waits cut (\d+)\))? \| .*present ([\d.]+) ms') {
            "   real {0} shown {1} late {2} avg {3} cut {4} present {5}" -f $Matches[1], $Matches[2], $Matches[3], $Matches[5], $Matches[6], $Matches[7]
        }
    }
}
