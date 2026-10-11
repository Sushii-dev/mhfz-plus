# Tests of the MHFZ Plus app (build\MHFZ-Plus-App.exe) without a window, on a made-up install in %TEMP% (fake game
# folders: the real ones are never read or written): every page renders and fast enough, the icon is in the exe in
# every size, a chosen server is remembered, only web pages are opened, the update check only offers this project's
# release pages. Runs any time (nothing shows on the screen). Exit code = number of failures.
#   tools\apptest.ps1 [-Exe build\MHFZ-Plus-App.exe] [-Keep]   (-Keep: leave the made-up install and the shots)
param([string]$Exe = '', [switch]$Keep)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $root 'build\MHFZ-Plus-App.exe' }
$Exe = (Resolve-Path $Exe).Path
$t = Join-Path $env:TEMP 'mhfz-apptest'
if (Test-Path $t) { Remove-Item $t -Recurse -Force }
$base = Join-Path $t 'MHFZ Plus'   # (a space in the path on purpose)
$fails = 0
function Check($cond, $what) { if ($cond) { "  PASS $what" } else { "  FAIL $what"; $script:fails++ } }

# the made-up install: two games, the default mod settings, the backdrop, a few English files
foreach ($d in "$t\games\meze", "$t\games\rain\Client", "$base\fps", "$base\ui", "$base\english\meze", "$base\english\rain") { New-Item -ItemType Directory -Force $d | Out-Null }
Copy-Item (Join-Path $root 'package\fps\mhfz-fps.ini') "$base\fps\"
Copy-Item (Join-Path $root 'package\ui\backdrop.jpg') "$base\ui\"
Set-Content "$base\version.txt" '1.4.0'
foreach ($s in 'meze', 'rain') { Set-Content "$base\english\$s\manifest.txt" (1..12 | ForEach-Object { "dat\f$_.bin|1|a|1|b|f$_.delta" }) }
Set-Content "$base\MHFZ-Plus.ini" -Encoding Unicode @"
[general]
server=meze
update_check=0
[meze]
game=$t\games\meze
fps_mode=stay
[rain]
game=$t\games\rain\Client
fps_mode=swap
"@
$env:MHFZPLUS_BASE = $base

