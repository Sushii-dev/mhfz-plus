# Builds the shareable package: dist\MHFZ-Plus-<version>\ and dist\MHFZ-Plus-<version>.zip
#   Install.cmd, README.txt, setup\*.ps1, payload\(MHFZ-Plus.exe, MHFZ-Plus-App.exe, ui\, version.txt, fps\, english\)
# Usage: tools\package.ps1 [-NoBuild] [-Public]
#   -Public   the GitHub release: no English data (derived from the game's text) -> dist\public\MHFZ-Plus-<version>.zip
param([switch]$NoBuild, [string]$Version = "1.4.3", [switch]$Public)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
# the scripts that ship: no line that only fails when it runs (an `else` cut off from its `if` broke setup in 1.4.0/1.4.1)
& "$root\tools\scriptcheck.ps1" -Paths "$root\package" | Write-Host
if ($LASTEXITCODE) { throw "scriptcheck found problems in package\ (see above): nothing packaged" }
if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$root\build.ps1" | Out-Null; if ($LASTEXITCODE) { throw "build failed" }
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$root\tools\buildsettings.ps1" | Out-Null; if ($LASTEXITCODE) { throw "app build failed" }
}
$name = "MHFZ-Plus-$Version"
$dist = if ($Public) { Join-Path $root "dist\public" } else { Join-Path $root "dist" }
$out = Join-Path $dist $name
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force "$out\setup", "$out\payload\fps" | Out-Null

Copy-Item "$root\package\Install.cmd", "$root\package\README.txt" $out
Copy-Item "$root\LICENSE" "$out\LICENSE.txt"; Copy-Item "$root\LICENSE" "$out\payload\LICENSE.txt"   # (MIT: the notice travels with every copy)
Copy-Item "$root\package\setup\Install.ps1", "$root\package\setup\Uninstall.ps1", "$root\package\setup\SteamShortcuts.ps1", "$root\package\setup\Report.ps1" "$out\setup"
# Linux: install.sh / uninstall.sh and the setup script (there MHFZ Plus goes into the Wine prefix the game runs in)
New-Item -ItemType Directory -Force "$out\setup\linux" | Out-Null
Copy-Item "$root\package\install.sh", "$root\package\uninstall.sh" $out
Copy-Item "$root\package\setup\linux\mhfzplus.py", "$root\build\mhfz-plus.png" "$out\setup\linux"
Copy-Item "$root\build\MHFZ-Plus.exe", "$root\build\MHFZ-Plus-App.exe" "$out\payload"
# the app's artwork (and its font, when there is one)
New-Item -ItemType Directory -Force "$out\payload\ui" | Out-Null
Copy-Item "$root\package\ui\*" "$out\payload\ui" -Recurse
Set-Content "$out\payload\version.txt" $Version
Copy-Item "$root\package\README.txt" "$out\payload"
Copy-Item "$root\build\d3d9.dll", "$root\package\fps\mhfz-fps.ini" "$out\payload\fps"
# DXVK 3.1.1 (32-bit d3d9.dll, unmodified; zlib license) for the "Graphics backend: DXVK" option
New-Item -ItemType Directory -Force "$out\payload\fps\dxvk" | Out-Null
Copy-Item "$root\third_party\dxvk-3.1.1\x32\d3d9.dll", "$root\package\fps\dxvk\LICENSE.txt", "$root\package\fps\dxvk\README.txt" "$out\payload\fps\dxvk"
if (-not $Public) { New-Item -ItemType Directory -Force "$out\payload\english" | Out-Null; Copy-Item "$root\dist-work\english\*" "$out\payload\english" -Recurse }
else {
    $note = "This download doesn't include the English translation (it is made from the game's own text, so it isn't`r`n" +
            "published). Everything about it below doesn't apply; setup offers only the frame-rate mod.`r`n`r`n"
    foreach ($f in "$out\README.txt", "$out\payload\README.txt") { $x = [IO.File]::ReadAllText($f); $i = $x.IndexOf("`n", $x.IndexOf("====")) + 1; [IO.File]::WriteAllText($f, $x.Substring(0, $i) + "`r`n" + $note + $x.Substring($i).TrimStart("`r", "`n")) }
}

