# Boot sequence — from `WINDREAM.EXE` start to the first map

What the game does between double-click and *Ile d'Angkor*, named from the
decompiled `WINDREAM.EXE` (imagebase `0x400000`; all addresses below are VAs).
The observed player-visible sequence — intro movie, short animation, menu,
new game, one more animation, first map — maps 1:1 onto code that is now
identified. **[verified]** throughout unless tagged otherwise; decompilations
were read from the Ghidra project, string references from a byte-level
file-offset→RVA scan (`out/boot/` scratch work).

```
entry 0x465538 (Watcom startup)
  └─ 0x48646d  main: heap/stack setup
       └─ 0x41745e  Game_Run ── the controller
            ├─ 0x4156bf  init assets   (fonts, icons, DREAMS.DAT, DIALOG.DRD)
            ├─ 0x415f00  init systems  (master frame handler, sound, CD audio)
            └─ loop:
                 ├─ 0x436481  BootScreen: intro → generic → menu → new-game video
                 └─ frame loop → master handler 0x416d45
                      └─ dispatcher 0x417078 → 0x4240ba  Transition_Tick
                           └─ 0x41f9db  Scene_SpawnProjectEntities (loads the map)
```

## The event sequence, as the player sees it

| # | On screen | What the code does | Asset |
|---|---|---|---|
| 1 | **Intro movie** (~3 min 05 s, skippable) | `BootScreen 0x436481` builds `data\hnm\` + `intro.hnm`, sends event `0x17` (play video), pumps frames until video end or skip keys | `DATA\HNM\INTRO.HNM` — disc 1, 2781 frames 640×304 @15 fps |
| 2 | **Short warp animation** (the "loading animation", ~7 s) | same function, second event `0x17` with `generic.hnm`; then loads the menu corner-marker sprites (`0x435c2b`) and enters the menu loop | `DATA\HNM\GENERIC.HNM` — 101 frames ≈ 6.7 s, light-speed tunnel; runs under/behind the menu |
| 3 | **Main menu** (2×2 grid) | menu tick `0x435fae` inside the frame loop; labels from pointer table at `0x4a2ed5`; background `data\tga\menu.tga`, icons `data\icone\icones.bf`, music = CD audio track 13 (`0x426fd8 == 0xd` fade logic) | see menu section below |
| 4 | **New game** | on confirm (item 0) the tick copies the current project's video name out of its `DREAMS.DAT` record (`+0x3c`), exits the menu; `BootScreen` resets entities, arms a **15 s** in-engine transition timer (`_DAT_005e5480 = 15.0`), then plays the project video if the record names one | Project 0 record names `ETE_E~1.HNM` — **absent from both discs**, so this play silently fails and is skipped (see "The ETE mystery") |
| 5 | **In-engine transition** (~15 s) | `Transition_Tick 0x4240ba` state≠loading branch counts the 15 s down: camera drifts (noise-driven deltas on the view globals), front/back buffers fade to white or black | no asset — rendered |
| 6 | **Talking-head animation** (~21 s) | `Transition_Tick` state=loading: current project is `Project0` and one-shot latch `DAT_0049da28` is set → stop music (event `0x1f`), play video, then immediately call the map loader — the head talks while/just before the level streams in | `DATA\HNM\TETE_E~1.HNM` — 313 frames; an elderly bearded man's head (the elder's briefing) |
| 7 | **Loading / CD-swap screen** | `Scene_SpawnProjectEntities 0x41f9db` → `0x427d64`: if the needed disc is not mounted shows *Please change to CD no %d*, else draws *Please wait while loading ...* and loads the level manifest | `LISTL1.TXT` for Project 0 |
| 8 | **First map: Ile d'Angkor** | manifest lines stream `.DSN`/`.DAN` files | `H18ANGKR.DSN` + `F84/F07BLEU×4/CH0/MINE.DAN`, on top of the always-resident `LISTL0.TXT` set |

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

### Main menu (`BootScreen` menu loop, tick `0x435fae`)

* Selection state `DAT_004a2ef9` 0–3; label pointer table at `0x4a2ed5`:
  **`NEW GAME` / `LOAD A GAME` / `OPTIONS` / `QUIT`** (`0x4c51b0…`). The
  uppercase spellings are unique to this screen.
* Navigation is a 2×2 grid (up/down cycles 0→2→3→1), highlighted by the four
  corner-marker sprites loaded by `0x435c2b` — `UpLfNA/UpRgNA/DnLfNA/DnRgNA`
  (inactive) and `UpLf/UpRg/DnLf/DnRg` (active) — placed on a 2×2 grid with
  per-resolution scaling.
* Background: the code tries to load `data\tga\menu.tga` (helper in the
  `0x43a0e2` region) — but **no such file exists on either disc** (`DATA\TGA\`
  is empty on disc 1, holds only reference JPEGs on disc 2), so the menu
  background is effectively the looping `GENERIC.HNM` warp video plus the
  sprite overlays. The `data\icone\icones.bf` icon bank (bound at init,
  `0x4341eb`) **is fully decoded** — corner markers, joypad caps, item icons
  and the golden menu titles; see the menu `TABLE` section in
  [file-formats.md](file-formats.md).
* Confirm on item 0 (new game) sets the exit flag with load/quit flags cleared;
  item 1 opens the save browser (`0x437aa2`), item 2 the options screen
  (`0x4314ec`), item 3 quits (`DAT_004a4780 = 1` → controller loop exits).
* Menu sounds travel through `0x446654` events 9 (move) / 10 (confirm).
* A ~3×40-tick idle timeout (`0x4363c8`) also exits the wait; combined with the
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

`0x4337c0` → `0x432b45` renders the **lowercase** `Load` / `Options` / `Quit`
items (`0x4c519e…`) — the ESC menu during play, not the boot menu.

## The ETE mystery — the new-game video that isn't there

The play-after-new-game video is **data-driven**: each project record in
`DREAMS.DAT` carries its intro-video filename, and the in-memory parsed record
exposes it at `+0x3c`. `BootScreen` plays `data\hnm\<that name>` only if the
field is non-empty.

* Project 0's record (file offset `0x400 + offs[0]`, chunk tag `0x04`) names
  **`ETE_E~1.HNM`**. No file by that name exists on either disc — only
  `TETE_E~1.HNM`. So on this release the per-project play fails and is
  skipped (event `0x17` returns nonzero → event `0x18` stop → continue).
* What the player actually sees after New Game is therefore **not** the
  project video but the hardcoded one in `Transition_Tick 0x4240ba`: when the
  project being entered is `Project0` and the one-shot latch
  `DAT_0049da28` is set, it plays **`data\hnm\tete_e~1.hnm`** directly
  (string at `0x4c493e`), then calls the map loader.
* Whether `ETE_E~1.HNM` is a stale name from a master that had both files, or
  the 8.3 mangling of a longer name that never shipped, is open. **[unverified]**

## Level entry, end to end

1. `Transition_Tick 0x4240ba`, state = loading, project = `Project0`, latch on:
   stop music → play `TETE_E~1.HNM` → `Scene_SpawnProjectEntities 0x41f9db`.
2. `0x427d64` CD check (`DATA\1CD.ID` / `DATA\2CD.ID` / `DATA\FULL.ID`
   sentinel files; *Please change to CD no %d*), then the loading screen
   (*Please wait while loading ...*, `0x4c4c1c`).
3. Manifest `%sListL%d.txt` (string `0x4c4c3a`) is parsed line by line;
   `LISTL0.TXT` is the always-resident universe (`H03PAQUE.DSN` night scene,
   `CH0.DAN`, `HOLO.DAN`, `XH_.DAN`, `MHE.DAN`, `H03AN001.HNM`), and
   `LISTL1.TXT` opens with `H18ANGKR.DSN` — Project 0, *Ile d'Angkor*, plus
   `F84.DAN`, four `F07BLEU.DAN`, `CH0.DAN`, `MINE.DAN` and the rest of the
   disc-1 level set (44 `.DSN` lines, see [level-map.md](level-map.md)).

## Event dispatcher (`0x4a306` → table `CTRL_Dispatcher`)

Screens and subsystems talk through a numbered-event dispatcher at `0x4a306`
(the *unknown message type in CTRL_Dispatcher* error string belongs to it).
Events identified during this trace:

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

## Function map (boot path)

| Address | Role |
|---|---|
| `0x465538` | `entry` (Watcom startup) |
| `0x48646d` | `main` — heap/stack init, calls the controller |
| `0x41745e` | `Game_Run` — init, boot/menu/game loop, shutdown |
| `0x4156bf` | asset init: heaps, DREAMS.DAT parse, fonts `hi640/480/320.spr`, cursor `sour_alp`, `icones.bf`, `DIALOG.DRD` check |
| `0x415f00` | system init: master frame handler `0x416d45`, sound events |
| `0x436481` | `BootScreen` — intro video, generic video, menu wait, new-game handling |
| `0x433c4e`/`0x433cb9` | install/restore the video-period handler around the intro |
| `0x435c2b` | load + place menu corner-marker sprites |
| `0x435fae` | `MainMenu_Tick` — input, item cycling, confirm dispatch, save/options submenus |
| `0x4363c8` | idle-timeout tick (3×40) |
| `0x43a64c` | frame pump — Windows messages, event queues, standing handler call |
| `0x43a0e2` (region) | `menu.tga` loader helper |
| `0x4341eb` | UI/icon init (`icones.bf`, icon-name bindings) |
| `0x416d45` | master per-frame handler (installed at init) |
| `0x417078` | in-handler state dispatcher; always calls `0x4240ba` |
| `0x4240ba` | `Transition_Tick` — 15 s in-engine transition, Project0 head video, map load kick |
| `0x41f9db` | `Scene_SpawnProjectEntities` — map + entity load |
| `0x427d64` | CD-swap / loading screen |
| `0x43a306` | event dispatcher (`CTRL_Dispatcher`) |

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
