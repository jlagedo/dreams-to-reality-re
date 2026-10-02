# 006 — Manual QA checklist for the direct GPU renderer (Windows)

Date: 2026-10-01. Authority: [spec 006](spec.md). Evidence for the automated
part: [windows-coverage.tsv](windows-coverage.tsv).

Automated, headless runs already cover: all 150 projects loading at their spawn
view under the strict audit build, menus, options, captions, movies, combat,
water and fog routes, ten save reloads, pacing at the 25 FPS cap, and a
comparison of every project's spawn view with the DOS 3dfx build. This list is
what those runs cannot judge: a visible window, real input, motion, sound, and
anything reached only by playing.

Two sittings are enough: sections 1 to 6 in the first (about two hours),
sections 4 (rest), 7 and 8 in the second.

## Commands used throughout

Run from the repository root. Every command names the renderer, so the result
does not depend on what the default is.

```sh
# Normal play from the two disc images (run directory out/recomp/windream/run-qa)
uv run python recomp/windream/run.py --discs --tag qa --renderer direct

# The same spot with the software renderer
uv run python recomp/windream/run.py --discs --tag qa --renderer software
```

- `--discs` needs exactly one `.cue` in the folder that holds each of
  `DREAMS_DISC1` and `DREAMS_DISC2`. Otherwise give the discs:
  `--disc1 PATH --disc2 PATH` (`.cue`, `.iso` without music, or a directory).
- `--tag qa` keeps logs, saves and dumps in `out/recomp/windream/run-qa/`.
  The game's own files (saves) are in `run-qa/sandbox/`. The software and the
  direct run share them when they use the same tag.
- Confirm the renderer in use: `run-qa/stderr.txt` has the line
  `[direct] shared renderer ready at <W>x<H>` only under direct.
- Keys: arrows move, Ctrl jumps or kicks, Alt punches, Space switches to
  combat, 1 to 3 pre-select magic, Esc opens the menu, holding F10 shows the
  controls, F11 toggles fullscreen.
- There is no Save command. The game autosaves on every level entry; load from
  the main menu (RIGHT to "Load", RETURN) or in game (Esc, RIGHT to the system
  tab, SPACE, "Load", SPACE).

### Starting in a specific level (developer tool)

"New game" loads project 0. `bank_patch.py` writes a modified `dreams.dat`
into a run's data directory with another project copied into slot 0. Use a
separate tag so normal play keeps the disc's own bank:

```sh
uv run python recomp/windream/debug/bank_patch.py list                 # all 150 projects: scene, level, links
uv run python recomp/windream/debug/bank_patch.py write out/recomp/windream/run-qa-level/sandbox --copy 17:0
uv run python recomp/windream/run.py --discs --tag qa-level --renderer direct
```

Then Esc through the intro and RETURN on "New game". A movie before the level
is skipped with Esc. For a project of level 3 or 4 the game asks for disc 2
and the host changes disc by itself. To go back to the normal start, delete
`out/recomp/windream/run-qa-level/sandbox/dreams.dat`. The tool refuses to
write outside `out/`.

The 3dfx reference for a project's spawn view is
`out/recomp/reference-3dfx/gamma/pNNN.png` (640x480, gamma applied; `README.md`
and `index.json` there describe it). It is a spawn view from an emulated
Voodoo with dithering, a few seconds apart from any recomp frame: compare
content, colours and lighting, not pixels.

## How to report

Report each finding once, with the section and item number.

**An error box, or the game closing by itself.** The direct renderer stops on
a case it does not support: a box titled "Dreams to Reality" with the reason,
then the process ends.

1. Copy the reason text (first paragraph of the box). The same text is the
   `[direct] FATAL: <reason>` line in `stderr.txt` and the whole content of
   `direct-fatal.txt`.
