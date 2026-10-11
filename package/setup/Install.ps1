# MHFZ Plus setup. Installs the launcher (English patch + high frame rate) for MezeLounge and/or Rain, with
# optional desktop, Start menu and Steam shortcuts. Started by Install.cmd.
#   -Unattended            no window: installs for every game found (add -Steam for the Steam shortcuts)
#   -Test                  for testing: no registry entry, no shortcuts, Steam only with -SteamConfig folders
#   -LinkDir <folder>      with -Test: the shortcuts are made there (Programs\, Desktop\) instead of left out
#   -Update [-Relaunch]    the in-app update: the install in -Target (or the usual folder) gets these program files and
#                          keeps every choice (games, English, frame rate, graphics backend, shortcuts, Steam entries);
#                          -Relaunch opens MHFZ Plus again after, also when the update failed (a message says why)
param([switch]$Unattended, [switch]$Test, [string]$MezeDir = '', [string]$RainDir = '', [string]$Target = '',
      [string]$KitDir = '',   # an earlier English kit (MHFZ-Meze-translation) whose backups can be reused
      [string[]]$SteamConfig = @(), [switch]$Steam, [switch]$NoEnglish, [switch]$NoFps, [switch]$NoDxvk,
      [string]$Screenshot = '',                            # testing: draw the window into this PNG and exit
      [string]$LinkDir = '', [switch]$Update, [switch]$Relaunch)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$pkg = Split-Path $PSScriptRoot          # package root (holds payload\ and setup\)
. (Join-Path $PSScriptRoot 'SteamShortcuts.ps1')
$title = 'MHFZ Plus setup'
$version = Get-Content (Join-Path $pkg 'payload\version.txt') -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $version) { $version = '1.x' }
function Show-Error($msg) { [void][System.Windows.Forms.MessageBox]::Show($msg, $title, 'OK', 'Error') }

if (-not (Test-Path (Join-Path $pkg 'payload\MHFZ-Plus.exe'))) {
    $msg = "The setup files are incomplete.`n`nExtract the whole zip file to a folder first (right-click > Extract All), then run Install.cmd from there."
    if ($Unattended) { throw $msg } else { Show-Error $msg; exit 1 }
}

# ------------------------------------------------------------------------------------------------ detection
# (folders that may not exist, on drives that may not exist: Join-Path throws for a drive letter the PC doesn't have -
# no D: drive stopped setup before its window opened - so plain strings, and never an error from looking)
function Test-Sub($d, $rel) { try { Test-Path -LiteralPath ([IO.Path]::Combine($d, $rel)) } catch { $false } }
function Test-MezeDir($d) { $d -and (Test-Sub $d 'mhf.exe') -and (Test-Sub $d 'dat') }
function Test-RainDir($d) { $d -and (Test-Sub $d 'Client\dat') -and (Test-Sub $d 'Launcher') }
function Find-MezeDir {
    $cands = @('C:\Games\MezeLounge', 'C:\MezeLounge', 'C:\Games\MezeLounge', 'D:\MezeLounge', 'D:\Games\MezeLounge',
               "$env:ProgramFiles\MezeLounge", "${env:ProgramFiles(x86)}\MezeLounge", "$env:USERPROFILE\MezeLounge",
               "$env:USERPROFILE\Desktop\MezeLounge", "$env:USERPROFILE\Downloads\MezeLounge")
    foreach ($c in $cands) { if (Test-MezeDir $c) { return $c } }
    # a shortcut to mhf.exe on the desktop or in the Start menu
    $shell = New-Object -ComObject WScript.Shell
    foreach ($dir in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('CommonDesktopDirectory'),
                       [Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('CommonPrograms'))) {
        foreach ($lnk in @(Get-ChildItem $dir -Filter *.lnk -Recurse -ErrorAction SilentlyContinue)) {
            try { $t = $shell.CreateShortcut($lnk.FullName).TargetPath } catch { continue }
            if ($t -like '*\mhf.exe' -and (Test-MezeDir (Split-Path $t))) { return (Split-Path $t) }
        }
    }
    return ''
}
function Find-RainDir {
    # Rain's launcher installer registers "Rain Frontier"; its uninstaller sits in <Rain>\Launcher
    $keys = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*', 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    foreach ($e in @(Get-ItemProperty $keys -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -match 'Rain' })) {
        foreach ($p in @([string]$e.UninstallString, [string]$e.InstallLocation, [string]$e.DisplayIcon)) {
            if (-not $p) { continue }
            if ($p -match '"([^"]+)"') { $p = $Matches[1] }
            $p = $p -replace ',\d+$', ''
            foreach ($root in @((Split-Path $p), (Split-Path (Split-Path $p)))) {
                if ($root -and (Test-RainDir $root)) { return $root }
            }
        }
    }
    foreach ($c in @('C:\Games\Rain', 'C:\Rain', 'C:\Games\Rain', 'C:\RainFrontier', 'D:\Rain', 'D:\Games\Rain')) {
        if (Test-RainDir $c) { return $c }
    }
    return ''
}

