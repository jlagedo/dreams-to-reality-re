# 008 — The Cryo level editor: what survives, how it was used, and bringing it back

Status: **Draft. Part A (knowledge) is complete to the level of static
analysis of all six July and October builds plus the banks and discs. Part B
(restoration) is designed, not started; phase 0 is what spec 005 already
runs (keypad 5, host mouse, one inserted draw call). Owner decisions of
2026-10-03: the editor is for players too (modding) and always available in
our builds, Page Up opens it as in July, the July menu is bundled with the
release, copy/paste on Ctrl+1..4 / Ctrl+Shift+1..4, and one milestone covers
every phase.**
Date: 2026-10-03
Depends on: [000 the recomp](../000-the-recomp/spec.md),
[005 retail debug tools](../005-debug-tools/spec.md),
[007 launcher and user data](../007-port-launcher/spec.md) (where written
files go)
Research: [cryo-editor.md](../../research/cryo-editor.md) (overview),
[file-formats.md](../../research/file-formats.md#the-record-codec-and-layout-verified)
(the record the editor edits),
[wip-editor-discovery.md](../../research/wip-editor-discovery.md) (first
pass over the demo)

## Goal and boundary

Dreams to Reality was laid out with an editor built into the game. The
retail executables keep almost all of its code and none of the way in. This
spec does two things:

- **Part A** records everything known about the editor: what each build
  keeps, how it is built, what it looks like, how it is driven, what it
  edits, how it changed between July and October 1997, and, marked as
  inference, how the team used it.
- **Part B** specifies how the recomp brings the complete editor back on
  top of the retail code: the July menu, working file pickers, a project
  bank, saving and export. It ships to players as a modding tool: the
  release package is our binary plus bundled resources, and the player
  supplies only the disc images.

Boundary for Part B, from [AGENTS.md](../../../AGENTS.md) and spec 005:

- No retail instruction changes. The recomp may insert calls (`lift.py`
  `CALLS`), replace whole entries (the replacement table), write data, and
  allocate guest memory (`shim_alloc`). Each such piece is host-only
  structure and says so in its comments and in this spec.
- Before any host glue binds a retail field, the field's retail readers are
  traced: valid values (including `0`, `1`, `−1` and `"EMPTY"`), coordinate
  space (scene object relative, see A5.3) and which retail function writes
  it. A July menu label is a lead, not proof: three header fields changed
  meaning by October.
- No game data in the repository. Anything taken from a game file (the July
  menu nodes, record contents) is generated under `out/` at build time,
  like the lifted code. The release package may bundle such generated
  resources beside the binary (owner decision, 2026-10-03: "we will
  distribute the binary and anything necessary to make it work"; the player
  provides only the disc images). This extends spec 007, whose port reads
  all game data from the player's images.
- Nothing is written to a disc image or to the install tree. Banks and lists
  go under `out/` (development) or the user-data directory of spec 007.
- The editor is a player feature, so it may not depend on the development
  control channel (`WD_DEVTOOLS`); tests drive it through that channel, but
  the editor code itself builds without it.

Out of scope: the DOS builds of the recomp (there are none), editing model
or scene geometry (the editor never did), and a new editor UI. The aim is
the original tool, working.

Evidence tags follow [the docs index](../../README.md#evidence-tags), with
**[verified in code]** for facts read in a disassembly or decompilation.
Report letters (A–E) refer to the five research reports under
`out/research/editor/reports/` (local, game-derived, not committed).

---

# Part A — What we know

## A1. Sources

| Build | File | Linked | What it gives |
|---|---|---|---|
| July demo, DOS software | `DREAMS.EXE` (`E:\dev_game\DREAMS_ISOS\DREAMS_WIP_PCJ\DREAMS`) | 1997-07-10 | Watcom v3 debug tables: original names of every function and global, source paths, the 351 menu nodes as named data |
| July demo, DOS Glide | `DREAMSFX.EXE` | 1997-07-10 | the same, plus engine type tables |
| July demo, Windows | `DREAMWIN.EXE`, `DRWIN.EXE` (two bytes apart) | 1997-07-10 | the Windows build of the same source; the bridge to retail |
| Retail, Windows | `WINDREAM.EXE`, `GDIDREAM.EXE` (the recomp's input) | 1997-10-29 | the October module |
| Retail, DOS | `DREAMS.EXE`, `DREAMSFX.EXE` (disc 1) | 1997 | the same October cut |
| July banks | `EDITOR.DAT` (1,305,600 bytes, 9 Jul), `DREAMS.DAT` (100,179 bytes, 27 Jun) | | two snapshots of the bank the editor wrote |
| Retail banks | `DREAMS.DAT` on disc 1 (later) and disc 2 | | the shipped export |
| Discs | `LISTL0..4.TXT`, `DATA\TGA\TEMP\*.JPG`, 3D Studio and Multi-Edit leftovers | | by-products of the editor and the pipeline |
| Manual | German and Spanish scans, p.29 | | credits |

Bulk decompilations of retail `WINDREAM.EXE`, demo `DREAMWIN.EXE` and demo
DOS `DREAMS.EXE`: `out/research/editor/decomp/`, read with
`uv run python out/research/editor/fn.py <program> <name-or-address>`
(`--grep REGEX`, `--range LO HI`). The July Ghidra project is
`ghidra/wip/dreams-july-1997.gpr`.

## A2. Survival by build

| | Demo DOS | Demo Windows | Retail Windows | Retail DOS |
|---|---|---|---|---|
| `WORKS.C` functions | 99 | 99 | 99 + 2 (mastering lists) | trimmed like Windows |
| Menu tree | 351 nodes | 351 nodes | 2 nodes | 2 nodes |
| Editor flag setter | `!` at width 640 | Page Up at width 640 | none (`VID_SetResolution` writes 0) | none |
| Gameplay gates on the flag | 17 functions, 25 reads | same | same | same |
| Mouse producer | `EVM_CollectEvents_` (`int 33h` library) | `0x42029a` (`GetCursorPos`) | removed with its imports | removed |
| Call of the editor frame `WorksEdit_` | none | none | none | none |
| Bank load | `editor.dat`, 150 raw records | same | `dreams.dat`, record 0 unpacked | same |
| Bank save | F10 and exit: `editor.dat` + `dreams.dat` | same | `SaveDiskScene_` emptied | same |
| Asset lists | 13-byte entries, filled at start | same | one-byte tables, fillers uncalled | same |

**[verified in code]** (reports A, B, C)

## A3. Address map

Retail addresses are `WINDREAM.EXE` = `GDIDREAM.EXE`. The DOS→retail offset
for `WORKS.C` initialized data is `+0x3df34c`. **[verified in code]**
(reports B §0, C §7–8)

### Functions

| Original name | Demo DOS | Demo Windows | Retail | Retail name / note |
|---|---|---|---|---|
| `SaveImage_` | 1e5a4 | 43e42d | 4479c7 | TGA capture |
| — | — | — | 447b72 | writes `listL0..4.txt` (retail only) |
| — | — | — | 447e7c | writes `copyL0..4.bat` (retail only) |
| `printmemory_` | 1e710 | 43e5d8 | 44869c | empty in Windows |
| `GetAll3dcInDirectory_` | 1e818 | 43e5f9 | 4486bd | `.3dc .dan .dsn` |
| `GetAllHnmInDirectory_` | 1e948 | 43e732 | 4487ed | `.ubb` (+ `.Hnm` retail) |
| `GetAllMap_` | 1e9a8 | 43e7a9 | 4488bf | scene materials |
| `GetAllAnimInDirectory_` | 1ea0c | 43e824 | 448937 | `data\anim\*.hnm` |
| `GetAllSymInDirectory_` | 1ea6c | 43e89b | 4489ab | `data\sym\*.sym` |
| `Push/PopCurrentScene_` | 1eacc/1eaf4 | 43e912/43e948 | 448a1f/448a55 | project copy/paste |
| `Push/PopCurrentSceneObjet_` | 1eb68/1eb90 | 43e998/43e9cd | 448aa5/448ada | |
| `Push/PopCurrentSceneBox_` | 1ec28/1ec50 | 43ea30/43ea65 | 448b3d/448b72 | |
| `Push/PopCurrentSceneLink_` | 1ecec/1ed14 | 43eac8/43eafd | 448bd5/448c0a | |
| `worksClearAllScene_` | 1edb0 | 43eb60 | 448c6d | `DDAT_InitEmptyRecords` |
| `WOR_RLECompress_` | 1ee30 | 43ec4b | 448d58 | |
| `WOR_RLEDecompress_` | 1ee8c | 43ed18 | 448e25 | `RLE_UnpackZeros` |
| `WOR_SceneRLECompress_` | 1eee4 | 43edbc | 448ec9 | writes `offset[150]` in retail |
| `LoadDiskScene_` | 1ef30 | 43ee4a | 448f5f | `DDAT_Load` |
| `SaveDiskScene_` | 1efd4 | 43eef1 | 44900a | empty in retail |
| `GetSeachSceneObjetAnimName_` / `MapName_` / `HnmName_` | 1f0b0/1f340/1f5d0 | 43efe9/43f293/43f53d | 44902b/4492d1/449577 | picker pages |
| `worksSeachFreeScene_` | 1f864 | 43f7ea | 449822 | |
| `ListSeachSceneName_` / `GetSeachSceneName_` | 1f89c/1f934 | 43f857/43f8ef | 44988f/44991e | project list and page |
| `worksSeachSceneName_` | 1fbd8 | 43fbde | 449bf9 | `DDAT_LoadRecord` |
| `worksCopyScenetoCurrent_` / `CurrenttoScene_` | 1fccc/1fcf0 | 43fd13/43fd49 | 449e0e/449e44 | `DDAT_CopyRecord` |
| `WorksCreat/Load/Save/DeleteScene_` | 1fd14/1fdec/1fe3c/1fe78 | 43fd7f/43fdee/43fe47/43fe85 | 449e7a/449ee9/449f42/449f80 | |
| Objet set (11 functions) | 1fef0–20440 | 43ff22–44054d | 44a01d–44a62b | |
| `GetSeachSceneObjet3dcName_` / `SymName_` | 204cc/20760 | 440606/4408b6 | 44a6e4/44a990 | "LOAD MESH", "LOAD SYMBOLE" |
| LinkAdventure set (11) | 209f4–20ecc | 440b64–441133 | 44ac3a–44b1f9 | |
| Link set (13) | 20f58–21524 | 4411ec–4418e8 | 44b2b2–44b9a7 | |
| Box set (14) | 21584–21ad0 | 441970–441fb4 | 44ba2f–44c06c | |
| `CompCreatSceneBoxInputPath_` / `OutputPath_` | 21b54/21bf4 | 442063/4420f1 | 44c11b/44c1a9 | Shift+4 / Shift+5 |
| `sceneKeyboard_` | 21c2c | 4421d6 | 44c28c | key table `0x44c1f7`, jumps `0x44c214` |
| `ShowBox_` / `ShowLink_` | 21fe8/220a4 | 44237a/442459 | 44c440/44c51f | 3D wireframes |
| `WorksGetEditor_` | 22158 | 44255f | 44c625 | semaphore dispatcher |
| `WorksEditDesSelectObjet_` | 22864 | 442c3a | 44cd00 | |
| `WorksEditEditObjetData_` | 22888 | 442c9c | 44cd62 | leaf editor |
| `WorksEditSelectObjet_` | 22c18 | 4430b9 | 44d1a6 | child rows |
| `WorksEditCompObjet_` | 22d38 | 443259 | 44d346 | tree walker |
| `WorksEdit_` | 22e18 | 443380 | 44d46d | editor frame, uncalled |
| `InitWorksSprite_` | 22f08 | 443491 | 44d57e | live |
| `CompPrintSprite_` | 22f38 | 4434da | 44d5c7 | `TEXT_DrawString`, live |
| `CompWorksSpriteCpu_` | 22f60 | 44356b | 44d658 | CPU-load bars, dead |
| `PutVideoBox_` / `PutVideoSpaceLine_` (ENGINE.C) | — | — | 41bacb/41bed6 | wireframe box, path polyline |
| `EditorSetPosObjet3dS_` (ENGINE.C) | — | 41b6ae | 41ec38 | `ENT_ResetToSpawn` |
| `PutSpriteC_` | — | — | 4255d0 | cursor blit |
| `MCM1_Dispatcher_` | 11094 | 414781 | 416d45 | `GAME_TickFrame` |
| `DreamsKey_` | 10340 | 4139a6 | 415aa7 | `GAME_HandleHotkeys` |

### Data

| Original name | Demo DOS | Demo Windows | Retail | Role |
|---|---|---|---|---|
| `_SemaSpeedLoadScene`, `_SemaCreatScene` … `_SemaLoadMaterialHnmName` | c5368… | — | 4a46b4…4a474c | the ~40 command semaphores |
| `_TgaScene`, `_TgaSceneImage` | c5404, c5408 | 495cb4 | 4a4750, 4a4754 | capture counters |
| `_SaveImageAllTime`, `_SaveImageThisTime` | c540c, c5410 | 495cbc, 495cc0 | 4a4758, 4a475c | capture flags (and the Δt 2.0 pin) |
| `_sceneIdent` … `_sceneBoxIdent` | | | 4a4760–4a4770 | free slot found for each kind |
| `_editorMouse` | c542c | 495cdc | 4a4778 | July Ctrl+\ toggle, never read |
| `_editor` | c5430 | 495ce0 | 4a477c | the editor flag |
| `_exitDos` | c5434 | 495ce4 | 4a4780 | quit; "Exit To DOS" value |
| `_editorObjetNull` / "Exit To DOS" node | c5438 | | 4a4784 | retail puts the exit node here |
| `_editorObjetMain` (root) | c5478 | 495d28 | 4a47c4 | "Dreams Editor" |
| `_SceneComeBack` | cacf8 | | 4a4804 | "return to the previous level" flag |
| `_nameSceneBack` | | | 633bf4 | previous level's name |
| `_RLE_SAVES` | | | 633c14 | compressed bank, 0x25400 bytes |
| `_tableSceneS` | 14efa0 | 63dcc8 | 659044 | the bank: 150 × 0x2200 in July, **one** record in retail |
| `_CurrentSceneBoxS` | | | 65b244 | working BOX |
| `_CurrentScene2S` | | | 65b344 | `DDAT_LoadRecord` output |
| `_CurrentSceneObjetPushS` | | | 65d544 | objet clipboard |
| `_CurrentSceneLinkAdventureS` | 2953b0 | | 65d604 | working LINKADVENT |
| `_CurrentSceneLinkS` | | | 65d644 | working LINK |
| `_CurrentScenePushS` | | | 65d6c4 | project clipboard |
| `_CurrentSceneObjetS` | | | 65f8c4 | working OBJET |
| `_CurrentSceneBoxPushS`, `_CurrentSceneLinkPushS` | | | 65f984, 65fa84 | box and link clipboards |
| `_CurrentSceneS` | 2978b0 | | 65fb04 | working project = the live level record |
| `_ObjetReference` | | | 661d04 | position captured by key `0` |
| `_SemaSaveSceneDisk` | | | 661da4 | bank dirty flag |
| `_Objet0` | | | 661da8 | slider sprite descriptor |
| `_ImportNewScenePtr` | 299bb0 | | 661e04 | the engine's current-project pointer |
| `_SemaImportNewScene` | | | 661e08 | level reload request |
| ten name tables | (13/16/12-byte entries) | | 661e1e–661e27 | one byte each in retail |
| mouse: x, y, dx, dy, buttons, click | PERIPH.C `0xcaeac`… | | 4a314c, 4a3150, 4a3154, 4a3158, 4a315c, 4a3160 | |
| key code | `_clavierChar` 299c1c (byte) | 611f38 (byte) | 626fd8 (word) | |

Retail addresses still to pin before Part B binds them: `_LoadSaveSceneSPtr`
and the four sub-record `…SPtr` (in `0x661d14`–`0x661da0`), the `_table*Nb`
/ `Offset` / `StartOffset` dwords, `GetObjetReference_`, and the engine
globals the July menu binds (A4.4).

## A4. How it is built

### A4.1 One module, one live record

`WORKS.C` is game code: it lives in `C:\DREAMS\src` with the game's other
49 modules, not in the R&D engine library (`C:\SOURCES\BEN11`). Its working
project `_CurrentSceneS` is the record the engine plays from;
`_ImportNewScenePtr`, used in 168 places, points at it after any load. An
edit to a field edits the running level. **[verified in code]** (report A
§0, §5)

```
 EDITOR.DAT (raw, 150 x 0x2200)                          DREAMS.DAT (shipped)
        |  LoadDiskScene_ (start, New Game)                    ^
        v                                                      |  WOR_SceneRLECompress_
  _tableSceneS (bank) ------------ SaveDiskScene_ (F10, exit, if dirty) -----> editor.dat
        |  Q: load project; level transitions copy the record  ^
        v                                                      |  W: project save (bank dirty)
  _CurrentSceneS (working project = live level) <------------- +
        |  Z/E/R/T create, S/D/F/G load       ^  X/C/V/B save into the project's slot
        v                                     |
  _CurrentSceneObjetS / LinkS / BoxS / LinkAdventureS (working sub-records)
        |  sliders, key 0, pickers, Shift+4/5
        v
  _SemaImportNewScene = 1  ->  testImportNewScene_ (retail SCENE_LoadLevel): reload from _CurrentSceneS
```

### A4.2 The frame

`WorksEdit_` (retail `0x44d46d`) per frame **[verified in code]**:

1. if `_SaveImageAllTime` or `_SaveImageThisTime`: `SaveImage_` writes the
   frame buffer as `data\tga\%s_%04d.tga` (`%s` = first 3 characters of the
   project's scene file name, `+0x60c`; counter `_TgaSceneImage`) and clears
   the one-shot flag;
2. `ShowBox_` (runs even with the editor off; draws nothing unless the
   working BOX is in use);
3. if `_editor`: set the player's three gauges (`+0x38/+0x3c/+0x40`) to
   100.0; `ShowBox_`, `ShowLink_`; blit the cursor (sprite set 10 =
   `data\objet\sour.alp`, frame 1); `_mouseYPos += 6`; `WorksGetEditor_()`;
   unless a page is open, print "Dreams Editor" at (20,20) and walk the tree
   from the root at (10,30); `_mouseYPos -= 6`.

### A4.3 The semaphore state machine

About 40 `_Sema*` flags. A key or a menu button sets one to 1;
`WorksGetEditor_` serves the **first** set flag each frame in this order and
returns non-zero while a page is open **[verified in code]** (report A §5):

vehicle mesh → LINKADVENT HNM → LINKADVENT create / load / delete / save →
LINKADVENT source objet / destination objet / source box / destination box
pickers → OBJET create (then opens LOAD MESH) / load / mesh / symbol / save /
delete → LINK create / load / delete / save / target scene / held objet →
BOX create / load / save / delete / path in / path out → intro HNM → project
create / delete / save / load / quick reload → material plasma / scroll /
HNM material / HNM file → otherwise copy the working project's light colours
(`+0x18..+0x38`) into the live ambient globals and return 0.

Multi-frame chains:

- **Sub-record save** (`X`, `C`, `V`, `B`): working record → its slot in
  `_CurrentSceneS` (LINK and BOX min/max sorted first), then
  `_SemaSaveScene`.
- **Project save** (`W`): `_CurrentSceneS` → `*_LoadSaveSceneSPtr` (the bank
  record bound by the last load or create), `_SemaSaveSceneDisk = 1`. At
  start the pointer is 0, so a project must be loaded or created first.
- **Mesh pick** (after `Z`, or `2`): if not `"EMPTY"`, `_SemaSaveSceneObjet`
  and `_SemaSpeedLoadScene` are set: OBJET → project → bank → copied back →
  reload, and the object appears.
- **Project load** (`Q`): picker, then `_ImportNewScenePtr = &_CurrentSceneS`,
  `_SemaImportNewScene = 1` and an immediate `testImportNewScene_`: the
  player is moved into the chosen level.
- **Delete**: picker over a "DELETE …" banner; clears the in-use bit (and the
  first letter of the name for OBJET, LINK, LINKADVENT).
- **New Game** calls `LoadDiskScene_` again: unsaved edits are lost.

### A4.4 What the menu binds

259 leaves. By target **[verified in data]** (report A, `tree_decoded.tsv`):

| Target | Leaves | Notes |
|---|---:|---|
| `_CurrentSceneS` (project header) | 98 | +0x14 … +0x1f4 |
| `_CurrentSceneObjetS` | 37 | |
| `_CurrentSceneLinkAdventureS` | 32 | condition/action bits, times |
| `_CurrentSceneLinkS` | 18 | |
| `_CurrentSceneBoxS` | 8 | |
| `_Sema*` buttons | ~45 | create/load/save/delete/pickers |
| Engine globals | ~21 | `_TableParticleGravity` (attractor 0), `_TablePlaneGravity` (contact plane), `_K_OBJ_*`, `_Y_LOW/HIGH_FOLLOW_OBJ`, `_SpeedCamera`, `_SpeedTarget`, `_MAP_Anim*Ok` (5), `_ParticuleSprite`, `_particule0Immortel`, `_particuleResetNeg`, `_MaxiLoad`, `_exitDos` |

The engine-global leaves are session tweaks, not bank data: the Option
branch tunes the live camera, then the per-project camera fields under
Project carry values into the record.

### A4.5 Node format

0x40 bytes, the same in July and October **[verified in code]**:

| Offset | Field |
|---|---|
| +0x00 | label, 24 bytes |
| +0x18 | five child pointers (hence "Flags 2...", "Misc Scene 2...": the menu nests to stay within five) |
| +0x2c | value pointer: an `int` field, a `_Sema*` flag or an engine global |
| +0x30 / +0x34 | slider min / max |
| +0x38 | bit mask |
| +0x3c | flags |

| Flag | Meaning | Nodes |
|---|---|---|
| 0x01 | runtime: selected / expanded | — |
| 0x02 + 0x04 (6) | button: reaching it toggles its value to 1, deselects, waits 50 ms; sticky (does not collapse siblings) | 49 |
| 0x40 | bit toggle on the mask: shows `(v & mask) != 0`, writes `(v & ~mask) \| mask·new` | 54 |
| 0x08 / 0x10 / 0x20 | key `0` copies X / Y / Z of `_ObjetReference` | 8 / 10 / 8 |
| 0x80 | key `0` copies the angle | 2 |
| 0x100 / 0x200 / 0x400 | key `0` copies X / Y / Z × 256 (particle space) | 6 each |
| none | plain integer slider | 110 |

Symbol names encode the path: `_editorObjet<child digits, leaf first>`.
Three nodes are defined but unreachable even in July ("Condition",
"Action", "Link Action"). Three BSS nodes (`_editorObjet511`, `2511`,
`1511`) were declared and never filled. **[verified in data]**

## A5. How it looks and how it is driven

### A5.1 Layout (640×480 game frame) **[verified in code]**

```
(20,20) Dreams Editor                        16-px font (coure.016) on a darkened band
(20,40)   Project                            root children, 10-px rows (dosapp.008)
(20,50)   Scene Particle
(20,60)   Option
(20,70)   Debug
(20,80)   Exit To DOS
          an open node shows its five children 10 px lower and 10 px right; the open
          label is redrawn 1 px right (bold); one open path per level: a cascading column
            Player Pos X          [=======o========]          -85
            label at x            slider at x+180 (128 px)    value at x+380
```

Picker page (replaces the menu):

```
(20,20)  LOAD MESH               (150,20) highlighted choice
                                 (150,30) current value of the field
(20,40)  UP
(80,50 .. 120) 8 rows; x=160 a second column (project: scene file; objet: mesh)
(20,130) DOWN
(20,150) EXIT
```

Delete pages add a "DELETE <kind> DELETE …" banner at (150,20). Below 640
pixels wide the fonts drop two sizes; the layout assumes 640. The July
toggle refused any other width.

Slider: eight 16-px segments of sprite set 3 (`alphabe2.spr`) and a knob.
Hold the left button within the row (y to y+10, x ≥ label x−10) and drag:
`v = d·(max−min)/128` (centred when min < 0); the cursor is clamped to the
track. No text entry, no step, no keyboard nudge. The minimum shapes the
scale but is not a floor in July; October adds a positive minimum to the
value. **[verified in code]** (reports A §2, C §5.7)

3D overlays: `ShowBox_` draws the working BOX's min/max box and its path as
a polyline; `ShowLink_` draws the live level's eight LINK volumes; both add
the scene object's position (actor slot 2, OBJET0). **[verified in code]**

### A5.2 The menu

```
Dreams Editor
  Project
    Project Creat Shift A / Project Load Shift Q / Project Save Shift W / Project Delete
    Project Edit...
      Misc...
        Misc Player &Scene...
          Misc Player...      Vehicule Mesh; Init Pos (X, Y, Z, angle, speed); Init Mode (move
                              mode, handling, inertia walk/swim/fly); Init Sphere (collision,
                              move, shoot); Flags Player (GUN OK, SURF OK, FALL NO OK)
          Misc Scene...       Misc Scene 2 (oxygen drain, fade colour, footsteps, sky speed,
                              kill height); IA strategy; magic regeneration; CD track;
                              Camera (target min/max, back min/max, speed, low/high follow,
                              combat angle/back)
          Fluid...            underwater tint R/G/B and wave, water level
          Hnm Intro Shift 1
          Phys Gravite...     gravity vector X/Y/Z
        Material Light...     Medium RGB, Wave RGB, Base RGB, "3dtr" (palette increments,
                              player light, decor clip, integration), timed colour + time
        Material Hnm...       material name, HNM file
        Material Scroll / Material Plasma
      Link Adventure...       Creat T / Load G / Edit (source objet, destination objet and box,
                              condition flags in 4 groups, action flags in 5 groups) / Save B / Delete
      Link...                 Creat E / Load D / Edit (target scene Shift 3, condition flags,
                              held objet, volume min/max, the same volume ×10) / Save C / Delete
      Objet...                Creat Z / Load S / Edit (mesh Shift 2, symbol Shift 6, 15 flags in
                              3 groups, two "Comportement" pages of stats, position and angle,
                              movement and scale, spheres) / Save X / Delete
      Box...                  Creat R / Load F / Edit (path in Shift 4, path out Shift 5, mode,
                              min/max, intensity) / Save V / Delete
  Scene Particle              emission volume and speeds, generation, turbulence, life,
                              quantity, mana gain; force type; four attractors; contact plane;
                              sprite, immortal particle 0, reset
  Option                      session camera constants; five animated-material switches
  Debug                       button: _MaxiLoad (load a level's objects or not)
  Exit To DOS                 button: _exitDos
```

The full tree with bindings, flags, ranges and masks:
`out/research/wip_pcj/DREAMS.EXE.editor-tree.md` and
`out/research/editor/scratch/A/tree_decoded.tsv`. Ranges differ between the
builds only for LINKADVENT "Time" and "Time Cut" (DOS 0..127; Windows 0..500
and 0..64). **[verified in data]**

### A5.3 Placing by walking

Key `0` copies `_ObjetReference` into every visible leaf flagged X, Y, Z or
angle. `GetObjetReference_` (DOS `0x16004`) computes it as the player's
position (actor slot 1) minus the scene object's (actor slot 2, OBJET0),
truncated to integers, and the player's angle `+0x5c`. All captured
coordinates are therefore **scene-object relative**, as the engine reads
them (LINK and BOX tests subtract `0x4fbd48`). Shift+4 appends the same
position as the next BOX path point (at most 16; `+0xe4` holds the last
index); Shift+5 drops the last. Many bank values lie outside the slider
ranges, which only capture can produce. **[verified in code and data]**

### A5.4 Keys

`sceneKeyboard_` reads the DOS character (`_clavierChar`, the low byte of
the BIOS key), so letters must be uppercase and the labels say Shift.
**[verified in code]** The grid sits on an AZERTY keyboard **[unverified]**:

| | Project | Objet | Link | Box | LinkAdventure |
|---|---|---|---|---|---|
| create | A | Z | E | R | T |
| load | Q | S | D | F | G |
| save | W | X | C | V | B |

| Code (DOS char) | Retail VK reading | Effect |
|---|---|---|
| `1`..`6` | `1`..`6` | intro HNM, mesh, link target, path point in, path point out, symbol |
| `?` 0x3f / `.` 0x2e | — / Delete | copy / paste project (paste keeps the destination's name) |
| `/` 0x2f / `§` 0xf5 | VK_HELP / OEM | copy / paste OBJET (paste creates a new record) |
| `:` 0x3a / `!` 0x21 | — / Page Up | copy / paste LINK |
| `%` 0x25 / `µ` 0xe6 | Left / OEM | copy / paste BOX |
| Space / Esc | same | confirm / cancel a picker page |

No delete key; no LINKADVENT clipboard. Retail re-based the table by 0x21
for its 16-bit key code but kept the 29 cases. **[verified in code]**
(reports A §3, C §5.4)

The game's own hotkey handler runs in the same frame. July developer keys
(`DreamsKey_`, demo Windows `0x4139a6`) **[verified in code]** (report B
§2.3):

| DOS key | Windows demo | Effect | Retail |
|---|---|---|---|
| `!` | Page Up | save `data\game.dat`, then toggle `_editor` if the width is 640 | removed |
| F10 | F10 | `SaveDiskScene_` | key help |
| F6 / F7 | F6 / F7 | cycle resolution and letterbox; `_editor = 0` | `VID_SetResolution` keeps the `_editor = 0` |
| F1 | F1 | object HUD | resolution |
| `6` / `7` | `6` / `7` | capture every frame (and Δt 2.0) / one frame | removed |
| `8` | `8` | Frame Rate / Mem 3DTR | removed |
| `r` | F3 | record a demo / save it | removed |
| `A`, `I`, `H`, `D`, `9`, `-`, `M`, `*` | same codes | HUD, inventory, level movie, message 0x40, camera, menus | removed |
| Ctrl+\ | VK 0x1c | toggle `_editorMouse` (never read) | removed |
| `e f l v` | same | render classes (`Comp3dEngineClavier_`) | removed |

Same-frame clashes: `A` (HUD), `D` (message 0x40), `6` (capture), and `!`,
which is both the toggle and "paste link". **[verified in code; effect
unverified]**

### A5.5 Mouse

Events in the standing handler's queue: `0x34` cursor `x << 16 | y` and
deltas `dx << 16 | dy`, `0x35`/`0x36` left down/up, `0x37`/`0x38` right
down/up. The editor uses only the left button; menu rows react to a click
edge plus held bit, picker rows to the held button and Y only. In editor
mode `CAM_MoveMouse_` stops, so the mouse no longer steers the camera.
**[verified in code]**

### A5.6 Editor mode in play

The same 17 functions read the flag in every build **[verified in code]**
(report B §2.4):

| July reader | Retail | Effect while set |
|---|---|---|
| `MCM1_Dispatcher_` | `GAME_TickFrame` | hide the frame-rate readout |
| `CompAnimObjet_` | `ANIM_TickClip` | root motion ×4 |
| `calc_new_walk_parm_` | `CAM_ComputeChasePos` (2) | no camera easing reset |
| `CAM_MoveMouse_` | `CAM_TickFree` | mouse camera off |
| `COL_TestAllCollision_` | `PHYS_ResolveCollisions` | no collision |
| `COL_AdjustCameraPos_` | `CAM_CollideEye` | no camera collision |
| `ENG_PutVideoLocalVector_` (5) | `ENT_AdaptActorColor` | marker pixels (stubbed in Windows) |
| `EditorSetPosObjet3dS_` (2) | `ENT_ResetToSpawn` | no random path placement |
| `ENG_testLinkScene_` | `SCENE_CheckExits` | no exits |
| `ENG_CompMovePlayerWalk/Swim/Fly_` | `ENT_TickPlayerGround/Swimming/Flying` | skip the target-select block |
| `CompPut3DObjet_` (3) | `GAME_Tick` | actors 2–15 pinned to their records each frame; no `END.DSN` exit; no transformation tick |
| `CompParticles_` | `0x43cc32` | draw the four attractors |
| `Apply_Forces_On_Object_` | `PHYS_IntegrateMotion` | no momentum |
| `worksSeachSceneName_` | `DDAT_LoadRecord` | no "previous level" fallback |
| `WorksEdit_` | `0x44d46d` | the panel |

## A6. What it edits

The record, its fields and the event model are documented in
[file-formats.md](../../research/file-formats.md#the-record-codec-and-layout-verified),
with the editor's labels. Editor conventions that shape the data
**[verified in code and data]** (report D):

- **Names** are `<Kind><slot>`, given at create (`Project12`, `OBJET3`,
  `LINKADVENT5`); defaults are `"EMPTY"` strings and −1 indexes. All 1,325
  July records and all retail records follow this.
- **Liveness is a bit** (header `+0x14`, LINK `+0x18`, OBJET `+0x34`, BOX
  `+0xec`, LINKADVENT `+0x28`, bit 0). Delete clears it and at most the
  first letter of the name; a deleted BOX keeps its name (27 on disc 1).
- **Pickers write strings in place** without clearing the cell
  (`END.DSN\0.DSN` in the July Project 122).
- **LINK targets are project names**; `"EMPTY"` means "go back to the
  previous level" at run time (`_SceneComeBack`); 3 retail links still
  hold it.
- **LINKADVENT indexes are picker-list positions**, which equal slots only
  when the used slots have no gaps (24 of 189 July references differ).
- **The live record is saved as it is**: runtime values the engine writes
  into it ship (BOX `+0xf8` force-field handles in 92 boxes, "dead warrior"
  markers in 24 respawn boxes).

Bugs in the editor code, all latent in the shipped data **[verified in
code]**: the BOX free-slot search runs 16 slots where the record holds 12
(slots 12–15 would overwrite LINKADVENT records; no bank uses a box past
`BOX9`); the sub-record name searches run 150 iterations; a full 16-point
path makes the drawer read a 17th point; Delete Box shows the "DELETE
OBJET" banner over a stale list; the link-target page uses a stale project
list.

## A7. July → October

| Change | Consequence | Evidence |
|---|---|---|
| Menu node block (0x5840 bytes) deleted; "Exit To DOS" moved into `_editorObjetNull` | two-node tree; no labels left anywhere | **[verified in data]** |
| Ten name tables declared one byte long | pickers index `base + i`; filling them would overwrite `0x661e28` and the arena pointer `0x661e2c` | **[verified in code]**; the declaration **[unverified]** |
| `_tableSceneS` cut to one record; `_RLE_SAVES` grown to 0x25400; bank loaded compressed, record 0 unpacked; `DDAT_LoadRecord` unpacks on demand | project pages list nothing; anything walking 150 records from `0x659044` runs over other globals (`DDAT_InitEmptyRecords` would clear 1.3 MB past BSS) | **[verified in code]** |
| `SaveDiskScene_` emptied; `editor.dat` gone | no save | **[verified in code]** |
| `WOR_SceneRLECompress_` writes `offset[150]` | the shipped bank has it (137,855), July's has 0: the shipped bank came from October code that is dead in the shipped build | **[verified in code and data]** |
| `0x447b72`, `0x447e7c` added | mastering lists (A8) | **[verified in code]** |
| Key code byte → word, table re-based | punctuation cases unreachable or misread | **[verified in code]** |
| Mouse producer removed with `GetCursorPos`, `ScreenToClient` | no cursor | **[verified in code]** |
| Hotkeys: toggle, F10, capture, recorder and the rest removed | no way in | **[verified in code]** |
| HNM picker: scroll by 8, also `*.Hnm`; slider adds a positive minimum; fillers no longer called | small edits | **[verified in code]** |
| Header `+0x140`, `+0x144`, `+0x1c0..+0x1cc` repurposed (player speed, camera collision switch, Glide fog) | the July labels are wrong for retail there | **[verified in code]** |

Values in the shipped bank exceed the July slider ranges (oxygen 10000, fog
density 75, dialogue 177, BOX intensity −600), so the October developers'
tree differed from July's. **[verified in data; conclusion unverified]**

## A8. The mastering step

`0x447b72` walks the 150 projects; for each in-use one it writes the files
its OBJETs (with a `.DAN` twin for each `.3DC`), its player model and its
animated material need into `listL<g>.txt`, `g` = record `+0x1fc`, then
appends `XH_.dan` and `MHE.dan` to `listL0.txt`. Replaying it on disc 1's
`DREAMS.DAT` reproduces the shipped `LISTL0..4.TXT` line for line (6, 241,
166, 222, 164 lines) **[verified]** (`out/research/editor/scratch/listl_check.py`).
`0x447e7c` writes `copyL<g>.bat`: `COPY` lines for the same files plus the
intro and event movies into `D:\CD1\DATA` (groups 1–2) or `D:\CD2\DATA`
(3–4), group 0 to both, and fixed lines for shared files. On the discs
every group 1/2 file is on disc 1 and every group 3/4 file on disc 2.
**[verified]** Neither function has a caller. The retail DOS executables
carry the same strings.

## A9. Where it was wired

No build calls `WorksEdit_` or holds its address (LE fixups applied).
**[verified in code]** The frame handler is the same function everywhere
(report B §1.2):

| Step | Demo DOS | Demo Windows | Retail |
|---|---|---|---|
| decode events | `MCM1_Dispatcher_` entry | `0x414781` | `GAME_TickFrame` `0x416d45` |
| flip | `0x11249` | `0x41395c` | `VID_Swap` `0x417021` |
| input; demo record/replay | `CTRL_*` | | `INPUT_UpdateActions`, `DEMO_*` |
| game tick and render | `CompPut3DObjet_` `0x1127b` | `0x41fcc2` | `GAME_Tick` `0x417078` |
| HUD | `MENJ_AfficheMenu_` | `0x42befd` | `UI_DrawHud` (+ `UI_DrawKeyHelp`) |
| object HUD | `PutDebugInfo_` `0x1128a` | `0x41414d` | `DBG_DrawObjectInfo` `0x41708c` |
| **candidate 2** | `0x1128f` | `0x414a72` | `0x417091` |
| timing, Δt pin | | | `0x417268` |
| readout unless `_editor` | `0x113c1` | `0x414bb3` | `0x4172a2` |
| **candidate 1** | `0x114c1` | `0x414c9d` | `0x41743a` (the recomp's call) |
| hotkeys | `DreamsKey_` | `0x4139a6` | `GAME_HandleHotkeys` |

Constraints: after the render (the capture reads this frame's buffer, the
wireframes draw into it); before the hotkeys (picker pages zero the key
code after Esc or Space, which only matters to a later reader, and
`DreamsKey_` handles both). `0x4aa704` is the BEN11 library's `_PreRender`
hook, run before the span flush and per shadow pass: wrong for a 2D panel.
**[verified in code; conclusions unverified]** Candidates 1 and 2 cannot be
told apart statically. A developer capture taken with the readout on would
decide; the seven December 1996 captures on disc 2 are clean 800×600 press
shots and do not.

## A10. Switched off

| Feature | July | Retail | Flag (retail) |
|---|---|---|---|
| Editor panel | uncalled | uncalled | `_editor` + a call |
| Editor mode | `!` / Page Up | no setter | `0x4a477c` |
| Mouse | live | removed | — |
| Frame Rate / Mem 3DTR | `8` | never written | `0x49d5c0` |
| Object HUD | F1 | never written | `0x49d5d0` |
| HUD on/off | `A` | stuck at 1 | `0x49d5d4` |
| Capture, Δt 2.0 | `6` / `7` (capture needs the panel) | never written | `0x4a4758`, `0x4a475c` |
| Demo recorder | `r` / F3 records; title menu and idle play `REPLAY.BIN` (1,848 frames) | start and load dead; shipped `REPLAY.BIN` is 3 frames of 104 bytes, the writer uses 112 | `0x49d34a` |
| "Collision view" | never written (BEN11 `_build_list`) | never written | `0x4ac8c8` |
| Renderer profiler | never written | DOS Glide only, never written | `_rendertype` |
| `_MaxiLoad` ("Debug" leaf) | menu | reused by the Options page | `0x49d9f4` |
| `printmemory_` | uncalled | uncalled / empty | — |

**[verified in code]** (report B §3)

## A11. How the team used it (inferred)

Everything in this section is **[unverified]**: practice reconstructed from
verified facts. Each workflow lists what it rests on.

### A11.1 People and tools

| Role | Who (manual p.29) | Tools in evidence |
|---|---|---|
| Game code, editor | Emmanuel Chriqui, Olivier Denis (programming, also authors) | Watcom 11.0 under DOS (`C:\DREAMS\src`), Multi-Edit 6.1 (`STATUS.ME`), 4DOS (`DESCRIPT.ION`) |
| Interface code | Frédéric Mouveaux | `PutSpriteFred_`, `PutFontFred_`, `_FredTimerTickIsOn` |
| Engine, codecs | R&D: Benoît Hozjan, Olivier Nemoz, Hubert Nguyen, Pascal Urro | BEN11 3D library (`C:\SOURCES\BEN11`, Glide on `E:\ENGINE\BEN11`), HNM codecs ("Pascal URRO R&D" in HNM6 headers) |
| Art, authoring | Hatem Benabdallah, Yann Mallard (authors, art direction), a graphic designer, four lead and four other graphic artists, two animators | 3D Studio R4 under DOS (`SALLE.PRJ`: `C:\3DS4`, `D:\PROD_3D\REFERENC`, `F:\HATEM`) |
| Gameplay advice | Laurent Jorda | — |

**[sourced: manual scans; tools verified in data]** Cryo's R&D supplied
"engines, libraries and different tools for all the production teams"
[sourced: [Adventure Classic Gaming, 2008](http://www.adventureclassicgaming.com/index.php/site/interviews/313/)];
the editor is not one of them: it is game code in the game's tree.

### A11.2 Who used it

The authoring credits put the two programmers among the four authors, and
the editor's interface is a programmer's: a variable panel bound to
structure fields, French-English labels with field names (`K_OBJ_BACK_MIN`,
"Shoot Impact /128", "MUL 10"), unit hints in the labels, no text entry.
**Reading:** the people who placed objects and scripted events were the
authors themselves, programmers and lead artists working in the game, not a
separate level-design team with a separate tool.

### A11.3 Where they ran it

The key codes are DOS characters, the mouse code in DOS is a full library,
the July Windows build kept the DOS codes as virtual keys (which turned `!`
into Page Up) and the toggle demanded width 640, the DOS default.
**Reading:** editing happened mainly in the DOS build on French AZERTY
keyboards at 640×400/480 with a mouse driver (`INSTALL.BAT`: "mouse.com
required"); the Windows build could edit but was a port.

### A11.4 Workflow 1: a new level

Rests on: create defaults, scene in OBJET0, quick reload, capture key.

1. Artists export the room from 3D Studio; the scene arrives as a `.DSN`
   (with `.3DM` textures) in `data\3dc` (the converter is not on the discs).
2. In the game, `!` opens the editor; the game first quick-saves to
   `data\game.dat`.
3. `Shift+A` creates `Project<n>` in the first free slot; `Shift+Z`
   creates `OBJET0`, LOAD MESH lists `data\3dc`, the `.DSN` is picked and
   the level reloads around the player.
4. Walk (no collision, ×4 speed) to the start, open Project Edit > Misc >
   Misc Player > Init Pos, press `0`; set movement mode and handling.
5. Shape the look live: Material Light sliders (base, medium, wave),
   palette increments, fluid tint and water level, sky speed, far clip
   ("Decors Clip"); the panel copies light colours to the renderer every
   frame.
6. `Shift+W` saves the project into the bank; F10 writes `editor.dat` and
   `dreams.dat`.

### A11.5 Workflow 2: actors

Rests on: OBJET flags and stats leaves, mesh-pick reload chain, clipboard.

1. `Shift+Z`, pick the `.DAN` (character) or `.3DC` (prop); the level
   reloads with it.
2. Walk to its place, `0` on Init Pos, set the angle.
3. Flags: friend or enemy, runs, follows, platform, shadow, fire, shock,
   mana creation; stats: Life, Mana, Speed, Strength, Mass, Courage,
   Defence, Attack, Path (first patrol BOX), Speed Move, Scale.
4. `Shift+X` saves; in editor mode the actor is put back on its record
   position every frame, so the placement is checked at once.
5. A tuned enemy is cloned with `/` then `§` (a new OBJET with the same
   data), then moved.

Evidence of practice: 164 retail non-scene OBJETs never had their stat
block edited; between 27 June and 9 July the Attack value of about 28 NPCs
dropped from 94–114 to 20–50: a balancing pass made through these sliders.
**[verified in data]**

### A11.6 Workflow 3: exits and the level graph

Rests on: LINK leaves, `GetSeachSceneLinkSceneName_`, ×10 page.

1. `Shift+E` creates `LINK<n>`; `Shift+3` picks the target project by name
   (the list shows each project's scene file in a second column).
2. Stand at one corner, `0` on Pos Min; at the other, `0` on Pos Max (the
   editor sorts them on save); the "×10" page drags large volumes.
3. Conditions: enemies dead, holding an object (Link with Objet), the
   level's event done, Ctrl pressed, outside the box.
4. `"EMPTY"` as target makes a "return where you came from" exit.

Evidence: 45 retail links have an all-zero volume and fire on conditions
alone; P41 LINK1 was retargeted to a new project between June and July.
**[verified in data]**

### A11.7 Workflow 4: volumes and paths

Rests on: BOX mode, Shift+4/5, `ShowBox_`.

`Shift+R` creates a BOX; the designer sets its mode (patrol, force-field
path, mana pickups, props, spawn and re-entry points, air points, hazard
zones), then walks the route and presses `Shift+4` at each point while
`ShowBox_` draws the polyline. 262 of 308 July boxes carry recorded paths.
**[verified in data]** Patrol boxes are referenced by an OBJET's "Path".

### A11.8 Workflow 5: event scripting

Rests on: LINKADVENT leaves and the retail rule model.

`Shift+T` creates a rule; the designer picks the source and destination
objects (and box), ticks condition bits and action bits, sets the timer,
the movie and the dialogue number. The shipped data shows recurring
recipes: talk to an NPC (destination near → dialogue), timed narration,
pick up an item and its reward appears in its place, an item unlocks a
door, a delay opens the exit. Rules grew from 111 (July) to 328
(October), dialogue-linked rules from 19 to 166: most scripting and the
voice hookup came after the July demo. **[verified in data]**

### A11.9 Workflow 6: the play-test loop

Rests on: the toggle's quick-save, editor-mode gates, New Game reload.

Edit with collision off, press `!` to play the same level for real with
collision, exits and AI, press `!` again to fix what broke. The quick-save
to `data\game.dat` on each toggle gave a way back. `Debug` (`_MaxiLoad`)
reloaded a level without its objects, for a fast look at the scene alone.
New Game reloaded `editor.dat`, so F10 came first. The engine wrote
runtime state into the live record during these sessions and the next save
kept it, which is why force-field handles and dead spawn markers ship.
**[verified in data for the shipped state; the loop unverified]**

### A11.10 Workflow 7: screenshots, video and the attract demo

Rests on: `SaveImage_`, the capture keys, the recorder.

`7` saved one frame to `data\tga\<scene>_<nnnn>.tga`; `6` saved every frame
with the step pinned at 2.0 (15 captures per game second), enough to build
a video from the frames. The disc's December 1996 `DATA\TGA\TEMP\E09_0000.JPG`
and siblings carry exactly that naming, resized to 800×600 for the press.
`r` recorded a play session into `data\replay.bin`; the July build day's
1,848-frame recording is the attract demo its title screen plays when idle.
**[verified in data for the files]**

### A11.11 Workflow 8: sharing one bank

Rests on: a single `editor.dat`, `C.BAT`, the `Z:\` fallback, P122.

There is one bank file for the whole game, with no merge tool. Work was
most likely serialized (one owner of `editor.dat` at a time, or levels
handed over as copies), with personal sync scripts like the demo's
`DATA\3DC\C.BAT` and a network share (`Z:\`) for some assets. The July bank
shows a scratch use: Project 122 is a byte copy of Project 0 with the scene
swapped to `END.DSN`, a test of one setup in the ending scene.
**[verified in data for P122 and C.BAT; the practice unverified]**

### A11.12 Workflow 9: mastering and release

Rests on: `+0x1fc`, `0x447b72`/`0x447e7c`, disc dates.

In October each project got a chapter/disc group (`+0x1fc`); the editor
build wrote `listL0..4.txt` (the hard-disk install lists the game reads per
chapter) and `copyL0..4.bat` (staging copies to `D:\CD1` and `D:\CD2`).
The trees were checksummed by ThunderByte Anti-Virus on 9 October; the
final disc 1 `DREAMS.DAT` is dated 29 October, the day `WINDREAM.EXE` was
linked. Then the release build cut the way in rather than the code: the
menu data, the toggle, the mouse, the save body and the table sizes went;
the functions stayed because Watcom links whole objects.
**[verified for the files and code; the sequence unverified]**

### A11.13 Timeline

| Date | Evidence |
|---|---|
| 1996-03 | 3D Studio test room, Multi-Edit session: the earliest files |
| 1996-12-09/10 | in-game captures of six levels (`DATA\TGA\TEMP`) |
| 1997-05-26 | build banner "Works In Progress, CRYO 26 May 1997" |
| 1997-06-27 | July demo `DREAMS.DAT` export (134 projects) |
| 1997-07-09 | July demo `EDITOR.DAT` (138 projects; 51 changed since 27 June) |
| 1997-07-10 | July executables linked; attract demo recorded |
| 1997-10-08 | `LISTL*.TXT` generated; disc 2 `DREAMS.DAT` |
| 1997-10-09 | ThunderByte scan of the disc trees |
| 1997-10-29 | disc 1 `DREAMS.DAT` (150 projects, 328 rules); `WINDREAM.EXE` linked |

**[verified in data]** (report E §5, report D §4)

---

# Part B — Bringing it back

## B1. Decisions

| Question | Decision | Why |
|---|---|---|
| Who it is for | **Players too, as a modding tool** (owner, 2026-10-03). Always built in and always available: no build switch, no setting to enable it | Owner decision |
| How it is distributed | The release package is the binary plus a `resources\` folder; the player supplies only the two disc images (owner, 2026-10-03) | Owner decision |
| Where the menu comes from | Extracted at build time from the July demo `DREAMS.EXE` (SHA-256 checked) into `out/recomp/windream/editor/`, bundled as a resource in the release package, never committed. `release.py` fails if the resource is missing or its hash differs; a development build without the demo keeps the retail two-node tree | Owner decision; no game data in the repository |
| Milestone | **One milestone, every phase (1–6)** (owner, 2026-10-03) | Owner decision |
| How nodes reach the guest | Host-built 0x40-byte nodes in `shim_alloc` memory; the retail root `0x4a47c4` gets its child pointers (four empty slots plus the exit node), a data write | The retail walker and leaf editor take any tree; no instruction changes |
| Bindings | A committed table mapping each July value symbol (+offset) to a retail address, with the retail reader that proves it; leaves without a proven binding are left out | AGENTS.md: no field bound on a label alone |
| Picker lists | Host replacements of the page and list functions (the stride is compiled in), registered through the replacement table | Bigger buffers cannot fix `base + i` |
| Project bank | A host bank of 150 unpacked records in guest memory; `_LoadSaveSceneSPtr` points into it; the bank is recompressed into `_RLE_SAVES` so the retail `DDAT_LoadRecord` serves edited records | Keeps level transitions retail |
| Where files go | The user-data directory of spec 007 in release builds, `out/recomp/windream/run*/editor/` in development (`DREAMS.DAT`, `EDITOR.DAT`, `listL*.txt`, `data\tga\`); never the disc or install | Spec 007 user data rules |
| Call site | Keep candidate 1 (`0x41743a`) | Fits every constraint; candidate 2 is equal and unproven |
| Copy and paste keys | **Ctrl+1..4 copy, Ctrl+Shift+1..4 paste** for project, objet, link, box (owner, 2026-10-03); letters and digits keep their July meaning | Owner decision; the July punctuation keys are unreachable from Windows keys |
| Keyboard | Host translation of those keys to the DOS codes while the editor is on | The punctuation cases are compiled into `sceneKeyboard_` |
| Toggle | **Page Up** (owner, 2026-10-03), chosen over the DOS `!`; keypad 5 stays as an alias. The host handles the key itself and does not pass it to the game, as it already does for keypad 5 | Owner decision. Page Up is what the only Windows build with a toggle used: the July Windows hotkey handler (`0x4139a6`) tests code 0x21, which is VK_PRIOR. `!` was the developers' key in the DOS build (character 0x21, one unshifted key on AZERTY), but the Windows game reads key codes, not characters, and on QWERTY `!` is Shift+1, whose code 0x31 opens the intro HNM picker in `sceneKeyboard_`. Page Up is one physical key on every layout. Swallowing it matters: `sceneKeyboard_` reads 0x21 as "paste link", so in July one press toggled the editor and pasted a link; paste link moves to Ctrl+Shift+3. The only other retail reader of Page Up is `CAM_TickFree` (held state at `0x40b456`), which returns early while the editor flag is set and whose mode nothing in retail switches on |
| Toggle at any width | **Page Up opens the editor at every resolution**, even when the layout is cut off (owner, 2026-10-03). The July toggle acted only at a width of 640 (`_ScreenXRes`); the port drops that condition | Owner decision. The layout is drawn for 640 (rows 10 px apart, sliders at label x+180, values at x+380), so narrower frames clip the deeper levels |
| Save on toggle | **Each Page Up press runs the retail autosave** (owner, 2026-10-03, option A), the routine `GAME_StartLevel` calls on level entry (`0x439341` with the current project, from `0x42f1d7`), before the flag changes | Owner decision. July saved `data\game.dat` with `CTRL_SaveGame_` (`0x106d6` in `DreamsKey_`), the same function as a menu slot, but nothing in the demo loads that path: the only `CTRL_LoadGame_` call (`0x49e1f`) uses `data\game\game%d.dat`. Retail has no `data\game.dat` string; its saves are `GAME_SaveGame` (`0x40f542`, `game<n>.dat`) and `GAME_SaveIndex` (`0x40f202`). The autosave gives a restore point the Load menu can open, which a July-style file would not. How the autosave picks its slot is not traced |
| How players use edits | **Proposed:** a mod bank in user data (`mods\<name>\DREAMS.DAT`), chosen in the launcher; the game reads it instead of the disc's bank; disc files untouched | Open |
| One bank or two | **Proposed:** one bank based on disc 1 (the later snapshot), used for both discs' levels | Open: retail reads each disc's own bank (six records differ) |
| Runtime fields in exports | **Proposed:** keep them, as the retail bank did (BOX `+0xf8`, spent spawn markers) | Open |

### Files the editor needs

Checked 2026-10-03 against the July demo, both retail discs and the
install **[verified]**:

| File | Used by | July demo | Retail discs | Source for the port |
|---|---|---|---|---|
| July `DREAMS.EXE` (DOS software) | the menu tree (build-time extraction) | yes | no | **the only demo file needed**; read at build time, the bundled resource is derived from it, the executable itself is not shipped |
| `DATA\OBJET\SOUR.ALP` | cursor (sprite set 10) | yes | yes, identical (SHA-256) | the player's disc |
| `DATA\OBJET\OBJET0.SPR`, `PARTICLE.SPR`, `ALPHABE2.SPR` | sliders, `InitWorksSprite_` | yes | yes, identical | the player's disc |
| `DATA\FONT\COURE.016`, `DOSAPP.008`, `SMALLE.006/.008` | the July text printer | yes | **no** | not needed: retail loads `HI320/480/640.SPR` instead and its printer already draws the two retail editor rows (owner-observed, spec 005). Row legibility at 10-px spacing is checked in phase 1 |
| `DATA\SYM\*.SYM` | the symbol picker (OBJET `+0x1c`) | one (`SHAMAN.SYM`, a 1996 draft) | none | not needed: no bank uses a symbol file; the picker lists nothing |
| `EDITOR.DAT` | the July startup bank | yes | no | not needed: it is only the uncompressed form of the bank (150 × 0x2200 records); unpacking the disc's `DREAMS.DAT`, which is lossless zero-run RLE, gives the retail equivalent, which phase 4 builds and phase 5 writes back as the port's own `EDITOR.DAT`. The July file holds the July game (138 projects, 111 events) and names 39 files the retail discs lack (3 scenes, 10 models, about 25 movies) plus 16 under older extensions (`.3DC` now `.DAN`, `.UBB` now `.HNM`): loaded in the port it would replace the retail levels with broken July ones |

So the release bundles one derived resource (the menu), and everything the
editor draws or lists at run time comes from the player's discs.

## B2. Phases

The phases are steps inside one milestone (owner decision): the editor
ships when phases 1–6 all pass. Each phase's acceptance checks run through
the control channel (`run.py --ctl`, `recomp/windream/debug/wdctl.py`,
`game_nav.py`); tests skip without the build, the discs or, from phase 1,
the July demo.

### Phase 0 — baseline (done, spec 005)

Keypad 5 sets `0x4a477c`; `wd_editor_frame` (`lift.py` `CALLS` at
`0x41743a`) calls `0x44d46d` through `guest_call_regs`; `mouse_post`
posts `0x34`–`0x38` into `0x626f70` while the handler is `GAME_TickFrame`
or `CTRL_Dispatcher`.

Acceptance (to automate): with keypad 5 on in Project 0, the screen shows
"Dreams Editor" and "Exit To DOS"; a click on "Exit To DOS" sets
`0x4a4780`; `ShowLink_` draws LINK0's box.

### Phase 1 — the July menu

1. **Extract.** A build step reads the July `DREAMS.EXE` (path from
   `.dreams.local.env`, new `DREAMS_WIP_DIR`; SHA-256 checked), walks the
   tree from `_editorObjetMain`, and writes
   `out/recomp/windream/editor/tree.json` (labels, structure, flags, min,
   max, mask, value symbol + offset). `build.py` copies it beside the
   executable as `resources\editor-tree.json`; `release.py` puts it in the
   package and fails without it. At start the host looks for the resource
   beside the executable; without it the editor opens with the retail
   two-node tree.
2. **Bind.** A committed table `recomp/windream/editor/bindings.tsv`: July
   value symbol and offset → retail address → evidence (retail reader
   address). Working-record leaves translate by base (A3); `_Sema*` by
   `+0x3df34c`; engine globals one by one. Leaves whose retail meaning
   changed (header `+0x140`, `+0x144`, `+0x1c0..+0x1cc`) are relabelled from
   the retail reader (`ENT_LoadObject` `0x41d974`, camera collision
   `0x409d8c`, Glide fog) or dropped; the July range is widened where the
   shipped bank exceeds it.
3. **Install.** At game start (after `GAME_Init`), the host allocates the
   nodes in guest memory, writes labels, child pointers, value pointers and
   parameters, and points the retail root's children at the July branches
   with the existing exit node last. Host-only structure: a comment block
   names every piece.

Acceptance: the five root rows appear and stay legible at the July 10-px
row spacing with the retail `HI640.SPR` font (screenshot check; the
spacing is compiled into `WorksEditSelectObjet_`, so if rows overlap the
fix is a host replacement of that function, host-only structure); opening
Project > Project Edit > Misc
> Misc Player > Init Pos and pressing `0` writes the player's
scene-object-relative position into `0x65fb04 + 0xb4..0xbc` (checked
against the player actor and `0x4fbd48` through the channel); dragging
"Light Base R" changes `0x65fb04 + 0x30` and the screen; every installed
binding's address is inside the retail image or a working record.

### Phase 2 — keys

While `0x4a477c` is set, the host translates keys to the codes
`sceneKeyboard_` expects before posting event `0x33`: letters and digits
as they are; Ctrl+1..4 to the copy codes and Ctrl+Shift+1..4 to the paste
codes for project, objet, link and box (`0x3f`/`0x2e`, `0x2f`/`0xf5`,
`0x3a`/`0x21`, `0x25`/`0xe6`; owner decision); Space and Esc unchanged.
Page Up sets or clears `0x4a477c` while the game frame handler runs
(`0x626f74` = `GAME_TickFrame`) and is never passed to the game, so it
cannot also paste a link. It works at every resolution, and before
changing the flag it runs the retail autosave (`0x439341`, with the current
project `[0x661e04]`), as July saved the game before toggling (B1). To
trace before building: how that autosave chooses its slot, and that it is
safe to call from the frame handler outside `GAME_StartLevel`. Clashes with `GAME_HandleHotkeys` (Ctrl+S shadow
mode, `L`, `J`, `K`, `P`, Tab, F1–F6 which clear the flag) are listed in
the in-game key help and the README; no retail hotkey is suppressed.

Acceptance: `Shift`-less `A` creates `Project<n>` (in phase 4's bank);
`Z` then a mesh pick creates `OBJET<n>` and reloads the level with it;
copy/paste of an OBJET yields a new slot with equal data.

### Phase 3 — file pickers

Host replacements for `0x4486bd`, `0x4487ed`, `0x4488bf`, `0x448937`,
`0x4489ab` (fillers) and for the list parts of the picker pages
`0x44902b`, `0x4492d1`, `0x449577`, `0x44a6e4`, `0x44a990`. The replacements
keep the pages' screen layout and hit tests (A5.1), read names from the
mounted data roots (disc library and install, spec 007) with the July
patterns, keep 8.3 names and 13-byte semantics, and write the chosen name
into the same target field as the original. The replacement table is
render-owned today (`render_boundary.cpp`); this phase generalizes it or
adds an editor table, without changing how render entries work.

Acceptance: LOAD MESH lists the scene's `.3dc/.dan/.dsn` files; choosing
one writes it to `0x65f8c4 + 0x0c`; nothing outside the editor's own
buffers changes (memory diff of `0x661e18`–`0x661e40` before and after).

### Phase 4 — the project bank

A host bank of 150 × 0x2200 bytes (1.3 MB) in guest memory, unpacked at
run time, not by a build step: the bank must come from the player's disc
(or the active mod), which the build never sees, and unpacking 138 KB of
zero-run RLE takes milliseconds. The host does it the first time the editor
opens, from the `DREAMS.DAT` the game itself mounted (`_RLE_SAVES`
`0x633c14`, already in memory after `DDAT_Load`), so it edits exactly the
bank being played. Replacements for the functions that
walk `_tableSceneS` (`0x449822`, `0x44988f`, `0x44991e`, `0x449e7a`,
`0x449ee9`, `0x449f42`, `0x449f80`, `0x448c6d`) work on it and set
`_LoadSaveSceneSPtr` into it, so the project save inlined in
`WorksGetEditor_` writes there unchanged. When a level loads, the host
binds `_LoadSaveSceneSPtr` to that level's record (host-only; July needed a
`Q` first). On every project save the host recompresses the bank into
`_RLE_SAVES` with the retail compressor's format (offsets at `+0`, data
from `+0x400`, `offset[150]` = total), so the retail `DDAT_LoadRecord`
serves edited records on the next transition. Headroom: the retail bank
needs 138,879 of the 152,576 bytes (13.7 KB free); a save that would
overflow is refused with a message.

To trace before building: how `_SemaSpeedLoadScene` copies the bank record
back (through `_LoadSaveSceneSPtr` or by index); every other reader of
`0x659044`–`0x65b243`; that no retail path keeps a pointer into
`_RLE_SAVES` across a recompression.

Acceptance: load project 12 with `Q`; change its sky speed; save; walk
through an exit and back: the change persists; Project Delete clears
`+0x14` bit 0 in the host bank only.

### Phase 5 — saving and export

A host `SaveDiskScene_` (`0x44900a`, called by `GAME_Shutdown`) and an
editor key (F10 while `0x4a477c` is set, translated before the retail F10
key help) write, when the bank is dirty (`0x661da4`): `EDITOR.DAT` (raw)
and `DREAMS.DAT` (compressed) into the active mod's directory under user
data (release) or the run's `editor/` directory (development). Playing a
mod (the proposed launcher choice) mounts its `DREAMS.DAT` in place of the
disc's through the host file layer; the disc stays untouched.

Acceptance: with no edit, the written `DREAMS.DAT` equals the mounted
disc's byte for byte (the compressor and index format are exact); after
one edit, only that record's bytes and the later offsets differ; the
patched bank loads in a fresh run through `bank_patch.py`-style data roots.

### Phase 6 — mastering lists and capture (optional)

- Run the October generators' logic over the host bank (a host
  reimplementation or a repository tool next to `bank_patch.py`; the
  originals walk `0x659044`) and write `listL*.txt`, `copyL*.bat`. Acceptance:
  on the unedited disc 1 bank the lists equal the shipped `LISTL*.TXT`.
- TGA capture: create `data\tga` in the sandbox before the first write
  (the retail writer does not check `fopen`); keys `6`/`7` translated while
  the editor is on. Acceptance: one capture writes `<scn>_0000.tga` of the
  game frame.

## B3. Risks

| Risk | Mitigation |
|---|---|
| A July binding points at a field whose retail meaning changed | bindings table requires a retail reader per row; unproven rows are left out |
| Writing into `_RLE_SAVES` while a record is being read | recompress only between frames, from the editor's save path |
| The 16-slot BOX search writes into LINKADVENT slots | the phase 4 replacements cap at 12; the BOX search (`0x44baed`) is replaced or guarded |
| LINKADVENT indexes are list positions | phase 1 shows slot indexes; keep the July semantics until the engine's reading is traced, and document it |
| Editor mode pins actors and disables exits; a session saved in that state | saves come from the working records, not from actor state; runtime-written BOX `+0xf8` is cleared before export (or kept, matching retail; decide when phase 5 is built) |
| `!`/Page Up paste-and-toggle clash | the host consumes Page Up as the toggle and never passes it to the game; paste link is Ctrl+Shift+3 |
| Fonts and layout below 640 pixels wide | accepted (owner decision): the editor opens at any width and may be cut off |
| The toggle's autosave pushes out a player's save | trace the autosave's slot choice before phase 2 is built |
| A release without the menu resource, or with one from another build of the demo | `release.py` checks the resource's presence and the source hash; the host validates the file before installing nodes |
| A player's edited bank breaks a save or a level | mods live in their own directory; the disc bank stays the default; a broken mod is dropped by deselecting it in the launcher |

## B4. Verification plan

| Check | Phase | How |
|---|---|---|
| Panel and exit node | 0 | screenshot + `0x4a4780` after a scripted click |
| Root rows and a captured position | 1 | control channel: keypad 5, mouse script, read `0x65fb04+0xb4` |
| Binding table proof | 1 | test reads `bindings.tsv` and checks each retail reader address against the lifted code |
| Create and reload an OBJET | 2–3 | `game_nav` to Project 0, keys, wait on `_SemaImportNewScene`, read the OBJET slot |
| Persistence across a level exit | 4 | edit, save, `complete_level_triggers`, return, read the field |
| Byte-identical export | 5 | write `DREAMS.DAT` with no edits, compare with the disc |
| `LISTL` round trip | 6 | compare with the shipped lists |

## B5. Open questions

- Original call site: candidate 1 or 2 (A9).
- The trigger of `0x447b72`/`0x447e7c` in the developers' October build.
- How the engine reads LINKADVENT source and destination indexes.
- Whether the July Glide build already read `+0x1c0..+0x1cc` as fog.
- The October developers' menu (values beyond July ranges): only its
  labels' absence is known.
- Owner decisions still open (proposals in B1): how players pick and play
  a mod bank; one bank for both discs or one per disc; whether exports keep
  runtime-written fields.

## B6. Work items

1. Pin the retail addresses listed at the end of A3.
2. Bindings table with proofs (phase 1, step 2).
3. Tree extraction and install (phase 1).
4. Key translation (phase 2).
5. Generalized replacement table; picker replacements (phase 3).
6. Host bank, `_LoadSaveSceneSPtr` binding, recompression (phase 4).
7. Export and the byte-identity test (phase 5).
8. Fix `src/dreams/formats/project.py` (CD track from `+0x1f8`, strength
   read as radius, liveness by name, last BOX point dropped), which the
   tests of phases 4–6 will use.
9. Register the July names of the retail editor functions in
   `re/names/WINDREAM.EXE.tsv` (report C's mapping is the second source).
10. Release packaging: `resources\editor-tree.json` in `build.py` and
    `release.py` (fails without it), the mod directory under user data, and
    editor code that is always compiled in and builds without
    `WD_DEVTOOLS`.

## File map

| Path | Content |
|---|---|
| `docs/research/cryo-editor.md` | overview of Part A |
| `docs/research/file-formats.md` | the record and its editor labels |
| `docs/specs/005-debug-tools/spec.md` | phase 0 and the other debug features |
| `recomp/windream/host/sdl/user.c` | keypad toggles, `mouse_post`, `wd_editor_frame` |
| `recomp/windream/lift/lift.py` | `CALLS` (`0x41743a` → `wd_editor_frame`) |
| `recomp/windream/lift/replacements.py`, `host/render/render_boundary.cpp` | the replacement table phase 3 generalizes |
| `recomp/windream/host/core/runtime.c` | `shim_alloc`, `guest_call_regs`, `WD_POKE` |
| `out/research/editor/` | reports A–E, decompilations, `fn.py`, scratch scripts (local) |
| `out/research/wip_pcj/` | July trees, symbols, earlier extracts (local) |
