# MHFZ Plus

High frame rate for **Monster Hunter Frontier Z** on community servers, without changing game speed and without
adding input latency. An optional launcher can also apply an English translation.

> Fan project. Not affiliated with, endorsed by or connected to Capcom or any server team. Contains no Capcom code
> and no game files. See [Legal notes](docs/LEGAL.md).
>
> **Made with AI.** Almost all code and documentation here was written by an AI coding agent (Anthropic's Claude),
> directed and tested in game by the maintainer. Details: [AI usage](docs/AI-USAGE.md).

## What it does

MHF-Z runs its game logic at 30 ticks per second and draws one frame per tick. MHFZ Plus keeps that exactly: the game
still ticks at the same moments, reads input at the same moments and sends the server the same things. In the time the
game would otherwise spend sleeping between ticks, MHFZ Plus re-draws the last frame several times with every object,
the camera and the UI moved part of the way towards their next position. You see 60, 120, 144 or 175 frames per
second; the game sees nothing different.

It's a `d3d9.dll` placed next to the game. It hooks Direct3D 9 and the game's frame limiter `Sleep` (plus DirectInput,
if you turn on the optional tap catching). It never patches game code and never writes game memory.

- In-between frames up to your refresh rate (in heavy scenes on fast PCs it tops out around 140–150 fps).
- No added input latency. An optional setting reduces it slightly.
- FPS counter, an on/off key to compare, and a clip recorder: a key saves the last second of frames when something
  looks wrong, for bug reports.
- A settings app for keys and options.
- Works with Windows' Direct3D 9 or with [DXVK](https://github.com/doitsujin/dxvk) as the backend.

How it works in detail: [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md).

## Status

Beta. One player has tested it for many hours on MezeLounge and Rain (3440x1440 at 175 Hz). Known limits:

- Some objects can't be matched between two game frames. They're drawn where the game last put them (a 30 fps step)
  instead of glitching.
- Fast camera flicks around dense foliage are the hardest case. Report anything that flickers, with a clip.
- Rain's game code is a different build from MezeLounge's. The frame-rate mod only uses Direct3D calls, so it works
  on both, but Rain can change its client at any time.

## Install

Download the latest release, extract it, run `Install.cmd` and follow the window. The included `README.txt` covers
installing, playing, settings, reporting a glitch and uninstalling.

Before you install, check the rules of the server you play on: see [Servers](docs/LEGAL.md#servers).

## For developers

- [Building](docs/BUILDING.md): one C++ file plus a small proxy, built with clang (llvm-mingw, i686). The settings
  app uses the C# compiler that ships with Windows.
- [Testing](docs/TESTING.md): a Direct3D 9 test app with scenes that reproduce the hard cases, an offline pairing
  bench, a stress sweep and an in-game self-check.
- [How it works](docs/HOW-IT-WORKS.md) and [engine notes](docs/ENGINE-NOTES.md): what is known about MHF-Z's
  renderer, with addresses for the 2019 MezeLounge client.
- [Contributing](CONTRIBUTING.md).

## Credits

See [docs/CREDITS.md](docs/CREDITS.md): the tools and projects this built on, and the server communities that keep
the game playable.

## License

[MIT](LICENSE). The license covers this project's own code and documentation only; Monster Hunter Frontier and
everything in it belongs to Capcom.