"== every page, off-screen"
$shots = Join-Path $t 'shots'
$p = Start-Process $Exe -ArgumentList '--shots', "`"$shots`"" -Wait -PassThru
Check ($p.ExitCode -eq 0) "--shots ran (exit $($p.ExitCode))"
$n = @(Get-ChildItem $shots -Filter *.png -ErrorAction SilentlyContinue).Count
Check ($n -ge 20) "$n pictures (Play, Settings pages, Help, toast, key prompt, animation moments, hover cursors)"
foreach ($l in @(Get-Content (Join-Path $shots 'render-ms.txt') -ErrorAction SilentlyContinue)) {
    if ($l -match '^(\w+): whole window ([\d.]+) ms') { Check ([double]$Matches[2] -lt 40) "$($Matches[1]): a whole frame at 150% in $($Matches[2]) ms (under 40: animations stay smooth)" }
}

"== inside the app (Windows PowerShell, the app's own .NET)"
$inside = Join-Path $t 'inside.ps1'
Set-Content $inside -Encoding UTF8 @'
param([string]$Exe)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$asm = [Reflection.Assembly]::LoadFile($Exe)
$bf = [Reflection.BindingFlags]'NonPublic,Public,Instance,Static'
function Check($cond, $what) { if ($cond) { "  PASS $what" } else { "  FAIL $what" } }
$s = $asm.GetManifestResourceStream('mhfz-plus.ico')
Check ($s -ne $null) "the window icon is in the exe"
if ($s) {
    $ico = New-Object System.Drawing.Icon($s)
    $sizes = foreach ($z in 16, 24, 32, 48) { (New-Object System.Drawing.Icon($ico, $z, $z)).Width }
    Check (($sizes -join ',') -eq '16,24,32,48') "the icon at 16, 24, 32 and 48 px (got $($sizes -join ','))"
}
$t = $asm.GetType('MhfzPlus.HubForm')
$ini = Join-Path $env:MHFZPLUS_BASE 'MHFZ-Plus.ini'
$f = [Activator]::CreateInstance($t, @([single]1, $true))
$servers = $t.GetField('servers', $bf).GetValue($f)
Check ($servers.Count -eq 2) "both made-up games found ($($servers.Count))"
Check ($servers[$t.GetField('sel', $bf).GetValue($f)].Id -eq 'meze') "starts with the server in MHFZ-Plus.ini (meze)"
$t.GetMethod('ChooseServer', $bf).Invoke($f, @([int]([array]::IndexOf(@($servers | ForEach-Object { $_.Id }), 'rain')), $true))
Check ((Get-Content $ini) -contains 'server=rain') "choosing Rain writes server=rain"
$games = $t.GetField('games', $bf).GetValue($f)
Check ($games[$t.GetField('gameIdx', $bf).GetValue($f)].Name -eq 'rain') "Settings follow the chosen server"
$t.GetMethod('OpenUrl', $bf).Invoke($f, @('C:\Windows\System32\calc.exe'))
$toast = $t.GetField('toast', $bf).GetValue($f)
Check ($toast -ne $null -and $toast.GetType().GetField('Title').GetValue($toast) -eq "Couldn't open the link") "a program given as a link is not run"
$rp = $t.GetMethod('ReleasePage', $bf)
$repo = 'https://github.com/Sushii-dev/mhfz-plus'
Check ($rp.Invoke($null, @("$repo/releases/tag/v9.9.9")) -eq "$repo/releases/tag/v9.9.9") "the update check's release page is kept"
Check ($rp.Invoke($null, @('https://example.com/releases/x')) -eq "$repo/releases/latest") "another site from the update check is replaced by the project's page"
Check ($rp.Invoke($null, @("$repo/releases/x`" & calc")) -eq "$repo/releases/latest") "a page address with quotes is replaced"
$f.Dispose()
$f2 = [Activator]::CreateInstance($t, @([single]1, $true))
Check ($t.GetField('servers', $bf).GetValue($f2)[$t.GetField('sel', $bf).GetValue($f2)].Id -eq 'rain') "opened again: Rain is chosen"
$f2.Dispose()
Check (-not (Test-Path (Join-Path (Split-Path $env:MHFZPLUS_BASE) 'games\meze\mhfz-fps.ini.tmp'))) "no settings file left half-written"
# the session as the launcher's log tells it (lines as MHFZ-Plus.exe writes them: time stamp, two spaces, text)
$sessT = $asm.GetType('MhfzPlus.HubForm+Session')
$log = Join-Path (Split-Path $env:MHFZPLUS_BASE) 'session-test.log'
$f3 = [Activator]::CreateInstance($t, @([single]1, $true))
$sess = [Activator]::CreateInstance($sessT)
$sessT.GetField('Server').SetValue($sess, 'rain'); $sessT.GetField('Log').SetValue($sess, $log)
$t.GetField('session', $bf).SetValue($f3, $sess)
$follow = $t.GetMethod('FollowSession', $bf)
function Feed($text) { Add-Content $log ('01:00:00.000  ' + $text) -Encoding UTF8; [void]$follow.Invoke($f3, @()) }
function S($name) { $sessT.GetField($name).GetValue($sess) }
function ToastTitle { $x = $t.GetField('toast', $bf).GetValue($f3); if ($x) { $x.GetType().GetField('Title').GetValue($x) } }
Feed 'originals in place, started C:\x\rain-server-launcher.exe - press Play as usual'
Check ((S 'Head') -eq 'LAUNCHER OPEN') "session: the launcher open ($(S 'Head'))"
Feed 'launcher check passed -> English 40/40 files, frame-rate mod in (98 ms, game running)'
Check ((S 'Head') -eq 'CHECK PASSED') "session: Rain's check passed ($(S 'Head'))"
Feed 'game started'
Check ((S 'Head') -eq 'IN GAME') "session: in game ($(S 'Head'))"
Feed 'fps: could not copy d3d9.dll into C:\x\Client'
Check ((ToastTitle) -eq 'No frame-rate mod this time') "the frame-rate mod not put in: said ($(ToastTitle))"
Feed 'game crashed (exception c0000005)'
Check ((S 'Head') -eq 'GAME CRASHED' -and (S 'Problem') -and (ToastTitle) -eq 'The game crashed') "a crash: said, with a bug report button ($(S 'Head') / $(ToastTitle))"
Feed 'game closed -> originals restored'
Check ((S 'Head') -eq 'GAME CRASHED') "after a crash, 'game closed' keeps the crash in view ($(S 'Head'))"
$f3.Dispose()
'@
$ps5 = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
foreach ($l in @(& $ps5 -NoProfile -ExecutionPolicy Bypass -File $inside -Exe $Exe 2>&1)) {
    "$l"
    if ("$l" -match '^\s*FAIL' -or "$l" -match 'Exception|error') { $fails++ }
}
if (-not $Keep) { Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue } else { "kept: $t" }
""
if ($fails) { "$fails FAILED" } else { "ALL PASSED" }
exit $fails
