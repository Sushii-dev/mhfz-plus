# MHFZ Plus uninstaller (copied into the install folder). Puts the original game files back, removes the
# frame-rate mod, the shortcuts, the Steam entries and the install folder.
#   -Quiet   no questions (Steam entries are only removed when Steam is closed)
#   -Test    for testing: leaves registry, shortcuts and Steam alone
param([switch]$Quiet, [switch]$Test, [string[]]$SteamConfig = @())
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$title = 'Uninstall MHFZ Plus'
$dst = $PSScriptRoot
$exe = Join-Path $dst 'MHFZ-Plus.exe'
. (Join-Path $dst 'SteamShortcuts.ps1')
function Say($msg, $icon = 'Information') { if (-not $Quiet) { [void][System.Windows.Forms.MessageBox]::Show($msg, $title, 'OK', $icon) } else { $msg } }

if (-not $Quiet) {
    $r = [System.Windows.Forms.MessageBox]::Show("Remove MHFZ Plus?`n`nYour original game files are put back first.", $title, 'YesNo', 'Question')
    if ($r -ne 'Yes') { exit 0 }
}
if (-not $Test -and (Get-Process -Name 'MHFZ-Plus' -ErrorAction SilentlyContinue)) { Say 'MHFZ Plus is still running. Close the game first, then try again.' 'Warning'; exit 1 }
if (-not $Test -and (Get-Process -Name 'MHFZ-Plus-Settings' -ErrorAction SilentlyContinue)) { Say 'The MHFZ Plus settings window is open. Close it first, then try again.' 'Warning'; exit 1 }

# 1. originals back, mod out (refuses while a game is running)
$p = Start-Process $exe -ArgumentList 'all', '--restore' -Wait -PassThru
$log = Get-Content (Join-Path $dst 'MHFZ-Plus-restore.log') -ErrorAction SilentlyContinue
if ($p.ExitCode -ne 0 -or ($log -match 'could not put back')) {
    Say ("Could not put all original files back (is the game or its launcher still running?). Nothing was removed.`n`n" + ($log -join "`n")) 'Warning'
    exit 1
}
# the mod's own settings and log in the game folders (only where the mod is gone)
$ini = Join-Path $dst 'MHFZ-Plus.ini'
foreach ($line in Get-Content $ini -ErrorAction SilentlyContinue) {
    if ($line -match '^\s*game\s*=\s*(.+)$') {
        $g = $Matches[1].Trim()
        if (-not [IO.Path]::IsPathRooted($g)) { $g = Join-Path $dst $g }
        if ((Test-Path $g) -and -not (Test-Path (Join-Path $g 'd3d9.dll'))) {
            foreach ($f in 'mhfz-fps.ini', 'mhfz-fps.log') { Remove-Item (Join-Path $g $f) -Force -ErrorAction SilentlyContinue }
        }
    }
}

# 2. shortcuts, Steam, registry
if (-not $Test) {
    $names = 'MHF-Z MezeLounge (Plus)', 'MHF-Z Rain (Plus)'
    foreach ($n in $names) { Remove-Item (Join-Path ([Environment]::GetFolderPath('Desktop')) "$n.lnk") -Force -ErrorAction SilentlyContinue }
    Remove-Item (Join-Path ([Environment]::GetFolderPath('Programs')) 'MHFZ Plus') -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MHFZ-Plus' -Recurse -Force -ErrorAction SilentlyContinue
}
$cfgs = if ($SteamConfig) { $SteamConfig } elseif ($Test) { @() } else { Get-SteamUserConfigDirs }
$steamNote = ''
foreach ($c in $cfgs) {
    if (-not (Test-Path (Join-Path $c 'shortcuts.vdf'))) { continue }
    $has = (Read-VdfBinary ([IO.File]::ReadAllBytes((Join-Path $c 'shortcuts.vdf'))))['shortcuts'].Values | Where-Object { Test-OurShortcut $_ }
    if (-not $has) { continue }
    if (-not $SteamConfig -and (Get-Process -Name 'steam' -ErrorAction SilentlyContinue)) {
        $r = if ($Quiet) { 'No' } else { [System.Windows.Forms.MessageBox]::Show("Close Steam for a moment to remove the MHFZ Plus games from it?", $title, 'YesNo', 'Question') }
        if ($r -eq 'Yes') {
            Start-Process (Join-Path (Get-SteamPath) 'steam.exe') -ArgumentList '-shutdown'
            for ($i = 0; $i -lt 60 -and (Get-Process -Name 'steam' -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
            Start-Sleep 2
        }
        if (Get-Process -Name 'steam' -ErrorAction SilentlyContinue) { $steamNote = "`n`nThe Steam entries are still there: right-click them in Steam > Manage > Remove non-Steam game."; continue }
        Remove-SteamShortcuts $c
        Start-Process (Join-Path (Get-SteamPath) 'steam.exe')
    } else { Remove-SteamShortcuts $c }
}

# 3. the install folder (from a copy of this script's process: the folder can't be deleted while we run from it)
Set-Location $env:TEMP
$cmd = "Start-Sleep 2; Remove-Item -LiteralPath '$($dst.Replace("'", "''"))' -Recurse -Force -ErrorAction SilentlyContinue"
if ($Test) { Remove-Item -LiteralPath $dst -Recurse -Force }
else { Start-Process "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList '-NoProfile', '-WindowStyle', 'Hidden', '-Command', $cmd -WindowStyle Hidden }
Say "MHFZ Plus has been removed. Your game folders are back to the original files.$steamNote"
