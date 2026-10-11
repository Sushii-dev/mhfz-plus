# Testing

The game can't run in an automated test, so most testing happens against a small Direct3D 9 program that mimics
MHF-Z's frame structure, and against recordings of real game frames. Every hard case found in the game gets a scene
here first, then a fix.

## The gate: `tools\validate.ps1`

```powershell
.\tools\validate.ps1 -Dll build\d3d9.new.dll -Ref build\d3d9.previous.dll
```

Runs everything below that matters for a release (about 25 minutes) and prints one line per check:

| Check | Pass means |
|---|---|
| DXVK starts (`starthang.ps1`) | 0 hangs in 200 short starts with DXVK as the backend |
| Pairing hash (`pairhash.ps1`) | the same pairing on every tick of every scene as the reference, or a difference you can explain |
| Exit test (`exittest.ps1`) | the process exits cleanly every time |
| Regroup test (`regrouptest.ps1`) | background pairing and the regroup options give bit-identical frames, on Windows' d3d9 and on DXVK |
| Self-check (`selfcheckbench.ps1`) | the interpolation error per scene is no worse than the reference |
| Stress (`stress.ps1`) | every scene under eight option sets: no exceptions, no hangs, statistics keep coming |

## The test app

`build\testapp.exe <seconds> [billboards] [swap] [tag] [label] [menu] [skip] [ramp] [live] [heavy]`. Each flag
(0/1) adds one of the cases the game produced:

| Argument | Case |
|---|---|
| billboards | camera-facing foliage cards re-sorted by distance every frame |
| swap | one copy of a mesh leaves view while another enters (floating tree canopies) |
| tag | 4x4 name tags with identical glyphs |
| label (1, 2) | a sliding text label; 2: two letters also trade atlas slots every 7 ticks |
| menu | static menu text whose glyph atlas reshuffles |
| skip | objects set up and then not drawn (leftover constant uploads) |
| ramp | values that ramp per tick (fog noise scrolling) |
| live | uploads read by many later draws (shadow cascade lookups) |
| heavy | many draws, for timing |

`tools\regress.ps1 [dll]` runs the image-based regressions (ghosts, fog card, static text, moving label, tint, ramp)
and prints the key numbers.

## Recordings of the real game

The clip recorder (`retro=1`, saved with the clip key) writes the last second of frames plus `dump.txt` (the next
frame pair with its pairing). `tools\pairbench.cpp` replays the pairing from a recorded sequence offline, so a change
can be checked against the exact game frames that showed a glitch:

```powershell
.\build.ps1 -Bench
.\build\pairbench.exe <ticks.seq> -drawinfo T ...   # see the options at the top of tools\pairbench.cpp
```

The Python scripts in `tools\` read dumps and clips (`ghosts.py`, `flickerscan.py`, `pairjumps.py`, `drawat.py`,
`brightbias.py`, ...). Each one starts with a description of what it measures.

## In the game

- `selfcheck=1` predicts a real frame from its neighbours and logs the error. With `selfcheck_saves`, bad cases are
  saved with an ablation (which mechanism made it worse).
- `stats_interval` writes a statistics line to `mhfz-fps.log`: real and shown fps, late ticks, matching rates, timings.
- With developer keys on (`debug_keys=1`): Ctrl+Shift+1–5 switch single mechanisms on and off live, for A/B tests.

## The MHFZ Plus app

- `tools\apptest.ps1` (no window, any time): on a made-up install in `%TEMP%` with fake game folders, renders every
  page and moments of the animations off-screen (`--shots`) and times a frame (under 40 ms at 150%), then checks
  inside the app: the icon in every size, the chosen server remembered (and Settings following it), only web pages
  opened, the update check offering only this project's release pages.

## Launcher and installer

- `launcher\simtest.ps1` and `simtest2.ps1` run the launcher against simulated MezeLounge and Rain installs
  (`launcher\fakegame.cpp`): patching, game updates, stale caches, restore. `simtest.ps1 -Crash`: the games end with
  an access violation's exit code (the log must say "game crashed", the originals still come back);
  `-OtherClient`: another program called client.exe runs from elsewhere the whole time (it must be "left out", and
  the session must still end with the real game).
- `tools\installtest.ps1` installs the release zip as a player would (into a temporary folder, with a copy of the
  Steam configuration), plays both simulated servers, reinstalls and uninstalls; it also checks that a Steam
  shortcuts file of a layout we don't know is refused and left byte for byte as it was.
- `tools/linuxtest.sh` does the same on Linux (or in WSL), with Wine and every window in a virtual screen: `install.sh`
  from the unzipped release against a simulated Steam (Proton) setup and a Lutris / plain Wine setup, an update
  install, both servers played under Wine, the frame-rate mod with the test app on DXVK and on Wine's own Direct3D,
  restore and uninstall (the prefixes' registry and Steam's files must end up as they were). Needs `wine` with 32-bit
  support, `xvfb-run`, `python3` and `unzip`; 32-bit Vulkan drivers (Mesa's lavapipe will do) for the DXVK part.
