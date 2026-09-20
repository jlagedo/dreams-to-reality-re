# Docs index

Technical documentation for *Dreams to Reality* (Cryo Interactive, 1997).

`../AGENTS.md` is the top-level summary and orientation for an agent picking up
this work — Claude Code, Codex and any other AGENTS.md-aware tool all read it.
These docs hold the detail.

| Doc | Contents |
|---|---|
| [re-setup.md](re-setup.md) | **Ghidra + MCP setup**, repo layout for RE, suggested order of attack |
| [engine.md](engine.md) | Engine architecture, toolchain, the four binaries, subsystem layout, Windows video API |
| [toolchain.md](toolchain.md) | **Watcom C/C++ 10.6** pinned down; reference material; ~290 runtime symbols recovered per DOS binary |
| [cryolib.md](cryolib.md) | `CRYO.DLL` = CryoLib: 165 exports incl. a working **HNM6 decoder** |
| [game-content.md](game-content.md) | 150 levels, 30 inventory items, save system, from `DREAMS.INI` |
| [level-map.md](level-map.md) | **Complete project → scene map** — all 150 projects to 98 `.DSN` files |
| [dsn-loader.md](dsn-loader.md) | **`.DSN` loader decompiled** — stream API, header reader, the `__watcall` blocker |
| [assets.md](assets.md) | **Models, textures, animation, sound** — where content lives and how it's packed |
| [file-formats.md](file-formats.md) | Asset format catalogue with verified magic numbers |
| [hnm-video.md](hnm-video.md) | HNM inventory **and how to decode it** — all 113 videos decode |
| [hnm6-spec.md](hnm6-spec.md) | Full HNM6 container + codec specification (MultimediaWiki, mirrored) |
| [disc-layout.md](disc-layout.md) | Both discs inventoried, install manifest, disc-check mechanism, merge map |
| [running.md](running.md) | How to actually run the game, ranked by difficulty |
| [research-log.md](research-log.md) | Findings log, corrections, dead ends, open questions |

## Evidence tags

Claims carry one of three tags. Respect them — do not promote a tag without new evidence.

- **[verified]** — observed directly in the files/binaries on this machine, with the
  observation reproducible from the commands recorded in the doc.
- **[sourced]** — taken from external documentation, always linked.
- **[unverified]** — inference or hypothesis. Not tested. May be wrong.

## Conventions

- Disc paths are absolute and refer to `E:\dev_game\` (see `disc-layout.md`).
- Nothing in this repo contains copyrighted game data. Notes and tools only.
- Offsets are decimal unless prefixed `0x`. All multi-byte values in Cryo formats
  are little-endian.
