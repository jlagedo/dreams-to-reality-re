# 008 — The Cryo level editor: what survives, how it was used, and bringing it back

Status: **Draft. Part A (knowledge) is complete to the level of static
analysis of all six July and October builds plus the banks and discs. Part B
(restoration) is designed, not started; phase 0 is what spec 005 already
runs (keypad 5, host mouse, one inserted draw call). Owner decisions of
2026-10-03: the launcher offers three modes, **Play** (default; the discs as
shipped), **Develop** (Cryo's in-game developer tools, the editor included,
playing from a local copy of the discs where edits land as they did for the
developers) and **Play edits** (the developer folder's game with no tools,
with CD music); Develop reads keys as the DOS build did, by character, so
`!` opens the editor; Develop runs on the software renderer until a final
direct-renderer port; the July menu is bundled with the release; one
milestone covers every phase. A second round of static traces and a
developer-tool audit (2026-10-03) corrected Part B: see B1 and B2.**
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
  bank, saving and export. It ships to players as one of three launch modes
  chosen in the launcher: **Play** (default) is the shipped game from the
  disc images; **Develop** turns on Cryo's in-game developer tools and
  plays from a local copy of the discs, the developer folder, as the
  developers' game ran from its hard-disk tree; **Play edits** plays the
  developer folder's game with the tools off and CD music on, as the retail
  build played the editor's export. The release package is our binary plus
  bundled resources, and the player supplies only the disc images.

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
- Nothing is written to a disc image or to the retail install tree.
  Develop and Play edits write only inside the developer folder in the
  user-data directory of spec 007.
- Develop is a game option for Cryo's own tools, not our development
  harness. The harness (the control channel `WD_DEVTOOLS`, the `dreams-game`
  MCP tool, `run.py` options such as `--poke`, trace and audit builds) stays
  exactly as it is in every mode: a build-time option, absent from release
  builds as `release.py` checks today. Tests drive Develop through the
  harness, but the Develop code builds and runs without it.

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
(reports B §0, C §7–8) It does not hold for the BSS dword block: July
`0x299ab0–0x299b53` sits at retail `0x661d04–0x661da7` (same size, offset
`+0x3c8254`) but the dwords inside are reordered, so each one is matched
from code (table "Pinned 2026-10-03" below); the naive offset puts
`_LoadSaveSceneSPtr` on `…LinkAdventureSPtr`. **[verified in code]**

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

### Pinned 2026-10-03

Each from a July/retail function pair that uses it in the same role
**[verified in code]**:

| Original name | Demo DOS | Retail | Proving retail functions |
|---|---|---|---|
| `_ObjetReference` (+0xc angle) | 299ab0 | 661d04 (661d10) | `0x41c9dd` writes; `0x44c11b` reads |
| `_LoadSaveSceneBoxSPtr` | 299b3c | 661d88 | 44becb / 44bf3a / 44c029 / 44c06c |
| `_LoadSaveSceneLinkSPtr` | 299b40 | 661d8c | 44b74d / 44b7bc / 44b8ab / 44b8ee |
| `_LoadSaveSceneObjetSPtr` | 299b44 | 661d90 | 44a517 / 44a594 / 44a5ed / 44a62b |
| `_LoadSaveSceneSPtr` | 299b48 | **661d94** | 449e7a / 449ee9 / 449f42 / 449f80; `WorksGetEditor_` 0x44cb54, 0x44cb8d |
| `_LoadSaveSceneLinkAdventureSPtr` | 299b4c | 661d9c | 44b0e5 / 44b162 / 44b1bb / 44b1f9 |
| `_essai` | 299b34 | 661da0 | unreferenced; by elimination **[unverified]** |
| `GetObjetReference_` (ENGINE.C) | 16004 | **41c9dd** | slot 1 minus slot 2 position, `+0x5c` angle; callers `0x44c11b`, `0x44cd62` |

Name tables, Nb / Offset / StartOffset, proven by the list or filler that
counts Nb and the picker that pages it:

| Table | Retail | Proving retail functions |
|---|---|---|
| Scene | 661d5c / d78 / d4c | 44988f, 44991e |
| Objet | d70 / d1c / d44 | 44a14a, 44a1d3 |
| Link | d34 / d28 / d20 | 44b3df, 44b452 |
| LinkAdv | d30 / d14 / d2c | 44ad7d, 44aded |
| Box | d18 / d38 / d24 | 44bb5a, 44bbd0 |
| 3dc | d6c / d54 / d50 | 4486bd, 44a6e4 |
| Hnm | d98 / d40 / d68 | 4487ed, 449577 |
| Map | d7c / d48 / d84 | 4488bf, 4492d1 |
| Anim | d64 / d58 / d80 | 448937, 44902b |
| Sym | d60 / d74 / d3c | 4489ab, 44a990 |

Still to pin: the engine globals the July menu binds (A4.4). A lead: the
four dwords `0x4a0f68`–`0x4a0f74`, tested and never written (stuck at 1),
sit where July's `_MAP_Anim*Ok` would be by data order **[unverified]**.

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
  That is July. In retail `W` sets `_SemaSaveScene` (`0x4a46c0`) and the
  next frame calls `0x449f42` (not inlined); `0x661da4` is set and never
  read. The pointer `0x661d94` holds what the one-record bank allows: `Q`
  (`0x449ee9`) and Delete (`0x449f80`) bind it to `DDAT_LoadRecord`'s
  scratch output `_CurrentScene2S` (`0x65b344`), Create to `0x659044`; so a
  retail `W` after `Q` writes into scratch **[verified in code]**.
- **Quick reload** (`_SemaSpeedLoadScene` `0x4a46b4`, after a mesh pick):
  if `[0x661d94]` is non-zero, `DDAT_CopyRecord(ptr)` copies 0x2200 bytes to
  `0x65fb04`, then `_ImportNewScenePtr = 0x65fb04`, `_SemaImportNewScene =
  1` and `SCENE_LoadLevel` (`0x41f9db`) at once; never an index, never
  `DDAT_LoadRecord` **[verified in code]**.
- **Mesh pick** (after `Z`, or `2`): if not `"EMPTY"`, `_SemaSaveSceneObjet`
  and `_SemaSpeedLoadScene` are set: OBJET → project → bank → copied back →
  reload, and the object appears.
- **Project load** (`Q`): picker, then `_ImportNewScenePtr = &_CurrentSceneS`,
  `_SemaImportNewScene = 1` and an immediate `testImportNewScene_`: the
  player is moved into the chosen level.
- **Delete**: picker over a "DELETE …" banner; clears the in-use bit (and the
  first letter of the name for OBJET, LINK, LINKADVENT). For a project the
  retail code zeroes the whole `+0x14` dword, bits 16–18 included, which 90
  of the 150 shipped records carry (`0x10001`, `0x20001`, `0x40001`,
  `0x50001`; meaning unknown) **[verified in code and data]**.
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
**[verified in code]** The keyboard was AZERTY **[verified in data;
conclusion inferred]**: the July labels read "Hnm Intro Shift 1", "Load
Mesh Shift 2" … "Load Symb Shift 6", and digits need Shift only on AZERTY;
the clipboard pairs sit on neighbouring AZERTY keys (`:`/`!` unshifted copy
and paste a link, the same two keys shifted, `/`/`§`, an objet; `?`/`.` are
Shift+`,`/Shift+`;`, `%`/`µ` Shift+`ù`/Shift+`*`). The grid:

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
| `!` | Page Up | save `data\game.dat` (every press, before the width test), then toggle `_editor` if the width is 640 | removed |
| F10 | F10 | `SaveDiskScene_` | key help |
| F6 / F7 | F6 / F7 | cycle resolution and letterbox; `_editor = 0` | `VID_SetResolution` keeps the `_editor = 0` |
| F1 (held) | F1 | object HUD | resolution |
| `6` / `7` | `6` / `7` | capture every frame (and Δt 2.0) / one frame | removed; targets live |
| `8` | `8` | Frame Rate / Mem 3DTR | removed; target live |
| `r` | F3 | record a demo / save it | removed; start functions uncalled |
| `A` | `A` | HUD on/off (`0x49d5d4`) | removed; flag stuck at 1 |
| `-` | Insert | camera message `0x31`: free-fly camera (`CAM_TickFree` `0x40b429`) | removed; camera code live |
| `9` | `9` | camera message `0x30`: overhead camera (1,500 units up) | removed; camera code live |
| `D` | `D` | message `0x40` to the HUD queue: dialogue entry 0 | removed; handler live |
| `H` | `H` | play the level's movie (`+0x3c`) | removed; sequence live at level entry |
| `I`, `*`, `M`, `ù` (0x97) | `I`, VK_PRINT, `M`, — | prototype 3D inventory and menus (`MENG_`, `MENM_`, `MENI_`) | gone; retail's event `0x97` opens the object page instead |
| Ctrl+\ | VK 0x1c | toggle `_editorMouse` (never read) | removed |
| `e f l v` | Numpad 5, Numpad 6, VK_SEPARATOR, F7 | render classes (`Comp3dEngineClavier_`) | removed; target `0x4577cc` live |