2. Before starting the game again (a new run overwrites `stderr.txt`), copy
   from the run directory (`out/recomp/windream/run-qa/`, or `run-qa-level/`):
   - `stderr.txt`: the log, with the guest state report under
     `=== recomp: direct renderer FATAL: <reason> ===`;
   - `direct-fatal.txt`;
   - for a crash that is not a renderer stop (`=== recomp: CRASH` in
     `stderr.txt`), also the newest `crash-<pid>.dmp`. It holds all guest
     memory and is large; `--dump mini` makes a small one on the rerun.
3. Say the level (the save title, or the project number from `bank_patch.py`),
   what you were doing, and the options in force (real shadow, cinemascope).

A game started from the launcher or a release build writes `log.txt`,
`direct-fatal.txt` and `crash-<pid>.dmp` in its user data directory instead:
`userdata/` beside the executable, or `%APPDATA%\DreamsToReality` when that
folder cannot be written.

**A visual fault.** A screenshot (Win+Shift+S or Alt+PrtSc; `--snap-ms 5000`
also writes one `snap_*.png` of the window every 5 s into the run directory),
the level, where you stood and what you were doing, the window size or
fullscreen, and whether it is constant, flickers, or appears only in motion. For
a fullscreen or resize fault attach `stderr.txt`: every size change logs
`[display] client=<W>x<H> drawable=<W>x<H> fullscreen=<0|1>`.

**Renderer fault or game fault?** Repeat the same spot with
`--renderer software` and the same `--tag`:

- a level reached by play: load its autosave from the main menu;
- a level started with `bank_patch.py`: the same bank is still in the tag's
  sandbox, so "New game" starts there again.

If software shows the same thing, it is a game or runtime fault: report it as
such. If only direct shows it, check the table below first; a 3D difference
from software can be deliberate, because direct follows the 3dfx build.

## Known, accepted differences — do not report

| What you see | Why it is accepted |
|---|---|
| Thin black seams between wall or floor faces in Project39 (`E11_ANGK`) and similar architecture, clearer at high resolution | Defect of the game data: the faces are one world unit apart. The 3dfx build shows the same seams. Report only seams that flicker or open and close in motion, or seams on actors |
| The ending level (project 95, `END.DSN`) exits the game after one scene | Every build does, software included; the DOS build exits on entering it |
| A pause of about 3.4 s once, right after the opening caption of a level | Present under software too; game or runtime, cause not located. Section 8 asks only whether it is bothersome |
| The three empty quick-slot icons at the bottom right of the HUD are half as bright as in the 3dfx reference images | Deliberate: direct blends them as the Windows build does; the Voodoo could not blend 2D over 3D |
| The band behind dialogue text is a smooth blend, not the stipple of the 3dfx build | The same decision |
| Attacks and spells briefly light the surrounding scene; the 3dfx build did not in most levels | Deliberate: the Windows flashes are kept over the 3dfx-matching unlit base |
| Water, energy beams, parachutes, a lamp and an eye are half transparent, some textures tile, and `E29USINE` is evenly lit, where software shows opaque surfaces and a dark level with a lit patch round the player | Direct follows the 3dfx build for 3D output (section 4) |
| The 3D picture is sharper and smoother than software and has no dither pattern | Full-precision colour; RGB565 and dithering are not implemented |
| Widescreen shows more to the left and right; HUD, menus and movies stay a centred 4:3 area | Hor+ by design |

## 1. Start-up and display

