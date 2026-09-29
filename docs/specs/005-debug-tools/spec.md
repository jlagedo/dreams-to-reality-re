# 005 — Retail debug tools recovery

Status: **Three debug views, a step override and the Dreams Editor (noclip,
menu tree, mouse cursor, record pages) run in the recomp through data pokes,
keypad toggles and two restored links; the editor's file pickers are broken in
the retail code; the recorder start and several debug draws stay unreachable**
Date: 2026-09-29
Depends on: [000 the recomp](../000-the-recomp/spec.md)

## Goal and boundary

Cryo shipped its engine debug layer. The retail Windows programs still contain
the developers' frame-rate and memory readout, an object inspector, a
collision-mesh view, a step override, a demo recorder and most of an in-game
editor. The code that switched them on was removed; the features were not.

This spec records what survives, the exact condition each feature waits on, and
how the recomp turns them on. Flags are set **by writing data only**. Two
removed links are restored by host glue, without changing any retail
instruction: the mouse-event producer, and one call to the editor draw at the
place the evidence puts it. Features that neither can reach are listed as dead
code, with what would be needed to run them.

Addresses are `WINDREAM.EXE` virtual addresses. `GDIDREAM.EXE`, which the recomp
runs, has the same code at the same addresses; every gate below was
disassembled in `GDIDREAM.EXE` (`recomp/windream/debug/x86dis.py`). Evidence
tags follow [the docs index](../../README.md#evidence-tags).

## Summary

| Feature | Retail state | Condition | Recomp |
|---|---|---|---|
| Frame Rate / Mem 3DTR readout | live, flag never written | `0x49d5c0 != 0`, editor flag `0x4a477c == 0` | keypad 1 |
| Object debug HUD | live, flag never written | `0x49d5d0 != 0` | keypad 2 |
| Collision view | live, flag never written | byte `0x4ac8c8 != 0`, then hold Backspace | keypad 3 |
| Step pinned at 2.0 | live, flag never written; the TGA frame capture it pairs with is dead | `0x4a4758 == 1` outside demo recording | keypad 4 |
| Collision wireframe alone | live, but painted over | hold Backspace | visible only with keypad 3 |
| Gameplay hotkeys incl. Ctrl+I+R | live | keys (below) | original keys |
| Dreams Editor (`DREAMS.DAT` editor) | gameplay branches live; draw, mouse producer and flag setter removed; file pickers broken; fill stubbed | `0x4a477c != 0`, draw `0x44d46d` called | keypad 5: flag plus a restored draw call; noclip, menu tree, cursor, record pages (owner-observed) |
| Editor BOX gizmo | inside the editor draw, before its flag test | working BOX record `+0xec & 1` | drawn whenever the editor draw runs (owner-observed) |
| Editor TGA frame capture | inside the editor draw | `0x4a4758` (every frame) or `0x4a475c` (once) | keypad 4 with keypad 5; `data\tga` must exist |
| Demo recorder | per-frame code live, start functions dead | `0x49d34a` = 0 record / 1 play / 2 normal | untested poke |
| Box wireframes, axis gizmo, post-scene callback | dead | no caller, no pointer | not reachable |
| DOS 3dfx profiler | calls present in `DREAMSFX.EXE` | unknown gate | not examined |

## How the retail build hides them **[verified]**

- Every flag above sits in initialised data (`DGROUP`) with the value 0, and
  no instruction in the program writes it by address. `flag_hunt.py` (below)
  lists 84 such tested-but-unwritten globals; after removing tables written
  through pointers (the key table, structure fields) and runtime-library
  state, the four live flags in the summary remain.
- The dead features have no caller and no copy of their address anywhere in
  the file, so neither a call nor a function pointer can reach them.
- Watcom links whole object files, so unused functions in a used `.obj` stay
  in the executable. The developers disabled features by removing calls and
  setters, not code. The discs show the same habit elsewhere: French error
  strings, 3D Studio leftovers, a Multi-Edit session file in `DATA\OBJET`
  ([engine.md](../../engine.md)).

## Live features

### Frame Rate / Mem 3DTR readout **[verified]**

`GAME_TickFrame` (`0x416d45`) draws it at `0x4172a2` when `0x49d5c0 != 0` and
the editor flag `0x4a477c == 0`. It runs after `GAME_Tick`, the HUD and the
object HUD, and before the next `VID_Swap`, so it lands on the finished frame.

| Row | Value |
|---|---|
| `Frame Rate` | fps estimate `0x5df480`, from the same 200 Hz measurement as Δt |
| second number (`%i`) | wall-clock seconds: 200 Hz counter (MGM message `0x11`) / 200 |
| third number (`%f`) | wall-clock seconds minus simulated seconds: `0x49d5c4` adds Δt/30 every frame, so this is how far the simulation lags real time |
| `Mem 3DTR` | `0x661e2c − 0x661ee0` |
| `Mem 3DTR Free` | `0x661e2c − 0x661ee4` |

The rows are 6 pixels apart from the object HUD's; with both on they overlap,
as in the retail layout.

### Object debug HUD **[verified]**

`DBG_DrawObjectInfo` (`0x416606`) returns at once unless `0x49d5d0 != 0`.
`GAME_TickFrame` calls it every gameplay frame, not while a video plays. It
shows the actor at `0x4fbb48` when that is set, otherwise the player
(`0x4fba78`): `Project Name`, `Object Name`, position, speed, physics speed,
flag words, angles, `Object 3D Col`, both animation channels (current and
requested action, frame, time), `Nombre d'objet` and `dernier objet`
(inventory count and last item). Field meanings are in
[file-formats.md](../../file-formats.md) and [models.md](../../models.md).

### Collision view **[verified in code and in the recomp]**

Two retail pieces combine:

1. **The wireframe.** `PHYS_ResolveCollisions` (`0x40bff8`) returns early
   unless `0x49d2e8 != 0` and the editor flag is 0; this gates collision
   resolution itself, not only the drawing. `0x49d2e8` is set when a model
   loads (`RES_ReadFile`, `ENT_LoadModel`) and cleared as `SCENE_LoadLevel`
   starts. Inside, when key-table byte `0x6308e0` (virtual-key 8, Backspace;
   `INPUT_PollKeyboard` stores `GetAsyncKeyState(i)` at `0x6308d8 + i`) has
   its held bit, it calls `DBG_DrawCollisionMesh(g_frameBuffer, [0x4fbdc8],
   1)` (`0x45f2a0`). That function transforms every triangle of the resource
   by the camera node, projects the three corners and draws the edges with
   the line drawer `0x465c80`. Its only output is pixels: it and its helpers
   write no global (the clipper `0x46569c` stores the screen height
   `0x661ec8` back unchanged), and nothing reads the frame buffer except
   presentation.
2. **Why it is invisible alone.** `GAME_DrawFrame` (`0x423f60`) calls
   `PHYS_ResolveCollisions` and then `REND_DrawFrame`, whose span flush
   repaints the whole 3D view. The colour argument 1 is also nearly black in
   the 16-bit frame buffer.
3. **The switch.** When byte `0x4ac8c8` is set, `REND_DrawFrame` (`0x459320`)
   and `REND_DrawFrameEx` install `SW_CollectFaceTriangles` (`0x478800`) as
   the object hook, clear the second hook and **skip the flush**. The collector
   records each face's projected corners into arrays at `0x6760e4`/`0x67f0e4`
   (counters `0x6808e4`/`0x6808e8`, reset by `REND_DrawScene`); nothing reads
   them. No bounds check was seen in the collector.

With both, the 3D view freezes on the last rasterized frame while the game
runs on; the HUD and text still update. Holding Backspace adds the collision
triangles from the current camera each frame, and because nothing clears the
view they accumulate. The lines trace the ground and the stepped platform of
Project 0 coarsely. The handle `0x4fbdc8` is offset `+0x350` of the player
actor `0x4fba78`; that it is the level's `.3DI` collision mesh
([scene-geometry.md](../../scene-geometry.md)) rather than a local subset is
**[unverified]**, as is whether the tree roots in Project 0 are solid.

### Step pinned at 2.0 **[verified]**

`GAME_TickFrame` computes Δt for the next frame (`0x5e5388`, a double;
[engine.md](../../engine.md#the-fixed-step-verified)). At `0x417268`, if
`0x4a4758 == 1` it stores 2.0, and at `0x417285`, if demo mode `0x49d34a == 0`
(recording) it stores 1.0. Demo mode starts at 2 and no reachable code changes
it, so in normal play the 2.0 pin takes effect. Animation, forces and timers
scale with Δt but the position step does not, so the game runs faster with
floatier jumps and less fall damage; it is not a clean fast-forward. At the
recomp's 25 fps cap the normal Δt is about 1.2.

**It is half of a frame-capture mode.** The dead editor draw `0x44d46d`
calls the TGA writer `0x4479c7` every frame while `0x4a4758 == 1`, or once
while `0x4a475c == 1` (then clears it). The writer stores the frame buffer,
read as RGB565, as a 24-bit top-down TGA named
`data\tga\<first 3 characters of project +0x60c><counter>.tga`, with a
four-digit counter `0x4a4754`. A fixed 2.0 step gives 15 captured frames per
game second. In retail only the step half is reachable; in the recomp the
capture runs while the editor draw does (keypad 5). The writer passes
`fopen`'s result straight to `fwrite`, so a missing `data\tga` folder is not
handled.

### Gameplay hotkeys **[verified in code]**

`GAME_HandleHotkeys` (`0x415aa7`) reads the key table (index = virtual-key)
and the last key event code `0x626fd8` (message `0x33`):

| Keys | Effect |
|---|---|
| F1–F6 | `VID_SetResolution(0..5)` |
| Ctrl+S | `0x4a3168`: "True Shadow" / "2D Shadow" |
| Ctrl+I+R | `0x49da14`: "Girl Power" / "Duncan"; needs letterbox off and no message showing |
| Tab | `MENU_OpenSpellMenu` |
| Alt+5 … Alt+9, Alt+0 | camera presets 0–5 ("Camera", "Camera 1" … "Camera 5") |
| F10 | key help (`0x49d5cc`) |
| F11 / F12 | letterbox off / on (`0x49d9f8`); the recomp host also toggles fullscreen on F11 |
| L | `MENU_OpenLoadPage` |
| Alt+X | quit (`0x4a4780`) |
| J / K | joystick (input mode 3) / keyboard (mode 0) |
| P | pause: toggles `0x49da20`, read by `GAME_Tick` and both follow cameras (owner-observed) |
| Esc / Space | stop a playing video |
| event code `0x97` | `MENU_OpenObjectPage` |

**Girl Power.** `SCENE_InitLevel` loads the player as `mhe.3dc` with its
animation set instead of `xh_.3dc` when the flag is set and the project's
player-model field `+0x8c` is empty, so the swap applies from the next level
load. `MHE` is the red-haired woman in [models.md](../../models.md). When the
sword (`epee.3dc`) is attached, `0x440f15` hides node `NATE02` for her.
`SCENE_CheckExits` reads the flag four times; those branches are not traced.
The string and `mhe.3dc` are also in `DREAMSFX.EXE` and `DREAMS.EXE`; their
key handling is not checked.

## Dead code present in the binary

### Dreams Editor **[verified in code]**

An in-game editor for the `DREAMS.DAT` project graph
([file-formats.md](../../file-formats.md)), in `0x4479c7`–`0x44d658`.

- **Flag.** `0x4a477c`. Its only write is 0, in `VID_SetResolution`. It is
  read 25 times: `CAM_ComputeChasePos` (2), `CAM_CollideEye`, `CAM_TickFree`,
  `GAME_Tick` (3), `GAME_TickFrame`, `ANIM_TickClip`, `ENT_ResetToSpawn` (2),
  `SCENE_CheckExits`, `ENT_TickPlayerGround`, `ENT_TickPlayerSwimming`,
  `ENT_TickPlayerFlying`, `PHYS_ResolveCollisions`, `PHYS_IntegrateMotion`,
  `ENT_AdaptActorColor` (5), `DDAT_LoadRecord`, `0x43cc32` and the editor draw.
- **Draw.** `0x44d46d` (no caller, no pointer): calls the TGA writer
  `0x4479c7` when `0x4a4758` or `0x4a475c` is 1, runs the BOX gizmo
  `0x44c440`, and with the editor on sets three floats at
  `0x4fbab0..0x4fbab8` to 100.0, blits the cursor at `0x4a314c`/`0x4a3150`
  with `0x4255d0`, and unless the page dispatcher `0x44c625()` has a page open
  prints "Dreams Editor" and the menu tree. `DREAMSFX.EXE` shows no editor
  module next to its `DREAMS.DAT` loader (`DDAT_Load` `0x31a54`,
  `DDAT_LoadRecord` `0x32578`); only Ghidra's function list was checked.
- **BOX gizmo.** `0x44c440` tests the working BOX record's `+0xec & 1`
  (`0x65b330`), adds an offset read from `0x4fbd48`/`0x4fbd50`/`0x4fbd58`
  (where actor slot 2 starts) to its corners `+0x0c..+0x14` and
  `+0x18..+0x20` (`0x65b250`–`0x65b264`), draws the box with `0x41bacb` and a
  list of `+0xe4` entries at `+0x24` with `0x41bed6`. It runs before the
  editor flag test. In the recomp it draws a blue wireframe box in the level,
  around an NPC on Project 0's platform (owner-observed); the owner reads the
  boxes as level-change or trigger volumes. What fills the working copy
  outside the editor, and what the offset is, are **[unverified]**.
- **Pages.** `0x44c625` runs one page at a time from about 30 flags
  (`0x4a46b4`–`0x4a474c`). Each record kind has create, edit, copy, list and
  confirm functions; creating one names it after the kind and a counter:
  **Project** (`0x449e7a`, working copy `0x65fb04`), **OBJET** (`0x44a517`),
  **LINKADVENT** (`0x44b0e5`), **LINK** (`0x44b74d`) and **BOX** (`0x44becb`,
  working copy `0x65b244`; its min/max corners are what the gizmo draws).
  Pages seen in the recomp: "LOAD LINKADVENTURE" and "LOAD MESH" (`0x44a6e4`),
  each a scrolling list with `UP`, `DOWN` and `EXIT` rows. `0x447b72` and
  `0x447e7c` write text files with `fprintf` (not examined).
- **File pickers are broken in the retail code.** The fillers `0x4486bd`
  (`data\3dc\*.3dc`, `*.dan`, `*.dsn`), `0x4487ed` (`data\hnm\*.ubb`,
  `*.Hnm`), `0x448937` (`data\anim\*.hnm`, `data\sym\*.sym`) and `0x4489ab`
  (`data\sym\*.sym`) have no caller. They and the list pages both address
  entry *i* as `0x661e21 + i`: a 1-byte stride, so each name would overwrite
  the previous one but for its first letter. The buffer is not a list either:
  `0x661e2c`, eleven bytes on, is the live memory pointer the Mem 3DTR
  readout shows. With the list empty, a page's selection stays -1; confirming
  (a click in the title row, or Space) copies the string at `0x661e20` into
  the record. In the recomp, `Z` (new OBJET) then "LOAD MESH" confirm led to
  opening `data\3dc` with an empty name at the install and CD roots and the
  game's fatal "Dreams Internal Error : Read file Error" (owner-observed,
  file log in `stderr.txt`).
- **Developer paths.** `BF_Mount` (`0x43ad40`) opens a `.BF` archive by name,
  or, if that name does not exist, as `Z:\<name>` (string `0x4c550d`), then
  checks the `UBIK` magic ("%s is not a valid file"). Next to the editor's
  strings, 20 DOS copy targets under `D:\CD1\DATA` and `D:\CD2\DATA` (`3DC`,
  `ANIM`, `HNM`; `0x4c5b4b`–`0x4c5dcf`) stage files for the two CD masters,
  such as `COPY DATA\3DC\*.3DC D:\CD2\DATA\3DC` and
  `COPY DATA\HNM\GENERIC.* D:\CD1\DATA\HNM`. Their user is not traced.
- **Live edits.** With no page open, `0x44c625` copies the working project's
  `+0x18`, `+0x24` and `+0x30` RGB fields into the live lighting globals
  every frame, so lighting edits show at once. Loading or confirming a
  project (`Q`, `0x4a46bc`; `0x4a46b4`) points the current-project pointer
  `0x661e04` at the working copy and calls `SCENE_LoadLevel`.
- **Keys.** `0x44c28c` maps the last key code (`0x626fd8`) through a 30-byte
  table at `0x44c1f7` and a jump table at `0x44c214`. Letters and digits open
  pages (`A` new Project, `Q` load Project, `Z` new OBJET, `E` new LINK, `R`
  the fifth kind, `T` new LINKADVENT, `1`–`6` and others); PageUp, Left,
  Delete and a few other codes call the record copy helpers
  `0x448a1f`–`0x448c0a`, probably copy and paste.
- **Gameplay while the flag is set** (from the code after each test):

| Where | Editor-mode effect |
|---|---|
| `PHYS_ResolveCollisions` | returns at once: no collision (and no Backspace wireframe) |
| `PHYS_IntegrateMotion` | the velocity damping factor is 0, so velocity does not carry over between frames: no momentum, falls do not build up |
| `ANIM_TickClip` | root-motion deltas shifted left 3 instead of 1: movement ×4 |
| `CAM_CollideEye` | no camera collision |
| `CAM_ComputeChasePos` | the easing term `0x52c875` is zeroed (no lag); a second test skips a clamp |
| `CAM_TickFree` | skips the free-camera key handling |
| `SCENE_CheckExits` | no level exits |
| `ENT_TickPlayerGround`/`Swimming`/`Flying` | skip the block on input bits `0x49d312 & 5` with a target `0x4fbb48` set |
| `GAME_Tick` | skips `ENT_TickTransform` and the `END.DSN` special case; every tick calls `ENT_ResetToSpawn` for actors 2–15 of the table `0x4fb7a8` (stride `0x2d0`; slot 1 at `0x4fba78` is the player) whose `+0xa8` bit 0 is set and `+0xa9` bit `0x40` clear, so NPCs stay pinned at their `OBJET` positions (owner-observed: they snap back and hold one animation) |
| `ENT_ResetToSpawn` | no random path-point placement; skips a flag test |
| `ENT_AdaptActorColor` | samples the frame buffer (its pixel reader is a stub) |
| `0x43cc32` | calls `0x43ca25` (which reaches the stubbed plot `0x40224d`) |
| `DDAT_LoadRecord` | skips a 150-entry loop over `0x633bf4` |
| `GAME_TickFrame` | hides the Frame Rate readout |

  So the flag alone makes a free-moving, no-collision, no-exit mode. Set in
  the recomp (keypad 5), it plays as a noclip mode (owner-observed
  2026-09-29).
- **Menu tree.** Nodes are 0x40 bytes: name at `+0x00`, five child pointers
  at `+0x18`, a value pointer at `+0x2c`, flags at `+0x3c` (bit 0 expands a
  child's subtree). `0x44d346` lists a node's children as rows 10 pixels
  apart and recurses into expanded ones; a node with a value pointer is a
  leaf, drawn and edited by `0x44cd62` at `x + 0xb4`. A row is picked when
  button bit `0x4a315c & 1` and `0x4a3160` are set and `0x4a3150` lies in the
  row. The shipped tree is static and holds two nodes: the root
  "Dreams Editor" (`0x4a47c4`) and its only child "Exit To DOS"
  (`0x4a4784`, flags 6), whose value is the quit flag `0x4a4780`. No code
  writes the root's other four child slots, so the tree was a panel of
  adjustable variables with only its exit entry left. The recomp shows
  exactly these two rows (owner-observed).
- **Mouse: consumers kept, producer removed.** `GAME_TickFrame` and the
  live boot/menu dispatcher `CTRL_Dispatcher` (`0x40e75c`) decode the same
  events: `0x34` = cursor `x << 16 | y` into `0x4a314c`/`0x4a3150` and signed
  deltas `dx << 16 | dy` into `0x4a3154`/`0x4a3158`; `0x35`/`0x36` = left
  button down/up (`0x4a315c` bit 0 and `0x4a3160`); `0x37`/`0x38` = right
  button down/up (bit 1). The editor draw blits the cursor with `0x4255d0(10,
  1, x, y)` (`SPR_GetDescriptor` + `SPR_BlitSprite`); `GAME_Init` loads
  `data\objet\sour.alp`, the two-frame cursor sheet (*souris*), probably
  this set. With the recomp's mouse events the cursor follows the mouse
  (owner-observed). Every editor page hit-tests the cursor's Y against button rows
  (`0x1e`, `0x28`, `0x32`, `0x82`, `0x8c`, `0x96`, `0xa0`). Nothing produces
  the events: no `MGM_PostMessage` call posts `0x34`–`0x38`, the window
  procedure `0x44627b` passes every mouse message to `DefWindowProcA`, the
  only mouse-related imports are `LoadCursorA` and `ShowCursor`, and
  `DREAMSFX.EXE` has no `int 33h` (its real-mode interrupt helpers serve only
  the Watcom graphics library). The joystick event `0x39` writes the same
  delta and button globals. **[verified in code]**
- **Stubs.** The rectangle fill `0x402406` used by `0x44cd62` and `0x44d658`
  is a bare `RET`; `0x44d632` → `0x44d658` has no caller.

### Where the editor draw was called **[unverified]**

An inference from the verified code facts below. No leftover instruction
marks the removed call; the Windows build is
unoptimized, so a deleted call leaves no gap. The tail of `GAME_TickFrame`
places it before `GAME_HandleHotkeys` (`0x41743a`):

- The tail already handles the editor module's globals (`0x4a46b4`–`0x4a4780`):
  the capture flag `0x4a4758` (Δt pin), the editor flag `0x4a477c` (hides
  the readout) and the quit flag `0x4a4780`, the "Exit To DOS" leaf's value,
  which Alt+X also sets.
- The editor draw applies its own flag tests, which suits an unconditional
  call from the shared tail.
- The Frame Rate readout, drawn just before, gives way to the editor, which
  draws at the same top-left position.
- The editor's key handler reads the frame's key code `0x626fd8`, which
  `GAME_TickFrame` clears on entry and sets while processing messages;
  `GAME_HandleHotkeys` reads it at the same point.
- Frame capture needs the finished frame (3D, HUD, debug text) before
  presentation, and the editor's `SCENE_LoadLevel` calls belong at frame
  level, as in `GAME_Tick`, not inside the renderer.

The other candidate is directly after `DBG_DrawObjectInfo` (`0x41708c`).

A data-only route exists: the post-scene callback `0x4aa704`, which
`REND_DrawFrame`/`Ex` call when non-zero and whose only writer is the dead
`0x45842c`, accepts the argument-less editor draw (`--poke
0x4aa704=0x44d46d`, tested without a crash). It runs inside the renderer,
before the span flush repaints the 3D view, and again during the shadow
render, so the restored call replaces it. The live code calls through only
four game pointer slots: `0x4aa704`, the span flush `0x4aa708`, the object
hooks `0x4ac8cc`/`0x4ac8d0` (rewritten every frame) and the event handler
`0x626f74`; the rest belong to the Watcom runtime.

### Demo recorder start **[verified]**

Demo mode `0x49d34a`: 0 records (`DEMO_RecordFrame`, 6,144 frames, then
`DEMO_SaveReplay` writes `data\replay.bin` and sets 2), 1 plays back
(`DEMO_PlayFrame`, `DEMO_StopPlayback`), 2 is normal play and the initial
value. The start-recording function `0x40db80` and start-playback `0x40dac2`
(only called from `0x40ee88`) have no reachable caller and no pointer.

### Debug draws with no caller **[verified in code]**

All call the line drawer `0x465c80`; none has a caller or an address copy.

| Function | What it draws |
|---|---|
| `0x46115c` (from `0x4614b0`), `0x461334` | 12 edges each: boxes, next to `PHYS_UpdateNodeBoxes` (`0x4610e8`) **[box reading unverified]** |
| `0x458434` | axis gizmo: `(framebuffer, position, 3×3 matrix)`, three 100-unit axes in `0xf800`, `0x1fe0`, `0x00ff` with white end dots (`0x465ea4`) |
| `0x45842c` | setter of the post-scene callback `0x4aa704`, which `REND_DrawFrame`/`Ex` call when non-null; the gizmo's arguments do not fit that call |
| `0x45e950` (from `0x45ea00`, `0x45f104`, `0x45f264`) | not examined |
| `0x41b9d5` (from `0x41bacb`, `0x41bed6` via the editor's `0x44c440`/`0x44c51f`) | editor helper lines |

Other bare-`RET` stubs: `0x4020a8`/`0x4020ce` (the pixel reads of
`ENT_AdaptActorColor`), `0x40224d` (plots from `0x43ca8e`, no caller),
`0x44d920`.

### DOS 3dfx profiler **[verified in code; gate unknown]**

`DREAMSFX.EXE` has eight timer slots (`0x967c4` start, `0x9680c` stop,
`0x9677c` reset, `0x96748` calibration) around `REND_DrawFrame`/`Ex`, the
renderer init `0x70d04`, `0x6a770`–`0x6a964`, `0x7480c` and `0x757a4`. The
bar graph `0x96920` and `"%4.1f"` fps text `0x969b8` are in
`REND_DrawFrame`'s call list (feature export), while its decompiled C omits
them; the condition is not traced.

## Recomp support

Host glue in `recomp/windream/`. The lifted code is unchanged except for one
inserted runtime call.

- `WD_POKE="va=value,..."` (`runtime/runtime.c`, `apply_pokes`): dword writes
  into the loaded image after loading and before the entry point; addresses
  outside the image are rejected; each write is logged to `stderr.txt`.
- `run.py --poke VA=VALUE` (repeatable) fills `WD_POKE`; `run.py --overlays`
  pokes `0x49d5c0`, `0x49d5d0` and `0x4ac8c8` to 1. The byte flag is poked as
  a dword; its next three bytes start at 0.
- Keypad toggles (`runtime/user.c`, `g_debug_keys`), by scancode so Num Lock
  does not matter; the keys are not passed to the game (with Num Lock off,
  keypad 2 is no longer an alternative Down arrow):

| Key | Flag | Plain `run.py` | `--overlays` |
|---|---|---|---|
| keypad 1 | `0x49d5c0` readout | off | on |
| keypad 2 | `0x49d5d0` object HUD | off | on |
| keypad 3 | byte `0x4ac8c8` collision view | off | on |
| keypad 4 | `0x4a4758` step 2.0 | off | off |
| keypad 5 | `0x4a477c` editor flag, which also enables the editor draw (F1–F6 clear it) | off | off |

- Mouse (`runtime/user.c`, `mouse_post`): the runtime supplies the missing
  producer. SDL motion and left/right buttons, mapped to game-frame
  coordinates, are posted as `0x34`–`0x38` into the standing handler's queue
  `0x626f70` with the game's `MGM_PostMessage`, from the `PeekMessageA`
  bridge before the pump dispatches. Only while the handler is
  `GAME_TickFrame` or `CTRL_Dispatcher`, the two checked decoders; the
  router `MGM_DispatchMessages` would treat these types as fatal. The deltas
  also reach the joystick globals `0x4a3154`/`0x4a3158`, which the free
  camera and joystick input mode read: in play the mouse steers like a
  joystick (owner-observed).
- Editor call (`lift.py` `CALLS`, `runtime/user.c` `wd_editor_frame`):
  `lift.py` inserts `wd_editor_frame()` before the `GAME_HandleHotkeys` call
  at `0x41743a`, in `GAME_TickFrame` and in the two fragments the lifter makes
  from its jump targets; each call follows the `L_0041743A` label, so the
  jump that skips the readout also reaches it. `wd_editor_frame` calls
  `0x44d46d` while `0x4a477c` is set, through `guest_call_regs`
  (`runtime/runtime.c`), which restores every guest register. `CALLS` inserts
  before an instruction like `PROBES`, but may run guest code.
- Editor keys also reach the game's own hotkeys (`L`, `J`, `K`, `P`, digits).
  Picker pages and record creation hit the retail picker defect above; `Q`
  and confirming a project reload the level.

Pokes of code bytes have no effect in the recomp: instruction operands are
compiled into the lifted C, except the self-modifying sites `lift.py` lists.

## Reproduce

```sh
uv run --with capstone --with pefile python recomp/windream/debug/flag_hunt.py
uv run --with capstone --with pefile python recomp/windream/debug/flag_hunt.py --ptr 44d46d 458434 40db80
uv run --with capstone --with pefile python recomp/windream/debug/x86dis.py 40bff8-40c050 459320-4593a4 417268-4172c0
uv run python tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis -readOnly -postScript Inspect.java refs:004a477c refs:0049d34a
uv run python recomp/windream/run.py --overlays
uv run python recomp/windream/run.py --overlays --seconds 50 --snap-ms 4000 --keys 2000:ESC,9000:RETURN,36000:BACK,36400:BACK
```

The last run reaches Project 0 and taps Backspace; snapshots land in
`out/recomp/windream/run/`.

## Work items

1. **Names.** Register the functions above (`DBG_`, `DEMO_`, the editor) in
   `re/names/WINDREAM.EXE.tsv` with two sources each and run
   `tools/check_names.py`; nothing here is renamed in Ghidra yet.
2. **Editor.** Running in the recomp. Still open: which pages are safe
   without a file list, each page's fields, the text writers
   `0x447b72`/`0x447e7c` and the `D:\CD1`/`D:\CD2` copy commands' user,
   what fills the working BOX record outside the editor, and the gizmo's
   offset and point list.
3. **Editor file pickers (on hold).** Either leave them and use the
   editor for inspection and editing existing fields, or give the picker
   pages a real file list in the runtime. The 1-byte indexing is compiled
   into every picker page, so the second means replacing the fillers and the
   list parts of those pages with runtime code.
4. **Reachability pass.** Find flags whose only writers are dead code, as
   `0x4a477c`'s effectively is: `flag_hunt.py` only lists flags with no
   writer at all.
5. **Demo recorder.** Poke `0x49d34a=0` at start and check that a
   `data\replay.bin` appears in the sandbox after 6,144 frames; playback needs
   the loading done by the dead `0x40dac2`.
6. **Collision view over the live scene.** Draw the retail wireframe again
   after `REND_DrawFrame` in a visible colour. This needs a host hook, so it
   leaves the pokes-only boundary; opt-in only.
7. **DOS profiler.** Trace its gate in `DREAMSFX.EXE` and try it under DOSBox
   Staging.
8. **OpenDreams.** Carry the readout, object HUD and collision view into the
   runtime's debug overlay as retail-derived views ([north-star.md](../../north-star.md)).

## Open questions

- Is `0x4fbdc8` (player actor `+0x350`) always the level's `.3DI` mesh?
- The editor was mouse-driven, but no retail build keeps a mouse reader.
  Did the developers' build read it through Windows messages or DOS
  `int 33h`? The cursor sprite set is probably `SOUR.ALP`; confirm the set
  number `GAME_Init` assigns.
- Was the editor draw called before `GAME_HandleHotkeys` or after
  `DBG_DrawObjectInfo`? The recomp uses the first.
- What fills the working BOX record `0x65b244` in normal play, what is the
  gizmo's offset at `0x4fbd48`, and what does `0x41bed6` draw from the
  record's list?
- Who runs the `D:\CD1`/`D:\CD2` copy commands, and did the picker defect
  exist in the developers' build too?
- What do `SCENE_CheckExits`'s four Girl Power branches change?
- Can the triangle collector overrun its arrays in larger scenes?

## File map

| Path | Content |
|---|---|
| `recomp/windream/runtime/runtime.c` | `apply_pokes` (`WD_POKE`), `guest_call_regs` (register call into lifted code) |
| `recomp/windream/runtime/user.c` | `g_debug_keys`, `debug_toggle` (keypad 1–5), `mouse_post` (events `0x34`–`0x38`), `wd_editor_frame` |
| `recomp/windream/lift.py` | `CALLS`: runtime calls inserted before an instruction (`0x41743a` → `wd_editor_frame`) |
| `recomp/windream/run.py` | `--overlays`, `--poke` |
| `recomp/windream/debug/flag_hunt.py` | tested-but-unwritten flag scan; `--ptr` address-copy scan |
| `recomp/windream/debug/x86dis.py` | disassembly of `GDIDREAM.EXE` ranges |
