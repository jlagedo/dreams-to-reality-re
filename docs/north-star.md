# North star: OpenDreams

What we are building, why, and the decisions already made. This page is the
reference for every design choice in OpenDreams; change it before changing
the code. Research findings stay in the other docs; this one only points at
them.

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

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Language | **C++17**, no exceptions, no RTTI, standard library allowed | Close to the decompiled C; RAII and containers without a runtime; compiles everywhere including Emscripten |
| Platform layer | **SDL3**: window, input, audio, timers, file dialogs | One API on all four targets |
| Rendering | **sokol_gfx** with the platform's native backend (D3D11 on Windows, Metal on macOS, OpenGL on Linux, WebGL2 in the browser) | Small, C, one renderer for every target; SDL3's own GPU API has no browser backend |
| Video | HNM4/5/6 decoded by our own C++ decoder, from the spec in [hnm6-spec.md](hnm6-spec.md) and the decoder in `CRYO.DLL` ([cryolib.md](cryolib.md)) | No dependency on `CRYO.DLL` or on ffmpeg at runtime |
| Game data | **The runtime reads the two disc images** (`.cue`/`.bin`) through its own ISO 9660 reader and **native loaders for the original formats**. No extraction step for players. | Proves the reverse engineering; one code path; point at the original media and play, as OpenLara does |
| Music | **Read straight from the images' audio tracks** (raw 44.1 kHz PCM sectors) | The original plays CD audio through MCI; the image already holds it, so nothing is ripped or transcoded on desktop |
| Web pack | **Parked (desktop first).** When taken up: the only preprocessing; `opendreams pack` reads the images and writes a chunked, versioned pack: the loaders' structs serialized, music and voice as **Vorbis** (stb_vorbis), video in a browser-friendly form | The browser cannot fetch 1.4 GB or decode raw HNM fast enough; made by the same loaders, so there is no second format world |
| Repository | **This repository**, new top-level `opendreams/` directory; the Python toolkit stays as the reference decoder and test oracle; the Babylon.js viewer is frozen and retired once the runtime's web build supersedes it | Cross-checking C++ loaders against the Python decoders is a local test, not a cross-repo chore |
| Fidelity | **Port the original code, fixed step, free renderer** (below) | The only option where the result is recognisably the same game |
| Licence | MIT, like the rest of the repository; no game data in the repository, ever | |

## Port the original code, fixed step, free renderer

The engine is two halves with a hard line between them: the game, ported
from the original code, and the plumbing that lets it run on a modern
machine instead of inside DOSBox.

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

The game runs as a **fixed-step loop**. The original advanced the world by
the elapsed count of a 200 Hz counter, clamped, scaled to 30 animation
frames per second ([animation-timing.md](animation-timing.md)); at high frame
rates that produced tiny deltas and the landing-damage bug. We advance by a
fixed number of counter ticks per step, chosen inside the original's normal
envelope: 30 steps per second is not a whole number of ticks, so the
candidates are 6 ticks (33.3 steps/s) and 7 ticks (28.6 steps/s), or whatever
rate the original's clamps enforce; open until the clamps are traced. The
game never learns the display's refresh rate.
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
transform, projection, clipping, lighting, environment-map UVs) moves to the
GPU renderer.

**Widescreen.** The 3D view is Hor+: the original vertical FOV is kept and
wider screens see more at the sides. Menus, HUD, text and video are laid out
on a 4:3 canvas centred in the window. A "faithful 4:3" toggle pillarboxes
the 3D view. The projection reference test checks the central 4:3 region.

**Quads.** Glide draws triangles, so the faithful look depends on splitting
each quad along the diagonal `GLIDE_DrawObjectFaces` used; that split order is
part of the material specification (to be read from `DREAMSFX.EXE`).

## Architecture

```text
opendreams/
  platform/   SDL3: window, input, audio device, timers, paths
  data/       VFS over the disc images (cue/bin, ISO 9660, audio tracks) or a pack; native loaders: DRD, DSN, DAN, 3DC, SPR/ALP/BF, FSB, INI
  video/      HNM4/5/6 decoder
  sim/        fixed-step game: entities, actors, AI, physics, camera, level, inventory, menus, dialogue, saves
  render/     sokol_gfx: object hook, materials, sprites/text, video blit, post
  audio/      mixer: SFX (FSB), voice (DRD), music (tracks)
  app/        main loop, asset browser, game
  tools/      pack writer, trace/compare tools
```

Rules:

- **The game never touches SDL or sokol.** `sim/` is its own build target
  that cannot include `platform/` or `render/`; it is a library with a
  `step(inputs)` function and readable state, and the app owns the loop.
- **Inputs are logical actions per player, resolved once per step** from
  bindings held as data (keys, gamepad). The original polls a key-state table
  each tick (`INPUT_PollKeyboard`); the platform layer fills that table from
  the bindings. Replays record the table per step.
