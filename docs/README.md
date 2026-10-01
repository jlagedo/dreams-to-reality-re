# Docs index

Documentation for the *Dreams to Reality* recomp.

- `specs/` plans the recomp and records its status.
- `research/` holds what is known about the game and its binaries. It was
  written while the project was still a hand-written port, so some pages
  mention OpenDreams, ODViewer, the port map or `dreams` CLI commands that
  were removed (tag `opendreams-final`). The findings about the game stand.

`../AGENTS.md` lists project paths and commands. It holds no findings; these
docs do.

## Specs

| Spec | Contents |
|---|---|
| [Spec 000 — the recomp](specs/000-the-recomp/spec.md) | The static recompilation, differential tests and matching decompilation in `recomp/` and `re/matchdecomp/`; W1–W3 done: tools on Watcom 11.0, function boundaries fixed in Ghidra, 131 prototypes proven by byte-exact compiles; the recomp's collision bug traced to a lifter flag defect |
| [Spec 005 — retail debug tools](specs/005-debug-tools/spec.md) | **The shipped debug layer**: Frame Rate/Mem readout, object HUD, collision view and step override enabled in the recomp by data pokes and keypad 1–4; retail hotkeys incl. Ctrl+I+R; the Dreams Editor (`DREAMS.DAT` editor) running with a restored mouse and draw call, its broken file pickers; dead demo-recorder start and debug draws |
| [Spec 006 — finish Dreams rendering on Windows](specs/006-recomp-glide-renderer/spec.md) | Windows/D3D11 game and development/debug rendering; visual fidelity with imperceptible GPU/CPU differences accepted, protected game/memory contracts, finite Windows acceptance gates; live direct slice implemented, completion pending; [current status and evidence](specs/006-recomp-glide-renderer/implementation.md) |
| [Spec 007 — running the port](specs/007-port-launcher/spec.md) | **Planned**: a Dear ImGui launcher (disc setup and settings, shown on every start) instead of an installer, the user's two `.cue` disc images read in place, both discs open with the host switching on the game's marker polls, saves in a per-user directory, edition check by hash |

## Research

