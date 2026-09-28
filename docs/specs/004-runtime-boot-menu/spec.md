# 004 — ODRuntime boot and main menu

Status: **Front end implemented: intro, main menu, Load and Options pages, New Game to the idle Project 0 player with its CD music; save-state restoration and gameplay remain later milestones**
Date: 2026-09-28
Depends on: [001 foundation](../001-project-init/spec.md),
[002 disc access](../002-disc-navigation/spec.md), and the shared media and
renderer work in [003 previews](../003-level-load-preview/spec.md).

## Goal and boundary

Launch ODRuntime from the original two disc images, run the recovered opening
sequence, and present the original keyboard and controller-operable main menu.
This is the first player-facing use of ODShared's retail asset, movie, audio,
font, sprite and rendering paths. The original menu is game UI, not an ImGui
replacement. ImGui remains available only for an optional runtime debug overlay.

The current **New Game** checkpoint reaches the `Project0` scene, its `XH_`
player actor and a snapped follow camera at the recorded spawn: the runtime
runs the short transition, shows the hardcoded elder movie, loads the level,
and ticks the player's idle clip. Movement, collision, AI, triggers and HUD
still need live ports before this becomes playable gameplay.

All four retail main-menu choices work. Load and Options open their recovered
front-end pages. Restoring a saved gameplay session requires the save-state
port (`GAME_LoadGame`) and remains a later runtime deliverable. Until then the
Load page lists the real slots, and confirming a saved one takes retail's
failure branch: status 8, and the page stays open. A host-only notice then
explains that restoration is unavailable. This is a staged boundary, not a
claim that the retail Load action has been ported.

ODRuntime accepts `--cue1` and `--cue2` for reproducible launches and first
reads the per-user `SDL_GetPrefPath("OpenDreams", "ODRuntime")/sources.ini`.
This machine's user file contains the two images already used in ODViewer.
The gitignored `runtime.local.ini` remains a development fallback when the user
file is absent; a tracked `runtime.example.ini` explains the format. An
explicit `--config` selects another file, and CLI paths override its matching
slots. This source setup is outside the game's original menu.
`DREAMS_DISC1/2` name extracted toolkit trees and are not CUE paths.

## Recovered retail sequence

The addresses below are `WINDREAM.EXE` virtual addresses. They were checked
against [the boot trace](../../boot-sequence.md),
[the checked names](../../../re/names/WINDREAM.EXE.tsv), and read-only Ghidra
decompilation on 2026-09-27. The two Windows programs share these addresses;
verify bytes again when a function is actually mapped as a port.

| Stage | Retail behavior | Input, output and 004 consequence |
|---|---|---|
| Process start | `WinMain` `0x41745e` calls `VID_Init`, `GAME_Init`, `GAME_InitSubsystems`, then alternates `BOOT_Run` with the frame pump. | Port the game-side initialization order and explicit failure/teardown. Watcom and Win32 startup are platform replacements. |
| Assets and systems | `GAME_Init` `0x4156bf` establishes paths, stream, `DREAMS.DAT`, font, icon and dialogue state. `GAME_InitSubsystems` `0x415f00` creates queues, runs `INPUT_Init`, installs `GAME_TickFrame`, starts sound, opens CD audio and configures 200/15 Hz timers. | Use the 002/003 source-scoped ports; audit each remaining initialization side effect before claiming coverage. Opening CD audio does not establish a menu music track. |
| Intro | `BOOT_Run` `0x436481` clears both buffers, installs an intro-period handler with `BOOT_PushEventHandler` `0x433c4e`, opens Disc 1 `DATA/HNM/INTRO.HNM` with MGM command `0x17`, then starts it with `0x18` if open succeeded. The 2,781-frame movie runs until end event `0x3f` or a rising Space/Esc action; the handler is then restored. | Preserve the opening order and skip edge. Keep Disc 1 source identity: Disc 2's `INTRO.HNM` contains the generic warp clip. |
| Warp and menu | `BOOT_Run` opens/starts `GENERIC.HNM`, places the `INTERF` corner sprites through `MENU_PlaceCornerIcons` `0x435c2b`, and pumps `MENU_Tick` `0x435fae`. The ordinary `CTRL_Dispatcher` handles movie-end event `0x3f` by reopening the generic clip, so the 101-frame warp repeats. | Draw movie, sprites and text into the runtime drawable with one presentation owner. Keep input responsive through decode and audio output. |
| Main selection | `MENU_Draw` `0x435ea0` draws four active/inactive corners and the label selected from `NEW GAME`, `LOAD A GAME`, `OPTIONS`, `QUIT`. `MENU_Tick` handles navigation, sound IDs 9/10, branch selection and submenu calls. | Port the 2×2 selection and its dirty redraw/short post-confirm hold, not an ImGui button grid. |
| New Game | The menu clears the front-end exit/load flags. `BOOT_Run` clears level state and inventory, then reloads the project bank. `DDAT_Load` sets the loading state `0x661e08 = 1` at `0x448ff7`. `BOOT_Run` then writes 15.0 to the transition timer `0x5e5480` and reads the optional video C string at record `+0x3c`. Project 0 has a NUL there, and inert `ETE_E~1.HNM` bytes start at `+0x3d`, so there is no optional open. The first `GAME_Tick` takes the loading branch at `0x4240d5`. It starts the hardcoded `TETE_E~1.HNM` once, and loads Project 0 on the tick after playback. | Preserve the empty optional movie and the separate hardcoded elder movie. **The armed 15.0 never counts down on New Game.** The countdown branch is skipped in the loading state, and `SCENE_InitLevel` clears the timer (`0x41f45e`). The screen goes black after the menu hold; the level appears as a hard cut. |
| Exit | Quit sets the controller's exit flag; `WinMain` exits the pump and runs subsystem, game and video shutdown. | Close movie, audio, queues, source handles and GPU resources, including a quit during playback. |