| # | How | Correct | Defect | Done |
|---|---|---|---|---|
| 1.1 | `uv run python recomp/windream/run.py --discs --tag qa --renderer direct` | 1280x960 window, intro movie with sound, Esc reaches the main menu with its moving background | Error box, black or garbled window, no sound | [ ] |
| 1.2 | Add `--scale 1`, then `--scale 3` | 640x480; then 1920x1440 or the largest that fits the desktop. Same picture, sharper at the larger size | Wrong size, picture offset or cropped | [ ] |
| 1.3 | `--width 1920 --height 1080`, start a new game | Wider view of the same scene at the same vertical extent; HUD and dialogue centred at 4:3 proportions; nothing stretched | Stretched picture, HUD off centre or cropped, geometry missing at the left and right edges | [ ] |
| 1.4 | In a level, press F11, play ten seconds, press F11 again; repeat three times | Fullscreen at the desktop resolution and back to the window, same picture, no pause longer than the switch | Black screen, frozen picture, wrong proportions after returning, error box | [ ] |
| 1.5 | Add `--fullscreen` to 1.1 | Starts fullscreen (borderless); intro and menu at correct proportions | As 1.4 | [ ] |
| 1.6 | In a level, drag the window border: wide, tall, very small, then maximise and restore | The picture follows each size without stretching; a wider window shows more at the sides with the HUD centred; no leftover image at the borders. Note what a window taller than 4:3 does | Stretching, stale borders, flicker while dragging that does not stop, error box | [ ] |
| 1.7 | Resize during the intro movie and in the main menu | Movie and menu stay centred at 4:3 proportions | Movie stretched or cropped | [ ] |
| 1.8 | Windows display scaling at 125% or 150% (Settings, Display, Scale), then 1.1, 1.4 and 1.6 | Same picture; the `[display]` lines show a drawable size larger than the client size; nothing is blurred beyond the chosen filter or cut off | Picture fills only part of the window, or is cut off | [ ] |
| 1.9 | With two monitors: drag the window to the other one, F11 there, drag back | Fullscreen opens on the monitor the window is on; picture intact after each move | Fullscreen on the wrong monitor, black window after the move | [ ] |
| 1.10 | Alt+Tab away in a level and back; minimise and restore | The game pauses without focus (as the original does) and resumes with the picture intact | Black or stale picture after return, error box | [ ] |
| 1.11 | `--filter nearest`, then `--filter linear`, at `--scale 3` | Three different sharpness levels of the same picture (`pixelart` is the default) | No difference, or a broken picture | [ ] |
| 1.12 | Close the window with its close button during a level | The game ends; `stderr.txt` has no `FATAL` and no `CRASH` | Hang, crash block | [ ] |
| 1.13 | Launcher: start `out\recomp\windream\build\windream_recomp.exe` with no arguments from a shell where `WD_DISC1` is not set; choose the discs; on the display settings pick "New (GPU)"; Play | The game starts with direct (check the `[direct] shared renderer ready` line in the console or log) | Launcher error, game starts with the wrong renderer | [ ] |

## 2. Menus and mouse

The retail game reads no mouse. The recomp supplies mouse events to the two
handlers that still decode them (the boot/menu dispatcher and the game tick):
in play the mouse steers like a joystick, and the editor flag (keypad 5) shows
a cursor. No document records that the retail menus react to clicks, so 2.5
asks what happens; the items after it check the position mapping.

| # | How | Correct | Defect | Done |
|---|---|---|---|---|
| 2.1 | Main menu: RIGHT/LEFT through New game, Load, Options, Quit | Each item highlights; background movie keeps playing; text sharp and complete | Missing or doubled text, frozen background, trails | [ ] |
| 2.2 | In a level press Esc; RIGHT round the four tabs; SPACE into each page; Esc back out | Level picture dimmed behind the menu; each page complete; after closing, the level picture is restored exactly | Menu remains on screen after closing, level picture not restored, black background | [ ] |
| 2.3 | System tab, "Load", SPACE: the slot list | Ten slots with titles and the thumbnail of the selected save; the thumbnail shows the level at its spawn | Empty, garbled or upside-down thumbnail | [ ] |
| 2.4 | Hold F10 in a level | The controls screen; released, the level returns | Stale picture | [ ] |
| 2.5 | Main menu and in-game menu at `--scale 2`: move the mouse over items and click | Record what happens (nothing, highlight, selection). If items react, the item under the pointer is the one that reacts | An item other than the one under the pointer reacts | [ ] |
| 2.6 | Repeat 2.5 at `--scale 1`, at `--width 1920 --height 1080`, in fullscreen, and after resizing by hand to a narrow window | Same behaviour as 2.5 at every size; with letterbox bars the hit position follows the picture, not the window | Hits offset by the bar width or scaled wrongly | [ ] |
| 2.7 | Keypad 5 in a level (editor flag): move the mouse to the four corners of the picture, at the sizes of 2.6; keypad 5 again to leave | The cursor sprite sits under the system pointer everywhere inside the 4:3 area | Cursor offset or scaled; grows worse toward one edge | [ ] |
| 2.8 | In play, move the mouse left and right | The player turns as with a joystick, by the same amount at every window size | Turning speed depends on window size | [ ] |

