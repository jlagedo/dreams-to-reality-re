# North star: OpenDreams

What we are building, why, and the decisions already made. This page is the
reference for every design choice in OpenDreams; change it before changing
the code. Research findings stay in the other docs; this one only points at
them. Current evidence, coverage and remaining work are consolidated in
[re-status.md](re-status.md).

## Goal

**OpenDreams** is a new engine that runs *Dreams to Reality* from the original game data, in
the spirit of [OpenLara](https://github.com/XProger/OpenLara): the 1997 game,
its levels, characters, animation, AI, menus, dialogue and videos, on modern
machines, at any resolution, with real GPU 3D. Not a remake: a port of the
original game code, running on new plumbing, that keeps the game's behaviour
and look and runs outside DOSBox.

Requirements, from the owner:

1. **Portable**: Windows, macOS, Linux, and a browser build (WebGL2 via
   Emscripten), from one code base. **Desktop first**: the browser build is
   kept compiling from the start so no unportable code accretes, but web
   work (pack, delivery, caching) is parked until the game plays on desktop.
2. **No software rasterizer.** The original's span fillers (`SW_*`, about
   43 KB of `WINDREAM.EXE`) are not ported. Rendering is done on the GPU.
3. **Keep the game's identity while modernizing the runtime**: high
   resolution, real perspective 3D, a renderer that runs at the display's
   rate. The rendering is improved; the game is not reworked. The game logic
   is ported from the original, not redesigned.
4. **One renderer, two destinations**: ODRuntime renders into the entire
   drawable window or fullscreen target. ODViewer renders the same scene or
   movie rules into an offscreen target sized to its preview content region.
   Windows retail code is the game and traversal base; the 3dfx build is the
   reference for replacing the software renderer at the paired backend cuts.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Language | **C++17**, no exceptions, no RTTI, standard library allowed | Close to the decompiled C; RAII and containers without a runtime; compiles everywhere including Emscripten |
| Platform layer | **SDL3**: window, input, audio, timers, file dialogs | One API on all four targets |
| Rendering | **sokol_gfx** with the platform's native backend (D3D11 on Windows, Metal on macOS, OpenGL on Linux, WebGL2 in the browser) | Small, C, one renderer for every target; SDL3's own GPU API has no browser backend |
| Tool UI | **Dear ImGui**, wired through SDL3 and sokol in the foundation | Both applications exercise it with Hello World in spec 001; ODViewer uses it for navigation from spec 002. Original game menus/HUD retain their recovered implementation |
| Video | HNM4/5/6 decoded by our own C++ decoder, from the spec in [hnm6-spec.md](hnm6-spec.md) and the decoder in `CRYO.DLL` ([cryolib.md](cryolib.md)) | No dependency on `CRYO.DLL` or on ffmpeg at runtime |
| Game data | **The runtime reads the two disc images** (`.cue`/`.bin`) through its own ISO 9660 reader and **native loaders for the original formats**. No extraction step for players. | Proves the reverse engineering; one code path; point at the original media and play, as OpenLara does |
| Music | **Read straight from the images' audio tracks** (raw 44.1 kHz PCM sectors) | The original plays CD audio through MCI; the image already holds it, so nothing is ripped or transcoded on desktop |
| Web pack | **Parked (desktop first).** When taken up: the only preprocessing; `opendreams pack` reads the images and writes a chunked, versioned pack: the loaders' structs serialized, music and voice as **Vorbis** (stb_vorbis), video in a browser-friendly form | The browser cannot fetch 1.4 GB or decode raw HNM fast enough; made by the same loaders, so there is no second format world |
| Repository | **This repository**, new top-level `opendreams/` directory; the Python toolkit stays as the reference decoder and test oracle; the Babylon.js viewer is frozen and retired once ODViewer covers its inspection workflows and browser use | Cross-checking C++ loaders against the Python decoders is a local test, not a cross-repo chore |
| Applications | **ODShared**, the reconstructed engine shared by **ODRuntime**, the game, and **ODViewer**, the asset browser and port-validation tool | One retail-function port for game asset access and presentation; both applications consume it, and gameplay debugging stays in ODRuntime |
| Fidelity | **Port the original code, fixed step, free renderer** (below) | The only option where the result is recognisably the same game |
| Licence | MIT, like the rest of the repository; no game data in the repository, ever | |