The free camera's speed keys are held physical keys, not characters:
`CAM_MoveMouse_` tests `_tab_active_scankey` at scan codes 0x49, 0x51,
0x47, 0x4f (PgUp ×½, PgDn ×¼, Home ×2, End ×4) and returns at once while
`_editor` is set, because the editor owns the mouse **[verified in code]**.
In DOS nothing else used those keys. The July Windows port translated the
held-key table to virtual keys but left the character handlers comparing
DOS character values against virtual-key codes, so `!` (0x21) became
VK_PRIOR, the free camera's PgUp, and `-` became Insert, the look key; the
Windows key column above is that accident, not a design.

Same-frame clashes in DOS: `A` (HUD), `D` (message 0x40), `6` (capture),
and `!`, which is both the toggle and "paste link". **[verified in code;
effect unverified]**

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
| `_tableSceneS` cut to one record; `_RLE_SAVES` grown to 0x25400; bank loaded compressed, record 0 unpacked straight into `_CurrentSceneS` (the table stays zero); `DDAT_LoadRecord` unpacks on demand | project pages list nothing; anything walking 150 records from `0x659044` runs over other globals and leaves the image at record 49 (`DDAT_InitEmptyRecords` would clear 0x13ec00 bytes from `0x659044`, about 0.9 MB of it past the image end `0x6c1000`) | **[verified in code]** |
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
| Renderer profiler (`3DC_PROF.C`) | DOS software and Windows, behind `_rendertype` | Windows: whole module present, uncalled; DOS Glide: gate never written | `_rendertype` (DOS Glide `0x104f38`) |
| `_MaxiLoad` ("Debug" leaf) | menu | reused by the Options page | `0x49d9f4` |
| `printmemory_` | uncalled | uncalled / empty | — |
| Free-fly camera | `-` | camera code live, no sender of message `0x31` | — |
| Overhead camera | `9` | camera code live, no sender of `0x30` | — |
| Give all 14 items (`_flagImportAllObjects`) | no setter | only written by its clear | `0x49d5e0` |

**[verified in code]** (report B §3; developer-tool audit of 2026-10-03,
inventory in spec 005)

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

The key codes are DOS characters, the menu labels ask for Shift+digit (an
AZERTY habit, A5.4), the mouse code in DOS is a full library, the July
Windows build kept the DOS codes as virtual keys (which turned `!` into Page
Up, a clash with the free camera that DOS never had) and the toggle
demanded width 640, the DOS default.
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

### The data principle

The goal is to reactivate the editor using every piece that survives; any
data comes from the final retail build or is derived from it, and the
gaps are filled (owner, 2026-10-03). Four rules apply it (owner,
2026-10-03, as recommended):

1. **Retail code defines meaning.** A field means what the final retail
   code does with it: the Windows build the port runs, and the retail DOS
   Glide build for fog, which the port's renderer reproduces from the
   record (`render_live.cpp` runs `SCENE_SetFog` from the hook at
   `0x41f9ba`) **[verified in code]**.
2. **The final bank defines the values in use**: disc 1's `DREAMS.DAT`.
3. **July supplies the tool's shape, only where retail kept nothing**: the
   menu structure, labels, slider and position-capture flags. It never
   overrides rules 1 and 2.
4. **We fill the gaps**: whatever retail reads or the final bank uses that
   July lacks gets an entry in July's style, derived from its retail
   reader, and marked as ours in this spec.

