# The vertical camera limit

MHF-Z limits how far the camera can tilt up and down, even with the smoothest camera setting. Players have asked
whether it can be raised, so that you can look straight up or down.

## What's known

- The camera object is at `[0x1edaad60]` in the 2019 client: eye at +0x80, target at +0x8c, up at +0x98 (floats).
  `0x10baee10` builds the view matrix from them (`D3DXMatrixLookAtRH`) once per tick.
- The function that computes eye and target from the player's input hasn't been identified yet. These searches of
  the 2019 client found no camera pitch limit:
  - 16-bit angle clamps (`cmp` against an angle-sized immediate, then a store of the same value);
  - float clamps (`minss`/`maxss`/`comiss` against constants that look like angles in radians or degrees);
  - the functions that store into the eye or target directly. They turned out to be fixed and scripted cameras.

## Why it isn't a setting

The frame-rate mod only redraws what the game already drew. The camera limit is part of the game's own state:
aiming, what's culled (objects behind the camera aren't drawn) and the camera collision all follow it. The view can't
be tilted further at draw time. The world above or below the game's view isn't in the frame, and aiming would no
longer match the screen.

Raising the limit therefore means changing a value inside the running game. MHFZ Plus has never done that: it doesn't
patch game code or write game memory. Changing that would:

- add risk on servers that check the client (the game also checks parts of its own memory);
- change what the project can honestly say about itself (see [LEGAL.md](LEGAL.md#servers));
- need per-client work: Rain's client is a different build, so its addresses differ.

## Possible next steps

1. **Find the limit**, read-only, on MezeLounge. The mod has a developer tool for this, the camera probe:
   with developer keys on, Ctrl+Shift+F6 watches the camera's eye and target for a few seconds with hardware write
   breakpoints. It logs which game code writes them and how far the camera tilts. Tilt the camera against both limits
   while it runs, then read that code. That shows whether the limit is a constant in code, a value in a data table, or
   a result of the camera's collision. The probe reads only; it writes nothing in the game.
2. **If it's a value in data,** an opt-in experiment could set it at run time on MezeLounge only. Straight up or
   down (±90°) breaks the look-at math, so the practical maximum is about ±85–89°.
3. Whatever the outcome, it would stay a separate, clearly marked opt-in, off by default and never enabled on servers
   that forbid it.