# ------------------------------------------------------------------------------------------------ ini
# Keeps keys the player added by hand (start_args, ...) when setup runs again.
function Read-Ini($path) {
    $ini = [ordered]@{}
    $sec = ''
    if (Test-Path $path) {
        foreach ($line in Get-Content $path) {
            if ($line -match '^\s*\[(.+)\]\s*$') { $sec = $Matches[1].ToLower(); if (-not $ini.Contains($sec)) { $ini[$sec] = [ordered]@{} } }
            elseif ($sec -and $line -match '^\s*([^;=]+?)\s*=\s*(.*)$') { $ini[$sec][$Matches[1].ToLower()] = $Matches[2] }
        }
    }
    return $ini
}
function Write-Ini($path, $ini) {
    $out = @('; MHFZ Plus settings (written by Install.cmd; your own extra keys are kept when it runs again)', '')
    foreach ($s in $ini.Keys) {
        $out += "[$s]"
        foreach ($k in $ini[$s].Keys) { $out += "$k=$($ini[$s][$k])" }
        $out += ''
    }
    Set-Content $path $out -Encoding Unicode   # UTF-16 with BOM: read by GetPrivateProfileStringW (any folder name works)
}

# ------------------------------------------------------------------------------------------------ install
# d3d9_backend in a mhfz-fps.ini: only an empty value or an MHFZ Plus DXVK path is replaced; comments and the rest stay.
function Set-Backend($path, $value) {
    $lines = [Collections.Generic.List[string]]([IO.File]::ReadAllLines($path))
    $i = -1
    for ($k = 0; $k -lt $lines.Count; $k++) { if ($lines[$k] -match '^\s*d3d9_backend\s*=') { $i = $k; break } }
    if ($i -ge 0) {
        $cur = ($lines[$i] -split '=', 2)[1].Trim()
        if ($cur -and $cur -notmatch 'MHFZ-Plus\\fps\\dxvk\\d3d9\.dll$' -and $cur -notmatch '\\fps\\dxvk\\d3d9\.dll$') { return }   # (chosen by hand)
        $lines[$i] = "d3d9_backend=$value"
    } elseif ($value) {
        $s = -1; for ($k = 0; $k -lt $lines.Count; $k++) { if ($lines[$k].Trim() -eq '[fps]') { $s = $k; break } }
        if ($s -lt 0) { return }
        $e = $s + 1; while ($e -lt $lines.Count -and -not $lines[$e].TrimStart().StartsWith('[')) { $e++ }
        while ($e -gt $s + 1 -and -not $lines[$e - 1].Trim()) { $e-- }
        $lines.Insert($e, "d3d9_backend=$value")
    } else { return }
    [IO.File]::WriteAllText($path, ($lines -join "`r`n") + "`r`n", (New-Object Text.UTF8Encoding $false))
}
$names = @{ meze = 'MHF-Z MezeLounge (Plus)'; rain = 'MHF-Z Rain (Plus)' }
function Install-Plus($o) {
    $dst = $o.Target
    # (a real install only: -Test installs into its own folder for the simulated games, whatever is running)
    if (-not $Test -and (Get-Process -Name 'MHFZ-Plus' -ErrorAction SilentlyContinue)) { throw 'MHFZ Plus is running. Close the game first.' }
    if (-not $Test -and (Get-Process -Name 'MHFZ-Plus-App', 'MHFZ-Plus-Settings' -ErrorAction SilentlyContinue)) { throw 'The MHFZ Plus window is open. Close it first.' }
    New-Item -ItemType Directory -Force $dst | Out-Null
    # (an update: the default mod settings come new, with the graphics backend they had - new games start from them)
    $tpl = Join-Path $dst 'fps\mhfz-fps.ini'
    $keptBackend = if ($o.Keep -and (Test-Path $tpl)) { @([IO.File]::ReadAllLines($tpl) | Where-Object { $_ -match '^\s*d3d9_backend\s*=' } | ForEach-Object { ($_ -split '=', 2)[1].Trim() })[0] } else { $null }
    Copy-Item (Join-Path $pkg 'payload\*') $dst -Recurse -Force
    if ($keptBackend) { Set-Backend $tpl $keptBackend }
    Remove-Item (Join-Path $dst 'MHFZ-Plus-Settings.exe') -Force -ErrorAction SilentlyContinue   # (1.3 and older: now the MHFZ Plus app)
    Copy-Item (Join-Path $PSScriptRoot 'SteamShortcuts.ps1'), (Join-Path $PSScriptRoot 'Uninstall.ps1'), (Join-Path $PSScriptRoot 'Report.ps1') $dst -Force
    Get-ChildItem $dst -Recurse -File | Unblock-File   # downloaded files: no SmartScreen prompt on every start
    $exe = Join-Path $dst 'MHFZ-Plus.exe'

    $servers = @()
    if ($o.Meze) { $servers += 'meze' }
    if ($o.Rain) { $servers += 'rain' }
    $iniPath = Join-Path $dst 'MHFZ-Plus.ini'
    $ini = Read-Ini $iniPath
    if (-not $ini.Contains('general')) { $ini['general'] = [ordered]@{} }
    # ($o.Keep, the in-app update: the player's choices stay as they are - a public download has no English data, and
    # writing its "English off" would switch off a translation someone has)
    if ($servers -and -not ($o.Keep -and $ini['general'].Contains('server'))) { $ini['general']['server'] = $servers[0] }
    foreach ($s in $servers) {
        if (-not $ini.Contains($s)) { $ini[$s] = [ordered]@{} }
        $ini[$s]['game'] = if ($s -eq 'meze') { $o.MezeDir } else { Join-Path $o.RainDir 'Client' }
        if (-not ($o.Keep -and $ini[$s].Contains('english'))) { $ini[$s]['english'] = [int][bool]$o.English }
        if (-not ($o.Keep -and $ini[$s].Contains('fps'))) { $ini[$s]['fps'] = [int][bool]$o.Fps }
        if (-not $ini[$s].Contains('fps_mode')) { $ini[$s]['fps_mode'] = if ($s -eq 'meze') { 'stay' } else { 'swap' } }
    }
    Write-Ini $iniPath $ini

    # graphics backend: the DXVK that comes with MHFZ Plus (much less CPU time per frame), or Windows' own. Set in the
    # default settings (new games start from it) and in each game's own settings; a backend someone chose by hand stays.
    $dxvk = Join-Path $dst 'fps\dxvk\d3d9.dll'
    if ($o.Fps -and -not $o.Keep -and (Test-Path $dxvk)) {   # (an update keeps the backend the player has)
        $files = @((Join-Path $dst 'fps\mhfz-fps.ini'), (Join-Path $dst 'fps-state\rain\mhfz-fps.ini'))
        if ($o.Meze) { $files += Join-Path $o.MezeDir 'mhfz-fps.ini' }
        foreach ($f in $files) { if (Test-Path $f) { Set-Backend $f $(if ($o.Dxvk) { $dxvk } else { '' }) } }
    }

    # originals saved by the earlier English kit (MHFZ-Meze-translation): reused when they match the patch
    $kitDirs = @{ meze = 'backup'; rain = 'backup-rain' }
    foreach ($s in $servers) {
        $kb = if ($KitDir) { Join-Path $KitDir $kitDirs[$s] } else { '' }
        $man = Join-Path $dst "english\$s\manifest.txt"
        if (-not $kb -or -not (Test-Path $kb) -or -not (Test-Path $man)) { continue }
        foreach ($line in Get-Content $man) {
            if (-not $line -or $line.StartsWith('#')) { continue }
            $f = $line -split '\|'
            $src = Join-Path $kb $f[0]
            $bak = Join-Path $dst "backup\$s\$($f[0])"
            if ((Test-Path $src) -and -not (Test-Path $bak) -and (Get-FileHash $src -Algorithm SHA256).Hash.ToLower() -eq $f[2]) {
                New-Item -ItemType Directory -Force (Split-Path $bak) | Out-Null
                Copy-Item $src $bak
            }
        }
    }

    # build the English files from the player's own originals now (this also checks the game version)
    $report = @()
    foreach ($s in $servers) {
        $on = if ($o.Keep) { "$($ini[$s]['english'])" -ne '0' -and (Test-Path (Join-Path $dst "english\$s\manifest.txt")) } else { $o.English }
        if (-not $on) { $report += "${s}: English off"; continue }
        $p = Start-Process $exe -ArgumentList $s, '--prepare' -Wait -PassThru
        $log = Get-Content (Join-Path $dst "MHFZ-Plus-$s.log") -ErrorAction SilentlyContinue
        $ready = $log | Select-String 'english: (\d+) files ready.*, (\d+) left out' | Select-Object -Last 1
        if ($ready) {
            $n = $ready.Matches[0].Groups[1].Value; $left = $ready.Matches[0].Groups[2].Value
            $report += if ($left -eq '0') { "${s}: English ready ($n files)" } else { "${s}: English ready ($n files; $left don't match your game version and are left out)" }
        } else { $report += "${s}: the English patch could not be prepared (see MHFZ-Plus-$s.log)" }
    }

    if (-not $Test -or $LinkDir) {
        # one shortcut: the MHFZ Plus app (play, settings, help) in the Start menu and on the desktop, and an
        # "Apps & features" entry. The Start menu folder and per-server shortcuts of 1.3 and older go.
        # (tests: the shortcuts in -LinkDir, so this part runs in them too; no registry, no icon refresh)
        $shell = New-Object -ComObject WScript.Shell
        $app = Join-Path $dst 'MHFZ-Plus-App.exe'
        if ($LinkDir) {
            $programs = Join-Path $LinkDir 'Programs'; $desktop = Join-Path $LinkDir 'Desktop'
            New-Item -ItemType Directory -Force $programs, $desktop | Out-Null
        } else { $programs = [Environment]::GetFolderPath('Programs'); $desktop = [Environment]::GetFolderPath('Desktop') }
        Remove-Item (Join-Path $programs 'MHFZ Plus') -Recurse -Force -ErrorAction SilentlyContinue
        foreach ($n in $names.Values) { Remove-Item (Join-Path $desktop "$n.lnk") -Force -ErrorAction SilentlyContinue }
        function New-Link($path, $target, $arguments, $desc, $icon = "$app,0") {
            $l = $shell.CreateShortcut($path)
            $l.TargetPath = $target; $l.Arguments = $arguments; $l.WorkingDirectory = $dst; $l.IconLocation = $icon; $l.Description = $desc
            $l.Save()
        }
        $what = 'Play Monster Hunter Frontier Z with MHFZ Plus: settings, help and bug reports'
        New-Link (Join-Path $programs 'MHFZ Plus.lnk') $app '' $what
        if ($o.Desktop) { New-Link (Join-Path $desktop 'MHFZ Plus.lnk') $app '' $what }
        else { Remove-Item (Join-Path $desktop 'MHFZ Plus.lnk') -Force -ErrorAction SilentlyContinue }
    }
    if (-not $Test) {
        # Windows shows an updated exe with its old icon from its icon cache: tell the shell the icons changed
        try {
            Add-Type -Namespace MhfzPlus -Name Shell -MemberDefinition '[DllImport("shell32.dll", CharSet = CharSet.Unicode)] public static extern void SHChangeNotify(int e, uint f, string a, IntPtr b);'
            foreach ($lnk in @((Join-Path $programs 'MHFZ Plus.lnk'), (Join-Path $desktop 'MHFZ Plus.lnk'))) {
                if (Test-Path $lnk) { [MhfzPlus.Shell]::SHChangeNotify(0x2000, 0x5, $lnk, [IntPtr]::Zero) }   # SHCNE_UPDATEITEM, SHCNF_PATHW
            }
            [MhfzPlus.Shell]::SHChangeNotify(0x08000000, 0, $null, [IntPtr]::Zero)   # SHCNE_ASSOCCHANGED: the icon cache is reread
            $ie4 = Join-Path $env:SystemRoot 'System32\ie4uinit.exe'
            if (Test-Path $ie4) { Start-Process $ie4 -ArgumentList '-show' -WindowStyle Hidden }   # (Windows 10/11's own icon refresh)
        } catch { }
        $ps = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
        $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MHFZ-Plus'
        New-Item $key -Force | Out-Null
        $v = @{ DisplayName = 'MHFZ Plus (Monster Hunter Frontier Z: English + high frame rate)'; DisplayVersion = $version; Publisher = 'MHFZ Plus';
                InstallLocation = $dst; DisplayIcon = "$exe,0"; UninstallString = "`"$ps`" -NoProfile -ExecutionPolicy Bypass -File `"$dst\Uninstall.ps1`"" }
        foreach ($k in $v.Keys) { Set-ItemProperty $key $k $v[$k] }
        Set-ItemProperty $key NoModify 1 -Type DWord
        Set-ItemProperty $key NoRepair 1 -Type DWord
    }

    if ($o.Steam) { $report += Add-ToSteam $dst $exe $servers }
    Set-Content (Join-Path $dst 'install-report.txt') $report
    return $report
}

function Add-ToSteam($dst, $exe, $servers) {
    $cfgs = if ($SteamConfig) { $SteamConfig } elseif ($Test) { @() } else { Get-SteamUserConfigDirs }
    if (-not $cfgs) { return 'Steam: no Steam user found - not added' }
    $restart = $false
    if (-not $SteamConfig -and (Get-Process -Name 'steam' -ErrorAction SilentlyContinue)) {
        $r = if ($Unattended) { 'Yes' } else {
            [System.Windows.Forms.MessageBox]::Show("Steam has to close for a moment to add the games. Close Steam now?`n`n(It starts again afterwards.)", $title, 'YesNo', 'Question') }
        if ($r -ne 'Yes') { return 'Steam: not added (Steam was open) - run Install.cmd again to add them' }
        Start-Process (Join-Path (Get-SteamPath) 'steam.exe') -ArgumentList '-shutdown'
        for ($i = 0; $i -lt 60 -and (Get-Process -Name 'steam' -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
        if (Get-Process -Name 'steam' -ErrorAction SilentlyContinue) { return 'Steam: did not close - not added (run Install.cmd again with Steam closed)' }
        Start-Sleep 2
        $restart = $true
    }
    # the MHFZ Plus app, and a Play entry per server that starts the game in one press (Steam Deck, Big Picture)
    $app = Join-Path $dst 'MHFZ-Plus-App.exe'
    $entries = @([pscustomobject]@{ Name = 'MHFZ Plus'; Exe = $app; StartDir = $dst; LaunchOptions = ''; Icon = $app }) +
               @($servers | ForEach-Object { [pscustomobject]@{ Name = $names[$_]; Exe = $exe; StartDir = $dst; LaunchOptions = $_; Icon = $exe } })
    $problem = $null
    foreach ($c in $cfgs) {
        try { Set-SteamShortcuts $c $entries; Copy-SteamArtwork $c $entries }
        catch { $problem = $_.Exception.Message }   # (Steam's file left as it was: everything else is installed)
    }
    if ($restart) { Start-Process (Join-Path (Get-SteamPath) 'steam.exe') }
    if ($problem) { return "Steam: not added - $problem Add MHFZ-Plus-App.exe in Steam yourself (Games > Add a Non-Steam Game)." }
    return "Steam: added $(($entries | ForEach-Object Name) -join ' and ')"
}

# Artwork the player already set for another Monster Hunter Frontier shortcut is reused for ours.
function Copy-SteamArtwork($cfg, $entries) {
    $grid = Join-Path $cfg 'grid'
    if (-not (Test-Path $grid)) { return }
    $root = Read-VdfBinary ([IO.File]::ReadAllBytes((Join-Path $cfg 'shortcuts.vdf')))
    $src = $null
    $ours = @{}
    foreach ($e in $root['shortcuts'].Values) {
        $id = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$e['appid']), 0)
        $exe = [string]$e['Exe']
        if (Test-OurShortcut $e) { $ours[$id] = 1 }
        elseif (-not $src -and ($exe -match 'rain-server-launcher|mhf\.exe|mhfo' -or [string]$e['AppName'] -match 'Frontier') -and
                (Get-ChildItem $grid -Filter "$id*" -ErrorAction SilentlyContinue)) { $src = $id }
    }
    if (-not $src) { return }
    foreach ($id in $ours.Keys) {
        if (Get-ChildItem $grid -Filter "$id*" -ErrorAction SilentlyContinue) { continue }   # has its own artwork
        foreach ($f in Get-ChildItem $grid -Filter "$src*") {
            Copy-Item $f.FullName (Join-Path $grid ("$id" + $f.Name.Substring("$src".Length)))
        }
    }
}

# ------------------------------------------------------------------------------------------------ window
$defaultTarget = if ($Target) { $Target } else { Join-Path $env:LOCALAPPDATA 'MHFZ-Plus' }
# (the public GitHub release has no English data: the option is shown as not included)
$hasEnglish = Test-Path (Join-Path $pkg 'payload\english\meze\manifest.txt')
$prevIni = Read-Ini (Join-Path $defaultTarget 'MHFZ-Plus.ini')
$mezeFound = if ($MezeDir) { $MezeDir } elseif ($prevIni.Contains('meze')) { $prevIni['meze']['game'] } else { try { Find-MezeDir } catch { '' } }
$rainFound = if ($RainDir) { $RainDir } elseif ($prevIni.Contains('rain')) { Split-Path $prevIni['rain']['game'] } else { try { Find-RainDir } catch { '' } }
$prevVersion = if (Test-Path (Join-Path $defaultTarget 'MHFZ-Plus.exe')) { $v = Get-Content (Join-Path $defaultTarget 'version.txt') -ErrorAction SilentlyContinue | Select-Object -First 1; if ($v) { $v } else { 'earlier' } } else { '' }

if ($Update) {
    # the in-app update: this install as it is - its games and every choice, its shortcuts and Steam entries - gets the
    # new program files. MHFZ Plus closed itself to let it; it opens again after, also when this failed.
    $fail = $null
    try {
        if (-not ($prevIni.Contains('meze') -or $prevIni.Contains('rain'))) { throw "There is no MHFZ Plus install in $defaultTarget to update." }
        if (-not $Test) { for ($i = 0; $i -lt 80 -and (Get-Process -Name 'MHFZ-Plus-App' -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 250 } }
        $deskDir = if ($LinkDir) { Join-Path $LinkDir 'Desktop' } else { [Environment]::GetFolderPath('Desktop') }
        $r = Install-Plus ([pscustomobject]@{ Target = $defaultTarget; Keep = $true;
                Meze = $prevIni.Contains('meze') -and [bool](Test-MezeDir $mezeFound); MezeDir = $mezeFound;
                Rain = $prevIni.Contains('rain') -and [bool](Test-RainDir $rainFound); RainDir = $rainFound;
                English = $true; Fps = $true; Desktop = (Test-Path (Join-Path $deskDir 'MHFZ Plus.lnk')); Steam = $false; Dxvk = $true })
        $r
        "updated to $version"
    } catch { $fail = $_.Exception.Message; "update failed: $fail" }
    if ($fail -and -not $Test) {
        [void][System.Windows.Forms.MessageBox]::Show("MHFZ Plus couldn't update to ${version}: $fail`n`nTo update by hand: run Install.cmd from`n$pkg", $title, 'OK', 'Warning')
    }
    if ($Relaunch -and -not $Test) {
        $app = Join-Path $defaultTarget 'MHFZ-Plus-App.exe'
        if (Test-Path $app) { Start-Process $app -WorkingDirectory $defaultTarget }
    }
    if ($fail) { exit 1 }
    return
}

if ($Unattended) {
    $r = Install-Plus ([pscustomobject]@{ Target = $defaultTarget; Meze = [bool](Test-MezeDir $mezeFound); MezeDir = $mezeFound;
                                          Rain = [bool](Test-RainDir $rainFound); RainDir = $rainFound; English = $hasEnglish -and -not $NoEnglish;
                                          Fps = -not $NoFps; Desktop = -not $Test; Steam = [bool]$Steam; Dxvk = -not $NoDxvk })
    $r
    return
}

$form = New-Object System.Windows.Forms.Form
$form.Text = "$title $version"
try { $form.Icon = [System.Drawing.Icon]::ExtractAssociatedIcon((Join-Path $pkg 'payload\MHFZ-Plus-App.exe')) } catch { }   # (MHFZ Plus's icon, not PowerShell's)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.Font = New-Object System.Drawing.Font('Segoe UI', 9)
$form.AutoScaleMode = 'Font'
$y = 12
function Add-Label($text, $x, $y, $w = 600, $h = 20, $bold = $false) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $text; $l.Location = New-Object System.Drawing.Point($x, $y); $l.Size = New-Object System.Drawing.Size($w, $h)
    if ($bold) { $l.Font = New-Object System.Drawing.Font('Segoe UI', 9, [System.Drawing.FontStyle]::Bold) }
    $form.Controls.Add($l); $l
}
function Add-Check($text, $x, $y, $checked, $w = 580) {
    $c = New-Object System.Windows.Forms.CheckBox
    $c.Text = $text; $c.Location = New-Object System.Drawing.Point($x, $y); $c.Size = New-Object System.Drawing.Size($w, 22); $c.Checked = $checked
    $form.Controls.Add($c); $c
}
function Add-PathRow($y, $value) {
    $t = New-Object System.Windows.Forms.TextBox
    $t.Location = New-Object System.Drawing.Point(36, $y); $t.Size = New-Object System.Drawing.Size(470, 22); $t.Text = $value
    $b = New-Object System.Windows.Forms.Button
    $b.Text = 'Browse...'; $b.Location = New-Object System.Drawing.Point(514, ($y - 1)); $b.Size = New-Object System.Drawing.Size(90, 25)
    $b.Add_Click({
        $d = New-Object System.Windows.Forms.FolderBrowserDialog
        if ($t.Text -and (Test-Path $t.Text)) { $d.SelectedPath = $t.Text }
        if ($d.ShowDialog() -eq 'OK') { $t.Text = $d.SelectedPath }
    }.GetNewClosure())
    $form.Controls.Add($t); $form.Controls.Add($b); $t
}
[void](Add-Label 'Monster Hunter Frontier Z: English patch and high frame rate, for MezeLounge and Rain.' 12 $y 600 20 $true); $y += 24
if ($prevVersion) {
    $what = if ($prevVersion -eq 'earlier') { 'An earlier MHFZ Plus is installed' } else { "MHFZ Plus $prevVersion is installed" }
    [void](Add-Label "${what}: this updates it to $version. Your settings and clips are kept." 12 $y 600 20); $y += 24
}
$y += 4
$cMeze = Add-Check 'MezeLounge  (the folder with mhf.exe)' 12 $y ([bool](Test-MezeDir $mezeFound)); $y += 24
$tMeze = Add-PathRow $y $mezeFound; $y += 34
$cRain = Add-Check 'Rain  (the folder with Launcher and Client)' 12 $y ([bool](Test-RainDir $rainFound)); $y += 24
$tRain = Add-PathRow $y $rainFound; $y += 40
[void](Add-Label 'Use' 12 $y 600 20 $true); $y += 24
$cFps = Add-Check 'High frame rate (up to your monitor''s refresh rate; game speed stays the same)' 24 $y $true; $y += 24
$cEng = Add-Check ('English translation' + $(if (-not $hasEnglish) { ' - not included in this download' } else { '' })) 24 $y $hasEnglish; $y += 24
$cEng.Enabled = $hasEnglish
$cDesk = Add-Check 'Desktop shortcut' 24 $y $true; $y += 24
$hasDxvk = Test-Path (Join-Path $pkg 'payload\fps\dxvk\d3d9.dll')
$cDxvk = Add-Check 'Use DXVK: faster (needs a Vulkan graphics card; otherwise Windows Direct3D is used)' 24 $y $hasDxvk; $y += 24
$cDxvk.Enabled = $hasDxvk
$steamOk = [bool](Get-SteamUserConfigDirs)
$cSteam = Add-Check ('Add to Steam (MHFZ Plus, and a Play entry for each server)' + $(if (-not $steamOk) { ' - Steam not found' } else { '' })) 24 $y $steamOk; $y += 24
$cSteam.Enabled = $steamOk
$y += 10
[void](Add-Label 'Install to' 12 $y 600 20 $true); $y += 22
$tTarget = Add-PathRow $y $defaultTarget; $y += 40
[void](Add-Label ('Private servers may treat mods as cheating: use at your own risk. Your game files are only changed while you ' +
                  'play through MHFZ Plus, and Help > "Put the original files back" in MHFZ Plus undoes everything.') 12 $y 600 36)
$y += 44
$ok = New-Object System.Windows.Forms.Button
$ok.Text = if ($prevVersion) { 'Update' } else { 'Install' }; $ok.Location = New-Object System.Drawing.Point(414, $y); $ok.Size = New-Object System.Drawing.Size(90, 30)
$cancel = New-Object System.Windows.Forms.Button
$cancel.Text = 'Cancel'; $cancel.Location = New-Object System.Drawing.Point(514, $y); $cancel.Size = New-Object System.Drawing.Size(90, 30)
$cancel.Add_Click({ $form.Close() })
$ok.Add_Click({
    try {
        if (-not $cMeze.Checked -and -not $cRain.Checked) { Show-Error 'Choose MezeLounge, Rain, or both.'; return }
        if ($cMeze.Checked -and -not (Test-MezeDir $tMeze.Text)) { Show-Error "This folder has no mhf.exe and dat folder:`n$($tMeze.Text)"; return }
        if ($cRain.Checked -and -not (Test-RainDir $tRain.Text)) { Show-Error "This folder has no Launcher and Client\dat folders:`n$($tRain.Text)"; return }
        if (-not $cFps.Checked -and -not $cEng.Checked) { Show-Error 'Choose the frame-rate mod, the English translation, or both.'; return }
        $form.Cursor = 'WaitCursor'
        $ok.Enabled = $false
        $r = Install-Plus ([pscustomobject]@{ Target = $tTarget.Text; Meze = $cMeze.Checked; MezeDir = $tMeze.Text; Rain = $cRain.Checked;
                                              RainDir = $tRain.Text; English = $cEng.Checked; Fps = $cFps.Checked; Desktop = $cDesk.Checked; Dxvk = $cDxvk.Checked;
                                              Steam = $cSteam.Checked })
        $form.Cursor = 'Default'
        $msg = "MHFZ Plus is installed.`n`n$($r -join "`n")`n`nFrom now on, start the game from MHFZ Plus (Start menu, desktop or Steam) instead of the usual launcher: " +
               "it also has the settings, and Help for bug reports and the original files.`n`nOpen MHFZ Plus now?"
        $appExe = Join-Path $tTarget.Text 'MHFZ-Plus-App.exe'
        if ([System.Windows.Forms.MessageBox]::Show($msg, $title, 'YesNo', 'Information') -eq 'Yes' -and (Test-Path $appExe)) { Start-Process $appExe -WorkingDirectory $tTarget.Text }
        $form.Close()
    } catch {
        $form.Cursor = 'Default'
        $ok.Enabled = $true
        Show-Error "Setup failed: $($_.Exception.Message)"
    }
})
$form.Controls.Add($ok)
$form.Controls.Add($cancel)
$form.AcceptButton = $ok
$form.CancelButton = $cancel
$form.ClientSize = New-Object System.Drawing.Size(620, ($y + 44))
if ($Screenshot) {
    $form.StartPosition = 'Manual'
    $form.Location = New-Object System.Drawing.Point(-4000, -4000)
    $form.Show()
    [System.Windows.Forms.Application]::DoEvents()
    $bmp = New-Object System.Drawing.Bitmap($form.Width, $form.Height)
    $form.DrawToBitmap($bmp, (New-Object System.Drawing.Rectangle(0, 0, $form.Width, $form.Height)))
    $bmp.Save($Screenshot)
    $form.Close()
    return
}
[void]$form.ShowDialog()