## 3. Normal play through the first levels

Start with 1.1 and play as a player would for 30 to 45 minutes, without tools.
Level 1 begins in `H18ANGKR` (Project0) and links to `F08_GPIC` and `E13_ANGK`,
then the Angkor group (`E11_ANGK`, `E12_ANGK`).

| # | How | Correct | Defect | Done |
|---|---|---|---|---|
| 3.1 | New game: opening movie, first scene, opening caption with portrait | Movie, then the level with HUD (gauges, compass, quick slots); caption band smooth, portrait and text readable | Missing HUD parts, caption text cut, band missing | [ ] |
| 3.2 | Walk, run, jump and turn through the first level | Steady picture; walls, floor and actors solid; no holes; distant geometry stable | Flicker or shimmer on distant walls, faces popping in and out at screen edges or when close, see-through walls | [ ] |
| 3.3 | Walk close along walls and into corners; let the camera press against a wall | Near geometry clips cleanly | Large triangles vanishing, the inside of the player model filling the screen for more than a moment | [ ] |
| 3.4 | Fight the first enemies: punch, kick, Space for combat mode; get hit | Health gauge changes; hit and attack effects visible; attack flash lights the scene briefly | Gauge not updating, effects as opaque boxes, flash stuck on, error box | [ ] |
| 3.5 | Pick up mana motes and items; open the menu's magic and objects pages afterwards | Motes are translucent; mana gauge and inventory pages show what was collected, icons correct | Opaque black squares, wrong or missing icons | [ ] |
| 3.6 | Obtain a spell by play, select it with 1 to 3, cast it | Spell effect drawn (translucent where it should be), gauge fire flash on the mana gauge, scene flash | Missing effect, error box. Never run with naturally obtained spells: report whatever happens | [ ] |
| 3.7 | Leave the level through a link (a doorway or edge that loads the next level) | Transition movie where the game has one, loading, next level with correct picture; the autosave appears in the Load list with a thumbnail | Black level, leftover image of the previous level, error box | [ ] |
| 3.8 | Go back through the link to the previous level | Same picture as the first time | Stale textures, wrong lighting after return | [ ] |
| 3.9 | Die (let an enemy win, or fall) | Death movie, then the Load page; loading resumes the level with correct picture | Stuck screen, wrong picture after the load | [ ] |
| 3.10 | Reach water (`E12_ANGK`, the pool), dive with Alt, swim, and climb out | Under water the view takes the water tint (fog); out of water it clears at once; surface animates | Tint remains after leaving, no tint under water. Getting out of water was not reached by automated runs | [ ] |
| 3.11 | Trigger an in-level event movie by play (for example `E11_ANGK`, movie `ANGKOR.HNM`) | Movie plays centred with sound, then the level resumes with HUD | Movie over a stale picture, level not restored. Not reached by automated runs | [ ] |
| 3.12 | Go beyond the first rooms of each level: far ends, upper floors, outdoor parts | As 3.2. Automated checks saw only the spawn view of each level | Missing geometry, black areas, sky missing, error box | [ ] |
| 3.13 | Play on to the end of level 2 and the link to a level 3 project | The game asks for disc 2 (its prompt may show briefly) and the host changes disc by itself (`[disc] active disc 1 -> 2` in `stderr.txt`); music continues with the new disc's tracks; the level loads | "MCI Error" box, endless prompt, wrong level | [ ] |

## 4. Specific levels worth a visit

Start each with `bank_patch.py write ... --copy N:0` as described above. Walk
around for a few minutes, look at the named thing from several distances and
angles, and compare the spawn view with
`out/recomp/reference-3dfx/gamma/pNNN.png`.

### 4a. Translucent and tiling nodes (3dfx retyping)

