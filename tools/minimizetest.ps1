# Does interpolation come back after the window was minimized (DXVK reports "device lost" for a while)? Runs the
# test app, minimizes its window after 3 s, restores it 2 s later, and prints the per-second stats.
#   tools\minimizetest.ps1 -Dll build\d3d9.dll [-Backend dxvk\d3d9.dll | ""]
param([string]$Dll = "", [string]$Backend = "dxvk\d3d9.dll")
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class MinW { [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd); }
"@
$b = Join-Path (Split-Path $PSScriptRoot) "build"
if (-not $Dll) { $Dll = "$b\d3d9.dll" }
$w = Join-Path $b "minimize-run"
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item "$b\testapp.exe" $w -Force
Copy-Item $Dll "$w\d3d9.dll" -Force
if (-not (Test-Path "$w\dxvk")) { Copy-Item "$b\dxvk" "$w\dxvk" -Recurse -Force }
$be = if ($Backend) { "d3d9_backend=$Backend`r`n" } else { "" }
Set-Content "$w\mhfz-fps.ini" "[fps]`r`nmode=2`r`ntarget_fps=144`r`npresent_interval=0`r`nstats_interval=1`r`n$be"
$p = Start-Process -NoNewWindow "$w\testapp.exe" -ArgumentList 10 -WorkingDirectory $w -PassThru -RedirectStandardOutput "$w\out.txt"
Start-Sleep -Seconds 3
$p.Refresh(); $h = $p.MainWindowHandle
"minimize: " + [MinW]::ShowWindow($h, 6)
Start-Sleep -Seconds 2
"restore: " + [MinW]::ShowWindow($h, 4)   # (SW_SHOWNOACTIVATE: no focus taken)
$null = $p.WaitForExit(30000)
Get-Content "$w\mhfz-fps.log" | Where-Object { $_ -match '\] stats 1s|device|lost|Reset' } | ForEach-Object {
    if ($_ -match '^\[\s*([\d.]+)\] stats 1s: real ([\d.]+) fps, shown ([\d.]+) fps') { "  {0,6}s  real {1,5}  shown {2,6}" -f $Matches[1], $Matches[2], $Matches[3] } else { "  " + $_ }
}
