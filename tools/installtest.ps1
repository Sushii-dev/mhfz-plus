# End-to-end test of the package as a friend would use it, against the simulated games (launcher\simtest.ps1
# makes sim\) and a COPY of the Steam config: extract the zip, install, reinstall into another folder, play both
# servers, update in place, uninstall. Never touches the real Steam files, registry or shortcuts (-Test).
#   -NoPlay   without the two game sessions (they open windows: for when someone is at the PC)
param([string]$Zip = "", [switch]$NoPlay)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if (-not $Zip) { $Zip = Get-ChildItem "$root\dist\MHFZ-Plus-*.zip" | Sort-Object LastWriteTime | Select-Object -Last 1 -ExpandProperty FullName }
$sim = "$root\sim"
$s = Join-Path $env:TEMP "mhfz installtest"   # spaces and non-ASCII in the install paths (A/B below) on purpose
if (Test-Path -LiteralPath $s) { Remove-Item -LiteralPath $s -Recurse -Force }
New-Item -ItemType Directory -Force "$s\steamcfg\grid" | Out-Null
$fails = 0
function Check($cond, $what) { if ($cond) { "  PASS $what" } else { "  FAIL $what"; $script:fails++ } }
# the simulated games as untouched installs (a session leaves MezeLounge with the English files and the mod in, as
# simtest does: a fresh install then finds no originals to keep)
foreach ($pair in @(@("$sim\pristine-meze", "$sim\meze"), @("$sim\pristine-rain", "$sim\rain\Client"))) { Copy-Item "$($pair[0])\*" $pair[1] -Recurse -Force }
Remove-Item "$sim\meze\d3d9.dll", "$sim\meze\mhfz-fps.ini", "$sim\rain\Client\d3d9.dll" -ErrorAction SilentlyContinue

Expand-Archive $Zip "$s\pkg"
$setup = Get-ChildItem "$s\pkg\*\setup\Install.ps1" | Select-Object -First 1 -ExpandProperty FullName
. (Join-Path (Split-Path $setup) 'SteamShortcuts.ps1')
$real = Get-SteamUserConfigDirs | Select-Object -First 1
if ($real -and (Test-Path "$real\shortcuts.vdf")) {
    Copy-Item "$real\shortcuts.vdf" "$s\steamcfg\"
    Get-ChildItem "$real\grid" -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d+' } | Select-Object -First 8 | Copy-Item -Destination "$s\steamcfg\grid\"
}
# (the copy may already hold MHFZ Plus entries from a real install; uninstall removes all of ours, so compare the others)
function OtherEntries { if (-not (Test-Path "$s\steamcfg\shortcuts.vdf")) { return '' }
    ((Read-VdfBinary ([IO.File]::ReadAllBytes("$s\steamcfg\shortcuts.vdf")))['shortcuts'].Values | Where-Object { -not (Test-OurShortcut $_) } |
        ForEach-Object { "$($_['AppName'])|$($_['Exe'])|$($_['appid'])" }) -join "`n" }
$origVdf = if (Test-Path "$s\steamcfg\shortcuts.vdf") { OtherEntries } else { '' }
function Ours { (Read-VdfBinary ([IO.File]::ReadAllBytes("$s\steamcfg\shortcuts.vdf")))['shortcuts'].Values | Where-Object { Test-OurShortcut $_ } }

