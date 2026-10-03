# The Cryo level editor ("Dreams Editor"): a macro view

Date: 2026-10-03. Static analysis of the July 1997 work-in-progress demo
(four executables, two project banks) and the October 1997 retail builds,
plus the files on the discs and outside sources. Nothing here comes from
running the demo; the retail editor was run in the recomp earlier
([spec 005](../specs/005-debug-tools/spec.md)). Evidence tags follow
[the docs index](../README.md#evidence-tags); **[verified in code]** marks a
fact read in a disassembly or decompilation, and every reconstruction of how
the team worked is **[unverified]** by nature.

This document is the overview. The detail is in:

- [wip-editor-discovery.md](wip-editor-discovery.md): the first pass over
  the demo (Ghidra project, symbols, the 351-node tree, `EDITOR.DAT`);
- [spec 005](../specs/005-debug-tools/spec.md): the retail remnants and how
  the recomp runs them (keypad 5, restored mouse and draw call);
- [file-formats.md](file-formats.md#the-record-codec-and-layout-verified):
  the `DREAMS.DAT` record, now with the editor's own field names;
- the five research reports behind this page, under
  `out/research/editor/reports/` (A demo internals, B wiring, C retail
  against demo, D fields and banks, E workflow evidence), with their scripts
  in `out/research/editor/scratch/`. They are local, game-derived and not
  committed.

## In one page

Dreams was laid out inside the game. The editor is one source file of the
game, `C:\DREAMS\src\WORKS.C` (99 functions, 482 data symbols), linked into
every build, DOS and Windows, July and October. A designer ran the game at
640 pixels wide, pressed one key and got a mouse-driven panel over the live
level: a cascading menu of 351 nodes whose leaves are sliders bound to the
fields of the level record, plus pages that create, load, save, copy and
delete the records of a level (OBJET actors, LINK exits, BOX volumes and
paths, LINKADVENT event rules). Positions were not typed: the designer walked
the player, collision off, to a spot and pressed `0` to capture it, or
pressed Shift+4 at each waypoint of a path. Every edit applied to the
running level at once. F10 wrote the whole bank twice: raw as `editor.dat`
(the working file the game loaded at start) and compressed as `dreams.dat`
(the file that ships). In October the same code also wrote the per-disc file
lists and the CD-mastering copy scripts from the bank.

The retail game keeps almost all of the code and almost none of the way in.
Nothing calls the editor's per-frame function in any build, July included;
retail also lost the toggle key, the mouse feed, the menu (two nodes left)
and the bank save, and its file pickers are broken by a data-size change.

| | Demo DOS (`DREAMS.EXE`, `DREAMSFX.EXE`) | Demo Windows (`DREAMWIN.EXE`, `DRWIN.EXE`) | Retail Windows (`WINDREAM`/`GDIDREAM.EXE`) |
|---|---|---|---|
| Date | linked 1997-07-10 | linked 1997-07-10 | linked 1997-10-29 |
| Debug names | Watcom symbols: original names | none (names transferred) | none (names transferred) |
| `WORKS.C` functions | 99 | 99 | 99 + 2 new (mastering lists) |
| Menu tree | 351 nodes, five branches | same | root + "Exit To DOS" |
| Toggle | `!` (char 0x21) at width 640 | the same code read as VK 0x21, Page Up | setter removed; only `_editor = 0` in `VID_SetResolution` |
| Mouse | `EVM_CollectEvents_` posts 0x34–0x38 | `GetCursorPos` producer posts 0x34–0x38 | producer and imports removed |
| Per-frame call `WorksEdit_` | **none** | **none** | **none** |
| Bank at start | `editor.dat` (raw, 150 records) | same | `dreams.dat`, record 0 only |
| Bank save (F10, exit) | `editor.dat` + `dreams.dat` | same | `SaveDiskScene_` emptied |
| Asset pickers | 13-byte DOS 8.3 lists, filled at start | same | tables one byte long, fillers uncalled |

All rows **[verified in code]**: reports A and C, spec 005.

## Where the evidence comes from

| Source | What it gives |
|---|---|
| July demo `DREAMS.EXE` (DOS software) and `DREAMSFX.EXE` (DOS Glide) | Watcom v3 debug tables: every function and global of `WORKS.C` by its original name, module paths, the menu nodes as named data (`_editorObjet<path digits>`) |
| July demo `DREAMWIN.EXE` / `DRWIN.EXE` | The Windows build of the same source; bridge to retail (73 of 99 editor functions compile to the same normalized instructions as retail) |
| July demo `EDITOR.DAT` (1,305,600 bytes, 9 Jul) and `DREAMS.DAT` (100,179 bytes, 27 Jun) | Two snapshots of the bank the editor wrote; every record is named the way the editor names it |
| Retail `WINDREAM.EXE`, `GDIDREAM.EXE`, retail DOS `DREAMS.EXE`/`DREAMSFX.EXE` | The October module, its two new mastering functions and its broken data |
| Retail discs and install | `LISTL0..4.TXT` (written by the editor's code), 3D Studio and Multi-Edit leftovers, capture-named JPEGs, `ANTI-VIR.DAT` |
| Printed manual (German and Spanish scans, p.29) | Team credits |

Bulk decompilations of retail `WINDREAM.EXE`, the demo's `DREAMWIN.EXE`
and the demo's DOS `DREAMS.EXE` are in `out/research/editor/decomp/`, with
a lookup helper
(`uv run python out/research/editor/fn.py WIP_DREAMS.EXE WorksEdit_`).

## Architecture

### One module, one live record

The editor and the game share the same level record. `_CurrentSceneS` is at
once the editor's working project and the record the engine plays from
(`_ImportNewScenePtr` is the engine's current-scene pointer, used in 168
places, not an "import"). Editing a field edits the running level.
**[verified in code]** (report A §5)

```
 EDITOR.DAT (raw, 150 x 0x2200)                         DREAMS.DAT (shipped)
        |  LoadDiskScene_ (start, New Game)                   ^
        v                                                     |  WOR_SceneRLECompress_
  _tableSceneS  (the bank, 150 projects) ---- SaveDiskScene_ (F10, exit, if dirty) ---> editor.dat
        |  Q: load project / level transition                 ^
        v                                                     |  W: project save (sets dirty)
  _CurrentSceneS (working project = the live level) <-------- +
        |  Z/E/R/T create, S/D/F/G load        ^  X/C/V/B save (into the project's slot)
        v                                      |
  _CurrentSceneObjetS / LinkS / BoxS / LinkAdventureS (working sub-records)
        |  "0" key, sliders, pickers, Shift+4/5 path points
        v
  _SemaImportNewScene = 1  -->  testImportNewScene_ : reload the level from _CurrentSceneS
```

In retail the top of this chain changed: `DDAT_Load` reads the compressed
`dreams.dat` and unpacks only record 0, `DDAT_LoadRecord` unpacks one record
on demand into `_CurrentScene2S`, and `_tableSceneS` shrank to one record, so
the project pages have nothing to list. **[verified in code]** (report C §5.1)

### A semaphore state machine

About 40 `_Sema*` flags drive the editor. A key or a click on a menu button
sets one to 1; each frame `WorksGetEditor_` serves the first set flag in a
fixed priority order (vehicle mesh, event HNM, LINKADVENT, OBJET, LINK, BOX,
project, materials), and the routine it calls clears the flag when its job
or its picker page ends. While a picker page is open the menu is hidden.
With no flag set, it copies the working project's light colours into the
live ambient globals, so lighting edits show every frame. **[verified in
code]** (report A §5)

Chains run over several frames. Picking a mesh for a new OBJET sets
`_SemaSaveSceneObjet` and `_SemaSpeedLoadScene`: the OBJET is saved into the
project, the project into the bank (dirty), the record is copied back and
the level reloads with the new object in it. Loading a project (`Q`)
teleports the player into it. **[verified in code]**

### Per frame

`WorksEdit_` (demo DOS `0x22e18`, demo Windows `0x443380`, retail
`0x44d46d`):

1. if a capture flag is set, `SaveImage_` writes the frame as
   `data\tga\<3 chars>_<nnnn>.tga` (the 3 characters come from the project's
   scene file name);
2. `ShowBox_` draws the working BOX (wireframe of its min/max and its path),
   even with the editor off;
3. with `_editor` set: refill the player's three gauges to 100, draw
   `ShowBox_` and `ShowLink_` (the current level's 8 exit volumes), blit the
   cursor (`data\objet\sour.alp` frame 1), run `WorksGetEditor_`, and unless
   a page is open print "Dreams Editor" and walk the menu tree.

**[verified in code]** (report A §4; spec 005 for retail)

## The screen and how it was used

### Layout

Coordinates are game-frame pixels at 640×480 **[verified in code]**; the
drawing below is schematic.

```
(20,20) Dreams Editor                       <- 16-px font, darkened band
(20,40)   Project                           <- root rows, 10 px apart
(20,50)   Scene Particle
(20,60)   Option
(20,70)   Debug
(20,80)   Exit To DOS
            expanded node: its 5 children 10 px lower and 10 px right, bold label,
            one open path per level, so the menu grows as a cascading column
              Player Pos X          [========o=======]          -85
              ^ label at x          ^ slider at x+180, 128 px   ^ value at x+380
```

A picker page replaces the menu:

```
(20,20)  LOAD MESH                (150,20) highlighted choice
                                  (150,30) current value of the field
(20,40)  UP
(80,50..120)  8 list rows (x=160: second column, e.g. a project's scene file)
(20,130) DOWN
(20,150) EXIT
```

Delete pages add a "DELETE OBJET DELETE …" banner at (150,20). The page
hit-tests only the mouse's Y: hold the left button over a row to select,
over UP or DOWN to scroll (50 ms per step), on the title row to confirm,
on EXIT to cancel; Space confirms and Esc cancels. **[verified in code]**
(report A §3–4)

### The menu

Root children, in order: **Project**, **Scene Particle**, **Option**,
**Debug**, **Exit To DOS**. **[verified in data]**

| Branch | Contents |
|---|---|
| Project | Create (Shift A), Load (Shift Q), Save (Shift W), Delete; **Project Edit**: player start (position, angle, speed), movement (mode, handling, inertia walk/swim/fly), spheres, player flags (GUN OK, SURF OK, FALL NO OK); scene (oxygen drain, fade colour, footsteps, sky speed, kill height, AI strategy, magic regeneration, CD track); per-level camera constants; fluid (underwater tint, water level); gravity vector; intro HNM (Shift 1); lighting (medium, wave, base, palette increments, contrast, timed colour); animated materials (HNM, scroll, plasma); then the sub-record branches **Link Adventure** (Shift T/G/B), **Link** (Shift E/D/C, target scene Shift 3, conditions, volume ×1 and ×10), **Objet** (Shift Z/S/X, mesh Shift 2, symbol Shift 6, 15 behaviour flags, combat stats, position, movement, spheres), **Box** (Shift R/F/V, path points Shift 4/5, mode, volume, intensity) |
| Scene Particle | Emission volume and speeds, generation, turbulence, lifespans, quantity, player mana gain, force type, four attractors, contact plane, sprite and reset options |
| Option | Session camera constants (`K_OBJ_*`, speeds) and the five animated-material switches |
| Debug | One button: `_MaxiLoad`, whether a level's objects load at all (retail moved it to the player's Options page) |
| Exit To DOS | The quit flag `_exitDos` |

The full tree with its field bindings is
`out/research/wip_pcj/DREAMS.EXE.editor-tree.md`. The extraction left the
LINKADVENT leaves unbound because its script omitted that working record
(`_CurrentSceneLinkAdventureS`), not because the binary lacks them (report
D §1).

### Node format

64-byte nodes; the same layout in July and October. **[verified in code]**

| Offset | Field |
|---|---|
| +0x00 | label, 24 bytes |
| +0x18 | five child pointers |
| +0x2c | value pointer (an `int` field, a `_Sema*` flag or an engine global) |
| +0x30 / +0x34 | slider min / max (range only; no step) |
| +0x38 | bit mask for flag leaves |
| +0x3c | flags: 0x01 open/selected (runtime); 0x02+0x04 button (sets its value to 1); 0x40 bit toggle on the mask; 0x08/0x10/0x20 X/Y/Z and 0x80 angle captured by key `0`; 0x100/0x200/0x400 the same ×256 (particle space) |

Leaves are 128-pixel sliders dragged with the left button held; there is no
text entry. October changed one detail: the dragged value is offset by a
positive minimum. **[verified in code]** (reports A §2, C §5.7)

### Keys

`sceneKeyboard_` reads the DOS character, so the shortcut letters are
uppercase (hence "Shift" in every label) and the grid is laid out on an
AZERTY keyboard **[codes verified in code; keyboard reading unverified]**:

| Row | Project | Objet | Link | Box | LinkAdventure |
|---|---|---|---|---|---|
| create | A | Z | E | R | T |
| load | Q | S | D | F | G |
| save | W | X | C | V | B |

Shift+1 intro HNM, Shift+2 mesh, Shift+3 link target, Shift+4 add a path
point at the player, Shift+5 remove the last, Shift+6 symbol file. Copy and
paste of whole records sit on AZERTY punctuation (`?`/`.` project, `/`/`§`
objet, `:`/`!` link, `%`/`µ` box); paste creates a new record from the copy.
Key `0` writes the player's current position (relative to the scene object,
OBJET0) or angle into every visible X/Y/Z or angle leaf. **[verified in
code]** (report A §3)

The game's own hotkey handler still runs, so some keys did two things in
the same frame: `A` (interface toggle), `D`, `6` (capture), `!` (the editor
toggle itself, which on AZERTY is also "paste link"). **[verified in code;
same-frame overlap unverified]**

Other developer keys of the July hotkey handler: F10 save the bank; `6`
toggle continuous TGA capture (with Δt pinned at 2.0, 15 frames per game
second), `7` capture one frame; `r` (F3 in the Windows build) start or
stop a demo recording; F6/F7 change resolution and switch the editor off;
`8` the frame-rate readout; F1 the object HUD; `H` play the level's intro
movie. The Windows demo kept the DOS character codes as virtual-key codes,
which is how `!` became Page Up. Retail removed all of them. **[verified in
code]** (reports A §3, B §2.2–2.3, C §5.6)

### Editor mode in play

With `_editor` set the game turns into a placement mode, in July as in
retail: no collision, no camera collision, no momentum, movement ×4, no
level exits, the mouse no longer steers the camera, every actor is put back
on its record position each frame (so a saved position shows at once), and
the player's gauges are refilled. **[verified in code]** (report A §5,
spec 005 for retail)

## What it edits

The editor is the author of `DREAMS.DAT`. All 1,325 records of the July
`EDITOR.DAT` are named `<Kind><slot>` exactly as the create functions name
them, and so are all records of every bank. **[verified in data]** The
editor's menu labels name nearly every field of the record; with the retail
readers they settle most of the open meanings. The full dictionary is in
[file-formats.md](file-formats.md#project-header-fields-0x200-bytes-verified).
The editor's conventions explain several quirks of the shipped data:

- **In-use bits, not names, mark live records** (header `+0x14`, LINK
  `+0x18`, OBJET `+0x34`, BOX `+0xec`, LINKADVENT `+0x28`, bit 0). Delete
  clears the bit and at most the first letter of the name, which leaves the
  `INK0`/`BJET3` fragments and 27 named but deleted retail boxes.
- **Pickers write in place** without clearing the cell: Project 122 of the
  July bank holds `END.DSN\0.DSN`, the tail of the scene name it replaced.
- **Positions are decor-relative captures.** Many values lie outside the
  slider ranges because they were captured with `0`, not dragged.
- **Paths are recorded by walking**: a BOX holds up to 16 points and
  `+0xe4` is the index of the last one (−1 = none).
- **Event rules (LINKADVENT)** are "if all selected conditions hold, apply
  all selected actions": 13 condition bits (near, killed, held, timers,
  hidden counts) and 19 action bits (show, hide, move, change side, open
  the exit, camera cuts, inventory, light), with a source object, a
  destination object, a destination box, a timer and a dialogue ID.
- **Play-test state shipped.** The engine writes runtime values into the
  live record and a save keeps them: force-field handles in BOX `+0xf8`
  (92 retail boxes) and the "dead warrior" marker in 24 respawn boxes.

**[verified in code and data]** (report D §2, §4.6)

Bugs in the July code, all latent in the shipped data **[verified in
code]** (reports A §7, C §6.4):

- the BOX free-slot search runs 16 slots (`worksSeachFreeSceneBox_`,
  `cmp edx, 0x10`) where the record holds 12; slots 12–15 would land on the
  LINKADVENT array. No bank uses a box past `BOX9`;
- the sub-record name searches loop 150 times with their own stride, past
  the 8 or 16 slots;
- a full 16-point path makes the path drawer read a 17th point (the index
  field itself);
- Delete Box shows the "DELETE OBJET" banner and a stale list; the link
  target page uses a stale project list;
- the LINKADVENT source and destination fields store the position in the
  picker list, not the slot.

## July → October

Every `WORKS.C` function survives; most of the damage is in data.

| Change | Effect | Evidence |
|---|---|---|
| Menu node block (0x5840 bytes) deleted; "Exit To DOS" moved into the old empty node slot | Two-node tree | no node data or labels left anywhere in retail **[verified in data]** |
| The ten name tables declared one byte long | Every picker addresses `base + i`; fillers would overwrite the next module's globals (the memory-arena pointer at `0x661e2c`) | same code, strides 13/16/12 in July, 1 in October, in 20 functions at once **[verified in code; the declaration itself unverified]** |
| `_tableSceneS` cut from 150 records to one; bank loaded compressed, one record at a time | Project pages list nothing; `DDAT_InitEmptyRecords` would clear 1.3 MB past the end of BSS | **[verified in code]** |
| `SaveDiskScene_` emptied; `editor.dat` strings removed | No save path | **[verified in code]** |
| `WOR_SceneRLECompress_` writes `offset[150]` | The shipped bank has it (137,855), the July bank has 0: the October bank came from October editor code that is dead in the shipped build | **[verified in code and data]** |
| Two functions added: `0x447b72` writes `listL0..4.txt`, `0x447e7c` writes `copyL0..4.bat` | The mastering step (below) | **[verified in code]** |
| Key code byte → word; table re-based by 0x21 | The DOS punctuation cases become Page Up, Left, Delete, VK_HELP, OEM keys | **[verified in code]** |
| Mouse producer and its imports removed; toggle, F10, capture, F3 recording keys removed | No way in | **[verified in code]** |
| HNM picker scrolls by 8 and also lists `*.Hnm`; slider adds a positive minimum; directory fillers no longer called at load | Small source edits | **[verified in code]** |

Report C has the full 101-row mapping (retail address, demo Windows
address, DOS name, match quality, change).

## The mastering step

The October module has two functions the July demo lacks, both with no
caller in the shipped executable:

- `0x447b72` walks the 150 projects and, for each in-use project, writes the
  files its OBJETs, player model and animated material need into
  `listL<g>.txt`, where `g` is the project's group, record `+0x1fc`.
  Replaying it on disc 1's `DREAMS.DAT` reproduces the shipped
  `LISTL0.TXT`–`LISTL4.TXT` line for line (6, 241, 166, 222 and 164 lines).
  **[verified]** (reports C §6.1 and E §0, `listl_check.py`, `e_bank.py`)
- `0x447e7c` writes `copyL<g>.bat`, `COPY` lines that stage the same files
  plus the intro and event movies into `D:\CD1\DATA` (groups 1–2) or
  `D:\CD2\DATA` (groups 3–4), group 0 to both, and fixed lines for the
  shared files. These are the twenty `D:\CD1`/`D:\CD2` strings spec 005
  left untraced; no `copyL*.bat` ships. **[verified in code]** On the discs
  every group 1/2 file is on disc 1 and every group 3/4 file on disc 2.
  **[verified]**

So the level data drove the two-disc split and the per-chapter hard-disk
install lists that `CD_PrepareLevel` reads. The retail DOS executables carry
the same `COPY` strings (report E §3).

## Where the editor was wired

No build calls `WorksEdit_` or holds its address: not the two July DOS
builds, not the two July Windows builds, not the three October builds
(Windows, DOS software, DOS Glide), with LE fixups applied so that a pointer
in data would have shown. The call was removed before the July demo was
linked. **[verified in code]** (report B §1)

The per-frame handler is the same function in every build, so the gap can
be placed by what `WorksEdit_` needs:

| Step | Demo DOS `MCM1_Dispatcher_` `0x11094` | Demo Windows `0x414781` | Retail `GAME_TickFrame` `0x416d45` |
|---|---|---|---|
| decode events (key byte, mouse 0x34–0x38) | entry | entry | entry |
| present the previous frame | `flipVideo_` `0x11249` | `0x41395c` | `VID_Swap` `0x417021` |
| input, demo record/replay | `CTRL_TestControle_`, `CTRL_SaveJoyStatus_`/`CTRL_Replay_` | same | `INPUT_UpdateActions`, `DEMO_*` |
| game tick and 3D render | `CompPut3DObjet_` `0x1127b` | `0x41fcc2` | `GAME_Tick` `0x417078` |
| HUD | `MENJ_AfficheMenu_` | `0x42befd` | `UI_DrawHud`, `UI_DrawKeyHelp` (new) |
| object HUD | `PutDebugInfo_` `0x1128a` (absent from the Glide demo) | `0x41414d` | `DBG_DrawObjectInfo` `0x41708c` |
| ← **candidate 2** | `0x1128f` | `0x414a72` | `0x417091` |
| timing; Δt 2.0 while `_SaveImageAllTime` | `_dt` | same | `0x417268` |
| Frame Rate / Mem 3DTR unless `_editor` | `0x113c1` | `0x414bb3` | `0x4172a2` |
| ← **candidate 1** | `0x114c1` | `0x414c9d` | `0x41743a` (the recomp's choice) |
| hotkeys | `DreamsKey_` | `0x4139a6` | `GAME_HandleHotkeys` |

**[verified in code]** (report B §1.2)

- **After the render.** `SaveImage_` captures the buffer drawn this frame
  and `ShowBox_`/`ShowLink_` draw into it; called earlier, the render would
  erase both. **[verified in code; conclusion unverified]**
- **Before the hotkeys.** Every picker page zeroes the key byte after Esc or
  Space; the only later reader in the frame is `DreamsKey_`, whose cases
  include Esc and Space (stop a video). That zeroing only makes sense if the
  hotkeys ran after the editor. **[verified in code; conclusion unverified]**
- **Not a hook.** `0x4aa704`, which spec 005 tried as a data route, is
  `_PreRender` of the BEN11 library (`3DC_MEM.C`, setter `New_PreRender_`,
  uncalled everywhere). It runs before the span flush repaints the view and
  again per shadow pass, so a 2D panel drawn there is painted over.
  `WorksEdit_` is not a message handler and no pointer table holds it.
  **[verified in code]**

That leaves two sites, both inside the same tail, which the binaries cannot
tell apart (no line table for `DREAMS.C`; an unoptimized call leaves no
gap). Candidate 1, just before the hotkeys, keeps the editor next to the
other `WORKS.C` state the tail already handles (the capture Δt pin, the
`_editor`-gated readout at the same top-left position, the `_exitDos`
return) and does not depend on `PutDebugInfo_`, which the Glide demo
dropped while keeping the rest; candidate 2 groups it with the debug draw
and skips it during videos. **[unverified]** A developer capture would
decide it, since at candidate 1 a TGA includes the frame-rate readout and
at candidate 2 it does not. The only ones known, the seven December 1996
`DATA\TGA\TEMP\*.JPG` on disc 2, are clean 800×600 shots with no HUD,
readout or panel (one is the title card), so they do not. **[verified]**

One oddity either way: `!` is both the July editor toggle (in `DreamsKey_`)
and "paste link" (in `sceneKeyboard_`), so in editor mode one press would
paste a link and then leave the editor. **[verified in code; effect
unverified]**

## Switched off

The editor flag itself was never cut: the same 17 functions read it 25
times in every build, July and October, DOS and Windows. Only the setter
went; retail's one write, `_editor = 0` in `VID_SetResolution`, is what is
left of the July F6/F7 resolution keys. **[verified in code]** (report B
§2.4)

| Feature | July demo | Retail | Notes |
|---|---|---|---|
| Editor panel (`WorksEdit_`) | no caller | no caller | needs a restored call |
| Editor mode (noclip, no exits, ×4 movement, actors pinned) | **live**: `!` (DOS) / Page Up (Windows) at 640 wide | no setter | the July demo plays as a noclip mode without a panel **[verified in code; not run]** |
| Mouse producer | **live** (DOS `EVM_CollectEvents_`, Windows `0x42029a`) | removed, with `GetCursorPos`/`ScreenToClient` | retail DOS has no mouse library either |
| Frame Rate / Mem 3DTR (`_SemaPrintFrameRate`, retail `0x49d5c0`) | key `8` | never written | |
| Object HUD (`_debugInfo`, `0x49d5d0`) | F1 | never written | not called in the Glide demo |
| HUD on/off (`_FlagAfficheInterface`, `0x49d5d4`) | key `A` | stuck at 1 | |
| TGA capture and Δt pin (`_SaveImageAllTime`/`ThisTime`, `0x4a4758`/`0x4a475c`) | keys `6`/`7`; the pin works, the capture needs the panel | never written | spec 005's "step 2.0" is half of the capture mode |
| Demo recorder (`_FlagReplayMode`, `0x49d34a`) | **live**: `r` (DOS) / F3 records `data\replay.bin`; the title menu and its idle timeout play it | record start and load dead | the July `REPLAY.BIN` holds 1,848 frames; retail ships a 3-frame file of 104-byte frames while its writer uses 112 |
| Collision wireframe (Backspace) | white | colour 1, nearly black | inside the collision gate |
| "Collision view" (`_build_list`, `0x4ac8c8`, `3DC_LIST.C`) | never written | never written | a library debug list nobody reads, not an editor feature |
| Renderer profiler (`_rendertype`, `3DC_PROF.C`) | never written | DOS Glide gate `0x104f38` never written; removed from Windows | |
| BOX gizmo `ShowBox_`, exit volumes `ShowLink_` | inside the dead panel | same | the working BOX is filled only by editor pages |
| "Debug" menu leaf `_MaxiLoad` | toggles whether a level's objects load | node gone; flag reused on the Options page | |
| `printmemory_` (DPMI free-memory dump) | no caller | no caller (DOS); empty (Windows) | |
| Render-class keys `e f l v` (`Comp3dEngineClavier_`) | live | removed | |
| Other July keys: `I` inventory, `H` level movie, `D` message 0x40, `9`/`-` camera, `M`/`*` menus | live | removed | |

**[verified in code]** (report B §2–3)

The retail DOS executables keep the retail-trimmed editor: the two-node
tree, the uncalled panel (`0x25828` software, `0x357c8` Glide), the 17
gates, no setter, no mouse, and the mastering `COPY` strings. **[verified
in code]** (report B §4)

## How the team worked: a reconstruction

Everything in this section is **[unverified]**: a reading of the verified
facts above, not a record.

**People.** The manual credits two programmers (Emmanuel Chriqui, Olivier
Denis), an interface programmer (Frédéric Mouveaux), four R&D engineers who
supplied the 3D library, codecs and tools (Benoît Hozjan, Olivier Nemoz,
Hubert Nguyen, Pascal Urro) and a small authoring group (Hatem Benabdallah,
Yann Mallard, with Chriqui and Denis) **[sourced: manual p.29, local scans]**.
The game code (`C:\DREAMS\src`, 49 modules) and the engine library
(`C:\SOURCES\BEN11\3DC_*.C`, a separate tree, the Glide build on
`E:\ENGINE\BEN11`) were built apart; Cryo's R&D "was there to provide
engines, libraries and different tools for all the production teams"
**[sourced:
[Adventure Classic Gaming, 2008](http://www.adventureclassicgaming.com/index.php/site/interviews/313/)]**.
The editor is game code, not an R&D tool: it lives in the game's tree and
uses the game's globals.

**Assets.** Artists modelled in 3D Studio R4 under DOS (the demo's
`SALLE.PRJ` points at `C:\3DS4`, `D:\PROD_3D\REFERENC` and an art
director's `F:\HATEM`); conversion to `.DSN` scenes, `.DAN` characters and
`.3DC` props happened outside the game (no converter survives). Files were
dropped into `data\3dc`, `data\hnm`, `data\anim` and `data\sym`, where the
editor's pickers listed them with `_dos_findfirst`; Windows 95 long video
names survive in the bank as their 8.3 aliases (`TETE_E~1.HNM`), which is
what such a picker returns.

**A level session**, on the DOS build at 640×480 with an AZERTY keyboard and
a mouse driver:

1. Start the game: it loads `editor.dat`, the shared working bank, and
   plays Project 0. Press `!` (Page Up on Windows): the game quick-saves to
   `data\game.dat` and the editor opens; play becomes a noclip walk.
2. `Shift+Q` to pick the project to work on; the game jumps into it. Or
   `Shift+A` for a new one, named `Project<n>`, then pick its scene mesh.
3. Walk to the spawn, press `0` on the Player Pos leaves; set movement,
   water level, kill height, camera constants and light colours with the
   sliders, watching the level change live.
4. `Shift+Z` for each actor: pick its `.DAN` or `.3DC`, walk to its place,
   `0`, set its flags (friend or enemy, runs, follows, shadow) and combat
   numbers, `Shift+X`; the level reloads with the actor in it. Copy and
   paste (`/`, `§`) clone a tuned actor.
5. `Shift+E` for an exit: `Shift+3` picks the target project, `0` on the
   min and max corners while standing at them, conditions (enemies dead,
   key held, event done), `Shift+C`. `Shift+R` for a volume or a patrol,
   force-field or spawn path: walk it and press `Shift+4` at each point.
6. `Shift+T` for the scripted events of the level: source and destination
   object, conditions, actions, the movie and the dialogue line.
7. `Shift+W` saves the project into the bank; F10 writes `editor.dat` and
   `dreams.dat`. Press `!` again to play the level for real from the
   quick-save.
8. For screenshots and magazines: `7` grabs one frame, `6` records every
   frame at a fixed step into `data\tga` (the disc's `DATA\TGA\TEMP\E09_0000.JPG`
   and its siblings, December 1996, carry that naming). `r` recorded the
   attract-mode demo that the title screen plays when idle
   (`data\replay.bin`, 1,848 frames, recorded on the July build day).

Several people editing one `editor.dat` by hand-copying is consistent with
the leftovers (the personal `DATA\3DC\C.BAT` sync script, the `Z:\` network
fallback for `.BF` icon archives), but nothing shows how banks were merged.

**Between July and October** the banks show what the designers did: 51 of
the 134 projects changed in the twelve days before the demo (intro movies,
placeholder models replaced, NPC attack values halved, 22 new events); by
October 12 more projects, 26 rebuilt levels, events from 111 to 328,
dialogue-linked events from 19 to 166, and CD tracks from 24 to 127 levels
(report D §4). The shipped values exceed the July slider ranges, so the
October tree differed. The last step was mastering: a group number per
project, `listL`/`copyL` from the editor, copies staged to `D:\CD1` and
`D:\CD2`, a ThunderByte checksum pass over the trees on 9 October, and the
final `DREAMS.DAT` of disc 1 on 29 October, the day the executables were
linked.

**Then the release build.** The editor's way in was cut (menu data, keys,
mouse, save, bank size) rather than its code, which Watcom keeps whole: the
shipped game is a reduced configuration of a source whose full
configuration still worked, since the October bank and lists came from it.

## Bringing it back in the recomp

[Spec 008](../specs/008-editor-restoration/spec.md) turns this into a
phased plan with decisions and acceptance checks, and also gathers the
address map and the inferred workflows in detail. In short: spec 005
already runs the retail editor (keypad 5: flag plus a restored draw call;
mouse events from the host), and the July data gives a path to the complete
tool without changing a retail instruction:

1. **Graft the July menu.** The node format is unchanged, the retail tree
   walker (`0x44d346`) and leaf editor (`0x44cd62`) draw any tree, and the
   retail working records sit at known addresses (report C §8: `_CurrentSceneS`
   `0x65fb04`, OBJET `0x65f8c4`, LINK `0x65d644`, BOX `0x65b244`,
   LINKADVENT `0x65d604`; `_Sema*` flags `0x4a46b4`–`0x4a474c`). A host-built
   tree pointing there would give every field editor. It is host-only
   structure and must say so; each binding has to be checked against the
   retail reader first (header `+0x140`, `+0x144` and `+0x1c0..+0x1cc`
   changed meaning).
2. **Real picker lists.** The one-byte stride is compiled into 20
   functions (`base + i`), so bigger buffers alone do not help: the fillers
   and the list parts of the picker pages need host replacements (spec 005
   work item 3), with the July `GetAll3dcInDirectory_` and its 13-byte
   entries as the reference.
3. **A bank to edit.** Retail holds only the current record, and the
   project pages and the mastering generators still walk 150 records from
   `0x659044`, over other globals. They need host replacements working on a
   host bank of 150 unpacked records; a host `SaveDiskScene_` would then
   write a `DREAMS.DAT` under `out/`.
4. **Keys.** Map the DOS punctuation cases to keys a Windows or QWERTY
   player can reach.

## Corrections made with this pass

- [sprites-ui-dialog.md](sprites-ui-dialog.md): oxygen drains by project
  `+0x114`, not `+0x118` (`0x4234bd`); `+0x118` regenerates magic
  (`0x4235c8`); "opcode `0x40`" is the dialogue message, not a condition
  value (also in [asset-access.md](asset-access.md)).
- [install-and-discs.md](install-and-discs.md): the level's music is one
  track, the low byte of `+0x11c`.
- [file-formats.md](file-formats.md): OBJET `+0x3c` is attack strength,
  `+0x74..+0x98` are combat and movement stats, `+0x1c` a symbol file;
  header `+0x11c` is one CD track (`and edx, 0xff` at `0x42f1b4`), `+0x138`
  the exit fade colour, `+0x1f8` the level-entry save mode, `+0x8c` the
  player model replacement; LINK `0x40` inverts the volume test; BOX
  `+0xe4` is a last index; LINKADVENT `+0x20`/`+0x24` are bitmasks; activity
  is a bit, not a name. Missing fields added.
- [spec 005](../specs/005-debug-tools/spec.md): the TGA name has an
  underscore, the picker tables have ten bases, the BOX working copy is
  never filled outside the editor (the box seen with keypad 5 is most likely
  `ShowLink_`'s exit volume), and the open questions on the mouse, the copy
  commands and the gizmo offset are answered.
- Decoder `src/dreams/formats/project.py` (not changed here): `cd_track`
  reads `+0x1f8`, `radius` reads strength, liveness is taken from the name,
  BOX points stop one short.

## Open questions

- Where `WorksEdit_` was called: candidate 1 (before the hotkeys) or 2
  (after the object HUD). Only a developer capture taken with the
  frame-rate readout on would tell; none is known (see *Where the editor
  was wired*).
- What triggered the two mastering functions in the developers' build.
- Whether the July Glide build already read `+0x1c0..+0x1cc` as fog.
- How LINKADVENT source and destination indexes are read: the editor
  stores the position in its picker list, which differs from the slot in 24
  of 189 July references.
- Who wrote BEN11 (Benoît Hozjan and Hubert Nguyen are the candidates) and
  who "Stef" of `PutSpriteStef_` is.
- Whether the October developers' tree had more nodes: shipped values exceed
  the July slider ranges (oxygen 10000, fog density 75, dialogue 177).