## Port the original code, fixed step, free renderer

The port preserves the original game's code and replaces the plumbing that
lets it run on a modern machine instead of inside DOSBox. Keeping behaviour
faithful does not require reorganizing the recovered source into isolated
subsystem libraries; necessary replacements must remain traceable to the
original functions and call sites.

### The game is a port of the original code

Everything that decides *what happens* is ported function by function from
the decompiled `WINDREAM.EXE`, keeping the original's structure, names
(the registry in `re/names/`), data layouts, constants, its maths as written
(Q15 fixed point for transforms, floats and doubles for time and animation),
rounding and order of operations: the game tick (`GAME_Tick`), entity and
actor updates, the AI scheduler and its transition lists, the action-to-clip
tables, player movement, physics integration and collision, level exits,
triggers, damage, inventory and spells, menus, dialogue timing, save games.
See [ai-animation-runtime.md](ai-animation-runtime.md),
[animation-timing.md](animation-timing.md),
[animation-root-blending.md](animation-root-blending.md),
[boot-sequence.md](boot-sequence.md), [sprites-ui-dialog.md](sprites-ui-dialog.md).

This is the shape OpenLara's author arrived at on his second attempt (the
fixed-point engine in `src/fixed`, with the original's handler tables, integer
maths and random generator) after a first engine rebuilt "from behaviour"
drifted from the original at every frame rate. We start there. What is
replaced, not ported: the Windows and DOS plumbing (DirectDraw, GDI, MCI,
Miles, Glide, Watcom runtime, the message loop's OS half) and the software
rasterizer. Where the docs have not settled a behaviour, the rule is to go
back to the binary, not to invent.

The game runs as a **fixed-step loop at 30 steps per second**, passing
**Δt = 1.0** to the ported code each step. The original derives delta from a
200 Hz counter and clamps it to [0.2, 5.0]; its own demo recorder forces 1.0.
The recovered integrator and landing damage depend on delta and frame count,
so the step must stay fixed. See [engine.md](engine.md#the-fixed-step-verified)
for the trace and comparison. Three steps correspond to 20 counter ticks;
an accumulator represents this ratio without rounding each step to 6 or 7
ticks. The game never learns the display's refresh rate.
Consequences: gameplay is the same on every machine; the game is
deterministic (same inputs, same game), so replays and regression tests
against the original are possible.

The renderer draws the latest completed step. On a fast display that looks
like 30 fps for 3D motion, while menus, video, text and the camera-free UI
already run at the display rate. Smooth 3D motion at higher frame rates
(drawing positions blended between the last two steps) is an optional
renderer-only enhancement for milestone 7: the game code never depends on it,
and it can be dropped without touching gameplay. OpenLara never built it;
we do not require it.

Ruled out: changing mechanics because they feel dated; silently fixing
original bugs. Fixes and tweaks are options layered on top of the faithful
defaults, off by default, documented.

### The renderer is free

The renderer reads simulation state and draws it however we like. Its input
crosses the same boundary the original engine had: `REND_DrawObject`
(`0x47e498`) calls one **object hook** per visible object, and each build
linked a different hook (Glide, software). See "Renderer backends" in
[engine.md](engine.md).

We keep that boundary but move what crosses it **one stage earlier**. Glide 2
takes screen-space vertices, so the 3dfx hook received geometry already
transformed, projected, lit and clipped by the engine in fixed point for a
640x480 viewport (`REND_ProjectVertices`, `0x47b228`). Wiring a modern
renderer there would be a Glide emulator: upscalable, but locked to 4:3 and
the original FOV, culled at 640x480 limits, and unable to render more frames
than the simulation produces. Instead the hook's input is the *input* of the
transform stage:

- the object, its world transform and posed skeleton from the latest step;
- its face list in model space, with the per-face type codes the original
  hooks switch on, material, texture page, clamp and chroma-key flags;
- the camera (position, orientation, the original FOV by default);
- the lighting inputs the original used to compute vertex colours.

The GPU does view, projection, clipping and depth at any resolution, aspect
ratio and FOV.

The **3dfx build is the specification of the faithful look**. What
`GLIDE_DrawObjectFaces` and `GLIDE_Open` set up is what we reproduce by
default: bilinear texture filtering, gamma 0.8, decal texture with chroma-key
transparency, texture x Gouraud lighting, flat constant colour, per-face
texture clamp, the fog table, the depth function, the clears. Enhancements
(higher-resolution textures, draw distance, widescreen, filtering choices)
are toggles on top.

The original projection stays in the code base as a **reference path**, not
as the renderer: given an object and camera, it computes the 640x480 screen
positions the 1997 engine would have produced, and a test checks that the GPU
path lands within a pixel of them. That is how camera, FOV and aspect are
proven right.

**What the game reads back from the render stage** (traced in the binary,
2026-09-26; details in [engine.md](engine.md), "What the game reads from the
renderer"): no screen coordinates, no clip or cull flags, no lighting. One
value only: `REND_DrawObject` composes each object's position down the node
tree whose root is the camera, and stores the result at node `+0x4c` in
**camera space**. Three ported systems read it, one tick late: positional
sound (distance to volume, x to pan), the AI line-of-sight check in
`AI_TickCombat`, and the attack-object line-of-sight check in
`ENT_TickAttackObject`; the last two convert it back to world space with the
*current* camera, so their "world position" drifts while the camera moves.
Rule: **the game computes and stores that root camera-space position
itself**, with the original arithmetic, at the point where the original
rendered (`REND_DrawFrame`/`REND_DrawFrameEx`, after the collision separation
at `0x40bff8`); the renderer neither writes nor reads it. Everything else in
the original render stage (composition of the other nodes, sphere cull,
transform, projection, clipping, object shading, environment-map UVs) moves to
the GPU renderer. Original palette-state updates retain their fixed-step
timing and shared RNG consumption; only their visual application moves with
rendering (see [lighting.md](lighting.md)).

**Widescreen.** The 3D view is Hor+: the original vertical FOV is kept and
wider screens see more at the sides. Menus, HUD, text and video are laid out
on a 4:3 canvas centred in the window. A "faithful 4:3" toggle pillarboxes
the 3D view. The projection reference test checks the central 4:3 region.

**Stored triangles.** The Glide hook submits three-corner face records; even
four-sided shapes are already triangulated in the data. Preserve those
triangles instead of choosing a new quad diagonal. The recovered face modes,
samplers, palette cache, transparency, depth and fog are specified in
[glide-renderer.md](glide-renderer.md).

## Architecture

**Preserve the recovered source structure.** This is a faithful decompilation
rebuild, with necessary adaptations, and functions should remain easy to compare
side by side with the decompilation. Keep supported source groupings, function
names, tables, shared globals and call order. The original filenames and directory
tree are lost; [source-block analysis](re-setup.md#source-file-blocks-find_modulespy)
provides evidence with both proven and candidate boundaries, not an exact tree.
Do not infer a source file from a descriptive function prefix alone.

The priorities, in order, are:

1. Preserve recognizable functions, recovered source groupings, state and
   execution order.
2. Replace what needs replacing for portability, GPU rendering and the agreed
   timing model.
3. Document those changes so they can be traced back to the original.

Globals, mixed responsibilities, awkward call chains and strange function
boundaries can stay when they reflect the original. They help us compare the
port beside the decompilation and catch mistakes. **ODShared can start as one
tangled library shared by two executables.** Cleaner boundaries can emerge where
the evidence supports them; they are not prerequisites for getting the game
rebuilt.

Start with one ODShared library and two applications. ODViewer reads and previews
models, maps, props, animations, images, fonts, sound and video. It may initialize
shared resources and link the whole engine, but does not run gameplay. ODRuntime
runs the game and owns its optional gameplay debugging overlay. The initial
structure below distinguishes reconstructed code from new supporting code;
these directory names do not claim to reproduce Cryo's original paths.

ODViewer is a consumer and validation harness for the ported retail functions.
For any game-format or asset-lookup behavior with a retail counterpart, trace
and port that function and its required call path in ODShared, then use the same
implementation from ODViewer and ODRuntime. After an asset is obtained through
that path, ODViewer may add read-only decoders or presentation for information
the retail game did not compute. These extensions must not replace a retail
function or become the runtime's source of game behavior. New CUE/BIN and ISO
access, SDL/GPU integration, catalog search and ImGui controls are supporting
code because the retail game had no equivalent.

```text
opendreams/
  shared/              ODShared library
    port/              Reconstructed source units, including game and engine code
    platform/          SDL3/OS replacement implementations as needed
    render/            New sokol GPU implementation and shaders
    ui/                Shared Dear ImGui setup and SDL/sokol integration
    support/           New support, including cue/bin and ISO 9660 access
  apps/
    runtime/           ODRuntime entry point and optional gameplay debugging UI
    viewer/            ODViewer asset browser and preview controls
  tests/               Function, reference and corpus comparisons
  tools/               Pack writer and trace/compare tools, when needed
```

The detailed draft and evidence review are in
[001 — project initialization](specs/001-project-init/spec.md).

Rules:

- **Adapt at the recovered call sites.** ODShared may depend on SDL and sokol.
  Prefer narrow platform replacements, preserving original callers and state
  where practical. There is no mandatory dependency-free `core` or `sim` target.
  A function that mixes gameplay and platform work need not be split merely to
  satisfy a new layer rule. Document necessary changes and their evidence.
- **Keep game decisions independent of presentation timing.** Preserve the
  fixed-step policy and known renderer feedback described above. Tests may run
  selected paths headlessly with test services; headless execution does not
  require removing all platform libraries from the link.
- **Browse and preview the same retail implementation.** ODViewer uses recovered
  lookup, loader and playback functions with their required setup. Catalog
  adapters expose their results for search and provenance; viewer-specific
  interpretation may enrich already loaded assets where no retail function
  supplies the desired detail. ODRuntime uses the same ported functions, never
  a duplicate viewer loader. Investigate gameplay-dependent state through
  ODRuntime's overlay or focused tests; ODViewer does not host a game session.
- **Restore the original development tools first.** Start ODRuntime debugging
  with the surviving object HUD (`DBG_DrawObjectInfo`), collision wireframe
  (`DBG_DrawCollisionMesh`) and free-camera mode. Keep recovered routines in
  their source groupings, adapt text/line drawing to the GPU and verify their
  activation paths. New runtime controls and diagnostics extend these tools.
  The original free camera changes the game camera and may affect sound and
  line-of-sight behaviour; preserve it as an explicit state-changing developer
  mode. A later inspection-only camera would be a separate addition.
- **Inputs are logical actions per player, resolved once per step** from
  bindings held as data (keys, gamepad). The original polls a key-state table
  each tick (`INPUT_PollKeyboard`); the platform layer fills that table from
  the bindings. Replays record the table per step.
- **The original random generator, called only by ported code.** The game
  calls Watcom's `rand_` from 21 functions and never seeds it, so the sequence
  is fixed from launch. This includes **three draws per normal palette-lighting
  update** (`REND_TickPaletteLighting`), despite their visual purpose. Preserve
  these original calls and their order on the simulation clock; renderer-only
  enhancements, audio and new UI effects cannot consume this stream. See
  [lighting.md](lighting.md). GPU application of palette state does not move
  the original RNG calls onto the display clock.
- **Floating point: keep it simple.** The port uses the same float and double
  types as the original but does not emulate the x87; results differ from
  the original only in the last bits, which nobody can see. Two build rules
  keep the port's own replays identical on every target: no fast-math and no
  multiply-add fusion (`-ffp-contract=off`), and one `sin`/`cos` of our own
  instead of each platform's.
- **Clock discipline.** After a load, a stall or a video, accumulated time is
  dropped rather than caught up; catch-up is capped at a few steps. Dialogue
  and captions follow game time; the mixer follows the game.
- **Loaders produce plain structs**, checked against the Python decoders'
  output in tests. The Python toolkit is the oracle; the C++ loader is the
  product.
- **No game data in the repository.** Tests that need data read it from the
  configured game folder and skip when it is absent, like the Python
  `corpus` tests.
- **The web build is the same program** compiled with Emscripten, reading a
  pack over HTTP.
- **The VFS is case-insensitive** (DOS filenames on Linux, macOS and the web)
  and language-directory aware (`DATA\LANG\<language>\`), with captions
  loaded alongside voice.
- **Settings are a versioned struct** persisted by the platform code (user
  directory on desktop, IndexedDB in the browser).
- **Video is clocked by its audio.** The HNM decoder is also a mixer source;
  the frame shown is the one for the current audio sample position. Frames are
  held, not blended, on fast displays.
- **Debug time controls come with the fixed step**: pause, single-step, and
  rewind by replaying inputs from a snapshot, in ODRuntime from milestone 4.

## Data

**Input: the disc images, nothing else.** The player points the runtime at
the two `.cue`/`.bin` images. The VFS mounts both at once: the data tracks
through an ISO 9660 reader, with the precedence between discs taken from the
merge map in [disc-layout.md](disc-layout.md), and the audio tracks as raw
PCM (11 + 13 tracks). There is no install step, no extracted folder and no
"insert disc 2". Format decoders and their remaining coverage limits are
listed in [re-status.md](re-status.md), [assets.md](assets.md) and
[file-formats.md](file-formats.md). A plain folder can also be mounted, for
development only; it is not a supported way to play.

**The pack is the only preprocessing, it exists for the browser, and it is
parked until the desktop game plays.** The desktop build reads the images and
never needs a pack. When the web build is taken up (milestone 7),
`opendreams pack <disc1.cue> <disc2.cue> <out/>` reads the images with the
same loaders the game uses and writes their structs serialized, chunked so
the browser fetches on demand and caches (IndexedDB), the way OpenLara's web
build fetches level files; music and voice go to Vorbis (stb_vorbis decodes
it in the runtime; SDL3 does not decode compressed audio). Versioned, rebuilt
from the images, never shipped. Container, chunk boundaries and what happens
to video are the parked decisions listed at the end.

The current Python `extract`/`bake`/`pack` pipeline ([pipeline.md](pipeline.md))
keeps serving the Babylon viewer until ODViewer covers its inspection workflows
and browser use. New format knowledge lands in the Python decoders first (they are the
oracle) and in the C++ loaders second.

## Milestones

Each milestone ends with something that runs on Windows, macOS and Linux
and still compiles for the browser.

1. **Foundation (spec 001).** CMake project, dependencies and framework wiring.
   ODRuntime and ODViewer both run and display Hello World through shared SDL3,
   sokol and Dear ImGui integration, with working input and presentation. Exercise
   shader generation with a procedural background. CI for Windows/macOS/Linux
   plus Emscripten compile checks for both applications. No game-data loading.
2. **Asset browser, in stages.** **[Spec 002](specs/002-disc-navigation/spec.md)** mounts the two disc images,
   ports the retail lookup/open functions needed for asset navigation into
   ODShared, and makes ODViewer consume them across both sources. It presents
   physical files, supported indexed entries and project references, with
   viewer-only detail allowed where no retail function supplies it. No media
   previews or playback are required in 002. **[Spec 003](specs/003-level-load-preview/spec.md)**
   owns the viewer preview slices. It first exercises a selected DAN model
   through an in-memory preview project, adapted level-load path and native
   GPU drawing; Project 71 checks the real record path. The same spec extends
   the ported paths through models, animation, props, level geometry,
   textures, sprites/fonts, sounds, music, voice/captions and videos. Check
   each loader against Python and match renderer material modes against the
   3dfx build as previews arrive.
3. **Level viewer and projection (Spec 003 slice).** ODViewer previews levels with materials for
   all 95 scenes (including the former collision-vote fallbacks), with the projection
   reference test. Gameplay-dependent camera behaviour is exercised in
   ODRuntime or focused tests, not a game session embedded in ODViewer.
4. **First playable level.** ODRuntime with the original camera (`CAM_CompCameraPos`),
   player movement from the original tables,
   collision, physics, level exits, the HUD. Fixed-step loop at 30 Hz with
   Δt = 1.0; the renderer draws the latest completed step. Optional motion
   interpolation belongs to milestone 7.
5. **Gameplay systems.** AI scheduler, NPC actions, combat, inventory and
   spells, menus, dialogue, saves, the boot flow with videos. Level by level
   against the project map ([level-map.md](level-map.md)).
6. **The whole game.** All 150 projects playable start to finish.
7. **Polish and the web.** Enhancement toggles (including, if wanted, smooth
   3D motion above the step rate), a gamepad layout (a bindings table; the
   original is keyboard only, [engine.md](engine.md) "Input"), desktop
   packaging; then the parked web work: `opendreams pack`, on-demand loading
   with a browser cache, the web release.

## Reverse-engineering work the runtime depends on

The current evidence and ordered backlog live in [re-status.md](re-status.md).
The earlier blockers have narrowed substantially:

- **Milestones 2–3:** all 95 scenes now use the original tag-1 render graph;
  collision placement is diagnostic. Signed UVs and face types are preserved;
  ARC's open boundaries are source topology. Implement the recovered
  [Glide contract](glide-renderer.md) and [palette/light preparation](lighting.md),
  then validate against original-game screenshots. Remaining narrow questions
  include zero-density fog, some scene/video descriptors and type-2 light details.
- **Milestone 4:** collision/physics, player controllers and camera behavior
  are documented well enough to begin the port. The step is settled at
  Δt = 1.0. Remaining arithmetic, flags and special cases must be traced as
  the first playable slice is built and compared against the original.
- **Milestones 5–6:** complete trigger/effect semantics and special actors,
  then validate progression across 150 projects. Save layouts and the English
  build's compiled-in text path are already traced in
  [game-content.md](game-content.md); integration and round-trip checks remain.

Named-function counts are evidence coverage, not a percentage of the game
ported. The software rasterizer and obsolete OS plumbing are outside the port.

## What we take from OpenLara

OpenLara (`E:\dev\OpenLara`, studied 2026-09-26; notes in
`out/scratch/openlara-study.md`) is the closest precedent. It contains two
engines: a float engine that simulates at the render rate, and a later
fixed-point 30 Hz engine for consoles shaped like the original game's code.
The second is the shape we start from.

Copied: the thin platform contract (a handful of `os*` functions, logical
input, a pull-based mixer callback); logical inputs per step; separate
gameplay and visual randomness; animation events fired over the frame range
`(previous, current]` so no event is skipped or repeated across steps;
per-file on-demand loading with a browser cache; the video decoder as a mixer
source; every shader/pipeline permutation created at level load, then the
clock reset; debug overlays (collision, paths, lights, portals) as the
level-viewer tooling; case-insensitive file lookup.

Avoided: a variable-step float simulation; simulation and rendering in one
class with logic reading global settings; hand-maintained shaders per
graphics API (we use `sokol-shdc`); a unity build with a script per platform
and no CI; enhancements on by default and unflagged; gameflow hard-coded in
C++ instead of read from the game's own data; identifying data versions by
file size instead of content.

## How fidelity is tested

- **Loaders**: every C++ loader's output is compared with the Python decoder's
  on the full disc corpus.
- **Projection**: the reference path versus the GPU path, per object, within a
  pixel at 640x480.
- **Animation and timing**: frame counters and blend timings against the
  traces in [animation-timing.md](animation-timing.md) and
  [animation-root-blending.md](animation-root-blending.md).
- **Behaviour**: recorded input sequences replayed in the fixed-step
  simulation must reproduce recorded positions; where a trace of the
  original exists, the same sequence is compared against it within a small
  tolerance (a unit or two in position, the same animation state), not bit
  for bit.
- **Look**: screenshots of `DREAMSFX.EXE` under DOSBox
  ([running.md](running.md)) next to the runtime at 640x480 with the
  faithful settings.

## Non-goals

- Porting the software rasterizer or the DirectDraw/GDI presentation.
- Running the original executables, patching them, or wrapping them.
- New content, new mechanics, new levels.
- A general-purpose engine or editor. ODViewer is a maintained, separately
  distributable asset browser; it does not host gameplay or author levels.
- Shipping game data. The player supplies the discs.

## Open decisions

- **Fixed step size is settled:** 30 Hz, Δt = 1.0. Original demo playback
  timing remains a validation question, not a choice of simulation rate.
- **Parked until the web build (milestone 7), desktop first:**
  - **Pack format**: container, compression, chunk boundaries. Starting point
    on record: one chunk per original file as a relocatable image plus a
    manifest mapping projects to chunks; no format-level compression (the
    host's brotli/gzip does it); audio to Vorbis is the only transcoding.
  - **Video in the web pack**: keep HNM and decode it in WebAssembly (the
    codec targets a 1997 Pentium; same decoder and player as desktop; no
    ffmpeg in `opendreams pack`), or transcode. Starting point on record:
    keep HNM; transcode only if a browser measurably cannot hold 15 fps.
