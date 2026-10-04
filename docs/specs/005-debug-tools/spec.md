# 005 — Retail debug tools recovery

> **Note, 2026-09-30.** The hand-written OpenDreams port was removed (tag
> `opendreams-final`); the recomp is the project. Steps that carry these tools
> into OpenDreams or ODRuntime no longer apply.

> **Note, 2026-10-03.** The July 1997 demo keeps the complete editor with its
> original names (`WORKS.C`). [cryo-editor.md](../../research/cryo-editor.md)
> describes it end to end and answers several open questions below (mouse
> source, copy commands, BOX working copy, gizmo offset, key codes); the
> affected paragraphs are marked "Update 2026-10-03".

> **Note, 2026-10-03 (audit).** Four static audits of the July names, every
> July and retail trigger, the retail dead code and flags, and the engine
> library and builds found more tools than this spec listed (free-fly and
> overhead cameras, a give-all-items flag, the BEN11 collision displays and
> profiler) and corrected several readings. They are collected in
> [Developer-tool inventory](#developer-tool-inventory-audit-of-2026-10-03);
> corrected paragraphs are marked "Audit 2026-10-03". The launch mode that
> turns these tools on for players, Develop, is defined in
> [spec 008](../008-editor-restoration/spec.md).

> **Note, 2026-10-04 (traces).** Four traces for spec 008's phase 7 (the
> demo recorder, give all items, the Save page, save loading), static and in
> part live through the control channel, answer the open points those tools
> had; the affected paragraphs are marked "Trace 2026-10-04". Scratch and
> reports: `out/research/phase7-traces/` (local).

Status: **Three debug views, a step override and the Dreams Editor (noclip,
menu tree, mouse cursor, record pages) run in the recomp through data pokes,
keypad toggles and two restored links; the editor's file pickers are broken in
the retail code; the recorder start and several debug draws stay unreachable;
the 2026-10-03 audit inventories every surviving developer tool; the
2026-10-04 traces settle the recorder, give all items, the Save page and save
loading**
Date: 2026-09-29; audit 2026-10-03; traces 2026-10-04
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
| Editor BOX gizmo | inside the editor draw, before its flag test | working BOX record `+0xec & 1` | runs with the editor draw; the working BOX is empty until an editor page fills it, so the box seen is most likely `ShowLink_`'s exit volume (update 2026-10-03) |
| Editor TGA frame capture | inside the editor draw | `0x4a4758` (every frame) or `0x4a475c` (once) | keypad 4 with keypad 5; `data\tga` must exist |
| Demo recorder | per-frame code, save and stop live; start and load functions dead | `0x49d34a` = 0 record / 1 play / 2 normal | recorded and played back through memory writes alone (trace 2026-10-04) |
| Free-fly and overhead cameras (audit 2026-10-03) | live, message never posted | camera message `0x31` / `0x30` | not yet |
| Give all items (audit 2026-10-03) | live, flag only ever cleared | `0x49d5e0 != 0` | a poke, checked live (trace 2026-10-04); renames items already held |
| HUD on/off (audit 2026-10-03) | live, flag stuck at 1 | `0x49d5d4 == 0` hides the HUD | not yet (a poke) |
| BEN11 debug draws (`Display_*`, `Aff_Box_`, `Display_Frame_`), `New_PreRender_` | dead | no caller, no pointer | not reachable |
| BEN11 profiler | DOS 3dfx: calls present; Windows: module present, uncalled | DOS: `_rendertype` `0x104f38` | not reachable |

## How the retail build hides them **[verified]**

- Every flag above sits in initialised data (`DGROUP`) with the value 0, and
  no instruction in the program writes it by address. `flag_hunt.py` (below)
  lists 84 such tested-but-unwritten globals; after removing tables written
  through pointers (the key table, structure fields) and runtime-library
  state, the four live flags in the summary remain. *Audit 2026-10-03:* that
  count is too low. `flag_hunt.py` counts a clear as a writer and finds only
  `cmp`/`test` reads, so it misses flags whose only writer clears them
  (`0x49d5e0`, give all items) and flags read by `mov` then a call
  (`0x49d5d4`, HUD on/off). The audit lists at least 13 more never-set or
  stuck flags in game code, 2 more in the library
  (`out/research/devtools-audit/C/dead_flags.tsv`, local); the inventory below
  names them. **[verified in code]**
- The dead features have no caller and no copy of their address anywhere in
  the file, so neither a call nor a function pointer can reach them.
- Watcom links whole object files, so unused functions in a used `.obj` stay
  in the executable. The developers disabled features by removing calls and
  setters, not code. The discs show the same habit elsewhere: French error
  strings, 3D Studio leftovers, a Multi-Edit session file in `DATA\OBJET`
  ([engine.md](../../research/engine.md)).

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
| `Mem 3DTR` | `0x661e2c − 0x661ee0` (`_Mem_fin − _Mem_debut`) |
| `Mem 3DTR Free` | `0x661e2c − 0x661ee4` (`_Mem_fin − _Mem_libre`) |

The July names are from the demo's Watcom debug tables (audit 2026-10-03).

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
[file-formats.md](../../research/file-formats.md) and [models.md](../../research/models.md).

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
   them. No bounds check was seen in the collector; July's `Build_Generic_`
   has none either (audit 2026-10-03). *Update 2026-10-03:* the
   flag is `_build_list` of the BEN11 library (`3DC_LIST.C`), never written
   in the July builds either; it is a library debug list, not part of the
   editor.

With both, the 3D view freezes on the last rasterized frame while the game
runs on; the HUD and text still update. Holding Backspace adds the collision
triangles from the current camera each frame, and because nothing clears the
view they accumulate. The lines trace the ground and the stepped platform of
Project 0 coarsely. *Audit 2026-10-03:* the handle `0x4fbdc8` is actor slot
2 (`0x4fbd48`, the scene object OBJET0) `+0x80`, not the player's `+0x350`:
the `.3DI` collision-mesh field that `ENT_LoadModel` fills and
`PHYS_SetLevelMesh` uses, so it is the level's collision mesh
([scene-geometry.md](../../research/scene-geometry.md)). July reads the same
field (`0x126088`). **[verified in code]** Whether the tree roots in
Project 0 are solid is **[unverified]**.

### Step pinned at 2.0 **[verified]**

