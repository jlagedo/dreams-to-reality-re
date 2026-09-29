# Dreams to Reality — project paths and commands

## Repository map

| Path | Find here |
|---|---|
| `src/dreams/cli.py` | `dreams` CLI entry points |
| `src/dreams/formats/` | Asset format decoders |
| `src/dreams/paths.py` | Disc and output path resolution |
| `dev/paths.example.env` | Template for local path settings |
| `.dreams.local.env` | Machine paths (gitignored) |
| `tests/` | Python tests |
| `docs/README.md` | Research documentation index |
| `docs/re-setup.md` | Ghidra setup and workflow |
| `opendreams/PORT_MAP.md` | Retail function porting rules, including behavior coverage and Ghidra tags |
| `opendreams/port-map.tsv` | Authoritative function-to-C++ mapping, coverage, remaining work and owner review |
| `re/symbols/*.tsv` | Saved Ghidra symbols and comments |
| `re/names/*.tsv` | Function-name registry: each name's kind, sources and machine-checked facts (`tools/check_names.py`) |
| `re/structs/` | C layouts and typed-global lists for Ghidra (`windream.h`, `directx.h`, `windream-globals.tsv`) |
| `re/boundaries/*.tsv` | Reviewed function-boundary fixes (missing entries, jump tables, data decoded as code, shared tails) for `ApplyBoundaries.java` |
| `re/prototypes/*.tsv` | Prototypes proven by byte-exact Watcom 11.0 compiles, for `ApplyPrototypes.java` |
| `ghidra_scripts/` | Java scripts for Ghidra |
| `tools/` | `ghidra_import.py` Ghidra project import, `re_checkpoint.py` checkpoint, `ghidra_headless.py` analyzeHeadless wrapper; `lx-loader-watcom.cspec` for the DOS-build LE loader; `match_functions.py` cross-build function matcher; `match_identical.py` byte-identical code shared between binaries (CryoLib in the game); `find_modules.py` source-file blocks; `check_names.py` checks and applies the name registry; `sync_doc_comments.py` copies doc text into Ghidra comments; `fps_limit_launcher.py` starts the retail Windows build with a frame limiter patched in memory |
| `recomp/` | Static recompilation experiment: lifter driver, runtime shims, build/run scripts, differential tests (outputs in `out/recomp/`) |
| `ghidra/` | Local Ghidra project (gitignored) |
| `out/` | Default toolkit output (gitignored) |

Before porting or marking a retail function complete, follow
`opendreams/PORT_MAP.md`. Update `opendreams/port-map.tsv` with the C++ change,
run `uv run python tools/check_port_map.py`, and apply the map to both Windows
programs in Ghidra as described there. `status` records the port method;
`coverage` records whether all retail behavior has been accounted for.
`reviewed` records the project owner's explicit personal review; never infer it
from tests or an agent's analysis.

Before adding host-only glue at a retail port boundary, trace the source-to-
runtime contract in Ghidra: valid encoded pointer values (including `0` and
`1`), parent links, coordinate spaces, and which retail function writes each
transform. Keep retail-derived behavior distinct from host-only structure.
Test those relationships against original data and independent retail evidence
before treating the result as faithful; a plausible screenshot is not proof.
Do not add an extra transform or reject an unusual pointer value merely because
it fits a conventional engine design. If a relationship remains unverified,
mark the port partial and state the assumption. For the player path, specifically
check that source pointer `1` resolves to the first model node and that the
composed model root lands at the recorded project spawn.

## Static recompilation experiment (pcrecomp)

Porting test: lift the retail x86 to C with pcrecomp and run it. Hand-written
sources are in `recomp/` (`recomp/README.md`); everything they generate, build
or dump goes under `DREAMS_OUT/recomp/` (generated code is game-derived; never
commit).

| Path | What |
|---|---|
| `out/recomp/pcrecomp/` | Upstream toolbox clone (github.com/sp00nznet/pcrecomp; `DREAMS_PCRECOMP` overrides); `tools/lift/` lift32 lifter, `tools/ghidra/DumpBounds.java` |
| `recomp/windream/` | GDIDREAM.EXE recomp build: `lift.py` (bounds.csv → `out/recomp/windream/gen/`), `build.py` (clang-cl + Ninja, unoptimized), `run.py` (sandboxed run in `out/recomp/windream/run/`, scripted keys, snapshots, window and pad options), `runtime/` (hand-written shims: USER32/GDI32/WinMM/DirectSound on SDL3, built once into `out/recomp/sdl3/`; KERNEL32 on Win32; `phys_hook.c` collision hooks), `debug/` (full-dump readers, collision invariant, Unicorn replay of one call) |
| `recomp/difftest/` | Differential tests: a Watcom 11.0 test program (`--cc wc106` for 10.6) native vs recompiled (`difftest.py`, `wat.py`, `coverage.py`); work directories in `out/recomp/difftest/` |
| `out/recomp/nocturne/`, `out/recomp/pod-recomp/` | Reference recomp projects from the same author |
| `out/dev/research/pcrecomp/` | Notes copied from pcrecomp and related projects (pipeline, hybrid approach, philosophy) |
| `out/recomp/matchdecomp/` | Matching decompilation: `match.py <c> <func_> <va> [--flags "-5r -otexan -s"]` byte-diffs a compiled function against WINDREAM.EXE (defaults: Watcom 11.0 from `DREAMS_WATCOM_COMPILER`, `-5r -d2`); `flagsweep.py`, `cases.txt`, `src/`, `proto/` (spec 000 W3 prototype matches) |

