# Static recompilation experiment

The hand-written half of spec 000 (`docs/specs/000-the-recomp/spec.md`): lift
`GDIDREAM.EXE` to C with [pcrecomp](https://github.com/sp00nznet/pcrecomp)'s
`lift32`, build it against hand-written Win32 shims and run it; and test the
lifter on small Watcom programs run natively and recompiled.

Only hand-written sources live here. Everything they produce is game-derived or
bulky and goes under `DREAMS_OUT\recomp` (by default the repository's
`out\recomp`), never into git: the lifted C, `imports_gen.c`, builds, runs,
sandboxes, crash dumps and the difftest work directories.

## Setup

- pcrecomp: clone it to `out\recomp\pcrecomp`, or set `DREAMS_PCRECOMP`. The
  scripts were last run against commit `1f49cea`.
- `DREAMS_DISC1` (the exe), `DREAMS_INSTALL_ROOT` (the retail `CRYO\DREAMS`
  tree the game reads), `DREAMS_GHIDRA_ROOT` and the Watcom settings, as in
  `dev/paths.example.env`.
- Visual Studio with clang-cl, CMake and Ninja (found through `vswhere` and
  `vcvarsall.bat`; elsewhere, from `PATH`).
- SDL3 for the window, input and sound. The first build downloads the source
  pinned in `opendreams/cmake/Dependencies.cmake` and builds a static release
  library once into `out\recomp\sdl3\<commit>`, which every recomp and
  difftest build then shares.
- The USER32, GDI32, WinMM and DirectSound bridges run on SDL3 and build
  without `<windows.h>`. The KERNEL32 side (files, threads, virtual memory,
  crash report) still calls Win32, so the recompiled game builds and runs on
  Windows only for now.

## Commands

From the repository root:

```sh
uv run --with capstone --with pefile python recomp/windream/lift.py          # gen/ + lift-report.json
uv run --with capstone --with pefile python recomp/windream/gen_imports.py   # gen/imports_gen.c
uv run python recomp/windream/build.py        # out/recomp/windream/build (unoptimized)
uv run python recomp/windream/run.py          # play; logs and dumps in out/recomp/windream/run
uv run python recomp/windream/run.py --seconds 60 --keys 2000:ESC,5000:RETURN --snap-ms 4000
uv run python recomp/windream/run.py --overlays   # retail debug flags on; keypad 1-4 toggle (spec 005)
uv run python recomp/windream/run.py --poke 0x49da14=1   # any dword into the image before entry
uv run --with capstone --with pefile python recomp/windream/debug/flag_hunt.py   # unwritten flags
uv run python recomp/windream/run.py --fullscreen --pad keys
uv run --with capstone --with pefile python recomp/difftest/difftest.py t_core --tag od110
```

## Playing

The game polls the keyboard as it did on Windows (`docs/engine.md`, "Input"):
the arrows move, Ctrl jumps or kicks, Alt punches, Space switches to combat,
1 to 3 pre-select magic, Esc opens the menu, and holding F10 shows the
controls. The window pauses the game when it loses focus, as the original
does. F11 toggles fullscreen; the game sees F11 too.

| `run.py` option | Environment | Default | What |
|---|---|---|---|
| `--scale N` | `WD_SCALE` | 2 | Window size in multiples of 640x480, reduced to fit the desktop |
| `--fullscreen` | `WD_FULLSCREEN` | off | Start fullscreen (borderless, desktop resolution) |
| `--filter` | `WD_FILTER` | `pixelart` | Scaling: `pixelart` (sharp, even pixels), `nearest` or `linear` |
| `--pad` | `WD_PAD` | `winmm` | Gamepads: `winmm` shows them as WinMM joysticks (press J in game), `keys` makes them press keys, `off` ignores them |
| `--deadzone IN,OUT` | `WD_DEADZONE` | `10,95` | Scaled radial deadzone for sticks and triggers, percent of full deflection: below IN reads centred, past OUT reads full |
| `--fps N` | `WD_FPS` | 25 | Present cap; above 30 the original physics breaks (`docs/running.md`) |

With `--pad keys`, the stick and d-pad are the arrows, A is Ctrl, X is Alt,
Y is Space, B is Down, LB, RB and LT are 1, 2 and 3, and Start is Esc: the
layout in `docs/running.md`. Back is Return, for the menus. In `winmm` mode a
pad looks like an XInput pad does to WinMM on Windows (X/Y the left stick,
POV the d-pad, buttons A B X Y LB RB Back Start LS RS as 1 to 10). The game
reads only X, Y and the buttons, and it takes the stick position at start as
the centre, so leave the stick alone while the game starts.

## Layout

| Path | What |
|---|---|
| `recomp_env.py` | Shared paths (output root, pcrecomp), the build environment (Visual Studio via vcvarsall on Windows), the shared SDL3 build, configure + build |
| `windream/lift.py` | `bounds.csv` → `gen/`: lift32 plus the lifter fixes this game needed (flags at block starts, patched immediates, `push label; ret`, x87 and narrow mul/div), diagnostic `HOOKS` and `PROBES`, and `CALLS` (runtime calls inserted before an instruction, such as the editor draw of spec 005) |
| `windream/bounds.csv` | Function bounds exported from Ghidra with pcrecomp's `DumpBounds.java` |
| `windream/gen_imports.py` | One bridge per import; stubs for those no `runtime/*.c` implements |
| `windream/runtime/` | Host runtime. `user.c`, `gdi.c`, `winmm.c` and `dsound.c` emulate USER32, GDI32, WinMM and DirectSound on SDL3 (`host.h`); `guest_win32.h` holds the guest's Win32 constants and 32-bit layouts, which `win32_abi_check.c` checks against the SDK. `kernel.c`, `files.c`, `threads.c` and `vm.c` are KERNEL32 on Win32; `phys_hook.c` collision hooks; `crash_report.c` crash report and minidumps |
| `windream/debug/` | Full-dump readers, the collision invariant, Unicorn replay of one `PHYS_SweepAxis` call, `x86dis.py`, `flag_hunt.py` (unwritten debug flags, address-copy scan) |
| `windream/CMakeLists.txt`, `build.py`, `run.py` | Build (clang-cl + Ninja) and sandboxed run (scripted keys, snapshots, fps cap, window, pad and dump modes) |
| `difftest/` | `difftest.py` (compile with Watcom, bounds with Ghidra cached per source/flags/compiler, lift, build, run, diff), `wat.py`, the test programs `t_core.c` and `t_switch.c`, `gen_insn.py` (generates `t_insn.c` into the work root), `coverage.py`, `flagdiff.py`, `consumers.py`, `map2bounds.py` |

## pcrecomp

pcrecomp is MIT-licensed (`LICENSE-pcrecomp`). `runtime/crash_report.c`,
`crash_report.h`, `recomp_trace.c`, `recomp_trace.h` and `recomp_types.h` are
adapted from its `runtime/recomp32`; `lift.py` and `gen_imports.py` import its
`tools/lift` and `tools/pe` modules from the clone.
