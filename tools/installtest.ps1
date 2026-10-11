# End-to-end test of the package as a friend would use it, against the simulated games (launcher\simtest.ps1
# makes sim\) and a COPY of the Steam config: extract the zip, install, reinstall into another folder, play both
# servers, uninstall. Never touches the real Steam files, registry or shortcuts (-Test).
param([string]$Zip = "")
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
$r = powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Unattended -Test -MezeDir "$sim\meze" -RainDir "$sim\rain" -Target "$s\Plüs A" -SteamConfig "$s\steamcfg" -Steam 2>&1
$r | ForEach-Object { "    $_" }
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
$r = powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Unattended -Test -MezeDir "$sim\meze" -RainDir "$sim\rain" -Target "$s\Plüs B" -SteamConfig "$s\steamcfg" -Steam 2>&1
$o = @(Ours)
Check ($o.Count -eq 3) "still 3 Steam entries ($($o.Count))"
Check (($o | ForEach-Object { $_['appid'] }) -join ',' -eq ($ids -join ',')) 'same Steam ids (artwork and play time kept)'
Check (@($o | Where-Object { [string]$_['Exe'] -like "*\Plüs B\MHFZ-Plus.exe*" }).Count -eq 2 -and
       @($o | Where-Object { [string]$_['Exe'] -like "*\Plüs B\MHFZ-Plus-App.exe*" }).Count -eq 1) 'entries point at the new folder'
Remove-Item -LiteralPath "$s\Plüs A" -Recurse -Force

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
