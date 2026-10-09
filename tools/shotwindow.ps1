# Starts a program, waits, captures its main window into a PNG (PrintWindow) and closes it - to look at a UI from a script.
#   tools\shotwindow.ps1 -Exe build\x.exe -Out shot.png [-Args "..."] [-WaitMs 2500] [-Keys "{TAB}"]
param([string]$Exe, [string]$Out, [string]$Args = "", [int]$WaitMs = 2500, [string]$Keys = "")
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public struct RECT { public int L, T, R, B; }
}
"@
[W32]::SetProcessDPIAware() | Out-Null   # (real pixels: the window may be dpi-aware)
$p = if ($Args) { Start-Process $Exe -ArgumentList $Args -PassThru } else { Start-Process $Exe -PassThru }
Start-Sleep -Milliseconds $WaitMs
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { "no window"; $p | Stop-Process -Force; return }
if ($Keys) { [W32]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 300; [System.Windows.Forms.SendKeys]::SendWait($Keys); Start-Sleep -Milliseconds 700 }
$r = New-Object W32+RECT
[W32]::GetWindowRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[W32]::PrintWindow($h, $dc, 2) | Out-Null
$g.ReleaseHdc($dc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
$p | Stop-Process -Force -Confirm:$false
"saved $Out ($($r.R - $r.L)x$($r.B - $r.T))"