| Doc | Contents |
|---|---|
| [re-status.md](research/re-status.md) | **Current RE status and priorities**: recovered behavior, checked coverage, remaining gaps, field corrections and validation commands |
| [glide-renderer.md](research/glide-renderer.md) | **Faithful GPU contract**: complete face dispatch, clamp/wrap, deferred alpha, depth, texture uploads, palette cache and fog |
| [glide-call-inventory.md](research/glide-call-inventory.md) | **All 35 called Glide imports** in DREAMSFX.EXE, their 76 direct call sites, movie reachability and modern render mapping |
| [lighting.md](research/lighting.md) | **Palette and object lighting**: project RGB fields, row generation, actor binding, shared RNG, light records and remaining limits |
| [scene-placement.md](research/scene-placement.md) | **All 95 render graphs**, eleven former collision fallbacks, signed UVs and ARC's source topology |
| [re-setup.md](research/re-setup.md) | **Ghidra setup**, repo layout for RE, suggested order of attack; LE loader for the DOS builds, Glide typing, cross-build function matcher; **function naming**: registry, checked facts, doc comments in Ghidra |
| [engine.md](research/engine.md) | Engine architecture, toolchain, the four binaries, subsystem layout; renderer backends, **Glide ↔ DirectDraw/GDI map**, input and joystick paths |
| [toolchain.md](research/toolchain.md) | **Watcom 11.0** for the Windows builds, a 10.6/11.0 mix for DOS, with byte-exact and blind matching evidence; flags for matching decompilation; reference material; ~290 runtime symbols recovered per DOS binary |
| [localized-build-symbols.md](research/localized-build-symbols.md) | **Dutch and Spanish OMF symbol residue**: original renderer names, source paths and four missed English function entries |
| [edition-comparison.md](research/edition-comparison.md) | **Four retail editions**: project-bank revisions, shared visuals versus dubbed movie audio, extra voice/font assets and isolated byte anomalies |
| [binary-edition-comparison.md](research/binary-edition-comparison.md) | **Game executable differences across editions**: PE/LE sizes and layouts, imports, function matching and decompilation implications |
| [wip-editor-discovery.md](research/wip-editor-discovery.md) | **July demo editor discovery**: separate Ghidra project, 5,147 original function names, the 351-node editor, native mouse input, active EDITOR.DAT, recovered type layouts and 35 more retail names |
| [wip-layout-compiler.md](research/wip-layout-compiler.md) | **Linker layout and compiler-aware naming**: 88 more Windows names, exact module contributions, shared tails, biased arrays, two-profile compiler reproduction and candidate-hidden reviews |
| [wip-windows-transfers.md](research/wip-windows-transfers.md) | **Further Windows names from the demo**: 148 new original names, 79 original-name annotations, loaded-body hashes, relocation/address consistency checks and held cases |
| [wip-binary-comparison.md](research/wip-binary-comparison.md) | **July 1997 demo vs retail**: embedded Watcom symbols, 1,151 Windows body matches, six verified original names applied to both Windows builds, and surviving engine type tables |
| [cryolib.md](research/cryolib.md) | `CRYO.DLL` = CryoLib: 165 exports incl. a working **HNM6 decoder** |
| [game-content.md](research/game-content.md) | 150 levels, 30 inventory items, save system, from `DREAMS.INI` |
| [boot-sequence.md](research/boot-sequence.md) | **Boot flow decompiled** — intro → generic → menu → new game → head video → first map, with videos and menus named |
| [level-map.md](research/level-map.md) | **Complete project → scene map** — all 150 projects to 95 unique `.DSN` files |
| [dsn-loader.md](research/dsn-loader.md) | **`.DSN` loader decompiled** — header reader, and how `__watcall` was fixed |
| [scene-geometry.md](research/scene-geometry.md) | **`.DSN` to glTF** — vertex pool, face records, the stale-pointer problem, how to check a decode |
| [models.md](research/models.md) | **`.DAN` character and prop models** — the scene-graph node, skeletons, bridging faces, texture pages |
| [CAISSE retail render chain](research/cai-prop-retail-chain.md) | Project 71 `OBJET2` → `CAI.DAN` model/resource load → spawn transform → Windows frame draw |
| [animation-validation.md](research/animation-validation.md) | **Animation hypothesis harness** — named track bindings, translation fingerprints, elbow/knee limits, remaining uncertainties |
| [animation-timing.md](research/animation-timing.md) | **Original timing trace** — 200 Hz clock, 30 animation frames/second, actor modifiers and timer quirks |
| [animation-subsystem.md](research/animation-subsystem.md) | **Shared runtime** — NPC/player animation, complete rig exports, inspector, playback and lifecycle |
| [animation-smoothing.md](research/animation-smoothing.md) | **Smoothing** — recovered quaternion splines, easing, missing controls, and initialization-pose loop fix |
| [animation-root-blending.md](research/animation-root-blending.md) | **Root movement and blending** — translation curves, extracted deltas, original transition timing and inspector experiments |
| [ai-animation-runtime.md](research/ai-animation-runtime.md) | **Retail AI and action selection** — actor scheduler, project AI transition lists, NPC action requests, and player movement-to-clip lookup |
| [sprites-ui-dialog.md](research/sprites-ui-dialog.md) | **Retail sprites, menus, and timed dialogue** — indexed/blended pixels, decoded fonts, separate UI loops, and voice/captions |
| [assets.md](research/assets.md) | **Models, textures, animation, sound** — where content lives and how it's packed |
| [file-formats.md](research/file-formats.md) | Asset format catalogue with verified magic numbers |
| [hnm-video.md](research/hnm-video.md) | HNM inventory **and how to decode it** — all 113 videos decode |
| [hnm6-spec.md](research/hnm6-spec.md) | Full HNM6 container + codec specification (MultimediaWiki, mirrored) |
| [disc-layout.md](research/disc-layout.md) | Both discs inventoried, install manifest, disc-check mechanism, merge map |
| [install-and-discs.md](research/install-and-discs.md) | **Install, data roots and the disc swap**: what each installer copies, which files the game reads from the install, the data root and the CD, level numbers → disc, the full-install cache, what a port needs |
| [asset-access.md](research/asset-access.md) | **Recovered asset lookup and file access**: disc/install roots, UBIK VFS, project and DSN/DAN resource paths, dialogue and sound banks; contract for spec 002 |
| [running.md](research/running.md) | How to actually run the game, ranked by difficulty; **3dfx build verified** under DOSBox Staging; controllers |
| [research-log.md](research/research-log.md) | Findings log, corrections, dead ends, open questions |

## Function names

Functions are named `MODULE_VerbObject` (convention in `../AGENTS.md`). Every
name is recorded in `../re/names/<program>.tsv` with its sources and the facts
it rests on, checked by `re/tools/check_names.py`. Pages that carry the
*Function names verified* note cite only names from that registry.

## Evidence tags

Claims carry one of three tags. Respect them — do not promote a tag without new evidence.

For current conclusions, start at [re-status.md](research/re-status.md) and follow its
subsystem references. Dated log entries and sections explicitly marked
historical preserve previous readings; they are not current specifications.
Decoding data, naming functions and validating a running port are separate
levels of evidence.

- **[verified]** — observed directly in the files/binaries on this machine, with the
  observation reproducible from the commands recorded in the doc.
- **[sourced]** — taken from external documentation, always linked.
- **[unverified]** — inference or hypothesis. Not tested. May be wrong.

## Conventions

- Configure local disc paths in `.dreams.local.env` (see `../.dreams.example.env`).
  Absolute paths elsewhere in these docs record where an observation was made.
- Nothing in this repo contains copyrighted game data. Notes and tools only.
- Offsets are decimal unless prefixed `0x`. All multi-byte values in Cryo formats
  are little-endian.
