# How it works

## The constraint

MHF-Z's logic is fixed-step: everything in the game counts ticks, about 30 per second. Unlocking the frame rate
would speed up the whole game, and the server expects the original timing. So the game has to keep ticking exactly as
before, and any extra frames have to be made without its help.

The game's frame limiter makes room for that. Each tick it does its work (logic, then the drawing commands for
one frame), then **sleeps** until 33 ms after the previous tick, then presents the frame. On a modern PC most of those
33 ms are spent asleep.

## The idea

MHFZ Plus is a `d3d9.dll` proxy. Windows loads it from the game folder instead of the system's Direct3D 9, and it
forwards everything to the real one (or to [DXVK](https://github.com/doitsujin/dxvk)).

1. **Record.** Every Direct3D call of game frame N is recorded: state changes, shader constants, matrices, vertex
   data, draws.
2. **Pair.** Each draw of frame N is matched with the same draw in frame N−1. For matched draws, the values that
   place geometry on screen are paired too: world/view/projection matrices, bone palettes, the vertex positions of
   CPU-built geometry such as text and particles, and light positions.
3. **Replay.** In the game's limiter `Sleep`, frame N is replayed several times with those values blended between
   N−1 and N, each replay timed for when it will be on screen. Each one is presented.
4. **Hand back.** Shortly before the game's deadline the mod restores the device state, waits for the GPU and lets
   the game present frame N exactly when it would have anyway.

The game ticks at the same moments and reads input at the same moments, and the server sees nothing different.
Input latency isn't increased: the in-between frames show the past (between N−1 and N), and frame N appears at its
original time. The optional `latency_trim` setting makes the game read input a little later in each tick, without
moving the tick.

Only Direct3D 9 (in the system's or DXVK's `d3d9.dll`), the Windows `Sleep` function (in `KernelBase.dll`) and,
with `input_latch=1`, DirectInput (in `dinput8.dll`) are hooked. The game's own code and memory are never written.

## Why pairing is the hard part

Blending is easy once you know what corresponds to what. Most of the code exists to answer that question
correctly, because a wrong pair is much more visible than no pair (an unpaired object simply moves in 30 fps steps).

- **Draw identity.** Draws are keyed by shaders, vertex format, buffers, start index, primitive count and the first
  texture and render target. Repeated meshes (trees, grass cards, crowds) share a key and are assigned within their
  group by position.
- **Owners.** A matrix uploaded once and used by several later draws is paired through the draws that read it.
  Which constant registers a shader reads for its output position comes from analysing the shader bytecode
  (`vsinfo.h`). Only those registers are blended; colours, UV scrolling and flipbook frames snap.
- **Camera estimates.** Newly visible objects have no previous frame. The camera motion is estimated from the
  objects that did pair: a vote over full matrices, then a least-squares fit of how world-fixed points moved. New
  objects get a synthetic previous position from that. Shared camera matrices follow their own path, and plain
  matrices that fit a path follow it too, so ground and decals don't z-fight.
- **Verification.** After matching, every pair is checked against the camera motion. Pairs that would slide an
  object across the screen (a tree leaving view on one side while a copy enters on the other) are dropped.
  Groups that pair poorly are re-solved with other camera predictions.
- **Screen-space text.** Each glyph is its own quad. Glyphs are identified by the pixels of their atlas cell, and
  whole strings are matched as rigid runs, so identical letters don't swap.
- **Lights.** Light slots are re-used for different lights from tick to tick. Slot swaps are detected (exact
  position matches, range changes, coupled rows) and not blended.
- **Bones** are blended as quaternions, so limbs keep their length.
- **Cuts.** When most of the scene jumps (a camera cut, a loading screen), that tick isn't interpolated.

## Timing

- The in-between frames are scheduled backwards from the game's deadline: the last one an interval before it, the
  others evenly spaced, as many as fit after the game's own work for the tick.
- The real frame N has to be presented when vanilla would present it. On a 175 Hz monitor the first in-between frame
  is ready about 6.9 ms after the real one, but the first refresh slot comes at 5.7 ms. That's why heavy scenes top
  out around 150 fps instead of 175.
- GPU work is bounded so the game never ticks late: no new in-between frame is started that the GPU couldn't finish
  before the deadline. The final wait for the GPU before handing back stays unbounded. Cutting it short made DXVK
  hang at start-up.
- A watchdog thread logs where the render thread is if it stops making progress.

## Input

The game reads keyboard, mouse and gamepads through DirectInput once per tick. A press that starts and ends between
two reads is lost. With `input_latch=1` (off by default) the mod reads the same devices every couple of
milliseconds while the game sleeps and while it draws, and a press the game would have missed is reported as held at
its next read, once. It hooks only DirectInput's own objects. In the test app, 12 ms taps went from 33% seen to 100%.

## Diagnostics built in

- **Clip recorder** (`retro`): the last second of frames stays in video memory; the clip key saves it with what the
  mod saw (pairing, constants, a frame dump). Most fixes started from a player's clip.
- **Self-check** (`selfcheck`): predicts a real frame from its neighbours and measures the error, in game.
- **Replay tracer**: replays one frame exactly and interpolated, draw by draw, and names the first draw that differs.
- **Statistics** in `mhfz-fps.log` every few seconds.

## Source layout

| Path | What |
|---|---|
| `src/proxy.cpp`, `src/d3d9.def` | the proxy: loads the real `d3d9.dll` (or DXVK) and forwards its exports |
| `src/fps.cpp` | everything else: recording, pairing, replay, scheduling, overlay, recorder, settings |
| `src/vsinfo.h` | shader bytecode analysis: which constants reach the output position |
| `launcher/launcher.cpp` | MHFZ Plus launcher: puts the mod (and the English files) in for a session, takes them out after |
| `settings/Hub*.cs`, `settings/Anim.cs` | the MHFZ Plus app: Play, Settings and Help in one window, and its animations |
| `settings/Settings.cs` | the settings model (what each setting is) and the app's command line |
| `package/` | installer scripts, the player README, the default settings file |
| `test/` | the Direct3D 9 test app and other test programs |
| `tools/` | build, test, bench and analysis scripts |

Full engine notes: [ENGINE-NOTES.md](ENGINE-NOTES.md). The development log, with every glitch and its fix:
[DEVLOG.md](DEVLOG.md).
