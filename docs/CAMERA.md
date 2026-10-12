# The camera

MHF-Z's own camera turns slowly and stops well short of looking straight up or down. MHFZ Plus has an optional
**orbit camera** for controllers: the right stick turns a camera that circles the hunter, smoothly and as far up and
down as you set it (up to 85°), with collision, L3 to recenter, and distance, height and shoulder settings. It's based
on TYCHKA's [Frontier Orbit](https://github.com/Tychkaneg/Frontier-helper), used with permission under MIT.

It's off by default: MHFZ Plus > Settings > Camera, or `camera=1` in `mhfz-fps.ini`. Ctrl+Shift+C (`key_camera`)
switches between it and the game's own camera while playing.

## Why it writes game memory, and exactly what

Everything else in MHFZ Plus only changes what's drawn. The camera can't work that way. Aiming, culling (what's drawn
at all), lights and, above all, which way "forward" is for the hunter all follow the game's own camera. A camera that
only exists on screen would show a world the game isn't drawing, and the hunter would walk the wrong way.

So the orbit camera is the one exception to the project's rule. Once per tick, when the game builds its view, it
writes:

| What | Where (MezeLounge's 2019 `mhfo-hd.dll`, Rain's `client.dll`) | Size |
|---|---|---|
| the camera's position (eye) | camera object `[0x1edaad60]` (= `0x1e7fff40`) +0x80 | 3 floats |
| where it looks (target) | camera object +0x8c | 3 floats |
| the camera direction the hunter moves by, and its copy | `0x11c4a168`, `0x11c4a16a` | 2 × 16 bits |

Nothing else, and never code. The game writes the same values itself: the eye and target every tick (from its camera
controller), the direction whenever its own camera turns. When the orbit camera is off, nothing is written; switched
off while playing, the game's camera carries on from the direction last written.

Players should know this before they turn it on: a server that forbids any change to the client's memory forbids
this. The setting, the player README and [LEGAL.md](LEGAL.md#servers) all say so.

## How it's hooked

The game calls `D3DXMatrixLookAtRH` (from `d3dx9_43.dll`) in `0x10baee10` with the camera object's eye, target and up.
MHFZ Plus hooks that function inside `d3dx9_43.dll`, like it hooks `Sleep` inside `KernelBase`. The game's code and
its import table are never touched; Frontier Orbit patches the import table, MHFZ Plus doesn't.

- Windows' `d3dx9_43.dll` has a hot-patch prologue (`mov edi,edi` with 5 bytes of padding before it): the same patch
  as `Sleep`.
- Wine's own `d3dx9_43.dll` has a plain prologue (`push ebp; mov ebp,esp; push edi; push esi`): those 5 bytes move to
  a trampoline.
- Only the gameplay camera's call acts: eye and target must be the camera object's own (`+0x80`, `+0x8c`). Shadows,
  menus and other views pass other pointers and are left alone.
- The hook writes before the real function runs, so the game's view matrix (and the copies of it, the eye and the
  target that the game keeps after the call, see below) are built from the orbit camera.

## When it stands down

The camera checks every tick and leaves the game's camera alone when:

- the game uses another camera (`0x11c4a409` ≠ 0) or a special one, such as a cutscene (`0x11c4a41d` ≠ −1);
- there's no hunter, no map collision data, or the scene just changed (a new area, a teleport, a pause of more than
  half a second). After half a second of steady scene it resyncs: the first time from the game's own camera (its
  distance and height become the reference), later behind the hunter;
- no XInput controller is connected (keyboard and mouse turn the game's camera; Steam Input counts as XInput);
- another program has taken the game's import slot for `D3DXMatrixLookAtRH` (Frontier Helper does): both would
  write every tick, so MHFZ Plus stays off and says so.

While the game window isn't in front, the orbit camera stays but ignores the stick.

Before anything is hooked, 13 blocks of the game's code that use these addresses (the view builder, the camera
controller, the geometry sweep and its helpers, the ground query) are compared with their hashes (FNV-1a of
Frontier Orbit's exact-byte checks), and the data addresses must be committed memory of the game DLL. One changed byte
and the camera stays off for the session; the log names the check. Both clients share this code byte for byte. Rain's
client maps its encrypted game DLL at `0x10000000` itself (it's in no module list), MezeLounge loads `mhfo-hd.dll`
normally; the camera finds either.

## Collision

The camera asks the game's own queries, from the game's thread, the way the game's camera does:

- the geometry sweep `0x108d0f10` (start in ECX; end, radius, hit, surface mask on the stack): once with the game
  camera's wall mask (`0x8009`), once for floors (`4`), 28 units of radius and 4 of margin;
- the ground height `0x108202a0` (point in ESI, height in XMM0), sampled along the line to the camera (24 samples,
  then halving the first bad interval), where the map's ground cells say there is ground;
- the map's box from its collision grid (`[0x1e77dc2c]`).

The distance closes in at once and grows back smoothly. The reference distance is never changed by collision.

## With the frame-rate mod

The camera is computed once per tick, at the moment the game builds its view, so the game's real frame shows it and
the in-between frames follow it like any camera move (the camera paths turn the in-between frames with it). No input
lag is added: the stick is read right before the frame is drawn, later than the game reads its own input. The tick
pays about 0.02 ms for the camera itself on Windows and 0.03 ms under Wine, plus the game's own queries (a few
dozen per tick).

**Blinking (to watch for).** TYCHKA saw objects blink (lights in caves) after merging their camera with the frame-rate
mod. In `0x10baee10` the game computes two things from its own camera *before* the `D3DXMatrixLookAtRH` call, so
they still follow the game's camera that tick: the view direction it stores at camera +0xa4, and the up vector
(rotated by the camera's roll at +0xc0, which is normally 0). After the call it copies the matrix, eye and target into
the camera object (+0x00, +0x40, +0x4c), so those follow the orbit camera. Anything else that reads the game's camera
between its controller (`0x1082c580`, which copies the camera into the object every tick) and the view call would
also see the game's camera. If blinking shows up, these are the first suspects. The fix would be to write the
direction at +0xa4 too: still the camera's own values.

## Tests

- `test\cameratest.cpp` (`build.ps1 -Tests`), no window: the math on its own, then the real mod DLL against a
  stand-in game at `0x10000000` (camera object, hunter, collision grid, stand-in queries with the game's calling
  conventions, a stand-in controller). It checks the turn, the angle limits, the ground and walls, recentering, the
  double call while the game blends two cameras, other views left alone, every reason to stand down, and what the
  call costs the tick. Given MezeLounge's `mhfo-hd.dll` (`cameratest.exe <mod folder> <mhfo-hd.dll>`), its sections
  are mapped there as plain data, never run: the code checks must pass, and fail with one byte changed.
- `tools/linuxtest.sh` runs it under Wine (the trampoline hook).
- Not tested here: the real game. That needs a player with a controller, on MezeLounge and on Rain.

## The game's own pitch limit

Where the game limits its own camera's tilt still hasn't been found. Searches of the 2019 client for 16-bit angle
clamps, float clamps against angle-like constants, and the functions that store into eye or target directly found
nothing (those were fixed and scripted cameras). The orbit camera doesn't need it: it has its own limits. The
developer camera probe is still there (developer keys, Ctrl+Shift+F6): for a few seconds, hardware write breakpoints
on the camera's eye and target log which game code writes them and how far the camera tilts. It only reads.
