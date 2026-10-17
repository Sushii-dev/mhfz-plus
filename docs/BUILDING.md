# Building

Everything builds on Windows 10/11 from PowerShell. No Visual Studio needed.

## What you need

- **llvm-mingw** with the i686 target ([releases](https://github.com/mstorsjo/llvm-mingw/releases), the
  `ucrt` or `msvcrt` x86_64-hosted zip). Unzip it and either put its `bin` folder on the `PATH` or set
  `MHFZ_CLANG` to its `bin\clang++.exe`. (If [Windhawk](https://windhawk.net/) is installed, its bundled copy is
  used automatically.)
- **Python 3** with Pillow (`pip install pillow`). `tools\mkres.py` draws the icon and writes the version and
  manifest resources.
- For the analysis scripts only: `pip install pefile capstone`.
- The MHFZ Plus app uses the C# compiler of the .NET Framework 4, which is part of Windows.

## Commands

```powershell
.\build.ps1                    # build\d3d9.dll (the mod) and build\MHFZ-Plus.exe (the launcher)
.\build.ps1 -ModOnly           # just the mod
.\build.ps1 -Tests             # the test programs: build\testapp.exe, build\regrouptest.exe, ...
.\build.ps1 -Bench             # build\pairbench.exe, the offline pairing bench
.\build.ps1 -Profile           # build\prof\d3d9.dll with the sampling profiler, plus a link map
.\tools\buildsettings.ps1      # build\MHFZ-Plus-App.exe (the MHFZ Plus app; tools\apptest.ps1 tests it)
.\tools\package.ps1            # dist\MHFZ-Plus-<version>.zip (with dist-work\english, see below)
.\tools\package.ps1 -Public    # dist\public\MHFZ-Plus-<version>.zip, the GitHub release (the same contents)
```

The mod is one translation unit (`src/fps.cpp`) plus the proxy (`src/proxy.cpp`, `src/d3d9.def`). It's built
32-bit (the game is 32-bit), statically linked, without exceptions or RTTI.

`build\d3d9.map` (the link map) lets `tools\wdsym.py` turn the watchdog's `d3d9.dll+0x...` addresses from a
player's log into function names. Keep the map of every build you hand out.

## Trying a build

- **Test app:** copy `build\d3d9.dll` next to `build\testapp.exe` (the build folder already has it), add a
  `mhfz-fps.ini` and run `testapp.exe 5`. See [TESTING.md](TESTING.md) for the scenes.
- **In the game:** close the game, put `d3d9.dll` and `mhfz-fps.ini` in the game folder (MezeLounge: next to
  `mhf.exe`; Rain: in `Client`), and start the game. `mhfz-fps.log` appears next to it. Check your server's rules
  first ([LEGAL.md](LEGAL.md#servers)).
- **With DXVK:** set `d3d9_backend=<path to DXVK's 32-bit d3d9.dll>` in `mhfz-fps.ini`.

## DXVK

The package includes DXVK's 32-bit `d3d9.dll` (the "Graphics backend: DXVK" option). Download the
[DXVK 3.1.1 release](https://github.com/doitsujin/dxvk/releases/tag/v3.1.1) and put its `x32\d3d9.dll` in
`third_party\dxvk-3.1.1\x32\`. Its license text is in `package\fps\dxvk\LICENSE.txt`.

## The English patch data

`tools\package.ps1` expects the translation data in `dist-work\english\<server>\` (a `manifest.txt` and one
`.delta` per file, made with `tools\mkdelta.py` from original and translated game files). That data isn't in this
repository (the release download has it); see [LEGAL.md](LEGAL.md#the-english-translation). Without it, the package
has no English and says so in its README.
