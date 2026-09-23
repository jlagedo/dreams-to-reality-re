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
| `web/` | Babylon.js viewer; `web/src/` contains its source |
| `docs/README.md` | Research documentation index |
| `docs/re-setup.md` | Ghidra setup and workflow |
| `re/symbols/*.tsv` | Saved Ghidra symbols and comments |
| `ghidra_scripts/` | Java scripts for Ghidra |
| `tools/` | Ghidra import and checkpoint PowerShell scripts |
| `ghidra/` | Local Ghidra project (gitignored) |
| `out/` | Default toolkit output (gitignored) |

## Local paths

| Variable | Path |
|---|---|
| `DREAMS_DISC1`, `DREAMS_DISC2` | Extracted game discs |
| `DREAMS_WATCOM` | Watcom reference files |
| `DREAMS_WORK_ROOT` | Scratch and generated content; extraction defaults to its `extract/` subdirectory |
| `DREAMS_GHIDRA_ROOT` | Ghidra installation |
| `DREAMS_EXTRACT`, `DREAMS_OUT` | Optional output overrides |
| `DREAMS_NA_GAME_TOOL` | Optional path to the patched video decoder |

Process environment takes precedence over `.dreams.local.env`. The toolkit
uses the repository's `out/` directory when `DREAMS_OUT` is unset.

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
uv run ruff check .
uv run ruff format .
```

See `README.md` for CLI examples, including `disc`, `audio`, `scene`, `model`,
and binary inspection commands. `dreams extract` uses `ffmpeg` and, for video,
`na_game_tool`; setup details are in `docs/hnm-video.md`.

## Web viewer

```powershell
npm --prefix web ci
npm --prefix web run dev
npm --prefix web run build
```

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

`tools/ghidra-import.ps1` accepts `-Ghidra`, `-Project`, `-Disc1`, and
`-Disc2`; `tools/re-checkpoint.ps1` accepts `-SkipExport` when the Ghidra GUI
has the project open. See `docs/re-setup.md` for the full command reference.