| Question | Decision | Why |
|---|---|---|
| Launch modes | **Three, chosen in the launcher** (owner, 2026-10-03): **Play** (default), **Develop**, **Play edits**; stored as `[port] mode = retail \| dev \| edited` in `dreams.ini`, whatever the labels. The same release binary holds all three; no build switch. Internally two switches: Cryo's tools on or off, data from the images or from the developer folder; the fourth combination (tools on, images) is not offered, since edits would have nowhere to go | Owner decision. Supersedes "two modes" and the earlier "always available, no setting" row |
| Play | The shipped game from the disc images, with CD music and our visual improvements (renderer, smooth motion, camera smoothing); none of the host bindings added for Cryo's tools | Owner decision |
| Develop | Cryo's in-game developer tools, which the game already contains and the port only reconnects: the editor, the cameras, readouts, capture, the recorder and the rest of the inventory in spec 005 ("Developer-tool inventory"); the host bindings that reach them because retail removed their keys (DOS-character keys, keypad 1–9, the editor's mouse feed, the inserted editor call); data from the developer folder, no CD music | Owner decision: "a game option to enable the in-game developer tools", widened to "anything else we can find on the game that looks like a developer tool" |
| Play edits | The developer folder's game as a player sees it: tools off as in Play, CD music from the disc images. Offered once Develop has created the folder. Without the images it plays silently (2026-10-03, accepted as recommended) | Owner decision (music: yes). It is to Develop what the retail build was to Cryo's editor build: the export played without the tools; it checks edits in the shipping configuration (exits, autosave and load, music) |
| Saves | Develop and Play edits share the developer folder's `DATA\GAME`; Play keeps its own. The host refuses, with a message, to load a save whose project name no longer exists in the bank (the save stores the name; `GAME_LoadGame` would crash in `memcpy_` on the NULL record) | Owner decision (shared); the guard follows from it |
| Our harness | **Untouched and the same in every mode** (owner, 2026-10-03): the control channel, the MCP tool and the `run.py` options stay a build-time development option, absent from release builds | Owner decision: "not our work to build it" |
| Developer folder | **A local copy of both discs in the user-data folder** (owner, 2026-10-03). The first Develop launch copies the two images into it, except disc 1's `DATA\FULL.ID` and disc 2's leftover `DATA\GAME` saves (phase M). **The launcher does the copy, with a progress bar, and writes the "initialized" marker last; a launch that finds no marker resumes, skipping files already copied at their full size** (owner, 2026-10-03). Each later launch finds the marker, copies nothing and plays from that folder without reading the images. The folder is the game's data tree, like the developers' `C:\DREAMS\DREAMS`: edits and everything the game writes land in it as they are | Owner decision. The July builds have no disc logic at all (no `1CD.ID`, `2CD.ID`, `HD.ID`, `FULL.ID`, `LEVEL.ID` or `ListL` strings): the developers' game read one data tree, which `STATUS.ME` places at `C:\DREAMS\DREAMS\DATA` **[verified]** |
| Music in Develop | **None, as in the developers' build** (owner, 2026-10-03), by a host replacement of `CD_OpenAudio` (`0x4042f1`) that returns 0, as July's `ACD_Init_` did. Not by failing the MCI open: retail answers an MCI error with a modal "MCI Error" box (`CD_ShowMciError` `0x404278`) | Owner decision. The July DOS `ACD_Init_` (`0x42600`) is `xor eax, eax; ret`: the message manager stores 0 and never sets its audio-CD flag, so that build plays no CD music while the rest of `ACD.C` (Miles redbook) is intact **[verified in code]**. Every retail music path then stays silent (phase M) **[verified in code]**. Track numbers were assigned late: 24 of 138 projects in July, 127 of 150 in October **[verified in data]** |
| How it is distributed | The release package is the binary plus a `resources\` folder; the player supplies only the two disc images (owner, 2026-10-03) | Owner decision |
| Where the menu comes from | Extracted at build time from the July demo `DREAMS.EXE` (SHA-256 checked) into `out/recomp/windream/editor/`, bundled as a resource in the release package, never committed. `release.py` fails if the resource is missing or its hash differs; a development build without the demo keeps the retail two-node tree. The July tree is a **template** checked entry by entry against retail, with retail-only fields added (phase 1, option C) | Owner decision; no game data in the repository; data principle rule 3 |
| Milestone | **One milestone, every phase** (owner, 2026-10-03): M, 1–7 and D, phase 6 optional as it always was | Owner decision; phase 7 (developer tools) and phase D (direct renderer port) added 2026-10-03 |
| How nodes reach the guest | Host-built 0x40-byte nodes in `shim_alloc` memory; the retail root `0x4a47c4` gets its child pointers (four empty slots plus the exit node), a data write | The retail walker and leaf editor take any tree; no instruction changes |
| Bindings | A committed table mapping each July value symbol (+offset) to a retail address, with the retail reader that proves it; each entry kept, relabelled, widened, marked "no effect" or hidden by the rules of phase 1 (option C, owner, 2026-10-03) | AGENTS.md: no field bound on a label alone; data principle rules 1–3 |
| Picker lists | Host replacements of the page and list functions (the stride is compiled in), registered through the replacement table | Bigger buffers cannot fix `base + i` |
| Project bank | A host bank of 150 unpacked records in guest memory; `_LoadSaveSceneSPtr` (`0x661d94`) points into it and is bound at every level load to that level's record; the bank is recompressed into `_RLE_SAVES` so the retail `DDAT_LoadRecord` serves edited records | Keeps level transitions retail. Retail binds the pointer to a scratch buffer or to the one-record table (A4.3), so the binders are replaced (phase 4) |
| Full bank | **Project Create refuses with an on-screen message when no slot is free**; Delete frees one (2026-10-03, accepted as recommended) | All 150 shipped records are in use (July had 12 free); retail Create with no free slot still clears the live level **[verified in code and data]** |
| Where files go | Into the developer folder, in place: `DREAMS.DAT` and `EDITOR.DAT` at its root, `DATA\TGA\` captures, `DATA\REPLAY.BIN`, saves in `DATA\GAME\`, the mastering lists of phase 6; never a disc image or the retail install | Follows from the developer-folder decision |
| Call site | Keep candidate 1 (`0x41743a`) | Fits every constraint; candidate 2 is equal and unproven |
| Console | **A separate console window** that keypad 9 opens and closes in Develop (a console window on Windows, the terminal on Linux), showing the game's own `printf` diagnostics and the dump helpers' output (owner, 2026-10-03) | No new in-game UI; the output already reaches `stdout.txt` (spec 005) |
| Keyboard | **DOS keys** (owner, 2026-10-03): in Develop the host reads commands as typed characters, as the DOS build did, and held controls (arrows, Alt, Ctrl, Space, PgUp…) as physical keys. Rules: a key the host consumes as an editor or developer command is hidden from the game (key state and event); while the editor is on, editor commands win over the July developer keys; keypad text is ignored (the keypad stays host keys). Key tables and clashes: phase 2 | Owner decision, superseding Page Up and Ctrl+1..4. The developers worked in DOS (A11.3); the July Windows key map is a translation accident (A5.4) |
| Toggle | **`!`, read as a character** (one key on AZERTY, Shift+1 on QWERTY); keypad 5 stays as an alias. Consumed by the host, never passed to the game; in Play and Play edits it reaches the game unchanged | Owner decision. It frees PgUp for the free camera, as in DOS. `sceneKeyboard_` reads 0x21 as "paste link", so in July one press toggled the editor and pasted a link; paste link moves to Ctrl+Shift+3 |
| Toggle at any width | **`!` opens the editor at every resolution**, even when the layout is cut off (owner, 2026-10-03). The July toggle acted only at a width of 640 (`_ScreenXRes`); the port drops that condition | Owner decision. The layout is drawn for 640 (rows 10 px apart, sliders at label x+180, values at x+380), so narrower frames clip the deeper levels |
| Save on toggle | **None** (2026-10-03, with the DOS keys; supersedes option A, the retail autosave on each press). **Save-anywhere: Cryo's uncalled Save page** (`0x4313b5`: `MENU_InitSaveSlotSelect(1)`, `MENU_RunGameMenu`) on **keypad 0**, restored from the Load page (owner, 2026-10-03). To trace before building: the page's typed-title entry, unreachable since retail | July's `!` called `CTRL_SaveGame_("data\\game.dat")` on every press, entering and leaving; the string occurs once in the July binary and no build reads the file (the July Load page reads slot files; retail has no such string) **[verified in code]**. Why it saved is unknown **[unverified]**. The retail autosave (`0x439341`) is no substitute: it keys its slot by the level's display name, so it overwrites the level's own save instead of adding one, writes a protected slot's file, ignores record `+0x1f8` (P0, P5, P10, P52 never save) and must run from the frame hook behind five guards **[verified in code]** |
| How edits are played | In Develop and Play edits, by the folder itself: the editor's save overwrites the folder's `DREAMS.DAT` (and writes `EDITOR.DAT` beside it), and the next launch in either mode plays it, as July's `LoadDiskScene_` loaded `editor.dat` at every start and New Game and `SaveDiskScene_` wrote it back. Play always plays the discs. No separate mod system | Follows from the developer-folder decision; an earlier mods proposal was never approved and is withdrawn |
| Restoring the folder | **A launcher button "Reset edits"** that copies disc 1's `DREAMS.DAT` back over the folder's and keeps captures and saves (2026-10-03, accepted as recommended) | Closes the open question of how a player restores the folder |
| Disc number in the folder | **No host change; the one-frame prompt is accepted** (owner, 2026-10-03). Replacing `CD_GetDiscNumber` (`0x4287fa`) to answer from the level record's `+0x1fc` was considered and declined | It tests only `1CD.ID`, so a folder with both ID files always reports disc 1 and every disc-2 level passes through `CD_PromptSwap` ("Please change to CD no 2"), shown for one frame **[verified in code]** |
| Renderer in Develop | **The software renderer, until the direct port (phase D) passes** (owner, 2026-10-03; supersedes "the launcher's setting"). The launcher shows the renderer as fixed for Develop, with a note; Play and Play edits keep the setting. After phase D, Develop follows the setting | The software renderer runs Cryo's own drawing code, so every tool draws as written (panel, sliders, pickers, wireframes, cursor, centre dots, capture, render classes) and its 640×480 frame is the layout the editor was drawn for; the direct renderer stops on any unsupported draw and leaves the software frame buffer empty (phase 0 check). Software screenshots become the reference the port is checked against |
| Which `DREAMS.DAT` the folder gets | **Disc 1's** (owner, 2026-10-03), the final retail bank | Data principle rule 2. Disc 1's file is dated 1997-10-29, the day `WINDREAM.EXE` was linked; disc 2's 1997-10-08. In 5 of the 6 differing records (P31, P41, P55, P69, P75, P87) disc 2 keeps the value both July banks share and disc 1 changes it **[verified in data]**. It is also what a normal session plays: `DDAT_Load` reads the bank only at start (`GAME_Init`, `BOOT_Run`), never at a swap, so a game started with disc 1 uses disc 1's records for every level **[verified in code]**. Five of the six differing projects are disc-1 levels (groups 1–2); P75 (`E08_END`, group 4) differs only in magic regeneration (6 against 0). Dropped with disc 2's bank: P69 `LINKADVENT2` (dialogue 177, which `DIALOG.DRD`'s 178 entries hold), deleted on disc 1 |
| Merging the two discs | **The newer copy of each differing file wins** (owner, 2026-10-03); disc 1's `DATA\FULL.ID` and disc 2's `DATA\GAME` are not copied (phase M). Of the 10 files that differ (`disc-layout.md`), disc 1's is newer for `DREAMS.DAT` (10-29 / 10-08), `DATA\HNM\INTRO.HNM` (10-08, full 38 MB intro / 09-30, 1.7 MB), `DATA\ICONE\ICONES.BF` (09-10, 372,358 bytes / 08-03, 440,029) and `DATA\HD.ID` (09-18 / 09-01); disc 2's for `DATA\UNIVBE\UVCONFIG.EXE` (DOS only, never read by the port); the rest are junk (`DESCRIPT.ION`, `ANTI-VIR.DAT`, `SETUP.GID`) | Data principle. This **changes** `disc-layout.md`'s "keep disc 2 — larger" for `ICONES.BF`: disc 2's larger bank is the older generation, the same size as the `ICONES.BAK` backup disc 2 also carries **[verified in data: sizes and dates]** |
| Runtime fields in exports | **Keep them, as the retail bank did** (BOX `+0xf8` in 92 boxes, 24 spent spawn markers, `+0x138` after a save load) (owner, 2026-10-03) | Faithful to how Cryo's bank was made; it keeps the byte-identical export test meaningful and needs no list of runtime-written fields |

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
ships when every phase passes. Phase M (the three modes and the developer
folder) comes first because the others build on it; the rest keep their
numbers. Build order: M, 1, 2, 4, 3 (the project page lists phase 4's
bank), 5, 7, D (the direct renderer port; Develop runs on the software
renderer until then), with the optional 6 at any point after 4. Each phase's acceptance checks run through the control channel
(`run.py --ctl`, `recomp/windream/debug/wdctl.py`, `game_nav.py`); tests
skip without the build, the discs or, from phase 1, the July demo.

### Phase 0 — baseline (done, spec 005)

Keypad 1–5 each toggle one flag (`debug_toggle`, `user.c`): KP1 `0x49d5c0`,
KP2 `0x49d5d0`, KP3 byte `0x4ac8c8`, KP4 `0x4a4758`, KP5 `0x4a477c`; a
`WD_KEYS` script reaches the same toggles. `wd_editor_frame` (`lift.py`
`CALLS` at `0x41743a`) calls `0x44d46d` through `guest_call_regs`;
`mouse_post` posts `0x34`–`0x38` into `0x626f70` whenever the handler is
`GAME_TickFrame` or `CTRL_Dispatcher`, editor on or not. All of them are
compiled into every build, release and web included, not behind
`WD_DEVTOOLS`, and live in every session; phase M confines them to Develop.

Acceptance (to automate): with keypad 5 on in Project 0, the screen shows
"Dreams Editor" and "Exit To DOS"; a click on "Exit To DOS" sets
`0x4a4780`; `ShowLink_` draws LINK0's box.

Checked 2026-10-03 through the control channel, Project 0, editor flag
written directly (scripts `out/research/p0check/`) **[verified in the
recomp]**:

- Direct renderer: "Dreams Editor" and "Exit To DOS" draw, the cursor
  sprite shows at (0,0), and `ShowLink_` draws a LINK volume in blue
  lines; no `[direct] FATAL`; turning the flag off again is clean. Picker
  pages and sliders need phase 1 and are not checked yet.
- TGA capture (`0x4a475c`, one shot) works only while the inserted call
  runs, that is with the editor flag set (`wd_editor_frame` tests it).
  Software renderer: `data\tga\H18_0000.tga`, 640×480, the game frame.
  Direct renderer: the same file, all black: `SaveImage_` reads the
  software frame buffer, which the direct renderer leaves empty (phase D).

### Phase M — Play, Develop, Play edits and the developer folder

1. **The option.** The launcher (spec 007) gets a mode choice: Play
   (default), Develop, Play edits, stored as `[port] mode` in `dreams.ini`
   and emitted as `WD_MODE=dev|edited` only when not Play (the launcher
   leaves defaults out); the host reads it once at start and the mode holds
   for the session. `run.py --mode dev|edited` sets the same variable for
   the harness and tests. Play edits is greyed out until the folder is
   initialized. The Play button no longer requires the images in Develop
   and Play edits once the folder exists (`play_blocker`). The web build
   has no launcher and stays Play.
2. **Play.** Every host binding added for Cryo's tools is inert: keypad
   1–9, the DOS-character keys of phase 2, the editor's mouse feed
   (`mouse_post`), the inserted editor call (`wd_editor_frame` returns at
   once). The gate sits in each binding, not in `debug_toggle` alone, so
   `WD_POKE` (`run.py --poke`) and harness scripts keep working in every
   mode. Keys reach the game as retail delivers them; Cryo's debug flags
   stay at their retail values. Data come from the disc images, with CD
   music, exactly as spec 007.
3. **Developer folder, first Develop launch.** The port copies the two
   images into its folder in the user-data directory, merging the discs by
   the "newer copy wins" rule of B1 (170 shared names, 10 differing files;
   disc 1's `DREAMS.DAT`), with two exclusions:
   - disc 1's `DATA\FULL.ID`: visible in a single tree it makes
     `CD_PrepareLevel` run `CD_PurgeCache` (`0x4281d9`), which deletes
     `DATA\3DC\*.dsn`, `*.dan` and `DATA\ANIM\*.hnm` under the install
     root before the copy that should refill them; the copy reads the
     deleted files and `FILE_CopyIfMissing` (`0x428404`) treats a missing
     source as success, so the folder loses its level files at the first
     level load **[verified in code; outcome inferred]**. The host also
     hides it if present, as disc mode and the browser build already do;
   - disc 2's `DATA\GAME`: leftover `GAME.DAT` (300 bytes; retail 340),
     `GAME0.DAT` (10,364; retail 11,388) and `GAME0.ICO`, which would
     become the player's saves **[verified in data]**.

   The launcher does the copy (owner, 2026-10-03), before it starts the
   game, with a progress bar; it writes the "initialized" marker as its
   last step. A launch that finds the folder without the marker resumes:
   files already present at their full size are skipped, the rest copied
   again. Running out of space stops the copy with a message and no
   marker.
4. **Develop and Play edits, every launch.** The port finds the
   initialized folder, copies nothing and serves it as the only root: the
   CD root (working directory, `DATA\1CD.ID`, `2CD.ID`) and the install root
   (`CRYO\DREAMS` prefix stripped, `DATA\HD.ID`, saves in `DATA\GAME`), the
   layout the browser build already serves (`files.c`, under
   `__EMSCRIPTEN__` today). With `FULL.ID` hidden, `LEVEL.ID` and the
   `LISTL` lists are never read. Both ID files are required: without
   `2CD.ID` disc-2 levels hang in `CD_PromptSwap`, without `1CD.ID` disc-1
   levels do. `CD_GetDiscNumber` (`0x4287fa`) tests `1CD.ID` alone, so the
   folder always reads as disc 1 and each disc-2 level shows the swap
   prompt for one frame before its `fopen` succeeds; accepted (B1). The images are not read, except in Play edits for CD audio.
5. **Music.** Develop: a host replacement of `CD_OpenAudio` (`0x4042f1`)
   returns 0, as July's `ACD_Init_`. Then the device id `0x49d1b0` stays 0
   and bit `0x200` of `0x626f80` is never set; MGM `0x1d`, `0x20`–`0x23`
   test the bit, `0x1e` and `0x1f` reach `CD_StopAudio`, which tests the id,
   `CD_TickPlaylist` gets mode 1 from `CD_GetMode` and plays nothing, and no
   path waits on a track, retries or prompts **[verified in code]**. Not an
   MCI error from `imp_mciSendCommandA`: retail shows a modal "MCI Error"
   box for it. Play edits: files from the folder, CD audio from the disc
   images through the existing MCI device; to check that the host can
   serve both at once. Without the images Play edits plays silently.
6. **Develop tools on.** The bindings of point 2 are live, the editor's
   saves land in the folder (phase 5), and the tools of spec 005's
   inventory are reachable on the keys of phase 2.
7. **Play edits tools off.** As Play, with the folder as data.
8. **Renderer.** Develop starts with `WD_RENDERER=software` whatever the
   launcher's renderer setting, until phase D passes (B1); Play and Play
   edits use the setting.

Acceptance: in Play keypad 1–9 and the developer keys change no guest
memory and no `0x34`–`0x38` event is posted; the first Develop launch
creates and marks the folder without `FULL.ID` or disc 2's saves; a second
launch opens no file from the images (the channel's `open` and `disc`
events) and plays a disc-2 level (the one-frame swap prompt is accepted); no `cd` event occurs
in Develop and no "MCI Error" box appears; Play edits plays the folder's
`DREAMS.DAT` with `cd` events; a save made in Develop is in the folder's
`DATA\GAME` after a restart and loads in Play edits; a save naming a
deleted project is refused with a message.

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
   `+0x3df34c`; engine globals one by one. Each July entry is then treated
   by these rules (option C, owner, 2026-10-03):

   | Case | Rule | Examples |
   |---|---|---|
   | Retail reads the field, meaning unchanged | keep the July label verbatim | almost all of the 193 record leaves |
   | Retail meaning changed | our label, from the retail reader | `+0x140` "Camera Combat Angle" → player speed (v/64, `ENT_LoadObject` `0x41d974`); `+0x144` "Camera Combat back" → camera collision off (`0x409d8c`, `0x40a988`); `+0x1c0..+0x1cc` "Particle Four" → fog R, G, B, density (moved next to Fluid and Light); LINKADVENT "Time Cut" → dialogue ID; condition `0x100` "Src SYM END" → source at its initial life |
   | The final bank holds values outside the July range | widen the range to cover them | oxygen `+0x114` up to 10000 (July 0..100), fog density up to 75 (0..32), dialogue up to 177 (`DIALOG.DRD` has 178 entries; July 0..127 or 0..64), BOX intensity down to −600 (0..1000) |
   | The bank sets the field, no retail code reads it | keep, labelled "no effect", so the data stays editable | `+0x13c` "Bruit pas", `+0x1f4` "Perso Integration", action `0x10` "Flag Ele Src KILL" |
   | No retail reader and never set in the bank | hide | `+0xac` "Sphere move", LINKADVENT source box `+0x10` |
   | Bound to an engine global (camera constants, particle attractor 0, contact plane, animated-material switches) | keep only where the retail global is found and means the same | "Debug" → `_MaxiLoad`: retail reused that address (`0x49d9f4`) for an Options toggle, so it is dropped unless proven the same |

   A slider maps 128 pixels onto its range, so a widened range moves in
   coarser steps (oxygen 0..10000: about 78 per pixel); position capture
   with `0` is unaffected. Accepted.

   **Gap entries** (data principle rule 4), for what the final bank uses and
   the July menu lacks:

   | Field | Retail meaning | Entry |
   |---|---|---|
   | Header `+0x1f8` | save mode on level entry: 0 autosave, 1 autosave and clear hotkeys, 2 no save, 3 no save and no restore (`GAME_StartLevel` `0x42f1c9`, `SCENE_RestoreLevelState` `0x41b2b2`) | slider 0..3 |
   | Header `+0x1fc` | chapter and disc group 0..4 (`SCENE_GetLevelNumber` `0x41ad5a`, which reads it at `0x41ad77`; `CD_PrepareLevel`, the `LISTL` lists) | slider 0..4 |
   | Header `+0x140`, `+0x144` | player speed; camera collision switch | the relabelled July entries above |
   | Header `+0x1c0..+0x1cc` | Glide fog colour and density | the relabelled July entries above |
   | LINK `+0x18` bit `0x80` | any actor may take the exit | flag toggle |
   | LINKADVENT condition `0x2000` | the player does not hold the destination object | flag toggle |
   | LINKADVENT action `0x40000` | the player takes damage | flag toggle |

   No entry for fields the engine writes during play (BOX `+0xf8`, spent
   spawn points). The node format allows five children per entry, so new
   entries go into new nested groups as Cryo did ("Flags 2...", "Misc Scene
   2..."): for example "Misc Scene 3..." for `+0x1f8` and `+0x1fc`, and an
   extra flag group for the new event bits.
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

DOS keys (owner, 2026-10-03), Develop only (phase M). The host reads
commands as typed characters (SDL text input), as the DOS build read
`_clavierChar`, and held controls as physical keys, as both builds read
the key-state table. While the editor is on it posts the DOS character as
event `0x33`, so `sceneKeyboard_` (and the pickers' Space and Esc) see the
codes they were compiled for; while it is off the game gets retail
virtual-key events, minus the keys the host consumes.

Rules:

1. A key the host consumes as an editor or developer command is hidden from
   the game: no key-state bit, no event `0x33`. This keeps, for example,
   Shift+1 (`!` on QWERTY) from also using item slot 1.
2. While the editor is on, editor commands win over the July developer
   keys: `A`, `D` and `6` act only as editor commands (July also hid the
   HUD, played dialogue 0, or started full capture with Δt pinned at 2.0).
3. Keypad keys never count as characters (with NumLock they type digits
   and `-`); the keypad stays host keys.
4. The launcher's WASD remap is suspended while the editor is on; the
   player walks with the arrows (`W`, `A`, `S`, `D` are editor commands).
5. Retail game keys keep working unless a rule above hides them. F1–F6
   still switch the editor off (`VID_SetResolution`), as F6/F7 did in July.

Encoding. The DOS build's characters are code page 850, not 437: the July
cases compare `§` = `0xF5`, `µ` = `0xE6` and `ù` = `0x97` (CP437 has `§`
at `0x15`) **[verified in code]**. The host maps typed Unicode characters
to CP850 and ignores any character CP850 lacks.

Consumption. SDL reports a key press (`SDL_EVENT_KEY_DOWN`) before the
character it types (`SDL_EVENT_TEXT_INPUT`), in the same pump. The host
holds a key's state bit and its event `0x33` back until the pump is
drained; if the key produced a consumed character, both are dropped, and so
are that key's repeats and its release. The Ctrl+Shift fallbacks type no
character and are matched by scancode. Text input is on only in Develop.

**Editor keys** (editor on). Letters are uppercase (Shift+letter on any
layout):

| | Project | Objet | Link | Box | Event (LinkAdventure) |
|---|---|---|---|---|---|
| Create | `A` | `Z` | `E` | `R` | `T` |
| Load | `Q` | `S` | `D` | `F` | `G` |
| Save | `W` | `X` | `C` | `V` | `B` |

| Char | AZERTY | QWERTY | Action |
|---|---|---|---|
| `1` | Shift+`&` | `1` | intro movie picker |
| `2` | Shift+`é` | `2` | mesh picker (LOAD MESH) |
| `3` | Shift+`"` | `3` | exit-target picker |
| `4` / `5` | Shift+`'` / Shift+`(` | `4` / `5` | add / drop a BOX path point |
| `6` | Shift+`-` | `6` | symbol picker |
| `0` | Shift+`à` | `0` | capture the position into the visible X/Y/Z/angle rows |
| `?` / `.` | Shift+`,` / Shift+`;` | Shift+`/` / `.` | copy / paste project |
| `/` / `§` | Shift+`:` / Shift+`!` | `/` / Ctrl+Shift+2 | copy / paste objet |
| `:` / Ctrl+Shift+3 | `:` / Ctrl+Shift+3 | Shift+`;` / Ctrl+Shift+3 | copy / paste link (`!` is the toggle) |
| `%` / `µ` | Shift+`ù` / Shift+`*` | Shift+`5` / Ctrl+Shift+4 | copy / paste box |
| Space / Esc | | | confirm / cancel a picker page; hidden from the game while a page is open |
| F10 | | | write the bank (phase 5); F10 is the retail key help while the editor is off |
| left mouse button | | | menu rows, sliders, picker rows |

