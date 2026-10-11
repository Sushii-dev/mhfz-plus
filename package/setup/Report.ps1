# MHFZ Plus bug report (MHFZ Plus > Help > "Make a bug report"): every MHFZ Plus log, the settings and the PC's
# specs in one zip on the desktop, ready for a GitHub issue. User names in file paths are replaced with <user> and the
# PC's name with <pc>. The newest clip (the Delete key) goes into zips of its own, in parts GitHub takes (25 MB a file).
#   -Dir <folder>   the MHFZ Plus folder (default: this script's)   -Out <folder>   where the zips go (default: desktop)
#   -NoUI           no windows (testing)    -PartMB <n>   the clip's part size in MiB (22: under GitHub's 25 MB with room to spare)
param([string]$Dir = '', [string]$Out = '', [switch]$NoUI, [double]$PartMB = 22)
$ErrorActionPreference = 'Stop'
if (-not $Dir) { $Dir = $PSScriptRoot }
if (-not $Out) { $Out = [Environment]::GetFolderPath('Desktop') }
New-Item -ItemType Directory -Force $Out | Out-Null
$title = 'MHFZ Plus bug report'
$issue = 'https://github.com/Sushii-dev/mhfz-plus/issues/new?template=bug_report.yml'
if (-not $NoUI) { Add-Type -AssemblyName System.Windows.Forms }
function Say($msg, $buttons = 'OK', $icon = 'Information') {
    if ($NoUI) { $msg; return 'OK' }
    return [System.Windows.Forms.MessageBox]::Show($msg, $title, $buttons, $icon)
}

$stamp = Get-Date -Format 'yyyy-MM-dd-HHmm'
$work = Join-Path $env:TEMP "mhfz-plus-report-$stamp"
Get-ChildItem $env:TEMP -Directory -Filter 'mhfz-plus-report-*' -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue   # (an earlier run that stopped)
New-Item -ItemType Directory -Force $work | Out-Null
$lines = New-Object System.Collections.Generic.List[string]
function Add-Line($s) { $lines.Add([string]$s) }

# what to collect: our own files only (never the game's, never the Rain launcher's logs)
$patterns = 'MHFZ-Plus*.log', 'mhfz-fps.log', 'mhfz-fps.prev.log', 'mhfz-fps.ini', 'mhfz-fps-dump.txt', '*_d3d9.log', '*_dxgi.log',
            'install-report.txt'
function Copy-Ours($from, $to) {
    if (-not $from -or -not (Test-Path -LiteralPath $from)) { return 0 }
    $n = 0
    foreach ($p in $patterns) {
        foreach ($f in @(Get-ChildItem -LiteralPath $from -Filter $p -File -ErrorAction SilentlyContinue)) {
            New-Item -ItemType Directory -Force $to | Out-Null
            Copy-Item -LiteralPath $f.FullName (Join-Path $to $f.Name) -Force
            $n++
        }
    }
    return $n
}
# (Join-Path throws for a drive the PC doesn't have, e.g. a game on a removed USB drive)
function P($a, $b) { if (-not $a) { return $null }; try { [IO.Path]::Combine($a, $b) } catch { $null } }
function Exists($p) { $p -and (Test-Path -LiteralPath $p -ErrorAction SilentlyContinue) }
function Read-Ini($path) {
    $ini = @{}
    $sec = ''
    if (Test-Path -LiteralPath $path) {
        foreach ($line in [IO.File]::ReadAllLines($path)) {   # (UTF-16 or UTF-8, by its byte-order mark)
            if ($line -match '^\s*\[(.+)\]\s*$') { $sec = $Matches[1].ToLower(); $ini[$sec] = @{} }
            elseif ($sec -and $line -match '^\s*([^;=]+?)\s*=\s*(.*)$') { $ini[$sec][$Matches[1].ToLower()] = $Matches[2] }
        }
    }
    return $ini
}

$version = Get-Content (Join-Path $Dir 'version.txt') -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $version) { $version = 'unknown' }
Add-Line "MHFZ Plus bug report, $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
Add-Line "MHFZ Plus version: $version"
Add-Line ''
# the short version for the form's "Your setup" (filled in through the form's link)
$setup = New-Object System.Collections.Generic.List[string]

