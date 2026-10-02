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
| `recomp/windream/host/` | Host runtime by API: `core/` (guest runtime, trace, crash report: `crash_report.c` portable, `crash_win32.c`/`crash_posix.c`/`crash_none.c` per system), `sdl/` (KERNEL32 files, process and threads, USER32, GDI32, WinMM and DirectSound on SDL3), `vm/` (the guest's virtual memory: `vm_win32.c` the original on Windows page state, `vm_ledger.c` the portable one over the `vm_os_*.c` layer, `vm_shadow.c` both compared; chosen with `build.py --vm`), `render/` (GPU renderer adapters, `render_*`), `hooks/` (`phys_hook.c` collision hooks) |
| `recomp/windream/verify/` | Renderer verification: retail-x86 Unicorn oracles (`render_*_smoke.py`; `render_dos_palette_smoke.py` replays `DREAMSFX.EXE` code from a Ghidra byte dump), live isolated runs (`render_*_live_smoke.py`), the acceptance runner (`render_acceptance.py`), `direct_render_validate.py`, `test_render_codegen.py`, the dump reader `mdmp.py`; `kernel_bridge_smoke.py`, the retail-x86 Unicorn oracle for the KERNEL32 bridges; C/C++ tests, oracles and hook hosts in `native/` |
| `recomp/windream/debug/` | Collision and dump tools: `colliders.py`, `invariant.py`, `replay_sweep.py`, `replay_full.py`, `sortcheck.py`, `x86dis.py`, `flag_hunt.py`; live collision: `collision_check.py` (the retail rules of the collision world, read through the control channel), `collision_walk.py` (walk and jump across the first map to the face tower with every collision check on; `--self-test` plants a fault for each detector); live control: `wdctl.py` (client and CLI of the control channel), `wd_mcp.py` (MCP server over it, registered in `.mcp.json`), `game_nav.py` (boot to a level, open the menus, load a save, read the event log), `bank_patch.py` (a modified `DREAMS.DAT` under `out/` to start a new game in any project), `cd_audio_check.py` (aligns a mixer dump with a disc track) |
| `recomp/windream/devtools/` | Development-only control channel inside the host (`WD_DEVTOOLS`, off in release; inert unless `WD_CTL` is set): keys, wait-until conditions, guest memory, screenshots, pause and step, event log, audio dump. `devtools.h` is the only header the host includes and lists what to delete to remove it |
| `recomp/windream/build.py`, `run.py`, `CMakeLists.txt` | Build (Ninja, unoptimized; clang-cl on Windows, gcc or clang on Linux) and sandboxed run (scripted keys, snapshots, window and pad options) |
| `recomp/render/` | GPU renderer: `ODRender` (sokol_gfx core, `direct.*`, shadows, fog) and `ODGraphics` (SDL3 backends), pinned dependencies in `cmake/`, tests in `tests/` |
| `recomp/disc/` | Disc library (C99, no host or SDL dependency): opens a `.cue`, `.iso` or extracted directory, ISO 9660 lookup and reads, audio track table, SHA-256; `build.py`, the `disc_list` tool |
| `recomp/launcher/` | Launcher library (Dear ImGui on `SDL_Renderer`, no host or render headers): disc setup and port settings, `dreams.ini`, one entry point returning `WD_*` pairs; `build.py`, `launcher_demo` |
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
| `docs/specs/` | Recomp plans and status: 000 the recomp, 005 retail debug tools, 006 Windows rendering, 007 launcher, disc images and user data |
| `docs/research/` | What is known about the game and its binaries (`re-setup.md` Ghidra workflow, `re-status.md`, `engine.md`, `toolchain.md`, `install-and-discs.md` install, data roots and disc swap, ...); index in `docs/README.md` |
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
`run.py` and the launcher default to the direct GPU renderer on Windows and to
software elsewhere; direct aborts on a case it does not support (no fallback).
Run from the repository root:

```sh
uv run --with capstone --with pefile python recomp/windream/lift/lift.py
uv run --with capstone --with pefile python recomp/windream/lift/gen_imports.py
uv run python recomp/windream/build.py
uv run python recomp/windream/build.py --vm shadow
uv run python recomp/windream/run.py
uv run python recomp/windream/run.py --renderer software
uv run python recomp/windream/run.py --headless --renderer direct --seconds 30
uv run python recomp/windream/run.py --discs
uv run python recomp/disc/build.py
uv run python recomp/launcher/build.py
uv run python recomp/windream/run.py --discs --headless --ctl --tag play
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play status
uv run python recomp/windream/debug/bank_patch.py list
uv run --with pefile python recomp/windream/release.py
uv run --with mcp --with pillow pytest
uv run python recomp/windream/verify/direct_render_validate.py --gpu
uv run --with unicorn --with capstone --with pefile python recomp/windream/verify/kernel_bridge_smoke.py
uv run --with unicorn python recomp/windream/verify/render_dos_palette_smoke.py
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

## Checking a change in the running game

Prefer these to fixed key schedules (`run.py --keys`), which are blind and
depend on timing. All of it is development-only and absent from release
builds. Reference: `recomp/README.md`, "Development control channel",
"MCP server for the control channel" and "Launcher and release".

| Need | Use | Where to look |
|---|---|---|
| Drive a live game: press keys, wait for a condition, read or write game memory, take a screenshot, pause and step, tail file opens, dump the mixer | Control channel: `run.py --ctl`, then `recomp/windream/debug/wdctl.py` (CLI, or `start_game()` and `Ctl` from Python) | `tests/recomp/test_devtools.py` is a minimal session; the command list is in `recomp/README.md` |
| The same as tools in a Claude Code session | MCP server `dreams-game` (`.mcp.json`, `recomp/windream/debug/wd_mcp.py`), approved once per machine | `tests/recomp/test_wd_mcp.py` |
| Get through the menus: boot to a level, open the in-game menu, open the Load list, load a save by title | `recomp/windream/debug/game_nav.py`: `boot_into`, `new_game`, `open_game_menu`, `open_system_page`, `open_load_list_in_game`, `open_load_list_from_main_menu`, `slot_names`, `load_slot`, `press_until`, `press_until_opened`. Each waits on a file open or a guest variable; the menu state addresses are constants there. Take `wdctl` from it (`nav.wdctl`) | `tests/recomp/test_disc_play.py` |
| Start a new game in any project, or stand in a link's exit | `recomp/windream/debug/bank_patch.py`: writes a modified `DREAMS.DAT` into a run's data directory (`--copy SRC:0`, `--spawn-in-link`, `--link`, `--set32`); only under `out/` | `test_disc_play.py` fixtures `bank_patch`, `link` |
| Fire a level's exit without playing it | Write a non-zero dword to `0x6155e4` (`SCENE_IsTriggerDone`) through the channel: `game_nav.complete_level_triggers` | `test_disc_play.py` |
| Check which files, disc and CD track the game used | `Ctl.log()` events (`open`, `disc`, `cd`), or `game_nav.events_after`, `disc_changes`, `opens`, `assert_level_files_come_from` | `test_disc_play.py` |
| Check the music without listening | `Ctl.audio_dump()` then `recomp/windream/debug/cd_audio_check.py` (`align`, `locate`) against a track from `disc_list` | `test_disc_play.py`, the music tests |
| List or read a disc image | `out/recomp/disc/build/disc_list.exe <cue>` (`--find`, `--cat`, `--no-hash`) | `tests/recomp/test_disc.py` |
| Disc-mode file rules without a game (marker rule, fallback, saves) | Native harness `recomp/windream/verify/native/disc_mode_tests.c` on two synthetic discs | `tests/recomp/test_disc_mode.py` |
| Drive the launcher window: clicks by widget id, keys, drops, file-dialog results, virtual gamepad, `expect` on its state, screenshots | `DREAMS_LAUNCHER_SCRIPT=<file>` with `launcher_demo` (`recomp/launcher/build.py`); grammar, widget ids and facts are in `recomp/launcher/launcher.h` | `tests/recomp/test_launcher_ui.py` (`run_ui`); `--play` path in `test_launcher.py` |
| Check a release | `recomp/windream/release.py`; it fails if development or test code is in the executable | `tests/recomp/test_release.py` |

Tests that start the game skip when the build, the discs or `WD_DEVTOOLS` are
missing, use their own `--tag` and remove their run directory. Two games can
run at once; a rebuild cannot happen while one runs from that build directory.

Found by running (Windows, Git Bash tool):

- Host source changed and you run `render_acceptance.py` or `render_thumbnail_smoke.py` → rebuild the audit build first: `uv run python recomp/windream/build.py --render-audit`. Both run `out/recomp/windream/build-audit/windream_recomp.exe`, not `build/`; a stale one passes silently with the old code.
- Checking which executable a run used → `grep -m1 "read root" <run>/stderr.txt`: forward slashes mean the SDL `files.c`, backslashes the old Win32 one.
- Counting lifted call sites of an import → `cat out/recomp/windream/gen/recomp_0*.c | grep -ic "MEM32(0x49C304)"` (slot VA without leading zeros or `u`; `0x0049C304u` finds nothing).
- Bash heredoc holding backslashes (`\\`, `\x56`, `\n` in Python or C text) → do not use it, even with `<<'EOF'`: each `\\` arrives as `\`. Write the script with the Write tool, or use Edit.
- `grep -P` → fails with "supports only unibyte and UTF-8 locales"; use `grep -E`.
- Unicorn 2.1.4, 32-bit guest needing `fs:` (TIB) → writing `UC_X86_REG_FS_BASE` is a no-op with a deprecation warning; load a GDT entry instead (`Cpu.set_fs` in `recomp/windream/verify/kernel_bridge_smoke.py`).
- pcrecomp `analyze_pe()` → returns a `PEInfo` object, not a dict: `analyze_pe(exe).entry_point_rva`.
- Loading a save in a `--tag` run crashes `GAME_LoadGame` at `memcpy_` (NULL from `DDAT_LoadRecord`), in every `--vm` build → the tag's sandbox starts empty, so the slot list comes from the install root, whose six `game<n>.dat` are foreign 10,364-byte files (retail is 11,388); run untagged, or copy `out/recomp/windream/run/sandbox/CRYO` into the tag's `sandbox/` first.
- Replaying a save load headless with the thumbnail smoke's key schedule (`...,28000:ESC,30000:LEFT,32000:RETURN,34000:RETURN`) → never opens a save (no `[save] open` line): fixed timings miss the menu states. Drive it through the control channel instead; the working key sequences are in `recomp/windream/debug/game_nav.py`.
- Looking for a Save command in the game → there is none in retail (the system page is Load / Options / Quit); the game autosaves on every level entry.
- Setting a `WD_*` variable inside the process with `SDL_setenv_unsafe` only → C `getenv` (`host_env`) does not see it on Windows; `main` also calls `_putenv_s`.
- Starting the host yourself (not through `run.py`) on a program that is not the game, or for a software reference → set `WD_RENDERER=software`: unset means direct on Windows, which replaces game functions (`difftest.py` sets it).
- Starting `windream_recomp.exe` with no arguments and no `WD_DISC1` → opens the launcher window instead of printing usage; pass the EXE path or use `run.py`.
- Rebuilding while a game from that build directory is still running → the link fails (the executable is locked); stop the run first.
- `run.py` importing `build.py` → `ModuleNotFoundError: build` when `tests/recomp` load `run.py` via importlib without `recomp/windream` on `sys.path`; shared helpers go in `recomp/recomp_env.py`.
- clangd "'imports.h' file not found" cascades in `host/` → include-path noise, not errors; syntax-check one file with `clang-cl /c` from a `uv run python -c` snippet using `recomp_env.build_env()`, `recomp_env.host_includes()` and `/I<recomp_env.ensure_sdl3()>/include`.
- Compiling a POSIX-only host file on this machine → WSL (Debian, gcc 14): `wsl -e sh -c 'cd /mnt/e/dev/dreams && gcc -std=c99 -fsyntax-only -Irecomp/windream/host/core -Irecomp/windream/host/sdl -Irecomp/windream/host/vm -Irecomp/windream/host/render recomp/windream/host/vm/vm_os_posix.c'`.
- Building and running on Linux from this machine → WSL has no `uv`; the build and run scripts need only the standard library: `wsl -e sh -c 'cd /mnt/e/dev/dreams && PYTHONPATH=src python3 recomp/windream/build.py'` (output `out/recomp/windream/build-linux`, SDL3 in `out/recomp/sdl3/<commit>/linux`). `run.py` needs `DREAMS_DISC1` and `DREAMS_INSTALL_ROOT` as `/mnt/e/...` paths in the environment, since `.dreams.local.env` holds Windows ones.
- `diff` of a Windows `vm.log` against a Linux one → every line differs (CRLF); use `diff --strip-trailing-cr`.
- `-ffp-contract=off` with clang-cl → "unknown argument ignored"; spell it `/clang:-ffp-contract=off` (CMakeLists.txt does).
- `imports_gen.c` from before the crash-report split → link error on `wd_import_bridge_names`; rerun `gen_imports.py` (cached difftest work directories too).
- `grep vm-shadow <run>/stderr.txt` → also matches the sandbox path of a run tagged `vm-shadow`; grep `MISMATCH`.
- Rewriting a code stub between Unicorn runs (`mem_write` over bytes already executed) → the old translation keeps running; make the stub read its result from a data cell (`render_dos_palette_smoke.py`).
- Comparing a level's brightness with a 3dfx capture taken right after load → the palette offsets start at ±128 and settle over the first seconds (`cur += (target - cur) >> 3` per tick); use the later captures (`reference-3dfx/raw/pNNN-c.png` and after).
- A level looks cut off at a distance in the direct renderer → project `+0xcc` is the far plane; retail culls whole nodes by it and never clips faces (`capture_scene`, `SceneSnapshot::view_projection`).
- Comparing against the DOS 3dfx build (`DREAMSFX.EXE`) → scripts and example configs in `recomp/windream/debug/dosbox/` (`drive.ps1`, `run-staging.ps1`, `run-x.ps1`; work directory `out/recomp/dosbox/`); the kept 3dfx reference captures and comparison figures are in `out/recomp/reference-3dfx/` (game-derived, never committed); DOSBox Staging at `E:\games\dosbox-staging`, DOSBox-X at `C:\DOSBox-X`. Mount a copy of the install as C:, never `E:\games\dreams`: the game autosaves over `DATA\GAME\GAME<n>.DAT` on every level entry.
- Comparing against the DOS 3dfx build on Linux → `recomp/windream/debug/dosbox/dosbox_x.py` (`setup`, `start`, `steps`, `stop`, `run`): DOSBox-X from apt (2024.03.01, software Voodoo) on its own Xvfb display, keys through xdotool. `DREAMSFX.EXE` needs `GLIDE2X.OVL` beside it (`setup` takes it from `3DFX\GRTVGR.EXE`; without it: "Fatal error: unable to load DLL"). The host key is F12 on Linux: F12+P screenshot, F12+I video (ZMBV AVI with audio). A key must be held about 150 ms: a bare `xdotool key` is missed.
- Starting the DOS build in another project → it reads `DREAMS.DAT` from the CD drive, not from `C:\CRYO\DREAMS`; DOSBox cannot overlay a CD mount, so mount a hardlinked copy of the extracted disc (`cp -al`) with only `DREAMS.DAT` replaced by a `bank_patch.py` bank (`mount d <dir> -t cdrom`; no CD music).
- Sending keys to DOSBox without focus (locked desktop) → DOSBox Staging takes `PostMessage` `WM_KEYDOWN`/`WM_KEYUP` with the scancode in lParam, and Ctrl+F5 writes a raw 640x480 PNG to `capture_dir` (`default_image_capture_formats = raw`, before any shader). DOSBox-X ignores posted keys: put `AUTOTYPE -w 12 -p 3 esc enter` before the game in `[autoexec]`, set `output = surface` and grab the window with `PrintWindow`. Its OpenGL Voodoo and Glide passthrough need an unlocked desktop to capture.

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