The Ctrl+Shift fallbacks post the paste codes (`0xf5`, `0x21`, `0xe6`) and
work on every layout; `§` and `µ` also work where the layout has them.

**Developer keys** (Develop, editor on or off unless rule 2 applies):

| Key | Action | Reconnected by |
|---|---|---|
| `!` | toggle the editor at any width; no save (B1) | host writes `0x4a477c` |
| `-` | free-fly camera; mouse turns, buttons fly, PgUp/PgDn/Home/End held change the speed; inert while the editor is on | host posts camera message `0x31` (`CAM_PostMessage` `0x409998`) |
| `9` | overhead camera | host posts `0x30` |
| `8` | Frame Rate / Mem 3DTR readout (also keypad 1) | host toggles `0x49d5c0` |
| `6` / `7` | capture every frame (Δt 2.0) / one frame, to `DATA\TGA` | host toggles `0x4a4758` / `0x4a475c` |
| `A` | HUD on/off | host toggles `0x49d5d4` |
| `D` | dialogue test | host posts `0x40` to the HUD queue |
| `H` | replay the level's movie | host call |
| `r` | record a demo / stop and write `DATA\REPLAY.BIN` | host calls the recorder (spec 005) |
| `e` `f` `l` `v` | render classes (Develop runs on the software renderer; the game's Load page stays on Shift+`L`, which types no developer character) | host calls `0x4577cc` |
| keypad 1–5 | readout, object HUD, collision view, capture flag, editor (as phase 0) | host |
| keypad 6–9 | give all items, collision views, profiler overlay, console window (spec 005; phase 7) | host |
| keypad 0 | Cryo's Save page (`0x4313b5`) | host call (phase 7) |

Held keys stay physical and reach the game: arrows, Alt, Ctrl, Space,
Insert (look), Backspace (collision mesh, already live), PgUp, PgDn, Home,
End. `ù` on AZERTY types character `0x97`, which retail's hotkeys read as
the object page (`MENU_OpenObjectPage`), unreachable from Windows key codes.

**Game keys** (retail, every mode): arrows move (Insert held: look);
Alt, Ctrl, Space, Esc action buttons, Esc/Space also stop a video; `1`
`2` `3` item slots; F1–F6 resolution; F10 key help; F11/F12 letterbox (F11
also host fullscreen); Tab spell menu; `L` Load page; `P` pause; `J`/`K`
joystick/keyboard; Ctrl+S shadow mode; Ctrl+I+R "Girl Power"; Alt+5…Alt+0
camera presets; Alt+X quit; menus: arrows, Return, Esc, Tab, Backspace.

**Clashes and their resolution:**

| Clash | Origin | Resolution |
|---|---|---|
| `!` toggles and pastes a link | July | `!` only toggles; paste link on Ctrl+Shift+3 |
| `A`, `D`, `6`: editor command and developer key | July | rule 2 |
| Editor `1`–`3` and the game's item slots; QWERTY `!` = Shift+1 | retail added the slots | rule 1 |
| `l` render class and `L` Load page | retail added `L` | in Develop `l` is the render class (rule 1 hides it from the game) and Shift+`L` opens the Load page; phase D decides whether the render classes survive the direct port |
| F10 bank save and key help | retail | editor on: bank save; off: key help |
| F1–F6 switch the editor off | July and retail | kept; listed in the key help |
| Space/Esc confirm a picker and act in the game | **[unverified]** | hidden from the game while a page is open |
| WASD remap and the editor letters | our launcher | rule 4 |
| Keypad digits typed as characters | SDL text input | rule 3 |
| Caps Lock makes plain letters editor commands | DOS too | listed in the key help |
| `§`, `µ`, `ù` exist only on French layouts | layout | Ctrl+Shift+2, Ctrl+Shift+4; the object page needs AZERTY |

The free camera and the editor do not clash: the camera stops while the
editor is on (A5.4). All keys and clashes go into the in-game key help and
the README.

Acceptance: `a` creates nothing and `A` (Shift+a) creates `Project<n>` in
phase 4's bank (or refuses when the bank is full); `Z` then a mesh pick
creates `OBJET<n>` and reloads the level with it; `/` then `§` (or
Ctrl+Shift+2) yields a new OBJET with equal data; `!` opens and closes the
editor without changing any link record; with the editor on, `2` opens
LOAD MESH and the item slot 2 is not used; `-` starts the free camera with
the editor off and does nothing with it on.

### Phase 3 — file pickers

Host replacements for `0x4486bd`, `0x4487ed`, `0x4488bf`, `0x448937`,
`0x4489ab` (fillers) and for the list parts of the picker pages
`0x44902b`, `0x4492d1`, `0x449577`, `0x44a6e4`, `0x44a990`, and the project
page `0x44991e` (which the link-target page `0x44b9a7` also uses; it lists
the host bank of phase 4). The table addresses are pinned in A3. The replacements
keep the pages' screen layout and hit tests (A5.1), read names from the
developer folder (phase M) with the July
patterns, keep 8.3 names and 13-byte semantics, and write the chosen name
into the same target field as the original. The replacement table is
render-owned today (`render_boundary.cpp`); this phase generalizes it or
adds an editor table, without changing how render entries work.

Acceptance: LOAD MESH lists the scene's `.3dc/.dan/.dsn` files; choosing
one writes it to `0x65f8c4 + 0x0c`; nothing outside the editor's own
buffers changes (memory diff of `0x661e18`–`0x661e40` before and after).

### Phase 4 — the project bank

A host bank of 150 × 0x2200 bytes (1.3 MB) in guest memory, unpacked at
run time, not by a build step: the bank must come from the developer
folder, which the build never sees, and unpacking 138 KB of
zero-run RLE takes milliseconds. The host does it at the first level load
in Develop (the pointer binding below needs the bank from then on), from
the `DREAMS.DAT` the game itself mounted (`_RLE_SAVES` `0x633c14`, already
in memory after `DDAT_Load`), so it edits exactly the bank being played.

Traced 2026-10-03 **[verified in code]**:

- Only six functions touch `0x659044`–`0x65b243`: `0x449822` (free-slot
  search) and `0x44988f` (project list, which also `strcpy`s names into the
  one-byte tables `0x661e25/26`) walk 150 records; `0x448c6d` clears
  0x13ec00 bytes, called only by `DDAT_Load` when `dreams.dat` fails to
  open; the compressor `0x448ec9` and the mastering functions `0x447b72`,
  `0x447e7c` walk 150 records but are dead. No other module reads the range.
- The quick reload copies through `_LoadSaveSceneSPtr` (A4.3), never by
  index, so a pointer into the host bank is all it needs.
- No retail path keeps a pointer into `_RLE_SAVES` across frames:
  `DDAT_LoadRecord` uses a local pointer and unpacks at once; the one
  pointer held across frames (`0x5e547c`, from `SCENE_CheckExits`,
  `ENT_ApplyAttackHit`, `GAME_LoadGame`, consumed by `GAME_Tick`
  `0x424568`) points at `_CurrentScene2S`, `_CurrentSceneS` or `0x52c970`.
  Saves store the record name and reload by name, so they see edits.
- The format: a zero run is `00 k` (greedy, k ≤ 255), any other byte is
  copied; offset[i] at `+4i`, data from `+0x400`, offset[150] at
  `0x633e6c`; `DDAT_LoadRecord`'s memset clears only the compressed length,
  so every record must decode to exactly 0x2200 bytes; names are compared
  on the raw compressed bytes. Disc 1's `DREAMS.DAT` passes a decode and
  re-encode round trip byte for byte (138,879 bytes; bytes 0x25c–0x3ff
  zero; 150 unique names; largest record 2,592 bytes) **[verified in
  data]**.

Design. Host replacements of `0x449822`, `0x44988f`, `0x448c6d`,
`0x449e7a` (create), `0x449ee9` (load) and `0x449f80` (delete) work on the
host bank and set `_LoadSaveSceneSPtr` (`0x661d94`) into it, so `0x449f42`
(project save) and the quick reload work unchanged. `0x44991e` pages the
one-byte name tables and goes to phase 3 with the other pickers. When a
level loads, the host binds the pointer to that level's record by name
(host-only; July needed a `Q` first); without that, `0x44a517` and
`0x44b0e5` call Create when the pointer is 0, which clears the live level.
Create refuses with a message when no slot is free (all 150 shipped
records are in use; B1). On every project save the host
recompresses the bank into `_RLE_SAVES` with its own copy of the
compressor (the retail one hardcodes `0x659044`), in place, between
frames, so the retail `DDAT_LoadRecord` serves edited records on the next
transition. Headroom: the retail bank needs 138,879 of the 152,576 bytes
(13.7 KB free); a save that would overflow is refused with a message.

Runtime state reaches the bank through the live record: `GAME_LoadGame`
sets `+0x138 = 1` (fade colour), so a `W` after loading a save writes it;
other indirect writes are not traced. Exports keep such fields, as the
retail bank did (B1).

Acceptance: load project 12 with `Q`; change its sky speed; save; walk
through an exit and back: the change persists; Create on the full shipped
bank shows the message and leaves the live level intact; Project Delete
zeroes `+0x14` in the host bank only.

### Phase 5 — saving and export

A host `SaveDiskScene_` (`0x44900a`, called by `GAME_Shutdown`) and an
editor key (F10 while `0x4a477c` is set, consumed before the retail F10
key help) write, when the bank is dirty (`0x661da4`, set by the retail
project save and read only by this host function): `EDITOR.DAT` (raw) and
`DREAMS.DAT` (compressed) into the developer folder's root, overwriting
its `DREAMS.DAT` in place as July's `SaveDiskScene_` did. The next Develop
or Play edits launch plays it. The disc images are never touched.

Acceptance: with no edit, the written `DREAMS.DAT` equals the one the
folder started with, byte for byte (the round trip of phase 4); after one
edit, only that record's bytes and the later offsets differ; a restart in
Develop or Play edits plays the edited level.

### Phase 7 — Cryo's developer tools

The Develop tools of spec 005's inventory, on the keys of phase 2. Built
after phase 5. Each is Cryo's code reached by a host call or a data write,
except where noted as host-only.

| Tool | Key | Work | Acceptance |
|---|---|---|---|
| Free-fly camera | `-` | host posts camera message `0x31`; relative mouse mode (hidden cursor, unbounded deltas) while the camera is mode 7, absolute cursor for the editor | mouse turns it, buttons fly it, PgUp/PgDn/Home/End change the speed; `-` again or a `0x2b` post returns to follow; inert with the editor on |
| Overhead camera | `9` | host posts `0x30` | the eye sits 1,500 units above the player; `9` again returns |
| HUD on/off | `A` | toggles `0x49d5d4` | the HUD disappears and returns |
| TGA capture | `6`, `7` | toggles `0x4a4758` / `0x4a475c`; below | one capture writes `<scn>_0000.tga` of the game frame |
| Readouts, object HUD, collision view | `8`, keypad 1–3 | as phase 0 | as today |
| Dialogue test | `D` | host posts `0x40` with a dialogue id | entry 0 plays with voice and caption |
| Level movie | `H` | host call into the level-entry sequence (`0x416015`) | the project's movie plays again |
| Give all items | keypad 6 | writes `0x49d5e0 = 1` | 14 items over 14 frames; to trace first: adding an item the player already holds |
| Collision views | keypad 7 | host call after the render per collider (`PHYS_GetCollider` `0x45f638`) into `Display_Collision_Sphere_` and the box draws | lines and centre dots over the level |
| Profiler overlay | keypad 8 | host-only: times frame, 3D render, animation and collision, and draws Cryo's bar layout itself (`cpu_init_lib_` hangs in `vbl_`, a double start calls `getch_`) | the bar tracks the frame time |
| Console window | keypad 9 | host-only: opens or closes a console window (Windows) or uses the terminal (Linux) showing the game's stdout; the dump helpers print there | Cryo's diagnostics appear as the game prints them |
| Save page | keypad 0 | host call `0x4313b5`; to trace first: its typed-title entry, dead since retail | a titled save appears in the Load list and restores |
| Demo recorder | `r` | host calls into `0x40db80` / `0x40dac2` / `0x40ee88`; to trace first: the start sequence, the flags `0x49d34a`, `0x4a2f05`, `0x49d33a`, `0x49d5dc`, and the writer's 112-byte frame against the shipped 104-byte `REPLAY.BIN` | a recording replays the same inputs |
| Render classes | `e f l v` | host calls `0x4577cc` on `[0x4fbdbc]` | the level's texturing changes and changes back |

TGA capture: create `data\tga` in the developer folder before the first
write (the retail writer does not check `fopen`). `SaveImage_` runs from
`WorksEdit_` step 1 whether the editor is on or off, but the inserted call
reaches `WorksEdit_` only while the editor flag is set, so in Develop
`wd_editor_frame` calls it every frame (with the flag off it draws
nothing: `ShowBox_` needs a working BOX in use). Develop runs on the
software renderer, where the capture is the game frame (phase 0 check);
the direct renderer's black capture is phase D's.

The save guard of B1 (refuse a save whose project no longer exists) is
built here too; to trace first: where the Load page reaches
`GAME_LoadGame`, so the host can refuse before `DDAT_LoadRecord` returns
NULL.

### Phase D — the direct renderer port

Develop moves from the software renderer to the launcher's setting once
every editor page and tool works under the direct renderer (owner,
2026-10-03: "we will port it later"). Last phase of the milestone.

- **Reference.** Phases 1–7 record software screenshots of each editor
  page and tool (the menu, a slider drag, each picker page, the delete
  banner, `ShowBox_`/`ShowLink_` wireframes, the cursor, the cameras, the
  collision views, the profiler overlay). The port matches them, through
  the project's parity tools (`render_parity.py` and the screenshot
  tests).
