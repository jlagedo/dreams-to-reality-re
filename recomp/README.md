# The recomp

Lift `GDIDREAM.EXE` to C with [pcrecomp](https://github.com/sp00nznet/pcrecomp)'s
`lift32`, build it against a hand-written host runtime and GPU renderer, and
run it; and test the lifter on small Watcom programs run natively and
recompiled. Background and results: `docs/specs/000-the-recomp/spec.md`;
rendering: `docs/specs/006-recomp-glide-renderer/spec.md`.

Only hand-written sources live here. Everything they produce is game-derived or
bulky and goes under `DREAMS_OUT\recomp` (by default the repository's
`out\recomp`), never into git: the lifted C, `imports_gen.c`, builds, runs,
sandboxes, crash dumps and the difftest work directories.

## Setup

- pcrecomp: clone it to `out\recomp\pcrecomp`, or set `DREAMS_PCRECOMP`. The
  scripts were last run against commit `1f49cea`.
- `DREAMS_DISC1` (the exe), `DREAMS_INSTALL_ROOT` (the retail `CRYO\DREAMS`
  tree the game reads), `DREAMS_GHIDRA_ROOT` and the Watcom settings, as in
  `.dreams.example.env`.
- Windows: Visual Studio with clang-cl, CMake and Ninja (found through
  `vswhere` and `vcvarsall.bat`). Linux: gcc or clang, CMake, Ninja and the
  X11/Wayland, OpenGL and ALSA or PulseAudio development packages, from `PATH`.
- SDL3 for the window, input and sound. The first build downloads the source
  pinned in `recomp/render/cmake/Dependencies.cmake` and builds a static release
  library once into `out\recomp\sdl3\<commit>` (`<commit>/linux`,
  `<commit>/darwin` off Windows), which every recomp and difftest build then
  shares.
- The KERNEL32 file, process, thread and synchronisation bridges and the
  USER32, GDI32, WinMM and DirectSound bridges run on SDL3 and build without
  `<windows.h>`. The guest's virtual memory is the portable ledger
  (`host/vm/vm_ledger.c`); only its three-call OS layer is per system
  (`vm_os_win32.c`, `vm_os_posix.c`, `vm_os_wasm.c`). The crash report is
  split the same way: `host/core/crash_report.c` prints the guest's state on
  every host, and one file per system catches the fault (`crash_win32.c`:
  exception handler, dbghelp stack walk, minidump; `crash_posix.c`: signals and
  raw `backtrace` frames; `crash_none.c` for WebAssembly). Nothing asks the
  host what is at a guest address: that is `vm_state`.
- The game builds and runs on Windows and on Linux (x86-64, gcc, checked under
  WSL: same virtual-memory log as the Windows run for the same key script).
  The macOS paths (`vm_os_posix.c` with 16 KB pages, `crash_posix.c`, the Metal
  backend) have not been compiled; the WebAssembly ones have not been built.
  The `win32` and `shadow` virtual-memory builds and the retail-oracle
  verification scripts stay Windows-only.

On Linux there is no `uv` requirement for building and running: the scripts
need only the standard library. `.dreams.local.env` holds Windows paths, so
give the two the run needs in the environment. Builds go to
`out/recomp/windream/build-linux` and share the generated C with Windows:

```sh
PYTHONPATH=src python3 recomp/windream/build.py
DREAMS_DISC1=/mnt/e/<disc 1> DREAMS_INSTALL_ROOT=/mnt/e/<...>/CRYO/DREAMS \
  PYTHONPATH=src python3 recomp/windream/run.py --headless --renderer direct --seconds 30
```

The build stages the editor menu and the July fonts only when it finds the
July demo, so give `DREAMS_WIP_DIR` as a Linux path too
(`DREAMS_WIP_DIR=/mnt/e/<...>/DREAMS`); without it Develop keeps the retail
two-node menu. Develop, Play edits and the editor and tool suites were
checked under WSL on 2026-10-04 (spec 008 phase M), including a developer
folder on a case-sensitive file system.

### Browser tool environment (Windows)

The browser tools on this machine are configured in the gitignored
`out/recomp/web-tools/tools.json`. From PowerShell in the repository root:

```powershell
. ./recomp/web-env.ps1
emcc --version
cmake --version
ninja --version
```

The script activates Emscripten 6.0.10 and adds the existing CMake, Ninja,
project Python, SDK Node and pinned `sokol-shdc` to the current shell's PATH.
It sets `DREAMS_EMSDK`, `DREAMS_WEB_SDL3`, `SDL3_DIR`, and `DREAMS_SHDC`, and adds
the browser SDL3 install to `CMAKE_PREFIX_PATH`. A different local configuration
can be selected with `-Config <tools.json>` or `DREAMS_WEB_TOOLS`.

SDL3 is the repository's pinned source, built separately for WebAssembly with
pthreads under `out/recomp/sdl3/fa2c02bb6e21/wasm-6.0.10-pthreads/`. Future browser
targets should be configured with `emcmake cmake`, link with `-pthread`, and
pass `-DSDL3_DIR="$env:SDL3_DIR"` (cross-compilation otherwise searches only the
SDK sysroot) and `-DOD_SHDC_EXECUTABLE="$env:DREAMS_SHDC"` to reuse the pinned
host shader compiler. Threaded browser runs need a server providing COOP/COEP headers.
`emrun` is included in the SDK for local browser serving.

This prepares the tools and dependencies only. It does not build the game for
the browser or adapt its runtime. The setup record and standalone toolchain
checks are under `out/recomp/web-tools/`.

## Commands

From the repository root:

```sh
uv run --with capstone --with pefile python recomp/windream/lift/lift.py          # gen/ + lift-report.json
uv run --with capstone --with pefile python recomp/windream/lift/gen_imports.py   # gen/imports_gen.c
uv run python recomp/windream/build.py        # out/recomp/windream/build (unoptimized)
uv run python recomp/windream/run.py          # play (direct GPU renderer on Windows, software elsewhere); logs and dumps in out/recomp/windream/run
uv run python recomp/windream/run.py --renderer software   # the original software rendering
uv run python recomp/windream/run.py --mute --renderer direct --seconds 30 # silent unattended run
uv run python recomp/windream/run.py --headless --renderer direct --seconds 30 # hidden, muted run
uv run python recomp/windream/run.py --seconds 60 --keys 2000:ESC,5000:RETURN --snap-ms 4000
uv run python recomp/windream/run.py --overlays   # retail debug flags on; keypad 1-4 toggle (spec 005)
uv run python recomp/windream/run.py --poke 0x49da14=1   # any dword into the image before entry
uv run --with capstone --with pefile python recomp/windream/debug/flag_hunt.py   # unwritten flags
uv run python recomp/windream/run.py --fullscreen --pad keys
uv run python recomp/windream/run.py --discs  # play from the two disc images, not the extracted trees
uv run python recomp/windream/run.py --headless --discs --ctl   # with the development control channel (debug/wdctl.py)
uv run --with pefile python recomp/windream/release.py   # DreamsToReality.exe + README.txt, zipped
uv run --with capstone --with pefile python recomp/difftest/difftest.py t_core --tag od110
```

## Playing

The game polls the keyboard as it did on Windows (`docs/research/engine.md`, "Input"):
the arrows move, Ctrl jumps or kicks, Alt punches, Space switches to combat,
1 to 3 pre-select magic, Esc opens the menu, and holding F10 shows the
controls. The window pauses the game when it loses focus, as the original
does. F11 toggles fullscreen; the game sees F11 too.

| `run.py` option | Environment | Default | What |
|---|---|---|---|
| `--renderer` | `WD_RENDERER` | `direct` on Windows, `software` elsewhere | `direct` is the GPU renderer: it stops with an error on a case it does not support and never falls back. `software` is the original rendering. The launcher's Renderer setting (`dreams.ini` `[port] renderer = gpu` or `software`) has the same default |
| `--scale N` | `WD_SCALE` | 2 | Window size in multiples of 640x480, reduced to fit the desktop |
| `--fullscreen` | `WD_FULLSCREEN` | off | Start fullscreen (borderless, desktop resolution) |
| `--filter` | `WD_FILTER` | `pixelart` | Scaling: `pixelart` (sharp, even pixels), `nearest` or `linear` |
| `--pad` | `WD_PAD` | `winmm` | Gamepads: `winmm` shows them as WinMM joysticks (press J in game), `keys` makes them press keys, `off` ignores them |
| `--deadzone IN,OUT` | `WD_DEADZONE` | `10,95` | Scaled radial deadzone for sticks and triggers, percent of full deflection: below IN reads centred, past OUT reads full |
| none | `WD_KEYMAP` | unset | Keyboard remap, comma-separated `PHYSICAL=GAME` key pairs: `W=UP,A=LEFT,S=DOWN,D=RIGHT` makes WASD the arrows (W then no longer reads as W). Names: letters, digits, `UP DOWN LEFT RIGHT`, `CTRL SHIFT ALT` (`LCTRL`, `RALT`... for one side), `SPACE RETURN ESC TAB BACKSPACE`, `F1`-`F24`, `KP0`-`KP9`, `INSERT DELETE HOME END PAGEUP PAGEDOWN`, and punctuation (`MINUS EQUALS COMMA PERIOD SLASH SEMICOLON QUOTE GRAVE LBRACKET RBRACKET BACKSLASH`). The `WD_KEYS` script is not remapped |
| none | `WD_PAD_DIRECTION` | `stick` (`winmm`), `both` (`keys`) | Which pad control gives direction: `stick`, `dpad` or `both`. `winmm`: the stick drives X/Y and the d-pad the POV hat; `dpad` makes the d-pad drive X/Y (POV centred); `both` lets either drive X/Y. `keys`: which of them press the arrows |
| none | `WD_PADMAP` | unset | Pad button remap, comma-separated `BUTTON=TARGET` pairs over the defaults. BUTTON: `a b x y lb rb back start ls rs`, and in `keys` mode `lt rt`. TARGET: `button1`-`button32` in `winmm` mode (default `a b x y lb rb back start ls rs` = 1-10), a key name as for `WD_KEYMAP` in `keys` mode |
| `--retail-timing` | `WD_FIXED_STEP=0` | fixed step (`1`) in native builds, retail in the browser | The original timing: the frame delta from the game's 5 ms tick counter (0.9 to 1.35 at a steady 25 fps), paced by `--fps`. The default instead presents on a 30 Hz grid with a frame delta of exactly 1.0, the step the engine's demo recorder uses (`host/sdl/pacing.c`); it also turns display interpolation off. The launcher's "Smooth motion" (`dreams.ini` `[port] smooth`, default 1) is the same switch |
| `--fps N` | `WD_FPS` | 25 | With `--retail-timing`: present cap; above 30 the original physics breaks (`docs/research/running.md`) |
| `--no-interpolate` | `WD_INTERPOLATE=0` | on with the direct renderer in a visible window on Windows | Display interpolation draws a frame at every display refresh, the game's scene blended between its last two frames with its HUD replayed over it; the display trails the game by about 25 ms and frames with no 3D scene (menus, dialogue) show as drawn (`host/render/render_live.cpp`, `render_interp.cpp`). Each frame is placed on the display's own refresh timing from DWM (`DwmGetCompositionTimingInfo`), estimated where a backend has none. Off headless and on other systems (no D3D11 frame-latency object: a present would hold the game up), where `WD_INTERPOLATE=1` forces it on |
| `--smooth-camera MS` | `WD_SMOOTH_CAMERA` | 60 | With display interpolation: the shown camera follows the game's through a lag of that time constant, which evens out the follow camera's per-frame easing and dead zone (display only); at a steady speed it trails by exactly that time, on top of the game camera's own easing (about 116 ms). `0` turns it off. The launcher's "Camera smoothing" (`[port] smooth_camera`) |
| `--mute` | `WD_MUTE` | off | Open no audio device; keep mixing on a timer so sound/CD cursors and completion polling still advance |
| `--headless` | `WD_HEADLESS` | off | Keep the SDL window hidden, run muted, and enable scripted input while reporting focus |

With `--pad keys`, the stick and d-pad are the arrows, A is Ctrl, X is Alt,
Y is Space, B is Down, LB, RB and LT are 1, 2 and 3, and Start is Esc: the
layout in `docs/research/running.md`. Back is Return, for the menus. In `winmm` mode a
pad looks like an XInput pad does to WinMM on Windows (X/Y the left stick,
POV the d-pad, buttons A B X Y LB RB Back Start LS RS as 1 to 10). The game
reads only X, Y and the buttons, and it takes the stick position at start as
the centre, so leave the stick alone while the game starts.

The three remap variables work on devices only: one physical key or button
stands in for another, and the host knows nothing about game actions. Bad
entries are logged (`[keys]`, `[joy]`) and skipped, and a start-up line prints
the effective pad table when either pad variable is set. In `winmm` mode the
triggers are the Z axis, not buttons, so `lt` and `rt` cannot be mapped there;
a mapped button above 10 raises the button count the game reads. The pure
parsing and lookup is `windream/host/sdl/input_map.h`, tested by
`tests/recomp/test_input_map.py`.

## Reading the game from the disc images

By default `run.py` gives the host directories: the extracted disc 1 tree and
the install tree (`WD_READ_ROOTS`), with the guest EXE as the first argument.
In disc mode the host needs neither. It opens the user's two discs through
`recomp/disc`, loads `GDIDREAM.EXE` from disc 1, serves every file the game
asks for from them, and plays the CD music from the same images.

| `run.py` option | Environment | What |
|---|---|---|
| `--discs` | `WD_DISC1`, `WD_DISC2` | Disc mode with the one `.cue` in the folder that holds each of `DREAMS_DISC1` and `DREAMS_DISC2` |
| `--disc1 PATH --disc2 PATH` | `WD_DISC1`, `WD_DISC2` | Disc mode with the given discs: each a `.cue`, an `.iso` (no music) or an extracted directory (music from the `(Track NN).bin` files beside it). Both are required, and each must be the disc its variable names |
| set by `--discs`/`--disc1` | `WD_DATA_DIR` | The user data directory: every write goes there and every read looks there first. It is the write sandbox under another name and takes the place of `WD_WRITE_ROOT`; `run.py` sets it to `<run dir>/sandbox` |
| none | `WD_DISC_ACTIVE` | `2` starts with disc 2 in the drive (default 1), for testing the change back to disc 1 |
| none | `WD_FILES_LOG` | How many file opens are logged (default 200; either mode) |

With `WD_DISC1` unset nothing changes: `WD_READ_ROOTS`, `WD_WRITE_ROOT`,
`WD_CD_DIR` and the EXE path work as before. With it set and no EXE path on
the command line (`windream_recomp --run`), the EXE is disc 1's.

What the guest sees in disc mode (`windream/host/sdl/files.c`, spec 007):

- A path at the CD root, or relative: the data directory, then the active disc.
- A path under `CRYO\DREAMS\`: the data directory, then the same path without
  that prefix on the active disc, then on the other one. Two exceptions:
  `CRYO\DREAMS\DATA\FULL.ID` is never found, so the game stays in its
  read-from-CD mode and copies nothing; and files of `CRYO\DREAMS\DATA\GAME\`
  (the saves) come from the data directory only, because disc 2 carries a
  leftover `GAME.DAT`, `GAME0.DAT` and `GAME0.ICO` there.
- Disc 1 is active at start. `DATA\1CD.ID` and `DATA\2CD.ID` open only on
  their own disc; when the game opens the other disc's marker twice in a row
  (its "Please change to CD" prompt polling), the host makes that disc the
  active one and logs `[disc] active disc 1 -> 2`. The music stops and the CD
  device then has the new disc's tracks.
- Log lines name the source: `[files] open R "DATA\1CD.ID" -> disc1:DATA/1CD.ID`.

CD music in disc mode plays each track from its `INDEX 01` for its own length,
so a single-`.bin` image works and the 2-second pregap is skipped; with
`WD_CD_DIR` (directory mode) a track file is still played whole. The MCI
device reports the disc's track count with the data track (12 on disc 1, 14 on
disc 2) in both modes. `MCI_PLAY` with no start position and no current track
succeeds silently in both modes; it used to fail, which the game showed as an
"MCI Error" box after its disc prompt. `MCI_PLAY` of any track also succeeds
silently when the device has no audio track at all (an `.iso`); with audio
tracks, a track that is not one of them is still out of range.

A stop, a pause or a track change takes effect for the game at once (the MCI
mode), but the mixer fades the music out over 10 ms (`CD_FADE`,
`windream/host/sdl/dsound.c`) instead of cutting it at whatever sample it had
reached, which clicked; the next track fades in. Two other mixer rules came from
the same clicks. A new 8-bit DirectSound buffer holds 8-bit silence (0x80), as
Windows' DirectSound gives it. And, departing from retail, `DSOUND_LoadWav`'s
clear of a longer previous voice line, a `memset_` of 0 (full-scale negative in
the 8-bit voice channel), is turned into 8-bit silence. Before that, voice lines
ended in seconds of DC offset: a loud click at each end, and sound effects
clipped in between.

The three disc-mode paths are read with `SDL_getenv`, which on Windows gives
the Unicode environment as UTF-8, so paths with non-ASCII characters work
whatever the ANSI code page; code that sets them inside the process must use
`SDL_setenv_unsafe` before the host's first file call. `tests/recomp/test_disc_mode.py`
runs the production file bridges on two small directory discs
(`windream/verify/native/disc_mode_tests.c`): resolution, the marker rule in
both directions, the saves, listings and reads.

## Launch modes and the developer folder

Spec 008 (`docs/specs/008-editor-restoration/spec.md`, phase M): **Play** is
the shipped game; **Develop** reconnects Cryo's in-game developer tools and
plays from the developer folder, one tree made from the two discs as the
developers' hard-disk game had it; **Play edits** plays that folder's game with
the tools off and CD music from the disc images. The launcher offers the three
in a mode row above the discs (`dreams.ini` `[port] mode = retail | dev | edited`);
its Develop tab shows the folder, Reset edits (disc 1's `DREAMS.DAT` back over the
folder's, the editor's `EDITOR.DAT` removed) and the key list. Develop's first Play copies both discs into
`<data dir>/developer` with a progress dialog (`--play`: on stderr) and writes
`.developer-folder` last; an interrupted copy resumes. Play edits needs that folder
and plays silently without the images. `run.py` gives the modes for development and
tests; it makes the folder the same way and, once `.developer-folder` is there, never
copies into it again (the copy replaces every file whose size differs from the disc's,
which would undo the editor's `DREAMS.DAT` and the saves).

| `run.py` option | Environment | What |
|---|---|---|
| `--mode dev` | `WD_MODE=dev`, `WD_TREE` | Develop: the DOS keys (`host/sdl/dev_keys.c`, below), keypad 1-5 (`host/sdl/user.c`), the editor's mouse feed and the inserted editor call are live (in Play and Play edits they are inert and the keypad reaches the game); `CD_OpenAudio` (`0x4042f1`) is replaced to return 0, so no CD music and no MCI error (`host/sdl/launch_mode.c`); the platform's renderer as in Play (direct on Windows) unless `--renderer` says otherwise (spec 008 phase D) |
| `--mode edited` | `WD_MODE=edited`, `WD_TREE`, `WD_DISC1`, `WD_DISC2` | Play edits: files from the developer folder, CD audio from the discs |
| `--tree DIR` | `WD_TREE` | The developer folder (default `DREAMS_OUT/recomp/windream/developer`). `run.py` makes or completes it first with `disc_list --copy-merged` (`recomp/disc`, `disc_copy_merged`): disc 1 wins a shared path but `DATA\UNIVBE\UVCONFIG.EXE`; disc 1's `DATA\FULL.ID` and disc 2's `DATA\GAME` are left out; a resumed copy skips files already at full size. About 655 MB |

The Dreams Editor menu of Develop (spec 008 phase 1): the build extracts the
July 1997 demo's menu from its `DREAMS.EXE` (`DREAMS_WIP_DIR`, SHA-256
checked), binds each leaf to its retail address with
`windream/editor/bindings.tsv` and copies the result beside the executable as
`resources/editor-tree.tsv` (`windream/editor/editor_tree.py build` makes it
alone, with `menu.txt`, the menu as text, under `out/recomp/windream/editor/`).
In Develop the host builds those nodes in guest memory before the game starts
(`windream/host/sdl/editor_menu.c`, `[editor] menu: 349 nodes` in the log);
without the resource the editor keeps the retail two-node menu. While the
inserted editor call runs, the editor prints in the July fonts, which the build
copies from the demo (SHA-256 checked) as `resources/fonts/`: the host puts them
into the tree's `DATA\FONT` if missing, loads them into font slots 4-7 with
retail's `TEXT_LoadFont` (`[editor] July fonts loaded into slots 4-7`) and lets
slots 0-3 hold them during the editor's frame (without them, `HI320` in slot 1).
The sliders are the DOS build's, ported: retail never
loads their sprite set (`alphabe2.spr`, `[editor] sprite set 3 ... loaded` once
the editor first opens) and its slider blit `0x402406` is empty, so the host
loads the set with retail's own `LoadFileSpr_` and replaces the blit with the
DOS `_ZoomSpriteL16` (`lift/replacements.py` `HOST_ENTRIES`). Without
`DREAMS_WIP_DIR`, `release.py` refuses to build.

The keyboard of Develop is DOS keys (spec 008 phase 2,
`windream/host/sdl/dev_keys.c`): text input is on, and the character a key
types, in code page 850 as the DOS build read it, decides whether the host
takes the key. A key it takes (an editor command while the editor is on, a
developer key, `!`) never reaches the game: no key state, no event `0x33`, and
its repeats and release are dropped. The editor reads its characters from a
queue, one per frame, which the host writes into the frame's key code
`0x626fd8` for the editor call alone and then puts the game's code back, so
`GAME_HandleHotkeys` sees only virtual keys and the editor only characters.
Shift+letter is the editor's capital (`a` is no command, `A` creates a project);
the keypad never types; Ctrl+Shift+2/3/4 paste an objet, link and box on any
layout; Space and Esc go to the editor alone while one of its pages is open;
`WD_KEYMAP` is suspended while the editor is on. The key list
(`recomp/launcher/dev_keys.h`) is in the launcher's Develop tab and printed on
stdout when Develop starts; `[keys] ...` lines in the log show what was taken.
While Cryo's Save page is open (keypad 0) the developer keys are suspended:
typed ASCII is its title and only Backspace, Tab, Return and Esc reach the
game. Test keys with the
channel's `type` command: its `key` command sets the key state directly and
bypasses all of this.

Cryo's developer tools (spec 008 phase 7, `windream/host/sdl/dev_tools.c`,
`[tools] ...` in the log) are Cryo's code reached by a host call or a data
write, served from the inserted editor call once per gameplay frame:
`6`/`7` capture every frame or one frame with Cryo's `SaveImage_` into the
folder's `DATA\TGA`, which the host makes at Develop start (`<scn>_0000.tga`,
640x480 24-bit; the counter starts at 0000 each session, as in retail, over
older files; keypad 4 is the same flag as `6`); keypad 6 gives all 14 items
(the retail flag `0x49d5e0`), refused unless the empty inventory entries cover
the missing names (plus one when any is held), and puts back the spelling,
count and level of the entries already held; keypad 7 draws the collision
world's spheres with Cryo's `Display_Collision_Sphere_` and
`Display_Overlap_Sphere_` (red wall boxes, white floor faces, yellow centres;
software renderer); keypad 8 is Cryo's profiler bar and frame rate
(`Display_Info_Timer_`, `info_timer_text_`) over four stages the host times:
the frame (top grey rows), then the 3D render (red), the entities (green) and
collision (blue) on the rows between (`lift.py` `CALLS` around their calls);
keypad 9 opens a console window (Windows; the terminal elsewhere) that shows
the game's own standard output, and Cryo's `Scan_Mem_` and `PrintMisEntry_`
print into it on each opening; `-` frees the camera with SDL's relative mouse
mode on (its turn deltas are zeroed each frame, so it stops with the mouse);
`H` plays the project's movie again (the boot sequence's four calls, `data\hnm\`
plus the record's `+0x3c`; none in project 0 after New Game); `e f l v`
change the render classes of the level's model (`[0x4fbdbc]`) under the
software renderer only (the direct renderer, like the 3dfx build, draws
nothing for classes 6 and `0x1c`: there the keys only say so). Under the
direct renderer the sliders, the profiler and the collision views' centre
dots, which Cryo's code writes into the guest frame with the CPU, reach the
screen through `windream/host/sdl/dev_overlay.c` (a marker fill before the
draw, the changed pixels handed to the renderer after it), and a TGA capture
reads the GPU frame back into guest memory first; `tests/recomp/test_editor_direct.py`
matches every editor page and tool against the software renderer. The channel's
`mouse` command takes `dx`, `dy` for relative motion; `tests/recomp/test_dev_tools.py`
checks each tool and saves its screen as `DREAMS_OUT/recomp/editor-reference/tool-*.png`.

Keypad 0 opens Cryo's Save page (`windream/host/sdl/dev_save_page.c`), the
Load page in save mode that retail never calls: the host picks an empty slot,
else the least recent unprotected one, clears its title for typing, and when
the page is confirmed with Return makes the save Cryo's page never made
(`GAME_SaveGame`, `GAME_SaveIndex`, `GAME_SaveThumbnail`: `game<n>.dat`, the
index `game.dat` and `game<n>.ico` in the folder's `DATA\GAME`), refusing a
title another save has; Esc saves nothing. The page closes the menu with it.
`r` (editor off) records a demo with Cryo's recorder and `r` again writes
`DATA\REPLAY.BIN` in the folder and returns to the title (Cryo's own end);
`R` (editor off) plays it, refusing any file that is not 4 + n x 112 bytes
(the folder's shipped `REPLAY.BIN` is a DOS recording of 104-byte records);
Space stops it, and the host stops it when a game menu opens (the death page)
and puts the input mode back when it ends. `tests/recomp/test_dev_save.py`
checks both, on a copy of the folder.

The save guard runs in every mode (`windream/host/sdl/save_guard.c`):
`GAME_LoadGame` is replaced by a host check of the save it is about to read
(through the file layer, `[save] guard: ...` in the log), which refuses a file
that is not 11,388 bytes or whose project name is not in the bank by
`DDAT_LoadRecord`'s rule, before anything is overwritten, and otherwise calls
the original. The load pages then show "Save refused" with the reason in help
entry 5's place, on the main menu too (`lift.py` `CALLS` `0x43630c`). Without
it such a save crashed the game (`memcpy_` from a NULL record) or loaded the
last level.

The editor's projects are a bank of 150 unpacked records in guest memory (spec
008 phase 4, `windream/host/sdl/editor_bank.c`; the codec is
`editor_bank_rle.c`): each time `DDAT_Load` reads the folder's `DREAMS.DAT`
the host unpacks it (`[bank] 150 records unpacked ...; records at 0x...`), and
the retail functions that walk the 150 records (free slot, project list, empty
bank, Create, Load, Delete, Save) are replaced (`HOST_ENTRIES`). Before each
editor frame the save pointer `0x661d94` is bound to the record of the level
being played (`[bank] the level is ProjectN ...`), so `W` writes that record and
the host packs the bank back into `DREAMS.DAT` in memory (`[bank] Project
Save ...`): the next level transition loads the edit. Create refuses with a
message at the screen's foot while all 150 records are in use (the shipped
bank); Delete frees a record in the bank only, until the next save. Closing LOAD MESH (`2`) on a mesh other than
`EMPTY`, even with Esc, saves the project and reloads the level, as in July.

The bank reaches the disk (spec 008 phase 5) as July's `SaveDiskScene_` wrote
it: once it changed (`0x661da4`, which the editor sets after a project save or
delete), F10 with the editor on, and the game's own quit (`GAME_Shutdown`'s
`SaveDiskScene_`, `0x44900a`: the system page's Quit and Alt+X), write `DREAMS.DAT` (packed, what the game holds in memory) and
`EDITOR.DAT` (the 150 records raw, which no retail code reads) into the
developer folder's root, each through a `.tmp` file and a rename (`[bank] Bank
written (F10): ...`). The next Develop or Play edits start plays them; Reset
edits in the launcher (or disc 1's `DREAMS.DAT` copied back by hand) undoes
them. Edits not saved with `W`, and a game ended by the control channel's
`quit` or a kill, are not written. The disc images are never touched.

The editor's pickers (spec 008 phase 3, `windream/host/sdl/editor_pickers.c`)
page host tables, never retail's one-byte tables, which ran over the resource
heap's globals: LOAD MESH (`2`) lists every `.3dc`, `.dan` and `.dsn` of the
folder's `DATA\3DC`, LOAD HNM (`1`) `DATA\HNM`, LOAD SYMBOLE (`6`) `DATA\SYM`,
LOAD Anim `DATA\ANIM` (refilled each time a page opens, through the game's own
find calls, 8.3 names only: `[pickers] mesh list: 270 files ...`); LOAD Map the
scene's materials; the project page (`Q`, Delete, the link target `3`) the
bank; Objet, Link, Box and Link Adventure Load (`S`, `D`, `F`, `G`) the live
level's slots. The layout and hit tests are retail's: rows at y 50 + 10 k,
UP 40, DOWN 130, EXIT 150, the title line confirms; Space confirms, Esc
cancels; a confirm with no valid row writes nothing. Drive a page from a test
with the channel's `mouse` command (`game_nav.page_click`, `page_choose`).

What the guest sees in tree mode (`WD_TREE`, `windream/host/sdl/files.c`; the
browser build is always in it): the tree is the only read root and the write
root, and the guest EXE is its `GDIDREAM.EXE`; `CRYO\DREAMS\x` is `x`;
`DATA\FULL.ID` is never found (its copy-to-hard-disk mode would purge the
tree's level files); the three disc markers are created if missing. With
`WD_DISC1` and `WD_DISC2` as well, the discs are opened for their audio only,
and the last marker the game opened picks the disc whose tracks play. The
host runs the launcher only when it has no EXE path, no `WD_DISC1` and no
`WD_TREE`.

The mastering lists (spec 008 phase 6) come from a repository tool, not the
game:

```sh
uv run python recomp/windream/debug/mastering_lists.py [--tree DIR] [--bank DAT] [--out DIR] [--check]
```

`windream/debug/mastering_lists.py` replays retail's two uncalled generators
(`0x447b72`, `0x447e7c`) over the developer folder's `DREAMS.DAT` and writes
`listL0..4.txt` (the files each level group needs, which `CD_PrepareLevel`
copies to the hard disk) and `copyL0..4.bat` (the `COPY` lines that stage them
and the movies to `D:\CD1`, `D:\CD2`) into the folder's root, over the folder's
`LISTL*.TXT` under their own names (matched case-insensitively, so Linux keeps
one file). `--tree` defaults to `WD_TREE`, else
`DREAMS_OUT/recomp/windream/developer`. `--check` prints, as information, the
listed files on no disc, on the wrong disc and missing from the folder (22 on
the shipped bank: retail lists them and skips them) and writes nothing unless
`--out` is given. Standard library only; it
runs under WSL `python3`. On disc 1's bank the lists are the shipped ones byte
for byte (`tests/recomp/test_mastering_lists.py`).

## Launcher and release

The executable started without arguments opens the launcher (`launcher/`):
choose the two disc images, change the port settings, press Play. It keeps
them in `dreams.ini` beside the executable and the game's files in `userdata/`
there; when that folder cannot be written to, both go to the per-user
directory (`%APPDATA%\DreamsToReality` on Windows).

| Option | What |
|---|---|
| `--play` | No window: validate the discs and start the game, or print the reason and exit with code 2 |
| `--disc1 PATH`, `--disc2 PATH` | The discs for this run (`.cue`, `.iso` or directory); not saved |
| `--data DIR` | The user data directory for this run; not saved |

The launcher and the host do not know each other. `launcher_run` returns `WD_*`
name and value pairs, and `main` (`windream/host/core/runtime.c`, the only code
that knows both) puts each into the process environment and starts the host as
in disc mode, with `--run` implied. A variable already set in the real
environment is kept, so it wins over `dreams.ini`. The log shows both cases:

```
[launcher] WD_DISC1=E:\discs\Dreams to Reality (Europe) (Disc 1).cue
[launcher] WD_SCALE=3 (set in the environment; the launcher's "4" is not used)
```

`main` runs the launcher only when the first argument is not an EXE path and
`WD_DISC1` is not set. `run.py` always gives one or the other, so it never
shows the launcher; neither do the difftests and verify hosts. `-DWD_LAUNCHER=OFF`
builds without it.

On Windows the C runtime's `getenv` reads a copy of the environment made at
start and does not see `SDL_setenv_unsafe`, which changes SDL's table and the
Win32 environment only. `main` therefore sets the C runtime's copy too
(`_putenv_s`, the same UTF-8 bytes) and reads every pair back through both; a
`[launcher] WARNING: NAME not visible to ...` line means a setting is ignored.
`main` receives UTF-8 arguments on Windows (`SDL_main.h`), and the EXE path
argument is opened as UTF-8 through SDL.

`release.py` builds the executable users get, in `out/recomp/windream/build-release`,
and stages `out/recomp/windream/release/DreamsToReality/` (`DreamsToReality.exe`,
`README.txt` and `resources/editor-tree.tsv`) and `DreamsToReality-<version>-windows-x64.zip` beside it. It fails if
the folder holds any other file or if the executable imports a DLL Windows
does not ship: the release links the C runtime and SDL3 statically (a second
SDL3 build, `out/recomp/sdl3/<commit>/install-mt`). The compiler flags are the
development build's; `--optimize` builds CMake's Release type, which has never
been verified on the lifted code.

The version is `git describe --tags --always` (`v0.1.0` on the tagged commit,
`v0.1.0-3-gabc1234` after it) or `--version`. It is compiled in as
`WD_VERSION`, whose `[*] Dreams to Reality port v0.1.0` line opens the log,
and it is in `README.txt` and the file names. Beside the zip, release.py keeps
the build's `DreamsToReality-<version>-windows-x64.pdb`. It is not published. A
user's machine has no symbols, so its crash report shows `exe+0x...` host
addresses. This PDB names them and gives a debugger the symbols for the
user's `crash-<pid>.dmp`.

Publishing a release (the GitHub Releases page; the asset is the zip alone):

```sh
uv run --with capstone --with pefile python recomp/windream/lift/lift.py   # gen/ from the tagged sources
uv run --with capstone --with pefile python recomp/windream/lift/gen_imports.py
git tag -a v0.1.0 -m "Dreams to Reality for Windows 0.1.0"
uv run --with pefile python recomp/windream/release.py
uv run --with pefile pytest tests/recomp/test_release.py
git push origin main v0.1.0
gh release create v0.1.0 --prerelease --notes-file <notes> out/recomp/windream/release/DreamsToReality-v0.1.0-windows-x64.zip
```

Before publishing, start the unzipped program with `--play` and both disc
images, and check `userdata\log.txt`.

The launcher's test-only machinery (the `DREAMS_LAUNCHER_SCRIPT` driver in
`launcher/testscript.cpp` and the variables `DREAMS_LAUNCHER_HOME`,
`DREAMS_LAUNCHER_SHOT`, `DREAMS_LAUNCHER_SHOT_TAB` and `DREAMS_LAUNCHER_VERBOSE`,
all documented in `launcher/launcher.h`) is compiled only with the CMake option
`DREAMS_LAUNCHER_TESTING` (default ON, so `launcher/build.py` and development
builds have it). `WD_RELEASE` forces it OFF: `testscript.cpp` is not compiled,
the hooks in `launcher.cpp` and `ui.cpp` are the empty inline stand-ins of
`launcher/testing.h`, and none of the variables is read. The release executable
has to be driven the way a user does: `--play`, `--disc1`/`--disc2`, `--data DIR`,
with `dreams.ini` beside the executable. `release.py` fails if the executable
holds the text `launcher-testing` (which the compiled-in code embeds) or if the
build's CMake cache does not say `DREAMS_LAUNCHER_TESTING` is OFF.

The release executable is a GUI-subsystem program (`WD_RELEASE` in
`CMakeLists.txt`): no console window. Its stderr and stdout go to `log.txt` in
the user data directory, opened once `WD_DATA_DIR` is known (output before
that, and a launcher error, is not logged: a failed `--play` shows a message
box instead, unless `WD_HEADLESS` is set). Once the file layer is up the data
directory is also the current directory, so `crash-<pid>.dmp` and `WD_SNAP_MS`
snapshots land beside `log.txt`.

## Development control channel

A live way into a running game for tests and for work at a shell: press a
game key, wait for a condition, save a frame, read guest memory, pause and
step, tail the file opens, record the mixer's output. Scripted runs
(`--keys 2000:ESC,...`) press keys at fixed times and are read afterwards from
the log; with the channel a test waits for what it needs and decides the next
step.

It is development-only and separate from the game:

- All of it is `windream/devtools/`. The host calls it through seven one-line
  hooks declared in `devtools/devtools.h` (included once, by `host/sdl/host.h`),
  which are empty inline functions unless the build defines `WD_DEVTOOLS`.
  Scripts that compile host files without CMake get the empty ones.
- The CMake option `WD_DEVTOOLS` is on for development builds and forced off by
  `WD_RELEASE`. `release.py` fails if the release executable holds the text
  `wd-devtools`, which the channel embeds, or if its CMake cache does not say
  `WD_DEVTOOLS` is off.
- Without `WD_CTL` in the environment a devtools build does nothing different:
  no socket, no thread, no log line.

`run.py --ctl [PORT]` sets `WD_CTL` (default 0: a free port). The host listens
on 127.0.0.1 only, writes the port to `<run dir>/ctl.port` and logs
`[ctl] listening on 127.0.0.1:<port>`. One client at a time.

```sh
uv run python recomp/windream/run.py --headless --discs --tag play --ctl   # in one shell (or in the background)
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play status
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play key ESC
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play wait_until opened generic.hnm --since 23   # 23: the "seq" the key answered with
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play screenshot out/tmp/x.bmp
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play read 0x661e04 4
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play project
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play quit
```

Each `wdctl.py` invocation is one connection and prints the answer as JSON. A
pause, a held key, a tap still to be released and an audio dump stay in force
when the client disconnects; an answer it was still waiting for is dropped.

From Python (`windream/debug/wdctl.py`, standard library only; used by
`tests/recomp/test_devtools.py`):

```python
# The process ends when the block is left.
with wdctl.start_game(tag="mytest", discs=True, headless=True) as game:
    ctl = game.ctl
    intro = ctl.wait_until_opened("intro.hnm", since=0)
    ctl.tap("ESC")
    ctl.wait_until_opened("generic.hnm", since=intro["event"])
    # {'name': 'Project0', 'level': 1, 'objet0': 'OBJET0', 'objet0_asset': 'H18ANGKR.DSN', ...}
    print(ctl.current_project())
```

`start_game` builds the command with `run.py`'s own `parse_args` and `prepare`,
so the run directory, the environment and the log (`<run dir>/stderr.txt`) are
those of `run.py`. What a guest address means (`current_project`: the record
behind the pointer at `0x661e04`) is the client's knowledge; the host side
knows keys, frames, memory, files and sound only.

The protocol is one JSON object per line each way: `{"id":N,"cmd":"...", ...}`
is answered by `{"id":N,"ok":true, ...}` or `{"id":N,"ok":false,"error":"..."}`.
Requests are flat (strings, numbers, booleans); a number may also be a string
such as `"0x661e04"`. Every `ok:true` answer carries `frame` (frames presented
since start), `ms` (host time since start) and `seq` (the number of the last
recorded event). The listener thread only queues requests; they run on the
host's main thread at `host_pump`, where SDL events are drained.

| Command | Arguments | Answer, and what it does |
|---|---|---|
| `ping` | | `build`, `render_audit` |
| `status` | `opens` (how many, default 8) | `headless`, `renderer`, `disc_mode`, `disc` (active disc, 0 outside disc mode), `paused`, `audio_dump`, `cd_track`, `cd_disc`, `cd_state` (`playing`, `paused`, `stopped`), `opens` (the last file opens, as in `log`) |
| `key` | `name` (a `WD_KEYS` name), `action` `down`, `up` or `tap` (default), for a tap `frames` or `ms` (default 150 ms) | `vk`. A game key, read where a `WD_KEYS` key is: not remapped by `WD_KEYMAP`. Unlike in a `WD_KEYS` script, `F11` and `KP1`-`KP5` are plain game keys here, not the host's fullscreen and debug toggles |
| `type` | `key` (an SDL scan-code name: `A`, `1`, `-`, `'`, `Keypad 2`, `F10`, `Space`, `Escape`), `text` (what the layout would type, optional), `mods` (`shift`, `ctrl`, `alt` joined by `+`), `ms` (hold, default 150) | `scancode`. A key as a keyboard delivers it: SDL key events (the modifiers first), the text, the release after `ms`, all through `host_pump`, so `WD_KEYMAP`, the keypad toggles and, in Develop, the DOS keys apply (spec 008 phase 2). The host never derives `text` from the key: pass `"A"` with `shift` for Shift+a |
| `mouse` | `x`, `y` (game pixels, mapped through the software renderer; `client`: 1 for window pixels as given, the direct renderer), `action` (`move`, `down`, `up`, `click`: down, the release after `ms`, default 150), `button` (`left`, `right`), `dx`, `dy` (a move's relative deltas, 32-bit two's complement: Develop's free camera) | `window_x`, `window_y`. The pointer as a mouse delivers it: SDL motion and button events through `host_pump`, so in Develop the editor's cursor and buttons (spec 008 phase 3). The editor sees a press on the frame after it reaches the pump: for a one-frame click, `step` once, then `mouse down`, `step`, `mouse up`, `step` (`game_nav.page_click`) |
| `wait` | `frames` and/or `ms` | Answers after that many presented frames or host milliseconds, the first to pass: `waited_frames`, `waited_ms` |
| `wait_until` | `cond` and its arguments, `timeout_frames` and/or `timeout_ms` (60000 ms when neither is given) | Answers when the condition holds, or with the error `timeout after N frames (M ms)`. Checked at every pump, at least once per frame |
| | `cond: "mem"`, `addr`, `size` 1, 2 or 4 (default 4), `op` `==` `!=` `<` `>` `&`, `value` | `value`: what was read. Unsigned compare; `&` is "any of these bits set"; an address that is not mapped is not true |
| | `cond: "opened"`, `path` (part of a guest path, case ignored), `ok` (optional: the open succeeded or failed), `since` (optional event number) | `event`, `path`, `host`, `open_ok`. A matching open after the command was issued, or after event `since`: pass the `seq` of an answer received before the key that causes the open |
| | `cond: "disc"`, `"cd_track"` or `"frame"`, `value` | The active disc is `value`; the CD's current track is `value`; the frame counter has reached `value` |
| `read` | `addr`, `size` (at most 65536) | `hex`. Guest memory; an error unless every page is committed (`vm_state`) |
| `read_cstr` | `addr`, `max` (default 256) | `text`, `length` |
| `write` | `addr`, `hex` | `size`. For test setup, as `WD_POKE` is |
| `screenshot` | `path` | `path`, `width`, `height`, `format`. The next presented frame, written by the host's snapshot code (`WD_SNAP_MS`): a 24-bit BMP of the game's frame with the software renderer, a PNG of the window with the direct one, whatever the name's extension. While paused it steps one frame. The limits are the snapshot code's: with the direct renderer a hidden (headless) window can be captured on the D3D11 backend only, and elsewhere the host stops with `FATAL: hidden frame capture currently requires the D3D11 backend` |
| `pause`, `resume` | | Pause holds the guest inside `host_pump`; the channel is still served and SDL keeps the window alive (a close request resumes) |
| `step` | `frames` (default 1) | Lets that many frames be presented, pauses again, then answers |
| `log` | `since` (event number, default 0), `max` (default 100) | `events`, `dropped`. File opens (`kind: "open"`: `path`, `host`, `write`, `ok`), CD changes (`"cd"`: `text` such as `play track 9 (disc 1)`) and changes of the active disc (`"disc"`), each with `seq`, `ms`, `frame`. A ring of the last 512; every open is recorded, also those past the `WD_FILES_LOG` budget |
| `audio_dump` | `path` | Writes the mixer's output (44.1 kHz 16-bit stereo, what goes to the audio device) to a WAV file from now on; works muted and headless, where the mixer runs on a timer |
| `audio_dump_stop` | | `path`, `frames`, `rate` |
| `trace` | `path`, `frames`, `ranges` (`"va:size,..."`, sizes up to 64) | After each of the next `frames` presents, one line in `path`: frame, host nanoseconds, each range as hex (`-` when unmapped). The game is not paused, so the timing is its own; answers at once |
| `display_shot` | `path` | The next display interpolation frame (`WD_INTERPOLATE`), not the game's own, as a PNG from the swapchain; answers at once, the file follows with that frame |
| `overlay_shot` | `before`, `after` | Develop, in a level: the game frame of the next gameplay frame just before the editor's call and the tools' overlays (`user.c` `wd_editor_frame`) and just after them, as 24-bit BMPs: the guest frame with the software renderer, the GPU frame read back with the direct one. Their difference is exactly what Develop drew (`tests/recomp/test_editor_direct.py`). While paused it steps one frame; fails after 120 frames without a gameplay frame |
| `quit` | `code` (default 0) | Ends the process as `main` does when the game returns: trace flushed, renderer closed, `exit(code)` |

Time is not paused: `timeGetTime` is the host's clock (`SDL_GetTicks`), and the
mixer has its own thread. During a pause the guest's clock therefore keeps
running and sound and CD music play on while the picture stands still; the
frame after a pause sees the whole pause as elapsed time (how each of the
game's timers takes that has not been examined).

### MCP server for the control channel

`windream/debug/wd_mcp.py` wraps `wdctl.py` as an MCP server (stdio), so an AI
coding assistant can drive a running game as tools: start one, press keys, wait
for a file to open or a memory cell to change, read guest memory, look at a
frame. It has no game logic of its own: each tool is one call into `wdctl`
(`start_game`, `Ctl`), and it needs the same development build with
`WD_DEVTOOLS` as the channel. It exists for development only; nothing in the
game or the release depends on it.

In Claude Code the repository's `.mcp.json` registers it as the project-scope
server `dreams-game` (Claude Code asks once to approve a project server; the
relative path is resolved from the repository root, where `claude` is started):

```json
{"mcpServers": {"dreams-game": {"command": "uv", "args": ["run", "--with", "mcp", "--with", "pillow", "python", "recomp/windream/debug/wd_mcp.py"]}}}
```

Any other MCP client runs the same command over stdio. `mcp` is the official
Python SDK (1.x `FastMCP` and 2.x `MCPServer` both work); Pillow only turns the
software renderer's BMP into a PNG for `game_screenshot`.

| Tool | What |
|---|---|
| `game_start(tag, discs=True, headless=True, renderer=None, extra_env={})` | Start a game as `run.py` does (run directory `out/recomp/windream/run-<tag>`) and keep it; one at a time. Returns `port`, `run_dir`, `pid` |
| `game_attach(run_dir=None, port=None)` | Attach to a game already running with `run.py --ctl` |
| `game_stop()`, `game_status()` | End the game this server started (an attached one is only disconnected); the host's status and whether the process is alive |
| `game_key(name, action="tap", frames=None)` | A game key: tap, down or up |
| `game_type(key, text=None, mods=None, ms=None)` | A key typed through the host's key path (the `type` command; Develop's DOS keys) |
| `game_mouse(x, y, action="click", button="left", ms=None, dx=None, dy=None)` | The mouse through the host's mouse path (the `mouse` command; Develop's editor) |
| `game_wait(frames=None, ms=None)`, `game_wait_until(cond, value, addr, op, size, path, since, ok, timeout_ms, timeout_frames)` | Wait; `cond` is `opened`, `mem`, `disc`, `cd_track` or `frame`, 60 s timeout by default |
| `game_read(addr, size)`, `game_read_cstr(addr)`, `game_write(addr, hex)`, `game_project()` | Guest memory (addresses are ints or hex strings) and the current project record |
| `game_log(since=0)`, `game_stderr(tail=50)` | File-open and CD events; the last lines of the run's `stderr.txt` |
| `game_pause()`, `game_resume()`, `game_step(frames=1)` | Hold, release, single-step |
| `game_audio_dump(path)`, `game_audio_dump_stop()` | Record the mixer's output to a WAV file |
| `game_screenshot()` | The next frame, saved as a PNG under `<run dir>/mcp-shots/` and returned as an image with its path |

An error from the channel comes back as a tool error with the channel's message.
The game the server started is ended when the server exits (`wdctl.Game`'s
`atexit` and, on Windows, a job object).
`tests/recomp/test_wd_mcp.py` (run with `uv run --with mcp --with pillow pytest
tests/recomp/test_wd_mcp.py`) lists the tools and runs one short headless
session; it is skipped without `mcp`, the development build or the discs.

### Tools built on the channel

```sh
uv run python recomp/windream/debug/bank_patch.py list
uv run python recomp/windream/debug/bank_patch.py show 26
uv run python recomp/windream/debug/bank_patch.py write out/recomp/windream/run-play/sandbox --copy 116:0
```

- `debug/bank_patch.py` writes a modified `DREAMS.DAT` (as `dreams.dat`) into
  a run's data directory, where it overrides the disc's. `--copy SRC:DST` puts
  one project's record in another slot (slot 0 is where a new game starts),
  `--spawn-in-link SLOT:DEST` moves the spawn into a link's box,
  `--link SLOT:LINK:DEST` repoints a link, `--set32` writes a field. It
  refuses to write outside `out/`.
- `debug/motion_trace.py` boots into the first level, walks, and measures
  motion over a `trace`: ticks and frame delta per frame, Duncan's speed, the
  chase camera's wobble; with `--smooth` also the display frames
  (`WD_INTERP_TRACE`): refresh spacing, how far the shown game step is from
  even motion, and the shown camera's speed changes. `--visible` (muted, focus
  kept) gives real vsync; a window covered by another one takes no frames.
- `debug/cd_audio_check.py` aligns a mixer dump (`audio_dump`) with a track of
  the disc image: `align()` needs only the standard library, `locate()` and
  the CLI need numpy.
- `debug/game_nav.py` gets through the game's menus by waiting on file opens
  and guest variables, not on times: `boot_into`, `new_game`,
  `open_game_menu`, `open_load_list_in_game`, `open_load_list_from_main_menu`,
  `slot_names`, `load_slot`, and readers of the event log (`events_after`,
  `disc_changes`, `opens`). Import `wdctl` through it (`nav.wdctl`).
- `tests/recomp/test_disc_play.py` uses all of the above: disc 2 levels, the
  disc-changing link, saves across discs, music.

```python
import sys

sys.path.insert(0, "recomp/windream/debug")
import game_nav as nav

with nav.wdctl.start_game(tag="play", headless=True) as game:
    nav.boot_into(game.ctl, "H18ANGKR.DSN")  # new game, first level loaded
    nav.open_load_list_in_game(game.ctl)
    print(nav.slot_names(game.ctl))
```

`AGENTS.md`, "Checking a change in the running game", maps each need to its
tool and test file.

## Browser build

The game also runs in a browser: the lifted C and the host runtime built for
WebAssembly (Emscripten, pthreads), the renderer on WebGL 2, and a small web
shell that fetches a demo pack of game data and starts the game. No disc is
needed; the pack is a few files assembled by the demo stream
(`recomp/web/demo/`). Contract between the pieces: `recomp/web/CONTRACT.md`;
what the shell expects of the engine build: `recomp/web/NOTES-page.md`.

```powershell
. ./recomp/web-env.ps1                           # Emscripten, CMake, Ninja (see "Browser tool environment")
# engine: out/recomp/windream/build-web/dreams.{js,wasm}   (WASM stream)
# pack:   out/recomp/web/demo/manifest.json + chunks       (DEMO stream)
python recomp/web/package.py                     # -> out/recomp/web/dist (page, engine, pack, _headers)
python recomp/web/serve.py                       # http://127.0.0.1:8765/ with the same headers as Pages
```

The page starts the download as soon as it loads and shows its progress; the
click on "Click to play" unlocks audio and starts the game (`?autostart` skips
the click). The shell is `recomp/web/index.html` and `loader.js`, with no
framework and no build step. It checks cross-origin isolation and says what is
missing when `SharedArrayBuffer` is not available; fetches `manifest.json`
(`?demo=<manifest or base url>`, else `window.DREAMS_DEMO_BASE`, else `demo/`),
downloads the chunks four at a time, checks each file's sha256 and keeps it in
the Cache API keyed by the manifest, so a reload starts without a download. In
`preRun` it writes the files under `/dreams`, sets `WD_INSTALL_ROOT`, mounts
IDBFS at `/dreams/DATA/GAME` for saves and syncs it every 5 seconds and on
`pagehide`. Game keys are not passed on to the browser (no scrolling, no menu
bar on Alt); the HUD has mute and fullscreen (the page asks for Keyboard Lock
so that Esc, the game's menu key, does not leave fullscreen). `?debug` shows
the log; `window.dreamsPage` holds the state.

The page is a tribute with the original manual's story, artwork, team photograph
and full credits. Prepare its images from the 33-page Spanish manual before
packaging (the PDF and extracted images remain under `out/`, never committed):

```sh
uv run --with pypdf --with pillow python recomp/web/manual_assets.py /path/to/manual-es.pdf --box /path/to/box-front.jpg
```

The `--box` JPEG is copied unchanged for the demo's start screen. Subsequent
manual extractions retain that image when `--box` is omitted.
`package.py` copies `out/recomp/web/manual/` into `site/manual/`; use
`--manual DIR` for another prepared asset folder. The page's look, sources,
controls overlay and in-game key handling are described in
`recomp/web/NOTES-design.md`.

### Publish with the Cloudflare CLI

The tribute is hosted at **https://dreams.lagedo.dev/** as the assets-only
Worker `dreams-to-reality`. Use the authenticated `cf` CLI (currently tested
with 1.0.0-beta.12). Package the release engine, then deploy:

```sh
uv run python recomp/web/package.py --engine out/recomp/windream/build-web-release
uv run python recomp/web/deploy.py
```

`deploy.py` copies the packaged site into cf's prebuilt output under
`out/recomp/web/cloudflare/.cloudflare/output/v0/` and invokes
`cf deploy --prebuilt` there. The custom-domain configuration provisions
`dreams.lagedo.dev`, its proxied DNS record and HTTPS certificate. The
existing `_headers` rules supply the isolation headers needed by the game.
It uploads the local release and game-derived data, which remain under `out/`.
Use `--prepare-only` to generate output without publishing, or `--name`,
`--domain` and `--account-id` to select a different deployment target.

### Legacy Cloudflare Pages hosting

Deploy the release engine, not the development one: `--web-release` builds
`out/recomp/windream/build-web-release` at Release (-O3) and leaves out the
verification tools' frame capture (`WD_WEB_CAPTURE`). Check it, then deploy:

```sh
uv run python recomp/windream/build.py --web-release
uv run python recomp/web/package.py --engine out/recomp/windream/build-web-release
grep -c wd_web_capture out/recomp/web/dist/dreams.js   # 0: no capture in the release
wrangler pages deploy out/recomp/web/dist --project-name <name>
```

`package.py` writes the `_headers` file Pages needs:

| Path | Headers |
|---|---|
| everything | `Cross-Origin-Opener-Policy: same-origin`, `Cross-Origin-Embedder-Policy: require-corp` (threads need both), `Cross-Origin-Resource-Policy: same-origin` |
| `/`, `index.html`, `loader.js`, `config.js` | `Cache-Control: no-cache` |
| `dreams.js`, `dreams.wasm` | `max-age=0, must-revalidate`; `Content-Type: application/wasm` for the wasm |
| `demo/*` | `max-age=31536000, immutable` (the page requests `<chunk>?v=<manifest hash>`, so a new pack has new URLs); `demo/manifest.json` is `no-cache` |

Pages rejects any file over 25 MiB and more than 20000 files; `package.py`
warns for both. The demo chunks are at most 20 MiB each. Pages adds the headers
of all matching rules, which is why Cache-Control is only set in specific rules
and detached (`! Cache-Control`) for the manifest. `serve.py` reads the same
`_headers` file, so what it serves is what Pages will send; it also supports
Range, `--gzip`, and `--cors` (to play an asset host, see below).

### Pack on R2 (or any other host)

If the pack is too big for Pages or should be shared between deploys, leave it
out of the folder and point the page at the bucket:

```sh
python recomp/web/package.py --demo-url https://pack.example.com/dreams-demo/
wrangler pages deploy out/recomp/web/dist
# upload out/recomp/web/demo/* to that URL, for example with rclone, or per file:
wrangler r2 object put <bucket>/dreams-demo/manifest.json --file out/recomp/web/demo/manifest.json
```

The page is cross-origin isolated (COEP `require-corp`), so the bucket must let
it read the assets: put a CORS rule on the bucket allowing `GET` from the page's
origin (`Access-Control-Allow-Origin: https://<your pages domain>`, or `*`),
and, for assets that are embedded rather than fetched, serve
`Cross-Origin-Resource-Policy: cross-origin`. The loader fetches in CORS mode,
so the CORS header is what matters; give the bucket's custom domain a cache
rule for long caching of the chunks and no caching of `manifest.json`. The
engine itself (`dreams.js`, `dreams.wasm`, workers) stays on the page's origin:
workers must load from it. `serve.py out/recomp/web/demo --port 8766 --cors`
plays the bucket locally: open
`http://127.0.0.1:8765/?demo=http://127.0.0.1:8766/manifest.json`.

The owner who deploys a pack is responsible for having the right to
redistribute the game data in it: the repository ships none, and `out/` is
never committed.

### Checking the shell

```sh
uv run --with playwright python -m playwright install chromium     # once
uv run --with playwright python recomp/web/browser_check.py            # real build if built, else the mock
uv run --with playwright python recomp/web/browser_check.py --real --run-seconds 20
uv run --with playwright pytest tests/recomp/test_web_page.py          # skips without Playwright, a browser or the mock
python recomp/web/mock/build_mock.py                                   # mock engine (emcc) and fake pack, under out/recomp/web/mock
```

`browser_check.py` packages, serves and opens the page in headless Chromium and
checks: cross-origin isolation, progress and sha256 verification, the files in
`/dreams`, the game starting, a pthread running (mock), keys not reaching the
browser, mute, saves surviving a reload (IDBFS), the cache serving a reload
without requests, a damaged chunk and a missing COOP/COEP being reported
clearly, and a pack on another origin. The mock (`recomp/web/mock/`) is a
real Emscripten module that lists `/dreams` on the canvas; screenshots go to
`out/recomp/web/check/`.

### Render parity with the Windows build

`recomp/windream/verify/render_parity.py` compares the browser's 3D frame with
the Windows build's from the same guest state, without anyone playing:

```sh
uv run --with playwright --with pillow python recomp/windream/verify/render_parity.py all
uv run python recomp/windream/verify/render_parity.py capture --projects 0,62 --shots 4
uv run --with playwright --with pillow pytest tests/recomp/test_render_parity.py
```

- **capture** (Windows build, needs the discs): starts the game in each demo
  project and, at paused frames, writes through the control channel the scene
  inputs the renderer was about to draw (`scene_capture`, a `.wds` file), the
  guest's committed memory (`memory_dump`, a `.wdmi` file) and a screenshot.
  It pauses before arming the scene request, then uses a screenshot request to
  advance one presentation. The scene and PNG share that presentation; memory
  is dumped afterward while paused. A `.capture.json` records the frame, render
  root, file hashes and capturing executable. Missing requested shots fail the
  capture command rather than producing a successful partial corpus.
- **build**: `verify/native/render_parity.cpp`, the production scene adapter
  and renderer behind `od::GraphicsBackend`, for Windows (D3D11) and for the
  browser (WebGL2).
- **run**: every case on both, then two comparisons. Data: the adapter
  captures the scene from the memory image on both hosts and the two snapshots
  must be the same bytes (a 64-bit host against wasm32). Pixels: the 640x480
  frames; a pixel fails beyond `--tolerance` (2), a case beyond `--budget`
  (0.2% of the pixels). Failing or differing cases get an image in `report/`:
  Windows, browser, difference.
  Reports record the thresholds, input/output hashes, replay executable hashes
  and source contents at comparison time. Existing cases without capture records
  remain usable, with their capture provenance unknown. Browser execution errors
  fail pytest; only explicit missing prerequisites or missing Chrome skip it.

Everything is under `out/recomp/render-parity/` (game-derived). It covers the
3D scene pass of a display frame. It does not cover the 2D interface, captions
and movies, the shadow and thumbnail passes, or the lifted game code itself:
a difference in what the game computes shows up as different inputs, which
this test takes from Windows.
The live scene also includes host-owned palettes and fog that a plain memory
replay omits. Scene inputs precede submission/callback effects, and the memory
dump follows presentation. Agreement between backends for either input kind
does not establish agreement between those two inputs or with the saved PNG.

### The browser's own frames

`recomp/windream/verify/render_web_capture.py` takes frames from the browser
build itself, with nobody playing: the demo pack with a `DREAMS.DAT` whose slot
0 is the project (`bank_patch.Bank.copy`), packaged and served as for a deploy,
the page with `?autostart` in Chrome on the GPU. After the level's autosave the
page asks the engine for a frame (`Module._wd_web_capture()`, compiled in with
CMake `WD_WEB_CAPTURE`, on by default): the scene inputs, the frame as
presented and the committed guest memory, in render_parity's order.

```sh
uv run --with unicorn --with pillow --with playwright \
    python recomp/windream/verify/render_web_capture.py all --project 46
```

`analyze` runs the pose sweep below on the browser's memory, compares the level
with the Windows build's memory image of the same project node by node, and
the live frame with its own scene inputs replayed on D3D11 and WebGL2 (inside
the camera's viewport). Output: `out/recomp/web-capture/`.

`spawns --count 16` starts both builds at spawn points spread over the level's
floors (`Bank.set_spawn`, at the record's height over its floor), one browser
session and one Windows run (control channel) each, and checks every pair.
Its last step, also `same-camera` alone, sets the browser's camera in both
memory images (the builds' cameras turn at their own pace) and compares what
retail draws in each, face by face, and the two frames: a level face drawn by
one build only, or a region dark in the browser and lit on Windows, is game
state that differs. Faces of nodes whose transform differs between the two
(actors at their own animation phase) are counted apart.

### Camera-pose sweep against retail

`recomp/windream/verify/render_pose_sweep.py` takes those memory images to
camera poses nobody played: it writes a pose into the image, runs retail's
`REND_DrawScene` on Unicorn and takes the faces retail draws (the visible lists
after `REND_CullFaces`, plus `REND_ClipFaceNear`), and compares them with the
faces the direct renderer keeps for the same memory (the native adapter, then
its far flag, Glide's types, the near clip, the GPU back-face test and the
viewport, on the CPU):

```sh
uv run --with unicorn --with pillow python recomp/windream/verify/render_pose_sweep.py \
    out/recomp/render-parity/cases/p046-0.wdmi --poses 300
uv run --with unicorn --with pillow --with playwright python recomp/windream/verify/render_pose_sweep.py \
    out/recomp/render-parity/cases/p046-0.wdmi --poses 40 --browser 40
uv run --with unicorn --with pillow pytest tests/recomp/test_render_pose_sweep.py
```

Poses stand over the level's horizontal faces that have a roof, at the
captured camera's height, in eight directions and four pitches. A face retail
draws that direct drops is reported with the stage that dropped it and its
pixels in the 3D viewport; a pose fails at `--fail-pixels` (64). `--browser N`
runs the first N posed images through the parity harness above (wasm32 scene
bytes and WebGL2 pixels against Windows). Reports and images are under
`out/recomp/pose-sweep/<image>/`. The adapter sees node transforms composed for
the pose's camera; a live capture that reads the previous frame's is not
modelled. It compares which faces are drawn, not their colours: a surface drawn
black (palette, shade, fog) passes.

## Layout

| Path | What |
|---|---|
| `windream/devtools/` | The development control channel (`WD_CTL`): `devtools.h` (the hooks the host calls, empty without `WD_DEVTOOLS`), `devtools.c` (commands), `ctl_net.c` (the TCP listener), `ctl_json.c`. Not in a release build |
| `launcher/` | The launcher library (`launcher.h`, one entry point): `dreams.ini`, disc validation and the Dear ImGui window; `launcher_demo`, `build.py`. Linked into the host (`dreams_launcher`) |
| `windream/release.py` | The release build, its checks and the zip |
| `disc/` | The disc library (`disc.h`): opens a `.cue`, `.iso` or extracted directory, reads its ISO 9660 files and lists its audio tracks; `disc_list` tool, `build.py`. Linked into the host (`dreams_disc`) |
| `recomp_env.py` | Shared paths (output root, pcrecomp, `LIFT`, `HOST`, `HOST_DIRS`, `DISC`), the build environment (Visual Studio via vcvarsall on Windows), the shared SDL3 build, configure + build |
| `windream/lift/lift.py` | `bounds.csv` → `gen/`: lift32 plus the lifter fixes this game needed (flags at block starts, patched immediates, `push label; ret`, x87 and narrow mul/div), diagnostic `HOOKS` and `PROBES`, and `CALLS` (runtime calls inserted before an instruction, such as the editor draw of spec 005) |
| `windream/lift/bounds.csv` | Function bounds exported from Ghidra with pcrecomp's `DumpBounds.java` |
| `windream/lift/gen_imports.py` | One bridge per import; stubs for those no `host/*/*.c` implements |
| `windream/lift/replacements.py`, `render_audit.py`, `render_bulk.py` | Generated entry wrappers for the renderer boundary, guest-memory probes and GPU-aware bulk transfers |
| `windream/host/core/` | Guest runtime (`runtime.c`), function-entry trace, crash report and minidumps, `recomp_types.h`, `imports.h` |
| `windream/host/sdl/` | The Win32 the guest sees, on SDL3 (`host.h`): `files.c` (read roots or the two discs, write sandbox, case-insensitive names, `FindFirstFile` wildcards, file times), `kernel.c` (process, console, code pages 1252 and 437, last error), `threads.c` (handles, threads, events, critical sections, TLS), and `user.c`, `gdi.c`, `winmm.c`, `dsound.c` for USER32, GDI32, WinMM and DirectSound; `guest_win32.h` holds the guest's Win32 constants and 32-bit layouts, which `win32_abi_check.c` checks against the SDK |
| `windream/host/vm/` | The guest's virtual memory: `vm_front.c` (`VirtualAlloc`/`VirtualFree`/`VirtualQuery` bridges and `vm_state`, the one way host code asks what is at a guest address), `vm_win32.c` (the original, on Windows page state), `vm_ledger.c` (the portable one, over the `vm_os_*.c` layer), `vm_shadow.c` (both, compared); chosen with `build.py --vm` |
| `windream/host/render/` | Adapters between the lifted game and the GPU renderer: boundary and surface ownership, scene capture and draw, UI, movies, live frame, metrics, hooks |
| `windream/host/hooks/` | `phys_hook.c` collision hooks |
| `windream/verify/` | Renderer verification: retail-x86 oracles (`render_*_smoke.py`), live isolated runs (`render_*_live_smoke.py`, project and thumbnail smokes), `render_acceptance.py`, `render_content_inventory.py`, `direct_render_validate.py`, `test_render_codegen.py`, and the dump reader `mdmp.py`; and `kernel_bridge_smoke.py`, the retail-x86 oracle for the KERNEL32 bridges. The scripts import each other by name, so they share one directory; their C/C++ sources are in `native/` |
| `windream/debug/` | Collision and dump tools: the collision invariant, Unicorn replay of one `PHYS_SweepAxis` call, `x86dis.py`, `flag_hunt.py` (unwritten debug flags, address-copy scan); `wdctl.py`, the client of the development control channel; `wd_mcp.py`, its MCP server |
| `web/` | The browser build's web shell: `index.html`, `loader.js`, `package.py` (-> `out/recomp/web/dist` with the Cloudflare `_headers`), `serve.py`, `browser_check.py`, the mock engine in `mock/`; `demo/` builds the demo pack. See "Browser build" |
| `windream/CMakeLists.txt`, `build.py`, `run.py` | Build (Ninja; clang-cl on Windows, gcc or clang elsewhere) and sandboxed run (scripted keys, snapshots, fps cap, window, pad and dump modes) |
| `render/` | GPU renderer: `ODRender` (sokol_gfx core: `direct.*`, shadows, fog) and `ODGraphics` (SDL3 backends for D3D11, Metal, OpenGL); pinned dependencies and shader generation in `cmake/`; tests in `tests/` |
| `difftest/` | `difftest.py` (compile with Watcom, bounds with Ghidra cached per source/flags/compiler, lift, build, run, diff), `wat.py`, the test programs `t_core.c` and `t_switch.c`, `gen_insn.py` (generates `t_insn.c` into the work root), `coverage.py`, `flagdiff.py`, `consumers.py`, `map2bounds.py` |

## KERNEL32 bridge oracle

`windream/verify/kernel_bridge_smoke.py` runs original `GDIDREAM.EXE` code in
Unicorn against the production bridges (`host/sdl/files.c`, `kernel.c`,
`threads.c` and `host/vm/`), built into a DLL with the production
runtime setup. The guest arena is mapped into Unicorn, so both sides see the
same memory.

```sh
uv run --with unicorn --with capstone --with pefile python recomp/windream/verify/kernel_bridge_smoke.py
uv run --with unicorn --with capstone --with pefile python recomp/windream/verify/kernel_bridge_smoke.py --case-walk
```

- What runs: the Watcom startup from the entry point to the call of `WinMain`;
  the runtime's `open_`, `read_`, `write_`, `lseek_`, `fopen_`, `fread_`,
  `_dos_findfirst_`, `rename_`, `getcwd_`, `getenv_`, `malloc_` and others; the
  game's `CD_InitPaths`, `VFS_Open`/`Read`/`Seek`/`Close`, `FILE_Exists` and
  `SYS_CreateInstanceMapping`; and `push ...; call [slot]` stubs for the
  imports no convenient retail caller reaches (events, waits, TLS, code-page
  conversion, file times, each `CreateFileA` disposition).
- Guest threads run on the host thread the bridge creates, each in its own
  Unicorn instance; three of them increment one counter under a critical
  section.
- A plain run compares its observations with
  `verify/kernel_bridge_baseline.json`. That baseline was captured from the
  Win32 implementation of the three bridge files: the commit before they moved
  to `host/sdl`, checked out in a worktree and built with `--host TREE`. A
  pass therefore says the SDL3 bridges give original code the answers Win32
  gave. `--capture` replaces the baseline with the current run: only for a
  deliberate change of behaviour.
- `--case-walk` forces the per-segment case-insensitive path lookup that a
  case-sensitive host file system takes.
- Checked in place on every run, independent of the baseline:
  - the number of argument slots a bridge pops, against the Windows SDK
    import libraries;
  - file times against the fixture's own, and local times against the
    machine's zone;
  - 1,489 generated wildcard patterns, the bridge's listing against
    `FindFirstFileW` on the same directory;
  - paths longer than `MAX_PATH`, a conversion with a size but no
    destination, a second open of a file being written, calls on a closed
    handle, a closed standard handle, a timeout above 2^31 ms.
- What the comparison cannot see: the last-error value is compared only where
  Win32 documents it (the failures of the file, find, conversion and
  file-time calls, and always after `CreateFileA` and `CreateFileMappingA`).
  Elsewhere the script restores the value from before the call, so a bridge
  that leaves a different error after `CloseHandle`, a wait or a TLS call is
  not noticed.
- Not covered: `ExitProcess` and `ExitThread` (they would end the test
  process), `CREATE_SUSPENDED`, two threads using and closing one handle at
  the same time, non-ASCII file names, a real case-sensitive file system.
- `--vm win32|ledger|shadow` builds the DLL with another virtual memory
  implementation; the baseline came from the Win32 one, so the others must
  give the same observations.
- Windows only, because of the DLL and the clang-cl build: the host DLL asks
  `vm_state` for the committed ranges Unicorn maps. Outputs go to
  `DREAMS_OUT/recomp/kernel-bridge`.

Result (2026-10-01): 287 observations, none different between the Win32 and
the SDL3 bridges, with and without `--case-walk`. Intended differences from
the Win32 implementation, which the comparison leaves out or cannot see:

- `CP_ACP` and `CP_OEMCP` convert as code pages 1252 and 437, the ones
  `GetACP` and `GetOEMCP` report to the guest. Under Win32 they followed the
  machine's code page.
- `GetLastError` is the guest's own per-thread value. Under Win32 it was the
  host thread's, which the host's own calls could change. `CloseHandle`,
  `ReadFile`, `WriteFile`, `SetFilePointer`, `FindNextFileA`, `SetEvent` and
  `WaitForSingleObject` now set `ERROR_INVALID_HANDLE` for a handle that is
  not one; under Win32 the bridges failed and left the last error as it was.
- The single-instance mapping (`CreateFileMappingA`) is counted inside the
  process: two running copies of the game no longer see each other.
- Share modes are SDL's, not the guest's. On Windows a file open for writing
  cannot be opened again, and a file open for reading cannot be opened for
  writing. Such an open fails with `ERROR_SHARING_VIOLATION` and logs
  `[files] open "..." refused`; no run so far has logged one.
- Files report `FILE_ATTRIBUTE_ARCHIVE` only (no read-only bit) and no 8.3
  alternate name, so a wildcard never matches through a short name. Windows'
  best-fit character substitutions are not reproduced.
- `OPEN_ALWAYS` without write access resolves in the sandbox. Under Win32 it
  could create the file in a read root.
- A path that, with the current directory, is longer than `MAX_PATH` fails
  with `ERROR_FILENAME_EXCED_RANGE`. Under Win32 it overran a stack buffer in
  the bridge.
- A closed standard handle keeps its number. Under Win32 the number could be
  given to the next file opened, which then received the guest's console
  output.

Known gaps, unchanged from the Win32 bridges:

- The sandbox and the read roots are not merged. `FindFirstFileA` answers
  from the first of them with a match, and a directory that exists in the
  sandbox always has one for `*.*` (its `.` entry), which hides the read
  roots' files in that directory. After no match the error is the last read
  root's.
- `CREATE_NEW`, `CREATE_ALWAYS`, `DeleteFileA` and `MoveFileA` look for an
  existing file in the sandbox only, so a file in a read root does not count
  as existing for them.
- `SetCurrentDirectoryA` accepts a directory that does not exist;
  `GetCurrentDirectoryA` and `GetModuleFileNameA` return the full length
  whatever the buffer size; `FindClose` returns 1 for any handle.
- `ExitThread` on a thread the bridge did not start ends the process. That
  is the main thread; nothing is known to run guest code on any other.

## Renderer-boundary smokes

`windream/verify/render_smoke.py` replays original x86 on full retail-process
dumps, checking the proposed modern renderer cut against the original front
end. `--lifted` also compiles an isolated replacement-wrapper experiment from
the current generated functions; `--gpu` tests offscreen sokol/D3D11 depth,
orientation and RGB565 readback. Neither changes the production recomp.

```sh
uv run --with unicorn python recomp/windream/verify/render_smoke.py --lifted --gpu out/scratch/retail-gdidream-222659.dmp out/scratch/retail-gdidream-223423.dmp
```

The input dumps are local game-derived artifacts, not repository fixtures.
Generated code, replay inputs and JSON reports go to `DREAMS_OUT/recomp/render-smoke`.
The native compilation/GPU portions currently require Windows and the existing
pinned sokol checkout (`--sokol-dir` overrides its location). An optional
`--shadow-dump` and `--shadow-arena` check the real-shadow mask on a captured
state. See [the design and measured limits](../docs/specs/006-recomp-glide-renderer/modern-cut.md#smoke-results-2026-09-29).

The second boundary has its own pixel oracle and integer GPU compositor:

```sh
uv run --with unicorn python recomp/windream/verify/render_2d_smoke.py --gpu
```

It uses a local retail dump (`--dump`) and an existing decoded movie frame
(`--movie-rgb565`), creates ordered drawing fixtures, and compares every GPU
checkpoint with original x86 results. `--tag` keeps another run in a separate
output directory. Results and generated fixtures live under
`DREAMS_OUT/recomp/render-2d-smoke`. Validation readbacks are separate from the
composition benchmark, which uses GPU copies and destination-sampling passes.
Coverage, fallback requirements and timings are in the
[GPU 2D design](../docs/specs/006-recomp-glide-renderer/2d-cut.md).

## Shared direct-renderer implementation

The shared `ODRender` / `ODGraphics` targets, generation-checked GPU resources,
ordered RGBA8 compositor and basic posed-scene pipeline are implemented. The
game defaults to the direct renderer on Windows and to software rendering
elsewhere; `run.py --renderer software` selects the original. Full R0–R4 gates
remain open. See [implementation status](../docs/specs/006-recomp-glide-renderer/implementation.md).

Build and check the renderer core on its own:

```powershell
uv run python recomp/windream/verify/direct_render_validate.py --gpu `
  --fixtures out/recomp/render-2d-smoke `
  --fixtures out/recomp/render-2d-smoke/long-capture
```

Omit `--fixtures` for synthetic checks only; omit `--gpu` for CPU checks.
GPU oracle execution currently requires Windows/D3D11. Shader generation covers
all four selected dialects. Outputs and fixture hashes are recorded under
`DREAMS_OUT/recomp/direct-render`.

The lift emits common replacement entries and original-body aliases. No native
game handlers are installed by default. `build.py --render-audit` produces
`build-audit`; `run.py --render-audit` selects it. Surface probes are diagnostic
infrastructure with the coverage limitations recorded in the implementation
notes, not evidence that every framebuffer access has been intercepted.

`build.py --vm win32|ledger|shadow` picks the guest's virtual memory
implementation (`host/vm`); the default `ledger` (the portable one) builds in
`build`, the others in `build-vm-<impl>`. `win32` is the original, Windows'
page state as the truth, kept as the reference. `shadow` runs
both and prints a `[vm-shadow]` line for every disagreement
(`WD_VM_SHADOW=abort` stops at the first). `WD_VM_LOG=<file>` writes one line
per call plus the state at exit, for comparing two builds.

`run.py --vm win32|ledger|shadow` runs the matching build; `--vm-log` writes
the call log to `<run>/vm.log` and `--vm-shadow log|abort` sets
`WD_VM_SHADOW`. `windream/verify/vm_compare.py A.log B.log` compares the logs
of two builds driven by the same `--keys` script: the virtual-memory
decisions (addresses, results, `VirtualQuery` answers) must be identical even
though the game's own memory differs with timing. A run stopped by
`--seconds` has no exit state in its log; only a game that exits writes one.
What the game itself exercises is startup allocations, commits and two
`VirtualQuery` calls: it never frees. Decommit and release are proven by the
host audit test (`test_render_codegen.py`, both implementations) and the
oracle's `free` case, not by play.

`run.py --capture-scene` captures original scene inputs on the first full-sized
3D frame to `DREAMS_OUT/recomp/windream/run/direct-scene.wds`. It is a diagnostic
observer: the original frame still renders in software. The native adapter can
also run against independent retail dumps with `verify/render_scene_smoke.py`;
`WDSceneGpuTests` renders those packets at native and widescreen sizes with
diagnostic primitive colours. Commands and limitations are in the implementation
record above.

## pcrecomp

pcrecomp is MIT-licensed (`LICENSE-pcrecomp`). `host/core/crash_report.c`,
`crash_report.h`, `recomp_trace.c`, `recomp_trace.h` and `recomp_types.h` are
adapted from its `runtime/recomp32`; `lift.py` and `gen_imports.py` import its
`tools/lift` and `tools/pe` modules from the clone.
