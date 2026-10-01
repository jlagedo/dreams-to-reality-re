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
- Visual Studio with clang-cl, CMake and Ninja (found through `vswhere` and
  `vcvarsall.bat`; elsewhere, from `PATH`).
- SDL3 for the window, input and sound. The first build downloads the source
  pinned in `recomp/render/cmake/Dependencies.cmake` and builds a static release
  library once into `out\recomp\sdl3\<commit>`, which every recomp and
  difftest build then shares.
- The USER32, GDI32, WinMM and DirectSound bridges run on SDL3 and build
  without `<windows.h>`. The KERNEL32 side (files, threads, virtual memory,
  crash report) still calls Win32, so the recompiled game builds and runs on
  Windows only for now.

## Commands

From the repository root:

```sh
uv run --with capstone --with pefile python recomp/windream/lift/lift.py          # gen/ + lift-report.json
uv run --with capstone --with pefile python recomp/windream/lift/gen_imports.py   # gen/imports_gen.c
uv run python recomp/windream/build.py        # out/recomp/windream/build (unoptimized)
uv run python recomp/windream/run.py          # play; logs and dumps in out/recomp/windream/run
uv run python recomp/windream/run.py --mute --renderer direct --seconds 30 # silent unattended run
uv run python recomp/windream/run.py --headless --renderer direct --seconds 30 # hidden, muted run
uv run python recomp/windream/run.py --seconds 60 --keys 2000:ESC,5000:RETURN --snap-ms 4000
uv run python recomp/windream/run.py --overlays   # retail debug flags on; keypad 1-4 toggle (spec 005)
uv run python recomp/windream/run.py --poke 0x49da14=1   # any dword into the image before entry
uv run --with capstone --with pefile python recomp/windream/debug/flag_hunt.py   # unwritten flags
uv run python recomp/windream/run.py --fullscreen --pad keys
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
| `--scale N` | `WD_SCALE` | 2 | Window size in multiples of 640x480, reduced to fit the desktop |
| `--fullscreen` | `WD_FULLSCREEN` | off | Start fullscreen (borderless, desktop resolution) |
| `--filter` | `WD_FILTER` | `pixelart` | Scaling: `pixelart` (sharp, even pixels), `nearest` or `linear` |
| `--pad` | `WD_PAD` | `winmm` | Gamepads: `winmm` shows them as WinMM joysticks (press J in game), `keys` makes them press keys, `off` ignores them |
| `--deadzone IN,OUT` | `WD_DEADZONE` | `10,95` | Scaled radial deadzone for sticks and triggers, percent of full deflection: below IN reads centred, past OUT reads full |
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

## Layout

| Path | What |
|---|---|
| `recomp_env.py` | Shared paths (output root, pcrecomp, `LIFT`, `HOST`, `HOST_DIRS`), the build environment (Visual Studio via vcvarsall on Windows), the shared SDL3 build, configure + build |
| `windream/lift/lift.py` | `bounds.csv` → `gen/`: lift32 plus the lifter fixes this game needed (flags at block starts, patched immediates, `push label; ret`, x87 and narrow mul/div), diagnostic `HOOKS` and `PROBES`, and `CALLS` (runtime calls inserted before an instruction, such as the editor draw of spec 005) |
| `windream/lift/bounds.csv` | Function bounds exported from Ghidra with pcrecomp's `DumpBounds.java` |
| `windream/lift/gen_imports.py` | One bridge per import; stubs for those no `host/*/*.c` implements |
| `windream/lift/replacements.py`, `render_audit.py`, `render_bulk.py` | Generated entry wrappers for the renderer boundary, guest-memory probes and GPU-aware bulk transfers |
| `windream/host/core/` | Guest runtime (`runtime.c`), function-entry trace, crash report and minidumps, `recomp_types.h`, `imports.h` |
| `windream/host/sdl/` | `user.c`, `gdi.c`, `winmm.c` and `dsound.c` emulate USER32, GDI32, WinMM and DirectSound on SDL3 (`host.h`); `guest_win32.h` holds the guest's Win32 constants and 32-bit layouts, which `win32_abi_check.c` checks against the SDK |
| `windream/host/win32/` | `kernel.c`, `files.c`, `threads.c` and `vm.c`: KERNEL32 on Win32, the part to replace for other systems |
| `windream/host/render/` | Adapters between the lifted game and the GPU renderer: boundary and surface ownership, scene capture and draw, UI, movies, live frame, metrics, hooks |
| `windream/host/hooks/` | `phys_hook.c` collision hooks |
| `windream/verify/` | Renderer verification: retail-x86 oracles (`render_*_smoke.py`), live isolated runs (`render_*_live_smoke.py`, project and thumbnail smokes), `render_acceptance.py`, `render_content_inventory.py`, `direct_render_validate.py`, `test_render_codegen.py`, and the dump reader `mdmp.py`. The scripts import each other by name, so they share one directory; their C/C++ sources are in `native/` |
| `windream/debug/` | Collision and dump tools: the collision invariant, Unicorn replay of one `PHYS_SweepAxis` call, `x86dis.py`, `flag_hunt.py` (unwritten debug flags, address-copy scan) |
| `windream/CMakeLists.txt`, `build.py`, `run.py` | Build (clang-cl + Ninja) and sandboxed run (scripted keys, snapshots, fps cap, window, pad and dump modes) |
| `render/` | GPU renderer: `ODRender` (sokol_gfx core: `direct.*`, shadows, fog) and `ODGraphics` (SDL3 backends for D3D11, Metal, OpenGL); pinned dependencies and shader generation in `cmake/`; tests in `tests/` |
| `difftest/` | `difftest.py` (compile with Watcom, bounds with Ghidra cached per source/flags/compiler, lift, build, run, diff), `wat.py`, the test programs `t_core.c` and `t_switch.c`, `gen_insn.py` (generates `t_insn.c` into the work root), `coverage.py`, `flagdiff.py`, `consumers.py`, `map2bounds.py` |

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
game defaults to software rendering. `run.py --renderer direct` runs the tested
first-scene GPU path, including HUD and introductory dialogue. Full R0–R4 gates
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
