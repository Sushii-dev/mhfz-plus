# Checks that the offline bench (build\pairbench.exe) reproduces the game's pairing exactly: runs the test app's
# town-sized scene with debug_pairhash=1, triggers pairing captures through the ini, then runs the bench on them and
# compares every capture's hash with the "pairhash" log line of the same tick.
$ErrorActionPreference = "Stop"
$b = Join-Path (Split-Path $PSScriptRoot) "build"
$caps = Join-Path $b "paircap"
if (Test-Path $caps) { Get-ChildItem $caps -Filter *.bin | ForEach-Object { [IO.File]::Delete($_.FullName) } }
$base = "[fps]`r`nmode=2`r`ntarget_fps=170`r`npresent_interval=0`r`nstats_interval=0`r`nselfcheck=0`r`nretro=0`r`ndebug_pairhash=1`r`n"
Set-Content "$b\mhfz-fps.ini" ($base + "paircap_trigger=0`r`n")
$p = Start-Process "$b\testapp.exe" -ArgumentList '12', '0', '0', '0', '0', '0', '0', '0', '0', '1' -WorkingDirectory $b -PassThru
Start-Sleep 3
Set-Content "$b\mhfz-fps.ini" ($base + "paircap_trigger=1`r`n")
$p.WaitForExit()
$log = Get-Content "$b\mhfz-fps.log"
$log | Select-String 'paircap|remote' | ForEach-Object { $_.Line }
$hashes = @{}
foreach ($l in $log) { if ($l -match 'pairhash (\d+) (\w+)') { $hashes[[int]$Matches[1]] = $Matches[2] } }
$out = & "$b\pairbench.exe" -n 10 @(Get-ChildItem "$caps\*.bin" | ForEach-Object FullName)
$out
$bad = 0
foreach ($l in $out) {
    if ($l -match 'cap_(\d+)\.bin.*hash (\w+)') {
        $tick = [int]$Matches[1]
        $ok = $hashes[$tick] -eq $Matches[2]
        if (-not $ok) { $bad++ }
        "tick {0}: game {1}  bench {2}  {3}" -f $tick, $hashes[$tick], $Matches[2], $(if ($ok) { 'MATCH' } else { 'DIFFERENT' })
    }
}
"=== $bad mismatch(es)"