"=== install"
# (shortcuts into a folder of the test, so setup's shortcut part runs here too: 1.4.0/1.4.1 failed right there)
$links = Join-Path $s 'links'
New-Item -ItemType Directory -Force "$links\Desktop" | Out-Null
Set-Content "$links\Desktop\MHF-Z Rain (Plus).lnk" 'a 1.3 shortcut'   # (setup removes these)
$r = powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Unattended -Test -LinkDir $links -MezeDir "$sim\meze" -RainDir "$sim\rain" -Target "$s\Plüs A" -SteamConfig "$s\steamcfg" -Steam 2>&1
$r | ForEach-Object { "    $_" }
Check (-not ($r -match 'Setup failed|not recognized|Exception')) 'setup ran to its end'
$lnk = Join-Path $links 'Programs\MHFZ Plus.lnk'
$target = if (Test-Path $lnk) { (New-Object -ComObject WScript.Shell).CreateShortcut($lnk).TargetPath } else { '' }
Check ($target -like '*\Plüs A\MHFZ-Plus-App.exe') "Start menu shortcut to the MHFZ Plus app ($target)"
Check (-not (Test-Path "$links\Desktop\MHF-Z Rain (Plus).lnk")) 'the 1.3 desktop shortcut removed'
Check ($r -match 'meze: English ready \(41') 'meze English prepared'
Check ($r -match 'rain: English ready \(40') 'rain English prepared'
$o = @(Ours)
Check ($o.Count -eq 3) "3 Steam entries: the app and a Play entry per server ($($o.Count))"
Check (@($o | Where-Object { [string]$_['Exe'] -like '*\MHFZ-Plus-App.exe*' }).Count -eq 1) 'one of them is the MHFZ Plus app'
$ids = $o | ForEach-Object { $_['appid'] }

$dxvkIni = Get-Content "$s\Plüs A\fps\mhfz-fps.ini" -Encoding UTF8 -ErrorAction SilentlyContinue | Where-Object { $_ -match '^d3d9_backend=' }
Check ((Test-Path "$s\Plüs A\fps\dxvk\d3d9.dll") -and (Test-Path "$s\Plüs A\fps\dxvk\LICENSE.txt")) 'DXVK and its license installed'
Check ($dxvkIni -match '^d3d9_backend=.*\\Plüs A\\fps\\dxvk\\d3d9\.dll$') "settings use the bundled DXVK ($dxvkIni)"

"=== reinstall into another folder"
$r = powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Unattended -Test -LinkDir $links -MezeDir "$sim\meze" -RainDir "$sim\rain" -Target "$s\Plüs B" -SteamConfig "$s\steamcfg" -Steam 2>&1
Check (-not ($r -match 'Setup failed|not recognized|Exception')) 'setup ran to its end'
$target = if (Test-Path $lnk) { (New-Object -ComObject WScript.Shell).CreateShortcut($lnk).TargetPath } else { '' }
Check ($target -like '*\Plüs B\MHFZ-Plus-App.exe') "the shortcut follows to the new folder ($target)"
$o = @(Ours)
Check ($o.Count -eq 3) "still 3 Steam entries ($($o.Count))"
Check (($o | ForEach-Object { $_['appid'] }) -join ',' -eq ($ids -join ',')) 'same Steam ids (artwork and play time kept)'
Check (@($o | Where-Object { [string]$_['Exe'] -like "*\Plüs B\MHFZ-Plus.exe*" }).Count -eq 2 -and
       @($o | Where-Object { [string]$_['Exe'] -like "*\Plüs B\MHFZ-Plus-App.exe*" }).Count -eq 1) 'entries point at the new folder'
Remove-Item -LiteralPath "$s\Plüs A" -Recurse -Force

if ($NoPlay) { "=== play: left out (-NoPlay)" } else {
"=== play"
Remove-Item "$sim\report.txt" -ErrorAction SilentlyContinue
foreach ($srv in 'meze', 'rain') {
    $p = Start-Process "$s\Plüs B\MHFZ-Plus.exe" -ArgumentList $srv -PassThru
    if (-not $p.WaitForExit(200000)) { $p.Kill() }
    Check ($p.ExitCode -eq 0) "$srv session exit code 0"
}
$rep = Get-Content "$sim\report.txt"
$rep | ForEach-Object { "    $_" }
Check ($rep -match 'in game: 41 English') 'meze: English in game'
Check ($rep -match 'GET_TXT: 40 English.*at 1 s present') 'rain: English and mod in game'
Check ($rep -match 'check passed') 'rain: launcher check passed'
}