- **Early warning.** Each phase from 1 on ends with the direct smoke of
  `out/research/p0check/` (about a minute, not blocking): a new draw that
  stops the direct renderer is known when it is added, not at the end.
  Known on 2026-10-03: the panel text, the cursor and `ShowLink_`'s lines
  already work under direct.
- **Known gaps.** TGA capture writes black under direct; proposed: a host
  replacement of `SaveImage_` (`0x4479c7`), active only under the direct
  renderer, that reads back the presented game frame, as the snapshot and
  autosave-thumbnail paths already do, and writes it at 640×480 with
  Cryo's name, counter and TGA layout (host-only structure). The collision
  views' centre dots go through `C3D_Pixel_` (`0x465ea4`), which the
  direct renderer does not replace. The render classes `e f l v` set face
  classes 6 and `0x1c`, which the direct renderer may not support; ported
  or left software-only is decided here.
- **Acceptance.** Every reference screenshot matched under the direct
  renderer with no `[direct] FATAL`; then the launcher stops fixing
  Develop's renderer.

### Phase 6 — mastering lists (optional)

- Run the October generators' logic over the host bank (a host
  reimplementation or a repository tool next to `bank_patch.py`; the
  originals walk `0x659044`) and write `listL*.txt`, `copyL*.bat`. Acceptance:
  on the unedited disc 1 bank the lists equal the shipped `LISTL*.TXT`.