# the PC
try {
    $os = Get-CimInstance Win32_OperatingSystem
    Add-Line "Windows: $($os.Caption) $($os.Version) ($($os.OSArchitecture)), RAM $([math]::Round($os.TotalVisibleMemorySize / 1MB)) GB"
    $setup.Add("MHFZ Plus $version on $($os.Caption -replace '^Microsoft\s+', '') ($($os.Version))")
    foreach ($c in @(Get-CimInstance Win32_Processor)) {
        Add-Line "CPU: $($c.Name.Trim()) ($($c.NumberOfCores) cores, $($c.NumberOfLogicalProcessors) threads)"
        $setup.Add("CPU: $($c.Name.Trim()) ($($c.NumberOfCores) cores, $($c.NumberOfLogicalProcessors) threads), RAM $([math]::Round($os.TotalVisibleMemorySize / 1MB)) GB")
    }
    $gpus = @(Get-CimInstance Win32_VideoController)
    foreach ($g in $gpus) {
        $mode = if ($g.CurrentHorizontalResolution) { ", $($g.CurrentHorizontalResolution)x$($g.CurrentVerticalResolution) at $($g.CurrentRefreshRate) Hz" } else { '' }
        $date = if ($g.DriverDate) { ([datetime]$g.DriverDate).ToString('yyyy-MM-dd') } else { '?' }
        Add-Line "GPU: $($g.Name), driver $($g.DriverVersion) ($date)$mode"
        # (the setup: only the cards showing a picture, unless none says so - not idle on-board graphics or virtual displays)
        if ($mode -or -not ($gpus | Where-Object { $_.CurrentHorizontalResolution })) { $setup.Add("GPU: $($g.Name), driver $($g.DriverVersion)$mode") }
    }
} catch { Add-Line "(PC details not available: $($_.Exception.Message))"; $setup.Add("MHFZ Plus $version on Windows") }
$vk = Join-Path ([Environment]::GetFolderPath('SystemX86')) 'vulkan-1.dll'
Add-Line "32-bit Vulkan (needed for DXVK): $(if (Test-Path $vk) { 'installed' } else { 'missing' })"
if (-not (Test-Path $vk)) { $setup.Add('32-bit Vulkan (needed for DXVK): missing') }
Add-Line ''

# MHFZ Plus and the games
$n = Copy-Ours $Dir (Join-Path $work 'logs\mhfz-plus')
$iniFile = Join-Path $Dir 'MHFZ-Plus.ini'
if (Test-Path -LiteralPath $iniFile) {
    New-Item -ItemType Directory -Force (Join-Path $work 'logs\mhfz-plus') | Out-Null
    Copy-Item -LiteralPath $iniFile (Join-Path $work 'logs\mhfz-plus\MHFZ-Plus.ini')
}
$ini = Read-Ini $iniFile
$clips = @()
foreach ($s in 'meze', 'rain') {
    if (-not $ini.ContainsKey($s)) { continue }
    $game = $ini[$s]['game']
    if ($game -and -not [IO.Path]::IsPathRooted($game)) { $game = P $Dir $game }
    $name = if ($s -eq 'meze') { 'MezeLounge' } else { 'Rain' }
    Add-Line "${name}: game folder $game ($(if (Exists $game) { 'found' } else { 'NOT FOUND' }))"
    Add-Line "  english=$($ini[$s]['english'])  fps=$($ini[$s]['fps'])  fps_mode=$($ini[$s]['fps_mode'])"
    $state = P $Dir "fps-state\$s"
    if (Exists $game) { $n += Copy-Ours $game (Join-Path $work "logs\$s-game") }
    $n += Copy-Ours $state (Join-Path $work "logs\$s-state")
    # the frame-rate mod's settings in use: the game folder's, else the kept ones (Rain), else the defaults
    $modIni = @((P $game 'mhfz-fps.ini'), (P $state 'mhfz-fps.ini'), (P $Dir 'fps\mhfz-fps.ini')) |
        Where-Object { Exists $_ } | Select-Object -First 1
    $backend = "Windows' Direct3D 9"
    if ($modIni) {
        $be = (Get-Content -LiteralPath $modIni | Where-Object { $_ -match '^\s*d3d9_backend\s*=' } | Select-Object -First 1)
        if ($be -and ($be -split '=', 2)[1].Trim()) { $backend = 'DXVK / ' + ($be -split '=', 2)[1].Trim() }
        Add-Line "  graphics backend: $backend"
    }
    $eng = switch ("$($ini[$s]['english'])".ToLowerInvariant()) { '0' { 'off' } 'safe' { 'tested parts only' } default { 'on' } }
    $mod = if ("$($ini[$s]['fps'])" -eq '0') { 'off' } else { 'on, ' + ($backend -replace '^DXVK / .*', 'DXVK') }
    $setup.Add("${name}: English $eng, frame-rate mod $mod")
    # other DLLs in the game folder (another mod: ReShade, dgVoodoo...)
    if (Exists $game) {
        $dlls = @(Get-ChildItem -LiteralPath $game -Filter *.dll -File | Where-Object { $_.Name -match '^(d3d9|dxgi|dinput8|d3d8|ddraw|winmm|version|dsound)\.dll$' } |
            ForEach-Object { $mine = [IO.File]::ReadAllText($_.FullName).Contains('mhfz-fps.ini'); "$($_.Name)$(if ($mine) { ' (MHFZ Plus)' } else { ' (NOT MHFZ Plus: another mod?)' })" })
        Add-Line "  proxy DLLs in the game folder: $(if ($dlls) { $dlls -join ', ' } else { 'none' })"
    }
    # clips (Delete key): retro, retro-2... in the game folder or fps-state
    foreach ($d in @($game, $state)) {
        if (Exists $d) { $clips += @(Get-ChildItem -LiteralPath $d -Directory -Filter 'retro*' -ErrorAction SilentlyContinue) }
    }
}
Add-Line ''