"=== update in place (the app's Update now: setup -Update keeps every choice)"
$B = "$s\Plüs B"
$iniB = Join-Path $B 'MHFZ-Plus.ini'
$sec = ''   # (the player's choices: Rain chosen, English off for MezeLounge, an app option)
$x = foreach ($l in (Get-Content $iniB -Encoding Unicode)) {
    if ($l -match '^\[(.+)\]$') { $sec = $Matches[1] }
    if ($sec -eq 'meze' -and $l -match '^english=') { 'english=0' }
    elseif ($sec -eq 'general' -and $l -match '^server=') { 'server=rain' }
    else { $l; if ($l -eq '[general]') { 'animations=0' } }
}
Set-Content $iniB $x -Encoding Unicode
$pkgVer = Get-Content (Join-Path (Split-Path (Split-Path $setup)) 'payload\version.txt') | Select-Object -First 1
Set-Content "$B\version.txt" '1.0.0'
Set-Content "$links\Desktop\MHFZ Plus.lnk" 'the desktop shortcut the player has'
$vdfBefore = [Convert]::ToBase64String([IO.File]::ReadAllBytes("$s\steamcfg\shortcuts.vdf"))
$r = powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Unattended -Update -Test -LinkDir $links -Target $B 2>&1
$r | ForEach-Object { "    $_" }
$exitUpd = $LASTEXITCODE
Check ($exitUpd -eq 0 -and ($r -match "updated to $([regex]::Escape($pkgVer))") -and -not ($r -match 'Setup failed|not recognized|Exception|update failed')) "setup -Update ran to its end (exit $exitUpd)"
$after = Get-Content $iniB -Encoding Unicode
$sec = ''; $kv = @{}
foreach ($l in $after) { if ($l -match '^\[(.+)\]$') { $sec = $Matches[1] } elseif ($l -match '^([^=]+)=(.*)$') { $kv["$sec.$($Matches[1])"] = $Matches[2] } }
Check ($kv['meze.english'] -eq '0' -and $kv['rain.english'] -eq '1') "English stays off for MezeLounge, on for Rain ($($kv['meze.english']) / $($kv['rain.english']))"
Check ($kv['general.server'] -eq 'rain' -and $kv['general.animations'] -eq '0') "the chosen server and the app's options stay ($($kv['general.server']), animations=$($kv['general.animations']))"
Check ($kv['meze.game'] -eq "$sim\meze" -and $kv['rain.game'] -eq "$sim\rain\Client") 'both games found again from the install'
Check ($r -match 'meze: English off' -and $r -match 'rain: English ready \(40') 'English rebuilt only where it is on'
Check ((Get-Content "$B\version.txt" | Select-Object -First 1) -eq $pkgVer) "the new program files are in ($pkgVer)"
$tpl = Get-Content "$B\fps\mhfz-fps.ini" -Encoding UTF8 | Where-Object { $_ -match '^d3d9_backend=' }
Check ($tpl -eq "d3d9_backend=$B\fps\dxvk\d3d9.dll") "the default mod settings keep the bundled DXVK ($tpl)"
Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes("$s\steamcfg\shortcuts.vdf")) -ceq $vdfBefore) 'Steam entries untouched'
$dl = Join-Path $links 'Desktop\MHFZ Plus.lnk'
$target = if (Test-Path $dl) { try { (New-Object -ComObject WScript.Shell).CreateShortcut($dl).TargetPath } catch { '' } } else { '' }
Check ($target -like '*\Plüs B\MHFZ-Plus-App.exe') "the desktop shortcut the player had stays ($target)"
$r = powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Unattended -Update -Test -LinkDir $links -Target "$s\nothing here" 2>&1
Check ($LASTEXITCODE -eq 1 -and ($r -match 'no MHFZ Plus install') -and -not (Test-Path "$s\nothing here")) 'no install there: nothing done, said why'
# another program has MHFZ-Plus-App.exe open while the update copies (1.4.4: Steam or an antivirus right after the
# app closed - setup stopped half-way): for a moment, all along as a running program's file, all along and locked
$newApp = (Get-FileHash (Join-Path (Split-Path (Split-Path $setup)) 'payload\MHFZ-Plus-App.exe')).Hash
function Update-Held($share, [int]$seconds) {
    $app = "$B\MHFZ-Plus-App.exe"
    $h = [IO.File]::Open($app, 'Open', 'Read', $share)
    $out = Join-Path $s 'held.txt'
    $p = Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$setup`" -Unattended -Update -Test -LinkDir `"$links`" -Target `"$B`"" -NoNewWindow -RedirectStandardOutput $out -PassThru
    $null = $p.Handle   # (so its exit code is kept)
    if ($seconds -ge 0) { Start-Sleep $seconds; $h.Close() }
    $p.WaitForExit()
    $held = @(Get-ChildItem $B -Filter 'MHFZ-Plus-App.exe.old-*').Count
    if ($seconds -lt 0) { $h.Close() }
    [pscustomobject]@{ Code = $p.ExitCode; Out = @(Get-Content $out); Aside = $held; Same = (Get-FileHash $app).Hash -eq $newApp }
}
$u = Update-Held 'Read' 3
Check ($u.Code -eq 0 -and ($u.Out -match 'updated to') -and $u.Same) "held for 3 s: waited for, updated (exit $($u.Code))"
$u = Update-Held 'Read, Delete' -1
Check ($u.Code -eq 0 -and ($u.Out -match 'updated to') -and $u.Same -and $u.Aside -eq 1) "in use all along (as a running program): moved aside, updated (exit $($u.Code), $($u.Aside) aside)"
$u = Update-Held 'Read' -1
$why = ($u.Out | Where-Object { $_ -match 'update failed' }) -join ' '
Check ($u.Code -eq 1 -and $why -match 'MHFZ-Plus-App\.exe is in use by another program \(by .+\)') "locked all along: stopped, says by whom ($why)"
Check (@(Get-ChildItem $B -Recurse -Filter '*.old-*').Count -eq 0) 'the file moved aside last time is gone'
Set-Content $iniB ((Get-Content $iniB -Encoding Unicode) -replace '^english=0$', 'english=1') -Encoding Unicode

