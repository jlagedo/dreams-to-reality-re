# Docs index

Technical documentation for *Dreams to Reality* (Cryo Interactive, 1997).

`../AGENTS.md` lists project paths and commands. It holds no findings; these
docs do.

| Doc | Contents |
|---|---|
| [re-setup.md](re-setup.md) | **Ghidra setup**, repo layout for RE, suggested order of attack; LE loader for the DOS builds, Glide typing, cross-build function matcher; **function naming**: registry, checked facts, doc comments in Ghidra |
| [engine.md](engine.md) | Engine architecture, toolchain, the four binaries, subsystem layout; renderer backends, **Glide ↔ DirectDraw/GDI map**, input and joystick paths |
| [toolchain.md](toolchain.md) | **Watcom C/C++ 10.6** pinned down; reference material; ~290 runtime symbols recovered per DOS binary |
| [cryolib.md](cryolib.md) | `CRYO.DLL` = CryoLib: 165 exports incl. a working **HNM6 decoder** |
| [game-content.md](game-content.md) | 150 levels, 30 inventory items, save system, from `DREAMS.INI` |
| [boot-sequence.md](boot-sequence.md) | **Boot flow decompiled** — intro → generic → menu → new game → head video → first map, with videos and menus named |
| [level-map.md](level-map.md) | **Complete project → scene map** — all 150 projects to 98 `.DSN` files |
| [dsn-loader.md](dsn-loader.md) | **`.DSN` loader decompiled** — header reader, and how `__watcall` was fixed |
| [scene-geometry.md](scene-geometry.md) | **`.DSN` to glTF** — vertex pool, face records, the stale-pointer problem, how to check a decode |
| [models.md](models.md) | **`.DAN` character and prop models** — the scene-graph node, skeletons, bridging faces, texture pages |
| [animation-validation.md](animation-validation.md) | **Animation hypothesis harness** — named track bindings, translation fingerprints, elbow/knee limits, remaining uncertainties |
| [animation-timing.md](animation-timing.md) | **Original timing trace** — 200 Hz clock, 30 animation frames/second, actor modifiers and timer quirks |
| [animation-subsystem.md](animation-subsystem.md) | **Shared runtime** — NPC/player animation, complete rig exports, inspector, playback and lifecycle |
| [animation-smoothing.md](animation-smoothing.md) | **Smoothing** — recovered quaternion splines, easing, missing controls, and initialization-pose loop fix |
| [animation-root-blending.md](animation-root-blending.md) | **Root movement and blending** — translation curves, extracted deltas, original transition timing and inspector experiments |
| [ai-animation-runtime.md](ai-animation-runtime.md) | **Retail AI and action selection** — actor scheduler, project AI transition lists, NPC action requests, and player movement-to-clip lookup |
| [sprites-ui-dialog.md](sprites-ui-dialog.md) | **Retail sprites, menus, and timed dialogue** — indexed/blended pixels, decoded fonts, separate UI loops, and voice/captions |
| [assets.md](assets.md) | **Models, textures, animation, sound** — where content lives and how it's packed |
| [file-formats.md](file-formats.md) | Asset format catalogue with verified magic numbers |
| [hnm-video.md](hnm-video.md) | HNM inventory **and how to decode it** — all 113 videos decode |
| [hnm6-spec.md](hnm6-spec.md) | Full HNM6 container + codec specification (MultimediaWiki, mirrored) |
| [disc-layout.md](disc-layout.md) | Both discs inventoried, install manifest, disc-check mechanism, merge map |
| [running.md](running.md) | How to actually run the game, ranked by difficulty; **3dfx build verified** under DOSBox Staging; controllers |
| [pipeline.md](pipeline.md) | **Extract → bake → pack**: the data root the web app reads, its JSON, releases for static hosting |
| [research-log.md](research-log.md) | Findings log, corrections, dead ends, open questions |

## Function names

Functions are named `MODULE_VerbObject` (convention in `../AGENTS.md`). Every
name is recorded in `../re/names/<program>.tsv` with its sources and the facts
it rests on, checked by `tools/check_names.py`. Pages that carry the
*Function names verified* note cite only names from that registry.

## Evidence tags

Claims carry one of three tags. Respect them — do not promote a tag without new evidence.

- **[verified]** — observed directly in the files/binaries on this machine, with the
  observation reproducible from the commands recorded in the doc.
- **[sourced]** — taken from external documentation, always linked.
- **[unverified]** — inference or hypothesis. Not tested. May be wrong.

## Conventions

- Configure local disc paths in `.dreams.local.env` (see `../dev/paths.example.env`).
  Absolute paths elsewhere in these docs record where an observation was made.
- Nothing in this repo contains copyrighted game data. Notes and tools only.
- Offsets are decimal unless prefixed `0x`. All multi-byte values in Cryo formats
  are little-endian.
