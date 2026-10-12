# MHF-Z engine notes

What is known about the game's frame loop and renderer, from static analysis of the 2019 MezeLounge client
(`mhfo-hd.dll`, image base 0x10000000, no ASLR) and from the mod's own logs. Addresses are static ones; at runtime the
DLL is usually relocated (on MezeLounge `ogg.dll` takes 0x10000000 and `mhfo-hd.dll` moves by +0x24e0000).

Rain runs its own build of the client, so its addresses differ. The frame-rate mod doesn't use addresses at all,
only Direct3D calls.

Points not confirmed by a measurement are marked *(believed)*.

## Frame loop

- **Frame limiter** `0x1000a2f0`: `wait = last + 33 - timeGetTime()`, `Sleep(wait)`, `last = timeGetTime()`. The tick
  is 33 ms (about 30.3 Hz), not 33.33 ms. Windows' `Sleep` oversleeps, so the game actually ticks at about 29–30 Hz.
- **Late ticks:** when a tick runs late, the limiter catches up in 33 ms steps and clears an "on time" flag. That flag
  is checked at 148 places, so the game skips drawing but still runs its logic.
- **Logic is fixed-step per tick.** A 60 Hz counter exists, but the per-frame time delta it feeds is read at only two
  places. Unlocking the limiter speeds up the game.
- **Frame end** `0x1000a4c0`: EndScene (through a wrapper), the limiter, then `IDirect3DDevice9::Present`
  (vtable +0x44) and a `Clear`.
- **Main frame function** `0x108e39c0`. BeginScene goes through a counting wrapper gated by the "on time" flag.
- The game checks the integrity of parts of its own memory during each tick. The mod never writes game code, and
  game memory only in the opt-in orbit camera (the camera's position, target and direction, values the game itself
  changes every tick; see [CAMERA.md](CAMERA.md)). (Details of those checks are deliberately left out here.)

## Direct3D 9 device

- `CreateDevice(HAL, D3DCREATE_HARDWARE_VERTEXPROCESSING)`, not multithreaded. SwapEffect COPY, no MSAA, D24S8,
  lockable back buffer.
- The game records a state block once at start-up. Direct3D 9 implements state-block recording by swapping the
  device's vtable for a recording copy, and `EndStateBlock` restores the original, which wipes any vtable hooks. The
  mod hooks `EndStateBlock` inside the recording vtable and re-hooks afterwards.
- All Direct3D calls come from `0x1000xxxx`–`0x1002xxxx`.

## What a frame looks like

- In town: about 4,400 draws per frame, about 95,000 recorded commands (3.8 MB).
- Models: static MANAGED vertex/index buffers, `DrawIndexedPrimitive`, skinning on the GPU through vertex-shader
  constant palettes (`[R|t]` rows, 3 registers per bone).
- Effects and UI: `DrawPrimitiveUP` (65 call sites). Text is one pre-transformed quad per glyph (stride 28), each
  label drawn twice (shadow, then the letters).
- Every object is drawn into the main render target and then into two depth render targets with the same matrices.
  Plus three shadow cascades (G32R32F) and a fog post-process chain into small render targets.
- `gWorldViewProj` usually sits in c38–c41. MHF's per-object matrices include a per-object depth mapping, so two
  objects' matrices don't share one view-projection.
- Foliage is camera-facing cards: hundreds of instances of one 4-vertex mesh, re-sorted by distance every frame.
- Trees are skinned meshes, placed through their root bone.
- **Lights:** pixel-shader positions in c21–c23 and c162, ranges in c24–c26 and c163 (an unused slot holds
  2.7e28), colours in c146–c158. Skinned bodies get their lights in world space. Slots are re-assigned between
  lights from tick to tick.
- The game sets up several objects (buffers, shaders, all constants) and then skips drawing those that turn out
  invisible. The leftover uploads pile up in a number and order that change per tick.

## Camera

- The camera object is at `[0x1edaad60]`: eye at +0x80, target at +0x8c, up at +0x98 (three floats each).
  `0x10baee10` builds the view matrix from them with `D3DXMatrixLookAtRH`.
- Angles elsewhere in the game are 16-bit (0x10000 = 360°).
- Where the vertical angle is limited hasn't been found yet. See [CAMERA.md](CAMERA.md).

## Input

- Keyboard, mouse and gamepads go through DirectInput 8 in immediate mode: the game reads the device state once per
  tick. A press that starts and ends between two reads is never seen.
- The game also calls `GetAsyncKeyState`. Its "pressed since the last call" bit is shared by every caller in the
  system, so the mod reads its own keys without consuming it.

## Rain

Rain runs its own build of the client. The frame-rate mod works the same there, because it only sees Direct3D calls.