"=== Steam's file kept safe (a layout we don't know is left alone)"
$vs = Join-Path $s 'vdfsafe'
New-Item -ItemType Directory -Force $vs | Out-Null
$bad = [byte[]](@(0) + [Text.Encoding]::ASCII.GetBytes('shortcuts') + @(0, 0) + [Text.Encoding]::ASCII.GetBytes('0') + @(0, 7) +
                [Text.Encoding]::ASCII.GetBytes('big') + @(0, 5, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8))   # (a uint64 field: not ours to rewrite)
[IO.File]::WriteAllBytes("$vs\shortcuts.vdf", $bad)
$refused = $false
try { Set-SteamShortcuts $vs @([pscustomobject]@{ Name = 'MHFZ Plus'; Exe = 'C:\a.exe'; StartDir = 'C:\'; LaunchOptions = ''; Icon = 'C:\a.exe' }) } catch { $refused = $true }
Check ($refused -and [Convert]::ToBase64String([IO.File]::ReadAllBytes("$vs\shortcuts.vdf")) -ceq [Convert]::ToBase64String($bad)) 'a file we don''t know: refused, left exactly as it was'

"=== uninstall"
powershell -NoProfile -ExecutionPolicy Bypass -File "$s\Plüs B\Uninstall.ps1" -Quiet -Test -SteamConfig "$s\steamcfg" 2>&1 | ForEach-Object { "    $_" }
Check (-not (Test-Path "$s\Plüs B")) 'install folder removed'
Check (@(Ours).Count -eq 0) 'Steam entries removed'
if ($origVdf) { Check ((OtherEntries) -eq $origVdf) 'the other Steam entries are as before' }
$bad = 0
foreach ($pair in @(@("$sim\meze", "$sim\pristine-meze"), @("$sim\rain\Client", "$sim\pristine-rain"))) {
    Get-ChildItem $pair[1] -Recurse -File | ForEach-Object {
        if ((Get-FileHash $_.FullName).Hash -ne (Get-FileHash (Join-Path $pair[0] $_.FullName.Substring($pair[1].Length + 1))).Hash) { $bad++ }
    }
}
Check ($bad -eq 0) "game folders back to the originals ($bad wrong)"
Check (-not (Test-Path "$sim\meze\d3d9.dll") -and -not (Test-Path "$sim\rain\Client\d3d9.dll")) 'frame-rate mod removed'
"=== $fails failure(s)"