## B3. Risks

| Risk | Mitigation |
|---|---|
| A July binding points at a field whose retail meaning changed | bindings table requires a retail reader per row; unproven rows are left out |
| Writing into `_RLE_SAVES` while a record is being read | recompress only between frames, from the editor's save path |
| The 16-slot BOX search writes into LINKADVENT slots | the phase 4 replacements cap at 12; the BOX search (`0x44baed`) is replaced or guarded |
| LINKADVENT indexes are list positions | phase 1 shows slot indexes; keep the July semantics until the engine's reading is traced, and document it |
| Editor mode pins actors and disables exits; a session saved in that state | saves come from the working records, not from actor state; runtime-written fields (BOX `+0xf8`, spawn markers, `+0x138`) are kept, matching retail (B1) |
| `!` paste-and-toggle clash | in Develop the host consumes `!` as the toggle and never passes it to the game; paste link is Ctrl+Shift+3 |
| A key acts twice, as a character and as a key code | rule 1 of phase 2: a consumed key is hidden from the game's key state and events; tests press each developer key and check that no game action word changes |
| Fonts and layout below 640 pixels wide | accepted (owner decision): the editor opens at any width and may be cut off |
| A release without the menu resource, or with one from another build of the demo | `release.py` checks the resource's presence and the source hash; the host validates the file before installing nodes |
| A player's edits break a level or a save | edits stay inside the developer folder; Play always plays the untouched discs; "Reset edits" restores the bank (B1) |
| A save names a project the editor deleted or renamed | the host refuses the load with a message (B1, Saves) |
| The first Develop launch is interrupted or runs out of space | the launcher writes the "initialized" marker only after a complete copy (about 480 MB merged, `disc-layout.md`) and resumes a partial one, skipping files present at full size (phase M) |
| An editor draw stops the direct renderer | Develop runs on the software renderer until phase D; the direct smoke at the end of each phase finds such draws early |
| Capture under the direct renderer writes black | not reached before phase D (Develop is software); the proposed `SaveImage_` replacement there |
| `FULL.ID` or disc 2's saves reach the folder | excluded from the copy and `FULL.ID` hidden by the host (phase M) |
| Create on a full bank clears the live level | the phase 4 Create refuses when no slot is free |

