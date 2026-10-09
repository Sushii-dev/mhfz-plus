# Screenshot of the test app with the fps overlay on (overlay=1 in the ini; no key presses). Captures the window
# itself with PrintWindow, so other windows on the desktop are never in the picture and it works when covered.
#   tools\overlayshot.ps1 [-AppArgs "10 0 0 0 0 0 0 0 0 1"] [-Ini "overlay_pos=1;mode=0"] [-Out build\overlay_shot.png] [-At 4000]
param([string]$AppArgs = "10 0 0 0 0 0 0 0 0 1", [string]$Ini = "", [string]$Out = "", [int]$At = 4000)
$ErrorActionPreference = "Stop"
$b = Join-Path (Split-Path $PSScriptRoot) "build"
if (-not $Out) { $Out = Join-Path $b "overlay_shot.png" }
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr dc, uint flags);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
public struct RECT { public int L, T, R, B; }
'@
Set-Content "$b\mhfz-fps.ini" ("[fps]`r`nmode=2`r`ntarget_fps=170`r`npresent_interval=0`r`nstats_interval=2`r`nselfcheck=0`r`nretro=0`r`noverlay=1`r`n" + ($Ini -replace ';', "`r`n") + "`r`n")
$p = Start-Process "$b\testapp.exe" -ArgumentList ($AppArgs -split ' ') -WorkingDirectory $b -PassThru
Start-Sleep -Milliseconds $At
$p.Refresh()
$h = $p.MainWindowHandle
$r = New-Object W.U+RECT
[void][W.U]::GetWindowRect($h, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][W.U]::PrintWindow($h, $dc, 2)   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc)
$bmp.Save($Out)
$p.WaitForExit()
Get-Content "$b\mhfz-fps.log" | Select-String 'EXCEPTION|overlay' | ForEach-Object { $_.Line }
"saved $Out"
