# Steam non-Steam-game shortcuts (userdata\<id>\config\shortcuts.vdf, binary VDF).
# Types: 0x00 map, 0x01 string, 0x02 int32, 0x08 end of map. Strings are UTF-8, zero-terminated.

function Get-SteamPath {
    $p = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $p) { $p = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath }
    if ($p -and (Test-Path $p)) { return (Resolve-Path $p).Path }
    return $null
}

function Get-SteamUserConfigDirs {
    $s = Get-SteamPath
    if (-not $s) { return @() }
    @(Get-ChildItem (Join-Path $s 'userdata') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne '0' -and (Test-Path (Join-Path $_.FullName 'config')) } |
        ForEach-Object { Join-Path $_.FullName 'config' })
}

function Read-VdfBinary([byte[]]$b) {
    $script:vp = 0
    function rstr {
        $start = $script:vp
        while ($b[$script:vp] -ne 0) { $script:vp++ }
        $s = [Text.Encoding]::UTF8.GetString($b, $start, $script:vp - $start)
        $script:vp++
        $s
    }
    function rmap {
        $m = [ordered]@{}
        while ($true) {
            $t = $b[$script:vp]; $script:vp++
            if ($t -eq 8) { return $m }
            $k = rstr
            switch ($t) {
                0 { $m[$k] = rmap }
                1 { $m[$k] = rstr }
                2 { $m[$k] = [BitConverter]::ToInt32($b, $script:vp); $script:vp += 4 }
                default { throw "unsupported VDF type $t at $($script:vp)" }
            }
        }
    }
    $root = rmap
    return $root
}

function Write-VdfBinary($map) {
    $ms = New-Object IO.MemoryStream
    function wstr([string]$s) { $x = [Text.Encoding]::UTF8.GetBytes($s); $ms.Write($x, 0, $x.Length); $ms.WriteByte(0) }
    function wmap($m) {
        foreach ($k in $m.Keys) {
            $v = $m[$k]
            if ($v -is [System.Collections.IDictionary]) { $ms.WriteByte(0); wstr $k; wmap $v }
            elseif ($v -is [int]) { $ms.WriteByte(2); wstr $k; $x = [BitConverter]::GetBytes([int]$v); $ms.Write($x, 0, 4) }
            else { $ms.WriteByte(1); wstr $k; wstr ([string]$v) }
        }
        $ms.WriteByte(8)
    }
    wmap $map
    return $ms.ToArray()
}

# Id for a new non-Steam shortcut: crc32(exe + name) with the top bit set, as tools like Steam ROM Manager do. Steam keeps
# whatever id the file holds (it also names the artwork files in config\grid), so ids of existing entries are kept.
function Get-ShortcutAppId([string]$exe, [string]$name) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($exe + $name)
    $crc = [uint32]::MaxValue
    foreach ($x in $bytes) {
        $crc = $crc -bxor $x
        for ($i = 0; $i -lt 8; $i++) { if ($crc -band 1) { $crc = ($crc -shr 1) -bxor 0xEDB88320 } else { $crc = $crc -shr 1 } }
    }
    $crc = ($crc -bxor [uint32]::MaxValue) -bor 0x80000000
    return [BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$crc), 0)
}

# An entry of ours: it starts MHFZ-Plus.exe (from any folder: a moved install or an 8.3 path still counts).
function Test-OurShortcut($e) {
    $exe = [string]$e['Exe']; if (-not $exe) { $exe = [string]$e['exe'] }
    return $exe.Trim('"') -like '*\MHFZ-Plus.exe'
}

# Adds (or updates) shortcuts. $entries: objects with Name, Exe, StartDir, LaunchOptions, Icon.
# Entries of ours with the same launch options are replaced (keeping their id, so Steam keeps artwork and play
# time), everything else is kept as it was.
function Set-SteamShortcuts([string]$configDir, $entries) {
    $file = Join-Path $configDir 'shortcuts.vdf'
    $root = [ordered]@{ 'shortcuts' = [ordered]@{} }
    if (Test-Path $file) {
        Copy-Item $file "$file.mhfz-plus-backup" -Force
        $root = Read-VdfBinary ([IO.File]::ReadAllBytes($file))
        if (-not $root.Contains('shortcuts')) { $root['shortcuts'] = [ordered]@{} }
    }
    $old = $root['shortcuts']
    $keep = New-Object System.Collections.ArrayList
    $prev = @{}   # our earlier entries by launch options: their id, play time and tags are kept
    foreach ($k in $old.Keys) {
        $e = $old[$k]
        $mine = $false
        foreach ($n in $entries) { if ((Test-OurShortcut $e) -and [string]$e['LaunchOptions'] -eq $n.LaunchOptions) { $mine = $true } }
        if ($mine) { $prev[[string]$e['LaunchOptions']] = $e } else { [void]$keep.Add($e) }
    }
    foreach ($n in $entries) {
        $q = "`"$($n.Exe)`""
        $p = $prev[$n.LaunchOptions]
        $e = [ordered]@{}
        $e['appid'] = if ($p -and $p.Contains('appid')) { $p['appid'] } else { Get-ShortcutAppId $q $n.Name }
        $e['AppName'] = $n.Name
        $e['Exe'] = $q
        $e['StartDir'] = "`"$($n.StartDir)`""
        $e['icon'] = $n.Icon
        $e['ShortcutPath'] = ''
        $e['LaunchOptions'] = $n.LaunchOptions
        $e['IsHidden'] = 0
        $e['AllowDesktopConfig'] = 1
        $e['AllowOverlay'] = 1
        $e['OpenVR'] = 0
        $e['Devkit'] = 0
        $e['DevkitGameID'] = ''
        $e['DevkitOverrideAppID'] = 0
        $e['LastPlayTime'] = if ($p -and $p.Contains('LastPlayTime')) { $p['LastPlayTime'] } else { 0 }
        $e['FlatpakAppID'] = ''
        $e['sortas'] = ''
        $e['tags'] = if ($p -and $p.Contains('tags')) { $p['tags'] } else { [ordered]@{} }
        [void]$keep.Add($e)
    }
    $new = [ordered]@{}
    for ($i = 0; $i -lt $keep.Count; $i++) { $new["$i"] = $keep[$i] }
    $root['shortcuts'] = $new
    $bytes = Write-VdfBinary $root
    [IO.File]::WriteAllBytes($file, $bytes)
}

# Removes our shortcuts (everything that starts MHFZ-Plus.exe).
function Remove-SteamShortcuts([string]$configDir) {
    $file = Join-Path $configDir 'shortcuts.vdf'
    if (-not (Test-Path $file)) { return }
    Copy-Item $file "$file.mhfz-plus-backup" -Force
    $root = Read-VdfBinary ([IO.File]::ReadAllBytes($file))
    $old = $root['shortcuts']
    $keep = New-Object System.Collections.ArrayList
    foreach ($k in $old.Keys) { $e = $old[$k]; if (-not (Test-OurShortcut $e)) { [void]$keep.Add($e) } }
    $new = [ordered]@{}
    for ($i = 0; $i -lt $keep.Count; $i++) { $new["$i"] = $keep[$i] }
    $root['shortcuts'] = $new
    [IO.File]::WriteAllBytes($file, (Write-VdfBinary $root))
}
