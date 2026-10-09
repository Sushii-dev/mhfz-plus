# Edge cases for MHFZ-Plus.exe, run after launcher\simtest.ps1 (uses its sim\ folder):
#   update-rain   the Rain launcher brings a new version of a file: it must never be overwritten
#   update-meze   MezeLounge's login writes a new version of a file: left alone after a few seconds
#   stale-cache   a damaged English file in the cache is rebuilt
#   kit-state     English files from the old kit (other timestamps) are recognised, no endless re-copying
#   restore-all   --restore all puts every original back and removes the frame-rate mod
param([string[]]$Only = @())
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$sim = Join-Path $root "sim"
$plus = "$sim\plus"
$kit = "$env:MHFZ_KIT"
$fails = 0
function Check($cond, $what) { if ($cond) { "  PASS $what" } else { "  FAIL $what"; $script:fails++ } }
function Run($s, $extra = @()) {
    Remove-Item "$sim\report.txt" -ErrorAction SilentlyContinue
    $p = Start-Process "$plus\MHFZ-Plus.exe" -ArgumentList (@($s) + $extra) -PassThru
    if (-not $p.WaitForExit(240000)) { $p.Kill(); "  launcher hung" }
    $script:log = Get-Content "$plus\MHFZ-Plus-$s.log"
    $script:report = if (Test-Path "$sim\report.txt") { Get-Content "$sim\report.txt" } else { @() }
    $script:log | ForEach-Object { "    log: $_" }
    $script:report | ForEach-Object { "    sim: $_" }
}
function Hash($p) { (Get-FileHash $p -Algorithm SHA256).Hash }
function Want($n) { -not $Only -or $Only -contains $n }

if (Want 'update-rain') {
    "=== update-rain"
    $rel = 'dat\mhfinf.bin'
    $orig = "$sim\pristine-rain\$rel"
    Copy-Item $orig "$sim\pristine-rain-keep.bin" -Force
    $b = [IO.File]::ReadAllBytes($orig); $b[100] = $b[100] -bxor 0x55; [IO.File]::WriteAllBytes($orig, $b)   # the server's new version
    Copy-Item $orig "$sim\rain\Client\$rel" -Force                                                           # downloaded by the launcher
    $new = Hash $orig
    try {
        Run 'rain'
        Check ((Hash "$sim\rain\Client\$rel") -eq $new) 'updated file untouched after the session'
        Check ($report -match 'check passed') 'launcher check passed'
        Check ($log -match 'mhfinf.bin is a version we don''t know') 'logged as unknown version'
        Check ($report -match 'GET_TXT: 39 English') 'other 39 files English in game'
    } finally {
        Copy-Item "$sim\pristine-rain-keep.bin" $orig -Force
        Copy-Item $orig "$sim\rain\Client\$rel" -Force
    }
}

if (Want 'update-meze') {
    "=== update-meze"
    $rel = 'dat\mhfpac.bin'
    $orig = "$sim\pristine-meze\$rel"
    Copy-Item $orig "$sim\pristine-meze-keep.bin" -Force
    $b = [IO.File]::ReadAllBytes($orig); $b[200] = $b[200] -bxor 0x55; [IO.File]::WriteAllBytes($orig, $b)   # login brings this version
    $new = Hash $orig
    try {
        Run 'meze'
        Check ((Hash "$sim\meze\$rel") -eq $new) 'new version left in place'
        Check ($log -match 'mhfpac.bin is a version we don''t know') 'logged as unknown version'
        Check ($report -match 'in game: 40 English') 'other 40 files English in game'
        Check (@($log -match 'launcher replaced').Count -le 3) "no endless re-copying ($(@($log -match 'launcher replaced').Count) replace lines)"
    } finally {
        Copy-Item "$sim\pristine-meze-keep.bin" $orig -Force
        Copy-Item "$sim\english-meze\$rel" "$sim\meze\$rel" -Force
    }
}

if (Want 'stale-cache') {
    "=== stale-cache"
    $c = "$plus\cache\meze\dat\mhfgao.bin"
    $b = [IO.File]::ReadAllBytes($c); $b[10] = $b[10] -bxor 1; [IO.File]::WriteAllBytes($c, $b)
    Run 'meze'
    Check ($log -match 'files ready \(1 built now\)') 'damaged cache file rebuilt'
    Check ((Hash $c) -eq (Hash "$sim\english-meze\dat\mhfgao.bin")) 'cache file correct again'
    Check ($report -match 'in game: 41 English') 'all 41 English in game'
}

if (Want 'kit-state') {
    "=== kit-state"
    # English files put in by the old translation kit: same content, other timestamps
    Get-ChildItem "$sim\english-meze" -Recurse -File | ForEach-Object {
        $d = Join-Path "$sim\meze" $_.FullName.Substring("$sim\english-meze".Length + 1)
        [IO.File]::WriteAllBytes($d, [IO.File]::ReadAllBytes($_.FullName))
    }
    Run 'meze'
    Check ($log -match 'english: 41/41 files in place') 'kit files recognised as English'
    Check (@($log -match 'launcher replaced').Count -le 2) "no endless re-copying ($(@($log -match 'launcher replaced').Count) replace lines)"
    Check ($report -match 'start: 41 English') 'English at start'
}

if (Want 'safe-mode') {
    "=== safe-mode"
    $ini = "$plus\MHFZ-Plus.ini"
    $keep = Get-Content $ini -Raw
    # start from a full English session's leftovers: all 41 English in the game folder
    Get-ChildItem "$sim\english-meze" -Recurse -File | ForEach-Object { Copy-Item $_.FullName (Join-Path "$sim\meze" $_.FullName.Substring("$sim\english-meze".Length + 1)) -Force }
    Set-Content $ini ($keep -replace '(?ms)(\[meze\].*?)english=1', '$1english=safe') -NoNewline
    try {
        Run 'meze'
        Check ($log -match 'english: 36 files ready') '36 files in the safe set'
        Check (@($log -match 'not used this time').Count -eq 5) '5 leftover English files put back'
        Check ($report -match 'in game: 36 English') '36 English in game'
        Check ((Hash "$sim\meze\dat\mhfgao.bin") -eq (Hash "$sim\pristine-meze\dat\mhfgao.bin")) 'mhfgao.bin original'
    } finally { Set-Content $ini $keep -NoNewline }
}

if (Want 'restore-all') {
    "=== restore-all"
    Copy-Item "$plus\fps\d3d9.dll" "$sim\rain\Client\d3d9.dll" -Force
    $p = Start-Process "$plus\MHFZ-Plus.exe" -ArgumentList 'all', '--restore' -PassThru -Wait
    Get-Content "$plus\MHFZ-Plus-restore.log" | ForEach-Object { "    log: $_" }
    $bad = 0
    foreach ($s in 'meze', 'rain') {
        $game = if ($s -eq 'meze') { "$sim\meze" } else { "$sim\rain\Client" }
        Get-ChildItem "$sim\pristine-$s" -Recurse -File | ForEach-Object {
            $g = Join-Path $game $_.FullName.Substring("$sim\pristine-$s".Length + 1)
            if ((Hash $g) -ne (Hash $_.FullName)) { $bad++ }
        }
        Check (-not (Test-Path "$game\d3d9.dll")) "$s d3d9.dll removed"
    }
    Check ($bad -eq 0) "all originals back ($bad wrong)"
    Check ($p.ExitCode -eq 0) "exit code 0"
}
"=== $fails failure(s)"
