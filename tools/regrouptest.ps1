# Runs test\regrouptest with regroup=0/1/2 (Windows' d3d9 and DXVK) and compares the frame hashes bit for bit.
# The build folder's mhfz-fps.ini is replaced for the runs and put back afterwards.
param([int]$Frames = 40, [int]$SleepMs = 25, [string[]]$Backends = @('', 'dxvk\d3d9.dll'))
$ErrorActionPreference = 'Stop'
$b = Join-Path $PSScriptRoot '..\build' | Resolve-Path
$ini = Join-Path $b 'mhfz-fps.ini'
$keep = Join-Path $b 'mhfz-fps.ini.keep'
Copy-Item -LiteralPath $ini $keep -Force
$fail = 0
try {
    foreach ($be in $Backends) {
        $runs = @{}
        foreach ($rg in 0, 1, 2) {
            $lines = @('[fps]', 'mode=2', 'target_fps=170', 'present_interval=0', 'stats_interval=2', "regroup=$rg", 'regroup_check=1')
            if ($be) { $lines += "d3d9_backend=$be" }
            Set-Content -LiteralPath $ini -Value $lines
            $all = & (Join-Path $b 'regrouptest.exe') $Frames $SleepMs 2>&1 | ForEach-Object { "$_" }
            $runs[$rg] = @($all | Where-Object { $_ -notmatch 'state reads' })
            $reads = $all | Where-Object { $_ -match 'state reads' }
            $log = Get-Content -LiteralPath (Join-Path $b 'mhfz-fps.log')
            $name = if ($be) { 'DXVK' } else { 'Windows d3d9' }
            "--- $name, regroup=$rg ($reads)"
            if ($reads -notmatch 'wrong: 0$') { $fail++ }
            $log | Select-String 'regroup' | Select-Object -Last 4 | ForEach-Object { '  ' + $_.Line }
            $log | Select-String 'EXCEPTION' | Select-Object -First 3 | ForEach-Object { '  ' + $_.Line }
        }
        # matching in the background (at EndScene) must give the same pairs as at Sleep
        $ph = @{}
        foreach ($pa in 0, 1) {
            $lines = @('[fps]', 'mode=2', 'target_fps=170', 'present_interval=0', 'regroup=2', 'debug_pairhash=1', 'stats_interval=1', "pair_async=$pa")
            if ($be) { $lines += "d3d9_backend=$be" }
            Set-Content -LiteralPath $ini -Value $lines
            & (Join-Path $b 'regrouptest.exe') $Frames $SleepMs *> $null
            $h = @{}
            Get-Content -LiteralPath (Join-Path $b 'mhfz-fps.log') | Select-String 'pairhash (\d+) (\S+)' | ForEach-Object { $h[$_.Matches[0].Groups[1].Value] = $_.Matches[0].Groups[2].Value }
            $ph[$pa] = $h
            if ($pa -eq 1) { Get-Content -LiteralPath (Join-Path $b 'mhfz-fps.log') | Select-String 'matching in the background' | Select-Object -Last 1 | ForEach-Object { '  ' + ($_.Line -replace '^.*\| game frame at', 'game frame at') } }
        }
        $same = 0; $differ = 0
        foreach ($k in $ph[0].Keys) { if ($ph[1].ContainsKey($k)) { if ($ph[0][$k] -eq $ph[1][$k]) { $same++ } else { $differ++ } } }
        "  pairing in the background vs not: $same ticks identical, $differ different"
        if ($differ -or -not $same) { $fail++ }
        foreach ($rg in 1, 2) {
            $diff = 0
            for ($i = 0; $i -lt $Frames; $i++) { if ($runs[0][$i] -ne $runs[$rg][$i]) { $diff++ } }
            $res = if ($diff) { "DIFFERENT in $diff of $Frames frames" } else { "identical ($Frames frames)" }
            "  regroup=$rg vs regroup=0: $res"
            if ($diff) { $fail++ }
        }
    }
} finally {
    Move-Item -LiteralPath $keep $ini -Force
}
if ($fail) { 'FAILED' } else { 'all identical' }
