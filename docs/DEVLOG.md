# Development log

The working notes kept while the mod was built, in order: what each glitch was, how it was found and how it was fixed. Addresses are for the 2019 MezeLounge client. Kept as written, so some early statements are superseded by later ones.


Session log started 2026-10-06. Addresses are vanilla MezeLounge `mhfo-hd.dll` (base 0x10000000, no ASLR).

## Engine facts (static analysis)

**Frame limiter:** `0x1000a2f0`.
- Waits `wait = last + 33 - timeGetTime()` with `Sleep(wait)`, then sets `last = timeGetTime()`. The tick is 33 ms (about 30.3 Hz), not 33.33 ms.
- Late path: if `[0x119dd750]` is set, it catches up in 33 ms steps and clears the "on-time" flag `[0x119dd74c]`. That flag is checked at 148 sites, so the game skips drawing (frame skip) but still runs logic.
- Special case: when `[0x11a9758c]` is set and the area id is 0x17b, a drift correction targets 33.333 ms per tick (`[0x11a97594]`).
- It also keeps a 60 Hz tick counter (`t*3/50`). The per-frame delta goes to `[0x1e7fff38]`, which is read at only 2 sites. **Game logic is fixed-step per frame.** A plain unlock would speed the game up.

**Frame end:** `0x1000a4c0`.
- Then `0x108e3f00`, which calls EndScene via the `0x1000a2b0` wrapper.
- Then the limiter.
- Then `IDirect3DDevice9::Present` (vtable +0x44) using the device at `[0x1e811a3c]`, followed by `Clear`.

**Main frame function:** `0x108e39c0`, called from `0x1158f8b0`. BeginScene goes through the `0x1000a260` wrapper (counted, and gated by the on-time flag).

- **Never patch game code.** The mod hooks only D3D9 and `KernelBase!Sleep`.

**Device:**
- `CreateDevice(HAL, HARDWARE_VERTEXPROCESSING 0x40)`, no MULTITHREADED flag.
- SwapEffect COPY, no MSAA, D24S8, lockable backbuffer.

**Rendering:**
- All D3D calls are in `0x1000xxxx`–`0x1002xxxx`.
- Models use static MANAGED VB/IB (one CreateVertexBuffer site) with `DrawIndexedPrimitive`. Skinning happens on the GPU through VS constants.
- Effects and UI use `DrawPrimitiveUP` (65 sites).
- D3DXCreateEffect is imported.

## Design (implemented in `src/`)

The mod is a d3d9.dll proxy that hooks the device vtable.
- Every D3D command of frame N is recorded, from Present(N-1) to the limiter `Sleep`.
- In the `Sleep` hook (render thread, first Sleep after draws in a frame), frame N is replayed k times before the real deadline. Each replay interpolates between N-1 and N:
  - VS float constants,
  - SetTransform matrices,
  - positions in UP vertex data.
- Draw matching uses a key: shaders, decl/FVF, VB, IB, start index, primitive count, tex0 and rt0, taking the k-th occurrence.
- Constants are paired within the segment that leads up to each matched draw.
- A "snap" test skips packets whose values jump by more than 75%.
- The backbuffer holding N is saved with StretchRect and restored. A full device state block is applied at the start of each replay, and the end state is restored afterwards.
- The real Present(N) happens exactly when vanilla would do it:
  - the simulation rate is unchanged,
  - no input latency is added.

Build with `build.ps1` (the Windhawk llvm-mingw toolchain, i686). The test app is in `test\testapp.cpp`; frame captures confirm correct interpolation.

## In-game findings (MezeLounge, 2026-10-06)

**Loading and hooks:**
- `mhfo-hd.dll` is **relocated** to 0x124e0000 because `ogg.dll` takes 0x10000000. Static addresses get +0x24e0000 at runtime.
- The game records a state block once at start-up. The D3D9 runtime implements state-block recording by rewriting the per-device vtable (a heap copy), and EndStateBlock restores the pristine copy, which wipes vtable hooks. Fix: hook EndStateBlock inside the recording vtable and re-hook after it. `CheckHooks()` runs in the Sleep hook as a backstop.

**Town load:**
- About 4,400 draws per frame, about 95k recorded commands (3.8 MB).
- Each replay took about 8 ms including Present. The window is created with the default vsync interval, so Present blocks. Shown frame rate in town was about 60 fps; menus reached about 170.

**Tick rate:**
- Vanilla ticks at 29.2 Hz on this PC because Sleep oversleeps. The first precise wait ran at 30.0 Hz, which made the game 2.7% fast.
- `precise_tick=0` (the default) now ends with a real Sleep for the remaining whole milliseconds, which reproduces the vanilla rate.

**Artifacts from v1, and their fixes:**
- UP sprites and glyphs were paired with the wrong ones. Fix: a content hash of the non-position, non-colour vertex bytes is part of the key, plus a jump guard.
- UV-animation and colour constants were being lerped. Fix: vertex-shader bytecode dataflow analysis (`vsinfo.h`) plus a per-frame liveness pass. Only registers that reach oPos are lerped.
- Identical meshes were paired by draw order. Fix: pick the nearest previous instance by its last positional constant packet (window ±24).

**Round 3: analysis of debug-view captures (magenta = not lerped):**
- Only about 2% of draws were not lerped: UI text, and a 249-vertex dome mesh drawn 8×/frame whose instance matching used the wrong packet.
- The "seams/voids along roads" were really grass/flower billboard batches (DrawPrimitiveUP). These are rebuilt in a different order every frame (distance sort), so vertex k was lerped to a different flower, smearing white streaks.
  - Fix: a per-triangle consistency test (`UpConsistent`). In world space a vertex may move at most its triangle's size. RHW triangles must move rigidly and by less than 15% of the screen. One failing triangle snaps the whole batch.
