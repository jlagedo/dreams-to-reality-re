# Boot sequence — from `WINDREAM.EXE` start to the first map

> **Function names verified (2026-09-26).** Every `WINDREAM.EXE` function this page names is in [`re/names/WINDREAM.EXE.tsv`](../re/names/WINDREAM.EXE.tsv) with two independent sources (these docs and a blind review of the decompilation) and facts checked against the binary by `tools/check_names.py`.

What the game does between double-click and *Ile d'Angkor*, named from the
decompiled `WINDREAM.EXE` (imagebase `0x400000`; all addresses below are VAs).
The observed player-visible sequence — intro movie, short animation, menu,
new game, one more animation, first map — maps 1:1 onto code that is now
identified. **[verified]** throughout unless tagged otherwise; decompilations
were read from the Ghidra project, string references from a byte-level
file-offset→RVA scan (`out/boot/` scratch work).

```
entry 0x465538 (Watcom startup)
  └─ 0x48646d  __NTMain (Watcom runtime): heap/stack setup
       └─ 0x41745e  WinMain ── the controller
            ├─ 0x4156bf  init assets   (fonts, icons, DREAMS.DAT, DIALOG.DRD)
            ├─ 0x415f00  init systems  (master frame handler, sound, CD audio)
            └─ loop:
                 ├─ 0x436481  BOOT_Run: intro → generic → menu → new-game video
                 └─ frame loop → master handler 0x416d45  GAME_TickFrame
                      └─ dispatcher 0x417078 → 0x4240ba  GAME_Tick
                           └─ 0x41f9db  SCENE_LoadLevel (loads the map)
```

## The event sequence, as the player sees it

