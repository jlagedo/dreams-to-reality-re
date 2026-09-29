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

## Renderer-boundary smokes

`windream/debug/render_smoke.py` replays original x86 on full retail-process
dumps, checking the proposed modern renderer cut against the original front
end. `--lifted` also compiles an isolated replacement-wrapper experiment from
the current generated functions; `--gpu` tests offscreen sokol/D3D11 depth,
orientation and RGB565 readback. Neither changes the production recomp.

```sh
uv run --with unicorn python recomp/windream/debug/render_smoke.py --lifted --gpu out/scratch/retail-gdidream-222659.dmp out/scratch/retail-gdidream-223423.dmp
```

The input dumps are local game-derived artifacts, not repository fixtures.
Generated code, replay inputs and JSON reports go to `DREAMS_OUT/recomp/render-smoke`.
The native compilation/GPU portions currently require Windows and the existing
pinned sokol checkout (`--sokol-dir` overrides its location). An optional
`--shadow-dump` and `--shadow-arena` check the real-shadow mask on a captured
state. See [the design and measured limits](../docs/specs/006-recomp-glide-renderer/modern-cut.md#smoke-results-2026-09-29).

The second boundary has its own pixel oracle and integer GPU compositor:

```sh
uv run --with unicorn python recomp/windream/debug/render_2d_smoke.py --gpu
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
game defaults to software rendering. `run.py --renderer direct` runs the tested
first-scene GPU path, including HUD and introductory dialogue. Full R0–R4 gates
remain open. See [implementation status](../docs/specs/006-recomp-glide-renderer/implementation.md).

Build and check the shared core without the viewer, loaders or ImGui:

```powershell
uv run python recomp/windream/debug/direct_render_validate.py --gpu `
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

`run.py --capture-scene` captures original scene inputs on the first full-sized
3D frame to `DREAMS_OUT/recomp/windream/run/direct-scene.wds`. It is a diagnostic
observer: the original frame still renders in software. The native adapter can
also run against independent retail dumps with `debug/render_scene_smoke.py`;
`WDSceneGpuTests` renders those packets at native and widescreen sizes with
diagnostic primitive colours. Commands and limitations are in the implementation
record above.

## pcrecomp

pcrecomp is MIT-licensed (`LICENSE-pcrecomp`). `runtime/crash_report.c`,
`crash_report.h`, `recomp_trace.c`, `recomp_trace.h` and `recomp_types.h` are
adapted from its `runtime/recomp32`; `lift.py` and `gen_imports.py` import its
`tools/lift` and `tools/pe` modules from the clone.