In these levels direct makes the named surfaces 50% translucent (or tiling)
as the 3dfx build did; software shows them opaque. Only projects 17 and 39
were confirmed by eye; the others have log evidence only
(`[direct] source_mode=-7` or `=9` in `stderr.txt`).

| # | Project (scene), level | Look at | Correct | Defect | Done |
|---|---|---|---|---|---|
| 4.1 | 17, 60, 136 (`F02_CASC`), 2 | The pool surface `eaup` by the waterfall | Water half transparent, the bottom visible through it, texture animates; correct against actors standing in it | Opaque water, water drawn over things in front of it, flicker between water and floor | [ ] |
| 4.2 | 41 (`M07LACM`), 1 | Lake surfaces and the two rocks | Translucent water left and right, rocks half transparent | Opaque, or sorting flicker when the camera turns | [ ] |
| 4.3 | 7 (`E02ARAI0`), 2 | Central tube and its centre | Tube translucent; centre texture repeats without smearing | Opaque tube, stretched edge texels | [ ] |
| 4.4 | 8 (`E03ARAI1`); 9, 115 (`E04ARAI2`); 73 (`E99ARAI2`), 2 | Energy beams (`!NRJ`) | Translucent beams in motion, no flicker, things behind remain visible | Opaque beams, beams vanishing at some angles | [ ] |
| 4.5 | 72 (`E98ARAI1`), 2 | The spider (`ARAI`) | The node `ARAI` translucent throughout; this is the largest translucent load in the game | Parts opaque, parts missing, a `deferred translucent blocks` line in `stderr.txt` | [ ] |
| 4.6 | 112, 118 (`E05GAUDI`); 24 (`E05GAUD2`), 3 and 4 | The four `Para` canopies and their shadow web on the floor | Translucent canopies; floor brightness like the 3dfx image | Opaque web, dark floor | [ ] |
| 4.7 | 75 (`E08_END`), 4 | Nodes `Pt1` to `Pt7` | Translucent | Opaque | [ ] |
| 4.8 | 3 (`H18PUIT`), 2; 33, 89, 132 (`F05CAB`), 3; 37 (`M05AUTEL`), 1 | The eye; the lamp; the three `m05mom` objects | Translucent | Opaque | [ ] |
| 4.9 | 2 (`E09COL`), 2; 84 (`E25_RIDE`), 1; 69, 90 (`M08ENT`), 1 | Sky and repeated textures | Sky present and tiling without seams or smears. Project 84 had a black sky before today's fix | Black sky, stretched single texel bands | [ ] |

### 4b. Project39 (`E11_ANGK`, projects 39 and 70, level 1)

| # | How | Correct | Defect | Done |
|---|---|---|---|---|
| 4.10 | Project 39 at `--scale 1`, `--scale 3` and fullscreen; walk round the pool | Thin black seams on the architecture edges are expected (accepted). Pool rim and floor texture tile; fog toward the distance in blue-grey | Seams that flicker, widen in motion, or appear on actors; missing faces | [ ] |
| 4.11 | Same level, slow camera turns | Fog steady, distant geometry fades smoothly | Fog banding that jumps, objects popping through the fog | [ ] |

### 4c. `E29USINE` and the other +0xc8 levels