The displayed background is the generic movie plus `INTERF` sprites and
`TEXT_Print` glyphs. The retail code tries `data\tga\menu.tga`, but that file is
absent from both inspected discs. `TITRES.SPR` is not the source of these four
labels. The main-menu selection's direction mapping is recovered as
up `0→2→3→1`, right `0→1→3→2`, with the other directions reversing those
cycles. Navigation repeats through `INPUT_UpdateKeyStateRepeat`; confirmation
and cancel are rising actions. `BOOT_TickFrame` counts three 200 ms periods
after leaving the selection loop before finalizing the branch. Its role as an
attract-mode timer is unsupported by this call path.

### Movie sound and text

The three relevant Disc 1 movies all carry `SD` audio. A local outer/inner
chunk scan found:

| Movie | `IX` frames | `SD` chunks | `ST` text chunks |
|---|---:|---:|---:|
| `INTRO.HNM` | 2,781 | 2,733 | 0 |
| `GENERIC.HNM` | 101 | 115 | 0 |
| `TETE_E~1.HNM` | 313 | 327 | 0 |

So **boot-movie subtitles are not a 004 requirement**. The existing viewer
can display generic HNM `ST` text, but no subtitle asset or runtime text event
was found for these three files. `DIALOG.DRD`'s timed voice captions and
portraits are a separate in-game path through `MENJ_PlayVoiceCaptions`; they
belong with dialogue/gameplay. Preserve movie `ST` data in the shared decoder
and design the runtime presenter so later captioned movies can use it. If a
visual transcript is desired for accessibility, specify it as an enhancement
and source its text separately; the retail discs do not provide it here.

## Current implementation and remaining 004 work

| Area | Current implementation | Remaining 004 work |
|---|---|---|
| Runtime shell | ODRuntime has intro, menu, entry-movie and world phases under one SDL/sokol presentation loop. The world scene renders into a full-drawable target. | Retail init, message-pump and shutdown side effects beyond the front end (see the init audit record). |
| Disc and project data | Both CUEs mount; the active Project 0 record loads from Disc 1 and remains alive through the scene handoff. | Visited-level, inventory and save-state resets belong to the gameplay/save milestones. |
| Movies | Intro, looping generic and elder HNM play in the runtime with decoded PCM. Frames follow the played SD sample count, falling back to 15 Hz host time only for a drained audio tail. | Pixel parity against a retail capture. |
| 2D UI | `MENU_PlaceCornerIcons`, `MENU_Tick`, `MENU_Draw`, `MENU_DrawOptionsPage`, `MENU_DrawSaveSlots` and their input handlers are ported. They use the original INTERF sprites, the HI640/HI480 fonts, descriptor anchors and retail half-brightness rows. | Exact RGB555 glyph blend arithmetic, other resolutions and save-mode (in-game) pages. |
| Sound | Menu FSB sound IDs 9/10 play from the mounted source. The options volume toggle applies `DSOUND_SetMasterVolume`, and Project 0 starts its CD track 9 playlist on the first gameplay tick. | Retail five-voice allocation and positional mixing belong to gameplay. |
| Input | SDL keyboard/gamepad/joystick actions operate the intro, the menu and both pages. | Port action-word and CTRL event semantics beyond this front-end slice. |

