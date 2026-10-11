# Builds build\MHFZ-Plus-App.exe, the MHFZ Plus window (settings\Hub.cs, and a file per page: Hub.Play.cs,
# Hub.Settings.cs, Hub.Help.cs; Hub.Shots.cs for tests) with the settings model and command line
# (settings\Settings.cs) and its animations (settings\Anim.cs), with the C# compiler of Windows' own .NET Framework 4 -
# nothing to install. The icon is the launcher's (build\mhfz-plus.ico, from tools\mkres.py): the exe's icon, and the
# same .ico as a resource for the window (every size, for the taskbar and Alt+Tab).
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { $csc = "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe" }
if (-not (Test-Path "$root\build\mhfz-plus.ico") -or (Get-Item "$root\tools\mkres.py").LastWriteTime -gt (Get-Item "$root\build\mhfz-plus.ico").LastWriteTime) {
    python "$root\tools\mkres.py"; if ($LASTEXITCODE) { throw "mkres failed" }
}
& $csc /nologo /target:winexe /optimize+ /platform:anycpu /out:"$root\build\MHFZ-Plus-App.exe" /win32icon:"$root\build\mhfz-plus.ico" `
    "/resource:$root\build\mhfz-plus.ico,mhfz-plus.ico" /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Core.dll "$root\settings\Settings.cs" "$root\settings\Hub*.cs" "$root\settings\Anim.cs"
if ($LASTEXITCODE -ne 0) { throw "app build failed" }
Get-Item "$root\build\MHFZ-Plus-App.exe" | Select-Object Name, Length, LastWriteTime