- **The original random generator, called only by ported code.** The game
  calls Watcom's `rand_` from 21 functions and never seeds it, so the sequence
  is fixed from launch; the port keeps that generator and nothing outside the
  game (renderer, audio, UI effects) may draw from it.
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
- **Settings are a versioned struct** persisted by `platform/` (user
  directory on desktop, IndexedDB in the browser).
- **Video is clocked by its audio.** The HNM decoder is also a mixer source;
  the frame shown is the one for the current audio sample position. Frames are
  held, not blended, on fast displays.
- **Debug time controls come with the fixed step**: pause, single-step, and
  rewind by replaying inputs from a snapshot, in `app/` from milestone 4.

## Data

**Input: the disc images, nothing else.** The player points the runtime at
the two `.cue`/`.bin` images. The VFS mounts both at once: the data tracks
through an ISO 9660 reader, with the precedence between discs taken from the
merge map in [disc-layout.md](disc-layout.md), and the audio tracks as raw
PCM (11 + 13 tracks). There is no install step, no extracted folder and no
"insert disc 2". Every format on the discs that the game needs is decoded
([assets.md](assets.md), [file-formats.md](file-formats.md)), except the gap
listed below. A plain folder can also be mounted, for development only; it
is not a supported way to play.

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
keeps serving the Babylon viewer until the runtime's browser build replaces
it. New format knowledge lands in the Python decoders first (they are the
oracle) and in the C++ loaders second.

## Milestones

Each milestone ends with something that runs on Windows, macOS and Linux
and still compiles for the browser.

1. **Skeleton.** CMake project, SDL3 window, sokol triangle, CI for
   Windows/macOS/Linux plus an Emscripten compile check. Nothing from the
   game yet.
2. **Asset browser.** The owner's proposed first deliverable. Mount the two
   disc images (cue/bin, ISO 9660, audio tracks) and browse them: models with
   skeletons and animation playback at the original 30 frames per second,
   props, level geometry and textures, sprites and fonts, sounds, music
   tracks, voice with captions, videos. Every loader lands here, checked
   against the Python decoders. This is where the renderer's material modes
   are matched against the 3dfx build.
3. **Level viewer with the real camera.** Levels with materials for all 98
   scenes (needs the DSN gap below), the original camera (`CAM_CompCameraPos`),
   the projection reference test.
4. **First playable level.** Player movement from the original tables,
   collision, physics, level exits, the HUD. Fixed-step loop with
   interpolated rendering.
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

Ordered by how early a milestone hits it.

1. **`.DSN` faces and materials for multi-arena scenes**: 90 of 95 scenes
   decode geometry but not faces, materials or UVs
   ([scene-geometry.md](scene-geometry.md)). Blocks milestone 3. The answer
   is in the loader code of `WINDREAM.EXE` (`DSN_*` in the registry), not in
   more heuristics.
2. **Collision and physics**: only `PHYS_TickEntity` and
   `PHYS_IntegrateMotion` are named; the collision model (tag 2 of the scene,
   entity radii, floor finding) is not documented. Blocks milestone 4.
3. **The fixed-step size**: the delta clamps and the landing-damage
   dependence on delta, so the step (6 or 7 ticks of the 200 Hz counter, or
   the clamped rate) is chosen inside the original's envelope.
   Milestone 4.
4. **Gameplay state machines**: `0x421717` (5 KB, requests animation states
   and posts messages), the entity controller under `ENT_TickEntity`,
   inventory and spell effects, triggers, damage. Milestone 5.
5. **Lighting inputs**: what the original fed into per-vertex colours and the
   `.3DM` shading tables ([assets.md](assets.md)). Milestone 2 for the look,
   not blocking.
6. **Save-game format** and the remaining `DREAMS.INI` semantics
   ([game-content.md](game-content.md)). Milestone 5.

About 43% of the game's own code is named today. The rasterizer (the largest
unnamed block) never needs to be.

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
- A general-purpose engine or editor. The asset browser exists to exercise
  the loaders and renderer, not as a product.
- Shipping game data. The player supplies the discs.

## Open decisions

- **Fixed step size**: 6 or 7 ticks of the 200 Hz counter, or the rate the
  original's clamps enforce. Settled by the RE item above, before milestone 4.
- **Parked until the web build (milestone 7), desktop first:**
  - **Pack format**: container, compression, chunk boundaries. Starting point
    on record: one chunk per original file as a relocatable image plus a
    manifest mapping projects to chunks; no format-level compression (the
    host's brotli/gzip does it); audio to Vorbis is the only transcoding.
  - **Video in the web pack**: keep HNM and decode it in WebAssembly (the
    codec targets a 1997 Pentium; same decoder and player as desktop; no
    ffmpeg in `opendreams pack`), or transcode. Starting point on record:
    keep HNM; transcode only if a browser measurably cannot hold 15 fps.