### Input contract

`INPUT_PostEvents` `0x42493b` calls `INPUT_PollKeyboard` `0x440757`, posts key
press event `0x33`, a 15 Hz event `0x3b`, a performance-counter event `0x3d`,
and changed joystick events `0x39`/`0x3a`. `CTRL_Dispatcher` `0x40e75c`
consumes these 12-byte messages, and `INPUT_UpdateActions` `0x40dce4`
updates eleven action words. On the retail keyboard path the menu directions
are arrow keys, Space confirms, and Esc cancels/quits or skips the intro.
`INPUT_UpdateKeyState` `0x40ec72` sets bit 0 while held and bit 2 on an edge;
`INPUT_UpdateKeyStateRepeat` `0x40ece9` supplies directional repeat at the
200 Hz timer's 40-tick cadence. Enter is handled separately by `MENU_Tick`.

The Windows build also detects WinMM joysticks and can post POV or ordinary
axis/button messages; `J`/`K` select joystick/keyboard during gameplay. The
retail mode-3 joystick action path still reads keyboard Space/Esc, and no
recovered boot call enables joystick mode. A controller-operated front end is
therefore a **documented modern adaptation**. Use SDL3 gamepad bindings for
D-pad/left stick → directions, south/Start → confirm and east/Back → cancel;
also accept a raw SDL joystick with explicit axis/button bindings. Apply
dead-zone and repeat rules once before updating the shared action words.
Keyboard and controller must both work from launch, and focus loss/unplug
must release held actions. Preserve the retail `J`/`K` behavior for gameplay
when that code is reached; do not present it as the prerequisite for using a
modern controller in the boot menu.

### Front-end branches

| Choice | Recovered destination | 004 plan |
|---|---|---|
| New Game | `BOOT_Run` reset/transition, then the one-shot `GAME_Tick` elder-movie and level-load branch. | Preserve the 15-engine-frame effect, the hardcoded elder clip and the later Project 0 load. |
| Load a Game | `GAME_LoadIndex` `0x40f3aa` reads `data\game\game.dat` once at startup. `MENU_InitSaveSlotSelect` `0x437aa2`, `MENU_DrawSaveSlots` `0x437c01` and `MENU_HandleSaveSlotInput` `0x4373a7` run the page. | **Implemented.** Ten HI480 rows sit at x=390 (381 for protected rows), y=100+20i, with only the selected row undimmed. The newest slot is selected. Up/Down move silently, with sound 9 only at a clamp. Esc returns with sound 10, and confirming an empty slot does nothing. The main-menu page never draws the thumbnail; the retail icon load is only a side effect. `GAME_LoadGame` is not ported. |
| Options | `MENU_DrawOptionsPage` `0x430a45`, `MENU_HandleOptionsInput` `0x430cd3`. `MENU_UpdateResolutionIndex` `0x4314ec` is dead code in this build. | **Implemented.** Four HI640 rows at x=390, y=100/130/160/190. Defaults come from the data section: 2D shadow, automatic fight, volume MAX, Cinemascope. Up/Down clamp with sound 10. Enter/Space toggles silently. Esc leaves. Retail never persists these settings. Volume applies `DSOUND_SetMasterVolume(100/40)`. Cinemascope letterboxes the world view to the middle 3/4. The shadow and fight consumers are gameplay code. |
| Quit | Controller flag to `WinMain` cleanup. | Release all runtime state and exit cleanly. |

## Implementation order

1. **Recover the front-end closure.** Decompile and annotate the checked
   `GAME_Init`, `BOOT_Run`, menu/CTRL/input, sound and New Game branches with
   callers, callees, mutable globals, source blocks, failure paths and cleanup.
   Resolve the exact menu layout, options values, save-slot data and the
   15-engine-frame transition before marking any function complete.
2. **Runtime foundation.** Use the implemented CUE-path CLI/config mounting,
   initialize ODShared in retail order, replace the sample UI with
   explicit boot/movie/menu/transition states, and finish the 003 renderer
   destination seam for the full drawable.
   Keep the SDL callback loop nonblocking.
