# 008 — The Cryo level editor: what survives, how it was used, and bringing it back

Status: **Draft. Part A (knowledge) is complete to the level of static
analysis of all six July and October builds plus the banks and discs. Part B
(restoration): phase 0 is what spec 005 already runs (keypad 5, host mouse,
one inserted draw call); phases M (launch modes) and 1 (the July menu) are
built and checked live (2026-10-04); phases 2–7 and D remain. Owner decisions of
2026-10-03: the launcher offers three modes, **Play** (default; the discs as
shipped), **Develop** (Cryo's in-game developer tools, the editor included,
playing from a local copy of the discs where edits land as they did for the
developers) and **Play edits** (the developer folder's game with no tools,
with CD music); Develop reads keys as the DOS build did, by character, so
`!` opens the editor; Develop ran on the software renderer until the
direct-renderer port (phase D, 2026-10-04), and now follows the renderer
setting; the July menu is bundled with the release; one
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
| `_TableObjetIdent` (sprite sets by index), `_TableSpriteIdent` | 12e32c, 12df2c | | 62bdac, 62b9ac | written only by `LoadFileSpr_` (`0x43eea0`) and its free routine; set 3 (`0x62bdb8`) read only by the leaf editor (`0x44ce14`, `0x44d090`), never loaded in retail (phase 1) |
| `_Objet0` | 299b54 | | 661da8 | the sprite descriptor of the sliders and (leftover) the cursor |
| font slots (`TEXT_LoadFont`, 0x40c each) | | | 5ea4bc | 0 `HI640`, 1 `HI480`, 2 and 3 `HI320` (`GAME_Init` `0x415780`…) |
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

Engine globals the July menu binds, pinned 2026-10-04 (27 leaves on 22
symbols, each proven by a matched July/retail function pair; rows in
`out/research/phase1-globals/engine-globals.tsv`, the source of phase 1's
bindings table) **[verified in code]** except `_K_OBJ_TARGET_PAS`
**[verified in data]**:

| Original name | Retail | Proof (July = retail) | Same meaning? | Rule |
|---|---|---|---|---|
| `_TableParticleGravity` +0/+4/+8/+0x20 | 4a2fc4 / c8 / cc / e4 | `PAR_InitParticlesAttract_` = `PART_SetAttractor` 0x43b9ae; `ShowGravityFirst_` = 0x43ca25 | yes: both overwrite attractor 0 every tick with the player's position, strength 4 | keep (a live readout) |
| `_TablePlaneGravity` +4 (Y min) | 4a3088 | the contact-plane mover = 0x43c5c1 | yes; particle type 3 only (1 level) | keep |
| `_TablePlaneGravity` +0, +8 (X, Z min) | 4a3084, 4a308c | PARTICLE.C block offset | unread in both builds | hide |
| `_ParticuleSprite` | 4a2f9c | 0x43ca25, 0x43ca8e | only dead code reads it | hide |
| `_particule0Immortel` | 4a2fb0 | `CompParticles_` = 0x43cc32 | yes | keep |
| `_particuleResetNeg` | 4a2fac | 0x43cc32 (0 reloads the level's particles, then 1) | yes | keep |
| `_K_OBJ_TARGET_MIN` / `MAX`, `_K_OBJ_BACK_MIN` / `MAX`, `_Y_LOW_FOLLOW_OBJ` / `_Y_HIGH_` | 49d1cc / d0, d8 / dc, e4 / e8 | `calc_new_cam_and_target_pos_` = `CAM_UpdateFollowPos` 0x40a866; `CAM_InitCameraParam_` = `CAM_LoadPreset` 0x40b729; `calc_new_walk_parm_` = `CAM_ComputeChasePos` 0x409de2 | **no**: retail reloads them every follow frame (below) | shown, not wired (owner, 2026-10-04) |
| `_K_OBJ_TARGET_PAS` | 49d1d4 | layout; initial 25 in both | unread | hide |
| `_K_OBJ_BACK_PAS` | 49d1e0 | the same eye-height formula in both | yes | keep, minimum 1 (ours: 0 divides by zero on the x87 in both builds) |
| `_SpeedCamera` | 49d1f0 | chase/orbit divisor, rewritten every frame | yes | keep |
| `_SpeedTarget` | 49d1f4 | chase/orbit/`CAM_TickTrack` divisor | yes | keep, minimum 1 |
| `_MAP_AnimHnmOk` / `Plasma` / `Scroll` / `Color` | 4a0f68 / 6c / 70 / 74 | `SCENE_HasAnimTexture`, `SCENE_StartAnimTexture`, `SCENE_TickPaletteLighting` 0x42f024 (the same gates in the same order) | yes | keep |
| `_MAP_AnimParticleOk` | 4a0f78 | MAPANIM.C block offset | unread | hide |
| `_MaxiLoad` ("Debug") | **49d9f0** | `testImportNewScene_` = `SCENE_LoadLevel` (the same test at 0x41fb78) | yes; initial 1, never written | keep |
| `_exitDos` | 4a4780 | `GAME_TickFrame`, `WinMain` | yes | keep: the retail exit node (`0x4a4784`, flags 6) already binds it |

Retail block offsets for the three modules: CAMERA.C `+0x3d2050`,
PARTICLE.C `+0x3d82a0`, MAPANIM.C `+0x3d5db4`. The retail initial values
equal July's except where retail inserted variables (the camera preset
table at `0x49d1f8`, the fight-mode toggle at `0x49d9f4`).

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
| Engine globals | 27 leaves on 22 symbols (A3) | `_TableParticleGravity` (attractor 0), `_TablePlaneGravity` (contact plane), `_K_OBJ_*`, `_Y_LOW/HIGH_FOLLOW_OBJ`, `_SpeedCamera`, `_SpeedTarget`, `_MAP_Anim*Ok` (5), `_ParticuleSprite`, `_particule0Immortel`, `_particuleResetNeg`, `_MaxiLoad`, `_exitDos` |

The engine-global leaves are session tweaks, not bank data. In July the
Option branch tuned the live camera (the six follow constants were loaded
once per level, `ReInit3dEngine_` → `CAM_InitCameraParam_`) and the
per-project camera fields under Project carried values into the record. In
retail `CAM_TickFollow` reloads the six every frame (`CAM_ApplyCloseRange` →
`CAM_LoadPreset`) from the preset table row `0x49d2bc` and the record's
`+0x120..+0x134` and `+0x1d8` when non-zero, so the Project > Camera leaves
act live from the next frame and the Option > Camera globals keep no edit
**[verified in code]**.

**Known not to work: Option > Camera.** The six sliders (Target min and
max, Back min and max, Y low and high follow) are shown with their July
labels and July ranges, but each is bound to a host-only cell of its own
(`shim_alloc`), not to the retail global, so dragging one moves the slider
and changes nothing in the game (owner, 2026-10-04). Binding them to the
globals would look broken (the next frame overwrites the value); binding
them to the camera preset table would change the camera of every level
without its own values, only on camera preset 0, and only until the game
restarts. To tune a level's camera, use Project > Camera: retail reads
those fields every frame and saves them with the level. Each cell starts
at the retail global's initial value, so the slider shows what the game
starts with.

Every reference to the six, checked 2026-10-04 (lifted code; script
`out/research/phase1-globals/camera_refs.py`) **[verified in code]**: the
follow camera reads them (`CAM_UpdateFollowPos` `0x40a866`,
`CAM_ComputeChasePos` `0x409de2`, `CAM_ApplyCloseRange` `0x409ba0`);
`CAM_LoadPreset` (`0x40b729`) writes them, from `CAM_ApplyCloseRange` on
every follow tick with no condition, from the Alt+5…Alt+0 presets and at
level load. No cutscene camera mode (fixed, track, entity pair, ride,
overhead, free) reads them, so they never shaped a cutscene. Since
`CAM_TickFollow` runs `CAM_UpdateFollowPos` before the reload, a write
would last one frame at most. One more writer is dead: `0x40b8d3`, called
by `ENT_TickPlayerControl` every frame, jumps from its prologue straight
to its epilogue (`0x40b8ee`); its body set the six, `_K_OBJ_BACK_PAS`,
`_SpeedCamera` and `_SpeedTarget` by movement mode (walking: target
0x220–0x43c; swimming or flying: 0x20) and eased them in combat stance, a
camera behaviour switched off for release.

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
| 0x02 + 0x04 (6) | button: reaching it toggles its value (`v = (v + 1) & 1`, leaf editor `0x44cd62`), deselects, waits 50 ms; sticky (does not collapse siblings) | 49 |
| 0x40 | bit toggle on the mask: shows `(v & mask) != 0`, writes `(v & ~mask) \| mask·new` | 54 |
| 0x08 / 0x10 / 0x20 | key `0` copies X / Y / Z of `_ObjetReference` | 8 / 10 / 8 |
| 0x80 | key `0` copies the angle | 2 |
| 0x100 / 0x200 / 0x400 | key `0` copies X / Y / Z × 256 (particle space) | 6 each |
| none | plain integer slider | 110 |

A label may fill all 24 bytes, ended by the first child pointer: five
July leaves do, and one branch, "Flags C. Element Misc2..", whose label
then runs into its children's address bytes **[verified in data]**. The
two LINKADVENT flag groups ("Flags Condition...", "Flags Action...") are
single nodes listed under both "Link Adv Objet With..." and "Link Adv Box
With...": 351 distinct nodes **[verified in data]**.

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
(20,20)  LOAD MESH               (150,20) highlighted choice   (300,20) its second column
                                 (150,30) current value of the field (asset pages only)