## B4. Verification plan

| Check | Phase | How |
|---|---|---|
| Play is clean | M | keypad 1–9 and the developer keys change no guest memory; no `0x34`–`0x38` event posted |
| Developer folder set up once, then used | M | first Develop launch creates and marks the folder, without `FULL.ID` or disc 2's saves; second launch: no `open` or `disc` event on an image, no `cd` event, no "MCI Error" box, a disc-2 level plays (one-frame swap prompt accepted) |
| Play edits | M | plays the folder's `DREAMS.DAT` with `cd` events; loads a Develop save; refuses a save naming a deleted project |
| DOS keys | 2 | each developer key acts once; consumed keys change no game action word; `!` leaves link records unchanged; CP850 characters reach `sceneKeyboard_` |
| Developer tools | 7 | per tool, the acceptance column of phase 7, through the control channel |
| Software reference screenshots | 1–7 | each editor page and tool, captured under the software renderer as it is built |
| Direct smoke | each phase (not blocking) | `out/research/p0check/` scripts: panel rows, `ShowLink_` lines, no `[direct] FATAL` |
| Direct port | D | every reference screenshot matched under the direct renderer; a capture with non-black pixels |
| Panel and exit node | 0 | screenshot + `0x4a4780` after a scripted click |
| Root rows and a captured position | 1 | control channel: keypad 5, mouse script, read `0x65fb04+0xb4` |
| Binding table proof | 1 | test reads `bindings.tsv` and checks each retail reader address against the lifted code |
| Create and reload an OBJET | 2–3 | `game_nav` to Project 0, keys, wait on `_SemaImportNewScene`, read the OBJET slot |
| Persistence across a level exit | 4 | edit, save, `complete_level_triggers`, return, read the field |
| Byte-identical export | 5 | write `DREAMS.DAT` with no edits, compare with the folder's original |
| `LISTL` round trip | 6 | compare with the shipped lists |