# sanity: every delta named in the manifests is there, and the scripts parse in Windows PowerShell 5.1
foreach ($s in $(if ($Public) { @() } else { 'meze', 'rain' })) {
    foreach ($line in Get-Content "$out\payload\english\$s\manifest.txt") {
        if (-not $line -or $line.StartsWith('#')) { continue }
        $d = ($line -split '\|')[5]
        if (-not (Test-Path "$out\payload\english\$s\$d")) { throw "missing delta $s\$d" }
    }
}
$check = Join-Path $env:TEMP "mhfz-parsecheck.ps1"
Set-Content $check 'param($dir) foreach ($f in Get-ChildItem "$dir\*.ps1") { $e = $null; [void][System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$null, [ref]$e); if ($e) { "$($f.Name): $($e[0])"; exit 1 } }'
& powershell -NoProfile -ExecutionPolicy Bypass -File $check "$out\setup"
if ($LASTEXITCODE) { throw "script parse error" }
& python -I -c "import ast, sys; ast.parse(open(sys.argv[1], encoding='utf-8').read())" "$out\setup\linux\mhfzplus.py"
if ($LASTEXITCODE) { throw "mhfzplus.py parse error" }
# CRLF line endings for the text files people open in Notepad; LF for the Linux scripts
foreach ($f in Get-ChildItem $out -Recurse -Include *.txt, *.cmd, *.ps1, *.ini) {
    $t = [IO.File]::ReadAllText($f.FullName) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText($f.FullName, $t, (New-Object Text.UTF8Encoding $false))
}
foreach ($f in Get-ChildItem $out -Recurse -Include *.sh, *.py) {
    $t = [IO.File]::ReadAllText($f.FullName) -replace "`r`n", "`n"
    [IO.File]::WriteAllText($f.FullName, $t, (New-Object Text.UTF8Encoding $false))
}
$zip = Join-Path $dist "$name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($out, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
# entries marked as made on Unix with file modes, so unzip on Linux keeps install.sh, uninstall.sh and mhfzplus.py
# executable (Windows' own extraction ignores this); names with / (Windows PowerShell 5.1's zip writer uses \, which
# Linux takes as part of a file name - and the in-app update refuses)
$b = [IO.File]::ReadAllBytes($zip)
$e = $b.Length - 22
while ($e -ge 0 -and [BitConverter]::ToUInt32($b, $e) -ne 0x06054b50) { $e-- }
if ($e -lt 0) { throw "zip: no end of central directory" }
$p = [BitConverter]::ToUInt32($b, $e + 16)
for ($i = 0; $i -lt [BitConverter]::ToUInt16($b, $e + 10); $i++) {
    if ([BitConverter]::ToUInt32($b, $p) -ne 0x02014b50) { throw "zip: bad central directory entry $i" }
    $nlen = [BitConverter]::ToUInt16($b, $p + 28)
    $lh = [BitConverter]::ToUInt32($b, $p + 42)   # (its local header: the name again, same length)
    if ([BitConverter]::ToUInt32($b, $lh) -ne 0x04034b50 -or [BitConverter]::ToUInt16($b, $lh + 26) -ne $nlen) { throw "zip: bad local header $i" }
    for ($k = 0; $k -lt $nlen; $k++) {
        if ($b[$p + 46 + $k] -eq 0x5C) { $b[$p + 46 + $k] = 0x2F }
        if ($b[$lh + 30 + $k] -eq 0x5C) { $b[$lh + 30 + $k] = 0x2F }
    }
    $entry = [Text.Encoding]::UTF8.GetString($b, $p + 46, $nlen)
    $mode = if ($entry.EndsWith('/')) { 0x41ED } elseif ($entry -match '\.(sh|py)$') { 0x81ED } else { 0x81A4 }   # 040755 / 100755 / 100644
    $b[$p + 4] = 20; $b[$p + 5] = 3   # version made by: 2.0 on Unix
    [BitConverter]::GetBytes([uint32]([uint64]$mode * 65536 + ([BitConverter]::ToUInt32($b, $p + 38) -band 0xFFFF))).CopyTo($b, $p + 38)
    $p += 46 + $nlen + [BitConverter]::ToUInt16($b, $p + 30) + [BitConverter]::ToUInt16($b, $p + 32)
}
[IO.File]::WriteAllBytes($zip, $b)
$files = Get-ChildItem $out -Recurse -File
"{0}: {1} files, {2:N1} MB -> {3} ({4:N1} MB)" -f $name, $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB), $zip, ((Get-Item $zip).Length / 1MB)