`GAME_TickFrame` computes Δt for the next frame (`0x5e5388`, a double;
[engine.md](../../research/engine.md#the-fixed-step-verified)). At `0x417268`, if
`0x4a4758 == 1` it stores 2.0, and at `0x417285`, if demo mode `0x49d34a == 0`
(recording) it stores 1.0. Demo mode starts at 2 and no reachable code changes
it, so in normal play the 2.0 pin takes effect. Animation, forces and timers
scale with Δt but the position step does not, so the game runs faster with
floatier jumps and less fall damage; it is not a clean fast-forward. At the
recomp's 25 fps cap the normal Δt is about 1.2.

**It is half of a frame-capture mode.** The dead editor draw `0x44d46d`
calls the TGA writer `0x4479c7` every frame while `0x4a4758 == 1`, or once
while `0x4a475c == 1` (then clears it). The writer stores the frame buffer,
read as RGB565, as a 24-bit top-down TGA named `data\tga\%s_%04d.tga`:
the first 3 characters of project `+0x60c` (OBJET0's scene file), an
underscore and the four-digit counter `0x4a4754` (`_TgaSceneImage`; update
2026-10-03). The July demo set these flags with keys `6` (every frame) and
`7` (once). A fixed 2.0 step gives 15 captured frames per
game second. In retail only the step half is reachable; in the recomp the
capture runs while the editor draw does (keypad 5). The writer passes
`fopen`'s result straight to `fwrite`, so a missing `data\tga` folder is not
handled.

### Gameplay hotkeys **[verified in code]**

`GAME_HandleHotkeys` (`0x415aa7`) reads the key table (index = virtual-key)
and the last key event code `0x626fd8` (message `0x33`). The audit of
2026-10-03 found no case missing from this table:

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
| event code `0x97` | `MENU_OpenObjectPage`; unreachable: `INPUT_PostEvents` posts virtual-key codes and VK `0x97` is unassigned. `0x97` is the DOS character `ù` (CP437), July's `MENI` menu key (audit 2026-10-03) |

**Girl Power.** `SCENE_InitLevel` loads the player as `mhe.3dc` with its
animation set instead of `xh_.3dc` when the flag is set and the project's
player-model field `+0x8c` is empty, so the swap applies from the next level
load. `MHE` is the red-haired woman in [models.md](../../research/models.md). When the
sword (`epee.3dc`) is attached, `0x440f15` hides node `NATE02` for her.
`SCENE_CheckExits` reads the flag four times; those branches are not traced.
The string and `mhe.3dc` are also in `DREAMSFX.EXE` and `DREAMS.EXE`; their
key handling is not checked.

## Dead code present in the binary

### Dreams Editor **[verified in code]**

An in-game editor for the `DREAMS.DAT` project graph
([file-formats.md](../../research/file-formats.md)), in `0x4479c7`–`0x44d658`.

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
  boxes as level-change or trigger volumes. *Update 2026-10-03:* only editor
  functions write the working copy (`_CurrentSceneBoxS`), so it is empty in
  play and the gizmo draws nothing; the box seen with keypad 5 is most likely
  `ShowLink_` (`0x44c51f`, editor flag only), which draws the current
  project's live LINK volumes (Project 0: `LINK0` → Project134). The offset
  `0x4fbd48` is actor slot 2, OBJET0's world position: BOX and LINK
  coordinates are relative to the scene object. `0x41bed6` is
  `PutVideoSpaceLine_`, the BOX path polyline. **[verified in code; the
  identification of the observed box unverified]**
- **Pages.** `0x44c625` runs one page at a time from about 30 flags
  (`0x4a46b4`–`0x4a474c`). Each record kind has create, edit, copy, list and
  confirm functions; creating one names it after the kind and a counter:
  **Project** (`0x449e7a`, working copy `0x65fb04`), **OBJET** (`0x44a517`),
  **LINKADVENT** (`0x44b0e5`), **LINK** (`0x44b74d`) and **BOX** (`0x44becb`,
  working copy `0x65b244`; its min/max corners are what the gizmo draws).
  Pages seen in the recomp: "LOAD LINKADVENTURE" and "LOAD MESH" (`0x44a6e4`),
  each a scrolling list with `UP`, `DOWN` and `EXIT` rows. `0x447b72` and
  `0x447e7c` (no caller) write `listL0..4.txt` and `copyL0..4.bat`, the
  per-chapter file lists and CD-mastering copy scripts; replayed on disc 1's
  `DREAMS.DAT`, the first reproduces the shipped `LISTL*.TXT` line for line
  (update 2026-10-03, [cryo-editor.md](../../research/cryo-editor.md#the-mastering-step)).
- **File pickers are broken in the retail code.** The fillers `0x4486bd`
  (`data\3dc\*.3dc`, `*.dan`, `*.dsn`), `0x4487ed` (`data\hnm\*.ubb`,
  `*.Hnm`), `0x448937` (`data\anim\*.hnm`, `data\sym\*.sym`) and `0x4489ab`
  (`data\sym\*.sym`) have no caller. They and the list pages address entry
  *i* as `base + i`: a 1-byte stride, so each name would overwrite the
  previous one but for its first letter. *Update 2026-10-03:* there are ten
  such tables, one byte each at `0x661e1e`–`0x661e27` (`0x661e21` is the
  mesh list); the July builds compile the same code with strides of 13, 16
  and 12, so the fault is a change in the tables' declared sizes, not in
  the code. The next module's globals follow (`0x661e28`, and `0x661e2c`,
  the memory pointer the Mem 3DTR readout shows). With the list empty, a page's selection stays -1; confirming
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
  `COPY DATA\HNM\GENERIC.* D:\CD1\DATA\HNM`. *Update 2026-10-03:* their
  only user is `0x447e7c`, which writes them into `copyL0..4.bat` by the
  project's disc group (record `+0x1fc`).
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
  `0x448a1f`–`0x448c0a`, probably copy and paste. *Update 2026-10-03:* the
  table holds DOS characters from the July DOS build, re-based by 0x21; the
  copy/paste cases are AZERTY punctuation (`?`/`.` project, `/`/`§` objet,
  `:`/`!` link, `%`/`µ` box), which read as virtual keys become Page Up,
  Left, Delete and unreachable OEM codes.
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
| `0x43cc32` (`CompParticles_`) | calls `0x43ca25` (`ShowGravityFirst_`, the four attractors; July inlined the same loop in `CompParticles_`), which reaches the stubbed plot `0x40224d` |
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
  exactly these two rows (owner-observed). *Update 2026-10-03:* the July
  demo has the whole tree, 351 nodes in the same format under Project,
  Scene Particle, Option, Debug and Exit To DOS; retail deleted the node
  data and put "Exit To DOS" in the July empty-node slot.
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
  delta and button globals. **[verified in code]** *Update 2026-10-03:* the
  July builds have the producer: DOS `EVM_CollectEvents_` (`int 33h` mouse
  library) and Windows `0x42029a` (`GetCursorPos`, `ScreenToClient`).
  *Audit 2026-10-03:* there are three decoders, not two: the intro-video
  handler `0x4339cf` decodes the same events. The commands that enabled mouse
  events and showed or hid the cursor (MGM 0–3) were removed with the
  producer.
- **Stubs.** The rectangle fill `0x402406` used by `0x44cd62` and `0x44d658`
  is a bare `RET`; `0x44d632` → `0x44d658` has no caller. *Audit
  2026-10-03:* `0x402406` is a single `RET` placed directly before an intact
  July assembly body at `0x402407`, like the stubs listed under the debug
  draws below.

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

*Update 2026-10-03:* the July builds do not settle it, because none of them
calls the draw either. Two more facts support a site after the render and
before the hotkeys: `SaveImage_` captures the buffer drawn this frame, and
every picker page zeroes the key code after Esc or Space, which only matters
if `GAME_HandleHotkeys` (July `DreamsKey_`, whose cases include Esc and
Space) runs later in the frame. Both candidates satisfy them; a TGA capture
with the readout on would tell them apart
([cryo-editor.md](../../research/cryo-editor.md#where-the-editor-was-wired)).

A data-only route exists: the post-scene callback `0x4aa704`, which
`REND_DrawFrame`/`Ex` call when non-zero and whose only writer is the dead
`0x45842c`, accepts the argument-less editor draw (`--poke
0x4aa704=0x44d46d`, tested without a crash). It runs inside the renderer,
before the span flush repaints the 3D view, and again during the shadow
render, so the restored call replaces it. (`0x4aa704` is `_PreRender` of the
BEN11 3D library, `3DC_MEM.C`; its setter `New_PreRender_` is uncalled in
every build, July included.) The live code calls through only
four game pointer slots: `0x4aa704`, the span flush `0x4aa708`, the object
hooks `0x4ac8cc`/`0x4ac8d0` (rewritten every frame) and the event handler
`0x626f74`; the rest belong to the Watcom runtime.

### Demo recorder start **[verified]**

Demo mode `0x49d34a`: 0 records (`DEMO_RecordFrame`, 6,144 frames, then
`DEMO_SaveReplay` writes `data\replay.bin` and sets 2), 1 plays back
(`DEMO_PlayFrame`, `DEMO_StopPlayback`), 2 is normal play and the initial
value. The start-recording function `0x40db80` and start-playback `0x40dac2`
(only called from `0x40ee88`) have no reachable caller and no pointer.
*Audit 2026-10-03:* their July names are `CTRL_RecordReplaySequence_`,
`CTRL_ReplaySequence_` and `CTRL_LoadReplaySequence_` (from the July
Windows build and link order). Two more flags belong to the recorder and are
written only by those dead functions: `0x4a2f05`, which makes `UI_DrawHud`
draw the joystick and button inputs on screen, and `0x49d33a`, the replay
frame count `DEMO_PlayFrame` stops at (read from the file by
`0x40ee88`). `UI_DrawHud` also keeps a blinking REC/PLAY icon driven by
`0x49d5dc` (`_flagRep`, toggled by `DEMO_RecordFrame`) and the demo mode.
Writing `0x49d34a` alone is therefore not enough: recording and playback need
host calls into the start functions. **[verified in code]** *Trace
2026-10-04:* wrong as stated: live, mode 0 plus an index wrote the file, and
playback emulated with memory writes alone played to its end. Calling the
functions is simply easier. **[verified in the recomp]**

*Update 2026-10-03:* in the July demo the recorder is live: `r` (F3 in the
Windows build) records, and the title menu and its idle timeout play
`data\replay.bin`, which there holds 1,848 frames recorded on the build day.
Retail ships a 3-frame `REPLAY.BIN` of 104-byte frames, while the retail
writer uses 0x70-byte frames (`0x40ee31`), so the shipped file no longer
matches its own format. *Trace 2026-10-04:* not a format change but struct
packing: the July DOS build lays the same record out in 104 bytes (doubles at
`+0x4c`, count at `+0x64`, a 100-byte compare), the July Windows build
already in 0x70. The shipped file, identical on both discs and in the install,
is a DOS recording. Retail reads it, short read included: record 0 gets count
2 and near-zero positions, so it would play about 6–9 frames inside the fade
and return to the title (worked out from the bytes, not run).

*Trace 2026-10-04,* the full paths **[verified in code; live runs in
`out/research/phase7-traces/recorder/`]**:

- **Record.** `0x40db80` sets index `0x49d346` = 0, last-applied index
  `0x49d336` = −1, mode `0x49d34a` = 0, repeat count `0x49d342` = 0 and the
  input display `0x4a2f05` = 1; it also clears `0x49d5b8`, sets `0x5e5480` =
  15.0 and `0x5e547c` = `[0x661e04]`, which starts the 15-frame exit
  transition into the current project. That reload (`SCENE_SaveLevelState`,
  a self-copy through `DDAT_CopyRecord`, `SCENE_LoadLevel`,
  `SCENE_RestoreLevelState`) is the recording's start anchor. Each frame
  `GAME_TickFrame` calls `DEMO_RecordFrame` (`0x40d833`) after
  `INPUT_UpdateActions` and before `GAME_Tick`, never during a video or the
  caption loop; in mode 0 both `GAME_TickFrame` and `CTRL_Dispatcher` force
  Δt to 1.0. Buffer `0x52eb98`, 6,144 records of 0x70 bytes: `+0x00` the 11
  action dwords `0x49d2fe`–`0x49d326`, `+0x2c` player `+0x5c..+0x70`
  (heading, bank, pitch with their rates), `+0x44` movement mode (`+0x34`),
  `+0x48` current action (`+0x15c`), `+0x50` three position doubles,
  `+0x68` repeat count. The run-length repeat never merges: the 0x6c-byte
  compare runs against the current slot before its position is written, so
  it always differs (every count is 0, live and in July's file). One record
  is one frame: 204.8 s at 30 Hz.
- **Save.** `DEMO_SaveReplay` (`0x40edaf`) is live but called only when the
  buffer is full: it writes `FILE_GetInstallRoot()` + `data\replay.bin` as a
  dword count and count × 112 bytes, then sets mode 2, `0x4a2f05` = 0, index
  0, quit `0x4a4780` = 1 and reset `0x49d5d8` = 1, so `WinMain` reruns
  `BOOT_Run`: back to the intro and the title. `DEMO_RecordFrame` toggles
  `_flagRep` itself. Live: a 688,132-byte file under the sandbox's
  `CRYO\DREAMS\data\`, its records byte-equal to the buffer.
- **Replay.** The loader `0x40ee88` reads the count into `0x49d33a` with no
  limit check (a count above 6,144 overruns the buffer), the records into the
  buffer, and calls `0x40dac2`, which saves the input device mode, sets it to
  4, sets mode 1 and the input display, writes record 0's position into the
  player and starts the same reload. `DEMO_PlayFrame` (`0x40e4ed`) loads
  record i's words, on each new record its orientation, and moves the
  position halfway toward the recorded one (only x and z while walking,
  `+0x34 == 1`: height is left to physics), then advances `round(Δt)`
  records through `DEMO_AdvanceFrames`, which ORs the next record's words
  into the live ones, so every key edge arrives a frame early. Playback
  stops at the last record or when Space is held; `DEMO_StopPlayback` sets
  mode 2, clears the words, sets quit and reset and restores the device
  mode: back to the title. Not after the player dies: the death path opens
  the system page, playback stays in demo mode there and Space no longer
  stops it (trace 2026-10-04, July's recording repacked: it plays 910 of
  its 1,848 records faithfully on retail data, then Duncan walks off the
  level and dies) **[verified in the recomp]**.
- **Determinism.** The file names no level (playback reloads the current
  project) and the random seed is never set or stored. At the recomp's fixed
  step the replayed positions equal the recorded ones through the reload
  and diverge from the first key edge (largest gap 42 units; one Alt press
  became a different action). Under `--retail-timing` playback skips
  records after the reload (largest gap 114 units), and a caption started
  during playback saved device mode 4 into the same save slot `0x49d2f9`, so
  after the stop the input stayed dead: a retail bug. **[verified in the
  recomp]** Cryo's playback is approximate by design.

Develop's keys and guards (`r` record and save as in July; `R` with the
editor off to replay) are in spec 008, phase 7.

### Debug draws with no caller **[verified in code]**

All call the line drawer `0x465c80` (`C3D_Line_`); none has a caller or an
address copy. *Audit 2026-10-03:* the names are the BEN11 library's
(`3DC_COL2.C`, `3DC_COLL.C`, `3DC_MEM.C`, from the July debug tables), and
none of them had a caller in July either. The direct renderer replaces
`C3D_Line_` but not the pixel writer `0x465ea4` (`C3D_Pixel_`), so with it
their lines draw and their dots do not.

| Function | What it draws |
|---|---|
| `0x46115c` `Aff_Box_` (from `0x461334` `Display_Box_`, from `0x4614b0` `Display_Boxes_`) | 12 edges each: the oriented collision boxes whose corners `PHYS_UpdateNodeBoxes` (`0x4610e8`) refreshes every frame; `Display_Boxes_` also prints a count. Whether any shipped model has boxes is **[unverified]** (`Enable_Box_`, `Check_Collision_` have no caller) |
| `0x458434` `Display_Frame_` | axis gizmo: `(framebuffer, position, 3×3 matrix)`, three 100-unit axes in `0xf800`, `0x1fe0`, `0x00ff` with white end dots (`0x465ea4`); July repeated the draw once per light, retail drops that loop |
| `0x45842c` `New_PreRender_` | setter of the post-scene callback `0x4aa704` (`_PreRender`), which `REND_DrawFrame`/`Ex` call when non-null; the gizmo's arguments do not fit that call |
| `0x45e950` `Display_Line_` (from `0x45ea00` `Display_Collision_Sphere_`, `0x45f104` `Display_Overlap_Sphere_`, `0x45f264` `Display_Face_`) | a world-space line through the camera. `Display_Collision_Sphere_` draws a collider's centre, then for each triangle it touches three lines across the triangle's bounding box and a line from the centre to the contact point; `Display_Overlap_Sphere_` the centre and its candidate pairs; `Display_Face_` one triangle. The collider record comes from `PHYS_GetCollider` (`0x45f638`); which record is the player's is **[unverified]** |
| `0x45e8e8` `Display_Pixel_` | a world-space point (the centre dots above) |
| `0x41b9d5` (from `0x41bacb`, `0x41bed6` via the editor's `0x44c440`/`0x44c51f`) | editor helper lines |

Other bare-`RET` stubs: `0x4020a8`/`0x4020ce` (the pixel reads of
`ENT_AdaptActorColor`), `0x40224d` (plots from `0x43ca8e`, no caller),
`0x44d920`. *Audit 2026-10-03:* `0x4020a8`, `0x4020ce`, `0x40224d` and
`0x402406` are each a single `RET` placed directly before an intact July
assembly body (`_GetXYColor`, `_PutXYColor`, `_PutSpriteL16` and the
routine at `0x402407`). The bodies load a selector into `FS`, which
suggests the Windows port disabled DOS-only code this way; a host
replacement could supply them. **[verified in code; the reason
unverified]**

### BEN11 profiler **[verified in code]**

`DREAMSFX.EXE` has eight timer slots (`0x967c4` start, `0x9680c` stop,
`0x9677c` reset, `0x96748` calibration) around `REND_DrawFrame`/`Ex`, the
renderer init `0x70d04`, `0x6a770`–`0x6a964`, `0x7480c` and `0x757a4`. The
bar graph `0x96920` and `"%4.1f"` fps text `0x969b8` are in
`REND_DrawFrame`'s call list (feature export), while its decompiled C omits
them.

*Audit 2026-10-03:* the gate is the BEN11 flag `_rendertype` (`0x104f38` in
retail `DREAMSFX.EXE`), tested at `0x73899` and `0x73984` and never written;
the same flag drives the per-scanline span and edge meters (`0x83fc3`,
`0x84319`). Poking it to 1 under DOSBox should show both. The module is
`3DC_PROF.C`: eight timer banks of which July uses four (whole frame, 3D
render, animation, collision).

It is not DOS-only. In July the profiler was wired into the DOS software and
Windows builds and absent from the Glide build. Retail `WINDREAM.EXE` keeps
the whole module, uncalled: 11 functions at `0x4995e4`–`0x4998e0`, banks at
`0x68117c` (16 bytes each), current bank `0x6811fc`, reference time
`0x681200`, the stacked frame-time bar `Display_Info_Timer_` (`0x4997f8`,
top scanline) and the fps text `info_timer_text_` (`0x499874`, through
`C3D_Print_` `0x4655b0`). The per-scanline meters are compiled out of the
Windows build. Three things stop a direct reconnection: `cpu_init_lib_`
waits on the VGA status port in `vbl_` (`0x48a62f`), and the lifter turns
`in al, dx` into a no-op, so it never returns; starting a bank twice calls
`getch_`, a pause trap; and both displays write pixels, which the direct
renderer does not show. A host overlay timing the same four stages is the
practical route (inventory below).

## Developer-tool inventory (audit of 2026-10-03)

Four static audits, none yet run in the game: the July demo's names and
source modules (A), every July and retail trigger: keys, messages, command
line, environment, files (B), the retail dead code and flags (C), and the
BEN11 engine library, renderers and build differences (D). Their tables are
local and game-derived: `out/research/devtools-audit/A`–`D/`.

- **Counts.** 551 of the 2,065 retail functions are dead (356 with no caller
  and no stored address, 195 reached only from dead code): 158 game code
  (20 developer tools, 61 cut features, 77 unclear), 93 editor, 300 library
  (138 BEN11, 52 Watcom runtime, 107 unused import thunks, 3 codec). No game
  function is kept alive only by a pointer in data. **[verified in code]**
- **No switch outside the game.** No build reads a developer option from the
  command line, the environment or a config file: both `WinMain`s ignore
  `lpCmdLine`, the DOS `main`s ignore `argv`, and only middleware and the
  Watcom runtime call `getenv`. **[verified in code]**
- **Develop mode.** [Spec 008](../008-editor-restoration/spec.md) defines
  three launch modes, Play, Develop and Play edits; Develop turns the tools
  below on. Its keys are read as typed DOS characters, as the July DOS build
  read them; the full key plan and the clash rules are in spec 008, phase 2.
  Keypad 1–5 stay as in [Recomp support](#recomp-support); keypad 6–9 take
  the tools with no July key, as below (2026-10-03). Develop ran on the
  software renderer until spec 008's phase D ported it to the direct
  renderer (2026-10-04); the "Direct renderer" columns below describe what
  that port had to solve, and spec 008's phase D "Built" notes how.

### Tier 1: Cryo's tools, cheap to reconnect

| Tool | July trigger | Retail address and state | Reconnect | Develop key | Direct renderer |
|---|---|---|---|---|---|
| Free-fly camera | `-` (DOS char `0x2d`; the July Windows build read it as Insert) → camera message `0x31` | `CAM_ToggleFree` `0x40b386`, `CAM_TickFree` `0x40b429` (camera mode 7) live; no code posts `0x31` | host calls `CAM_PostMessage` (`0x409998`) with `0x31` | `-` | yes |
| Overhead camera | `9` → `0x30` | `CAM_ToggleOverhead` `0x40b274` (mode 6, eye 1,500 units above the player) live; no sender | same call with `0x30` | `9` | yes |
| Frame Rate / Mem 3DTR | `8` | `0x49d5c0`, live | flag | `8`, keypad 1 | yes (game text) |
| Object HUD | F1 held | `0x49d5d0`, live; retail F1 sets the resolution | flag | keypad 2 | yes |
| Collision view | — | byte `0x4ac8c8` plus Backspace held | flag | keypad 3, Backspace | software frame only |
| HUD on/off | `A` | `0x49d5d4` (`_FlagAfficheInterface`), initial 1, never written | write 0 | `A` | yes |
| TGA capture, Δt 2.0 | `6` every frame, `7` once | `0x4a4758`, `0x4a475c`; the writer runs in the editor draw | flags; the inserted call must run the draw every frame in Develop mode (the draw tests the flags itself) | `6`, `7` | the writer reads the software frame buffer **[unverified with direct]** |
| Demo recorder | `r` (F3 in July Windows) | start `0x40db80` and loader `0x40ee88` (which calls the playback start `0x40dac2`) uncalled; `DEMO_SaveReplay` `0x40edaf` live but only on a full buffer; flags `0x49d34a`, `0x4a2f05`, `0x49d33a`, `0x49d5dc` (Demo recorder start, above; trace 2026-10-04) | host calls `0x40db80` / `0x40edaf` (record, save) and `0x40ee88` (replay, after a size check) | `r`; `R` replay | yes |
| Give all items | none, in July either | `0x49d5e0` (`_flagImportAllObjects`); while non-zero `GAME_TickFrame` (`0x4170e4`) adds one item per frame through `ENT_AddInventoryItem` (`0x42a182`), 14 names at `0x49db12` (feu, arc, epee, guerison, bouclier, connaiss, temps, spirit, holo, resurec, invivib, mine, shaman, vitesse), then clears it at `0x417121`, its only write; `MENJ_Dispatcher` shortens the pick-up wait while it is set; items already held are renamed (below, trace 2026-10-04) | write 1, with guards and a host-only fix-up of held items (spec 008, phase 7) | keypad 6 | yes |
| Dialogue test | `D` (plays entry 0) | `MENJ_Dispatcher` handles message `0x40` from the HUD queue `[0x626f2c]`, live | host calls `MGM_PostMessage` (`0x43b31a`) with (`[0x626f2c]`, `0x40`, id, 0) | `D` | yes |
| Replay level movie | `H` (the project's movie, `+0x3c`) | the same sequence runs at level entry (`0x416015`) | host call | `H` | yes |
| Console log | always on | the game's own `printf`: `Nom: %s` (archive entries), `map %s not used`, `No 3di file`, `ATTENTION Problème de hiérarchie !!!`, `Heap overflow`, `Erreur liberation`, `Error SprSet %d`, `Pb de scanline qui recule`, `Sound Is On`; it already reaches `run/stdout.txt` **[verified in data]** | host shows it | keypad 9 | n/a |

The free camera turns with the mouse deltas (`0x4a3154`/`0x4a3158`) and flies
forward or back on button word `0x4a315c` 1 or 2; held PgUp, PgDn, Home and
End scale its speed by ½, ¼, 2 and 4 (key table `0x6308d8` + VK). It starts
300 units from the actor while the player keeps playing; any `0x2b` post
(a hit, a trigger, a level start) returns the camera to follow. It does
nothing while the editor flag is set: July's `CAM_MoveMouse_` tests `_editor`
first, because both use the mouse. July DOS read the speed keys by scan code
(`_tab_active_scankey + 0x49`, `0x51`, `0x47`, `0x4f`) and the trigger by
character, so they never clashed; the July Windows port read the old
character codes as virtual keys, which made `!` (the editor toggle, `0x21`)
Page Up. **[verified in code]**

*Trace 2026-10-04,* give all items **[verified in code and in the recomp;
live runs in `out/research/phase7-traces/giveall/`]**:

- **The loop.** While `0x49d5e0` is set, `GAME_TickFrame` runs `i =
  [0x49d5e4]++` and `ENT_AddInventoryItem(0x4fba78, 0x49db12 + 9·i)`; at 14
  it zeroes the counter and the flag. One press adds exactly 14 items over 14
  frames; setting it again mid-sequence changes nothing. It runs during a
  video (the 14 messages wait and are handled afterwards) and waits during a
  dialogue or the game menu, which block the frame; with no inventory
  (`actor+0x30` = 0) nothing is added; items given during a New Game load
  are lost when the inventory block is cleared.
- **The inventory** (`actor+0x30`, `0x61520c` live, 0x3b8 bytes, 32 entries;
  layout in [game-content.md](../../research/game-content.md)). An item
  already held (matched with `stricmp`) gets count + 1, its level follows
  the count, and `strcpy` overwrites its stored name with the give-all's
  lower-case spelling (`EPEE` became `epee`, count 2, HUD "3"). A repeat
  still needs a free entry; with all 32 used every add is refused.
- **Messages.** Message `0x41` is posted before the slot search, so also for
  a refused add: with a full inventory that leaves a "ghost" hotkey (icon set,
  entry −1; the HUD prints the bits of 50.0f). The HUD queue holds 50
  messages and drops more silently. A usable item takes the first free
  hotkey slot unless it is already in one, but the slot is saved only on the
  HUD's own tick inside its visible branch: with the HUD hidden
  (`0x49d5d4` = 0), or after a video, all 14 messages pick slot 0 and the
  last item wins it.
- **Effects.** Adding writes only the inventory, the owner pointer, the HUD
  state and, through `ENT_BindHotkeySlot`, the owner's `+0xd8..+0xec`: no
  weapon state, no model, no passive effect; the level matters only for
  `guerison` (each extra copy heals 6 more). But `ENT_HasInventoryItem` is a
  case-sensitive `strncmp`, and pick-ups store the OBJET name in upper case
  (all 23 item OBJETs): the lower-case give-all names satisfy no "held" test
  and block no "not held" test. Only three bank names are concerned:
  `VITESSE` (Project 99, LINK0 → Project 31), `INVIVIB` (Project 100, LINK0
  → Project 37) and `HOLO` (Project 10, LINKADVENT1–5, "not held" rules).
  So a give-all while `VITESSE` or `INVIVIB` is held renames it and closes
  the only exit of Project 99 or 100; live, Project 99's exit stayed closed
  until `VITESSE` was written back, then opened at once. The renamed
  inventory also goes into the next autosave.

So giving all items is not harmless when some are held; spec 008's phase 7
guards the press and restores the held entries' spelling, count and level
afterwards (host-only).

### Tier 2: engine-library views that need a host hook

| Tool | Retail | Reconnect | Develop key | Direct renderer |
|---|---|---|---|---|
| Collision sphere, overlap and face displays | `Display_Collision_Sphere_` `0x45ea00`, `Display_Overlap_Sphere_` `0x45f104`, `Display_Face_` `0x45f264` (debug draws, above) | a host call after the frame for each collider (`PHYS_GetCollider` `0x45f638`) | keypad 7 | lines yes, centre dots no |
| Oriented-box wireframes | `Aff_Box_`, `Display_Box_`, `Display_Boxes_` | the same hook | keypad 7 | yes (lines) |
| Collision counters | `_Add_OverlapCount` `0x4aa9c4`, `_Remove_OverlapCount` `0x4aa9c8`, `_OverlapCount` `0x4aa9cc`, `_debug_count` `0x4aa9d0`: counted in `PHYS_AddCandidate`/`PHYS_RemoveCandidate`, reset in `PHYS_UpdateCollider` (`0x45f4b8`), never read; `_flag4` `0x4ac8d7`, never set, only adjusts `_debug_count` | host overlay reads them | keypad 8 | n/a |
| Memory totals | `MemoryUsed_` `0x424d7b` (`_heapwalk_` sum of used blocks), `MMS_GetFreeSpace_` `0x43b1fd`, `Get_Free_Memory_`; uncalled | host overlay calls them | keypad 8 | n/a |
| Profiler | `3DC_PROF.C` (BEN11 profiler, above) | host reimplementation: time frame, 3D render, animation and collision and draw the bar itself | keypad 8 | host-drawn |
| Dump helpers | `Scan_Mem_` `0x458384` (3D arena block chain), `PrintMisEntry_` `0x43afcd` (archive directory), `Mat_Print_` `0x45bbd4`, `Vect_Print_` `0x45bc04`; `0x47b304` prints projected vertices outside the view (July `Check_Project_Verts_` by its strings, **[unverified]**); all uncalled, all print to stdout | host calls, output to the console | console (keypad 9) | n/a |

The two audits disagree on whether `_debug_count` is ever reset
**[unverified]**.

### Tier 3: software renderer only, or low value

| Tool | Retail | Notes |
|---|---|---|
| Render classes `e f l v` | July `Comp3dEngineClavier_` is gone; its target `Change_Object_Classes_` = `MDL_ReplaceMaterial` (`0x4577cc`) is live; the scene handle is `[0x4fbdbc]` (actor slot 2 `+0x74`, written by `ENT_LoadModel`) | `l` 3→6, `e` 6→3, `f` 3→`0x1c` (`BT_Mapping_Flat_`), `v` `0x1c`→3, over every face of the level; Develop keys `e f l v` under the software renderer only; whether the direct renderer handles classes 6 and `0x1c` is **[unverified]** |
| Background clear colour | `0x4ac744`, setter `Set_BackGrd_Color_` (`0x4583c0`) uncalled | shows holes in the level; software renderer only; no key planned |
| Title-screen attract demo | July alternated `REPLAY.BIN` and `s81b_hi.ubb` after about 12 s idle; retail still counts idle steps (`0x626f04`) but nothing tests the count; the loader `0x40ee88` is uncalled | needs a new recording (the shipped `REPLAY.BIN` is in a stale format) |
| Stuck option flags | `0x4a0f68`–`0x4a0f74` (map-animation enables, initial 1, never written; July "Option > Map Anim" leaves by data order **[unverified]**), `0x4a2fb0` (particle defaults, initial 1) | leads for spec 008's engine-global bindings |

### Not reconnecting

- **CPU-load bars** (editor `0x44d658`): `_CpuDisk` `0x49d1a4` and `_CpuHnm`
  `0x49d1ac` are never written in any build, `_CpuVideo`'s producer `0x4040c8`
  is uncalled, and the bar fill is the bare `RET` at `0x402406`.
- **Render statistics** and the **per-scanline meters**: compiled out of
  the Windows build (only the collector indices `0x6808e4`/`0x6808e8`
  remain); the meters survive in retail `DREAMSFX.EXE` behind `_rendertype`.
- **`GENETIC.C`**: an R&D genetic-classifier AI with its own test harness
  (`GEN_TestPop_`), dead in every build.
- **`FLU_*`** (`MENU.C`, `0x4398be`): a ripple-filter test over `menu.tga`
  with a console key loop (`kbhit_`/`getch_`, `pix.raw`), dead in every build.
- **`MENG`, `MENM`, `MENI`** (July `*`, `M`, `ù`): prototype gameplay menus
  (icon ring, spells, inventory), not developer tools; their strings are
  gone from retail.
- **DOS-only tracing and memory reports**: Miles `MSS_DEBUG`/`MSS_SYS_DEBUG`,
  Glide `GDBG_LEVEL`/`GDBG_FILE`, `printmemory_` and `ErreurExit_` (DPMI);
  CryoLib's debug switches exist only in `CRYO.DLL`.
- **`3DC_Z.C`**: pre-rendered Z-buffer backgrounds, a cut feature.

### Triggers with no retail sender **[verified in code]**

- **Camera messages** (`CAM_CompCameraPos`, 19 call sites): only `0x2b` and
  `0x2d` are posted. No sender for `0x2c` (fixed camera), `0x2e` (entity
  pair), `0x2f` (ride look), `0x30` (overhead), `0x31` (free).
- **`MGM_SendMessage`** (76 call sites, all with constant numbers): no sender
  for 6, 7, 8, 9, `0xb`, `0x10`, `0x13`, `0x14`, `0x15`, `0x1d`, `0x20`,
  `0x21`, `0x24`, `0x25`. These are plumbing (joystick POV on/off, sound
  shutdown, CD close and door, a countdown that posts event `0x3c`, which no
  handler takes, two stubs); 7 is the only route to `JOY_EnablePov`, so
  joystick event `0x39`'s decoder cases are dead.
- **Events**: `0x3c` and `0x3d` are never forwarded; the mouse events
  `0x34`–`0x38` have no producer (above); hotkey code `0x97` is unreachable
  (Gameplay hotkeys).

### Cut gameplay features (not developer tools)

A July 3D inventory ring (about 20 `INV_*` functions, July key `I`) and its
stat page `INV_PutStat_`; the old HUD `PutScreenInfo_` with its life, mana
and oxygen bars; a water-distortion effect (`0x423ca7`, `0x423dc8`); a
retail-only table of per-level node overrides keyed by level name
(`0x41ce67`); `clonePLayer_`; the camera-path follower `compCameraPath2_`;
unused AI tactics; the motion-blur and smooth-blur filters; the magic bar;
and the in-game Save page opener `0x4313b5` (`MENU_InitSaveSlotSelect(1)`,
then `MENU_RunGameMenu`), the twin of the `L` load page, which Develop
reconnects as its save-anywhere (spec 008, B1 and phase 7).

### Save page and save loading (trace 2026-10-04) **[verified in code]**

Scratch: `out/research/phase7-traces/savepage/` and `saveguard/` (local).

- **What is dead.** `0x4313b5` is `MENU_OpenLoadPage` with `0x4a155b` = 1;
  it has no caller and no pointer. The SYSTEM page's item 1 (Save) is
  skipped by Up/Down (`0x431669`, `0x4316a5`). The page itself is live
  code: `MENU_RunGameMenu` (`0x4337c0`) makes `CTRL_Dispatcher` the handler
  (the game is frozen) and reaches `MENU_HandleSaveSlotInput` (`0x4373a7`);
  `MENU_DrawSaveSlots(1)` sets save mode `0x4a2f41` = 1 and draws the
  blinking `_` cursor.
- **The typed-title entry is live.** It runs in a frame where a key event
  arrived and Esc, Up and Down were not pressed (`0x4376d5`), takes codes
  0x20–0x7e from the last event `0x33` (`0x626fd8`), at most 20 characters
  (`0x4379e7`, `0x437a41`), and types into the slot's name in the index
  (`0x5dabf8` + slot·0x16); Backspace needs Backspace held. Retail posts
  virtual-key codes there, so only upper-case letters, digits and space type
  as intended (arrows type `%&'(`, F-keys `p`…`{`); July's twin read DOS
  characters with the same filter and limit.
- **What confirm writes.** Return on an unprotected slot sets its recency to
  max + 1 and calls `GAME_SaveIndex` only: `game.dat` is rewritten, but no
  `game<n>.dat` and no `.ico`, so the renamed slot still restores the slot's
  old autosave. `GAME_SaveGame` and `GAME_SaveThumbnail` are called only by
  the autosave (`0x439341`). Every save and load lookup takes the first slot
  whose name matches, so a title equal to another slot's name saves into, and
  loads from, that slot.
- **Retail defects.** The selection is frozen in save mode (`0x437587`,
  `0x43763f`): the page stays on the slot `MENU_InitSaveSlotSelect(1)`
  picked, the most recent unprotected one; with no unprotected used slot
  (a fresh folder on P0, which never autosaves) that function leaves the
  slot index uninitialised (`0x437b21`). `0x4a2f41` is set only by the
  draw (`0x437c35`), so until the first redraw the page runs in load mode.
  Tab writes an unconfirmed title, possibly with the cursor, into
  `game.dat`; after a save the blink highlights the sorted-away slot.
- **Loading.** `GAME_LoadGame` (`0x40f94a`) is the only reader of
  `game<n>.dat` and has one call site, `0x437960` in
  `MENU_HandleSaveSlotInput` (0 → code 5, else code 8). It finds the slot by
  title, reads the 0x2880-byte level-state ring, the dword `0x49da84`, then
  the record name (`+0x2884`, `char[32]`; a save is 11,388 bytes), calls
  `DDAT_LoadRecord(name)` (`0x40fa9f`) and `memcpy_(0x52c970, rec, 0x2200)`.
  `DDAT_LoadRecord` returns NULL only with the editor flag set or while
  `0x633bf4` is still empty (filled by the first successful lookup: exit,
  death, resurrection or save load); otherwise an unknown name loads the
  level named at `0x633bf4` with the save's state, sets `0x4a4804` = 1 and
  puts the player in the wrong level. No automatic restore reads a save: the
  resurrection item moves the player to `Project10`, `SCENE_RestoreLevelState`
  works from memory. The in-game Load page shows `MENU_DrawHelpText(5)` for
  code 8 (static English table `0x4a102c`, 99 bytes per entry); the main
  menu stores code 8 at `0x4a2ef5` and never reads it.

Spec 008 phase 7 turns this into the Save page recipe (a host opener, ASCII
text entry, the three save calls after a confirm, duplicate titles refused)
and the save guard (a host replacement of `GAME_LoadGame`).

## Recomp support

Host glue in `recomp/windream/`. The lifted code is unchanged except for one
inserted runtime call.

- `WD_POKE="va=value,..."` (`host/core/runtime.c`, `apply_pokes`): dword writes
  into the loaded image after loading and before the entry point; addresses
  outside the image are rejected; each write is logged to `stderr.txt`.
- `run.py --poke VA=VALUE` (repeatable) fills `WD_POKE`; `run.py --overlays`
  pokes `0x49d5c0`, `0x49d5d0` and `0x4ac8c8` to 1. The byte flag is poked as
  a dword; its next three bytes start at 0.
- Keypad toggles (`host/sdl/user.c`, `g_debug_keys`), by scancode so Num Lock
  does not matter; the keys are not passed to the game (with Num Lock off,
  keypad 2 is no longer an alternative Down arrow):

| Key | Flag | Plain `run.py` | `--overlays` |
|---|---|---|---|
| keypad 1 | `0x49d5c0` readout | off | on |
| keypad 2 | `0x49d5d0` object HUD | off | on |
| keypad 3 | byte `0x4ac8c8` collision view | off | on |
| keypad 4 | `0x4a4758` step 2.0 | off | off |
| keypad 5 | `0x4a477c` editor flag, which also enables the editor draw (F1–F6 clear it) | off | off |

*Audit 2026-10-03:* each keypad key toggles its flag (`debug_toggle`); the
same toggles are reachable from a `WD_KEYS` script naming virtual keys
`0x61`–`0x65`. The keypad toggles, `mouse_post` and `wd_editor_frame` are
compiled into every build, release included: none is behind `WD_DEVTOOLS`,
and `mouse_post` posts `0x34`–`0x38` whenever the handler is
`GAME_TickFrame` or `CTRL_Dispatcher`, editor on or not, so the mouse steers
in normal play. Spec 008, phase M, confines them to Develop mode.
**[verified in code]**

- Mouse (`host/sdl/user.c`, `mouse_post`): the runtime supplies the missing
  producer. SDL motion and left/right buttons, mapped to game-frame
  coordinates, are posted as `0x34`–`0x38` into the standing handler's queue
  `0x626f70` with the game's `MGM_PostMessage`, from the `PeekMessageA`
  bridge before the pump dispatches. Only while the handler is
  `GAME_TickFrame` or `CTRL_Dispatcher`, the two checked decoders; the
  router `MGM_DispatchMessages` would treat these types as fatal. The deltas
  also reach the joystick globals `0x4a3154`/`0x4a3158`, which the free
  camera and joystick input mode read: in play the mouse steers like a
  joystick (owner-observed).
- Editor call (`lift.py` `CALLS`, `host/sdl/user.c` `wd_editor_frame`):
  `lift.py` inserts `wd_editor_frame()` before the `GAME_HandleHotkeys` call
  at `0x41743a`, in `GAME_TickFrame` and in the two fragments the lifter makes
  from its jump targets; each call follows the `L_0041743A` label, so the
  jump that skips the readout also reaches it. `wd_editor_frame` calls
  `0x44d46d` while `0x4a477c` is set, through `guest_call_regs`
  (`host/core/runtime.c`), which restores every guest register. `CALLS` inserts
  before an instruction like `PROBES`, but may run guest code.
- Editor keys also reach the game's own hotkeys (`L`, `J`, `K`, `P`, digits).
  Picker pages and record creation hit the retail picker defect above; `Q`
  and confirming a project reload the level. Spec 008, phase 2, replaces this
  with DOS-character keys and rules for each clash (the editor wins while it
  is on; keys the host consumes are hidden from the game).

Pokes of code bytes have no effect in the recomp: instruction operands are
compiled into the lifted C, except the self-modifying sites `lift.py` lists.

## Reproduce

```sh
uv run --with capstone --with pefile python recomp/windream/debug/flag_hunt.py
uv run --with capstone --with pefile python recomp/windream/debug/flag_hunt.py --ptr 44d46d 458434 40db80
uv run --with capstone --with pefile python recomp/windream/debug/x86dis.py 40bff8-40c050 459320-4593a4 417268-4172c0
uv run python re/tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis -readOnly -postScript Inspect.java refs:004a477c refs:0049d34a
uv run python recomp/windream/run.py --overlays
uv run python recomp/windream/run.py --overlays --seconds 50 --snap-ms 4000 --keys 2000:ESC,9000:RETURN,36000:BACK,36400:BACK
```

The last run reaches Project 0 and taps Backspace; snapshots land in
`out/recomp/windream/run/`.

## Work items

1. **Names.** Register the functions above (`DBG_`, `DEMO_`, the editor) in
   `re/names/WINDREAM.EXE.tsv` with two sources each and run
   `re/tools/check_names.py`; nothing here is renamed in Ghidra yet. The
   July `WORKS.C` names now map to every retail editor function (101-row
   table in `out/research/editor/reports/C-retail-vs-demo.md`; 73 of them by
   normalized instruction match).
2. **Editor.** Running in the recomp. The open points here (page fields,
   the text writers, the copy commands, the BOX working copy, the gizmo)
   are answered by the July demo; restoring the whole editor moved to
   [spec 008](../008-editor-restoration/spec.md).
3. **Editor file pickers.** Moved to spec 008, phase 3 (host replacements
   of the fillers and the list parts of the pages, since the 1-byte
   indexing is compiled into every picker page).
4. **Reachability pass.** Find flags whose only writers are dead code, as
   `0x4a477c`'s effectively is: `flag_hunt.py` only lists flags with no
   writer at all. *Audit 2026-10-03:* done by hand
   (`out/research/devtools-audit/C/dead_flags.tsv`); left: teach
   `flag_hunt.py` to ignore writes of 0 and to follow `mov` reads.
5. **Demo recorder.** Poke `0x49d34a=0` at start and check that a
   `data\replay.bin` appears in the sandbox after 6,144 frames; playback needs
   the loading done by the dead `0x40dac2`. *Audit 2026-10-03:* recording
   also needs `0x40db80` called (it sets the input display `0x4a2f05`), and
   playback the frame count `0x49d33a` that only `0x40ee88` reads in.
   *Trace 2026-10-04:* done live; the loader is `0x40ee88` (it calls the
   playback start `0x40dac2`), the save `0x40edaf` returns the game to the
   title, and memory writes alone suffice (Demo recorder start, above).
6. **Collision view over the live scene.** Draw the retail wireframe again
   after `REND_DrawFrame` in a visible colour. This needs a host hook, so it
   leaves the pokes-only boundary; opt-in only.
7. **DOS profiler.** Trace its gate in `DREAMSFX.EXE` and try it under DOSBox
   Staging. *Audit 2026-10-03:* the gate is `_rendertype` (`0x104f38`); left:
   poke it under DOSBox and capture the bars and scanline meters.
8. **OpenDreams.** Carry the readout, object HUD and collision view into the
   runtime's debug overlay as retail-derived views (north-star.md).
9. **Developer tools for Develop mode.** Reconnect the inventory's tiers 1
   and 2 under the launch mode and keys of spec 008 (cameras, HUD off, give
   all items, dialogue test, level movie, recorder, console, collision
   displays, host profiler overlay).

## Open questions

- ~~Is `0x4fbdc8` (player actor `+0x350`) always the level's `.3DI` mesh?~~
  It is actor slot 2 `+0x80`, the scene object's `.3DI` collision-mesh field
  (answered by the audit, 2026-10-03).
- ~~What did the July keys `9`, `-` and `D` do?~~ Overhead camera, free-fly
  camera, dialogue entry 0 (answered by the audit, 2026-10-03).
- ~~Is giving all items safe when the player already holds some
  (`0x49d5e0`)?~~ No: a held item is renamed to the lower-case spelling and
  counted twice, which can close the exits of Projects 99 and 100 (answered
  by the trace, 2026-10-04).
- Do the `VITESSE` and `INVIVIB` pick-ups respawn on a return visit to
  Projects 99 and 100?
- How would July's 1,848-frame `REPLAY.BIN`, repacked from 104 to 112-byte
  records, play on retail data?
- Where can the main menu show a refused save load, since it discards code
  8 (`0x4a2ef5`): after `MENU_DrawSaveSlots` (`0x437c01`), or not at all?
- What are the never-written flags `0x4a2ecd` (`UI_DrawHud` draws the
  `infosne` icon when set), `0x49d2c8` (stuck at 1; `CAM_ApplyLookAt` takes a
  branch only then), `0x49d9f0` (stuck at 1; `SCENE_LoadLevel` runs a
  16-entry loop over project `+0x6c0`) and `0x4a1577` (`MENU_RunGameMenu`
  calls `VID_SetResolution` when it is 1; nothing sets 1)?
- Is `_debug_count` (`0x4aa9d0`) reset with the other collision counters?
- ~~Did the developers' build read the mouse through Windows messages or
  DOS `int 33h`?~~ Both: the July DOS build through an `int 33h` library,
  the July Windows build with `GetCursorPos`. The cursor is
  `data\objet\sour.alp`, sprite set 10, frame 1 (answered 2026-10-03).
- Was the editor draw called before `GAME_HandleHotkeys` or after
  `DBG_DrawObjectInfo`? The recomp uses the first. Still open; a capture
  test would decide.
- ~~What fills the working BOX record `0x65b244` in normal play, what is the
  gizmo's offset at `0x4fbd48`, and what does `0x41bed6` draw?~~ Nothing;
  OBJET0's position; the BOX path polyline (answered 2026-10-03).
- ~~Who runs the `D:\CD1`/`D:\CD2` copy commands, and did the picker defect
  exist in the developers' build too?~~ `0x447e7c` writes them into
  `copyL*.bat`; the July builds index the pickers correctly, and the October
  bank and lists were produced by a build whose tables were full size
  (answered 2026-10-03; the last point is an inference).
- What do `SCENE_CheckExits`'s four Girl Power branches change?
- Can the triangle collector overrun its arrays in larger scenes?

## File map

| Path | Content |
|---|---|
| `recomp/windream/host/core/runtime.c` | `apply_pokes` (`WD_POKE`), `guest_call_regs` (register call into lifted code) |
| `recomp/windream/host/sdl/user.c` | `g_debug_keys`, `debug_toggle` (keypad 1–5), `mouse_post` (events `0x34`–`0x38`), `wd_editor_frame` |
| `recomp/windream/lift/lift.py` | `CALLS`: runtime calls inserted before an instruction (`0x41743a` → `wd_editor_frame`) |
| `recomp/windream/run.py` | `--overlays`, `--poke` |
| `recomp/windream/debug/flag_hunt.py` | tested-but-unwritten flag scan; `--ptr` address-copy scan |
| `recomp/windream/debug/x86dis.py` | disassembly of `GDIDREAM.EXE` ranges |
