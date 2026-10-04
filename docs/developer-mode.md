# Developer mode manual

Develop is the launch mode that brings back Cryo's own in-game tools: the
Dreams Editor menu of the July 1997 demo, the developer keys and the debug
views. It plays the **developer folder**, one tree made from both discs, so
edits never touch the disc images. This manual lists every command available
in Develop. Background and evidence: `docs/specs/008-editor-restoration/spec.md`;
implementation notes: `recomp/README.md`, "Launch modes and the developer
folder".

## Contents

1. [Starting Develop](#1-starting-develop)
2. [How keys work in Develop](#2-how-keys-work-in-develop)
3. [The editor](#3-the-editor)
4. [Editor keys](#4-editor-keys)
5. [Developer keys](#5-developer-keys)
6. [Keypad](#6-keypad)
7. [Game keys that still work](#7-game-keys-that-still-work)
8. [Saving your work](#8-saving-your-work)
9. [Files Develop reads and writes](#9-files-develop-reads-and-writes)
10. [Repository tools](#10-repository-tools)
11. [Driving Develop from scripts](#11-driving-develop-from-scripts)
12. [Known limits](#12-known-limits)
13. [Appendix: the full editor menu](#appendix-the-full-editor-menu)

## 1. Starting Develop

| Mode | What it plays | Tools | CD music |
|---|---|---|---|
| Play | the discs (the shipped game) | off | from the disc images |
| **Develop** | the developer folder | **on** | none |
| Play edits | the developer folder | off | from the disc images (silent without them) |

From the launcher: pick **Develop** in the mode row above the discs, then
Play. The **Develop tab** shows the folder, an **Open folder** button,
**Reset edits** (puts disc 1's `DREAMS.DAT` back and removes `EDITOR.DAT`) and
the key list. The first Develop start copies both discs into
`<data dir>/developer` (about 655 MB, with a progress dialog; an interrupted
copy resumes). `dreams.ini` keeps the choice as `[port] mode = retail | dev | edited`.

From the repository:

```sh
uv run python recomp/windream/run.py --mode dev                 # Develop
uv run python recomp/windream/run.py --mode edited              # Play edits
uv run python recomp/windream/run.py --mode dev --tree DIR      # another developer folder
uv run python recomp/windream/run.py --mode dev --renderer software
```

| `run.py` option | What |
|---|---|
| `--mode dev` | Develop |
| `--mode edited` | Play edits |
| `--tree DIR` | the developer folder (default `out/recomp/windream/developer`); made from the discs the first time, never copied into again once `.developer-folder` exists |
| `--renderer direct\|software` | Develop follows the normal default (direct on Windows); the render-class keys need `software` |
| `--scale 1` | window at game size (useful with `--mouse` scripts) |
| `--ctl` | open the development control channel (section 11) |

The build needs `DREAMS_WIP_DIR` (the July demo's `DREAMS` directory) to
stage the editor menu and its fonts; without it Develop keeps retail's
two-row menu. The console prints the key list when Develop starts, and the
log (`stderr.txt` of a `run.py` run) tags what each part did: `[editor]`,
`[keys]`, `[bank]`, `[pickers]`, `[tools]`, `[save]`.

## 2. How keys work in Develop

- **Keys are typed characters, as in the DOS build.** What counts is the
  character the key types on your layout: Shift+a is `A`, `a` is not. Cryo
  used an AZERTY keyboard; on QWERTY `!` is Shift+1. Characters are read in
  code page 850, so `§`, `µ` and `ù` work where the layout has them.
- **A key Develop takes never reaches the game**: no game action, no repeat,
  no release. Shift+1 opens the editor and does not also use item slot 1.
- **While the editor is open, editor commands win** over developer keys:
  `A`, `D` and `6` are editor commands then.
- **The keypad never types.** It stays the debug keys of section 6.
- **WASD is suspended while the editor is open** (the letters are editor
  commands): walk with the arrows.
- **Caps Lock** turns plain letters into editor commands, as in DOS.
- **F1 to F6** change the resolution and also close the editor.
- The Ctrl+Shift fallbacks (section 4) paste on any layout.

## 3. The editor

Open and close it with `!` (or keypad 5). It works at any resolution; opening
or closing it saves nothing.

**The menu** is Cryo's July tree, 349 rows at 10 px, drawn in the July fonts:
Project, Scene Particle, Option, Debug, Exit To DOS. With the mouse (left
button):

| Row | Looks like | Click does |
|---|---|---|
| Branch | `Name...` | opens or closes it; children appear indented below |
| Slider | `Name` with a track and a value | drag the knob to set the value (range in the appendix); values below zero are centred |
| Toggle | `Flag ...` | flips one bit of a field |
| Button | `Name Creat`, `Load`, `Save`, `Delete`, `Load Mesh`... | runs the command (the same as its key in section 4) |

What you edit:

- **Project** rows edit the working copy of the level being played (player
  start, movement, physics, fog, light, fluid, camera, sky, CD track...).
  Light rows act on the screen at once; most others take effect when the
  level reloads. **Project > Camera** acts from the next frame.
- **Objet, Link, Box, Link Adventure** rows edit a working copy of one slot,
  which you pick with that set's **Load** (a list page) or make with
  **Creat**; **Save** copies it back into the level.
- **Scene Particle** edits the level's mana-mote particles.
- **Option > Map Anim** switches the animated materials; **Option > Camera**
  is shown but **not wired** (use Project > Camera).
- **Debug** toggles whether a level loads its objects. **Exit To DOS** quits.

**Capture the position**: with a position block visible (for example Project
> Project Edit > Misc > Misc Player & Scene > Misc Player > Init Pos), press
`0` to write the player's current X, Y, Z and angle into its rows.

**List pages** (pickers) replace the menu while you choose a file or a slot:

| Part | Where (game pixels) | Action |
|---|---|---|
| title line | top | click confirms the highlighted row |
| UP | y 40 | scroll up |
| rows | y 50 to 120, 8 per page | hold the left button to highlight |
| DOWN | y 130 | scroll down |
| EXIT | y 150 | cancel |
| Space / Esc | | confirm / cancel (hidden from the game) |

Confirming with no valid row writes nothing. The pages list:

| Page | Opened by | Lists |
|---|---|---|
| LOAD MESH | `2`, Objet Edit > Load Mesh, Vehicule Mesh | every `.3dc`, `.dan`, `.dsn` in `DATA\3DC` |
| LOAD HNM | `1`, Hnm Intro, Hnm Name | `DATA\HNM` |
| LOAD SYMBOLE | `6`, Load Symb | `DATA\SYM` (empty on the discs) |
| LOAD Anim | Material Hnm | `DATA\ANIM` |
| LOAD Map | Material Name, Scroll, Plasma | the level's materials |
| Projects | `Q`, Project Delete, Link Scene (`3`) | the 150 projects of the bank |
| Objets, Links, Boxes, Link Adventures | `S`, `D`, `F`, `G` and their Delete | the level's slots |

Closing LOAD MESH on a mesh other than `EMPTY`, even with Esc, saves the
project and reloads the level, as in July.

**Messages** (refusals, writes) show at the foot of the screen for about 3 s.

## 4. Editor keys

Only while the editor is open. Letters are capitals (Shift+letter).

| | Project | Objet | Link | Box | Event (Link Adventure) |
|---|---|---|---|---|---|
| Create | `A` | `Z` | `E` | `R` | `T` |
| Load | `Q` | `S` | `D` | `F` | `G` |
| Save | `W` | `X` | `C` | `V` | `B` |

| Key (character) | QWERTY | AZERTY | Action |
|---|---|---|---|
| `1` | `1` | Shift+`&` | intro movie page (LOAD HNM) |
| `2` | `2` | Shift+`é` | mesh page (LOAD MESH) |
| `3` | `3` | Shift+`"` | exit-target page (the link's destination project) |
| `4` / `5` | `4` / `5` | Shift+`'` / Shift+`(` | add / drop a box path point |
| `6` | `6` | Shift+`-` | symbol page (LOAD SYMBOLE) |
| `0` | `0` | Shift+`à` | put the player's position into the visible X/Y/Z/angle rows |
| `?` / `.` | Shift+`/` / `.` | Shift+`,` / Shift+`;` | copy / paste a project |
| `/` / `§` | `/` / Ctrl+Shift+2 | Shift+`:` / Shift+`!` | copy / paste an objet |
| `:` / Ctrl+Shift+3 | Shift+`;` / Ctrl+Shift+3 | `:` / Ctrl+Shift+3 | copy / paste a link |
| `%` / `µ` | Shift+`5` / Ctrl+Shift+4 | Shift+`ù` / Shift+`*` | copy / paste a box |
| Space / Esc | | | confirm / cancel a list page |
| F10 | | | write the bank to the developer folder (section 8) |
| left mouse button | | | menu rows, sliders, list rows |

Create refuses while all 150 project slots are in use (the shipped bank is
full): delete one first. Project Delete frees a slot in memory only, until
the next save.

## 5. Developer keys

Editor open or closed, unless noted. "Editor off" keys do nothing while the
editor is open (its commands win, section 2).

| Key | Action |
|---|---|
| `!` | open / close the editor (keypad 5 too) |
| `-` | free-fly camera (editor off): mouse turns, mouse buttons fly, PgUp / PgDn / Home / End held change the speed; `-` again ends it |
| `9` | overhead camera; `9` again returns |
| `8` | frame rate and memory readout (keypad 1 too) |
| `6` | capture every frame to `DATA\TGA` (editor off; the frame step is pinned at 2.0 while it runs); `6` again stops |
| `7` | capture one frame to `DATA\TGA` |
| `A` | HUD on / off (editor off) |
| `D` | dialogue test: plays dialogue 0 (editor off) |
| `H` | play the level's movie again (none in project 0 after New Game) |
| `r` | record a demo; `r` again stops, writes `DATA\REPLAY.BIN` and returns to the title (editor off) |
| `R` | replay `DATA\REPLAY.BIN` (editor off); Space stops; it also stops when a game menu opens (death) |
| `e` `f` `l` `v` | render classes of the level's model (software renderer only; under direct the keys say so). `l` is a render class here: the game's Load page is Shift+`L` |
| `ù` (AZERTY) | the game's object page |

Captures are named `<scene>_0000.tga` (640×480, 24-bit); the counter restarts
at 0000 each session and writes over older files.

## 6. Keypad

| Key | Action |
|---|---|
| keypad 1 | frame rate and memory readout (as `8`) |
| keypad 2 | object HUD |
| keypad 3 | collision view (hold Backspace for the collision mesh) |
| keypad 4 | capture-every-frame flag (as `6`) |
| keypad 5 | editor (as `!`) |
| keypad 6 | give all 14 items (refused when the inventory lacks room; items already held keep their count and level) |
| keypad 7 | collision views: red wall boxes, white floor faces, yellow centres |
| keypad 8 | Cryo's profiler: the frame (grey rows), then 3D render (red), entities (green), collision (blue), and the frame rate |
| keypad 9 | console window with the game's own output; Cryo's memory and entry dumps print into it each time it opens; keypad 9 again closes it |
| keypad 0 | Cryo's Save page: save anywhere, with a title, into an empty slot, else the oldest unprotected one |

**Save page (keypad 0).** Type the title (the developer keys are suspended
while it is open; only Backspace, Tab, Return and Esc reach the game).
Return saves (`game<n>.dat`, its icon and the save list in the folder's
`DATA\GAME`); a title another save already has is refused; Esc saves
nothing. The save loads from the in-game Load page, in Develop and in Play
edits.

## 7. Game keys that still work

Arrows move (Insert held: look); Alt, Ctrl, Space, Esc are the action
buttons (Esc / Space also stop a video); `1` `2` `3` item slots (with the
editor closed); F1–F6 resolution; F10 key help (editor closed); F11 / F12
letterbox (F11 also fullscreen); Tab spell menu; Shift+`L` Load page; `P`
pause; `J` / `K` joystick / keyboard; Ctrl+S shadow mode; Alt+5…Alt+0 camera
presets; Alt+X quit; in menus: arrows, Return, Esc, Tab, Backspace.

## 8. Saving your work

Edits go through three levels:

1. **The working copy.** Sliders and toggles change the working record of the
   level, objet, link, box or event you are editing.
2. **The bank in memory.** `W` saves the project (and `X`, `C`, `V`, `B`
   copy a slot back first). The next level transition plays the edit.
3. **The developer folder.** **F10** (editor open) or quitting the game
   (the system page's Quit, Alt+X) writes `DREAMS.DAT` and `EDITOR.DAT` into
   the folder's root, through a `.tmp` file and a rename. Nothing is written
   when nothing changed.

The next Develop or Play edits start plays the written levels. **Reset edits**
in the launcher's Develop tab undoes them (or copy disc 1's `DREAMS.DAT` back by
hand). Not written: edits never saved with `W`, and a game that is killed or
ended by the control channel's `quit`. The disc images are never touched.

## 9. Files Develop reads and writes

All in the developer folder (default `out/recomp/windream/developer`, or the
launcher's `<data dir>/developer`).

| File | Written by |
|---|---|
| `DREAMS.DAT` | F10 and quit (the level bank) |
| `EDITOR.DAT` | F10 and quit (the 150 records unpacked; read by no retail code) |
| `DATA\TGA\<scene>_NNNN.tga` | `6`, `7` |
| `DATA\REPLAY.BIN` | `r` (the shipped one is a DOS recording; `R` refuses it) |
| `DATA\GAME\game<n>.dat`, `.ico`, `game.dat` | keypad 0 and the game's autosave on level entry |
| `DATA\FONT\COURE.016`, `DOSAPP.008`, `SMALLE.008`, `SMALLE.006` | copied from the build's `resources/fonts` if missing |
| `LISTL0..4.TXT`, `copyL0..4.bat` | `mastering_lists.py` (section 10) |

The save guard runs in every mode: a save that is not 11,388 bytes or names
a level not in the bank is refused with "Save refused" and the reason on the
load page, instead of crashing.

## 10. Repository tools

```sh
# Mastering lists: replay Cryo's generators over the folder's DREAMS.DAT
uv run python recomp/windream/debug/mastering_lists.py              # write listL0..4.txt, copyL0..4.bat into the folder
uv run python recomp/windream/debug/mastering_lists.py --check      # report listed files on no disc; writes nothing
uv run python recomp/windream/debug/mastering_lists.py --tree DIR --bank DAT --out DIR

# The editor menu resource (built by build.py; alone:)
uv run python recomp/windream/editor/editor_tree.py build

# A modified bank for a run (start a new game in any project)
uv run python recomp/windream/debug/bank_patch.py list
uv run python recomp/windream/debug/bank_patch.py show 26
```

## 11. Driving Develop from scripts

Development builds have a control channel (absent from release builds).

```sh
uv run python recomp/windream/run.py --mode dev --ctl --tag dev            # start with the channel
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-dev status
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-dev type 1 "!" shift      # Shift+1: open the editor
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-dev mouse 20 30 click     # click at game pixel (20, 30)
```

| Command | Use in Develop |
|---|---|
| `type` | a key as a keyboard delivers it (SDL key + text): the only way to test Develop keys. `key` sets the key state directly and bypasses Develop's key path |
| `mouse` | SDL mouse events: `x`, `y` in game pixels, `action` `move`/`down`/`up`/`click`, `dx`/`dy` for the free camera |
| `overlay_shot` | the frame just before and just after the editor and tools drew: their difference is exactly what Develop drew |
| `read`, `write`, `wait_until`, `screenshot`, `pause`, `step`, `log`, `quit` | as in every mode (`recomp/README.md`, "Development control channel") |

From Python, `recomp/windream/debug/game_nav.py` has `page_click` and
`page_choose` for list pages (a one-frame click needs a step before the
press). In Claude Code the MCP server `dreams-game` exposes the same as
`game_type`, `game_mouse` and the other `game_*` tools.

Tests that cover Develop: `tests/recomp/test_editor_tree.py`, `_menu`,
`_keys`, `_bank`, `_pickers`, `_save`, `_direct`, `test_dev_tools.py`,
`test_dev_save.py`, `test_developer_disc2.py`, `test_mastering_lists.py`.

## 12. Known limits

- **Option > Camera** sliders move but change nothing (retail reloads those
  values every frame); use Project > Camera.
- **Render classes** `e f l v` work on the software renderer only.
- **Capture every frame** (`6`) is slow under the direct renderer (each frame
  is read back from the GPU).
- **Box wireframes** are not shown (no model with boxes was found); the
  collision spheres are.
- **Rows marked `(no effect)`** in the menu (Bruit pas, Perso Integ) are kept
  from July but nothing in retail reads them.
- Keys were tested as injected key events, not on a physical French keyboard.
- The full list of what is not yet verified is in spec 008, B2 "Milestone
  status".

## Appendix: the full editor menu

The installed tree, generated by `editor_tree.py build` (`menu.txt`). Each
row: label, then the key it answers to if any, the guest address it edits,
and for sliders the range. `flags=0x6` is a button; `flags=0x40 mask=...` a
bit toggle; `flags=0x8/0x10/0x20` the X/Y/Z rows and `0x80` the angle row that
`0` fills from the player; `0x100/0x200/0x400` the same for particle
coordinates (× 256). A "host cell" is a row shown but not wired.

```text
Dreams Editor
  Project
    Project Creat Shift A  0x4a46b8  flags=0x6
    Project Load  Shift Q  0x4a46bc  flags=0x6
    Project Edit...
      Misc...
        Misc Player &Scene...
          Misc Player...
            Vehicule Mesh  0x4a4710  flags=0x6
            Init Pos...
              Player Pos X  0x65fbb8  flags=0x8  -8000..8000
              Player Pos Y  0x65fbbc  flags=0x10  -8000..8000
              Player Pos Z  0x65fbc0  flags=0x20  -8000..8000
              Player Angle Y  0x65fc10  flags=0x80  -4096..4096
              Player Speed Move  0x65fce0  0..128
            Init Mode...
              Move Mode  0x65fba0  0..8
              Move Maniability  0x65fba8  0..128
              Move Inertie walk  0x65fba4  0..128
              Move Inertie swim  0x65fbdc  0..128
              Move Inertie fly  0x65fbe0  0..128
            Init Sphere...
              Sphere collision  0x65fbac  0..2000
              Sphere shoot  0x65fbb4  0..10000
            Flags Player...
              Flag GUN OK  0x65fb18  flags=0x40  mask=0x10000
              Flag SURF OK  0x65fb18  flags=0x40  mask=0x20000
              Flag FALL NO OK  0x65fb18  flags=0x40  mask=0x40000
          Misc Scene...
            Misc Scene 2...
              Scene Dec Oxygen MUL 10  0x65fc18  0..10000
              Fondu b n  0x65fc3c  0..16
              Bruit pas (no effect)  0x65fc40  0..5
              Sky Speed  0x65fce8  -64..64
              Sky Dead  0x65fcf4  flags=0x10  -8000..8000
            Scene IA Strategic  0x65fbd4  0..2
            Scene Add Mana MUL 10  0x65fc1c  -100..100
            Scene CD Track  0x65fc20  0..20
            Camera...
              Camera K_OBJ_TARGET...
                Camera K_OBJ_TARGET_MIN  0x65fc24  -1024..1024
                Camera K_OBJ_TARGET_MAX  0x65fc28  -1024..1024
              Camera K_OBJ_BACK...
                Camera K_OBJ_BACK_MIN  0x65fc2c  -1024..1024
                Camera K_OBJ_BACK_MAX  0x65fc30  -1024..1024
                Camera Speed  0x65fcdc  0..8
              Camera K_OBJ_Y...
                Camera Y_LOW_FOLLOW_OBJ  0x65fc34  -2048..2048
                Camera Y_HIGH_FOLLOW_OBJ  0x65fc38  -2048..2048
              Speed & Collision...
                Player Speed  0x65fc44  0..512
                Camera No Collision  0x65fc48  0..1
          Fluid...
            Fluid Med R  0x65fbe4  -127..127
            Fluid Med G  0x65fbe8  -127..127
            Fluid Med B  0x65fbec  -127..127
            Fluid Ond RGB  0x65fbf0  -127..127
            Fluid YPos Level   0x65fbd8  flags=0x10  -10000..10000
          Hnm Intro  Shift 1  0x4a471c  flags=0x6
          Misc Scene 3...
            Phys Gravite...
              Const Vect X  0x65fc04  -10..10
              Const Vect Y  0x65fc08  -10..10
              Const Vect Z  0x65fc0c  -10..10
            Fog...
              Fog R  0x65fcc4  0..255
              Fog G  0x65fcc8  0..255
              Fog B  0x65fccc  0..255
              Fog Density  0x65fcd0  0..128
            Scene Save Mode  0x65fcfc  0..3
            Scene Chapter CD  0x65fd00  0..4
        Material Light...
          Light Medium...
            Light Med R  0x65fb1c  -127..127
            Light Med G  0x65fb20  -127..127
            Light Med B  0x65fb24  -127..127
            Light Contrast  0x65fcec  -127..127
            Contrast Just Scene  0x65fcf0  0..1
          Light Onde...
            Light Ond R  0x65fb28  -127..127
            Light Ond G  0x65fb2c  -127..127
            Light Ond B  0x65fb30  -127..127
          Light Base...
            Light Base R  0x65fb34  0..256
            Light Base G  0x65fb38  0..256
            Light Base B  0x65fb3c  0..256
          Light 3dtr...
            Persos Palette inc  0x65fbc4  -8..8
            Decors Palette inc  0x65fbc8  -8..8
            Player Light  0x65fbcc  0..1
            Decors Clip  0x65fbd0  0..20000
            Perso Integ (no effect)  0x65fcf8  0..8
          Light Time...
            Light Time R  0x65fbf4  -127..127
            Light Time G  0x65fbf8  -127..127
            Light Time B  0x65fbfc  -127..127
            Time  0x65fc00  0..1024
        Material Hnm...
          Material Name  0x4a4748  flags=0x6
          Hnm Name  0x4a474c  flags=0x6
        Material Scroll  0x4a4744  flags=0x6
        Material Plasma  0x4a4740  flags=0x6
      Link Adventure...
        Link Adv Creat Shift T  0x4a46d8  flags=0x6
        Link Adv Load  Shift G  0x4a46dc  flags=0x6
        Link Adv Edit...
          Link Adv Objet With...
            Name Objet  0x4a4720  flags=0x6
            Link with Name Objet  0x4a4724  flags=0x6
            Link with Name Box  0x4a472c  flags=0x6
            Flags Condition...
              Flags C. Element Src..
                Flag Ele. Src NEAR  0x65d624  flags=0x40  mask=0x1
                Flag Ele. Src KILL  0x65d624  flags=0x40  mask=0x4
                Flag Ele. Src In Invent.  0x65d624  flags=0x40  mask=0x20
                Flag E. Src NOT In Invt.  0x65d624  flags=0x40  mask=0x200
                Flag Ele. Src LIFE INIT  0x65d624  flags=0x40  mask=0x100
              Flags C. Element Dest..
                Flag Ele. Dest NEAR  0x65d624  flags=0x40  mask=0x40
                Flag Ele. D. NEAR src   0x65d624  flags=0x40  mask=0x1000
                Flag E. Dest NOT In Inv  0x65d624  flags=0x40  mask=0x2000
              Flags C. Element Misc..
                Flag All Ele Src KILL  0x65d624  flags=0x40  mask=0x8
                Flag Just One Freeze  0x65d624  flags=0x40  mask=0x400
                Flag Time End  0x65d624  flags=0x40  mask=0x10
                Flag Time Cycle  0x65d624  flags=0x40  mask=0x80
                Time  0x65d640  0..127
              Flags C. Element Misc2.
                Flag All UnFreeze  0x65d624  flags=0x40  mask=0x800
            Flags Action...
              Flags A. Element Dest..
                Flag Ele Dest OK  0x65d628  flags=0x40  mask=0x1
                Flag Ele Dest KILL  0x65d628  flags=0x40  mask=0x4
                Flag Ele Dest MOVE OK  0x65d628  flags=0x40  mask=0x20
                Flag Ele Dest SEE OK  0x65d628  flags=0x40  mask=0x40
                Flag Ele Dest Near Src  0x65d628  flags=0x40  mask=0x400
              Flag A. Elem Dest 2...
                Flag A. Dest CHANGE  0x65d628  flags=0x40  mask=0x80
                Flag A. Dest Recharge  0x65d628  flags=0x40  mask=0x100
                Flag A. Dest Follow  0x65d628  flags=0x40  mask=0x8000
                Flag A. Dest In INV  0x65d628  flags=0x40  mask=0x10000
              Flags A. Element src..
                Flag Src KILL no effect  0x65d628  flags=0x40  mask=0x10
              Flags A. Element Misc..
                Flag all Ele Dest OK  0x65d628  flags=0x40  mask=0x2
                Flag Element Dest HNM  0x4a4730  flags=0x6
                Flag all Dest Time Susp  0x65d628  flags=0x40  mask=0x200
                Flag all D. Time NO Susp  0x65d628  flags=0x40  mask=0x800
                Flag A. Player DAMAGE  0x65d628  flags=0x40  mask=0x40000
              Flags A. El. Misc 2..
                Flag Camera Obj to Obj  0x65d628  flags=0x40  mask=0x1000
                Flag Camera Pt to Obj  0x65d628  flags=0x40  mask=0x2000
                Dialog Number (0=none)  0x65d620  0..178
                Link OK  0x65d628  flags=0x40  mask=0x4000
                Light OK  0x65d628  flags=0x40  mask=0x20000
          Link Adv Box With...
            Link with Name Objet  0x4a4724  flags=0x6
            Link with Name Box  0x4a472c  flags=0x6
            Flags Condition...
              Flags C. Element Src..
                Flag Ele. Src NEAR  0x65d624  flags=0x40  mask=0x1
                Flag Ele. Src KILL  0x65d624  flags=0x40  mask=0x4
                Flag Ele. Src In Invent.  0x65d624  flags=0x40  mask=0x20
                Flag E. Src NOT In Invt.  0x65d624  flags=0x40  mask=0x200
                Flag Ele. Src LIFE INIT  0x65d624  flags=0x40  mask=0x100
              Flags C. Element Dest..
                Flag Ele. Dest NEAR  0x65d624  flags=0x40  mask=0x40
                Flag Ele. D. NEAR src   0x65d624  flags=0x40  mask=0x1000
                Flag E. Dest NOT In Inv  0x65d624  flags=0x40  mask=0x2000
              Flags C. Element Misc..
                Flag All Ele Src KILL  0x65d624  flags=0x40  mask=0x8
                Flag Just One Freeze  0x65d624  flags=0x40  mask=0x400
                Flag Time End  0x65d624  flags=0x40  mask=0x10
                Flag Time Cycle  0x65d624  flags=0x40  mask=0x80
                Time  0x65d640  0..127
              Flags C. Element Misc2.
                Flag All UnFreeze  0x65d624  flags=0x40  mask=0x800
            Flags Action...
              Flags A. Element Dest..
                Flag Ele Dest OK  0x65d628  flags=0x40  mask=0x1
                Flag Ele Dest KILL  0x65d628  flags=0x40  mask=0x4
                Flag Ele Dest MOVE OK  0x65d628  flags=0x40  mask=0x20
                Flag Ele Dest SEE OK  0x65d628  flags=0x40  mask=0x40
                Flag Ele Dest Near Src  0x65d628  flags=0x40  mask=0x400
              Flag A. Elem Dest 2...
                Flag A. Dest CHANGE  0x65d628  flags=0x40  mask=0x80
                Flag A. Dest Recharge  0x65d628  flags=0x40  mask=0x100
                Flag A. Dest Follow  0x65d628  flags=0x40  mask=0x8000
                Flag A. Dest In INV  0x65d628  flags=0x40  mask=0x10000
              Flags A. Element src..
                Flag Src KILL no effect  0x65d628  flags=0x40  mask=0x10
              Flags A. Element Misc..
                Flag all Ele Dest OK  0x65d628  flags=0x40  mask=0x2
                Flag Element Dest HNM  0x4a4730  flags=0x6
                Flag all Dest Time Susp  0x65d628  flags=0x40  mask=0x200
                Flag all D. Time NO Susp  0x65d628  flags=0x40  mask=0x800
                Flag A. Player DAMAGE  0x65d628  flags=0x40  mask=0x40000
              Flags A. El. Misc 2..
                Flag Camera Obj to Obj  0x65d628  flags=0x40  mask=0x1000
                Flag Camera Pt to Obj  0x65d628  flags=0x40  mask=0x2000
                Dialog Number (0=none)  0x65d620  0..178
                Link OK  0x65d628  flags=0x40  mask=0x4000
                Light OK  0x65d628  flags=0x40  mask=0x20000
        Link Adv Save  Shift B  0x4a46e0  flags=0x6
        Link Adv Delete  0x4a46e4  flags=0x6
      Link...
        Link Creat Shift E  0x4a46ec  flags=0x6
        Link Load  Shift D  0x4a46f0  flags=0x6
        Link Edit...
          Link Scene Shift 3  0x4a470c  flags=0x6
          Link Condition
            Link Condition Flag...
              Flag OK OBJET/ NOT  0x65d65c  flags=0x40  mask=0x8
              Flag NO Player Action/Y  0x65d65c  flags=0x40  mask=0x20
              Flag NO LinkAdvent/ YES  0x65d65c  flags=0x40  mask=0x10
              Flag NO EnemiDead/ YES  0x65d65c  flags=0x40  mask=0x2
              Flag NO HitLink/ YES  0x65d65c  flags=0x40  mask=0x4
            Link with Objet   0x4a46e8  flags=0x6
            Link Condition Flag...
              Flag In Space/ Out  0x65d65c  flags=0x40  mask=0x40
              Flag Player/ Any Objet  0x65d65c  flags=0x40  mask=0x80
          Link Space...
            Link  Pos Min...
              Pos X  0x65d668  flags=0x8  -5000..5000
              Pos Y  0x65d66c  flags=0x10  -5000..5000
              pos Z  0x65d670  flags=0x20  -5000..5000
            Link  Pos Max...
              Pos X  0x65d674  flags=0x8  -5000..5000
              Pos Y  0x65d678  flags=0x10  -5000..5000
              Pos Z  0x65d67c  flags=0x20  -5000..5000
            Link  10 * Pos Min...
              Pos X  0x65d668  flags=0x8  -32000..32000
              Pos Y  0x65d66c  flags=0x10  -32000..32000
              pos Z  0x65d670  flags=0x20  -32000..32000
            Link  10 * Pos Max...
              Pos X  0x65d674  flags=0x8  -32000..32000
              Pos Y  0x65d678  flags=0x10  -32000..32000
              Pos Z  0x65d67c  flags=0x20  -32000..32000
        Link Save  Shift C  0x4a46f4  flags=0x6
        Link Delete  0x4a46f8  flags=0x6
      Objet...
        Objet Creat Shift Z  0x4a46c8  flags=0x6
        Objet Load  Shift S  0x4a46cc  flags=0x6
        Objet Edit...
          Load Mesh Shift 2  0x4a4718  flags=0x6
          Load Symb Shift 6  0x4a4714  flags=0x6
          Flags & Comportement...
            Flags...
              Flag NO ANI/ ANI  0x65f8f8  flags=0x40  mask=0x2
              Flag Ami/Enemi  0x65f8f8  flags=0x40  mask=0x4
              Flag NO Run/Run  0x65f8f8  flags=0x40  mask=0x10
              Flag NO See-Move/ YES  0x65f8f8  flags=0x40  mask=0x40
              Flag NO Light/Light  0x65f8f8  flags=0x40  mask=0x8
            Flags 2...
              Flag NO SHADOW / YES  0x65f8f8  flags=0x40  mask=0x100
              Flag NO DN Bless/Bless  0x65f8f8  flags=0x40  mask=0x20
              Flag NO Hit-Link/ YES  0x65f8f8  flags=0x40  mask=0x80
              Flag NO PosRand/ YES  0x65f8f8  flags=0x40  mask=0x200
              Flag NO Follow / YES  0x65f8f8  flags=0x40  mask=0x400
            Flags 3...
              Flag NO Platf / YES  0x65f8f8  flags=0x40  mask=0x800
              Flag NO Fire / YES  0x65f8f8  flags=0x40  mask=0x1000
              Flag YES Ami hit / NO  0x65f8f8  flags=0x40  mask=0x2000
              Flag NO ManaCreat /YES  0x65f8f8  flags=0x40  mask=0x4000
              Flag YES Shock /NO  0x65f8f8  flags=0x40  mask=0x8000
            Comportement...
              Life  0x65f8f0  0..1024
              Mana  0x65f8f4  0..1024
              Speed  0x65f8fc  0..512
              Strenght  0x65f900  0..512
              Masse  0x65f940  0..1024
            Comportement 2...
              Courage  0x65f944  0..128
              Defence  0x65f948  0..128
              Attack  0x65f94c  0..128
              Path  0x65f950  0..16
              Speed Move  0x65f954  0..128
          Init Pos Mode Sphere...
            Init Pos...
              Pos X  0x65f904  flags=0x8  -16000..16000
              Pos Y  0x65f908  flags=0x10  -4000..4000
              Pos Z  0x65f90c  flags=0x20  -16000..16000
              Angle Y  0x65f920  flags=0x80  -4096..4096
            Init Mode...
              Move Mode  0x65f928  0..8
              Move Inertie  0x65f92c  0..128
              Move Maniability  0x65f930  0..128
              Shoot Impact /128  0x65f958  0..128
              Scale  0x65f95c  0..128
            Init Sphere...
              Sphere collision  0x65f934  0..1000
              Sphere move  0x65f938  0..10000
              Sphere see  0x65f93c  0..10000
        Objet Save  Shift X  0x4a46d0  flags=0x6
        Objet Delete  0x4a46d4  flags=0x6
      Box...
        Box Creat Shift R  0x4a46fc  flags=0x6
        Box Load  Shift F  0x4a4700  flags=0x6
        Box Edit...
          Path Input  Shift 4  0x4a4734  flags=0x6
          Path Output Shift 5  0x4a4738  flags=0x6
          Box mode  0x65b334  0..16
          Box Space...
            Box Pos Min...
              Pos X  0x65b250  flags=0x8  -10000..10000
              Pos Y  0x65b254  flags=0x10  -10000..10000
              Pos Z  0x65b258  flags=0x20  -10000..10000
            Box Pos Max...
              Pos X  0x65b25c  flags=0x8  -10000..10000
              Pos Y  0x65b260  flags=0x10  -10000..10000
              Pos Z  0x65b264  flags=0x20  -10000..10000
          Mode 2 Phys Intensity  0x65b338  -1000..1000
        Box Save  Shift V  0x4a4704  flags=0x6
        Box Delete  0x4a4708  flags=0x6
    Project Save  Shift W  0x4a46c0  flags=0x6
    Project Delete  0x4a46c4  flags=0x6
  Scene Particle
    Init...
      Init Volume...
        Coord Min...
          Pos X  0x65fc4c  flags=0x100  -256000..256000
          Pos Y  0x65fc50  flags=0x200  -1024000..1024000
          Pos Z  0x65fc54  flags=0x400  -256000..256000
        Coord Max...
          Pos X  0x65fc58  flags=0x100  -256000..256000
          Pos Y  0x65fc5c  flags=0x200  -1024000..1024000
          Pos Z  0x65fc60  flags=0x400  -256000..256000
      Init Speed...
        Speed Min...
          Speed X  0x65fc64  -1024..1024
          Speed Y  0x65fc68  -2048..2048
          Speed Z  0x65fc6c  -1024..1024
        Speed Max...
          Speed X  0x65fc70  -1024..1024
          Speed Y  0x65fc74  -2048..2048
          Speed Z  0x65fc78  -1024..1024
      Init Comp...
        Particle OK  0x65fce4  0..1
        Generation  0x65fc7c  0..3
        Turbulence  0x65fc80  0..15
        Player Add Mana  0x65fcd8  0..2048
      Life...
        Init Life Min  0x65fc84  0..2048
        Init Life Max  0x65fc88  0..2048
        Life Dec  0x65fc8c  0..64
        Life Gen Dec  0x65fc90  0..7
        Part Quant Init  0x65fcd4  0..240
    Comp...
      Type  0x65fc94  0..3
      T1 Force...
        Force X  0x65fc98  -64..64
        Force Y  0x65fc9c  -64..64
        Force Z  0x65fca0  -64..64
      T2 Part Attract...
        Particle one...
          Pos X  0x4a2fc4  -1024000..1024000
          Pos Y  0x4a2fc8  -1024000..1024000
          Pos Z  0x4a2fcc  -1024000..1024000
          Gravity  0x4a2fe4  0..32
        Particle Two...
          Pos X  0x65fca4  flags=0x100  -1024000..1024000
          Pos Y  0x65fca8  flags=0x200  -1024000..1024000
          pos Z  0x65fcac  flags=0x400  -1024000..1024000
          Gravity  0x65fcb0  0..32
        Particle Three...
          pos X  0x65fcb4  flags=0x100  -1024000..1024000
          Pos Y  0x65fcb8  flags=0x200  -1024000..1024000
          Pos Z  0x65fcbc  flags=0x400  -1024000..1024000
          Gravity  0x65fcc0  0..32
      T3 Contact...
        Y Min   0x4a3088  flags=0x200  -128000..128000
    Option...
      Part 0 No Life  0x4a2fb0  flags=0x6
    Reset  0x4a2fac  flags=0x6
  Option
    Camera...
      Camera K_OBJ_TARGET...
        Camera K_OBJ_TARGET_MIN  host cell from 0x49d1cc  -1024..1024
        Camera K_OBJ_TARGET_MAX  host cell from 0x49d1d0  -1024..1024
        Camera SpeedCamera  0x49d1f0  1..32
        Camera SpeedTarget  0x49d1f4  1..32
      Camera K_OBJ_BACK...
        Camera K_OBJ_BACK_MIN  host cell from 0x49d1d8  -1024..1024
        Camera K_OBJ_BACK_MAX  host cell from 0x49d1dc  -1024..1024
        Camera K_OBJ_BACK_PAS  0x49d1e0  1..64
        Camera Y_LOW_FOLLOW_OBJ  host cell from 0x49d1e4  -1024..1024
        Camera Y_HIGH_FOLLOW_OBJ  host cell from 0x49d1e8  -1024..1024
    Map Anim...
      Map Anim Hnm  0x4a0f68  flags=0x6
      Map Anim Plasma  0x4a0f6c  flags=0x6
      Map Anim Scroll  0x4a0f70  flags=0x6
      Map Anim Color  0x4a0f74  flags=0x6
  Debug  0x49d9f0  flags=0x6
  Exit To DOS  retail node 0x4a4784  flags=0x6
```
