# Dreams to Reality recomp — project paths and commands

The project is a static recompilation of the retail Windows game with
pcrecomp: lift `GDIDREAM.EXE` to C, build it against a hand-written host
runtime and GPU renderer, and run it. Everything else in the repository
either builds that or supplies the reverse engineering it depends on.

## Repository map

| Path | Find here |
|---|---|
| `recomp/README.md` | Recomp setup, commands, run options, verification scripts |
| `recomp/recomp_env.py` | Shared paths (`LIFT`, `HOST`, `HOST_DIRS`), build environment, the shared SDL3 build |
| `recomp/windream/lift/` | `lift.py` (`bounds.csv` → `out/recomp/windream/gen/`), `gen_imports.py` (import bridges), `replacements.py`, `render_audit.py`, `render_bulk.py`, `bounds.csv` (from Ghidra, pcrecomp `DumpBounds.java`) |
| `recomp/windream/host/` | Host runtime by API: `core/` (guest runtime, trace, crash report), `sdl/` (KERNEL32 files, process and threads, USER32, GDI32, WinMM and DirectSound on SDL3), `vm/` (the guest's virtual memory: `vm_win32.c` the original on Windows page state, `vm_ledger.c` the portable one over the `vm_os_*.c` layer, `vm_shadow.c` both compared; chosen with `build.py --vm`), `render/` (GPU renderer adapters, `render_*`), `hooks/` (`phys_hook.c` collision hooks) |
| `recomp/windream/verify/` | Renderer verification: retail-x86 Unicorn oracles (`render_*_smoke.py`), live isolated runs (`render_*_live_smoke.py`), the acceptance runner (`render_acceptance.py`), `direct_render_validate.py`, `test_render_codegen.py`, the dump reader `mdmp.py`; `kernel_bridge_smoke.py`, the retail-x86 Unicorn oracle for the KERNEL32 bridges; C/C++ tests, oracles and hook hosts in `native/` |
| `recomp/windream/debug/` | Collision and dump tools: `colliders.py`, `invariant.py`, `replay_sweep.py`, `replay_full.py`, `sortcheck.py`, `x86dis.py`, `flag_hunt.py` |
| `recomp/windream/build.py`, `run.py`, `CMakeLists.txt` | Build (clang-cl + Ninja, unoptimized) and sandboxed run (scripted keys, snapshots, window and pad options) |
| `recomp/render/` | GPU renderer: `ODRender` (sokol_gfx core, `direct.*`, shadows, fog) and `ODGraphics` (SDL3 backends), pinned dependencies in `cmake/`, tests in `tests/` |
| `recomp/difftest/` | Differential tests: a Watcom 11.0 test program (`--cc wc106` for 10.6) native vs recompiled (`difftest.py`, `wat.py`, `coverage.py`) |
| `re/tools/` | `ghidra_import.py` Ghidra project import, `re_checkpoint.py` checkpoint, `ghidra_headless.py` analyzeHeadless wrapper; `lx-loader-watcom.cspec` for the DOS-build LE loader; `match_functions.py` cross-build function matcher; `match_identical.py` byte-identical code shared between binaries (CryoLib in the game); `find_modules.py` source-file blocks; `find_cut.py` link slots where two builds swap code (backend cut); `check_names.py` checks and applies the name registry; `sync_doc_comments.py` copies doc text into Ghidra comments; `fps_limit_launcher.py` starts the retail Windows build with a frame limiter patched in memory |
| `re/ghidra_scripts/` | Java scripts for Ghidra |
| `re/symbols/*.tsv` | Saved Ghidra symbols and comments |
| `re/names/*.tsv` | Function-name registry: each name's kind, sources and machine-checked facts (`re/tools/check_names.py`) |
| `re/structs/` | C layouts and typed-global lists for Ghidra (`windream.h`, `directx.h`, `windream-globals.tsv`) |
| `re/boundaries/*.tsv` | Reviewed function-boundary fixes (missing entries, jump tables, data decoded as code, shared tails) for `ApplyBoundaries.java` |
| `re/prototypes/*.tsv` | Prototypes proven by byte-exact Watcom 11.0 compiles, for `ApplyPrototypes.java` |
| `re/matchdecomp/` | Matching decompilation scripts: `match.py <c> <func_> <va> [--flags "-5r -otexan -s"]` byte-diffs a compiled function against WINDREAM.EXE (defaults: Watcom 11.0 from `DREAMS_WATCOM_COMPILER`, `-5r -d2`); `flagsweep.py`, `cases.txt`, compiler probes, the blind test (`blind/`) |
| `src/dreams/` | Python support package: `paths.py` (local path settings), `watcom.py`, `formats/` (`lz`, `node`, `project`, `rig`, `scene`: the readers the verification scripts use) |
| `tests/` | Python tests: `recomp/`, `re/`, `toolkit/` |
| `docs/specs/` | Recomp plans and status: 000 the recomp, 005 retail debug tools, 006 Windows rendering |
| `docs/research/` | What is known about the game and its binaries (`re-setup.md` Ghidra workflow, `re-status.md`, `engine.md`, `toolchain.md`, ...); index in `docs/README.md` |
| `.dreams.example.env` | Template for local path settings |
| `.dreams.local.env` | Machine paths (gitignored) |
| `ghidra/` | Local Ghidra project (gitignored) |
| `out/` | Everything generated (gitignored) |

The hand-written OpenDreams port (ODRuntime, ODViewer, the port map) and the
larger Python asset toolkit (`dreams` CLI, extraction, most format decoders)
were removed. The last commit that has them is tagged `opendreams-final`;
research documents may still mention them.

Before adding host-only glue at a retail boundary, trace the source-to-
runtime contract in Ghidra: valid encoded pointer values (including `0` and
`1`), parent links, coordinate spaces, and which retail function writes each
transform. Keep retail-derived behavior distinct from host-only structure.
Test those relationships against original data and independent retail evidence
before treating the result as faithful; a plausible screenshot is not proof.
Do not add an extra transform or reject an unusual pointer value merely because
it fits a conventional engine design. If a relationship remains unverified,
say so and state the assumption. For the player path, specifically check that
source pointer `1` resolves to the first model node and that the composed
model root lands at the recorded project spawn.

## Recomp commands

Hand-written sources are in `recomp/`; everything they generate, build or dump
goes under `DREAMS_OUT/recomp/` (generated code is game-derived; never commit).
Run from the repository root:

```sh
uv run --with capstone --with pefile python recomp/windream/lift/lift.py
uv run --with capstone --with pefile python recomp/windream/lift/gen_imports.py
uv run python recomp/windream/build.py
uv run python recomp/windream/build.py --vm shadow
uv run python recomp/windream/run.py
uv run python recomp/windream/run.py --headless --renderer direct --seconds 30
uv run python recomp/windream/verify/direct_render_validate.py --gpu
uv run --with unicorn --with capstone --with pefile python recomp/windream/verify/kernel_bridge_smoke.py
uv run --with capstone --with pefile python recomp/difftest/difftest.py t_core --tag od110
uv run pytest
uv run --with capstone --with pefile pytest recomp/windream/verify/test_render_codegen.py
uv run ruff check .
uv run ruff format .
```

| Output path | What |
|---|---|
| `out/recomp/pcrecomp/` | Upstream toolbox clone (github.com/sp00nznet/pcrecomp; `DREAMS_PCRECOMP` overrides); `tools/lift/` lift32 lifter, `tools/ghidra/DumpBounds.java` |
| `out/recomp/sdl3/` | Static SDL3 build shared by every recomp and difftest build |
| `out/recomp/windream/` | `gen/` lifted C, `build/` (`build-audit/`, `build-trace/`), `run/` sandbox, logs, snapshots and crash dumps |
| `out/recomp/difftest/` | Differential-test work directories |
| `out/recomp/direct-render/` and the other `out/recomp/<check>/` directories | Verification builds, fixtures and reports |
| `out/recomp/matchdecomp/` | Matched C (`src/`, `proto/`) and blind-test work data for `re/matchdecomp/` |
| `out/recomp/nocturne/`, `out/recomp/pod-recomp/` | Reference recomp projects from the same author |

Found by running (Windows, Git Bash tool):

- Host source changed and you run `render_acceptance.py` or `render_thumbnail_smoke.py` → rebuild the audit build first: `uv run python recomp/windream/build.py --render-audit`. Both run `out/recomp/windream/build-audit/windream_recomp.exe`, not `build/`; a stale one passes silently with the old code.
- Checking which executable a run used → `grep -m1 "read root" <run>/stderr.txt`: forward slashes mean the SDL `files.c`, backslashes the old Win32 one.
- Counting lifted call sites of an import → `cat out/recomp/windream/gen/recomp_0*.c | grep -ic "MEM32(0x49C304)"` (slot VA without leading zeros or `u`; `0x0049C304u` finds nothing).
- Bash heredoc holding backslashes (`\\`, `\x56`, `\n` in Python or C text) → do not use it, even with `<<'EOF'`: each `\\` arrives as `\`. Write the script with the Write tool, or use Edit.
- `grep -P` → fails with "supports only unibyte and UTF-8 locales"; use `grep -E`.
- Unicorn 2.1.4, 32-bit guest needing `fs:` (TIB) → writing `UC_X86_REG_FS_BASE` is a no-op with a deprecation warning; load a GDT entry instead (`Cpu.set_fs` in `recomp/windream/verify/kernel_bridge_smoke.py`).
- pcrecomp `analyze_pe()` → returns a `PEInfo` object, not a dict: `analyze_pe(exe).entry_point_rva`.
- Loading a save in a `--tag` run crashes `GAME_LoadGame` at `memcpy_` (NULL from `DDAT_LoadRecord`), in every `--vm` build → the tag's sandbox starts empty, so the slot list comes from the install root, whose six `game<n>.dat` are foreign 10,364-byte files (retail is 11,388); run untagged, or copy `out/recomp/windream/run/sandbox/CRYO` into the tag's `sandbox/` first.
- Replaying a save load headless with the thumbnail smoke's key schedule (`...,28000:ESC,30000:LEFT,32000:RETURN,34000:RETURN`) → never opens a save (no `[save] open` line); load by hand in a windowed `run.py` (unverified beyond one attempt).
- `run.py` importing `build.py` → `ModuleNotFoundError: build` when `tests/recomp` load `run.py` via importlib without `recomp/windream` on `sys.path`; shared helpers go in `recomp/recomp_env.py`.
- clangd "'imports.h' file not found" cascades in `host/` → include-path noise, not errors; syntax-check one file with `clang-cl /c` from a `uv run python -c` snippet using `recomp_env.build_env()`, `recomp_env.host_includes()` and `/I<recomp_env.ensure_sdl3()>/include`.
- Compiling a POSIX-only host file on this machine → WSL (Debian, gcc 14): `wsl -e sh -c 'cd /mnt/e/dev/dreams && gcc -std=c99 -fsyntax-only -Irecomp/windream/host/core -Irecomp/windream/host/sdl -Irecomp/windream/host/vm -Irecomp/windream/host/render recomp/windream/host/vm/vm_os_posix.c'`.
- `grep vm-shadow <run>/stderr.txt` → also matches the sandbox path of a run tagged `vm-shadow`; grep `MISMATCH`.

Toolchains (evidence: `re/matchdecomp/fpscan.py`, `fpruns.py`, `libversion.py`, `linkver.ps1`): WINDREAM/GDIDREAM are **Watcom 11.0** throughout (compiler, linker, runtime; not 11.0a); DREAMS.EXE and DREAMSFX.EXE link the 10.6 runtime and mix 10.6- and 11.0-compiled object files by address range (DREAMSFX's 10.6 code uses `-d1+`). Match flags: most Windows game code is unoptimized, stack-checked and built with debug info, `-5r -d2` (retail starts `push N; call __CHK`; plain `-od` misses the `je +2; jmp` branches); optimized modules use `-5r -otexan -s`. Blind 24-function test and its scripts: `re/matchdecomp/blind/` (`tally.py`). Compilers under `DREAMS_WATCOM`: `wc106`, `wc110\11.0`, `wc110\11.0a`. Evidence and flags: `docs/research/toolchain.md`.

## Local paths

| Variable | Path |
|---|---|
| `DREAMS_DISC1`, `DREAMS_DISC2` | Extracted game discs (the executables are lifted from disc 1) |
| `DREAMS_INSTALL_ROOT` | Retail installation tree (`CRYO\DREAMS`) the recompiled game reads; never a disc source |
| `DREAMS_GHIDRA_ROOT` | Ghidra installation |
| `DREAMS_WATCOM` | Watcom reference files |
| `DREAMS_WATCOM_COMPILER`, `DREAMS_WATCOM_COMPILER_106` | Optional; the 11.0 and 10.6 compilers for difftest and matching decompilation (default `DREAMS_WATCOM\wc110\11.0`, `DREAMS_WATCOM\wc106\watcom10.6`) |
| `DREAMS_OUT` | Optional output override (default: the repository's `out/`) |
| `DREAMS_PCRECOMP` | Optional pcrecomp clone (default `DREAMS_OUT/recomp/pcrecomp`) |

Copy `.dreams.example.env` to `.dreams.local.env` and set the paths. Process
environment takes precedence over `.dreams.local.env`.

## Ghidra

```sh
uv run python re/tools/ghidra_import.py --import-symbols
uv run python re/tools/ghidra_import.py --no-analyze
uv run python re/tools/re_checkpoint.py --no-commit
uv run python re/tools/re_checkpoint.py -m "describe analysis"
uv run python re/tools/re_checkpoint.py --skip-export -m "describe analysis"
```

Read-only headless decompilation from the repository root
(`ghidra_headless.py` defaults the project to `ghidra dreams` and adds
`-scriptPath re/ghidra_scripts`):

```sh
uv run python re/tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis -readOnly -postScript Decompile.java 004175bc
```

Headless scripts that take arguments go through Ghidra's `analyzeHeadless.bat`
launcher, which splits on `=`, so `Rename.java` uses `address:name` (or `@file`):

```sh
uv run python re/tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis -postScript Rename.java 0043a306:MGM_SendMessage
uv run python re/tools/match_functions.py DREAMSFX.EXE WINDREAM.EXE --renames
uv run --with capstone python re/tools/match_identical.py CRYO.DLL WINDREAM.EXE --insn
uv run python re/tools/find_modules.py WINDREAM.EXE --cross DREAMSFX.EXE
uv run python re/tools/check_names.py WINDREAM.EXE --renames --twin GDIDREAM.EXE
uv run python re/tools/check_names.py DREAMSFX.EXE --renames
uv run python re/tools/sync_doc_comments.py WINDREAM.EXE
```

`Rename.java @out\ghidra\match\names-<program>.tsv` applies checked names;
`ApplyDocComments.java @out\ghidra\match\docsync-<program>.tsv` replaces the
`[DOCS_SYNC]` comments; `MergeFragments.java parent:fragment` folds a function
Ghidra split off at a jump target back into its parent.

### Naming convention

Names follow Cryo's own style, taken from the names that survive in the
binaries, so recovered and descriptive names read alike.

- **Recovered names stay verbatim**:
  - error-string names: `MGM_SendMessage`, `MGM_DispatchMessages`,
    `CTRL_Dispatcher`, `MENJ_Dispatcher`, `CAM_CompCameraPos`, `DAN_Load3DC`;
  - CryoLib `GL_*`;
  - Miles `AIL_*`;
  - the Watcom runtime (trailing underscore: `memcpy_`, `read_`).
- **Descriptive names** are `MODULE_VerbObject`:
  - MODULE is 2-6 uppercase letters;
  - the rest is PascalCase, verb first (`Load`, `Open`, `Read`, `Init`,
    `Find`, `Draw`, `Tick`);
  - examples: `DRD_LoadEntry`, `RES_Load`, `CD_FindDrive`.
- **File-format modules take the format's name**: `DSN_`, `DAN_`, `DRD_`,
  `BF_`, `FSB_`, `HNM6_`.
- **Reuse a recovered prefix** (`DAN_`, `MGM_`, `CAM_`, `CTRL_`, `MENJ_`) only
  where the code is clearly the same module. A shared prefix claims a shared
  source file.
- **Fixed prefixes.** Add new ones here rather than inventing variants:
  - core, video and sound: `GAME_`, `SYS_`, `MEM_`, `VID_`, `DDRAW_`, `GDI_`,
    `DSOUND_`, `REND_`, `SW_`;
  - input: `INPUT_`, `JOY_`;
  - text and UI: `TEXT_`, `SPR_`, `ICON_`, `UI_`, `MENU_`, `BOOT_`, `TRANS_`;
  - world: `SCENE_`, `ENT_`, `MDL_`, `ANIM_`;
  - data and files: `RES_`, `STRM_`, `LZ_`, `RLE_`, `VFS_`, `FILE_`, `CD_`,
    `DDAT_` (DREAMS.DAT);
  - behaviour and maths: `AI_` (squad AI), `PHYS_`, `MATH_` (fixed-point
    vector, matrix and quaternion helpers), `HNM5_`, `PART_` (the level's
    mana-mote particles);
  - `DBG_` for debug code; `DEMO_` for input recording and replay;
  - DOS build: `GLIDE_`, `KBD_`, `TIMER_`, `DPMI_`.
- **Globals** are `g_camelCase` (`g_frameBuffer`, `g_videoWidth`); API
  objects keep the API's name (`g_DirectDraw`).
- **Avoid Ghidra's auto-label prefixes**: `FUN_`, `DAT_`, `LAB_`, `PTR_`,
  `s_`.
- **Provenance goes in the plate comment**: `Name recovered: <source>` or
  `Descriptive name`, plus the evidence.
- **Every name goes in the registry** `re/names/<program>.tsv`, with its kind,
  sources and facts; `re/tools/check_names.py` checks the facts against the binary
  and writes the rename file. Nothing is renamed in Ghidra by hand.
- **A name needs two independent sources.** Either of:
  - the docs or earlier analysis, plus a blind review of the decompilation
    with the docs comments stripped;
  - a byte-identical match (`match_identical.py`).
- Names go to `WINDREAM.EXE` and to `GDIDREAM.EXE` (same bytes at the same
  addresses; check before copying).

`re/tools/ghidra_import.py` accepts `--project`, `--project-name`,
`--import-structs` and `--binaries` (only the listed files are re-imported);
Ghidra and the discs come from `DREAMS_GHIDRA_ROOT`, `DREAMS_DISC1` and
`DREAMS_DISC2`. `re/tools/re_checkpoint.py` accepts `--skip-export` when the
Ghidra GUI has the project open and `--programs` (default includes
`DREAMSFX.EXE`). `DREAMSFX.EXE` needs the LE loader and
`ApplyGlideImports.java`; the matcher needs `ExportFunctionFeatures.java` run on
both programs first. Type passes that `ImportSymbols.java` does not restore
(re-run on a fresh project): `FixWatcomBss.java` (automatic on import),
`ApplyWatcomSigs.java` + `ApplyWatcomHeaders.java` (Watcom runtime, from
`DREAMS_WATCOM`), `ApplyBoundaries.java re/boundaries/<program>.tsv` then
`CreateWatcomFunctions.java apply` (reviewed function-boundary fixes; check
with `ReportBoundaries.java`), `ApplyPrototypes.java
re/prototypes/<program>.tsv` (prototypes proven by byte-exact Watcom 11.0
compiles), `ApplyWatcall.java`, `ApplyTypes.java re/structs/directx.h
re/structs/windream-globals.tsv` (Windows DirectX globals). See
`docs/research/re-setup.md` for the full command reference.
