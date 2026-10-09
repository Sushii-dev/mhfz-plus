# Records pairbench's per-tick FullHash (pairs, lerp masks, text decisions, moved screen quads) for every retro save in
# build\user-retro-*, or compares two recordings: an optimisation must give the same hash on every tick.
#   tools\fullhash.ps1 -Out build\fh-new [-Bench build\pairbench.exe]     (records)
#   tools\fullhash.ps1 -A build\fh-base -B build\fh-new                   (compares)
param([string]$Out = "", [string]$A = "", [string]$B = "", [string]$Bench = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if ($A) {
    $tot = 0; $diff = 0
    foreach ($f in Get-ChildItem "$A\*.txt") {
        $other = Join-Path $B $f.Name
        if (-not (Test-Path $other)) { "{0,-22} missing in {1}" -f $f.BaseName, $B; continue }
        $ha = @(Get-Content $f.FullName | Where-Object { $_ -match '^tick .* hash ' })
        $hb = @(Get-Content $other | Where-Object { $_ -match '^tick .* hash ' })
        $d = @(Compare-Object $ha $hb).Count
        $tot += $ha.Count; $diff += $d
        "{0,-22} {1,3} ticks, {2} lines differ" -f $f.BaseName, $ha.Count, $d
    }
    "TOTAL: $tot ticks, $diff lines differ"
    return
}
$exe = if ($Bench) { $Bench } else { "$root\build\pairbench.exe" }
New-Item -ItemType Directory -Force $Out | Out-Null
foreach ($s in Get-ChildItem "$root\build\user-retro-*\*\ticks.seq") {
    $name = "$($s.Directory.Parent.Name)-$($s.Directory.Name)"
    & $exe -seq $s.FullName -hash 2>&1 | ForEach-Object { "$_" } | Set-Content "$Out\$name.txt"
}
Get-ChildItem "$Out\*.txt" | ForEach-Object { "{0,-22} {1}" -f $_.BaseName, (Get-Content $_.FullName | Select-String -SimpleMatch 'average').Line }
