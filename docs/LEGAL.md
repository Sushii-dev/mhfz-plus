# Legal notes

This page explains how the project relates to the game and to the people who run it. It isn't legal advice.

## Who owns what

- *Monster Hunter*, *Monster Hunter Frontier* and *Monster Hunter Frontier Z* are trademarks of Capcom. The game's
  code, art, text, sound and data are Capcom's copyrighted work. The official service ended in December 2019; that
  doesn't change who owns the game.
- MHFZ Plus is an unofficial fan project. It isn't affiliated with, endorsed by or supported by Capcom, by anyone
  who published or ran the game, or by any server team. The game's name appears here only to say what the project
  works with.
- The license in [LICENSE](../LICENSE) covers only this project's own code and documentation.

## What this repository contains, and what it doesn't

It contains:

- original source code, build scripts, tests and documentation;
- notes on how the game's renderer and frame timing behave, with addresses in the 2019 client
  ([ENGINE-NOTES.md](ENGINE-NOTES.md)). These came from reverse engineering for one purpose: making a graphics mod
  work with the game.

It doesn't contain:

- game executables, DLLs or data files, or anything extracted, decrypted or decompiled from them;
- game text or translations of it;
- server software, server data or account tools.

You need your own copy of the game, installed through a server's official launcher.

## The English translation

The launcher can apply an English translation. The translation data isn't in this repository. Where it's
distributed, it ships as differences against the original files: they only apply to an exact original file the
player already has, and they're checked by SHA-256 before use. The translated text is machine-generated (see
[AI-USAGE.md](AI-USAGE.md)).

## Servers

MHF-Z is only playable on community-run servers, and each one sets its own rules. Please respect them.

- **Ask first.** Look for the server's mod rules, or ask its staff, before you use any mod. Many servers treat any
  change to the client as cheating, even a visual one. A server can ban accounts, and some record hardware details.
- **What the frame-rate mod changes:** only what's drawn on your screen. It doesn't change game speed, timing, input,
  network traffic or anything the server receives. It doesn't patch game code, doesn't write game memory, and
  doesn't touch the game's own integrity checks.
- **The optional orbit camera is different** (off unless you turn it on): it writes values inside the running game,
  the camera's position and target and the direction your hunter walks by, once per tick. Never game code, nothing
  else. It changes what you see and which way "forward" is, like turning the camera yourself, but a server that
  forbids changing the client's memory forbids this. Ask before you turn it on. Details: [CAMERA.md](CAMERA.md).
- **What the launcher changes:** it puts files into the game folder for a play session and takes them out again
  when the game closes, so the game folder is back to its original state between sessions.
- **Rain, plainly:** Rain's launcher checks the game files before every start. MHFZ Plus waits until that check has
  passed, then puts the frame-rate mod (and, if chosen, the English files) in, and takes them out when the game closes.
  So Rain's check always sees the original files, and the game runs with the mod. Rain's team may see that as working
  around their check. Use it on Rain only if you accept that, and stop if Rain asks players to.
- If a server team asks us not to support their server, we'll respect that.

## Third-party software

See [CREDITS.md](CREDITS.md). In short:

- The release binaries are built with llvm-mingw. They statically link parts of LLVM's runtime (libc++, libunwind,
  compiler-rt: Apache-2.0 with LLVM exception) and the mingw-w64 startup code (permissive licenses).
- [DXVK](https://github.com/doitsujin/dxvk) 3.1.1 (zlib license) comes with the release as the optional "Graphics backend:
  DXVK": its 32-bit `d3d9.dll`, unmodified, with its license text (`fps\dxvk\LICENSE.txt`). It isn't in this repository.

## No warranty

The software is provided as is, without warranty of any kind (see the license). Using mods on an online game is at
your own risk.
