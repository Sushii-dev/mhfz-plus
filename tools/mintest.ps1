# Minimized window test: runs the test app, minimizes its window from 3 s to 7 s, and prints the stats lines.
# Expected: "shown" drops to the real ~29 fps and "minimized: N ticks" appears while minimized; full rate otherwise.
$ErrorActionPreference = "Stop"
$b = Join-Path (Split-Path $PSScriptRoot) "build"
Add-Type -Namespace W -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);'
Set-Content "$b\mhfz-fps.ini" "[fps]`r`nmode=2`r`ntarget_fps=120`r`npresent_interval=0`r`nstats_interval=2`r`nselfcheck=0`r`nretro=0`r`n"
$p = Start-Process -NoNewWindow "$b\testapp.exe" -ArgumentList '11' -WorkingDirectory $b -PassThru
Start-Sleep -Milliseconds 3000
$p.Refresh()
[void][W.U]::ShowWindow($p.MainWindowHandle, 6)   # SW_MINIMIZE
Start-Sleep -Milliseconds 4000
[void][W.U]::ShowWindow($p.MainWindowHandle, 9)   # SW_RESTORE
$p.WaitForExit()
Get-Content "$b\mhfz-fps.log" | Select-String 'stats \d|minimized' | ForEach-Object {
    if ($_.Line -match '\[\s*([\d.]+)\] stats.*real ([\d.]+) fps, shown ([\d.]+) fps') { "{0,6} s  real {1} fps, shown {2} fps" -f $Matches[1], $Matches[2], $Matches[3] }
    else { "         " + ($_.Line -replace '^\[\s*[\d.]+\]\s*', '') }
}