- Floating text scrambled because identical glyphs in different labels were paired. Fix: nearest-position matching for UP draws (first vertex).
- Per-packet positional snaps caused half-lerped objects (flicker, depth-equal voids). Removed. A whole-scene cut detector replaces them: if more than 40% of positional packets jump by more than 100% of their own magnitude, that tick is not interpolated.
- The instance signature is now chosen at record time: the newest packet with at least 3 registers that the draw's own VS reads for position.
- Captures go to VRAM first (StretchRect into RT surfaces) and are written after the sequence. Pacing stays normal; before, captures triggered the game's catch-up ticks.
- The replay-cost estimate is now a plain EMA with decay when it blocks every replay. It used to get stuck and stop extra frames in heavy scenes.

**Round 4: the seams, found by self-check evidence (selfcheck-run9\0061):**
- A big terrain mesh (draw 237, 7,799 tris, VS `10811200`, gWorldViewProj in c38–c41) was matched, but its c38 upload was not paired.
- The game uploads the matrix once and draws several pieces with it. Culling changes which piece comes first, so in the previous frame the upload sat in another draw's segment. Segment-local pairing therefore failed, and that piece stayed on the 30 Hz camera while its neighbours were lerped, tearing seams open.
- Fix: `CollectOwners` and `PairOwners`. Each draw records which VSCF uploads its VS reads for position at the draw (liveness). For a matched draw pair, the uploads read on both sides are paired by slot. Pairs are sorted by command offset afterwards.
- Reproduced in the test app (shared upload plus a draw that exists every other tick). The old build shows no rotation in the in-between frames; the new build interpolates.
- The self-check is blind to strictly alternating patterns, because it compares frames 2 ticks apart.

**Round 5: pop-in and twitching:**
- Newly visible objects (culling) and LOD switches are unmatched, so they used to sit at t=1.
- Fix: camera-motion estimation per (RT, slot) context. D = Mprev · Mcur⁻¹ is measured from matched 4×4 positional packets, and the most-agreed candidate is taken (≥2 votes, or a lone candidate). Unmatched packets get a synthetic previous value D·Mcur (`g_synth`, `Pair.synth`). `debug_cam=1` logs the contexts.
- Bone palettes: the VS analysis records the relative-address range (`relBase`/`relMax`). A span of 3 means [R|t] rows, and those bones are quaternion-slerped (`SlerpBone`) instead of lerped. In the test, linear blending shrank a 40° step by 12% at mid-tick; slerp keeps the size.
- `ConstLerpMask` now keeps unchanged registers in the mask, so bones stay whole, and returns "changed" separately.

**Round 6: leaf flicker:**
- Foliage is camera-facing billboard quads: 234 instances of one 4-vertex mesh per frame, with the draw order re-sorted by distance every frame (27 order inversions in one frame).
- Windowed nearest matching paired cards with other cards, so leaves jumped "closer and further".
- New matcher: draws are grouped by key. 1:1 groups match directly and feed a first camera-motion estimate.
- Repeated-mesh groups are assigned globally by ascending distance, for groups up to 120k pairs. The distance is the clip-space origin of the card, M·(0,0,0,1), carried back with D, compared in NDC plus relative depth, plus a small orientation term.
- Each group is assigned both with D and without D, and the lower total cost wins. This guards against a minority camera in the same context.
- Owners are collected before matching.
- `tools\dups.py` lists repeated meshes and order inversions in a dump.

**Round 7: ghost trees:**
- A rolling recorder (`retro=1`, 192 half-resolution frames in VRAM, saved on Delete or `retro_trigger`) caught whole tree canopies floating in the sky in in-between frames.
- This happened when a new tree came into view (tick stats: +20 draws, 20 rejected, 13 camera-estimated).
- Cause: the camera-motion vote used only the first 24 candidates and accepted 2 agreeing ones. Foliage cards each turn to the camera, so two nearby cards "agree" by coincidence. That gave a wrong D, which threw the newly visible tree meshes off.
- Fix: up to 512 candidates. Vote on an even sample of 96. Require at least max(3, n/4) agreeing.
- Tools: `tools\ghosts.py` (leaf-green pixels far from both real frames) and `tools\ghostview.py`.

**Round 8: remaining ghost canopies:**
- Off-screen objects were paired across opposite sides. Fix: a `CrossesScreen` guard in PairOwners, SynthesizePrev and InstDist. It caught about 6 pairs per tick.
- The sweep log (`retro\NNN\sweeps.txt`) was empty at a remaining ghost. So the canopies are not 4×4-placed: trees are bone-driven skinned meshes (gBlendMatrix palettes).
- Bone instances are now identified by the root bone's world position (`FirstBones`, `Inst.bones`). Group matching uses world distance with a limit of max(2, 16·median), capped at 150 units.
- 1:1 groups get the same 150-unit plausibility check.
- PairOwners refuses to blend any bone palette whose root jumps more than 150 units ('b' entries in the sweep log).

### Round 9: floating tree canopies (run18 recordings)

- **Symptom:** a whole tree canopy hangs in the sky in the in-between frames while the camera turns near trees.
- **Cause (sweep log):** one copy of a tree mesh leaves view at one edge while another copy enters at the other edge in the same tick (NDC x +1.22 to −1.21, just inside the ±1.25 screen-crossing guard).
  - Blending the two slides the tree through the middle.
  - Same mechanism: a far tree at the centre (w≈5000) paired with a near one at the edge (w≈2000).
  - In groups of two instances the median match distance *is* the bad pair, so MatchGroup's limit lets it through.
- **Fix: `VerifyMatches`** runs after matching. It measures the camera motion D over all matched pairs (unchanged matrices vote for a still camera), then checks every matched 4x4 against D:
  - The pair is dropped when its origin's own motion (clip units ≈ world units) is over 150 *and* the gap is visible: more than 0.5 NDC, or a depth change over 50%.
  - The dropped draw is unmatched, so its prev is synthesized from D, which is exact for scenery.
  - Skipped for a pass when more than a quarter of it would fail (that means D itself is wrong).
  - Sweep log codes: 'x' = rejected by this check; 'u' = a big jump in a pass that has no camera estimate.
