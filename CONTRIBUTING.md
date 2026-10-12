# Contributing

Thanks for wanting to help. A few things make a contribution easy to accept.

## Reporting a glitch

The most useful report is a clip:

1. Press the clip key (Delete by default) right after you see the problem. A message confirms it.
2. Zip the clip folder (`retro-N`, see the player README for where it is) together with `mhfz-fps.log`.
3. Open an issue: what you saw, where in the game (town, which area, a menu), your resolution and refresh rate,
   and whether you use DXVK.

For crashes and freezes, `mhfz-fps.log` and `mhfz-fps.prev.log` are enough. A `WATCHDOG` line in the log tells us
where the mod was stuck.

## Code changes

- **The rules don't change:** no added input latency, never delay or slow the game's tick, never patch game code
  or write game memory. A change that breaks one of these won't be merged, however nice the result. The one
  exception is the opt-in orbit camera ([docs/CAMERA.md](docs/CAMERA.md)): it writes the game's camera and nothing
  else. Don't widen what it writes, and don't add other writes.
- **Reproduce first.** If you fix a glitch, add a test-app scene or a recorded sequence that shows it, and show
  the numbers before and after.
- **Run the gate:** `tools\validate.ps1 -Dll <your build> -Ref <the current release's d3d9.dll>`, and say in the
  pull request what it printed. If the pairing hash changes, explain why.
- **New settings** go at the end of the `Config` struct in `src/fps.cpp` (offline tools copy saved configs by
  size), with a comment, a line in `package/fps/mhfz-fps.ini`, and an entry in the app's settings schema
  (`settings/Settings.cs`).
- Match the surrounding style: plain C++, comments that explain why, measurements in comments where a constant
  came from one.

## AI-assisted contributions

Welcome. Say so in the pull request, and say how you tested. See [docs/AI-USAGE.md](docs/AI-USAGE.md).

## Servers and legal

Don't add anything that changes gameplay, reads or writes game memory (beyond the opt-in camera's), talks to
servers, or touches anti-cheat.
The Rain launcher mode (files go in after Rain's own file check) is documented in [docs/LEGAL.md](docs/LEGAL.md);
don't extend that kind of workaround to other checks. Don't commit game files or anything extracted from them.