| # | On screen | What the code does | Asset |
|---|---|---|---|
| 1 | **Intro movie** (~3 min 05 s, skippable) | `BOOT_Run 0x436481` builds `data\hnm\` + `intro.hnm`, sends event `0x17` (play video), pumps frames until video end or skip keys | `DATA\HNM\INTRO.HNM` — disc 1, 2781 frames 640×304 @15 fps |
| 2 | **Short warp animation** (the "loading animation", ~7 s) | same function, second event `0x17` with `generic.hnm`; then loads the menu corner-marker sprites (`MENU_PlaceCornerIcons` (`0x435c2b`)) and enters the menu loop | `DATA\HNM\GENERIC.HNM` — 101 frames ≈ 6.7 s, light-speed tunnel; runs under/behind the menu |
| 3 | **Main menu** (2×2 grid) | menu tick `MENU_Tick` (`0x435fae`) inside the frame loop; labels from pointer table at `0x4a2ed5`; background `data\tga\menu.tga`, icons `data\icone\icones.bf`, music = CD audio track 13 (`0x426fd8 == 0xd` fade logic) | see menu section below |
| 4 | **New game** | on confirm (item 0) the tick copies the current project's video name out of its `DREAMS.DAT` record (`+0x3c`), exits the menu; `BOOT_Run` resets entities, arms a **15 s** in-engine transition timer (`_DAT_005e5480 = 15.0`), then plays the project video if the record names one | Project 0 record names `ETE_E~1.HNM` — **absent from both discs**, so this play silently fails and is skipped (see "The ETE mystery") |
| 5 | **In-engine transition** (~15 s) | `GAME_Tick 0x4240ba`, the per-frame gameplay tick, counts the 15 s down in its state≠loading branch: camera drifts (noise-driven deltas on the view globals), front/back buffers fade to white or black | no asset — rendered |
| 6 | **Talking-head animation** (~21 s) | `GAME_Tick` state=loading: current project is `Project0` and one-shot latch `DAT_0049da28` is set → stop music (event `0x1f`), play video, then immediately call the map loader — the head talks while/just before the level streams in | `DATA\HNM\TETE_E~1.HNM` — 313 frames; an elderly bearded man's head (the elder's briefing) |
| 7 | **Loading / CD-swap screen** | `SCENE_LoadLevel 0x41f9db` → `CD_PrepareLevel` (`0x427d64`): if the needed disc is not mounted shows *Please change to CD no %d*; with hard-disk caching it draws *Please wait while loading ...* and copies the files the level manifest lists to `X:\CRYO\DREAMS\` | `LISTL1.TXT` for Project 0 |
| 8 | **First map: Ile d'Angkor** | the level's `.DSN`/`.DAN` files load (the set the manifest lists) | `H18ANGKR.DSN` + `F84/F07BLEU×4/CH0/MINE.DAN`, on top of the always-resident `LISTL0.TXT` set |

## The three boot videos, identified

All three decode with `na_game_tool -ifmt hnm6` **[verified]**; first/mid
frames inspected visually.

| File | Frames | Length | Content |
|---|--:|---:|---|
| `INTRO.HNM` (disc 1) | 2781 | ~3:05 | the real intro — cosmic/dream imagery, starfield endings |
| `GENERIC.HNM` (both discs) | 101 | ~6.7 s | light-speed/warp tunnel radiating from a bright core |
| `TETE_E~1.HNM` (disc 1) | 313 | ~21 s | *tête* — close-up of a white-bearded elder talking |

`GENERIC.HNM` is byte-identical to disc 2's `INTRO.HNM` (already documented in
[hnm-video.md](hnm-video.md)) — disc 2 simply ships the generic warp clip
under the intro's name.

## Menus

### Main menu (`BOOT_Run` menu loop, tick `MENU_Tick` (`0x435fae`))

* Selection state `DAT_004a2ef9` 0–3; label pointer table at `0x4a2ed5`:
  **`NEW GAME` / `LOAD A GAME` / `OPTIONS` / `QUIT`** (`0x4c51b0…`). The
  uppercase spellings are unique to this screen.
* Navigation is a 2×2 grid (up/down cycles 0→2→3→1), highlighted by the four
  corner-marker sprites loaded by `MENU_PlaceCornerIcons` (`0x435c2b`) — `UpLfNA/UpRgNA/DnLfNA/DnRgNA`
  (inactive) and `UpLf/UpRg/DnLf/DnRg` (active) — placed on a 2×2 grid with
  per-resolution scaling.
* Background: the code tries to load `data\tga\menu.tga` (helper in the
  `0x43a0e2` region) — but **no such file exists on either disc** (`DATA\TGA\`
  is empty on disc 1, holds only reference JPEGs on disc 2), so the menu
  background is effectively the looping `GENERIC.HNM` warp video plus the
  sprite overlays. The `data\icone\icones.bf` icon bank (bound at init,
  `UI_InitIcons` (`0x4341eb`)) supplies the corner markers and other UI sprites. Disc 2 also
  contains gold/red title images in `TITRES.SPR`; this menu's labels come from
  the executable string table and are rendered by font routine `TEXT_Print` (`0x426073`).
  `TITRES.SPR` is omitted from the five-bank loader list, and no use elsewhere
  has been found. See the menu `TABLE` section in
  [file-formats.md](file-formats.md).
* Confirm on item 0 (new game) sets the exit flag with load/quit flags cleared;
  item 1 opens the save browser (`MENU_InitSaveSlotSelect` (`0x437aa2`) picks the
  default slot — most recent for load, most recent unprotected for save — and
  loads its thumbnail; `MENU_DrawSaveSlots` (`0x437c01`) draws the list); item 2
  sets the options submenu state `0x4a2eed = 1` and calls
  `MENU_UpdateResolutionIndex` (`0x4314ec`), which only maps the current
  width/height/scale to resolution index 0–5 (`0x4a2f01`); item 3 quits
  (`DAT_004a4780 = 1` → controller loop exits).
* Menu sounds are sound ids 9 (move) / 10 (confirm), sent as a 3-field play
  request `{1, id, 0x20}` to `DSOUND_PlaySound` (`0x446654`).
* The per-frame boot/menu step `BOOT_TickFrame` (`0x4363c8`) redraws the menu
  (`MENU_Draw` (`0x435ea0`)) when dirty flag `0x4a2f31` is set, swaps, and
  counts steps of `0x28` ticks of the 200 Hz MGM timer (200 ms each); after 3
  steps it sets exit flag `0x626f00`, which also exits the wait; combined with the
  second `generic.hnm` reference living in the replay/record subsystem
  (`data\hnm\generic.hnm` @ `0xe9e4`, beside `data\replay.bin` refs), this is
  consistent with an attract/demo path — **[unverified]**.

### Save/load browser (from the main menu)

Slot rendering at `0x437c–0x4384k`: `"%d.  %s"`, protected slots as
`"*%d. %s"`, empty slots as `"%d. Empty"`, matching the `[SYSTEM]` strings in
`DREAMS.INI` ([game-content.md](game-content.md)). Data files:
`data\game\game.dat` (slot index), `game%d.dat` (state), `game%d.ico`
(thumbnail).

### In-game pause menu (different code, for contrast)

`MENU_RunGameMenu` (`0x4337c0`) → `MENU_DrawGameMenu` (`0x432b45`) renders the **lowercase** `Load` / `Options` / `Quit`
items (`0x4c519e…`) — the ESC overlay during play, not the boot menu. Its
spell/object grids and four option toggles are traced in
[sprites-ui-dialog.md](sprites-ui-dialog.md).

## The ETE mystery — the new-game video that isn't there

The play-after-new-game video is **data-driven**: each project record in
`DREAMS.DAT` carries its intro-video filename, and the in-memory parsed record
exposes it at `+0x3c`. `BOOT_Run` plays `data\hnm\<that name>` only if the
field is non-empty.

* Project 0's record (file offset `0x400 + offs[0]`, chunk tag `0x04`) names
  **`ETE_E~1.HNM`**. No file by that name exists on either disc — only
  `TETE_E~1.HNM`. So on this release the per-project play fails and is
  skipped (event `0x17` returns nonzero → event `0x18` stop → continue).
* What the player actually sees after New Game is therefore **not** the
  project video but the hardcoded one in `GAME_Tick 0x4240ba`: when the
  project being entered is `Project0` and the one-shot latch
  `DAT_0049da28` is set, it plays **`data\hnm\tete_e~1.hnm`** directly
  (string at `0x4c493e`), then calls the map loader.
* Whether `ETE_E~1.HNM` is a stale name from a master that had both files, or
  the 8.3 mangling of a longer name that never shipped, is open. **[unverified]**

## Level entry, end to end

1. `GAME_Tick 0x4240ba`, state = loading, project = `Project0`, latch on:
   stop music → play `TETE_E~1.HNM` → `SCENE_LoadLevel 0x41f9db`.
2. `CD_PrepareLevel` (`0x427d64`) CD check (`DATA\1CD.ID` / `DATA\2CD.ID` / `DATA\FULL.ID`
   sentinel files; *Please change to CD no %d* from `CD_PromptSwap` (`0x428626`)),
   then, with hard-disk caching, the loading screen (*Please wait while
   loading ...*, `0x4c4c1c`).
3. Manifest `%sListL%d.txt` (string `0x4c4c3a`) is read line by line and
   `CD_CopyFileList` (`0x428356`) copies each listed file to `X:\CRYO\DREAMS\`;
   `LISTL0.TXT` is the always-resident universe (`H03PAQUE.DSN` night scene,
   `CH0.DAN`, `HOLO.DAN`, `XH_.DAN`, `MHE.DAN`, `H03AN001.HNM`), and
   `LISTL1.TXT` opens with `H18ANGKR.DSN` — Project 0, *Ile d'Angkor*, plus
   `F84.DAN`, four `F07BLEU.DAN`, `CH0.DAN`, `MINE.DAN` and the rest of the
   disc-1 level set (44 `.DSN` lines, see [level-map.md](level-map.md)).

## Message dispatcher (`0x43a306`, `MGM_SendMessage`)

Screens and subsystems talk through a numbered-message dispatcher at
`MGM_SendMessage` (`0x43a306`). Its own error string names it: *unknown message type in
MGM_SendMessage* **[verified]**. An earlier revision of this page called it
`CTRL_Dispatcher` (and gave `0x4a306`); that name belongs to a different
function, `CTRL_Dispatcher` (`0x40e75c`), whose error string is referenced at `0x40ea42`. The frame pump
below is `MGM_DispatchMessages` (`0x43a64c`) by the same evidence. The 3dfx build has the
same pair at `0x3c2c8`/`0x3c658`. Messages identified during this trace:

| Event | Meaning |
|---:|---|
| `0x11` | get time |
| `0x12` | init sound (8 voices, bank) |
| `0x17` | **open/play video** (`.HNM`/`.UBB` via `0x485dc`) |
| `0x18` | stop video |
| `0x19` | stop sound |
| `0x1c` | query (sound/CD?) — checked at init |
| `0x1f` | stop music |
| `0x26` | install master frame handler |
| `0x27`/`0x28`/`0x29` | install / query / restore per-frame handler (used to wrap the intro video handler `0x4339cf` around the standing one) |
| `0x2f` | (video subsystem internal) |

The complete command set of `MGM_SendMessage` (`0x43a306`), from its switch (2026-09-26,
**[verified]**; any other command is a fatal error). Most commands set or
clear a bit in the subsystem flag word `0x626f80` and return a value in
`0x4a2f88`:

| Cmd | Effect | `0x626f80` |
|---:|---|---|
| 4 / 5 | keyboard input on / off (on needs an input device, `0x626f70`) | bit 1 |
| 6, 9 | read an entry of the two-device table at `0x630864` (`0x440e39`, `0x440e97`) | |
| 7 / 8 | POV joystick on / off (`JOY_EnablePov`/`JOY_DisablePov`) | `0x800` |
| 10 / 11 | joystick on / off (`JOY_Enable`/`JOY_Disable`) | `0x1000` |
| 12 | timer rates (`SYS_SetTimerRates`; the game passes 200 and 15 Hz) | |
| 13 | `0x424ba2` | |
| 14 / 15 | flag on / off (role open) | 4 |
| 16 | flag on (role open) | 8 |
| `0x11` | update the timer, return the 200 Hz counter | |
| `0x12` | sound on: load `data\sound\fsb.dat` (`FSB_Load`) | `0x100` |
| `0x13` | sound off (when on): `DSOUND_Shutdown` (`0x4464d4`) | |
| `0x14` / `0x15` | flag on / off (role open) | `0x10` |
| `0x16` | set video mode (w, h); `VID_SetResolution` retries until it succeeds | |
| `0x17` | open a video file, remember its kind | clears `0x20`/`0x40` |
| `0x18` | start the opened video | `0x20` (kind 1) or `0x40` (kind 2) |
| `0x19` | stop video and sound | clears `0x20`/`0x40` |
| `0x1c` / `0x1d` | CD audio open / close (MCI `cdaudio`; `CD_OpenAudio` (`0x4042f1`) / `CD_CloseAudio` (`0x404368`)) | `0x200` |
| `0x1e` / `0x1f` | music on / stop; the music is the Red Book audio, and `0x1f` reaches the CD stop (`0x4045a5`) | `0x400` |
| `0x20` / `0x21` | CD tray open / close (`CD_OpenDoor` (`0x4043c2`) / `CD_CloseDoor` (`0x40441a`)) | |
| `0x22` / `0x23` | CD audio pause / resume (`CD_PauseAudio` (`0x4045fa`) / `CD_ResumeAudio` (`0x40464f`)) | |
| `0x25` | returns `0x404822`'s value (hidden in the decompilation) | |
| `0x26` | install the master frame handler | |
| `0x27` | install a per-frame handler (`0x626f74`) | |
| `0x28` / `0x29` / `0x2a` | return input-device presence, the handler, the flag word | |

**Input events** (queue `0x5e54b4`, posted by `INPUT_PostEvents` (`0x42493b`) each frame,
consumed by `CTRL_Dispatcher` (`0x40e75c`) in the boot, menu and caption loops as
`event − 0x33`):

| Event | Meaning |
|---:|---|
| `0x33` | key press (virtual-key code) |
| `0x34` | joystick axes |
| `0x35` / `0x36` | button 1 down / up |
| `0x37` / `0x38` | button 2 down / up |
| `0x39` | POV joystick: axes and five buttons |
| `0x3a` | joystick: axes and buttons |
| `0x3b` | **15 Hz tick**: the second timer (period 1000/15 ms) elapsed; at most one per frame |
| `0x3d` | `QueryPerformanceCounter` value |
| `0x3e`, `0x3f` | menu-internal (`0x3f` plays `data\hnm\generic.hnm`) |

## Function map (boot path)

| Address | Role |
|---|---|
| `0x465538` | `entry` (Watcom startup) |
| `0x48646d` | `__NTMain` (Watcom runtime, not a game main) — heap/stack init, calls `WinMain` |
| `0x41745e` | `WinMain` — init, boot/menu/game loop, shutdown |
| `GAME_Init` (`0x4156bf`) | one-time asset init: CD drive and paths, heaps, the `0x57800`-byte stream (`STRM_Create` (`0x415201`)), DREAMS.DAT parse, fonts `hi640/480/320.spr`, cursor `sour_alp`, `icones.bf`, `DIALOG.DRD` check |
| `GAME_InitSubsystems` (`0x415f00`) | system init: MGM message queue, `INPUT_Init` (`0x43a169`), 200 Hz timer, master frame handler `0x416d45`, sound, audio CD, optional intro |
| `0x436481` | `BOOT_Run` — intro video, generic video, menu wait, new-game handling |
| `BOOT_PushEventHandler` (`0x433c4e`)/`BOOT_PopEventHandler` (`0x433cb9`) | install/restore the video-period handler around the intro |
| `MENU_PlaceCornerIcons` (`0x435c2b`) | load + place menu corner-marker sprites |
| `0x435fae` | `MENU_Tick` — input, item cycling, confirm dispatch, save/options submenus |
| `BOOT_TickFrame` (`0x4363c8`) | per-frame boot/menu step: redraw when dirty, swap, exit after 3 × 200 ms steps |
| `0x43a64c` | `MGM_DispatchMessages` — frame pump: Windows messages, event queues, standing handler call |
| `0x43a0e2` (region) | `menu.tga` loader helper |
| `UI_InitIcons` (`0x4341eb`) | UI/icon init (`icones.bf`, icon-name bindings) |
| `GAME_TickFrame` (`0x416d45`) | master per-frame handler (installed at init): input events, demo record/play, `GAME_Tick`, HUD, key help (`UI_DrawKeyHelp` `0x416096`), video frames (`VID_DecodeFrame` `0x408816`), next frame's Δt |
| `0x417078` | in-handler state dispatcher; always calls `GAME_Tick` (`0x4240ba`) |
| `0x4240ba` | `GAME_Tick` — per-frame gameplay tick (entity ticks, AI scheduler, scene exits, pause, level-load kick); the 15 s in-engine transition and the Project0 head video are two of its states |
| `0x41f9db` | `SCENE_LoadLevel` — level load when the pending-load flag is set: `CD_PrepareLevel`, DSN textures, lights/fog, OBJET entity spawns |
| `CD_PrepareLevel` (`0x427d64`) | CD-swap prompt; hard-disk cache copy behind the loading screen |
| `0x43a306` | `MGM_SendMessage` — message dispatcher (not `CTRL_Dispatcher`, which is `0x40e75c`) |

## Reproducing

String/xref scan (code→DGROUP immediate references):

```bash
uv run python out/boot/strings.py <disc1>/WINDREAM.EXE   # strings + RVAs
uv run python out/boot/xref2.py   <disc1>/WINDREAM.EXE   # code refs into them
```

Decompilation (from the repo root, per `AGENTS.md`):

```powershell
. .\tools\dreams-env.ps1
& (Join-Path (Get-DreamsSetting DREAMS_GHIDRA_ROOT) 'support\analyzeHeadless.bat') ghidra dreams `
  -process WINDREAM.EXE -noanalysis -readOnly -scriptPath ghidra_scripts `
  -postScript Decompile.java 0041745e 00436481 00435fae 004240ba
```

Video frame checks:

```bash
na_game_tool -ifmt hnm6 <disc>/DATA/HNM/GENERIC.HNM  -ofmt imgseq 'g%04d.ppm'
na_game_tool -ifmt hnm6 <disc>/DATA/HNM/INTRO.HNM    -ofmt imgseq 'i%04d.ppm'
na_game_tool -ifmt hnm6 <disc>/DATA/HNM/TETE_E~1.HNM -ofmt imgseq 't%04d.ppm'
```
