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
  `vcvarsall.bat`; elsewhere, from `PATH`). The scripts are plain Python, but
  the runtime shims call Win32 directly, so the recompiled game itself builds
  and runs on Windows only for now.

## Commands

From the repository root:

```sh
uv run --with capstone --with pefile python recomp/windream/lift.py          # gen/ + lift-report.json
uv run --with capstone --with pefile python recomp/windream/gen_imports.py   # gen/imports_gen.c
uv run python recomp/windream/build.py        # out/recomp/windream/build (unoptimized)
uv run python recomp/windream/run.py          # play; logs and dumps in out/recomp/windream/run
uv run python recomp/windream/run.py --seconds 60 --keys 2000:ESC,5000:RETURN --snap-ms 4000
uv run --with capstone --with pefile python recomp/difftest/difftest.py t_core --tag od110
```

## Layout

| Path | What |
|---|---|
| `recomp_env.py` | Shared paths (output root, pcrecomp) and the build environment (Visual Studio via vcvarsall on Windows), configure + build |
| `windream/lift.py` | `bounds.csv` → `gen/`: lift32 plus the lifter fixes this game needed (flags at block starts, patched immediates, `push label; ret`, x87 and narrow mul/div), diagnostic `HOOKS` and `PROBES` |
| `windream/bounds.csv` | Function bounds exported from Ghidra with pcrecomp's `DumpBounds.java` |
| `windream/gen_imports.py` | One bridge per import; stubs for those no `runtime/*.c` implements |
| `windream/runtime/` | Host runtime and Win32/DirectSound/GDI shims; `phys_hook.c` collision hooks, `crash_report.c` crash report and minidumps |
| `windream/debug/` | Full-dump readers, the collision invariant, Unicorn replay of one `PHYS_SweepAxis` call, `x86dis.py` |
| `windream/CMakeLists.txt`, `build.py`, `run.py` | Build (clang-cl + Ninja) and sandboxed run (scripted keys, snapshots, fps cap, dump mode) |
| `difftest/` | `difftest.py` (compile with Watcom, bounds with Ghidra cached per source/flags/compiler, lift, build, run, diff), `wat.py`, the test programs `t_core.c` and `t_switch.c`, `gen_insn.py` (generates `t_insn.c` into the work root), `coverage.py`, `flagdiff.py`, `consumers.py`, `map2bounds.py` |

## pcrecomp

pcrecomp is MIT-licensed (`LICENSE-pcrecomp`). `runtime/crash_report.c`,
`crash_report.h`, `recomp_trace.c`, `recomp_trace.h` and `recomp_types.h` are
adapted from its `runtime/recomp32`; `lift.py` and `gen_imports.py` import its
`tools/lift` and `tools/pe` modules from the clone.
