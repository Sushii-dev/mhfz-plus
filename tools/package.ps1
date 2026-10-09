# Builds the shareable package: dist\MHFZ-Plus-<version>\ and dist\MHFZ-Plus-<version>.zip
#   Install.cmd, README.txt, setup\*.ps1, payload\(MHFZ-Plus.exe, MHFZ-Plus-Settings.exe, version.txt, fps\, english\)
# Usage: tools\package.ps1 [-NoBuild] [-Public]
#   -Public   the GitHub release: no English data (derived from the game's text) -> dist\public\MHFZ-Plus-<version>.zip
param([switch]$NoBuild, [string]$Version = "1.3.1", [switch]$Public)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$root\build.ps1" | Out-Null; if ($LASTEXITCODE) { throw "build failed" }
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$root\tools\buildsettings.ps1" | Out-Null; if ($LASTEXITCODE) { throw "settings build failed" }
}
$name = "MHFZ-Plus-$Version"
$dist = if ($Public) { Join-Path $root "dist\public" } else { Join-Path $root "dist" }
$out = Join-Path $dist $name
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force "$out\setup", "$out\payload\fps" | Out-Null

Copy-Item "$root\package\Install.cmd", "$root\package\README.txt" $out
Copy-Item "$root\LICENSE" "$out\LICENSE.txt"; Copy-Item "$root\LICENSE" "$out\payload\LICENSE.txt"   # (MIT: the notice travels with every copy)
Copy-Item "$root\package\setup\Install.ps1", "$root\package\setup\Uninstall.ps1", "$root\package\setup\SteamShortcuts.ps1" "$out\setup"
Copy-Item "$root\build\MHFZ-Plus.exe", "$root\build\MHFZ-Plus-Settings.exe" "$out\payload"
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
# CRLF line endings for the text files people open in Notepad
foreach ($f in Get-ChildItem $out -Recurse -Include *.txt, *.cmd, *.ps1, *.ini) {
    $t = [IO.File]::ReadAllText($f.FullName) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText($f.FullName, $t, (New-Object Text.UTF8Encoding $false))
}
$zip = Join-Path $dist "$name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal
$files = Get-ChildItem $out -Recurse -File
"{0}: {1} files, {2:N1} MB -> {3} ({4:N1} MB)" -f $name, $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB), $zip, ((Get-Item $zip).Length / 1MB)