- **Regression:** `testapp.exe 7 0 1` (scene 8, culling swap in game-like units) with `tools\swaptest.ps1 [dll]`. Old build: ghosts of 9,241 px at the swap ticks. New build: ≤3 px.

### Round 10: swapped letters, replay tracer (run20)

- **Text:** MHF draws every glyph as its own pre-transformed UP quad (stride 28; ~525 per frame), keyed per glyph by its texcoords.
  - Identical letters were matched by nearest first vertex. When a label moves further than one letter per tick, adjacent identical glyphs swap; "Small" shows as "Sma l" in the in-between frames (retro-run19\003).
  - **Fix: `RefineScreenGroups`.** Each repeated screen-space glyph's previous position is predicted from the median displacement of matched quads of *other* kinds within 64 px (the rest of its label). The group is re-assigned when that explains it better.
  - Regression: `tools\labeltest.ps1 <tag> [dll]` (scene 10, "Small EX").
- **Rejected idea:** a group-median shift from the group's own first assignment. With identical instances in a row that just locks in the wrong offset; the 4x4 name-tag scene (`testapp 5 0 0 1`) got worse, so it was removed.
- **Lighting:** the character is ~20% brighter in every replayed frame by day (not at night), independent of t. That points to a state that replay doesn't reproduce (sun shadow suspected), not to interpolation.
- **Replay tracer** (`trace_interval_s`, `trace_trigger`; also queued by the save key). After every draw of one real frame it snapshots RT0 into atlases (64x32 full view plus a 32x64 centre crop). It then replays the frame twice the same way: once exactly (t=1, no pairs) and once interpolated (t=0.5).
  - `trace\NNN\trace.txt` names the first draw where the exact replay differs from the game, and lists the largest per-draw interpolation jumps.
  - For those draws it gives textures (format/usage/RT surface), VS/PS disassembly and pointers, plus the atlases and the frame dump.
- **Save key** now works without game focus, and catches taps shorter than one tick.

### Round 11: scrambled menu text, night fog sheet (run21)

- **Menu text** (run20 retro\003): changing a value (Aiming Speed 13→14) scrambled letters across the whole menu in the in-between frames.
  - Glyph identity is texcoords, and the font atlas apparently reshuffles, so an unchanged letter can share its key with a different letter elsewhere.
  - **Fix (end of `RefineScreenGroups`):** a matched screen-space quad must move like the quads within 64 px of it (median displacement; tolerance 6 px + 25% of the median). Otherwise it becomes unmatched and is drawn where it is.
  - Regression: scene 11 (`testapp 5 0 0 0 0 1`) + `tools\statictext.py`. Old build: 1,275 stray pixels; new: 0.
