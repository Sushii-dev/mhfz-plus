# MHFZ Plus

High frame rate for **Monster Hunter Frontier Z** on community servers, without changing game speed and without
adding input latency. An optional launcher can also apply an English translation.

> Fan project. Not affiliated with, endorsed by or connected to Capcom or any server team. Contains no Capcom code
> and no game files. See [Legal notes](docs/LEGAL.md).
>
> **Made with AI.** Sushii-dev researched, designed, directed and tested the project; an AI coding agent
> (Anthropic's Claude) wrote most of the code and documentation under that direction. Details: [AI usage](docs/AI-USAGE.md).

## Rain players: please read first

MHFZ Plus is **not yet approved by Rain's staff**. A Rain staff member has asked players not to use mods that work
around the anti-cheat until staff have given a green light, and said that anyone who does so is at their own risk.

To be precise about MHFZ Plus: the frame-rate mod doesn't touch the game's anti-cheat. It doesn't patch game code or
write game memory, and it only changes what's drawn on your screen. But on Rain, the MHFZ Plus launcher puts the mod
(and the English files, if chosen) in after Rain's launcher has checked the game files. That steps around Rain's file
check, so we consider it covered by the staff's request.

We've contacted Rain's staff and will follow their decision, including removing Rain support if they ask. Until then,
please don't use MHFZ Plus on Rain unless you accept that risk. MezeLounge is not affected.

Rain's own mod policy: community mods are unsupported, are likely to stop working with the new client, and are used at
your own discretion.

## What it does

MHF-Z runs its game logic at 30 ticks per second and draws one frame per tick. MHFZ Plus keeps that exactly: the game
still ticks at the same moments, reads input at the same moments and sends the server the same things. In the time the
game would otherwise spend sleeping between ticks, MHFZ Plus re-draws the last frame several times with every object,
the camera and the UI moved part of the way towards their next position. You see 60, 120, 144 or 175 frames per
second; the game sees nothing different.

It's a `d3d9.dll` placed next to the game. It hooks Direct3D 9 and the game's frame limiter `Sleep` (plus DirectInput,
if you turn on the optional tap catching). It never patches game code and never writes game memory.

- In-between frames up to your refresh rate. How many you get depends on your monitor and your CPU: see
  [What frame rate to expect](#what-frame-rate-to-expect).
- No added input latency. An optional setting reduces it slightly.
- FPS counter, an on/off key to compare, and a clip recorder: a key saves the last second of frames when something
  looks wrong, for bug reports.
- The MHFZ Plus app: play, every setting, bug reports and help in one window, in Frontier's own style.
- Comes with [DXVK](https://github.com/doitsujin/dxvk) (Direct3D 9 on Vulkan) as a one-click option: it halves the CPU time per
  frame, which in our tests meant 50-70% more frames in towns and crowds. Without Vulkan 1.3 the game falls back to
  Windows' Direct3D 9.

How it works in detail: [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md).

## What frame rate to expect

The game makes 30 real frames a second, one per 33.3 ms tick. MHFZ Plus adds in-between frames in each tick, at most
one per refresh of your monitor (a frame sent faster than the screen refreshes is never fully shown). So:

**frames per second = 30 × (1 real frame + the in-between frames that fit in a tick)**

What decides how many fit is the time left in each tick after the work at its start: the game's own work, drawing its
real frame, and matching that frame to the previous one. That work grows with how much is on screen and is your CPU's
part. Your monitor's part is the refresh interval: the in-between frames have to fit into what's left of the tick.

| Monitor | Most frames | Needs the work per tick under | Otherwise (fps on those ticks) |
|---|---|---|---|
| 60 Hz | 60 | ~16.7 ms | 30 |
| 120 Hz | 120 | ~7.5 ms | 90 (under ~16 ms) |
| 144 Hz | 120 | ~12 ms | 90 (under ~19 ms) |
| 165 Hz | 150 | ~8.3 ms | 120 (under ~14.5 ms) |
| 175 Hz | 150 | ~9.8 ms | 120 (under ~15.5 ms) |
| 240 Hz | 210 | ~7.5 ms | 180 (under ~12 ms), 150 (under ~16 ms) |

The fps counter averages over many ticks, so a mix shows up in between: 105 on a 144 Hz monitor means about half
the ticks got 3 in-between frames and half got 2. Very light scenes can fit one frame more than the table shows.

An example from a tester (i7-8700K, RTX 5070 Ti, 144 Hz, DXVK): about 8 ms of work per tick in a normal town, 12–14 ms
in the busiest spots. That gives about 118 fps in most of town, which is the 144 Hz ceiling, and 87–99 fps in the
busiest spots. By the table, the same PC would hold 60 everywhere on a 60 Hz monitor and get about 180 in town on a
240 Hz one (estimated: at high refresh rates the CPU time of each in-between frame starts to count too).

- **60 Hz:** the monitor is the only limit. Almost any PC holds 60.
- **144 Hz and up:** the monitor sets the ceiling, and in busy scenes the CPU decides how close you get to it. DXVK (on
  by default) roughly halves the CPU time per frame, and it matters most here.
- **Setting to try:** in MHFZ Plus > Settings, tick "Advanced"; under Quality, "Use slots missed by up
  to half an interval" lets a frame
  that just missed its slot still go out. It gives more frames in busy scenes (on 144 Hz: 120 with up to ~15 ms of
  work). The cost: two frames can land closer than one refresh apart, which can tear with V-Sync off. It adds no input
  lag.
- Nothing here trades latency for frames: the game's real frame is always shown at the moment it would have been
  without MHFZ Plus.

## Status

Beta. One player has tested it for many hours on MezeLounge and Rain (3440x1440 at 175 Hz). Known limits:

- Some objects can't be matched between two game frames. They're drawn where the game last put them (a 30 fps step)
  instead of glitching.
- Fast camera flicks around dense foliage are the hardest case. Report anything that flickers, with a clip.
- Rain's game code is a different build from MezeLounge's. The frame-rate mod only uses Direct3D calls, so it works
  on both, but Rain can change its client at any time.

**Found a problem?** [Open a bug report](https://github.com/Sushii-dev/mhfz-plus/issues/new?template=bug_report.yml).
Press Delete in game right after a glitch to save a clip, then MHFZ Plus > Help > **Make a bug report**: it zips your
logs, settings and PC specs and opens the form with your setup filled in. Write what happened and drag the zip in.

## Install

Download the latest release, extract it, run `Install.cmd` and follow the window. The included `README.txt` covers
installing, playing, settings, reporting a glitch and uninstalling.

**Linux and Steam Deck:** extract it and run `sh install.sh` in a terminal. MHFZ Plus goes into the Wine prefix the
game already runs in (Steam/Proton, Lutris, Bottles, Heroic or plain Wine), with a menu entry and optional Steam
entries; `sh uninstall.sh` removes it again. Details in "LINUX AND STEAM DECK" in `README.txt`.

Before you install, check the rules of the server you play on: see [Servers](docs/LEGAL.md#servers).

## For developers

- [Building](docs/BUILDING.md): one C++ file plus a small proxy, built with clang (llvm-mingw, i686). The settings
  app uses the C# compiler that ships with Windows.
- [Testing](docs/TESTING.md): a Direct3D 9 test app with scenes that reproduce the hard cases, an offline pairing
  bench, a stress sweep and an in-game self-check.
- [How it works](docs/HOW-IT-WORKS.md) and [engine notes](docs/ENGINE-NOTES.md): what is known about MHF-Z's
  renderer, with addresses for the 2019 MezeLounge client.
- [Contributing](CONTRIBUTING.md).

## Tips and donations (Ko-fi)

MHFZ Plus is free and open source, and it stays that way. If you'd like to support the work, you can leave a tip on
[Ko-fi](https://ko-fi.com/sushiisquared). It's completely optional: everything works the same either way, and bug
reports with a clip help just as much.

## Credits

See [docs/CREDITS.md](docs/CREDITS.md): the tools and projects this built on, and the server communities that keep
the game playable.

## License

[MIT](LICENSE). The license covers this project's own code and documentation only; Monster Hunter Frontier and
everything in it belongs to Capcom.
