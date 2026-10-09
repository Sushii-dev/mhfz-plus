# Builds build\MHFZ-Plus-Settings.exe (settings\Settings.cs) with the C# compiler of Windows' own .NET Framework 4 -
# nothing to install. The icon is the launcher's (build\mhfz-plus.ico, from tools\mkres.py).
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { $csc = "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe" }
if (-not (Test-Path "$root\build\mhfz-plus.ico")) { python "$root\tools\mkres.py"; if ($LASTEXITCODE) { throw "mkres failed" } }
& $csc /nologo /target:winexe /optimize+ /platform:anycpu /out:"$root\build\MHFZ-Plus-Settings.exe" /win32icon:"$root\build\mhfz-plus.ico" `
    /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Core.dll "$root\settings\Settings.cs"
if ($LASTEXITCODE -ne 0) { throw "settings build failed" }
Get-Item "$root\build\MHFZ-Plus-Settings.exe" | Select-Object Name, Length, LastWriteTime