- **Night fog sheet** (selfcheck-run20 #42): a huge camera-facing fog card (WVP row0 = (s, 0, 0, tx)) pops in, so it is unmatched. The camera-motion estimate D·M treated it as world-fixed and turned it by the camera motion, so its edge swept across the in-between frames.
  - **Fix (`SynthesizePrev`):** for camera-facing matrices (|m02|, |m10|, |m30| < 1e-3·|m00|) only the origin is carried back; the orientation is kept.
  - Regression: the fog card in scene 8 (`tools\cardpop.py`). Edge 324/321/318 px against an expected 323/321/318; the old build gave 328/324/320.
- **Tracer:** 4,608 draws (48x24 / 24x48 tiles); readback in 192-row bands (Delete-triggered traces used to fail); only successful saves count; triggered 3 s after a save. The save key also writes `retro\NNN\dump.txt` (next frame pair with pairing).
- `tools\regress.ps1 [dll]` runs every test scene and prints the key numbers.

### Round 12: fog haze lost in the in-between frames (run22, open)

- run21: the settings text was fine; the fog sheet edge was gone, but the dawn/night **fog haze is weaker in every in-between frame**.
  - `tools\brightbias.py` shows the in-between frames broadly darker than the blend of the real frames around them.
  - Self-check #16–#18: the predicted frames lack the haze on the steps, and no fog draw is flagged.
- **Tracer:** the exact replay matches the game at all draws (day and night), so this is not a state problem; it is interpolation.
  - In moving scenes the tracer's t=0.5 comparison is dominated by motion. A near-still daytime trace showed no brightness problem, so the character brightening is intermittent.
- The fog post-process chain (DPUP quads with FVF 0x104 into small RTs) is identical in both frames and correctly paired.
- **Results (run22):**
  - Five exact replays in a row are pixel-identical at every draw (16 traces, up to 4,573 draws), so nothing accumulates per render.
  - In town the self-check predictions are near-perfect: they blend ticks N−2 and N at t=0.5 and are compared with N−1. The ablation therefore can't discriminate.
  - Yet the in-between frames (blending N−1 and N) show washed-out clerks: `tools\brightcount.py` gives about 10% more near-white pixels in the clerk area (run21 004).
  - Suspected: a difference that appears only when *neighbouring* ticks are blended (per-tick alternation).
  - `tools\pairjumps.py` on a consecutive-tick dump found only 4x4-atlas flipbook offsets (c5, 0.25 steps), which are already snapped.
- **run23 tracer:** side 1 blends N with N−2 at t=0.75, side 2 blends N with N−1 at t=0.5 (the same moment for linear motion). The per-draw jumps between them name the draws that only go wrong with neighbour-tick blending.
- **Fog ablation result (run22, 1,666–1,686 s):** only `no-camera-estimate` helps, by a lot. Mean error 10.8 → 5.3, 9.7 → 5.6, 3.0 → 0.7; bright/dark blocks 2125/7844 → 416/460. All other switches change nothing.
  - **Cause:** the camera-motion vote counted every *draw*. MHF draws each object several times with identical matrices (main pass + fog passes, e.g. a fog card 3x), so one object outvoted the scene in the fog-pass contexts. A context with a single candidate also accepted it as the camera.
  - **Fix:** one witness per distinct (prev, cur) matrix pair (`FirstWitness`), and at least two agreeing witnesses (no single-candidate D). Regressions unchanged.
- **Character brightness flicker, found (run24).** Clerks (Diva Defense etc.), Felynes and players looked brighter in the in-between frames, by day and at night.
  - The blend-with-N−1 vs blend-with-N−2 trace showed only small differences in the scene RT. Its large jumps were depth-packing passes (VS 10856400, PS packs depth in 1/256 steps), which magnify any difference.
  - `tools\pairjumps.py` (threshold 0.03) on a clerk dump: two clerks' light constants (PSCF c162, c21, VSCF c9) were paired crosswise.
  - `tools\drawat.py`: all of them sit in the segment before *one* draw. The game sets up several objects (VB, IB, VS, all constants) and then skips drawing those that turn out invisible. The leftover uploads pile up, in a number and order that change per tick.
  - `PairSegment` walked both segments in order, so a live upload got paired with a skipped object's (and lerped when within 20%).
  - **Fix:** only live uploads are paired (one overwritten later in the segment is skipped), each with the counterpart's *last* upload of the same slot.
  - Regression: scene 12 (`testapp 5 0 0 0 0 0 1`, `tools\regioncolor.py`). Old build: up to 20.8 levels of tint in the in-between frames; new: 0.0. Other scenes identical.
- **Remaining fog shimmer (run25).** run24 retro\001: the blurred signed difference of the in-between frames shows the fog's noise pattern displaced.
  - The dump shows the fog noise projection (VSCF c206–c209, a mixed packet "pos2/4") scrolling 0.085 → 0.123 per tick. That is a 31% relative change, so the 20% smooth rule snapped it and the fog texture stepped at 30 Hz.
  - The same rule snapped a near-camera object's PS copy of its matrix (PSCF c38: −4.28 → −6.02).
  - **New rule in `ConstLerpMask`:** lerp when the value moves like it did one tick earlier (per changed component the same sign, |v1 − v0| ≤ 35%). History comes from the previous tick's pairing (`SaveHistory` / `HistoryFor`, frame serials). Otherwise the old relative rule applies. Values on 1/64 steps (atlas frames) never lerp.
  - Both `PairSegment` and `PairOwners` (mixed packets) pass the history.
  - Regression: scene 13 (`testapp 5 0 0 0 0 0 0 1`, `tools\rampcheck.py`). Mean deviation 0.79 → 0.46 levels; the remaining peaks are the first tick after a reset, where no history exists.
- **Character/clerk flicker = "realistic shadows (town)" dropping out in the in-between frames** (the maintainer found it by toggling the setting; the flicker state equals shadows off). Status: OPEN.
  - Live A/B variants (Ctrl+Shift+F7, square colour) 1–4 (smooth constants, PS constants, UP quads, slerp) change nothing. Variants 5/6 (purple: passes other than the main RT not interpolated; cyan: main RT not interpolated) are deployed but untested.
  - The 3 shadow cascades (96 draws each, RTs 3440² G32R32F, `rtmatch.py`) are all matched within their own RT.
  - The main VS 0497b440 reads gDepthProj/1/2 (c182/c202/c206, cascade lookups, non-positional, smooth) and gShadowVolumeSize (c164).
  - The two per-object extra passes (PS 2559c900 / 253a39c0) only pack depth.
- **Scheduler (run27):** in-between slots are now placed backwards from the deadline (the last one an interval or a frame budget before it, evenly spaced, as many as fit after the game's own 6–8 ms of work). The old grid from the previous real frame dropped its first slot every tick: about 300 missed per 10 s and ~117 fps in town. Test app: ~174 fps.
- **"Slight regressions everywhere" with the new scheduler (run27).** The regression suite gives identical results at 120 and 170 fps (`$env:REG_TARGET`), so the fixes do not depend on frame rate.
  - What changed is the in-between t range: it now starts at ~0.15–0.25 instead of ~0.35. Anything not blended (unmatched, dropped, snapped) sits almost a full tick ahead of its surroundings in the early frames.
  - **Text fix (run28):** unpaired screen quads (dropped by the consistency rule, or new) get a synthetic previous UP command, the same quad shifted by the median displacement of the consistent quads within 64 px (`g_screenSynth`, `BuildScreenSynth`, `Pair.upSynth`).
  - Test: scene 10 with arg 2 (`testapp 5 0 0 0 2`: m and E trade atlas slots every 7 ticks while the label slides). The old build breaks the label at the swap ticks; the new one keeps it intact.
- **Shadow dropout, likely fix (run29, untested in game).** Constants other than position were paired by command order in each draw's segment.
  - Values uploaded once and read by many later draws (cascade lookups gDepthProj c182/c202/c206, light colours) were left unblended whenever the draw right after the upload had a different structure last tick.
  - run25 clerk dump (`tools\liveunpaired.py`): ~1,000 matched draws per frame read an unpaired c182, and 124 an unpaired c202/c206. An unblended lookup against a blended shadow map fails the shadow test, which reads as shadows off.
  - **Fix:** `VSInfo.reads` and `PSInfo` (pixel-shader read analysis, `GetPSInfo`) feed `CollectLive`, the uploads each draw reads. `PairLive` pairs each with the counterpart draw's upload for the same slot. `PairSegment` now only handles what no analysable reader covered (and XFORM/UP).
  - Hardening: `Replay` skips stale or duplicate pairs instead of stalling.
  - Test: scene 14 (`testapp 5 0 0 0 0 0 0 0 1`, `rampcheck.py`). Old build: mean 1.61 / max 3.59 levels; new: 0.35 / 0.96.
- Earlier tool, **self-check ablation**: a bad self-check is re-predicted with one mechanism off at a time (PS constants, smooth constants, UP geometry, FFP transforms, slerp, camera estimates, screen fixes, camera check). The log line `selfcheck #N ablation:` gives the mean error and brightness bias for each variant.

### Overnight 2026-10-07: release build, faster pairing, MHFZ Plus package

**Mod hardening (`src\fps.cpp`)**
- Release-safe defaults: `retro=0` (the recorder costs ~1 GB of video memory) and `debug_keys=0`. The release ini (`package\fps\mhfz-fps.ini`) keeps only the Pause on/off key. The dev ini in `<MezeLounge folder>` now sets `retro=1` and `debug_keys=1` explicitly.
- `debug_keys=0` turns off Ctrl+Shift+F7..F12 and the debug/mode/capture keys.
- Crash note (`CrashNote`, vectored handler, at most 8 notes): logs faults inside the DLL, faults with a return address into the mod on the stack (e.g. msvcrt memcpy called from a hook), and faults while drawing an in-between frame. It gives module+offset, the mod offset and the last D3D call. It only logs; the game's own handling continues. Test: `test\crashtest.cpp`.
- **Overlay re-entry guard.** An overlay that hooks the device vtable after us keeps our hook as its original. `CheckHooks` then re-hooks on top (`o_Present` = the overlay), and the two called each other: a stack overflow.
  - `Present`, `Reset` and `EndScene` now go straight to the runtime function (recorded at the first hook) when re-entered.
  - `test\overlaytest.cpp`: run29 dies with 0xC00000FD; the new build runs, and the overlay still gets its calls.
- **FPS overlay (2026-10-07 evening)**, `DrawOverlay`:
  - Insert (`key_overlay`, works in release) or `overlay=1` shows it.
  - Content: true presented fps (game + in-between frames), the game's own rate, target, average/max frame time and jitter over the last second, pair/logic ms and extra frames per tick (EMA), and a graph of the last 240 frame intervals (0–25 ms; blue game frame, green in-between frame, red hitch; lines at the target and 60 fps).
  - GDI-rendered glyph atlas (managed texture), one DPUP per frame, drawn with the original functions right before each Present (after captures and self-check copies). State is restored with a state block, plus RT/DS/viewport/scissor.
  - Verified: pair hashes identical with `overlay=1` (`pairhash.ps1 -Ini overlay=1`), self-check identical with it on and off. Screenshot: `tools\overlayshot.ps1` (PrintWindow, no key presses).
  - Also fixed: the first tick's logic time was measured from 0 (the "logic 4519187 ms" in the first stats window).
- **Minimized window = vanilla:** no in-between frames while `IsIconic`, so no GPU work. The stats show `minimized: N ticks`. Test: `tools\mintest.ps1`.
- `mhfz-fps.prev.log` keeps the previous session's log, so a crash report survives a restart. The log header names the build time. The DLL carries a version resource (`tools\mkres.py` writes the .res files; ld.lld links them, no rc needed).
- **Pairing cost**, measured with test scene 15 (`testapp N 0 0 0 0 0 0 0 0 1`), a town-sized load: ~1,900 draws, 3,000+ VSCF, 3,700 PSCF, 670 glyph quads. It reproduces the game's numbers: pairing 5.1 ms/tick, 145 fps at a 170 target.
  - Profiled with the sampling build (`build.ps1 -Profile`, `tools\prof.py`): half of `BuildPairs` was the two "sort all n×m edges, take greedily" assignments.
  - `GreedyAssign` computes the same greedy with per-row heaps and lazy popping. Ties are now ordered (cost, row, column) instead of left to `std::sort`.
  - `RefineScreenGroups` uses a row-sorted index (`ScreenIdx`) instead of a 3×3 hash-grid scan, and skips `nth_element` when all neighbour values are equal.
  - Exact floor tests replace the floorf calls (`IsInt`, `Quantized`).
  - Camera candidates are lazy (`CamCand`): an invertibility check on the left half only, and the matrix only for the ≤96 sampled ones.
  - Result: 5.1 → 3.65 ms/tick (−28%).
  - **Every step was verified bit-identical:** `debug_pairhash=1` logs a hash of pairs/matches/flags per tick, and `tools\pairhash.ps1` records all 12 scenes (1,854 ticks) and compares against `build\ph-baseline`.
  - The stats line now has `pair phases (ms/tick)` (setup, owners, live, group, refine, verify, pair, cam+synth, screen, sort, r.groups, r.consist, r.setup), which will show the real distribution in game. Benchmark: `tools\pairbench.ps1 -Dlls a.dll,b.dll`.

**MHFZ Plus (launcher + installer, `launcher\`, `package\`, `tools\package.ps1` → `dist\MHFZ-Plus-1.0.zip`)**
- `MHFZ-Plus.exe` replaces `play.cmd` / `play-rain.cmd`: one GUI-less exe per session, `meze` or `rain` as argument, so a Steam non-Steam shortcut works.
  - The English patch ships as deltas against the player's own originals (`english\<server>\manifest.txt` + `.delta`, format in `launcher.cpp`). They are built into `cache\` on first use.
  - **Never overwrites a game file it doesn't recognise** (`StateOf`: our original or our English by stamp, else by hash). A game update leaves the changed file alone and logs it.
  - Cache and backup are hash-checked on every start, so a new patch version or a damaged file gets rebuilt.
  - `english=safe` limits the patch to the set proven in game (mhfdat, mhfpac, mhfinf, stage\*.pac).
  - `all --restore` puts every original back.
  - Write-access check with a clear message (games under Program Files). The manifest sets asInvoker, so no UAC virtualization.
  - Rain: exits 10 s after the session (60 s before the game has started).
- `Install.cmd` → `setup\Install.ps1` (WinForms):
  - finds MezeLounge and Rain (the Rain uninstall registry entry);
  - imports the old kit's backups after a hash check;
  - prepares the English files;
  - creates the Start menu folder (games, "Put the original game files back", settings folder, uninstall), desktop shortcuts and an Apps & features entry;
  - adds the Steam shortcuts (`SteamShortcuts.ps1`: binary VDF; ours are found by the `MHFZ-Plus.exe` name; ids are kept on re-install; artwork from an existing Frontier shortcut is copied).
  - `Uninstall.ps1` restores, removes everything and refuses if any original can't be put back.
- **After a translation update**, regenerate the deltas, then run `tools\package.ps1`:
  - `python tools\mkdelta.py <translation kit>\backup <translation kit>\patch dist-work\english\meze`
  - the same with `backup-rain` / `patch-rain` → `dist-work\english\rain`
- Tests:
  - `launcher\simtest.ps1`: simulated Meze/Rain programs (`launcher\fakegame.cpp`).
  - `launcher\simtest2.ps1`: game updates on both servers, stale cache, old-kit English files, safe mode, restore-all.
  - `tools\installtest.ps1`: zip → install → re-install elsewhere → play both → uninstall, against the sim and a copy of the Steam config.
  - All pass. A read-only `--prepare` against the real `<MezeLounge folder>` and `<Rain folder>` built all 41 + 40 English files.

### 2026-10-08: text identity, light slots, faster matcher, repeated objects (night9–night16)

**Text (night9, night11).** MHF rewrites its 256² glyph atlas every frame and re-slots letters when new text needs
room, so a glyph's texture coordinates are no identity across ticks.
- Strings that lose their glyph keys are matched by layout (count, height, spacing, colour, edge clipping), never letter
  by letter.
- `text_ckeys=1`: glyphs are known by their pixels. `AtlasSnapshot` copies small managed 32-bit textures that screen
  quads sample, refreshed after any texture lock. `TextKeys` hashes each glyph's cell (one texel border shrunk).
- In the maintainer's night10 log, a flick had 7–20 strings/tick matched by layout and 2.5–5.7 left unmatched: the shake.

**Brightness flash (night12).** PS c21–c23 (and c162) are per-draw point-light positions in view space. MHF re-sorts
them as the player moves, so a slot holds another lamp next tick; blending slid that light through the character at
t≈0.5.
- `ConstLerpMask` now switches (doesn't blend) a w=1 row that moves more than 0.6 of its length, or takes another row's
  previous xyz.
- Stats `slotJumps`, log "position slots that switched". On the 7 saves: 2,359 rows switched; none of the 1,225 with
  history had been moving steadily (pairbench `-jumps`).
- The rows: light positions, a colour toggling white↔blue (PS c170), light directions alternating per tick (VS c9–c11).

**Faster matcher (night14/15, bit-identical).** About 40% less time per tick (saves offline 2.0–3.5 → 1.2–2.1 ms;
in-mod test app town 1.08 → 0.71 ms). Changes:
- Text matching (+`TextKeys`) runs as one more task of the per-frame passes' `ParallelFor` (it shares no data with
  them). Text runs, glyph keys and screen positions of the previous frame are reused from last tick (by frame serial
  and draw count). The bigram index is a sorted flat array with positions. Nearest-16 for frequent pairs is an
  outward scan over an x-sorted copy. The field median uses kept orders (falls back to a sort when a sum ends within
  rounding of the half).
- `CollectOwners/CollectLive/ComputePosMasks` walk runs of registers with the same upload. `Owners.info` keeps each
  owner's start/count/ordinal (no command-header reads in `FirstMat4`/`FirstBones`).
- Flat group lists keyed by the same `unordered_map` insertion order: `SolveCameras` depends on the visiting order.
- `g_lastPairs` is an open-addressing `U32Table`. First readers are one `FrE` array indexed by offset/16 (uploads are
  ≥20 bytes). `SolveCameras` votes stop early. `FirstWitness` skips exact repeats. Row-bucketed `ScreenIdx` sort.
- Verification: pairbench `-hash` per tick over pairs, masks, text decisions and moved quads (`PairHash` covers all).
  `tools\fullhash.ps1` covers the retro saves (baseline `build\fh-n12`); `tools\pairhash.ps1` now has 15 scenes,
  including atlas/titles/crowd, and `-Only`. Plus `tools\benchmin.ps1` (min of runs, `-Threads 1` for CPU work),
  `tools\pairtime.ps1` (the mod's own `pair` ms per build) and `tools\profasm.py` (capstone disassembly with sampler
  counts).

**Camera estimate fails in MHF; `field_match` (night16, off by default, Ctrl+Shift+4).**
- `SolveCameras` votes on whole D = Mp·Mc⁻¹. MHF's per-object c38 matrices agree in their x/y columns but differ wildly
  in the z/w columns (per-object depth mapping, z≈w). Log: 0.1 of 3.0 contexts/tick get an estimate.
- Without one, repeated meshes pair by nearest previous position. In a pan larger than their spacing they cross:
  save 1322/003 tick 3682 draws 1248/1249, distant props, lag plus blended lights.
- `field_match`: pass-0 singles give a screen-motion field per context. `MatchGroup`, only without D, predicts each
  instance from its 5 nearest samples. The prediction is used if its cost is < 0.75× nearest and every pair is ≤ 4e-3
  NDC² (without that cap it made 2–6 NDC² pairs). Field groups skip the 16×median rejection.
- On the saves: no change in menus or still scenes; 2–21 re-pairs per tick in flicks, all nearer their prediction.
  Test app identical (D works there). Tools: pairbench `-field`, `-dumpmatch`, `-drawinfo T i,j`.
- Open: a screen-space vote for D (apply candidates to each other's origins), which would also enable new-object
  synthesis and the pair check in MHF.

**In-game timing (maintainer's night10 log):** matching 1.78 ms in the background, render thread waited 0.68 ms/tick; game
frame 6.3 ms GPU (shadow cascades at 3440²). That GPU latency makes the first in-between miss its slot: ~146 of 170 fps
in town.

### 2026-10-09 night: night20/21 (GPU wait bounds, DXVK start hang, input latch, camera probe)

- **Late ticks in menus (n20a/b):** the in-between loop no longer waits for the GPU past the game's deadline (`WaitQueryUntil` /
  `WaitGpuUntil`, `St.gpuWaitCut`; stats line `late N (x ms avg, GPU waits cut N)`).
- **DXVK start hang (n20b/c, caught before deploy):** also bounding the final drain made DXVK's Present of the following game
  frame never return in ~3% of test-app starts (always the 4th Present, while the first pipelines compile). Variants: drain cut
  only 9/100, loop cut only 0/100, night19 0/100. night21: loop bounded, drain unbounded (WaitGpu) - 0/200.
- **Pre-existing:** regrouptest with DXVK (regroup=2) hangs at start inside DXVK while the deferred frame executes at the first
  texture lock: night19 4/150 with the player's settings, 1/60 with pair_async=0; night21 0/150, 2/60. Probably this PC's
  display environment (test windows end up moved to 11550,1569 at 5700x1497); the maintainer hasn't seen it in game.
- **Watchdog:** also lists return addresses in the backend d3d9.dll, marked `d3d9.dll[backend]+0x..` (`ModOf`), so DXVK waits
  show up; `tools\wdsym.py` resolves every log line with mod addresses.
- **Input:** MHF reads DirectInput 8 once per tick (immediate GetDeviceState). Late ticks only lengthen a tick (the limiter sets
  `last = timeGetTime()` after its Sleep; the catch-up path only runs when the game's own work overran 33 ms with
  `[0x119dd750]` set), so they don't drop input. `input_latch=1` (off by default): GetDeviceState hooked in dinput8's device
  vtables (found via our own keyboard device, A and W; CreateDevice hooked for other vtables); the game's devices are sampled
  every ~1-2 ms while it sleeps (WaitQ, after each in-between Present, before WaitDeadline) and while it draws (DIP/DPUP hooks);
  a press seen down and up between two game reads is reported held at the next read, once (keyboard 256, mouse 16/20, joystick
  80/272 with POV). test\inputtest.exe (12 ms SendInput F13 taps): 33-36% seen without, 100% with (Windows d3d9 and DXVK when
  the loop runs at speed; DXVK test runs here sometimes drop to ~18 Hz with or without the mod's latch - environmental).
- **Camera:** camera object `[0x1edaad60]` eye +0x80 / target +0x8c / up +0x98, view built in `0x10baee10`. The pitch clamp was
  not found statically (16-bit angle clamps, minss/maxss/comiss on angle-like constants, direct eye/target writers = fixed and
  scripted cameras). Camera probe (debug keys, Ctrl+Shift+F6 / `camprobe_trigger`, `camprobe_s`): DR0/DR1 write breakpoints on
  eye.x/target.x on every thread + a VEH; logs write sites (static addresses) and the pitch range. Self-test (`camprobe_test=1`,
  watches g_heartbeat) names PresentBody, hk_Sleep and Interp exactly.
- **Keys:** combination keys read bit 15 only while their modifiers are held; F7-F12 need Ctrl+Shift and debug_keys; key names
  `num0-9`, `mouse4/5`, `mmb`, punctuation names; a single digit is that number key.
- **Release 1.2:** settings app (`settings\Settings.cs`), installer shortcut/update text/version, package `dist\MHFZ-Plus-1.2.zip`
  (`tools\installtest.ps1`: 0 failures). Open-source prep: README/docs/CONTRIBUTING/LICENSE (draft MIT), `tools\mkrepo.ps1`.
- **Tools:** `validate.ps1` (the release gate; run with pwsh), `starthang.ps1`, `regrouphang.ps1`, `featuretest.ps1`, `mkrepo.ps1`;
  `build.ps1 -Tests`; `re\ghidra_scripts\Decomp.java` (headless decompile of the functions containing given addresses).

### 2026-10-11 night: 1.4.0 gate, robustness pass (no game)

- **1.4.0 gate (n26 vs n25, the deployed 1.3.3):** DXVK starts 0/200, pairhash 1979 ticks identical, exits 0/6, regroup
  identical, selfcheck per scene within noise, stress clean (8 option sets), featuretest incl. the DXVK abort fallback,
  installtest 0 failures. (validate.ps1 from a worktree needs `build\mhfz-fps.ini`, `build\dxvk` copied from main's build.)
- **input_latch taps (inputtest, 66 taps of 12 ms):** without 32-38%, with 74-92% between runs on this PC; n25 and n26
  run the same input code (n26's only mod change is the backend abort guard) - environmental spread, see the repeats.
- **Launcher CPU:** a process list (Toolhelp) costs ~1.1 ms here; Rain's session loop did two plus the launcher log every
  10 ms (~20% of a core before the game, ~10% during it). Now: log every 10 ms until the game runs (the English files
  must beat it), processes every 100 ms, everything 4x/s in game; MezeLounge waits on the game's process handle. Don't
  cache the log by its directory entry: an open log's size/time there can lag (the check-passed line would be missed).
- **Launcher safety:** a backup goes back only if its hash is the original's (a damaged one would break the game);
  backups flushed before rename; client.exe counts only from the game folder (FinalPath: junctions/subst/8.3 resolved;
  launchers stay name-only - they may update from elsewhere); a thread-less process (ended, still held) isn't running;
  the game's exit code >= 0xC0000000 is logged "game crashed" (Rain's client quits with 102, MezeLounge 0).
- **Steam shortcuts.vdf:** Linux rewrote an unparsable file as empty + ours (every other non-Steam game gone). Both OSes
  now change it only if parse + write gives its exact bytes (the owner's real file does), write via a temp file.
- **App:** single instance (mutex), crash log MHFZ-Plus-App.log + plain message, OpenUrl https only, update page must
  be this repo's /releases/, Hub.cs split by page, `tools\apptest.ps1` (no window; caught --shots rewriting server=).
- **Mod settings:** retro_frames 8..600 (VRAM), retro_ticks 0..48, margin_us 0..30000 (a negative one would slow the tick).

### 2026-10-11: in-app update (asked for by a player)

- **Flow:** "Update now" (toast once per new version, footer, Help > Check for updates) -> GitHub API releases/latest
  -> the asset `MHFZ-Plus-<tag>.zip`, only from `<repo>/releases/download/` and only with GitHub's `digest`
  (sha256) -> download to %TEMP%\MHFZ-Plus-update, hash checked, unzipped -> hidden `setup\Install.ps1 -Unattended
  -Update -Relaunch -Target <install>` -> app closes; setup waits for it, installs with Keep (server, english/fps per
  game, the backend in fps\mhfz-fps.ini, desktop shortcut kept; Steam untouched), reopens the app -> "Updated to X"
  (ini updated_from; equal to the running version = it failed: said). Linux: `mhfz-plus update --wait --relaunch`
  (same checks, then the NEW package's `mhfzplus.py install --update` with MHFZPLUS_UPDATER = its pid, because a
  process started from Wine carries WINEPREFIX and prefix_in_use would see the updater itself).
- **Pitfalls found by the tests:** the app's folder ends in `\` (`-Target "C:\x\"` escapes the quote: trimmed);
  Windows PowerShell's Get-Content reads the BOM-less UTF-8 mhfz-fps.ini as ANSI (non-ASCII paths garbled: ReadAllLines);
  package.ps1 under Windows PowerShell 5.1 wrote zip names with `\` (Linux unzip keeps them in the names, the update
  refuses them) - the released zips were made under pwsh 7 and are fine; package.ps1 now writes `/` either way.
- **First release that has it:** the one after 1.4.2. Players on 1.4.2 and older update by hand once; from then on in-app.

### 2026-10-11: START through Steam (controllers)

- A player: with a controller plugged in (untouched), the game's menus acted up when started from the app; from
  MHFZ Plus's Steam entry they don't (Steam Input). So START (Windows) asks Steam to run that entry:
  `steam.exe steam://rungameid/<appid << 32 | 0x02000000>`, appid read from userdata\*\config\shortcuts.vdf (the
  logged-in Steam account's first: HKCU\Software\Valve\Steam\ActiveProcess\ActiveUser), only an entry whose Exe is this
  install's MHFZ-Plus.exe with LaunchOptions = the server. `start_steam` = auto (a controller seen by winmm
  joyGetPosEx or XInput) / 1 / 0. Not when the app itself runs under Steam (SteamGameId set) or Wine.
- The session is "asked of Steam" until MHFZ-Plus.exe runs AND its log is newer than the ask (the launcher rotates
  the log at start; the old one would read as "game closed"); 2 min without: "Start without Steam" toast.

### 2026-10-11: an update stopped by a file in use

- A player's Update now 1.4.3 -> 1.4.4: "The process cannot access the file ...\MHFZ-Plus-App.exe because it is
  being used by another process". The app had closed (setup waits for it), but something still had its exe open
  for a moment - not known which: maybe Steam (when the app runs as its Steam entry, Steam may read its icon as it
  stops) or an antivirus. The next failure's message names the holder.
  Copy-Item stopped at the first error: half the files new, half old. And the message's "run Install.cmd from
  %TEMP%\...\pkg" pointed at a folder the reopened app had just deleted.
- Now (Install.ps1 Copy-Files): file by file, a held file is retried for 20 s; still held, it's renamed aside
  (`*.old-xxxxxxxx`, allowed for a program file in use; removed by the next setup) and the new one put in its
  place; still locked (no rename either): stopped with the holder's name from the Restart Manager (rstrtmgr
  RmGetList). The app keeps the download when an update failed; its message offers Update now again.
- installtest: three update runs with MHFZ-Plus-App.exe held by another process (3 s; all along with FILE_SHARE_DELETE
  as a running image; all along locked).
## Debug tooling

- **Live config:** the mod re-reads `mhfz-fps.ini` whenever it changes on disk (checked twice a second). Changing `capture_trigger` or `dump_trigger` starts a capture or dump remotely. Isolation switches: `interp_vs`, `interp_xform`, `interp_up`, `interp_ps`, `lerp_nonpos`.
- **Self-check (`selfcheck=1`):**
  - Each real frame is copied to VRAM.
  - Every `selfcheck_interval_ms`, real frame M is predicted by replaying M+1 lerped halfway to M-1, then compared with M as presented.
  - It logs the mean error and the share of bad 16×16 blocks (block mean above 24/255).
  - Bad cases are saved as `selfcheck\NNNN_{predicted,real,heat}.jpg`.
  - On the test app it scores 0% bad blocks. With `interp_up=0` it scores 0.9%, and the heat map highlights the moving quad.
- **Analysis scripts** (`tools\`):
  - `magenta.py`: scans captures for debug-view magenta.
  - `notlerped.py` and `dumpstat.py`: dump analysis.
  - `vtcheck.py` and `peek.py`: live process inspection.

## Status / next

- [x] Proxy and recorder work in the test app: 30 Hz logic, 120 fps output, correct in-between frames.
- [x] MezeLounge in game (many rounds; current build deployed, run29 kept as `build\d3d9.run29.dll`).
- [ ] Faster pairing (overnight build): confirm in game. The `pair` ms and `pair phases` in the stats show the real distribution.
- [ ] Shadow dropout fix (run29 `PairLive`): confirm in game.
- [ ] Rain with the frame-rate mod. MHFZ Plus swaps `Client\d3d9.dll` in after the launcher's check (`fps_mode=swap`). Still to check: does the client load it (`mhfz-fps.log` in `Client\`), and is Rain's limiter still `Sleep` based?
- [ ] MHFZ Plus on the real machine: run `dist\MHFZ-Plus-1.0\Install.cmd`, then play both servers from the shortcuts and from Steam.
- [ ] night16 in game (2026-10-08 evening): World Select paging, text in flicks (pixel keys), character brightness
  (light slots), and Ctrl+Shift+4 `field_match` A/B on repeated props.
- [ ] Camera estimate by screen-space vote (see 2026-10-08), behind a key.
- [ ] First in-between frame waits for the game frame's 6.3 ms of GPU (~146 of 170 fps in town); separate topic.
- Ghidra project: `ghidra\mhfo.gpr` (fully analyzed). Ghidra is at `<Ghidra>`, with JDK 21 from Temurin.