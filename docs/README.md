# Docs index

Technical documentation for *Dreams to Reality* (Cryo Interactive, 1997).

`../AGENTS.md` lists project paths and commands. It holds no findings; these
docs do.

| Doc | Contents |
|---|---|
| [north-star.md](north-star.md) | **OpenDreams, the engine we are building**: goal, decisions (C++17, SDL3, sokol_gfx, native loaders), the original game code ported as a fixed-step loop with a free GPU renderer, milestones, the RE work it depends on |
| [Spec 000 — the recomp](specs/000-the-recomp/spec.md) | The static recompilation, differential tests and matching decompilation under `out/recomp/`; W1–W3 done: tools on Watcom 11.0, function boundaries fixed in Ghidra, 131 prototypes proven by byte-exact compiles; the recomp's collision bug traced to a lifter flag defect |
| [Spec 001 — project initialization](specs/001-project-init/spec.md) | C/C++ foundation, build/dependencies and two runnable Hello World applications through SDL3, sokol and Dear ImGui |
| [Spec 002 — disc and source-asset navigation](specs/002-disc-navigation/spec.md) | Browser requirements for cue/bin images, source files, indexed entries, project references and tracks; no media previews or playback |
| [Spec 003 — ODViewer asset previews and playback](specs/003-level-load-preview/spec.md) | Single viewer implementation spec; CAI/Project 71, HNM5/HNM6 movies, static image previews, and FSB/DRD/CD audio playback are implemented |
| [Spec 004 — ODRuntime boot and main menu](specs/004-runtime-boot-menu/spec.md) | Draft runtime front end: recovered intro, looping movie menu, input/controller adaptation, New Game handoff and branch plan |
| [Spec 005 — retail debug tools](specs/005-debug-tools/spec.md) | **The shipped debug layer**: Frame Rate/Mem readout, object HUD, collision view and step override enabled in the recomp by data pokes and keypad 1–4; retail hotkeys incl. Ctrl+I+R; the Dreams Editor (`DREAMS.DAT` editor) running with a restored mouse and draw call, its broken file pickers; dead demo-recorder start and debug draws |
| [ODViewer browser implementation](reviews/002-viewer-browser.md) | Inspection layer versus retail ports, provenance of indexed fields, corpus validation and remaining navigation checks |
| [re-status.md](re-status.md) | **Current RE status and priorities**: recovered behavior, checked coverage, remaining gaps, field corrections and validation commands |
| [glide-renderer.md](glide-renderer.md) | **Faithful GPU contract**: complete face dispatch, clamp/wrap, deferred alpha, depth, texture uploads, palette cache and fog |
| [glide-call-inventory.md](glide-call-inventory.md) | **All 35 called Glide imports** in DREAMSFX.EXE, their 76 direct call sites, movie reachability and modern render mapping |
| [lighting.md](lighting.md) | **Palette and object lighting**: project RGB fields, row generation, actor binding, shared RNG, light records and remaining limits |
| [scene-placement.md](scene-placement.md) | **All 95 render graphs**, eleven former collision fallbacks, signed UVs and ARC's source topology |
| [re-setup.md](re-setup.md) | **Ghidra setup**, repo layout for RE, suggested order of attack; LE loader for the DOS builds, Glide typing, cross-build function matcher; **function naming**: registry, checked facts, doc comments in Ghidra |
| [engine.md](engine.md) | Engine architecture, toolchain, the four binaries, subsystem layout; renderer backends, **Glide ↔ DirectDraw/GDI map**, input and joystick paths |
| [toolchain.md](toolchain.md) | **Watcom 11.0** for the Windows builds, a 10.6/11.0 mix for DOS, with byte-exact and blind matching evidence; flags for matching decompilation; reference material; ~290 runtime symbols recovered per DOS binary |
| [localized-build-symbols.md](localized-build-symbols.md) | **Dutch and Spanish OMF symbol residue**: original renderer names, source paths and four missed English function entries |
| [edition-comparison.md](edition-comparison.md) | **Four retail editions**: project-bank revisions, shared visuals versus dubbed movie audio, extra voice/font assets and isolated byte anomalies |
| [binary-edition-comparison.md](binary-edition-comparison.md) | **Game executable differences across editions**: PE/LE sizes and layouts, imports, function matching and decompilation implications |
| [cryolib.md](cryolib.md) | `CRYO.DLL` = CryoLib: 165 exports incl. a working **HNM6 decoder** |
| [game-content.md](game-content.md) | 150 levels, 30 inventory items, save system, from `DREAMS.INI` |
| [boot-sequence.md](boot-sequence.md) | **Boot flow decompiled** — intro → generic → menu → new game → head video → first map, with videos and menus named |
| [level-map.md](level-map.md) | **Complete project → scene map** — all 150 projects to 95 unique `.DSN` files |
| [dsn-loader.md](dsn-loader.md) | **`.DSN` loader decompiled** — header reader, and how `__watcall` was fixed |
| [scene-geometry.md](scene-geometry.md) | **`.DSN` to glTF** — vertex pool, face records, the stale-pointer problem, how to check a decode |
| [models.md](models.md) | **`.DAN` character and prop models** — the scene-graph node, skeletons, bridging faces, texture pages |
| [CAISSE retail render chain](reviews/cai-prop-retail-chain.md) | Project 71 `OBJET2` → `CAI.DAN` model/resource load → spawn transform → Windows frame draw |
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
| [libcdio Windows spike](reviews/002-libcdio-windows-spike.md) | MSVC build and direct comparison: original split-track CUEs fail to expose ISO files; one-track CUEs list both discs completely |
| [ISO-reader options](reviews/002-iso-reader-options.md) | MSVC/lib9660 reads original data-track BINs and matches both file counts; libarchive lists both derived ISOs; integration gaps and hardening findings |
| [asset-access.md](asset-access.md) | **Recovered asset lookup and file access**: disc/install roots, UBIK VFS, project and DSN/DAN resource paths, dialogue and sound banks; contract for spec 002 |
| [running.md](running.md) | How to actually run the game, ranked by difficulty; **3dfx build verified** under DOSBox Staging; controllers |
| [pipeline.md](pipeline.md) | Historical notes for the retired web viewer's extract → bake → pack pipeline |
| [research-log.md](research-log.md) | Findings log, corrections, dead ends, open questions |

## Function names

Functions are named `MODULE_VerbObject` (convention in `../AGENTS.md`). Every
name is recorded in `../re/names/<program>.tsv` with its sources and the facts
it rests on, checked by `tools/check_names.py`. Pages that carry the
*Function names verified* note cite only names from that registry.

## Evidence tags

Claims carry one of three tags. Respect them — do not promote a tag without new evidence.

For current conclusions, start at [re-status.md](re-status.md) and follow its
subsystem references. Dated log entries and sections explicitly marked
historical preserve previous readings; they are not current specifications.
Decoding data, naming functions and validating a running port are separate
levels of evidence.

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