(20,40)  UP
(80,50 .. 120) 8 rows; x=160 a second column (project: scene file; objet: mesh)
(20,130) DOWN
(20,150) EXIT
```

The second-column highlight at (300,20) is drawn on the project and objet
pages; those two and the link, link-adventure and box pages draw no current
value at (150,30); the box page draws its highlight in font 0 **[verified in
code `0x44991e`, `0x44a1d3`, `0x44bbd0`]**. Hit tests read the mouse's y
raised by 6 inside the editor (`WorksEdit_` `0x44d46d`), so on screen each
zone sits 6 px above the drawn row (phase 3 "Built").

Delete pages add a "DELETE <kind> DELETE …" banner: the objet, link,
link-adventure and box pages at (150,20), over the highlight; the project
page at (150,30) **[verified in code `0x44a62b`, `0x449f80`]**. Below 640
pixels wide the fonts drop two sizes; the layout assumes 640. The July
toggle refused any other width.

Slider: eight 16-px segments of sprite set 3 (`alphabe2.spr`) and a knob,
in the DOS build only: the Windows builds' blit is empty (July `0x402386`,
retail `0x402406`) and retail no longer loads set 3 **[verified in code]**;
the port restores both (phase 1).
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
| Demo recorder | `r` / F3 records; title menu and idle play `REPLAY.BIN` (1,848 frames) | start and load dead, the save `0x40edaf` live but reached only when the buffer fills; shipped `REPLAY.BIN` is a DOS recording (104-byte records; Windows packs the same struct in 112) | `0x49d34a` |
| "Collision view" | never written (BEN11 `_build_list`) | never written | `0x4ac8c8` |
| Renderer profiler (`3DC_PROF.C`) | DOS software and Windows, behind `_rendertype` | Windows: whole module present, uncalled; DOS Glide: gate never written | `_rendertype` (DOS Glide `0x104f38`) |
| `_MaxiLoad` ("Debug" leaf) | menu | kept, never written (initial 1); the Options page's fight toggle is the new variable after it | `0x49d9f0` |
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
| Saves | Develop and Play edits share the developer folder's `DATA\GAME`; Play keeps its own (owner, 2026-10-03). **Save guard, in every mode (owner, 2026-10-04):** a host replacement of `GAME_LoadGame` (`0x40f94a`; its only caller is `0x437960` in the Load page) reads the save first and refuses one that is not 11,388 bytes or whose project name at `+0x2884` is not in the bank by `DDAT_LoadRecord`'s rule, before anything is overwritten; otherwise it calls the original, so valid loads stay byte-identical to retail. The Load page then shows its own load-failure text (help entry 5) with the guard's wording (phase 7) | The editor never removes a name (Delete zeroes `+0x14` only; Create always writes `Project<slot>`), so editor-made banks keep every save loadable; the guard covers banks changed outside the editor and foreign or wrong-size saves. Without it retail crashes (editor on, or no level lookup yet since boot) or loads the wrong level (it falls back to the last level name, `0x633bf4`) **[verified in code]**. Every disc save names an existing project, so valid saves are untouched in Play |
| Our harness | **Untouched and the same in every mode** (owner, 2026-10-03): the control channel, the MCP tool and the `run.py` options stay a build-time development option, absent from release builds | Owner decision: "not our work to build it" |
| Developer folder | **A local copy of both discs in the user-data folder** (owner, 2026-10-03). The first Develop launch copies the two images into it, except disc 1's `DATA\FULL.ID` and disc 2's leftover `DATA\GAME` saves (phase M). **The launcher does the copy, with a progress bar, and writes the "initialized" marker last; a launch that finds no marker resumes, skipping files already copied at their full size** (owner, 2026-10-03). Each later launch finds the marker, copies nothing and plays from that folder without reading the images. The folder is the game's data tree, like the developers' `C:\DREAMS\DREAMS`: edits and everything the game writes land in it as they are | Owner decision. The July builds have no disc logic at all (no `1CD.ID`, `2CD.ID`, `HD.ID`, `FULL.ID`, `LEVEL.ID` or `ListL` strings): the developers' game read one data tree, which `STATUS.ME` places at `C:\DREAMS\DREAMS\DATA` **[verified]** |
| Music in Develop | **None, as in the developers' build** (owner, 2026-10-03), by a host replacement of `CD_OpenAudio` (`0x4042f1`) that returns 0, as July's `ACD_Init_` did. Not by failing the MCI open: retail answers an MCI error with a modal "MCI Error" box (`CD_ShowMciError` `0x404278`) | Owner decision. The July DOS `ACD_Init_` (`0x42600`) is `xor eax, eax; ret`: the message manager stores 0 and never sets its audio-CD flag, so that build plays no CD music while the rest of `ACD.C` (Miles redbook) is intact **[verified in code]**. Every retail music path then stays silent (phase M) **[verified in code]**. Track numbers were assigned late: 24 of 138 projects in July, 127 of 150 in October **[verified in data]** |
| How it is distributed | The release package is the binary plus a `resources\` folder; the player supplies only the two disc images (owner, 2026-10-03) | Owner decision |
| Where the menu comes from | Extracted at build time from the July demo `DREAMS.EXE` (SHA-256 checked) into `out/recomp/windream/editor/`, bundled as a resource in the release package, never committed; the July fonts likewise (owner, 2026-10-04). `release.py` fails if the resource is missing or its hash differs; a development build without the demo keeps the retail two-node tree. The July tree is a **template** checked entry by entry against retail, with retail-only fields added (phase 1, option C) | Owner decision; no game data in the repository; data principle rule 3 |
| Milestone | **One milestone, every phase** (owner, 2026-10-03): M, 1–7 and D, phase 6 optional as it always was | Owner decision; phase 7 (developer tools) and phase D (direct renderer port) added 2026-10-03 |
| How nodes reach the guest | Host-built 0x40-byte nodes in `shim_alloc` memory; the retail root `0x4a47c4` gets its child pointers (four empty slots plus the exit node), a data write | The retail walker and leaf editor take any tree; no instruction changes |
| Bindings | A committed table mapping each July value symbol (+offset) to a retail address, with the retail reader that proves it; each entry kept, relabelled, widened, marked "no effect" or hidden by the rules of phase 1 (option C, owner, 2026-10-03) | AGENTS.md: no field bound on a label alone; data principle rules 1–3 |
| Picker lists | Host replacements of the page and list functions (the stride is compiled in), registered through the replacement table | Bigger buffers cannot fix `base + i` |
| Project bank | A host bank of 150 unpacked records in guest memory; `_LoadSaveSceneSPtr` (`0x661d94`) points into it and is bound at every level load to that level's record; the bank is recompressed into `_RLE_SAVES` so the retail `DDAT_LoadRecord` serves edited records | Keeps level transitions retail. Retail binds the pointer to a scratch buffer or to the one-record table (A4.3), so the binders are replaced (phase 4) |
| Full bank | **Project Create refuses with an on-screen message when no slot is free**; Delete frees one (2026-10-03, accepted as recommended) | All 150 shipped records are in use (July had 12 free); retail Create with no free slot still clears the live level **[verified in code and data]** |
| Where files go | Into the developer folder, in place: `DREAMS.DAT` and `EDITOR.DAT` at its root, `DATA\TGA\` captures, `DATA\REPLAY.BIN`, saves in `DATA\GAME\`, the mastering lists of phase 6; never a disc image or the retail install | Follows from the developer-folder decision |
| Call site | Keep candidate 1 (`0x41743a`) | Fits every constraint; candidate 2 is equal and unproven |
| Launcher layout | **A mode row at the top of the launcher window**, Play / Develop / Play edits, above the disc setup and always visible; a new **Develop** tab beside Display, Keyboard and Gamepad holds the developer folder's status (size, initialized), the copy's progress, **Reset edits** and the full key list with its clashes (owner, 2026-10-04) | Owner decision |
| Where the keys are shown | **The launcher's Develop tab and the console window**, which prints the key list when Develop starts; Cryo's in-game F10 key help stays retail (owner, 2026-10-04) | Owner decision; no host-drawn text in the game |
| Console | **A separate console window** that keypad 9 opens and closes in Develop (a console window on Windows, the terminal on Linux), showing the game's own `printf` diagnostics and the dump helpers' output (owner, 2026-10-03) | No new in-game UI; the output already reaches `stdout.txt` (spec 005) |
| Keyboard | **DOS keys** (owner, 2026-10-03): in Develop the host reads commands as typed characters, as the DOS build did, and held controls (arrows, Alt, Ctrl, Space, PgUp…) as physical keys. Rules: a key the host consumes as an editor or developer command is hidden from the game (key state and event); while the editor is on, editor commands win over the July developer keys; keypad text is ignored (the keypad stays host keys). Key tables and clashes: phase 2 | Owner decision, superseding Page Up and Ctrl+1..4. The developers worked in DOS (A11.3); the July Windows key map is a translation accident (A5.4) |
| Toggle | **`!`, read as a character** (one key on AZERTY, Shift+1 on QWERTY); keypad 5 stays as an alias. Consumed by the host, never passed to the game; in Play and Play edits it reaches the game unchanged | Owner decision. It frees PgUp for the free camera, as in DOS. `sceneKeyboard_` reads 0x21 as "paste link", so in July one press toggled the editor and pasted a link; paste link moves to Ctrl+Shift+3 |
| Toggle at any width | **`!` opens the editor at every resolution**, even when the layout is cut off (owner, 2026-10-03). The July toggle acted only at a width of 640 (`_ScreenXRes`); the port drops that condition | Owner decision. The layout is drawn for 640 (rows 10 px apart, sliders at label x+180, values at x+380), so narrower frames clip the deeper levels |
| Save on toggle | **None** (2026-10-03, with the DOS keys; supersedes option A, the retail autosave on each press). **Save-anywhere: Cryo's uncalled Save page** (`0x4313b5`: `MENU_InitSaveSlotSelect(1)`, `MENU_RunGameMenu`) on **keypad 0**, restored from the Load page (owner, 2026-10-03); it writes an empty slot, else the least recent unprotected one, so keypad 0 adds a save and never replaces the latest (owner, 2026-10-04; Cryo's page renamed and overwrote the most recent). Traced 2026-10-04: the page's typed-title entry is live; what is dead is its opener and the SYSTEM list item, and its confirm writes only the index, so the host completes the save (phase 7) | July's `!` called `CTRL_SaveGame_("data\\game.dat")` on every press, entering and leaving; the string occurs once in the July binary and no build reads the file (the July Load page reads slot files; retail has no such string) **[verified in code]**. Why it saved is unknown **[unverified]**. The retail autosave (`0x439341`) is no substitute: it keys its slot by the level's display name, so it overwrites the level's own save instead of adding one, writes a protected slot's file, ignores record `+0x1f8` (P0, P5, P10, P52 never save) and must run from the frame hook behind five guards **[verified in code]** |
| How edits are played | In Develop and Play edits, by the folder itself: the editor's save overwrites the folder's `DREAMS.DAT` (and writes `EDITOR.DAT` beside it), and the next launch in either mode plays it, as July's `LoadDiskScene_` loaded `editor.dat` at every start and New Game and `SaveDiskScene_` wrote it back. Play always plays the discs. No separate mod system | Follows from the developer-folder decision; an earlier mods proposal was never approved and is withdrawn |
| Restoring the folder | **A launcher button "Reset edits"** that copies disc 1's `DREAMS.DAT` back over the folder's and keeps captures and saves (2026-10-03, accepted as recommended) | Closes the open question of how a player restores the folder |
| Disc number in the folder | **No host change; the one-frame prompt is accepted** (owner, 2026-10-03). Replacing `CD_GetDiscNumber` (`0x4287fa`) to answer from the level record's `+0x1fc` was considered and declined | It tests only `1CD.ID`, so a folder with both ID files always reports disc 1 and every disc-2 level passes through `CD_PromptSwap` ("Please change to CD no 2"), shown for one frame **[verified in code]** |
| Renderer in Develop | **The software renderer, until the direct port (phase D) passes** (owner, 2026-10-03; supersedes "the launcher's setting"). The launcher shows the renderer as fixed for Develop, with a note; Play and Play edits keep the setting. After phase D, Develop follows the setting. **Phase D passed on 2026-10-04: Develop follows the setting** (`run.py --mode dev` and the launcher take the platform default, direct on Windows); the render classes `e f l v` stay software-only (phase D) | The software renderer runs Cryo's own drawing code, so every tool draws as written (panel, sliders, pickers, wireframes, cursor, centre dots, capture, render classes) and its 640×480 frame is the layout the editor was drawn for; the direct renderer stops on any unsupported draw and leaves the software frame buffer empty (phase 0 check). Software screenshots become the reference the port is checked against |
| Which `DREAMS.DAT` the folder gets | **Disc 1's** (owner, 2026-10-03), the final retail bank | Data principle rule 2. Disc 1's file is dated 1997-10-29, the day `WINDREAM.EXE` was linked; disc 2's 1997-10-08. In 5 of the 6 differing records (P31, P41, P55, P69, P75, P87) disc 2 keeps the value both July banks share and disc 1 changes it **[verified in data]**. It is also what a normal session plays: `DDAT_Load` reads the bank only at start (`GAME_Init`, `BOOT_Run`), never at a swap, so a game started with disc 1 uses disc 1's records for every level **[verified in code]**. Five of the six differing projects are disc-1 levels (groups 1–2); P75 (`E08_END`, group 4) differs only in magic regeneration (6 against 0). Dropped with disc 2's bank: P69 `LINKADVENT2` (dialogue 177, which `DIALOG.DRD`'s 178 entries hold), deleted on disc 1 |
| Merging the two discs | **The newer copy of each differing file wins** (owner, 2026-10-03); disc 1's `DATA\FULL.ID` and disc 2's `DATA\GAME` are not copied (phase M). Of the 10 files that differ (`disc-layout.md`), disc 1's is newer for `DREAMS.DAT` (10-29 / 10-08), `DATA\HNM\INTRO.HNM` (10-08, full 38 MB intro / 09-30, 1.7 MB), `DATA\ICONE\ICONES.BF` (09-10, 372,358 bytes / 08-03, 440,029) and `DATA\HD.ID` (09-18 / 09-01); disc 2's for `DATA\UNIVBE\UVCONFIG.EXE` (DOS only, never read by the port); the rest are junk (`DESCRIPT.ION`, `ANTI-VIR.DAT`, `SETUP.GID`) | Data principle. This **changes** `disc-layout.md`'s "keep disc 2 — larger" for `ICONES.BF`: disc 2's larger bank is the older generation, the same size as the `ICONES.BAK` backup disc 2 also carries **[verified in data: sizes and dates]** |
| Runtime fields in exports | **Keep them, as the retail bank did** (BOX `+0xf8` in 92 boxes, 24 spent spawn markers, `+0x138` after a save load) (owner, 2026-10-03) | Faithful to how Cryo's bank was made; it keeps the byte-identical export test meaningful and needs no list of runtime-written fields |
| What the Windows builds dropped | **Ported** (owner, 2026-10-04: "if there's something the Windows version doesn't do, port it"): where Cryo's tools did something in the DOS build that the Windows builds left out (an emptied function, a resource no longer loaded), the port restores the DOS behaviour as host-only structure, from the DOS code. First case: the slider tracks and knobs (phase 1) | Owner decision. The Windows builds were a translation of the DOS game, in which the tools were developed and used (A11.3); a stub there is a gap, not a design |

### Files the editor needs

Checked 2026-10-03 against the July demo, both retail discs and the
install **[verified]**:

| File | Used by | July demo | Retail discs | Source for the port |
|---|---|---|---|---|
| July `DREAMS.EXE` (DOS software) | the menu tree (build-time extraction) | yes | no | **the only demo file needed**; read at build time, the bundled resource is derived from it, the executable itself is not shipped |
| `DATA\OBJET\SOUR.ALP` | cursor (sprite set 10) | yes | yes, identical (SHA-256) | the player's disc |
| `DATA\OBJET\OBJET0.SPR`, `PARTICLE.SPR`, `ALPHABE2.SPR` | sliders, `InitWorksSprite_` (retail loads only `PARTICLE.SPR`; the port also loads `ALPHABE2.SPR`, phase 1) | yes | yes, identical | the player's disc |
| `DATA\FONT\COURE.016`, `DOSAPP.008`, `SMALLE.006/.008` | the July text printer: font slots 0–3 (July `InitGame_` `0x10131`…) | yes | **no** | **shipped** (owner, 2026-10-04: "ship the demo files"): the build copies the four from the demo, SHA-256 checked, as `resources\fonts\`, and the editor prints in them as July did (phase 1). Retail loads `HI640`, `HI480`, `HI320`, `HI320` there; at the 10-px row spacing `HI480` overlaps, so a build without the demo gives the editor `HI320` instead |
| `DATA\SYM\*.SYM` | the symbol picker (OBJET `+0x1c`) | one (`SHAMAN.SYM`, a 1996 draft) | none | not needed: no bank uses a symbol file; the picker lists nothing |
| `EDITOR.DAT` | the July startup bank | yes | no | not needed: it is only the uncompressed form of the bank (150 × 0x2200 records); unpacking the disc's `DREAMS.DAT`, which is lossless zero-run RLE, gives the retail equivalent, which phase 4 builds and phase 5 writes back as the port's own `EDITOR.DAT`. The July file holds the July game (138 projects, 111 events) and names 39 files the retail discs lack (3 scenes, 10 models, about 25 movies) plus 16 under older extensions (`.3DC` now `.DAN`, `.UBB` now `.HNM`): loaded in the port it would replace the retail levels with broken July ones |

So the release bundles one derived resource (the menu) and the four July
fonts, and everything else the editor draws or lists at run time comes from
the player's discs.

## B2. Phases

The phases are steps inside one milestone (owner decision): the editor
ships when every phase passes. Phase M (the three modes and the developer
folder) comes first because the others build on it; the rest keep their
numbers. Build order: M, 1, 2, 4, 3 (the project page lists phase 4's
bank), 5, 7, D (the direct renderer port; Develop runs on the software
renderer until then), with the optional 6 at any point after 4. Each phase's acceptance checks run through the control channel
(`run.py --ctl`, `recomp/windream/debug/wdctl.py`, `game_nav.py`); tests
skip without the build, the discs or, from phase 1, the July demo.

**Milestone status (2026-10-04, not yet committed).** Every phase is built
and its acceptance passes: M, 1, 2, 4, 3, 5, 7 (7a tools, 7b Save page,
recorder and save guard), D and the optional 6. Live suites on Windows:
`test_editor_tree`, `_menu`, `_keys`, `_pickers`, `_bank`, `_save`,
`_direct`, `test_dev_tools`, `test_dev_save`, `test_developer_disc2`,
`test_mastering_lists`; the same suites pass on Linux (WSL, gcc 14, the
WSLg display: 105 passed, 2026-10-04), with the launcher's. Coordinator
decisions taken while building are marked "Decided 2026-10-04
(coordinator; the owner may overturn)" in phases 3, 5 and 7; the owner's
are in B1. Left unverified, from the phases' "Built" notes:

- keys only as pushed SDL events (the channel's `type`), not a physical
  keyboard or a French layout; Space and Esc on a picker page acting in
  the game (hidden while a page is open, B5);
- a folder file with a name longer than 8.3 (none ships);
- a project save refused for overflow (only in the codec harness), and a
  run without `DREAMS.DAT` (the EMPTY bank of `0x448c6d`);
- the bank write on closing the window, and a failing write (read-only
  folder, full disk); a game ended by the channel's `quit` or killed
  writes nothing, by design;
- the free camera's mouse buttons and speed keys, the dialogue test's
  voice, the console window's own text, give-all then Project 99's exit,
  the box wireframes (no model with boxes found);
- the recorder's death stop only by writing vitality 0; Space ending a
  playback; a save whose level name has no terminator; a save once the
  file numbers run out;
- under the direct renderer, the Save page, the guard's message, the
  delete banner and the cursor checked by eye, not by pixels; the editor
  suites other than `test_editor_direct.py` run on the software renderer;
  the render classes `e f l v` stay software-only;
- phase 6: that the DOS builds' generators are the same code, and that
  the batch files' `D:\CDn\DATA\<dir>` targets must already exist;
- the one-frame swap prompt of a disc-2 level seen in the open events, not
  on a screenshot.

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
8. **Renderer.** Develop started with `WD_RENDERER=software` whatever the
   launcher's renderer setting, until phase D passed (B1); since then
   (2026-10-04) every mode uses the setting.

Acceptance: in Play keypad 1–9 and the developer keys change no guest
memory and no `0x34`–`0x38` event is posted; the first Develop launch
creates and marks the folder without `FULL.ID` or disc 2's saves; a second
launch opens no file from the images (the channel's `open` and `disc`
events) and plays a disc-2 level (the one-frame swap prompt is accepted); no `cd` event occurs
in Develop and no "MCI Error" box appears; Play edits plays the folder's
`DREAMS.DAT` with `cd` events; a save made in Develop is in the folder's
`DATA\GAME` after a restart and loads in Play edits; a save naming a
project missing from the bank (made with `bank_patch.py`) is refused with
the Load page's message (phase 7).

Built 2026-10-04 (branch `spec-008-phase-m`): `host_mode` and the gates
(`host/sdl/user.c`), tree mode on desktop builds (`host/sdl/files.c`,
`WD_TREE`), the `CD_OpenAudio` replacement (`host/sdl/launch_mode.c`,
`lift/replacements.py` `HOST_ENTRIES`), `disc_copy_merged` and `disc_list
--copy-merged` (`recomp/disc/merge.c`), `run.py --mode` and `--tree`, and the
launcher's mode row, Develop tab, copy with progress and resume, and Reset
edits. Checked live through the control channel and the launcher's own
`--play`: Play keeps keypad 1 inert and plays CD track 9 from disc 1;
Develop reads every file from the folder (the `CRYO\DREAMS` prefix
stripped), toggles keypad 1, has no `cd` event and no "MCI Error" box;
Play edits reads the folder and plays track 9 from disc 1; the merged
folder (655 MB) has no `FULL.ID` and no `DATA\GAME`, and its five
differing files are the expected discs' **[verified in the recomp]**.
Closed 2026-10-04, the checks that were left:

- **A disc-2 level** (`tests/recomp/test_developer_disc2.py`, a hard-linked
  copy of the folder whose bank has Project116, H15ARENE, level 3, in slot
  0). Develop: every file opened is the folder's, `CD_PrepareLevel` opens
  `DATA\1CD.ID` then `DATA\2CD.ID` there (the one-frame swap prompt of B1;
  seen in the open events, not on a screenshot), no disc change and no `cd`
  event. Play edits: the files still come from the folder, the host's disc
  marker changes 1 → 2 and track 2 plays from the disc 2 image, matched
  against the image's samples (score > 0.9 at the play event; disc 1's
  track 2 does not match) **[verified in the recomp]**. This also answers
  B5's question: the marker rule does switch Play edits' CD audio to disc 2.
- **A save carried from Develop to Play edits:** done in phase 7b (a keypad 0
  save loads from Play edits' main menu, `tests/recomp/test_dev_save.py`)
  **[verified in the recomp]**.
- **Linux** (WSL, Debian, gcc 14, the WSLg display): the game and the
  launcher build; the launcher's tests (`test_launcher_ui.py`,
  `test_launcher.py`) and the editor and tool suites pass there (see "Milestone
  status" in B2). On a copy of the folder on ext4 (case-sensitive) with
  three `DATA/3DC` files renamed to lower case, Develop installs the 349
  nodes, a typed `!` toggles the editor, LOAD MESH lists all 270 mesh
  files (the lower-case ones under their own names), F10 rewrites
  `DREAMS.DAT` in place (no second file) and adds `EDITOR.DAT`, and the
  Save page writes `game2.dat`/`.ico` into the folder's existing
  `DATA/game`; under the OpenGL direct renderer the picker page, the menu
  and the profiler's CPU-drawn rows show (`overlay_shot`) **[verified in
  the recomp]**.
- **The keypad 6–9 and DOS-key bindings:** phases 2 and 7.

### Phase 1 — the July menu

1. **Extract.** A build step reads the July `DREAMS.EXE` (path from
   `.dreams.local.env`, new `DREAMS_WIP_DIR`; SHA-256 checked), walks the
   tree from `_editorObjetMain`, and writes
   `out/recomp/windream/editor/tree.json` (labels, structure, flags, min,
   max, mask, value symbol + offset), then applies the bindings (step 2) and
   writes the resolved menu, `editor-tree.tsv`, with the retail addresses
   (one row per node: kind, address, range, mask, flags, children, label;
   the July and bindings hashes in its header). `build.py` copies it beside
   the executable as `resources\editor-tree.tsv`; `release.py` puts it in
   the package and fails without it or with other hashes. At start the host
   looks for the resource beside the executable; without it the editor
   opens with the retail two-node tree. (A tab-separated file rather than
   the JSON first named here: the host parses it in a few lines of C.)
2. **Bind.** A committed table `recomp/windream/editor/bindings.tsv`: July
   value symbol and offset → retail address → evidence (retail reader
   address). Working-record leaves translate by base (A3); `_Sema*` by
   `+0x3df34c`; engine globals one by one. Each July entry is then treated
   by these rules (option C, owner, 2026-10-03):

   | Case | Rule | Examples |
   |---|---|---|
   | Retail reads the field, meaning unchanged | keep the July label verbatim | almost all of the 193 record leaves |
   | Retail meaning changed | our label, from the retail reader | `+0x140` "Camera Combat Angle" → player speed (v/64, `ENT_LoadObject` `0x41d974`); `+0x144` "Camera Combat back" → camera collision off (`0x409d8c`, `0x40a988`); `+0x1c0..+0x1cc` "Particle Four" → fog R, G, B, density (moved next to Fluid and Light); LINKADVENT "Time Cut" → dialogue ID; condition `0x100` "Src SYM END" → source at its initial life |
   | The final bank holds values outside the July range | widen the range to cover them | oxygen `+0x114` up to 10000 (July 0..100), fog density up to 75 (0..32), dialogue up to 178 (value v plays entry v−1 of `DIALOG.DRD`'s 178; July 0..127 or 0..64), BOX intensity down to −600 (0..1000) |
   | The bank sets the field, no retail code reads it | keep, labelled "no effect", so the data stays editable | `+0x13c` "Bruit pas", `+0x1f4` "Perso Integration", action `0x10` "Flag Ele Src KILL" |
   | No retail reader and never set in the bank | hide | `+0xac` "Sphere move", LINKADVENT source box `+0x10` |
   | Bound to an engine global (camera constants, particle attractor 0, contact plane, animated-material switches) | keep only where the retail global is found and means the same (A3, pinned 2026-10-04): 16 keep, 5 hide, 6 shown but not wired | "Debug" → `_MaxiLoad` at `0x49d9f0` is kept (same test in `SCENE_LoadLevel`); the six Option > Camera follow constants are shown with July's labels but bound to host-only cells, because retail reloads the globals every frame (owner, 2026-10-04; A4.4 "Known not to work"). Rebinding them to the camera preset row 0 (`0x49d1f8`…) was considered and declined |

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
3. **Install.** At game start, before the entry point (no retail code
   writes the root's child pointers or the nodes; the walker sets only bit
   0 of each node's flags), the host allocates the nodes in guest memory,
   writes labels, child pointers, value pointers and parameters, and points
   the retail root's children at the July branches with the existing exit
   node last. Host-only structure: a comment block names every piece.

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

Built 2026-10-04: `recomp/windream/editor/editor_tree.py` (extract,
resolve, the resource; `build.py` and `release.py` call it),
`recomp/windream/editor/bindings.tsv` (a row per July leaf, then 10 of
ours), `host/sdl/editor_menu.c` (install, the slider port and the font slot below),
tests `tests/recomp/test_editor_tree.py` (offline: the table's rules,
every reader against the lifted code, the resolved menu) and
`tests/recomp/test_editor_menu.py` (live, Develop, 31 s). The 259 leaves:
230 keep, 8 relabel, 3 widen, 3 no-effect, 8 hide, 6 not wired, and the
exit node reused; ours: three July branches relabelled and one moved, a
new group and 5 gap entries. Every reader address
is a lifted instruction that addresses its field (or, for LINKADVENT,
the task `SCENE_InitTriggers` copies it into) **[verified in code]**,
except the four fog fields, which only the DOS Glide build reads
(`DREAMSFX.EXE` `0x29319`…`0x2932b`, `SCENE_SetFog`). What building it
found:

- **The July menu is a graph, not a tree.** "Flags Condition..." and
  "Flags Action..." are each one node listed under both "Link Adv Objet
  With..." and "Link Adv Box With..."; the extraction keeps them shared
  (351 distinct nodes) **[verified in data]**. Hidden nodes: 8; installed:
  349.
- **Labels fill all 24 bytes in six nodes.** For the five leaves the first
  child pointer, 0, ends the string, as in July; the branch "Flags C.
  Element Misc2.." would print its child pointers' bytes, so it loses one
  dot **[verified in data]**.
- **Every slider row crashed retail, and no Windows build drew sliders.**
  Each slider row draws eight 16-px track segments and a knob by calling
  `0x402406` (`0x44ce22`, `0x44d09f`) with the sprite descriptor `_Objet0`
  (`0x661da8`: x `+0x2c`, y `+0x2e`, mode `+0x34`, sprite `+0x3e`, `0x9400`
  track and `0x9800` knob) and the data of sprite set 3,
  `[_TableObjetIdent+0xc]+0x2c` (`0x62bdb8`). The July DOS build called
  `_ZoomSpriteL16` there (`spritea.asm`, `0x1ca86`); both Windows builds
  left an empty function (July `0x402386`, retail `0x402406`). Retail's
  `InitWorksSprite_` (`0x44d57e`, from `GAME_Init`) also loads
  `particle.spr` three times instead of July's `objet0`, `particle` and
  `alphabe2.spr`, so set 3 is never loaded although its descriptor is still
  in the data (`0x4a4684`, index 3), and the first slider row read address
  `0x2c` and crashed. Ported, by the "what the Windows builds dropped" rule
  (B1): at the first editor frame the host loads set 3 with retail's own
  `LoadFileSpr_` (`0x43eea0`) and descriptor, and a host replacement of
  `0x402406` (`lift/replacements.py` `HOST_ENTRIES`) draws as
  `_ZoomSpriteL16` did: entry = the high byte of `+0x3e` in the file's table
  at `+0x408`, placed at x, y minus the record's hotspot, size × 256 / zoom,
  clipped to the rectangle passed and the frame, 8-bit indices through the
  file's palette to RGB565 (byte 2 red, byte 1 green, byte 0 blue), mode
  `0x2000` half and half with the frame, otherwise a plain copy. The track
  is `ALPHABE2.SPR` entry `0x94` (17 × 2, a blue row with a white tick each
  16 px over a white row, hotspot 0,1), the knob entry `0x98` (7 × 11,
  hotspot 3,10). Not ported, as no retail caller reaches them: DOS's mode
  without `0x2000` or `0x4000` (the editor's calls get `0x4000`, which
  `WorksEdit_` leaves in `+0x34` for the cursor), which tints the sprite
  from the frame and blends its edges, and the packed source **[verified
  in code and in the recomp]**. The other caller of `0x402406`,
  `CompWorksSpriteCpu_` (the CPU-load bars, uncalled), now draws too. Under
  the direct renderer the slider pixels go to the software frame buffer,
  which it does not show (phase D; since phase D `dev_overlay.c` hands
  them to it).
- **The July fonts.** The editor prints its title in font slot 0 and its
  rows and values in slot 1 (`TEXT_DrawString` `0x44d5c7` → `TEXT_PrintAt`,
  which adds 2 below 640 wide), the same code as July's `CompPrintSprite_`
  and `GPrintf_`. July's `InitGame_` loaded `COURE.016`, `DOSAPP.008`,
  `SMALLE.008` and `SMALLE.006` into slots 0–3 with 1 px of letter spacing
  (the loader's third argument, record `+4`); retail's `GAME_Init` loads
  `HI640`, `HI480`, `HI320`, `HI320` with none, and `HI480`'s capitals
  (13 px) overlap at the compiled 10-px row spacing. The July files have the
  same format as `HI*.SPR` **[verified in data]**. They ship in
  `resources\fonts\` (owner, 2026-10-04); in Develop the host copies them
  into the data tree's `DATA\FONT` if missing (the developers' tree had
  them there), loads them at the first editor frame with retail's
  `TEXT_LoadFont` (`0x425c61`) into slots 4–7, which retail leaves empty
  (and so sprite sets 4–7: `SPR_LoadSet`'s table has 24 and retail uses 0–3
  and 10), and while the inserted editor call runs slots 0–3 hold the
  records of slots 4–7, then retail's again. So the title is `COURE.016`
  over the darkened band, the rows `DOSAPP.008` (capitals 7 px), and below
  640 wide the `SMALLE` sizes, as in July. Without the files slot 1 holds
  `HI320` (capitals 8 px) instead, which also fits **[verified in code and
  in the recomp]**.
- **"Light Base" lights the actors.** `WorksGetEditor_`'s idle branch
  copies the working project's light fields into the live globals every
  editor frame (`0x44cc98`…`0x44cce8`): Light Medium into the palette base
  `0x4a0f88..`, Light Onde into the flicker amplitudes `0x4a0fa0..`, Light
  Base into `0x62630c..`, which only `ENT_AdaptActorColor` reads (and the
  exit fades in `GAME_Tick`). So dragging "Light Base R" changes the field
  and the actors' ambient at once but barely the picture; "Light Medium R"
  recolours the whole view (red mean of the 3D view 24 at −127, 243 at
  127) **[verified in code and in the recomp]**.

Acceptance, checked live through the control channel (Project 0, editor
flag written, branches opened by their open bit, `tests/recomp/
test_editor_menu.py`): the 349 installed nodes match the resource, the
host cells start at their globals' values; the root rows are legible;
Init Pos and `0` write (−319, −372, −2967), the player's position minus
the scene object's; a scripted drag sets "Light Base R" from 152 to 128
and the ambient R follows; slider rows draw the DOS track and knob (the
track's pixels checked on "Player Speed Move"); every binding address is
in the retail image **[verified in the recomp]**. The direct smoke (`out/research/p0check`,
`--mode dev`) draws the July root rows, no `[direct] FATAL`.

Our placements (data principle rule 4), all in `bindings.tsv`'s last
section: "Misc Player &Scene..." is full, so its last entry, "Phys
Gravite...", moves into a new "Misc Scene 3..." that also holds "Fog..."
(July's "Particle Four...", relabelled) and the two header gap entries
"Scene Save Mode" (`+0x1f8`, 0..3) and "Scene Chapter CD" (`+0x1fc`,
0..4). LINK `0x80` "Flag Player/ Any Objet" joins "Flag In Space/ Out";
condition `0x2000` "Flag E. Dest NOT In Inv" goes to "Flags C. Element
Dest.."; action `0x40000` "Flag A. Player DAMAGE" takes the place of the
hidden "Flag all Ele Dest KILL" in "Flags A. Element Misc..". "Camera
COMBAT..." becomes "Speed & Collision..." for its two relabelled leaves.
Ranges chosen: fog colour 0..255, density 0..128; player speed 0..512 (as
OBJET "Speed", the same actor field); "Part Quant Init" from 0 (0 is the
default, 16), "Bruit pas" to 5, "Perso Integ" from 0, as the bank holds;
BOX intensity −1000..1000 (a negative minimum centres the slider, which
then spans ±(max−min)/2).

Not done in phase 1, by design: the buttons are bound but their pages and
the bank are retail's (one-byte name tables, the one-record bank), so
pickers, project load and save and the create and delete commands misbehave
until phases 3 and 4; the notes in `bindings.tsv` say which.

### Phase 2 — keys

DOS keys (owner, 2026-10-03), Develop only (phase M). The host reads
commands as typed characters (SDL text input), as the DOS build read
`_clavierChar`, and held controls as physical keys, as both builds read
the key-state table. While the editor is on it hands the editor the DOS
character in the frame's key code, for the editor call alone (as built:
not as event `0x33`, see "Built" below), so `sceneKeyboard_` (and the
pickers' Space and Esc) see the codes they were compiled for; the game
gets retail virtual-key events, minus the keys the host consumes.

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
6. While Cryo's Save page is open (keypad 0, phase 7), the developer keys
   are suspended and typed ASCII (0x20–0x7e) goes to its title entry as
   event `0x33`; the key state reports only Backspace, Tab, Return and Esc.

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
| `r` | record a demo / stop and write `DATA\REPLAY.BIN` (Cryo then returns to the title) | host calls `0x40db80` / `0x40edaf` (phase 7) |
| `R` (editor off) | replay `DATA\REPLAY.BIN` | host calls `0x40ee88` (phase 7) |
| `e` `f` `l` `v` | render classes (the software renderer only: under the direct renderer the keys say so and change nothing, phase D; the game's Load page stays on Shift+`L`, which types no developer character) | host calls `0x4577cc` |
| keypad 1–5 | readout, object HUD, collision view, capture flag, editor (as phase 0) | host |
| keypad 6–9 | give all items, collision views, profiler overlay, console window (spec 005; phase 7) | host |
| keypad 0 | Cryo's Save page (`0x4313b5`); while it is open the developer keys are suspended and typed ASCII goes to its title entry | host opener and save (phase 7) |

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
editor is on (A5.4). All keys and clashes are listed in the launcher's
Develop tab, printed by the console window when Develop starts, and in the
README (B1); Cryo's F10 key help stays retail.

Acceptance: `a` creates nothing and `A` (Shift+a) creates `Project<n>` in
phase 4's bank (or refuses when the bank is full); `Z` then a mesh pick
creates `OBJET<n>` and reloads the level with it; `/` then `§` (or
Ctrl+Shift+2) yields a new OBJET with equal data; `!` opens and closes the
editor without changing any link record; with the editor on, `2` opens
LOAD MESH and the item slot 2 is not used; `-` starts the free camera with
the editor off and does nothing with it on.

Built 2026-10-04 (not committed): `recomp/windream/host/sdl/dev_keys.c`
(the DOS keys), wired from `host/sdl/user.c` (`host_pump` hands every key
and text event to it in Develop and flushes it when the pump has drained;
`wd_editor_frame` serves the developer keys and hands the editor its
character; `WD_KEYMAP` is suspended while the editor is on; text input
starts with the window in Develop), the key list in
`recomp/launcher/dev_keys.h` (the launcher's Develop tab and the console at
Develop start, `launch_mode.c`), the control channel's `type` command
(`devtools.c`; `wdctl.Ctl.type`, the MCP tool `game_type`), which pushes SDL
key and text events through the host's own path, and
`tests/recomp/test_editor_keys.py`. Findings and our placements:

- **The editor gets its character for its call alone (host-only
  structure).** Both readers of the frame's key code `0x626fd8` run in the
  same frame, one after the other: the inserted editor call (`0x41743a`;
  `sceneKeyboard_` at `0x44c63d`, the leaf editor's `'0'` at `0x44d0e2`,
  the pickers' Esc/Space, all inside `0x44d46d`, the only caller of
  `0x44c625`) and `GAME_HandleHotkeys` (`0x415aa7`: Esc/Space stop a
  video, `J`, `K`, `P`, `0x97`; everything else from the key-state table)
  **[verified in code]**. Posting the character as event `0x33`, as this
  phase's text first said, would leave unconsumed keys' virtual keys in the
  same code: the left arrow is `%` (copy box), Delete `.` (paste project),
  `a` is VK `A` (create project). So the host keeps a queue of editor
  characters, writes the next one (or 0) into `0x626fd8` around the editor
  call and puts the game's code back after it: the editor sees only DOS
  characters, one per frame as `_clavierChar` gave, and the hotkeys only
  virtual keys. Typed `ù` (`0x97`) is written as the game's code for
  `GAME_HandleHotkeys`, so it opens the object page in Develop
  **[verified in the recomp]**.
- **Consumption.** A key press waits until the pump has drained; the text
  SDL reports after it is its character (CP850; the table is checked
  against Python's `cp850` codec). A taken key gets no state bit, so
  `INPUT_PostEvents` (`0x42493b`) posts no event `0x33` for it; its repeats
  and release are dropped. Editor commands are `sceneKeyboard_`'s table
  (`0x44c1f7`, 29 codes re-based by 0x21) without `!`, plus `'0'`; they are
  taken only while the editor is on and a level plays (`0x626f74` =
  `GAME_TickFrame`), else logged and dropped.
- **A page is open** when one of the 38 semaphores `WorksGetEditor_`
  (`0x44c625`) tests is 1 (host-only predicate); then Space and Esc go to
  the editor and not the game.
- **Developer keys** act in a level, served at the next editor frame
  (`GAME_TickFrame`'s context): `-` and `9` call `CAM_PostMessage`
  (`0x409998`) with `0x31` and `0x30` (camera mode byte `0x52c874` 7 and
  6), `D` posts `0x40` to `[0x626f2c]`, `H` replays the level-entry movie
  steps (`0x41600a`–`0x41605d`: the record's `+0x3c`, MGM `0x17` with 5,
  MGM `0x18`, `0x49d5b8 = 0`, `0x49d5b4 = 1`), `e f l v` call
  `MDL_ReplaceMaterial` (`0x4577cc`) on `[0x4fbdbc]` with July's pairs
  (`Comp3dEngineClavier_`: e 6→3, f 3→0x1c, l 3→6, v 0x1c→3); `8`, `A` and
  `!` toggle `0x49d5c0`, `0x49d5d4` and `0x4a477c` at once.
- **Stubs, consumed and logged "not reconnected yet":** `6`, `7` (capture:
  `SaveImage_` crashes without `DATA\TGA` and runs only from `WorksEdit_`,
  which phase 7 calls every frame), `r`, `R`, keypad 6–9 and 0 (phase 7);
  F10 with the editor on, since built (phase 5 "Built"). Rule 6 (the Save page) waits for its
  opener (phase 7).
- **LOAD MESH** (`2`, then Esc) leaves the level running even with no
  project bound: `WorksGetEditor_` then sets the objet save and the quick
  reload, which do nothing while `0x661d90` and `0x661d94` are 0
  **[verified in the recomp]**.

Checked live (`tests/recomp/test_editor_keys.py`, 15 tests, Project 0,
software renderer, `WD_KEYMAP` WASD): `!` (Shift+1) toggles the editor,
key `1` never reaches the game and the working and project link records
are unchanged; `a` leaves `0x4a46b8` at 0 and reaches the game as VK `A`
(not LEFT); `S` opens the objet picker hidden from the game, Esc closes it
hidden too; `2` opens LOAD MESH with key `2` hidden; keypad 2 with the text
"2" toggles the object HUD and types nothing; `/` copies the working objet
into `0x65d544` and Ctrl+Shift+2 pastes it into a new `OBJET<n>` with equal
data (with `_LoadSaveSceneSPtr` set as retail's `Q` leaves it,
`_CurrentScene2S`); `8` and `A` toggle their flags (`A` neither LEFT nor
A); `-` is inert with the editor on and toggles the free camera off;
`9` the overhead camera; `D` plays a dialogue; `ù` opens the object page
and Esc closes it; `H` (no movie in Project 0) and `l`/`e` are served;
with the editor off `w` is UP again. The phase 1 test's `0` is now typed.
Closed by phase 4 (2026-10-04): `A` refuses on the full bank with a
message and creates `Project149` once record 149 is deleted, and the objet
paste runs on the level's bound bank record (`test_editor_bank.py`,
`test_editor_keys.py`). The LOAD MESH key check now sets the objet's mesh to
`"EMPTY"` first: with a project bound, closing the page on another mesh
saves and reloads the level (phase 4 "Built"). `Z` then a mesh pick creates
`OBJET<n>` in the first free slot and reloads the level with it (phase 3,
`test_editor_pickers.py`). Deferred: the render classes' visible effect (phase 7's acceptance; in
Project 0 the `l`/`e` frames show no clear change **[unverified]**).

### Phase 3 — file pickers

Host replacements for `0x4486bd`, `0x4487ed`, `0x4488bf`, `0x448937`,
`0x4489ab` (fillers) and for the picker pages `0x44902b`, `0x4492d1`,
`0x449577`, `0x44a6e4`, `0x44a990`, and the project page `0x44991e`
(whose callers are Load `0x449ee9`, Delete `0x449f80` and the link-target
page `0x44b9a7`; it lists the host bank of phase 4). Also the objet set
(list `0x44a14a`, page `0x44a1d3`, find-by-name `0x44a4a9`) and the link,
link-adventure and box sets (lists `0x44b3df` / `0x44ad7d` / `0x44bb5a`,
pages `0x44b452` / `0x44aded` / `0x44bbd0`, finds `0x44b6df` / `0x44b07a` /
`0x44be5d`): their one-byte tables run over the same resource-heap globals
(research brief `out/research/phase3-pickers/BRIEF.md`). The table
addresses are pinned in A3. The replacements keep the pages' screen layout
and hit tests (A5.1), read names from the developer folder (phase M) with
the July patterns, keep 8.3 names and 13-byte semantics, and write the
chosen name into the same target field as the original. The replacement
table is already shared by render and host entries (`HOST_ENTRIES` in
`lift/replacements.py`, one table in `render_boundary.cpp`): adding the
addresses is enough, and render entries do not change.

Decided 2026-10-04 (coordinator; the owner may overturn):

1. A file list is refilled each time its page opens; the map list
   (`0x4488bf`, the scene's materials) stays per frame, as retail.
2. Confirm with no valid row (none selected, or past the end of the list)
   closes the page without writing; retail copied whatever lay there.
3. Folder files whose names are not 8.3 are skipped and logged once.
4. Scope: the objet, link, link-adventure and box sets above, and the
   project page pages phase 4's bank list (Load, Delete, and the
   link-target page, which rebuilds it from the bank every frame).
5. Map list entries are 16 bytes (the material name field).
6. Order: the host's sorted order within each pattern (July's DOS
   directory order cannot be reproduced).
7. Searches by name scan only the set's real slots (16 objets, 8 links, 16
   link adventures, 16 boxes), without checking the in-use flag, as both
   builds do.

Acceptance: LOAD MESH lists every `.3dc/.dan/.dsn` file of `data\3dc` (not
the scene's: the filler takes the whole directory **[verified in code
`0x4486bd`]**); choosing one writes it to `0x65f8c4 + 0x0c`; nothing is
written to retail's one-byte tables `0x661e1e`–`0x661e27`, and the
resource heap's globals `0x661e2c`, `0x661e3c`, `0x661e44` stay unchanged
(the wider range `0x661e1e`–`0x661f40` holds other live globals the game
itself changes, `0x661e28`, `0x661e34`, `0x661e38`). Closing LOAD MESH from
Objet Edit, by confirm or by cancel, on a field other than "EMPTY" saves
the objet and the project and reloads the level (`WorksGetEditor_`
`0x44c89c`), so the live record and the level change by design; the
reload needs phase 4's pointer binding.

Built 2026-10-04: `recomp/windream/host/sdl/editor_pickers.c` (23
replacements, Develop only, installed from `host_mode_install`), 23 new
`HOST_ENTRIES`, a `mouse` command on the control channel
(`devtools.c`; `Ctl.mouse`, `wdctl.py mouse X Y`, MCP `game_mouse`), the
helpers `game_nav.page_click` and `page_choose`, and
`tests/recomp/test_editor_pickers.py` (10 live tests, real keys and mouse).

- One page routine driven by a per-page descriptor (title, list, selection
  and start globals, step, bound, semaphore, current-value line, highlight
  font) draws every page in retail's order and runs retail's hit tests and
  keys; the lists live in host tables (`shim_alloc`, which the start-up
  does not clear), 13-byte entries for the four file lists, 16-byte for
  the rest, each with one zeroed spare entry for the bounds that accept a
  row one past the end. Nothing is written at `0x661e1e` or above.
- The file lists come from the game's own `_dos_findfirst_` /
  `_dos_findnext_` / `_dos_findclose_` (`0x4550b0` / `0x455124` /
  `0x45516e`) through `guest_call_regs`, with retail's pattern strings, so
  the host file layer resolves `data\3dc` in the developer folder and sorts
  each pattern by upper-cased name. Live on the folder (both discs merged):
  mesh 270 (16 `.3dc`, 159 `.dan`, 95 `.dsn`), HNM 89, anim 18, symbol 0;
  each equals the folder's listing **[verified live]**. Retail never closed
  its find handle; the replacement does.
- The objet, link, link-adventure and box names are 12-byte fields the
  shipped data fills without a terminator (`LINKADVENT10` runs into `+0x0c`,
  the source objet) **[verified in data]**; the lists keep 12 characters
  and the finds compare the 12-byte field (`strncmp`), where retail's
  `strcmp` read past it. Every caller passes a 16-byte local (or the 12-byte
  link-target field `0x65d650`, which gets project names of at most 10
  characters on the shipped bank).
- Found: `WorksGetEditor_` writes the objet page's list position `0x661d1c`
  into LINKADVENT `+0x0c` / `+0x14` (and the box page's `0x661d38` into
  `+0x10` / `+0x18`) whenever the page closes, Esc included, so cancelling
  those pickers still stores the highlighted row (or −1) **[verified in
  code `0x44c625`]**. Kept as retail; a host change there would be outside
  the pickers.
- Clicking from a test: the editor sees the held button on the frame after
  the press reaches the host's pump. `page_click` steps the game once, so
  it pauses at the first pump after a frame is presented, and lets the
  press and the release through one frame apart; a bare `pause` can land
  in a pump after that frame's editor call and deliver both to one tick
  (no click).
- Reference screenshots (software renderer, project 0, phase D matches
  them): `out/recomp/editor-reference/` `load-mesh`, `load-mesh-scrolled`,
  `load-objet`, `load-link`, `load-box`, `load-linkadventure`,
  `load-symbole`, `load-hnm`, `load-anim`, `load-map`, `load-project`,
  `link-scene`.
- Checked live: every acceptance item above; Objet Load lists the level's
  objets with their meshes and loads the chosen slot into `0x65f8c4`; Q
  lists the 150 bank projects and loads Project12 by mouse and Space; Link,
  Box and Link Adventure Load copy the chosen slot into their working
  records; a past-the-end confirm and Esc write nothing; the link-target
  page lists the bank. The phase 4 tests now choose projects by mouse (their
  `0x661d78` write is gone). Not checked: the pages under the direct
  renderer (phase D); a developer-folder file with a long name (no shipped
  file has one).

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

Built 2026-10-04 (not committed): `recomp/windream/host/sdl/editor_bank.c`
(the bank and its replacements, installed in Develop from
`host_mode_install`), `editor_bank_rle.c` (the codec, plain C99, checked by
the native harness `verify/native/editor_bank_tests.c`), eight
`HOST_ENTRIES` (`lift/replacements.py`), and
`tests/recomp/test_editor_bank.py`. Findings and our placements:

- **Two more replacements than the design listed.** `0x449f42` (Save) is
  replaced too: it does what retail does (the working copy into
  `*0x661d94`) and then packs the bank into `_RLE_SAVES`. A save cannot be
  seen from outside: `W` sets `_SemaSaveScene` in `sceneKeyboard_` and the
  same `WorksGetEditor_` call serves it, and the refusal on overflow needs
  the record as it was before the copy, which the replacement keeps and
  puts back. `DDAT_Load` (`0x448f5f`) is wrapped (the original, then the
  unpack), so the bank follows every read of `DREAMS.DAT`: at boot and again
  at New Game (two `[bank] ... unpacked` lines per run, both before any
  level; since phase 5 a dirty bank is written before the read, see phase 5)
  **[verified in the recomp]**. Retail's `SCENE_LoadLevel` (`0x41f9db`) is a render entry, so
  no level-load hook is added there.
- **The binding is done before each editor call (host-only).** Every
  reader of `0x661d94` runs inside the editor call (`0x449ee9`, `0x449f80`,
  `0x449f42`, the quick reload in `0x44c625`, the pastes `0x44a517` and
  `0x44b0e5`) **[verified in code: the 14 references]**, so the host
  compares the working copy's name with the last one seen and, when it
  changed, points `0x661d94` at the bank record of that name, or 0 when
  none has it (a save then does nothing, as retail with no project;
  never a stale record of another level). Create and Load set the name
  seen themselves. After `Q`, two exits and the editor's next frame the
  pointer is back on project 12 **[verified in the recomp]**.
- **Load and Delete** call the retail page `0x44991e` with a 256-byte host
  buffer (not the 16-byte stack local its unbounded `strcpy` could
  overrun), find the name in the bank and keep the pointer when the page is
  cancelled (retail set it to `DDAT_LoadRecord`'s answer every frame: 0
  while the page shows "NULL"). Load replays what `DDAT_LoadRecord` does on
  a hit (the previous name into `0x633bf4`, `0x4a4804 = 0`) and copies the
  record to `0x65fb04`; `WorksGetEditor_` then loads it. On a cancel
  `WorksGetEditor_` reloads the working copy if the pointer is non-zero
  (`0x44cb54`), so Load hides it (0) for the rest of that editor call and
  the host puts it back after the call. Delete zeroes the whole `+0x14`
  dword of the bank record, as retail zeroed its scratch copy's, and keeps
  the pointer on the level played.
- **The project list** is a host table, 16-byte names then 16-byte meshes
  (`+0x60c`) at `+0x960` (July's layout), count in `0x661d5c`
  (`editor_bank_list_names`, `editor_bank_list_slot` in `host.h`, for
  phase 3's page). The one-byte tables `0x661e25/26` are no longer written,
  so the 150 `strcpy`s no longer run over the resource heap's globals
  (`0x661e28`..; phase 3 research). Until phase 3 the retail page still
  pages those tables: its rows show leftover bytes and a click copies
  garbage, which Load treats as a cancel; a row of the host table is
  chosen by writing `0x661d78 = entry - 0x661e25` and typing Space.
- **Messages** are drawn for 90 editor frames at the screen's foot with
  `TEXT_DrawString` (`0x44d5c7`, font 0) after the editor call, while the
  July fonts are in slots 0-3, and printed as `[bank] ...` on stderr. The
  Create refusal reads "Project Create refused: all 150 projects are in
  use; delete one first" (`out/recomp/editor-bank/create-refused.png`).
- **Closing LOAD MESH saves and reloads.** With a project bound,
  `WorksGetEditor_` (`0x44c89c`) runs the objet save, the project save and
  the quick reload whenever LOAD MESH closes, Esc included, on a mesh other
  than `"EMPTY"`; the reload plays the level's entry dialogue again
  **[verified in the recomp]**. July's semantics, kept; phase 3's page
  decides what a cancel writes.
- **Exits fire only with the editor off** (`SCENE_CheckExits` `0x420b60`,
  the test at `0x420d6d`) **[verified in code]**; the live check leaves a
  level by writing a box-less, condition-less LINK (flags 1) into the
  working project.

Checked: the codec round-trips disc 1's `DREAMS.DAT` byte for byte (138,879
bytes), changes only record 12 and the later offsets on an edit, refuses an
over-full bank and rejects cut or disordered files
(`test_the_codec_round_trips_disc_1s_bank`). Live
(`tests/recomp/test_editor_bank.py`, 9 tests, Develop, software renderer):
the bank equals the developer folder's `DREAMS.DAT` the game holds; the
pointer is bound to Project0's record; `A` on the full bank refuses with the
message and leaves the working project and the pointer unchanged; `Q` and
Space on Project12 load `M01TORN.DSN` with the pointer on record 12; `Q`
then Esc reloads nothing and keeps the pointer; LOAD MESH then Esc saves and
reloads from the bank; sky speed `+0x1e4` changed and `W`: the bank and
`DREAMS.DAT` in memory hold it, no other record changed, and after exits to
Project89 and back to Project12 (both loaded by `DDAT_LoadRecord` from
memory) the working copy has it; Delete (the menu's semaphore, then the
page) zeroes `+0x14` of record 149 in the bank and leaves `DREAMS.DAT` in
memory as it was; `A` then takes slot 149 as `Project149`. The phase 2
objet paste (`test_editor_keys.py`) now runs on the bound record, without
setting the pointer itself. Not checked: a save refused for overflow in the
game (the codec's refusal is checked offline; the shipped bank has 13.7 KB
free), a run without `dreams.dat` (`0x448c6d`'s EMPTY bank) **[unverified]**.

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

Decided 2026-10-04 (coordinator; the owner may overturn): when `DDAT_Load`
is about to read `DREAMS.DAT` again while the bank is dirty, the host
writes the bank first with the same writer, so a New Game plays the edits
and no saved edit is lost. Found while building it: `DDAT_Load` runs only
from `GAME_Init` (`0x4156bf`) and from the boot menu's New Game
(`BOOT_Run` `0x436481`, called once, from `WinMain` `0x417496`), both before
any level, so in retail's flow no edit can be dirty there **[verified in
code]**; the rule is a guard, checked by marking the bank dirty at the main
menu.

Built 2026-10-04 (not committed): `editor_bank_write_disk` in
`recomp/windream/host/sdl/editor_bank.c`, a ninth bank replacement
(`0x44900a`, `HOST_ENTRIES`), F10 served as a frame action in `dev_keys.c`,
`tests/recomp/test_editor_save.py`. Findings and our placements:

- **What July wrote** (`SaveDiskScene_`, WORKS.C `0x1efd4`): when its dirty
  flag was 1, `editor.dat` (the 150 records raw, `0x13ec00` bytes), then
  the whole bank packed by `WOR_SceneRLECompress_` into `dreams.dat`, both
  in the current directory, the flag cleared after; on an open failure it
  printed and exited **[verified in code]**. Retail's `0x44900a` is an empty
  body (`__CHK` only), still called by `GAME_Shutdown` (`0x415872`).
- **The dirty flag** `0x661da4`: retail's `WorksGetEditor_` (`0x44c625`)
  sets it after the project save (`0x449f42`) and on every frame of the
  project delete page (`0x449f80`), after the objet, link, box and event
  deletes (`0x44a62b`, `0x44b1f9`, `0x44b8ee`, `0x44c06c`) and a link's
  mesh pick; `DDAT_Load` clears it **[verified in code]**. The host reads
  it and clears it after a write; it does not set it.
- **What the host writes**: the bank is packed into `_RLE_SAVES` first (so
  a Delete not yet saved reaches the file as it reached July's), then
  `EDITOR.DAT` (raw) and `DREAMS.DAT` (the packed bytes, as long as
  `BANK_HEADER` + offset 150) into `WD_TREE`'s root, each through
  `<name>.tmp` and a rename that replaces the old file, reusing the
  existing file's spelling (case-insensitive match). The rename is ours:
  July wrote in place. With no developer folder, a bank that would not fit
  or a failed write (read-only folder, full disk) nothing is replaced, the
  flag stays set and the message says why (screen foot while the editor is
  on, `[bank] ...` on stderr). F10 on a clean bank writes nothing and says
  so. When the write before a second `DDAT_Load` fails, the host keeps its
  bank and packs it over what the read brought, still dirty.
- **`EDITOR.DAT` is read by nothing in retail**: no `editor.dat` string in
  `GDIDREAM.EXE` **[verified in data]**; July's `LoadDiskScene_` read it.
  It is written for July's layout and for tools, and the launcher's Reset
  edits removes it with the edited `DREAMS.DAT`.
- **When the game quits**: the system page's Quit and Alt+X reach
  `GAME_Shutdown` and write (`[bank] Bank written (shutdown)`, then
  `ExitProcess(0)`) **[verified in the recomp]**; closing the window posts
  `WM_CLOSE` to the same loop **[unverified]**. The control channel's
  `quit` calls `exit` and a killed process writes nothing, so the existing
  Develop tests, which edit the bank in the shared developer folder, leave
  it as it was.
- **`run.py` no longer copies into a finished developer folder.** It ran
  `disc_list --copy-merged` at every `--mode dev|edited` start, and the copy
  replaces every file whose size differs from the disc's: an edited
  `DREAMS.DAT` of another size (and any save) was put back to disc 1's at
  the next start **[verified in the recomp]**. It now skips the copy when
  `.developer-folder` is there and writes that marker after a copy, as the
  launcher does (the launcher copies only before its marker exists).
- **Play edits** reads the written file with retail's `DDAT_Load` (no host
  bank in that mode; `DDAT_Load` reads up to `0x25400` bytes, which the
  writer never exceeds).

Checked live (`tests/recomp/test_editor_save.py`, 2 tests, about 2 min, on
a copy of the developer folder under `DREAMS_OUT/recomp/editor-save`, its
large directories hard-linked): the bank marked dirty at the main menu,
unchanged, and New Game: the host writes before the read and the file is
the folder's `DREAMS.DAT` byte for byte; project 0's sky speed (`+0x1e4`)
saved with `W`, then F10: `DREAMS.DAT` has it, records 1-149 keep their
packed bytes, offset 0 and bytes `0x25c`-`0x3ff` are unchanged, and
`EDITOR.DAT` is the 150 records raw; F10 again writes nothing; another
value, `W`, and Alt+X: the file has it and no `.tmp` is left; a Play
edits start on that folder plays project 0 with that value and installs no
bank. The system page's Quit writes as well (checked by hand,
`out/scratch/p5/explore.py`). The test also covers the `run.py` fix: the
edited `DREAMS.DAT` packs to another size than disc 1's (the sky value's
zero bytes), which the old copy pass reverted.

### Phase 7 — Cryo's developer tools

The Develop tools of spec 005's inventory, on the keys of phase 2. Built
after phase 5. Each is Cryo's code reached by a host call or a data write,
except where noted as host-only.

| Tool | Key | Work | Acceptance |
|---|---|---|---|
| Free-fly camera | `-` | host posts camera message `0x31`; relative mouse mode (hidden cursor, unbounded deltas) while the camera is mode 7, absolute cursor for the editor; the turn deltas `0x4a3154`/`0x4a3158` zeroed after each frame in mode 7 (only events `0x34`/`0x39`/`0x3a` write them and nothing clears them, so the camera would keep turning at the last delta) | mouse turns it, buttons fly it, PgUp/PgDn/Home/End change the speed; `-` again or a `0x2b` post returns to follow; inert with the editor on |
| Overhead camera | `9` | host posts `0x30` | the eye sits 1,500 units above the player; `9` again returns |
| HUD on/off | `A` | toggles `0x49d5d4` | the HUD disappears and returns |
| TGA capture | `6`, `7` | toggles `0x4a4758` / `0x4a475c`; below | one capture writes `<scn>_0000.tga` of the game frame |
| Readouts, object HUD, collision view | `8`, keypad 1–3 | as phase 0 | as today |
| Dialogue test | `D` | host posts `0x40` with a dialogue id | entry 0 plays with voice and caption |
| Level movie | `H` | `0x416015` is a block of `GAME_InitSubsystems` (the boot-time play of the record's movie), not a callable sequence: the host makes its four steps, each a retail call or write: path = the 32 bytes at `0x41564f` (`data\hnm\`) + the record's `+0x3c`, `MGM_SendMessage(0x17, path, 5)`, if it opened `MGM_SendMessage(0x18)`, `0x49d5b8 = 0`, `0x49d5b4 = 1`; only with no video, load, fade or demo under way | the project's movie plays again |
| Give all items | keypad 6 | guards, then writes `0x49d5e0 = 1`; restores held entries afterwards (below) | 14 items in 14 frames with three hotkeys filled; a second press changes nothing; a held `VITESSE` keeps its spelling and count; in Project 99 the exit stays closed until the pick-up |
| Collision views | keypad 7 | host call after the render, for each collider of the collision world `0x66e01c` (pointers `+0x10`, count `+0x40c`; `PHYS_GetCollider` `0x45f638` is a getter of one collider's fields, not a list), into `Display_Collision_Sphere_` (`0x45ea00`) and `Display_Overlap_Sphere_` (`0x45f104`); box draws left out until a model with boxes is found | lines and centre dots over the level |
| Profiler overlay | keypad 8 | host-only timing of four coarse stages (ours: frame, `REND_DrawFrame`, `ENT_TickAll`, `PHYS_ResolveCollisions`, through `lift.py` `CALLS`; July timed `Render_`, `Morph_Obj_*` and the sphere-mesh queries) into the banks `0x68117c`, drawn by Cryo's `Display_Info_Timer_` (`0x4997f8`) and `info_timer_text_` (`0x499874`); `cpu_init_lib_` is not used (it hangs in `vbl_`), the running flags stay 0 (1 makes the bar call `getch_`), nothing is drawn while the frame bank is 0 | the bar tracks the frame time |
| Console window | keypad 9 | host-only: opens or closes a console window (Windows) or uses the terminal (Linux) showing the game's stdout; Cryo's dump helpers `Scan_Mem_` (`0x458384`) and `PrintMisEntry_` (`0x43afcd`) print there once each time it opens | Cryo's diagnostics appear as the game prints them |
| Save page | keypad 0 | host opener, text entry, then the save Cryo's confirm never makes (below) | type "Dev Save 1", Return: `game.dat` lists it with its own `game<n>.dat` (11,388 bytes) and `.ico`; after changing health or inventory, loading it from Shift+`L` restores level, health, magic and inventory; Esc leaves `game.dat` byte-identical; a duplicate title is refused; a first save in a fresh folder on P0 lands in an empty slot; `r`, `-`, `9` typed on the page are text |
| Demo recorder | `r`, `R` | below | a recording of a scripted run saves with the expected count and returns to the title; replayed from a new game it follows the recorded positions exactly until the first key edge and within 50 units after, ends at its last record and returns to the title with input working; Space stops it; a playback that reaches the player's death is stopped by the host with input working; the shipped 104-byte file is refused |
| Render classes | `e f l v` | host calls `0x4577cc` on `[0x4fbdbc]` (July passed handle 0; in the Windows build group 0 is the camera, one node and no faces, and the level's model is `[0x4fbdbc]` = `0xFE0000` in project 0, so 0 would change nothing **[verified in the recomp]**) | the level's texturing changes and changes back |

TGA capture: create `data\tga` in the developer folder before the first
write (the retail writer does not check `fopen`; disc 1 has an empty
`DATA\TGA`, but the folder copy copies files, not empty directories).
`SaveImage_` runs from `WorksEdit_` step 1 whether the editor is on or
off, but the inserted call reaches `WorksEdit_` only while the editor flag
is set, so with the editor off `wd_editor_frame` makes `WorksEdit_`'s test
itself (`0x4a4758 == 1 || 0x4a475c == 1`: call `0x4479c7`, clear
`0x4a475c`); with it on, `WorksEdit_` makes it. Calling `0x44d46d` with the
editor off would also run `ShowBox_`, which draws a working BOX left over
from an editor BOX load (`0x65b330 & 1`). On the software renderer the
capture is the game frame (phase 0 check); under the direct renderer the
host reads the GPU frame back into guest memory first (phase D).

Decided 2026-10-04 (coordinator, under the owner's goal; the owner may
overturn): the tools act from `wd_editor_frame` (every Develop gameplay
frame) on requests phase 2's key path latches, under common guards (a level
playing, no load, no video); the profiler times four coarse stages of ours
and draws with Cryo's `Display_Info_Timer_` and `info_timer_text_`;
`Scan_Mem_` and `PrintMisEntry_` print once each time the console opens;
box wireframes wait for a live check that finds a model with boxes (the
collision spheres ship); the TGA counter restarts at 0000 each session, as
retail (listed in the key help); the host makes `DATA\TGA` in the folder at
Develop start; the dialogue test plays entry 0, as July.

**Built (phase 7a, 2026-10-04).** `recomp/windream/host/sdl/dev_tools.c`
(capture, give-all, collision views, profiler, console tee),
`dev_console.c` (the window; Windows `AllocConsole`, else `/dev/tty`),
`dev_keys.c` (the keys, `H` and `e f l v`), `files.c` (`wd_std_tee`, the
standard-handle writes), `lift.py` `CALLS` (six profiler points), the
channel's `mouse` command with `dx`/`dy`. `tests/recomp/test_dev_tools.py`
(14 live tests, about 3 minutes, on a copy of the folder) checks each row
of the table above except the Save page and the recorder (phase 7b) and
saves each tool's screen as `DREAMS_OUT/recomp/editor-reference/tool-*.png`
for phase D. Per tool, on the software renderer, project 0:

- Free camera **[verified in the recomp]**: `-` gives mode 7; one mouse
  move with deltas turns the drawn camera (group 0 node 0's matrix) once and
  it stays put after; `-` again returns. The flying buttons and the speed
  keys are retail's and not re-checked.
- Overhead **[verified in the recomp]**: `9` gives mode 6 (the eye above
  the player, screenshot); `9` again returns.
- HUD **[verified in the recomp]**: `A` toggles `0x49d5d4`.
- TGA capture **[verified in the recomp]**: `7` writes `H18_0000.tga`,
  921,618 bytes, type 2, 640x480, 24 bit, not black; `6` writes one file a
  frame until `6` again. Keypad 4 is the same flag as `6` (it was "frame
  step pinned at 2.0"; the pin is the capture's).
- Readouts **[verified in the recomp]**: `8` and keypad 2 still toggle
  `0x49d5c0`, `0x49d5d0`.
- Dialogue test **[verified in the recomp]**: `D` starts a dialogue under
  `CTRL_Dispatcher` with its caption (screenshot); the voice is not checked.
- Level movie **[verified in the recomp]**: with a movie in the record
  (`ANGKOR.HNM` written in: project 0 has none after New Game), `H` opens
  `data\hnm\ANGKOR.HNM` and sets `0x49d5b4`; Space stops it. Esc stops it
  too, but a second Esc opens the game menu.
- Render classes **[verified in the recomp]**: the scene group's 33 class
  entries go 3 → 6 (`l`) and back (`e`), 3 → `0x1c` (`f`, flat colours)
  and back (`v`).
- Give all items **[verified in the recomp]**: keypad 6 on an empty
  inventory adds the 14 names and fills the three hotkeys; a second press
  with a held `VITESSE` (upper case, the pick-up's spelling) leaves every
  name, count and level as it was. Not run: the Project 99 exit check.
- Collision views **[verified in the recomp]**: keypad 7 draws the wall
  boxes (red), floor faces (white, `Display_Overlap_Sphere_` ignores the
  colour) and centres (yellow) of the world's colliders. During the
  level's opening camera the drawn camera is elsewhere and the lines follow
  it, as they should: `[0x661ee8]` is group 0 node 0, the camera
  `REND_DrawFrame` draws with.
- Profiler **[verified in the recomp]**: grey frame rows from the top, the
  render stage (red) dominant on the rows between, the frame rate text from
  `C3D_Print_` at (10, 14) (about 23 frames a second headless): Cryo's font
  state works without `Init3D_Mem_`.
- Console window **[verified in the recomp]**: keypad 9 opens a new console
  window (a `run.py` game has a console with no window: the host frees it
  first) and the dumps print into the game's output; keypad 9 closes it.
  What the window shows was not read back (it is a window).
- Play **[verified in the recomp]**: the same keys change none of the
  tools' cells and log no `[tools]` line.

Direct renderer (input for phase D; one Develop game under
`--renderer direct`, project 0, no `[direct] FATAL`, screenshots in
`out/research/phase7a/shots/direct-*.png`): the collision views' lines show
(`C3D_Line_` is a render entry) **[verified in the recomp]**, their centre
dots go through `C3D_Pixel_`, which is not replaced **[unverified on
screen]**; the profiler bar and text write the software frame buffer and do
not show **[verified in the recomp]**; render classes 6 (`l`) and `0x1c`
(`f`) make the level's model vanish (nothing drawn, no FATAL) and `e`/`v`
bring it back **[verified in the recomp]**; the capture reads the software
frame buffer (black, as known; not re-run). The cameras, HUD, give-all,
dialogue, movie and console do not depend on the renderer. All of these
were settled in phase D (below): the dots, the profiler and the capture
reach the direct renderer, and the render classes stay software-only.

**Built (phase 7b, 2026-10-04).** `recomp/windream/host/sdl/dev_save_page.c`
(the Save page), the recorder in `dev_tools.c` (with `MENU_RunGameMenu`
`0x4337c0` counted by a Develop-only replacement), `save_guard.c` (the
guard, installed in every mode by `host_mode_install`), `files.c`
(`files_read_guest`: a guest path read whole through the file layer and
reported to the event log), `dev_keys.c` (keypad 0, `r`, `R`, rule 6),
`user.c` (`dev_tools_pump` in the `PeekMessageA` bridge, where the menu
and the title still run), `lift/replacements.py` (`0x40F94A`, `0x4337C0`;
re-lifted), `lift.py` `CALLS` `0x43630C` (`wd_menu_save_slots`).
`tests/recomp/test_dev_save.py` (12 live tests, about 5 minutes: one
Develop game and one Play edits game on a copy of the folder, one Play
game whose sandbox holds copies of the install root's saves) checks:

- Save page **[verified in the recomp]**: keypad 0 opens Cryo's page on
  the first empty slot (owner, 2026-10-04: an empty slot, else the least
  recent unprotected one, so keypad 0 adds a save and never replaces the
  latest); "Dev Save 1" typed through the host's key path, Return: the
  game is back in the level, `game.dat` lists the title as the most
  recent, with its own `game<n>.dat` (11,388 bytes, project name
  `Project0`) and `.ico` (8,192); a second save with the same title is
  refused and `game.dat` stays byte for byte; `r`, `-`, `9` typed on the
  page are its text and Esc leaves `game.dat` byte-identical and the slot
  list as it was; after vitality, magic and the first inventory entry are
  changed, loading the save from the in-game Load page restores vitality
  and the inventory exactly and magic within a unit (it moves every frame).
  The page's screen is `editor-reference/tool-save-page.png`.
- Two host additions beyond the recipe below **[verified in the recomp]**:
  the slot's title is cleared for the entry (Esc puts the old one back
  from `0x626f30`, as the page does), and the host ends the menu once the
  page has closed (`0x4a1533 = 1`, `0x4a153b = 0x4a155f = 0`, as
  `MENU_HandleGameMenuInput`'s Esc does): retail's save mode never closes
  it (after the confirm `MENU_HandleSystemPageInput` has no branch for
  `0x4a2f35 = 0x4a2f49 = 1` and Esc no longer reaches the menu), and after
  Esc it would fall back to the system list with the cursor on the hidden
  Save item **[verified in code 0x4313b5's callees]**. Characters are posted
  one per menu frame (when `0x5df488`, refreshed once a menu frame,
  changes), since `CTRL_Dispatcher` keeps only the last `0x33` of a drain.
- Recorder **[verified in the recomp]**: the folder's shipped DOS
  `REPLAY.BIN` (316 bytes) is refused by `R`; `r`, a scripted run (the
  research script's keys), `r` again writes `DATA\REPLAY.BIN` of 4 + 112 ×
  count bytes (192 frames) and returns to the title; from a new game `R`
  plays it: positions equal the recording's index for index from the
  level's placement of the player (the first jump of over 100 units; the
  indexes before it hold the previous game's position, which after this
  module's save load differs by one unit from a New Game's, found in the
  phase D run) until the first key edge and stay within 50 units after,
  playback ends, the title
  follows and the input mode byte `0x49d2f8` is what it was; a playback
  that reaches the player's death (vitality written to 0) is stopped by the
  host once the death path opens a game menu (`MENU_RunGameMenu` entered
  while `0x49d34a == 1`), with the input mode back. Captions do not run
  `MENU_RunGameMenu`, so they do not stop a playback.
- Save guard **[verified in the recomp]**: in Play edits, a save replaced
  by 10,364 zero bytes and a save whose project name is `NoSuchProject`
  are refused from the main menu (code 8, the level-state ring unchanged,
  the guard's wording drawn where the in-game page draws help entry 5:
  `editor-reference/tool-save-guard-main-menu.png`); the Develop save
  loads there (phase M's open check "a save carried from Develop to Play
  edits": done); in Play, a run whose sandbox holds the install root's
  foreign saves refuses one instead of crashing at `memcpy_`.

Direct renderer (input for phase D; one Develop game and one Play game
under `--renderer direct`, screenshots `out/research/phase7b/direct-*.png`,
no `[direct] FATAL`): the Save page, its typed title, the save and its
icon (the thumbnail readback) work; the guard's main-menu text shows
**[verified in the recomp]**.

Traced 2026-10-04 (static, and live through the control channel on the
software renderer where marked; scripts and notes in
`out/research/phase7-traces/`):

**Give all items.** One write of `0x49d5e0` makes `GAME_TickFrame`
(`0x4170e4`–`0x41711f`) add item `[0x49d5e4]++` per frame through
`ENT_AddInventoryItem` (`0x42a182`) until the counter reaches 14, then
clear both; a second write mid-sequence is absorbed **[verified in the
recomp]**. Adding never fires anything by itself: it writes the inventory
(`actor+0x30`, 32 entries) and the HUD state, posts message `0x41`, and
binds a usable item to the first free hotkey slot **[verified in code]**.
Three retail quirks matter:

- An item already held gets count + 1, its level follows the count, and
  its stored name is overwritten with the give-all's lower-case spelling.
  `ENT_HasInventoryItem` is case-sensitive and pick-ups store upper case,
  so a held `VITESSE` or `INVIVIB` stops counting: the only exits of
  Projects 99 and 100 (LINK0, "Link with Objet") close, and Project 10's
  `HOLO` rules change **[verified in the recomp for Project 99]**. The
  pick-ups do not respawn while the level's record is in the 8-entry
  level-state ring (`SCENE_RestoreLevelState` hides actors saved with life
  0; Project 100's `INVIVIB` appears only through a LINKADVENT whose source
  is then restored as removed), and the ring travels with saves, so a
  player who returns to Project 99 or 100 holding only the lower-case
  spelling is stranded until eight newer levels push the record out
  **[verified in the recomp for Project 99]**.
- Message `0x41` is posted even when the add is refused, so a full
  inventory leaves a "ghost" hotkey; with the HUD hidden all 14 messages
  pick hotkey slot 0.
- It runs during videos, waits during a dialogue or the game menu, and is
  lost on New Game.

Host recipe: keypad 6 acts only when `[0x4fba78+0x30] != 0`,
`0x661e08 == 0`, `0x49d5b4 == 0`, `0x49d5e0 == 0` and the free entries
cover the missing names, plus one when any of the 14 is held
(`ENT_AddInventoryItem` looks for an empty entry before it matches a held
name, so a held name needs one too **[verified in code 0x42a1de]**); the host snapshots the held entries, writes the
flag, and when it reads 0 again restores the held entries' spelling, count
(`+0x314`) and level (`+0x294`) and the level of any bound hotkey
(host-only structure, labelled as such).

**Save page.** `0x4313b5` is `MENU_OpenLoadPage` in save mode, never
called; the SYSTEM list skips item 1 (Save) **[verified in code]**. Inside
`MENU_RunGameMenu` (`0x4337c0`, the game frozen under `CTRL_Dispatcher`)
the typed-title entry of `MENU_HandleSaveSlotInput` (`0x4373a7`) is live:
codes 0x20–0x7e from event `0x33`, 20 characters, typed into the slot's
index name. Retail gaps: Return writes only `game.dat` (`GAME_SaveIndex`),
never `game<n>.dat` or the `.ico`, so the renamed slot restores the old
autosave; the selection is frozen on the slot `MENU_InitSaveSlotSelect(1)`
picks, which is left uninitialised when no unprotected used slot exists (a
fresh folder on P0, which never autosaves); the page runs in load mode
until its first draw sets `0x4a2f41`; all save and load lookups take the
first slot with a matching name.

Host recipe (host-only structure around Cryo's page):

1. Keypad 0 latches a request; `wd_editor_frame` serves it (the hook is
   the `GAME_HandleHotkeys` call, the `L` key's context) only with the
   editor off, no video (`0x49d5b4`, `0x49d5b8`), no pending load
   (`0x661e08`), no exit fade (`[0x661e04]+0x138 == 1` with
   `0x5e5480 > 0`) and no quit (`0x4a4780`).
2. Snapshot the index (`0x5dabf8`, `0x5dab98`, `0x5dabc0`, `0x52eb70`);
   choose the slot (owner, 2026-10-04: the first empty one, else the least
   recent unprotected one; at most five are protected); make `0x4313b5`'s
   five stores and `MENU_InitSaveSlotSelect(1)`'s own for that slot
   (`0x4a2f3d`, its title into `0x626f30`, `GAME_LoadSaveIcon`,
   `0x4a2f35 = 0x4a2f49 = 0x4a2f55 = 0`, `0x4a2f39 = 1`, `0x4a1563` 2 or
   1); clear the slot's title for the entry; write `0x4a2f41 = 1`; call
   `0x4337c0`. When the page closes (`0x4a2f35 = 1`), end the menu (built:
   retail's save mode never does, see "Built (phase 7b)").
3. While the page is open (`0x4a1537 == 2`, `0x4a153b`, `0x4a155f`,
   `0x4a155b` = 1, `0x4a2f35 == 0`), suspend the developer keys, report
   only Backspace, Tab, Return and Esc in the key state, and post typed
   ASCII 0x20–0x7e as event `0x33`, one per dispatch, into `[0x626f70]`
   while `[0x626f74] == 0x40e75c`.
4. A confirm shows status 4 (`0x4a1563 == 4`). When the menu returns,
   clear `0x626fd8`; if confirmed, take the title of the slot with the
   highest recency; a title another slot already has restores the
   snapshot and shows a message; otherwise call `GAME_SaveGame`,
   `GAME_SaveIndex` and `GAME_SaveThumbnail` on it (the autosave's order),
   each checked for 0. The files are the ones the Load page and
   `GAME_LoadGame` read.

**Demo recorder.** Record: `0x40db80` sets index `0x49d346`, mode
`0x49d34a = 0`, the on-screen input display `0x4a2f05`, and starts the
15-frame reload of the current project that anchors the recording;
`DEMO_RecordFrame` (`0x40d833`) then stores one 0x70-byte record per frame
(the 11 action words, the player's heading, bank and pitch with rates,
movement mode, action and position) into `0x52eb98`, at most 6,144
(204.8 s at 30 Hz; the run-length repeat never merges), with Δt forced to
1.0. Save: `DEMO_SaveReplay` (`0x40edaf`, live, reached only when the
buffer fills) writes install root + `data\replay.bin` as a count and the
records, then quits to the title (`0x4a4780`, reset `0x49d5d8`). Replay:
`0x40ee88` loads (no size check) and calls `0x40dac2`, which sets input
device mode 4 and reloads the level at record 0's position;
`DEMO_PlayFrame` (`0x40e4ed`) ORs each next record's words (edges arrive a
frame early) and pulls the position halfway to the record (only x and z
while walking, `+0x34 == 1`: height is left to physics); it ends at the
last record or on Space and returns to the title, but not once the player
dies: the death path opens the system page and playback, with input still
in demo mode, stays there and Space no longer stops it **[verified in the
recomp]**. Neither the level nor the
random seed is stored, so playback is approximate by design: at the fixed
step it matched exactly until the first key edge and then within 42 units;
under `--retail-timing` it drifted further, and a caption sequence during
playback left input dead after the stop (retail bug) **[verified in the
recomp]**. The 104-byte shipped records are the same struct packed by the
DOS compiler; retail plays a few frames of it and returns to the title.
July's own 1,848-frame recording (the attract demo, project 0 `H18ANGKR`,
made with the editor bank's spawn), repacked to 112 bytes, plays
faithfully for 910 records on retail data (walk, jump, take-off, flight;
11–13 units off walking, 3.6 in flight), then retail ends the flight
early, Duncan walks off the level and dies, and playback hangs on the
death page **[verified in the recomp]**; cut to about 900 records it would
end cleanly **[unverified]**.

Host recipe: `r` (editor off) calls `0x40db80` and sets `0x49d5dc = 1`,
or with `0x49d5dc == 1` calls `0x40edaf` and clears it, as July's key did,
from `wd_editor_frame` with no video, load or fade under way and playback
not running; Cryo's return to the title is kept. `R` (owner, 2026-10-04; editor
off; it is Box Create with the editor on) replays, and the host stops
playback (by calling `DEMO_StopPlayback`) when the game opens a menu page,
which the death path does (built: `MENU_RunGameMenu` entered during
playback; host-only guard against the hang above); the host refuses
`DATA\REPLAY.BIN` unless its size is 4 + n·112 with 1 ≤ n ≤ 6,144, saves
the input device mode `0x49d2f8`, calls `0x40ee88`, and restores the mode
when playback ends (host-only fix for the caption bug). `FILE_GetInstallRoot`
resolves to the developer folder (phase M).

**Save guard.** `GAME_LoadGame` (`0x40f94a`) is the only reader of
`game<n>.dat` and has one call site, `0x437960` in `MENU_HandleSaveSlotInput`,
reached from the in-game Load page (Esc menu, `L`, the death path of
`SCENE_CheckExits`) and the main menu (`MENU_Tick`); no automatic restore
reads a save file **[verified in code]**. It finds the slot by title,
overwrites the level-state ring `0x5e2b08` and `0x49da84`, then reads the
32-byte record name at `+0x2884` and calls `DDAT_LoadRecord`, so the name
is known only after state is lost. Host recipe: replace `0x40f94a` (title
in EAX, result in EAX, other registers kept); when the title and file are
found, read the file through the host file layer and refuse unless it is
11,388 bytes with a NUL-terminated name, not `"EMPTY"`, found in
`_RLE_SAVES` by `DDAT_LoadRecord`'s rule; on refusal return -1 (the menu
shows code 8 and stays in the slot list, nothing written) after writing the
guard's wording into help entry 5 (`0x4a121b`, restored on the next call);
otherwise call the original. The main menu discards code 8 (`0x4a2ef5`);
the host shows the same text (owner, 2026-10-04) by a call to `MENU_DrawHelpText(5)` after
`MENU_DrawSaveSlots` (`0x437c01`) while it is 8 (built: `lift.py` `CALLS`
`0x43630c`; the text lands at the panel's foot, where the in-game page
draws it, `editor-reference/tool-save-guard-main-menu.png`). Wiring: `0x40f94a` joins the replacement table (phase 3
generalizes it) and the read is reported to the control channel's event
log.

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

**Built (phase D, 2026-10-04).** What the direct renderer did not show were
the Develop draws that write the guest frame with the CPU: the sliders (the
host's DOS blit for `0x402406`, `editor_menu.c`), the profiler's bar
(`Display_Info_Timer_` stores pixels) and text (`C3D_Print_` `0x4655b0`),
and the collision views' centre dots (`C3D_Pixel_`). Everything else the
editor and the tools draw already goes through the renderer's leaves: the
panel and picker text, the bands, the cursor, `ShowBox_`/`ShowLink_` and the
collision lines (`C3D_Line_` `0x465c80`), the readouts, the object HUD, the
Save page and the guard's message **[verified in the recomp]**.

- **CPU pixels (host-only, `host/sdl/dev_overlay.c`).** Around each such
  host call site (the editor's call in `user.c` `wd_editor_frame`, the
  collision views and the profiler in `dev_tools.c`) the host fills the
  guest frame with a marker colour (RGB565 `0x0821`), lets Cryo's code run
  unchanged, then hands every pixel that no longer holds the marker to the
  renderer as one raw draw at that point of the frame
  (`wd_render_cpu_pixels`, `OD_DRAW_RAW` with coverage 0 for the others).
  The guest frame is not shown under the direct renderer and nothing else
  reads it, so the marker is invisible; a CPU pixel written with exactly the
  marker colour would be lost (none of these draws uses it). Under the
  software renderer the scope does nothing. The scope costs a 600 KB fill
  and scan per frame only while the editor or one of these tools is on.
- **TGA capture.** `SaveImage_` (`0x4479c7`) reads the guest frame pixel by
  pixel. Before it runs (dev_tools.c with the editor off; with it on, the
  editor scope begins with it because `WorksEdit_` captures in its first
  block) the host reads the GPU frame back into guest memory
  (`wd_render_materialize`: `wd_render_read_frame` at the logical 640×480,
  nearest sample of the window-sized target, the surface's authority set to
  the CPU for the write and back to the GPU after it), and the editor
  scope compares against that copy instead of the marker. Cryo's own
  writer keeps its name, counter and layout; the spec's proposed
  replacement was not needed. A readback per captured frame, so key `6`
  (every frame) is slow under the direct renderer **[verified in the
  recomp: the file matches the screen in the band y 330–400 for 90% or
  more of its pixels, editor off and on]**.
- **Render classes: software-only (decided by evidence).** The direct
  renderer follows the 3dfx build's pipeline: for face types 6 and `0x1c`
  the Glide hook (`DREAMSFX.EXE` `0x67568`) draws nothing, which
  `render_scene_draw.cpp` reproduces (`glide_no_draw`), so `l` and `f`
  made the level vanish. Drawing them would mean porting the Windows
  software rasterizer's two classes (`SW_DrawObjectFaces` `0x473014`)
  into the GPU scene renderer, a renderer change of its own for a
  debugging view. Under the direct renderer the keys now change nothing and
  say "render classes: software renderer only" at the frame's foot; under
  the software renderer they work as in phase 7.
- **The check (`tests/recomp/test_editor_direct.py`, 18 tests, about
  5 minutes).** One software and one direct Develop game, each on its own
  copy of the developer folder, run the same states in project 0: the
  menu, a slider page (Light Base), the ten picker pages, the collision
  views and the profiler. The control channel's new `overlay_shot`
  (devtools.c, a hook at both ends of `wd_editor_frame`) writes the game
  frame of one gameplay frame just before the editor's call and the tools
  and just after them (the GPU frame read back under the direct renderer):
  their difference is exactly what Develop drew. Screenshot pairs with the
  overlay on and off did not work: switching the editor flag moves the
  scene's camera, and the scene moves between frames. Per state the direct
  footprint covers the software one (1.000 on every state; the collision
  lines within 2 pixels, as the camera the two games reach differs a
  little) and adds nothing (at most 0.006), and where the two renderers'
  scenes agree under a pixel (78–98% of the footprint) the colours agree
  within 40 per channel on 99.7–100% of it; the picker bands halve what is
  under them, so they follow each renderer's scene elsewhere. The profiler
  is checked by layout (four full frame rows, four stage rows from x 0, the
  white frame-rate text), since its stage lengths are the measured times.
  Then, under the direct renderer: no `[direct] FATAL`, the TGA capture
  editor off and on, and the render classes left alone.
- **Not pixel-compared here.** The Save page and the guard's message
  (drawn by the game's menu code, the same leaves as the in-game menus;
  checked by eye under the direct renderer in phase 7b), the delete banner
  and the cursor (text through the same leaves as the panel), and the
  cameras, HUD, give-all, dialogue, movie and console, which do not depend
  on the renderer.
- **Renderer switch.** `run.py --mode dev` takes the platform default
  (direct on Windows) unless `--renderer` says otherwise; the launcher
  emits `WD_RENDERER` for Develop as for the other modes and its Develop
  tab notes that the render classes need the software renderer.

### Phase 6 — mastering lists (optional)

- Run the October generators' logic over the host bank (a host
  reimplementation or a repository tool next to `bank_patch.py`; the
  originals walk `0x659044`) and write `listL*.txt`, `copyL*.bat`. Acceptance:
  on the unedited disc 1 bank the lists equal the shipped `LISTL*.TXT`.
- **Built (2026-10-04): a repository tool,** `recomp/windream/debug/mastering_lists.py`
  (standard library only, so it also runs under WSL `python3` without uv; no
  host code, nothing in the executable). It reads the developer folder's
  `DREAMS.DAT` (`--tree`, else `WD_TREE`, else
  `DREAMS_OUT/recomp/windream/developer`; `--bank` and `--out` override) through
  `dreams.formats.project.records`, replays `0x447b72` and `0x447e7c` over the
  flat 150×0x2200 bank with their C semantics (strcpy to the NUL; strupr of the
  last three bytes of a 3DC-folder name only, then the `.DAN` twin when they read
  `3DC`; no empty-name test for a live OBJET; no sorting or de-duplication;
  lowercase `XH_.dan`/`MHE.dan`; CRLF as `fopen "wt"`) and writes
  `listL0..4.txt` and `copyL0..4.bat` into the folder's root. A record in use
  with a group outside 0..4 is an error (retail indexes `FILE*[5]` with it
  unchecked). Research and disassembly: `out/research/phase6-brief/`.
- **Verified** (`tests/recomp/test_mastering_lists.py`, 6 tests, under a
  second): on the unedited disc 1 bank the five lists equal the shipped
  `LISTL0..4.TXT` byte for byte, CRLF included (6, 241, 166, 222, 164 lines),
  and `copyL0..4.bat` (24, 271, 186, 247, 187 lines) have the brief's SHA-256
  values; disc 2's bank gives the same ten files (the two banks differ in no
  field the generators walk). A synthetic bank checks the rules the shipped
  data never exercises: a lowercase `x.3dc` gives `x.3DC` and `x.DAN`, an
  animated-texture name keeps its case, group 0 is staged to CD1 and CD2, a
  LINKADVENT movie with bit 0 clear is skipped, a live OBJET with an empty
  name still prints `DATA\3DC\`. `copyL` has nothing shipped to compare with
  (no `COPYL*.BAT` on the discs, the install or the images).
- **A8 refined (`--check`, information only).** The ten files name 384
  distinct sources; 22 of them are on neither disc nor in the developer
  folder, and retail lists them all (`CD_CopyFileList` counts a source that
  does not open as copied, `0x428452`), so the tool lists them too:
  - OBJET `.3DC` names whose `.3DC` was not shipped (the `.DAN` twins were):
    `SUR`, `AR0`, `H14`, `ISI`, `F07VERT`, `H11`, `CG0`, `F07ORIG`, `F21`,
    `F22`, `GG0`, `MOT`;
  - `.DAN` twins of meshes shipped only as `.3DC`: `ARC`, `EPEE`, `GUN`;
  - in `copyL` only, seven intro and LINKADVENT movies: `CINE_ED1`,
    `CINE_ED2`, `CINE_ED3`, `ARAI_06`, `OEIL_HI`, `F06FEU`, `F07EAU` (`.UBB`).

  Every listed file that exists is on the disc its group plays from, as A8
  says, except the fixed `copyL0` line for `DIALOG.DRD`, which disc 2 does not
  have. The retail DOS executables hold the same generator strings, with
  unpooled duplicates (13 `D:\CD1` literals against the Windows build's 9);
  that their code is the same generator is not checked (not matched in
  Ghidra).
- **Case on Linux.** The developer folder holds `LISTL0..4.TXT` from disc 1.
  The tool writes over an existing file whose name matches case-insensitively,
  under that file's name, and removes further matches, so a case-sensitive
  file system keeps one file per list (checked under WSL). Reset edits
  restores only `DREAMS.DAT`; regenerating from it gives the shipped lists back.
- Not done: an in-game trigger. The recomp never reads the lists (Develop
  hides `DATA\FULL.ID`); calling the lifted generators from a Develop key
  would need phase 4's bank at `0x659044` and the host `fopen` resolving a
  relative name to the folder's root, neither checked.

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
| A save names a project missing from the bank, or is foreign or the wrong size | the save guard refuses it before anything is overwritten (B1, Saves; phase 7) |
| Give all items renames an item the player already holds (`VITESSE` → `vitesse`), and the case-sensitive held test then closes the exits of Projects 99 and 100 | the host restores held entries after the sequence (phase 7) |
| A demo replay leaves input dead after a caption sequence (retail bug) | the host saves and restores the input device mode around playback (phase 7) |
| The first Develop launch is interrupted or runs out of space | the launcher writes the "initialized" marker only after a complete copy (655 MB merged, measured 2026-10-04) and resumes a partial one, skipping files present at full size (phase M) |
| An editor draw stops the direct renderer | Develop ran on the software renderer until phase D; the direct smoke at the end of each phase found such draws early; since phase D `tests/recomp/test_editor_direct.py` matches every page and tool against the software renderer |
| Capture under the direct renderer writes black | phase D: the GPU frame is read back into guest memory before `SaveImage_` reads it (`dev_overlay.c`, `wd_render_materialize`) |
| `FULL.ID` or disc 2's saves reach the folder | excluded from the copy and `FULL.ID` hidden by the host (phase M) |
| Create on a full bank clears the live level | the phase 4 Create refuses when no slot is free |

## B4. Verification plan

| Check | Phase | How |
|---|---|---|
| Play is clean | M | keypad 1–9 and the developer keys change no guest memory; no `0x34`–`0x38` event posted |
| Developer folder set up once, then used | M | first Develop launch creates and marks the folder, without `FULL.ID` or disc 2's saves; second launch: no `open` or `disc` event on an image, no `cd` event, no "MCI Error" box, a disc-2 level plays (one-frame swap prompt accepted) |
| Play edits | M | plays the folder's `DREAMS.DAT` with `cd` events; loads a Develop save |
| Save guard | 7 | `bank_patch.py` bank with a renamed project: its save refused in the main menu and in game (state unchanged, no `.DSN` open), the other saves load; a zero-filled 10,364-byte save refused |
| DOS keys | 2 | each developer key acts once; consumed keys change no game action word; `!` leaves link records unchanged; CP850 characters reach `sceneKeyboard_` |
| Developer tools | 7 | per tool, the acceptance column of phase 7, through the control channel |
| Software reference screenshots | 1–7 | each editor page and tool, captured under the software renderer as it is built |
| Direct smoke | each phase (not blocking) | `out/research/p0check/` scripts: panel rows, `ShowLink_` lines, no `[direct] FATAL` |
| Direct port | D | every reference screenshot matched under the direct renderer; a capture with non-black pixels |
| Panel and exit node | 0 | screenshot + `0x4a4780` after a scripted click |
| Root rows and a captured position | 1 | control channel: editor flag, open bits, key `0`, mouse script, read `0x65fb04+0xb4` (`tests/recomp/test_editor_menu.py`) |
| Binding table proof | 1 | test reads `bindings.tsv` and checks each retail reader address against the lifted code (`tests/recomp/test_editor_tree.py`) |
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
- Answered 2026-10-04: Play edits on a disc-2 level switches its CD audio
  to disc 2's tracks (phase M, `tests/recomp/test_developer_disc2.py`).
- Where the main menu can show the save guard's message (phase 7).
- Whether `VITESSE` or `INVIVIB` can be used up, which would strand a
  returning player even in retail.
- Decided in phase D (2026-10-04): TGA capture under the direct renderer
  reads the GPU frame back into guest memory and keeps Cryo's own
  `SaveImage_` (no replacement); the render classes stay software-only
  (under the direct renderer the keys only say so).

## B6. Work items

1. Done 2026-10-04: the editor's addresses and the engine globals the
   July menu binds are pinned (A3).
2. Done 2026-10-04: bindings table with proofs (phase 1, step 2).
3. Done 2026-10-04: tree extraction and install (phase 1).
4. Done 2026-10-04: DOS-character keys, the consumed-key rules and the
   developer-key bindings (phase 2), with the key list in the launcher's
   Develop tab, the console and the README; the phase 7 keys are routed
   to logged stubs (F10's bank write was built with phase 5).
5. Done 2026-10-04: picker replacements (phase 3); the replacement table was
   already shared by render and host entries.
6. Done 2026-10-04: host bank, `_LoadSaveSceneSPtr` binding, recompression
   (phase 4).
7. Done 2026-10-04: export and the byte-identity test (phase 5).
8. Fix `src/dreams/formats/project.py` (CD track from `+0x1f8`, strength
   read as radius, liveness by name, last BOX point dropped), which the
   tests of phases 4–6 will use.
9. Register the July names of the retail editor functions in
   `re/names/WINDREAM.EXE.tsv` (report C's mapping is the second source).
10. Done 2026-10-04: release packaging: `resources\editor-tree.tsv` in
    `build.py` and `release.py` (fails without it); Develop code always
    compiled in and independent of `WD_DEVTOOLS`, which `release.py` keeps
    banning as today.
11. Phase M: the launcher's three modes, Play's gating of every host
    binding, the developer-folder copy and merge (without `FULL.ID` and
    disc 2's saves) with its initialized check, serving the folder as the
    only root on desktop builds (the browser build's layout), the
    `CD_OpenAudio` replacement in Develop, CD audio from the images in Play
    edits, `run.py --mode`.
12. Phase 7: the developer tools of spec 005's inventory, each with its
    key (phase 2), the console window, the Save page on keypad 0, the save
    guard, and TGA capture, on the software renderer.
13. Done 2026-10-04: phase D, the direct renderer port against the
    software renderer (`tests/recomp/test_editor_direct.py`); the launcher
    and `run.py` stop fixing Develop's renderer.

## File map

| Path | Content |
|---|---|
| `docs/research/cryo-editor.md` | overview of Part A |
| `docs/research/file-formats.md` | the record and its editor labels |
| `docs/specs/005-debug-tools/spec.md` | phase 0, the other debug features and the developer-tool inventory |
| `docs/research/install-and-discs.md` | disc identity, `FULL.ID`, the purge |
| `recomp/windream/host/sdl/user.c` | keypad toggles, `mouse_post`, `wd_editor_frame` |
| `recomp/windream/editor/` | `editor_tree.py` (extract, resolve, the resource), `bindings.tsv` (phase 1) |
| `recomp/windream/host/sdl/editor_menu.c` | the menu install, sprite set 3 and the DOS slider blit, the editor's font slot (phase 1) |
| `tests/recomp/test_editor_tree.py`, `test_editor_menu.py` | phase 1, offline and live |
| `recomp/windream/host/sdl/files.c` | file serving; the browser build's single-tree layout phase M reuses |
| `recomp/windream/host/sdl/winmm.c` | the MCI `cdaudio` device |
| `recomp/launcher/` | `dreams.ini`, the mode option, `play_blocker` |
| `recomp/windream/lift/lift.py` | `CALLS` (`0x41743a` → `wd_editor_frame`) |
| `recomp/windream/lift/replacements.py`, `host/render/render_boundary.cpp` | the replacement table phase 3 generalizes |
| `recomp/windream/host/core/runtime.c` | `shim_alloc`, `guest_call_regs`, `WD_POKE` |
| `out/research/editor/` | reports A–E, decompilations, `fn.py`, scratch scripts (local) |
| `out/research/wip_pcj/` | July trees, symbols, earlier extracts (local) |
| `out/research/devtools-audit/` | developer-tool audit A–D: inventories, dead functions and flags, triggers (local) |
