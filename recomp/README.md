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
| `--fps N` | `WD_FPS` | 25 | Present cap; above 30 the original physics breaks (`docs/research/running.md`) |
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

The three disc-mode paths are read with `SDL_getenv`, which on Windows gives
the Unicode environment as UTF-8, so paths with non-ASCII characters work
whatever the ANSI code page; code that sets them inside the process must use
`SDL_setenv_unsafe` before the host's first file call. `tests/recomp/test_disc_mode.py`
runs the production file bridges on two small directory discs
(`windream/verify/native/disc_mode_tests.c`): resolution, the marker rule in
both directions, the saves, listings and reads.

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
and stages `out/recomp/windream/release/DreamsToReality/` (`DreamsToReality.exe`
and `README.txt`) and `DreamsToReality-windows-x64.zip` beside it. It fails if
the folder holds any other file or if the executable imports a DLL Windows
does not ship: the release links the C runtime and SDL3 statically (a second
SDL3 build, `out/recomp/sdl3/<commit>/install-mt`). The compiler flags are the
development build's; `--optimize` builds CMake's Release type, which has never
been verified on the lifted code.

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

The page's look, copy, controls overlay and in-game key handling (`site/style.css`, `site/ui.js`) are described in `recomp/web/NOTES-design.md`.

### Deploy on Cloudflare Pages

```sh
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