## B5. Open questions

- Original call site: candidate 1 or 2 (A9).
- The trigger of `0x447b72`/`0x447e7c` in the developers' October build.
- How the engine reads LINKADVENT source and destination indexes.
- Whether the July Glide build already read `+0x1c0..+0x1cc` as fog.
- The October developers' menu (values beyond July ranges): only its
  labels' absence is known.
- Why July's `!` saved `data\game.dat`, which no build reads.
- The meaning of project `+0x14` bits 16–18, which Delete clears.
- Whether Space and Esc on a picker page also act in the game.
- Whether the host can serve files from the folder and CD audio from the
  images at once (Play edits).
- The Save page's typed-title entry (phase 7).
- Traces before phase 7: the recorder's start sequence and file format;
  giving an item the player already holds; where the Load page reaches
  `GAME_LoadGame` (the save guard).
- Decided in phase D: TGA capture under the direct renderer (a
  `SaveImage_` replacement is proposed) and whether the render classes are
  ported or stay software-only.

## B6. Work items

1. Pin the engine globals the July menu binds (A3, "Still to pin"); the
   editor's own addresses were pinned on 2026-10-03.
2. Bindings table with proofs (phase 1, step 2).
3. Tree extraction and install (phase 1).
4. DOS-character keys, the consumed-key rules and the developer-key
   bindings (phase 2), with the in-game key help and the README.
5. Generalized replacement table; picker replacements (phase 3).
6. Host bank, `_LoadSaveSceneSPtr` binding, recompression (phase 4).
7. Export and the byte-identity test (phase 5).
8. Fix `src/dreams/formats/project.py` (CD track from `+0x1f8`, strength
   read as radius, liveness by name, last BOX point dropped), which the
   tests of phases 4–6 will use.
9. Register the July names of the retail editor functions in
   `re/names/WINDREAM.EXE.tsv` (report C's mapping is the second source).
10. Release packaging: `resources\editor-tree.json` in `build.py` and
    `release.py` (fails without it); Develop code always compiled in and
    independent of `WD_DEVTOOLS`, which `release.py` keeps banning as today.
11. Phase M: the launcher's three modes, Play's gating of every host
    binding, the developer-folder copy and merge (without `FULL.ID` and
    disc 2's saves) with its initialized check, serving the folder as the
    only root on desktop builds (the browser build's layout), the
    `CD_OpenAudio` replacement in Develop, CD audio from the images in Play
    edits, `run.py --mode`.
12. Phase 7: the developer tools of spec 005's inventory, each with its
    key (phase 2), the console window, the Save page on keypad 0, the save
    guard, and TGA capture, on the software renderer.
13. Phase D: the direct renderer port against the software reference
    screenshots; the launcher stops fixing Develop's renderer.

## File map

| Path | Content |
|---|---|
| `docs/research/cryo-editor.md` | overview of Part A |
| `docs/research/file-formats.md` | the record and its editor labels |
| `docs/specs/005-debug-tools/spec.md` | phase 0, the other debug features and the developer-tool inventory |
| `docs/research/install-and-discs.md` | disc identity, `FULL.ID`, the purge |
| `recomp/windream/host/sdl/user.c` | keypad toggles, `mouse_post`, `wd_editor_frame` |
| `recomp/windream/host/sdl/files.c` | file serving; the browser build's single-tree layout phase M reuses |
| `recomp/windream/host/sdl/winmm.c` | the MCI `cdaudio` device |
| `recomp/launcher/` | `dreams.ini`, the mode option, `play_blocker` |
| `recomp/windream/lift/lift.py` | `CALLS` (`0x41743a` → `wd_editor_frame`) |
| `recomp/windream/lift/replacements.py`, `host/render/render_boundary.cpp` | the replacement table phase 3 generalizes |
| `recomp/windream/host/core/runtime.c` | `shim_alloc`, `guest_call_regs`, `WD_POKE` |
| `out/research/editor/` | reports A–E, decompilations, `fn.py`, scratch scripts (local) |
| `out/research/wip_pcj/` | July trees, symbols, earlier extracts (local) |
| `out/research/devtools-audit/` | developer-tool audit A–D: inventories, dead functions and flags, triggers (local) |