3. **Input and front-end control.** Implement keyboard edges and repeat,
   SDL controller/joystick bindings, the relevant message/action routing, skip
   and menu selection. Prove a controller can operate the menu with no keyboard.
4. **Movies and sound.** Reuse the shared decoder and PCM output for the Disc 1
   intro, generic loop and elder movie. Schedule from the played audio sample
   count, preserve 15 Hz frame boundaries and the selected 3dfx 640×300 at
   row 90 letterbox placement (with Windows 640×304 at row 88 as a comparison), and
   handle finish/skip/error without stale audio or decoded frames.
5. **Retail 2D menu.** Draw `INTERF` corners, font labels, highlight and menu
   sounds over the generic movie. Port Load and Options front-end pages using
   their original data and controls; identify every still-deferred setting or
   save action on screen during staged development.
6. **New Game and shutdown.** Carry the retail reset, empty optional project
   video, 15-engine-frame transition and hardcoded video into the level-loader
   handoff. Exercise Quit, window close, source loss and repeated startup.

For each implemented retail function, follow
[PORT_MAP.md](../../../opendreams/PORT_MAP.md): add its checked-name row with
conservative `coverage`, run `uv run python tools/check_port_map.py`, and
apply the map to both Windows Ghidra programs. Never set `reviewed=yes`
without the project owner's explicit personal review. New SDL, GPU and
source-selection support code is labeled as adaptation rather than a recovered
retail function.

## Acceptance checks

- With the two original images mounted, the runtime chooses Disc 1's real
  intro, displays moving frames with sound, and accepts Space/Esc or the
  documented controller skip. Natural completion reaches the same next state.
- The generic 101-frame clip loops under the original selected label and four
  active/inactive corner sprites. Arrow keys and controller directions follow the recovered
  2×2 graph with repeat. Space/Enter and controller confirm branch once per
  press; Esc/controller cancel works in the applicable screen.
- The intro, generic clip and elder movie show no fabricated subtitle lines.
  Their SD PCM and decoded pixels agree with the shared decoder's independent
  003 oracles; long playback does not drift visibly from audio.
- New Game observes an empty C string at `+0x3c`, holds the menu, clears to
  black and plays `TETE_E~1.HNM` with no countdown. It then loads and presents
  Project 0 from its recorded spawn with a hard cut.
  It does not silently choose Disc 2's `INTRO.HNM` or open ODViewer.
- Load and Options display their recovered front-end pages. A real saved game
  is never reported as restored until the save-state port exists. Quit,
  mid-movie window close, controller unplug and bad/missing source leave a
  usable error or cleanly release the runtime.
- Native Windows/macOS/Linux builds run. Both applications still compile for
  Emscripten; browser disc delivery remains governed by the north star and 002.
  ODViewer media preview regressions remain green after shared-code changes.

## Implementation record

### Initial runtime slice — 2026-09-27

ODRuntime now mounts the configured CUEs, opens Disc 1 `INTRO.HNM`, accepts a
skip action, then loops `GENERIC.HNM` behind the original `INTERF.ALP` corner
sprites and `HI640.SPR` selected label. `--skip-intro` starts at the menu for
development checks. Keyboard arrows/Space/Enter/Esc and SDL gamepad or raw
joystick directions/buttons reach a small action adapter; the four-choice
navigation graph and sound IDs 9/10 are in `shared/port/boot_menu.cpp`.
`MENU_PlaceCornerIcons`, `MENU_Tick` and `MENU_Draw` have partial map rows and
Ghidra tags in both Windows programs. Load and Options currently display an
explicit pending-action notice; Quit exits. Retail `BOOT_Run` and `GAME_Tick`
now have entry-branch ports with partial coverage; the full message pump,
save/options and gameplay scheduler remain.

The menu canvas uses source palettes and pixel coverage. It currently uploads
RGBA sprites through sokol and draws the game quads via the shared ImGui GPU
backend; exact retail blended-edge arithmetic remains open. The selected
label starts at `(50,369)` on the 640×480 canvas, recovered from `MENU_Draw`
instructions `0x435f65–0x435f9d`. The HNM6 movie conversion was corrected from
the retail `HNM6_StoreBlockRGB16` blue term in both Windows and the 3dfx build;
the old NihAV color match was not sufficient evidence. This does not claim
full movie pixel or audio-clock parity.

