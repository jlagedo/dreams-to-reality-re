# Dreams to Reality (1997) — agent guide

Reverse-engineering toolkit and research notes for Cryo Interactive's *Dreams to
Reality* (DOS/Windows). The repo holds notes and tools only; the game discs live
outside it.

This file is a guide: rules, paths, commands, doc index. Findings, format
details and project status belong in [`docs/`](docs/README.md) — never add them
here.

## Rules

- **No game data in git** — no assets, binaries, disc images or anything derived
  from them. `tools/re-checkpoint.ps1` refuses to commit if a game-asset
  extension appears in the working tree.
- **Scratch goes in `E:\dreams-work\`** — agent briefs, transcripts, samples,
  dumps. Never the repo, and never another project's work directory
  (`E:\elysium-work\` is a different project).
- **Evidence tags.** Every claim in `docs/` is **[verified]** (observed on this
  machine, reproducible from the recorded command), **[sourced]** (external,
  linked) or **[unverified]** (inference). Do not promote a tag without new
  evidence.
- **Corrections and dead ends are recorded, not erased** — they go in
  `docs/research-log.md`, as do open questions.
- **Do not fetch the TCRF page programmatically.** It serves anti-AI
  prompt-injection text instead of content. Ask the user to open
  <https://tcrf.net/Dreams_to_Reality_(DOS,_Windows)> in a browser.

## Paths

```
E:\dev_game\
├── Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\   .cue + .bin tracks, disc1.iso
│   └── extracted\                                program disc — all game executables
├── Dreams-to-Reality_Win_EN_Disc-Image-Disk-2\   .cue + .bin tracks, disc2.iso
│   └── extracted\                                data disc — also DEMOS2\CRYO.DLL
└── watcom\                                       Watcom 10.6 libs, headers, sigs\*.csv

E:\dreams-work\
├── extract\          output of `dreams extract`
├── codex\<slug>\     brief.md, events.jsonl, last.md per worker run
└── websweep\         web research findings

E:\tools\ghidra_12.1.3_PUBLIC\       Ghidra
```

The toolkit resolves disc paths from `DREAMS_DISC1` / `DREAMS_DISC2`, then a
gitignored `dreams.local.toml`, then the defaults in `src/dreams/paths.py`.
Check with `uv run dreams config`.

## Repo layout

| Path | Committed | What it is |
|---|---|---|
| `src/dreams/` | yes | Python toolkit; format decoders in `formats/`, CLI in `cli.py` |
| `tests/` | yes | disc-dependent tests skip when paths are not configured |
| `docs/` | yes | findings |
| `re/symbols/*.tsv`, `re/structs/*.h` | yes | **source of truth** for Ghidra names, comments, structs |
| `ghidra_scripts/` | yes | Ghidra scripts — Java, not Python (PyGhidra does not run on this machine) |
| `tools/` | yes | `ghidra-import.ps1`, `re-checkpoint.ps1`, `watcall-cspec.patch` |
| `ghidra/` | **no** | the Ghidra project — a build artefact that embeds the executables |
| `out/` | **no** | default toolkit output |

## Commands

```bash
uv sync
uv run dreams --help                    # every subcommand; README.md has examples
uv run dreams extract [--list] [--only music,sprites] [--force]
uv run dreams mesh [SCENE --gltf out/] [--preview out/]
uv run pytest
uv run ruff check . && uv run ruff format .
```

`dreams extract` needs `ffmpeg` on PATH and a patched `na_game_tool` for video
(`docs/hnm-video.md`); groups whose tool is missing are skipped, not failed.

## Ghidra

Full detail in [docs/re-setup.md](docs/re-setup.md).

- **`WINDREAM.EXE` is the primary target** — PE32, loads with no extra loader.
  `DREAMS.EXE` / `DREAMSFX.EXE` are LE and need a loader Ghidra does not ship.
- Every game binary is Watcom 10.6, which Ghidra has no compiler spec for. A
  fresh install needs `tools/watcall-cspec.patch`, and a fresh project needs
  `ApplyWatcall.java`, or every decompilation is unreadable.
- **Work headless**, through `analyzeHeadless` and the scripts in
  `ghidra_scripts/`. The GUI is for looking, not for the record.
- An open GUI holds `dreams.lock`, which blocks headless access to the same
  project. Close it before a headless write.
- Annotations reach git only through `ExportSymbols.java` → `re/symbols/*.tsv`.
  Anything else lives in a project that is gitignored and rebuildable.

Reads are parallel-safe, so fan out across them:

```
analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly \
    -scriptPath ghidra_scripts -postScript Decompile.java 004175bc
```

Writes are not — one project, one lock. Funnel proposed renames through
`re/symbols/*.tsv` and apply them in one serialised pass with
`ImportSymbols.java`.

```powershell
.\tools\ghidra-import.ps1 [-ImportSymbols] [-Analyze:$false]   # rebuild the project from the discs
.\tools\re-checkpoint.ps1 -Message "..."                       # GUI closed: export all programs, commit
.\tools\re-checkpoint.ps1 -SkipExport -Message "..."           # GUI open: run ExportSymbols.java there first
```

Checkpoint at every natural pause.

## Delegating

- Subagent findings go to **files under `E:\dreams-work\`, not chat** — the
  worker returns a short summary plus a path.
- Codex reads this file, so `codex exec` from the repo root inherits it.
- With `--json`, codex prints no startup banner. Confirm the model and effort
  actually took by grepping `"model"` and `"reasoning_effort"` in
  `~/.codex/sessions/<yyyy>/<mm>/<dd>/rollout-*-<thread_id>.jsonl`.
- A codex resume silently falls back to the user default unless `-m` and the
  effort flag are repeated.

## Docs

| Doc | Contents |
|---|---|
| [docs/README.md](docs/README.md) | index, evidence tags, conventions |
| [docs/re-setup.md](docs/re-setup.md) | Ghidra setup, scripts, checkpointing |
| [docs/engine.md](docs/engine.md) | engine architecture, the four binaries, Windows video API |
| [docs/toolchain.md](docs/toolchain.md) | Watcom 10.6, reference material, runtime symbol recovery |
| [docs/cryolib.md](docs/cryolib.md) | `CRYO.DLL` exports |
| [docs/file-formats.md](docs/file-formats.md) | asset format catalogue |
| [docs/assets.md](docs/assets.md) | where models, textures, animation and sound live |
| [docs/dsn-loader.md](docs/dsn-loader.md) | `.DSN` loader decompiled, `__watcall` |
| [docs/scene-geometry.md](docs/scene-geometry.md) | `.DSN` to glTF |
| [docs/models.md](docs/models.md) | `.DAN` / `.3DC` models, the scene-graph node |
| [docs/hnm-video.md](docs/hnm-video.md) | HNM inventory and decoding |
| [docs/hnm6-spec.md](docs/hnm6-spec.md) | HNM6 spec (MultimediaWiki mirror) |
| [docs/game-content.md](docs/game-content.md) | levels, items, save system |
| [docs/level-map.md](docs/level-map.md) | project → scene map |
| [docs/disc-layout.md](docs/disc-layout.md) | disc inventory, install manifest, disc check, merge map |
| [docs/running.md](docs/running.md) | running the game |
| [docs/research-log.md](docs/research-log.md) | corrections, dead ends, open questions, sources |