Toolchains (evidence: `matchdecomp/fpscan.py`, `fpruns.py`, `libversion.py`, `linkver.ps1`): WINDREAM/GDIDREAM are **Watcom 11.0** throughout (compiler, linker, runtime; not 11.0a); DREAMS.EXE and DREAMSFX.EXE link the 10.6 runtime and mix 10.6- and 11.0-compiled object files by address range (DREAMSFX's 10.6 code uses `-d1+`). Match flags: most Windows game code is unoptimized, stack-checked and built with debug info, `-5r -d2` (retail starts `push N; call __CHK`; plain `-od` misses the `je +2; jmp` branches); optimized modules use `-5r -otexan -s`. Blind 24-function test and its scripts: `matchdecomp/blind/` (`tally.py`). Compilers under `DREAMS_WATCOM`: `wc106`, `wc110\11.0`, `wc110\11.0a`. Evidence and flags: `docs/toolchain.md`.

## Local paths

| Variable | Path |
|---|---|
| `DREAMS_DISC1`, `DREAMS_DISC2` | Extracted game discs |
| `DREAMS_INSTALL_ROOT` | Optional retail installation/cache tree for local comparisons; never a disc source |
| `DREAMS_WATCOM` | Watcom reference files |
| `DREAMS_WATCOM_COMPILER`, `DREAMS_WATCOM_COMPILER_106` | Optional; the 11.0 and 10.6 compilers for matching decompilation (default `DREAMS_WATCOM\wc110\11.0`, `DREAMS_WATCOM\wc106\watcom10.6`) |
| `DREAMS_WORK_ROOT` | Scratch and generated content; extraction defaults to its `extract/` subdirectory |
| `DREAMS_GHIDRA_ROOT` | Ghidra installation |
| `DREAMS_EXTRACT`, `DREAMS_OUT` | Optional output overrides (`extract/`, `out/`) |
| `DREAMS_NA_GAME_TOOL` | Optional path to the patched video decoder |
| `DREAMS_PCRECOMP` | Optional pcrecomp clone for `recomp/` (default `DREAMS_OUT/recomp/pcrecomp`) |

Process environment takes precedence over `.dreams.local.env`. The toolkit
uses the repository's `out/` directory when `DREAMS_OUT` is unset; pipeline
output always lives outside the repository.

## Python toolkit

Run from the repository root:

```powershell
if (-not (Test-Path .dreams.local.env)) { Copy-Item dev/paths.example.env .dreams.local.env }
uv sync
uv run dreams config
uv run dreams --help
uv run dreams scene --help
uv run dreams extract --list
uv run dreams extract --only music,sprites --force
uv run dreams mesh
uv run dreams mesh E01GROTT --gltf out/
uv run dreams mesh --preview out/
uv run pytest
uv run pytest -m corpus        # whole-disc sweeps (slow, needs discs)
uv run ruff check .
uv run ruff format .
```

See `README.md` for CLI examples, including `disc`, `audio`, `scene`, `model`
and binary inspection commands. `dreams extract` uses `ffmpeg` and, for video
extraction, `na_game_tool`; setup details are in
`docs/hnm-video.md`.

## Extraction

`dreams extract` writes a lossless reference archive from the discs to
`$DREAMS_WORK_ROOT/extract` (FLAC, FFV1 MKV, PNG, glTF, JSON, raw project
records). The native OpenDreams applications read original game sources
directly; the archive supports research and comparisons.

## Ghidra

```sh
uv run python tools/ghidra_import.py --import-symbols
uv run python tools/ghidra_import.py --no-analyze
uv run python tools/re_checkpoint.py --no-commit
uv run python tools/re_checkpoint.py -m "describe analysis"
uv run python tools/re_checkpoint.py --skip-export -m "describe analysis"
```

Read-only headless decompilation from the repository root
(`ghidra_headless.py` defaults the project to `ghidra dreams` and adds
`-scriptPath ghidra_scripts`):

```sh
uv run python tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis -readOnly -postScript Decompile.java 004175bc
```

Headless scripts that take arguments go through Ghidra's `analyzeHeadless.bat`
launcher, which splits on `=`, so `Rename.java` uses `address:name` (or `@file`):

```sh
uv run python tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis -postScript Rename.java 0043a306:MGM_SendMessage
uv run python tools/match_functions.py DREAMSFX.EXE WINDREAM.EXE --renames
uv run --with capstone python tools/match_identical.py CRYO.DLL WINDREAM.EXE --insn
uv run python tools/find_modules.py WINDREAM.EXE --cross DREAMSFX.EXE
uv run python tools/check_names.py WINDREAM.EXE --renames --twin GDIDREAM.EXE
uv run python tools/check_names.py DREAMSFX.EXE --renames
uv run python tools/sync_doc_comments.py WINDREAM.EXE
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
  sources and facts; `tools/check_names.py` checks the facts against the binary
  and writes the rename file. Nothing is renamed in Ghidra by hand.
- **A name needs two independent sources.** Either of:
  - the docs or earlier analysis, plus a blind review of the decompilation
    with the docs comments stripped;
  - a byte-identical match (`match_identical.py`).
- Names go to `WINDREAM.EXE` and to `GDIDREAM.EXE` (same bytes at the same
  addresses; check before copying).

`tools/ghidra_import.py` accepts `--project`, `--project-name`,
`--import-structs` and `--binaries` (only the listed files are re-imported);
Ghidra and the discs come from `DREAMS_GHIDRA_ROOT`, `DREAMS_DISC1` and
`DREAMS_DISC2`. `tools/re_checkpoint.py` accepts `--skip-export` when the
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
`docs/re-setup.md` for the full command reference.