# user name and PC name out of every text file (paths like \Users\<name>\... on any drive)
$profileName = Split-Path $env:USERPROFILE -Leaf
foreach ($f in @(Get-ChildItem -LiteralPath $work -Recurse -File)) {
    $t = [IO.File]::ReadAllText($f.FullName)
    $t = [regex]::Replace($t, '(?i)([\\/]Users[\\/])[^\\/\r\n]+', '$1<user>')
    foreach ($word in @($profileName, $env:USERNAME, $env:COMPUTERNAME) | Where-Object { $_ -and $_.Length -ge 3 } | Select-Object -Unique) {
        $t = [regex]::Replace($t, [regex]::Escape($word), $(if ($word -eq $env:COMPUTERNAME) { '<pc>' } else { '<user>' }), 'IgnoreCase')
    }
    [IO.File]::WriteAllText($f.FullName, $t, (New-Object Text.UTF8Encoding $false))
}

# zips of at most -PartMB each (GitHub takes files up to 25 MB): <name>.zip, or <name>-1of3.zip, <name>-2of3.zip...
function Write-Parts($folder, $base, $name) {
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $limit = [long]($PartMB * 1MB)
    $made = New-Object System.Collections.Generic.List[string]
    $fs = $null; $za = $null
    foreach ($f in @(Get-ChildItem -LiteralPath $folder -Recurse -File | Sort-Object FullName)) {
        $guess = if ($f.Extension -match '^\.(jpe?g|png|zip|mp4|webm)$') { $f.Length } else { [long]($f.Length / 3) }   # (pictures don't shrink in a zip; the rest does)
        if ($za -and $fs.Position + $guess -gt $limit) { $za.Dispose(); $fs.Dispose(); $za = $null }
        if (-not $za) {
            $made.Add((Join-Path $Out ("$name-part{0}.zip" -f ($made.Count + 1))))
            $fs = [IO.File]::Open($made[$made.Count - 1], 'Create')
            $za = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
        }
        $rel = $f.FullName.Substring($base.Length).TrimStart('\')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $f.FullName, $rel, [IO.Compression.CompressionLevel]::Optimal)
    }
    if ($za) { $za.Dispose(); $fs.Dispose() }
    for ($i = 0; $i -lt $made.Count; $i++) {
        $to = if ($made.Count -eq 1) { Join-Path $Out "$name.zip" } else { Join-Path $Out ("$name-{0}of{1}.zip" -f ($i + 1), $made.Count) }
        if (Test-Path -LiteralPath $to) { Remove-Item -LiteralPath $to -Force }
        Move-Item -LiteralPath $made[$i] $to
        $to
    }
}

# the newest clip (one press of Delete: a numbered folder in retro*), if it is from the last week. Its frames are
# full-size pictures (~100 MB a clip), so it comes in parts to drag in together; the names say when it was saved.
$clip = @($clips | ForEach-Object {
        $sub = @(Get-ChildItem -LiteralPath $_.FullName -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d+$' })
        if ($sub) { $sub } else { $_ } }) | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$clipZips = @()
if ($clip -and $clip.LastWriteTime -gt (Get-Date).AddDays(-7)) {
    $saved = $clip.LastWriteTime
    $clipZips = @(Write-Parts $clip.FullName (Split-Path $clip.FullName -Parent) "MHFZ-Plus-clip-from-$($saved.ToString('yyyy-MM-dd-HHmm'))")
    $mb = ($clipZips | ForEach-Object { (Get-Item -LiteralPath $_).Length } | Measure-Object -Sum).Sum / 1MB
    Add-Line "Clip: $(Split-Path (Split-Path $clip.FullName -Parent) -Leaf)\$($clip.Name), saved $($saved.ToString('yyyy-MM-dd HH:mm')) -> $($clipZips.Count) zip(s), $([math]::Round($mb, 1)) MB"
} else { Add-Line 'Clip: none from the last week (press Delete right after you see a glitch to save one)' }
Add-Line "Files collected: $n"

$report = ($lines -join "`r`n") + "`r`n"
$report = [regex]::Replace($report, '(?i)([\\/]Users[\\/])[^\\/\r\n]+', '$1<user>')
foreach ($word in @($profileName, $env:USERNAME) | Where-Object { $_ -and $_.Length -ge 3 } | Select-Object -Unique) { $report = [regex]::Replace($report, [regex]::Escape($word), '<user>', 'IgnoreCase') }
if ($env:COMPUTERNAME) { $report = [regex]::Replace($report, [regex]::Escape($env:COMPUTERNAME), '<pc>', 'IgnoreCase') }
[IO.File]::WriteAllText((Join-Path $work 'report.txt'), $report, (New-Object Text.UTF8Encoding $false))
# the form, with "Your setup" filled in (names taken out the same way)
$setupText = $setup -join "`n"
$setupText = [regex]::Replace($setupText, '(?i)([\\/]Users[\\/])[^\\/\r\n]+', '$1<user>')
foreach ($word in @($profileName, $env:USERNAME, $env:COMPUTERNAME) | Where-Object { $_ -and $_.Length -ge 3 } | Select-Object -Unique) { $setupText = [regex]::Replace($setupText, [regex]::Escape($word), '<user>', 'IgnoreCase') }
$form = "$issue&setup=$([Uri]::EscapeDataString($setupText))"

$zip = Join-Path $Out "MHFZ-Plus-report-$stamp.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $work '*') -DestinationPath $zip -CompressionLevel Optimal
Remove-Item -LiteralPath $work -Recurse -Force

$msg = "Saved on your desktop:`n  $(Split-Path $zip -Leaf)   (logs, settings and your PC's specs)"
if ($clipZips) {
    $msg += "`n  " + (($clipZips | ForEach-Object { Split-Path $_ -Leaf }) -join "`n  ")
    $msg += "`n  (the newest clip, saved $($saved.ToString('yyyy-MM-dd HH:mm'))$(if ($clipZips.Count -gt 1) { ", in $($clipZips.Count) parts: drag them in together" }). Add it only if it shows this problem.)"
}
$msg += "`n`nYour user name in file paths was replaced with <user>.`n`nOpen the bug report form on GitHub now? Your setup is filled in: write what happened and drag the zip file(s) into it."
if (-not $NoUI) { Start-Process explorer.exe "/select,`"$zip`"" }
if ((Say $msg 'YesNo' 'Information') -eq 'Yes') { Start-Process $form }
if ($NoUI) { $zip; $clipZips; $form }   # (MHFZ Plus reads these: the zips, then the form's link)
