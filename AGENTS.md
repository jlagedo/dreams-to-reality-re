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
| `ghidra_scripts/` | Java scripts for Ghidra |
| `tools/` | Ghidra import and checkpoint PowerShell scripts; `lx-loader-watcom.cspec` for the DOS-build LE loader; `match_functions.py` cross-build function matcher; `match_identical.py` byte-identical code shared between binaries (CryoLib in the game); `find_modules.py` source-file blocks; `check_names.py` checks and applies the name registry; `sync_doc_comments.py` copies doc text into Ghidra comments |
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

## Local paths

| Variable | Path |
|---|---|
| `DREAMS_DISC1`, `DREAMS_DISC2` | Extracted game discs |
| `DREAMS_INSTALL_ROOT` | Optional retail installation/cache tree for local comparisons; never a disc source |
| `DREAMS_WATCOM` | Watcom reference files |
| `DREAMS_WORK_ROOT` | Scratch and generated content; extraction defaults to its `extract/` subdirectory |
| `DREAMS_GHIDRA_ROOT` | Ghidra installation |
| `DREAMS_EXTRACT`, `DREAMS_OUT` | Optional output overrides (`extract/`, `out/`) |
| `DREAMS_NA_GAME_TOOL` | Optional path to the patched video decoder |

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

```powershell
.\tools\ghidra-import.ps1 -ImportSymbols
.\tools\ghidra-import.ps1 -Analyze:$false
.\tools\re-checkpoint.ps1 -NoCommit
.\tools\re-checkpoint.ps1 -Message "describe analysis"
.\tools\re-checkpoint.ps1 -SkipExport -Message "describe analysis"
```

Read-only headless decompilation from the repository root:

```powershell
. .\tools\dreams-env.ps1
& (Join-Path (Get-DreamsSetting DREAMS_GHIDRA_ROOT) 'support\analyzeHeadless.bat') ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly -scriptPath ghidra_scripts -postScript Decompile.java 004175bc
```

Headless scripts that take arguments go through a `.bat` launcher that splits
on `=`, so `Rename.java` uses `address:name` (or `@file`):

```powershell
& (Join-Path (Get-DreamsSetting DREAMS_GHIDRA_ROOT) 'support\analyzeHeadless.bat') ghidra dreams -process WINDREAM.EXE -noanalysis -scriptPath ghidra_scripts -postScript Rename.java 0043a306:MGM_SendMessage
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

`tools/ghidra-import.ps1` accepts `-Ghidra`, `-Project`, `-Disc1`, `-Disc2`
and `-Binaries` (only the listed files are re-imported); `tools/re-checkpoint.ps1`
accepts `-SkipExport` when the Ghidra GUI has the project open and `-Programs`
(default includes `DREAMSFX.EXE`). `DREAMSFX.EXE` needs the LE loader and
`ApplyGlideImports.java`; the matcher needs `ExportFunctionFeatures.java` run on
both programs first. Type passes that `ImportSymbols.java` does not restore
(re-run on a fresh project): `FixWatcomBss.java` (automatic on import),
`ApplyWatcomSigs.java` + `ApplyWatcomHeaders.java` (Watcom runtime, from
`DREAMS_WATCOM`), `ApplyTypes.java re/structs/directx.h
re/structs/windream-globals.tsv` (Windows DirectX globals). See
`docs/re-setup.md` for the full command reference.