| # | Project (scene), level | Correct | Defect | Done |
|---|---|---|---|---|
| 4.12 | 14, 71, 140, 141 (`E29USINE`), 3 | Evenly lit and mildly bright, as the 3dfx images; far walls, flame columns and the second spiral are drawn; brightness drifts slowly, without steps. Software shows a dark level with a lit patch round the player: that difference is deliberate | Black walls, missing flames, brightness jumping each second. One object in project 14 is a known open item: describe any that looks wrong | [ ] |
| 4.13 | 26, 56, 59, 74 (level 2); 50, 124, 133 (level 3); 109 (level 4) | Lighting and colour like the 3dfx image of the project (these differed before today's palette change) | Much darker or differently tinted than the 3dfx image | [ ] |
| 4.14 | 45 (`E15_RIDE`), 4; 31 (`F07_GROT`), 1; 109 (`L19_SANG`), 4 | Sky and far terrain present; in 109 the red glowing ceiling | Black sky, far terrain missing or cut along a straight line | [ ] |

### 4d. Fog

| # | Project (scene), level | Correct | Defect | Done |
|---|---|---|---|---|
| 4.15 | 31 (`F07_GROT`), 1 | Grey fog above the horizon as in the 3dfx image; smooth in motion | Black above the horizon, hard horizontal edge | [ ] |
| 4.16 | 28, 43 (`H15ARENE`), 2; 50, 133 (`L17_TUAF`), 3; 113 (`F38ARENE`), 3 | Coloured haze in the middle distance, steady while moving and turning | No haze, haze that flickers or changes with camera angle only | [ ] |
| 4.17 | 110, 126, 127, 128 (`L14_PETI`), 4 | Four different fog colours on the same scene. The haze of project 128 is a known open item: describe it | Fog over the HUD or menus, fog missing | [ ] |
| 4.18 | 55, 138 (`O01EAU01`), 2 | Brown and grey rock as in the 3dfx image | The whole cave tinted dark blue | [ ] |
| 4.19 | 30, 123 (`E12_ANGK`), 1: enter and leave the pool several times | Water tint on entry, clear on exit, each time | Tint stuck on or off | [ ] |

## 5. Options

In a level: Esc, RIGHT to the system tab, SPACE, DOWN to "Options", SPACE.
Four toggles; DOWN/UP selects, SPACE changes.

| # | How | Correct | Defect | Done |
|---|---|---|---|---|
| 5.1 | Real shadow on, no weapon in hand; walk, turn, jump, stand on slopes | The round blob under the player becomes a projected silhouette that follows the pose; other actors keep blobs | No shadow, a rectangle, a shadow detached from the feet, error box | [ ] |
| 5.2 | Real shadow on, then draw or hold a weapon (sword, bow or gun, Space for combat mode once you own one) | **Predicted to stop** with `shadow contains a non-mask material mode`. This has never been run. Report what happens either way, with the weapon and level | If it does not stop: a wrong or missing shadow | [ ] |
| 5.3 | Real shadow on, load a save, change level | The option holds; shadow correct after each | Shadow lost or frozen after a load | [ ] |
| 5.4 | Real shadow off again | Blob shadow returns, keyed (no dark square round it) | Square outline, missing shadow | [ ] |
| 5.5 | Manual fight on and off; fight in each | Combat behaves per the option; picture unaffected | Any picture change | [ ] |
| 5.6 | Volume max and min | Sound level changes; picture unaffected | - | [ ] |
| 5.7 | Cinemascope on and off, at 4:3 and at `--width 1920 --height 1080` | The game's own wide format and back to full screen; HUD and captions placed correctly in both; compare with software if unsure of the layout | Stale strips above or below the picture, HUD misplaced | [ ] |
| 5.8 | Same options page from the main menu ("Options") | Same four toggles, same results | - | [ ] |

`--poke 0x4a3168=1` on the `run.py` command line starts with real shadows on,
for repeating 5.2.

## 6. Movies

| # | How | Correct | Defect | Done |
|---|---|---|---|---|
| 6.1 | Watch the whole intro without Esc | Picture and sound stay together to the end; 4:3 proportions, centred; no tearing or stale frame | Sound ahead of or behind the picture, stretched picture, coloured noise | [ ] |
| 6.2 | The same in fullscreen on a widescreen monitor and at `--width 1920 --height 1080` | Centred with black bars left and right | Stretched to the full width | [ ] |
| 6.3 | Main menu background movie, left running for two loops | Loops without a flash or pause | Flash, wrong colours at the loop point | [ ] |
| 6.4 | Link movies: leave levels in section 3 without pressing Esc; with `bank_patch.py`, project 11 (`H04GLACE`, opens with `CONTROLE.UBB`) and project 39 (`ED2.HNM`) | Both movie formats play at correct speed and colours, then the level appears | Wrong colours (a palette fault), movie skipped, level picture missing after it | [ ] |
| 6.5 | Death movie (3.9) | Plays to its end and the Load page follows | Stuck last frame | [ ] |
| 6.6 | An in-level event movie (3.11); candidates by project: 37 `AUTEL.HNM`, 39 `ANGKOR.HNM`, 60 `CASCADE.HNM`, 69 `ARAI_FCA.HNM`, 10 `HNMFR2.UBB` | Plays and returns to the level | Not reached by automated runs: report whatever happens | [ ] |
| 6.7 | Esc during each kind of movie | Skips at once to what follows | Stale movie frame behind the menu or level | [ ] |
| 6.8 | Resize or F11 during a movie | Movie continues at correct proportions | Black window, error box | [ ] |

## 7. Debug views

Keypad 1 to 5 toggle the retail debug flags; `--overlays` starts with 1, 2 and
3 on. Each toggle logs `[debug] [0x........] = 0|1` in `stderr.txt`. Only
keypad 3 with Backspace has recorded evidence under direct.

| # | How | Correct | Defect | Done |
|---|---|---|---|---|
| 7.1 | Keypad 1 in a level | "Frame Rate / Mem 3DTR" readout over the picture, readable, updating | Missing text, error box | [ ] |
| 7.2 | Keypad 2 | Object HUD text over the picture | Missing text, error box | [ ] |
| 7.3 | Keypad 3, then hold Backspace and walk | The scene stops being redrawn (the previous image stays; nothing clears the frame) and the collision wireframe accumulates as lines over it | Black screen at once, no lines, error box | [ ] |
| 7.4 | Keypad 3 again | Normal rendering returns completely | Wireframe remains, picture not restored | [ ] |
| 7.5 | Keypad 4 on and off | Game step pinned at 2.0 while on (the game runs at a different speed); picture intact | Picture faults | [ ] |
| 7.6 | Keypad 5 on, look at the editor rows and cursor, off (see 2.7) | Editor rows and cursor drawn over the level; no collision while on; normal play after turning it off | Error box, missing cursor | [ ] |
| 7.7 | `uv run python recomp/windream/run.py --discs --tag qa --renderer direct --overlays`, then keypad 3 to return to normal rendering | Starts with readout, object HUD and collector on; each can be turned off | Error box at the first frame | [ ] |
| 7.8 | 7.3 at `--width 1920 --height 1080` and after a resize | Lines land on the geometry they belong to | Lines offset or scaled against the picture | [ ] |

## 8. Long session

| # | How | Correct | Defect | Done |
|---|---|---|---|---|
| 8.1 | Play one hour without restarting, through at least six level changes and several loads | No slowdown over time; memory of `windream_recomp.exe` in Task Manager levels off after the first levels | Steady growth of memory, growing hitches, error box | [ ] |
| 8.2 | Judge the motion at the 25 FPS cap in three levels (small room, large outdoor level, a fogged level) | Even motion; turning the camera is as smooth as under software | Regular hitches, hitches only under direct | [ ] |
| 8.3 | The pause of about 3.4 s after the opening caption of a level (also under software) | Note whether it is bothersome in normal play and in which levels it shows | A pause clearly longer under direct than under software | [ ] |
| 8.4 | The deliberate choices, judged in play: attack and spell flashes; half-bright empty quick-slot icons; smooth dialogue band; the evenly lit `E29USINE` levels | Say for each whether it looks right. These are owner decisions, not defects; the note is input for revisiting them | - | [ ] |
| 8.5 | Leave the game in a level for ten minutes untouched, then continue; leave it minimised for ten minutes | Continues normally | Black picture, error box on return | [ ] |
| 8.6 | Ten loads in a row of different saves (main menu and in-game Load) | Each resumes with correct picture and thumbnail list intact | Stale picture from the previous level, error box | [ ] |
| 8.7 | After the session, search `stderr.txt` for `FATAL`, `CRASH`, `glide_no_draw` and `deferred translucent` | None | Any of them: attach the log with the level | [ ] |
