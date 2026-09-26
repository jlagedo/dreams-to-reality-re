# Dreams to Reality — project paths and commands

## Repository map

| Path | Find here |
|---|---|
| `src/dreams/cli.py` | `dreams` CLI entry points |
| `src/dreams/formats/` | Asset format decoders |
| `src/dreams/bake.py`, `src/dreams/pack.py` | Pipeline stages 2 and 3: extract -> baked data root -> release (`docs/pipeline.md`) |
| `src/dreams/paths.py` | Disc and output path resolution |
| `dev/paths.example.env` | Template for local path settings |
| `.dreams.local.env` | Machine paths (gitignored) |
| `tests/` | Python tests |
| `web/` | Babylon.js viewer; `web/src/` contains its source; `web/src/content.ts` owns every data URL |
| `docs/README.md` | Research documentation index |
| `docs/re-setup.md` | Ghidra setup and workflow |
| `re/symbols/*.tsv` | Saved Ghidra symbols and comments |
| `re/structs/` | C layouts and typed-global lists for Ghidra (`windream.h`, `directx.h`, `windream-globals.tsv`) |
| `ghidra_scripts/` | Java scripts for Ghidra |
| `tools/` | Ghidra import and checkpoint PowerShell scripts; `lx-loader-watcom.cspec` for the DOS-build LE loader; `match_functions.py` cross-build function matcher; `match_identical.py` byte-identical code shared between binaries (CryoLib in the game); `find_modules.py` source-file blocks |
| `ghidra/` | Local Ghidra project (gitignored) |
| `out/` | Default toolkit output (gitignored) |

## Local paths

| Variable | Path |
|---|---|
| `DREAMS_DISC1`, `DREAMS_DISC2` | Extracted game discs |
| `DREAMS_WATCOM` | Watcom reference files |
| `DREAMS_WORK_ROOT` | Scratch and generated content; the pipeline defaults to its `extract/`, `baked/` and `releases/` subdirectories |
| `DREAMS_GHIDRA_ROOT` | Ghidra installation |
| `DREAMS_EXTRACT`, `DREAMS_BAKED`, `DREAMS_RELEASES`, `DREAMS_OUT` | Optional output overrides (`extract/`, `baked/`, `releases/`, `out/`) |
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
uv run dreams bake --list
uv run dreams bake
uv run dreams bake --only music,sfx --format opus
uv run dreams bake --force
uv run dreams pack demo --projects 0,62,134
uv run dreams mesh
uv run dreams mesh E01GROTT --gltf out/
uv run dreams mesh --preview out/
uv run pytest
uv run pytest -m corpus        # whole-disc sweeps (slow, needs discs)
uv run ruff check .
uv run ruff format .
```

See `README.md` for CLI examples, including `disc`, `audio`, `scene`, `model`,
`bake`, and binary inspection commands. `dreams extract` and `dreams bake` use
`ffmpeg` and, for video extraction, `na_game_tool`; setup details are in
`docs/hnm-video.md`.

## Pipeline: extract, bake, pack

Three stages, each with one rule; details in `docs/pipeline.md`.

1. **`dreams extract`**: discs -> `$DREAMS_WORK_ROOT/extract`, lossless and
   faithful to the disc (FLAC, FFV1 MKV, PNG, glTF, JSON, raw project records).
2. **`dreams bake`**: extract -> `$DREAMS_WORK_ROOT/baked`, the **data root**:
   exactly what the web app reads, laid out as it is served. Project JSON keeps
   raw engine units; fields whose meaning is unverified are named by offset
   (`x6c`). Every run rebuilds `index.json`. Bake never reads the discs.
3. **`dreams pack <name> --projects ...`**: data root + app build ->
   `$DREAMS_WORK_ROOT/releases/<name>/site`, a static site ready for
   `npx wrangler pages deploy`. Pack copies content unchanged.

**The web app reads only the data root.** The dev server mounts it at `/data`
as plain files; there are no API routes. If the viewer shows 404s for data,
run `uv run dreams bake`. No game data belongs in the repository or in
`web/` (`publicDir` is disabled). There is no format versioning: change bake
and the app together and re-bake.

## Web viewer

```powershell
npm --prefix web ci
npm --prefix web run dev
npm --prefix web run build
npm --prefix web test
```

`http://localhost:5173/?project=62` skips the boot flow and starts in that
project.

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
```

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