The menu corner renderer now applies the descriptor anchors used by retail
`SPR_BlitSprite` (`0x401935`): `(draw_x,draw_y) =
(place_x - field_0c,place_y - field_10)`. The two north `INTERF.ALP` halves are
63 pixels high with `field_10 = -1`; the 64-pixel south halves have zero offset.
At 640×480 this places the north pixels on rows 86–148 and the south pixels
on rows 149–212, removing the one-row background gap. The dark horizontal
divider remaining in the sprite is part of the source pixels.

Windows QA can save the composed menu to a PNG using `ODRuntime --skip-intro
--capture <path.png> --frames 10`. The D3D11 backend copies its completed
back buffer from a hidden SDL window, then closes without bringing a window
forward. This replaces desktop-wide screenshot capture during development.

## Evidence and remaining questions

Reproduce the control-flow trace with read-only `Decompile.java` on
`WINDREAM.EXE` at `0041745e 00415f00 00436481 00435fae 004363c8
0040e75c 0042493b 0043a306 0040dce4 004339cf 00408df2` as described in
[AGENTS.md](../../../AGENTS.md). The chunk counts above came from scanning
the 64-byte HNM header followed by 24-bit outer lengths and aligned inner
chunk lengths on the configured Disc 1 files; the same walk is visible in
`src/dreams/formats/video.py::extract_sd_audio` and the ported video walker.
These are file observations, not a retail playthrough recording.

From the repository root in PowerShell, reproduce the movie chunk count with:

```powershell
. .\tools\dreams-env.ps1
$disc = Get-DreamsSetting DREAMS_DISC1
@'
from collections import Counter
from pathlib import Path
from struct import unpack_from
import sys
for name in ('INTRO.HNM', 'GENERIC.HNM', 'TETE_E~1.HNM'):
    data = (Path(sys.argv[1]) / 'DATA' / 'HNM' / name).read_bytes()
    pos, counts = 64, Counter()
    while pos + 4 <= len(data):
        outer = unpack_from('<I', data, pos)[0] & 0xffffff
        if outer < 4 or pos + outer > len(data): break
        inner = pos + 4
        while inner + 8 <= pos + outer:
            size = unpack_from('<I', data, inner)[0] & 0x7ffffff
            if size < 8 or inner + size > pos + outer: break
            counts[data[inner + 4:inner + 6].decode('ascii')] += 1
            inner += (size + 3) & ~3
        pos += outer
    print(name, counts)
'@ | uv run python - $disc
```

### New Game to Project 0 — 2026-09-27

`ODRuntime` now keeps the menu visible for three 200 Hz timer steps after New
Game, stops the generic movie, advances the 15-engine-frame transition, plays
Disc 1 `TETE_E~1.HNM`, and loads `Project0` after the movie ends. The active
optional-video string at `+0x3c` is empty in both disc banks and the retail
installation bank. `ETE_E~1.HNM` begins one byte later and is inert. A hidden
runtime launch with `--start-new-game --capture` completed the full path and
reported 1,959 `H18ANGKR.DSN` scene faces, seven placed actors and no missing
actors. `ODGameEntryTests` checks the state ordering, the offset and assets,
Project 0 load, and the entry camera's default preset and spawn.

The runtime scene uses the shared DSN/DAN render graph and a snapped ground
follow eye/target calculated from Project 0's recorded spawn and heading.
`SCENE_InitLevel` now optionally owns the runtime player initialization:
`ENT_LoadObject` opens `XH_.DAN` and loads `XH_.3DC`, `ANIM_LoadEntitySet`
maps its 49 clip names into 64 action slots, and state 0 starts
`XH_AN000.3DA` at frame 1. The idle pose advances at the recovered 30-frame/s
base. `ODPlayerConeTests` checks 27 model nodes, 504 faces, the 200-frame
state-0 clip, spawn coordinates, and composition with the Project 0 scene.
A hidden muted runtime capture reached and continued rendering that actor.

The first player capture exposed a pelvis-to-thigh strip. The source
`XH_.DAN` uses serialized parent address `1` for four parts under `bassin`;
retail relocates it, while the earlier C++ reader treated it as null. After
fixing that parent link, the host player transform was adjusted to put the
model root at Project 0's spawn, as `ENT_MoveToPlayerSpawn` does. The player
cone test checks those links, bounded face edges through the idle cycle, and
the root's world position. A new hidden capture shows the strip gone and the
feet back at ground level.

This is an idle player and a fixed entry camera. Live player control, camera
updates, collision, AI, triggers, HUD, lighting/fog parity and scene exits
remain. The new player-cone functions and `SCENE_InitLevel` have partial map
rows with owner review pending in both Windows Ghidra programs.

