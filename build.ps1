# Builds build\d3d9.dll (the frame-rate mod, 32-bit) and build\MHFZ-Plus.exe (the launcher) with the llvm-mingw
# toolchain that ships with Windhawk. Resources (icon, version, manifest) come from tools\mkres.py.
#   -Profile   build\prof\d3d9.dll with the sampling profiler (FPS_SAMPLER) and a link map, for tools\prof.py
#   -Bench     build\pairbench.exe, the offline pairing bench (tools\pairbench.cpp); with -Profile: build\prof\pairbench.exe
#   -Tests     the Direct3D 9 test programs (test\*.cpp: testapp.exe, regrouptest.exe, ...) into build\ (or -TestsOut)
param([switch]$ModOnly, [switch]$Profile, [switch]$Bench, [switch]$Tests, [string]$TestsOut = "")
$ErrorActionPreference = "Stop"
# the compiler: $env:MHFZ_CLANG, else Windhawk's copy of llvm-mingw, else llvm-mingw's clang++ on the PATH
$cc = if ($env:MHFZ_CLANG) { $env:MHFZ_CLANG } elseif (Test-Path "C:\Program Files\Windhawk\Compiler\bin\clang++.exe") {
    "C:\Program Files\Windhawk\Compiler\bin\clang++.exe" } else { (Get-Command clang++ -ErrorAction Stop).Source }
$root = $PSScriptRoot
New-Item -ItemType Directory -Force "$root\build" | Out-Null
if ($Tests) {
    $dir = if ($TestsOut) { $TestsOut } else { "$root\build" }
    New-Item -ItemType Directory -Force $dir | Out-Null
    foreach ($src in Get-ChildItem "$root\test\*.cpp") {
        if ($src.BaseName -like '*backend') {  # (stand-in d3d9_backend DLLs, see the files)
            & $cc --target=i686-w64-mingw32 -O2 -shared -static -s "-Wl,--kill-at" -o "$dir\$($src.BaseName).dll" $src.FullName
            if ($LASTEXITCODE -ne 0) { throw "$($src.Name): build failed" }
            continue
        }
        & $cc --target=i686-w64-mingw32 -O2 -std=c++20 -static -s -Wall -Wno-unused-function -Wno-format `
            -o "$dir\$($src.BaseName).exe" $src.FullName -ld3d9 -ldinput8 -ldxguid -lwinmm -luser32 -lgdi32
        if ($LASTEXITCODE -ne 0) { throw "$($src.Name): build failed" }
    }
    Get-ChildItem "$dir\*.exe" | Where-Object { Test-Path "$root\test\$($_.BaseName).cpp" } | Select-Object Name, Length, LastWriteTime
    return
}
if ($Bench) {
    $out = if ($Profile) { "$root\build\prof\pairbench.exe" } else { "$root\build\pairbench.exe" }
    [string[]]$extra = if ($Profile) { @("-fno-omit-frame-pointer", "-DFPS_SAMPLER", "-Wl,-Map=$root\build\prof\pairbench.map") } else { @("-s") }
    New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
    & $cc --target=i686-w64-mingw32 -O2 -std=c++20 -static -Wall -Wno-unused-function -Wno-format -fno-exceptions -fno-rtti @extra `
        -o $out "$root\tools\pairbench.cpp" -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw "bench build failed" }
    Get-Item $out | Select-Object Name, Length, LastWriteTime
    return
}
if ($Profile) {
    New-Item -ItemType Directory -Force "$root\build\prof" | Out-Null
    & $cc --target=i686-w64-mingw32 -O2 -std=c++20 -shared -static -Wall -Wno-unused-function -Wno-format `
        -fno-exceptions -fno-rtti -fno-omit-frame-pointer -DFPS_SAMPLER "-Wl,-Map=$root\build\prof\d3d9.map" `
        -o "$root\build\prof\d3d9.dll" "$root\src\proxy.cpp" "$root\src\fps.cpp" "$root\src\d3d9.def" -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw "profile build failed" }
    Copy-Item "$root\build\testapp.exe" "$root\build\prof\" -Force
    Get-Item "$root\build\prof\d3d9.dll" | Select-Object Name, Length, LastWriteTime
    return
}
if (-not (Test-Path "$root\build\fps.res") -or (Get-Item "$root\tools\mkres.py").LastWriteTime -gt (Get-Item "$root\build\fps.res").LastWriteTime) {
    python "$root\tools\mkres.py"
    if ($LASTEXITCODE -ne 0) { throw "mkres failed" }
}
& $cc --target=i686-w64-mingw32 -O2 -std=c++20 -shared -static -s -Wall -Wno-unused-function -Wno-format `
    -fno-exceptions -fno-rtti "-Wl,-Map=$root\build\d3d9.map" `
    -o "$root\build\d3d9.dll" "$root\src\proxy.cpp" "$root\src\fps.cpp" "$root\src\d3d9.def" "$root\build\fps.res" `
    -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Get-Item "$root\build\d3d9.dll" | Select-Object Name, Length, LastWriteTime
if ($ModOnly) { return }
& $cc --target=i686-w64-mingw32 -O2 -std=c++20 -static -s -mwindows -municode -Wall -Wno-unused-function `
    -o "$root\build\MHFZ-Plus.exe" "$root\launcher\launcher.cpp" "$root\build\launcher.res" -lbcrypt -lshlwapi -luser32 -lshell32
if ($LASTEXITCODE -ne 0) { throw "launcher build failed" }
Get-Item "$root\build\MHFZ-Plus.exe" | Select-Object Name, Length, LastWriteTime