### Front-end pages, audio clock and corrected entry — 2026-09-28

Read-only Ghidra research (reports kept outside the repository under
`out/dev/research-{options,load,boot}.md`) closed the open questions:

- **Menu music: none.** The only MGM 0x1e (`CD_SetPlaylist`) call is at
  `0x42f1bf`, in a `GAME_Tick` gameplay-branch helper at `0x42f166`. It queues
  project `+0x11c` as a one-track playlist once per level load, and
  `CD_TickPlaylist` loops it. The intro, menu and elder movie play only their
  own SD audio and FSB sounds 9/10. Project 0 selects **CD track 9** from Disc
  1; the runtime now starts it on the first world tick.
- **Transition: none on New Game.** See the corrected New Game row above. The
  palette-shift and white/black-fill countdown belongs to level exits and save
  loads.
- **Layout.** Corners sit at (50,85), (114,85), (50,149) and (114,149) before
  anchors. The corners do not animate; highlighting swaps a fixed inactive
  sprite for an active one. The label uses HI640, and the glyphs pass through
  `SPR_BlitSprite`'s descriptor anchors.
- **Options** and the **slot browser** are specified in the branch table.
  Retail never saves option settings, and the page has no apply/cancel step.
- **Movie clock.** Retail paces sound movies with a 14-tick threshold and a
  13-tick advance on the 200 Hz counter, and never reads the DirectSound play
  position. Each file's first superchunk preloads 15 SD chunks (one second);
  every later superchunk carries one. The port decodes frame k when the device
  has played 1470·k samples, keeping exact 15 Hz boundaries against audio.
  INTRO.HNM has 48 more frames than SD chunks; that tail continues at 15 Hz
  host time.

`ODRuntime --menu-page load|options` opens a page for hidden captures, and
`--save-root <dir>` points `GAME_LoadIndex` at a retail install tree. By
default the per-user `SDL_GetPrefPath` directory stands in for
`X:\CRYO\DREAMS\`. With the installed tree, the page lists "Hamam
island", "Hamam Pool" and "Inside hamam", and selects slot 3 as retail does.
`ODBootMenuTests` covers:

- the grid, both pages and the success blink;
- a synthetic `game.dat`;
- the `DSOUND_SetChannelVolume` attenuation;
- the playlist packing.

`ODMovieClockTests` covers the audio and host clocks. `SDL_LOGGING=app=debug`
reports each movie's frames, wall time and played audio at its end.
INTRO.HNM presented all 2,781 frames in 185.444 s (185.400 s nominal),
after its 182.2 s of SD audio drained. GENERIC.HNM loops measured 101 frames
in 6.739–6.742 s wall time against 6.750 s of audio (6.733 s nominal).

### Startup audit and New Game reset — 2026-09-28

An ordered audit covers every call and global write from `WinMain` through
`GAME_Init`, `GAME_InitSubsystems` and `BOOT_Run`, plus shutdown
(`out/dev/research-init.md`). Its New Game items are now ported in
`shared/port/game_state.cpp`:

- the startup game state: health 100.0, magic 20.0, the startup
  `ENT_ResetInventory`, the hotkey sets and elder latch 1;
- `BOOT_ResetNewGame`:
  - `SCENE_ClearLevelStates` clears only the name byte of each of the eight
    0x510-byte visited-level slots, plus the ring index;
  - `ENT_ResetInventory` keeps retail's `0xffffff00` selection word and the
    four record flag words;
  - the `0x626f14`/`0x626f20` hotkeys, the HUD icon cache, the 15.0 value and
    the movie flag are reset.

New Game leaves health, magic, the `0x626f08` set and the elder latch alone,
as retail does. The latch now comes from that state rather than being
re-armed per New Game. The runtime also draws the menu during the hold and
presents one more menu frame before the black clear. Two retail behaviours
are deliberately not reproduced:

- After Quit, retail runs one more frame that opens `TETE_E~1.HNM` and never
  closes it.
- Retail freezes all work while the window lacks focus.

The runtime exits cleanly on Quit, on a mid-movie close and on a missing
source; this was checked with hidden runs.

Remaining for later milestones:

- `GAME_LoadGame` restore and the save-mode pages (in-game);
- the gameplay consumers of the shadow and fight options;
- the level-exit transition;
- exact glyph blend arithmetic;
- pixel parity against a retail capture.
