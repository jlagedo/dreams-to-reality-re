# 006 — Finish Dreams rendering on Windows

> **Note, 2026-09-30.** The hand-written OpenDreams port, ODViewer and the port
> map were removed (tag `opendreams-final`). The renderer now lives in
> `recomp/render/` and the recomp is its only consumer, so requirements here
> about keeping other shared consumers building, the Viewer adapter or port-map
> updates no longer apply. The supporting documents in this directory were
> written before the move and may still mention them.

Status: **Buildable/playable Windows checkpoint; full Windows acceptance remains
open. Software stays the default; direct rendering is explicitly selectable.**
Date: 2026-09-30. Owner direction: close Windows rendering quickly, reproduce
Dreams' visible rendering and game behavior, and accept imperceptible GPU/CPU
numerical differences.

## Deliverable

A dependable **Windows GDIDREAM recomp with direct GPU rendering**, reproducing
the original game's recognizable appearance throughout shipped gameplay:
levels, actors, animation, effects, lighting, shadows, HUD, menus, dialogue,
movies, save thumbnails and the supported Windows development/debug outputs.
It renders at native and high resolutions with Hor+ widescreen, preserves the
game, and needs no recurring scene readback or
software rasterizer.

The frame remains:

**lifted game and posed source geometry -> direct GPU 3D -> GPU 2D -> final
output correction -> present**

006 closes when its Windows acceptance matrix passes and no known rendering
blocker remains in the supported game and development/debug routes. Direct then
becomes the default; software remains an explicitly selected reference tool.
Completion of a single
scene, an isolated oracle or shader generation is insufficient.

This file is the implementation and acceptance authority. Older investigations
and numerical checklists retain their evidence; this scope and fidelity policy
supersede their broader or stricter delivery suggestions.

## Scope and deferred work

| In 006 | Separate later work |
|---|---|
| Windows/D3D11 game and development/debug rendering in the existing recomp | Metal/macOS, Linux/GL and WebGL2 execution and performance validation |
| Rendering required by shipped levels, actors, effects, UI, options and movies | Browser packaging and remaining non-Windows runtime/OS portability |
| Renderer boundary correctness, resource ownership and GPU surface access | Full handwritten native ODRuntime migration and additional Viewer features |
| Windowed/fullscreen/resize, high resolution, widescreen and mouse mapping | Display interpolation, new artwork, new lighting styles and optional enhancements |
| Shadow rendering, thumbnail rendering and their normal game consumers | Voodoo hardware emulation, scan-out replication and exhaustive historical renderer research |
| Windows debug/collector, wireframe/line, diagnostic/flat and obscure supported renderer modes, including existing required shared adapter modes | New developer tools and inspection UI beyond the existing renderer controls |

Keep the shared sokol_gfx/SDL3 core and the existing shader-generation workflow.
Keep existing consumers building when shared interfaces change. Existing
required Windows shared-adapter/debug rendering remains in scope. New platform
or Viewer features are not Windows completion gates.

Windows development/debug rendering remains in 006 by explicit owner choice.
Inventory its controls, collector/wireframe/line paths and obscure supported
modes, preserve their useful output and dependencies, and exercise them even
when normal gameplay does not trigger them. A mode is not omitted merely because
it is absent from the first scene or static geometry corpus. An intentional
Windows no-draw branch is recorded and respected; it does not imply inventing a
new visual feature. A missing Glide branch alone cannot justify dropping a
visible Windows mode. Exact diagnostic numbering/counts can differ when they
truthfully reflect modern visibility/clipping and do not break identification
or controls; document that difference under the same fidelity policy.

Unrelated game/runtime defects belong to their own spec; record them as Windows
release blockers if they prevent the acceptance routes. This rendering spec is
not authorization to rewrite the rest of the game or change its rules.

## Fidelity: preserve the game and visible result

The target is visually faithful Dreams on a modern GPU. Software rendering,
fixed-point projection and modern GPU arithmetic need not produce identical
floating-point values or identical final pixels.

| Contract | Required fidelity |
|---|---|
| Game-consumed state, pointer interpretation, visitation and side-effect timing | Preserve retail behavior; visual tolerance cannot waive a gameplay change |
| ABI, callbacks, lifetimes, surface routing, aliases and source versions | Correct and validated at the actual boundary |
| Packed-pixel operations, palette/key semantics and CPU serialization | Preserve the defined operation and format; final GPU imagery can have accepted visual variation |
| Art, model placement, animation poses, camera/FOV, lighting style, fog, materials, transparency and UI | Recognizably faithful and complete for supported Windows content |
| Visual projection, interpolation, filtering, light/fog arithmetic, clipping edges and shadow contour | Small numerical/raster differences are acceptable when imperceptible in normal viewing and free of functional regressions |

Do not make bit-for-bit 3D images, exact edge pixels, exact floating-point
lighting values or identical shadow-mask pixel counts a universal completion
gate. A tiny rounding difference found only by subtraction, extreme zoom or an
isolated mathematical probe is not automatically a rendering bug. Retain useful
oracles as regression and diagnosis tools; an exact match is evidence, not an
obligation to emulate the software pipeline everywhere.

Visual acceptance uses matched camera, pose, effect state and viewport wherever
possible, side-by-side or toggled stills, and movement at normal viewing size.
Use the original/all-lifted build for content, layout and game behavior, and the
recovered Glide rules for the selected 3D appearance. High-resolution/widescreen
sampling improvements are declared policy, so an upscaled 640x480 image is not
a universal pixel oracle.

Accept small differences in rounding, subpixel positions, edge coverage,
interpolation and quantization if they do not create noticeable wrong placement,
missing geometry, holes, seams, persistent jitter, shimmer, clipping pops, wrong
occlusion, altered lighting/fog style, transparency faults, or unreadable/cropped
UI. Judge scale and temporal visibility, not a universal one-pixel threshold.
An obvious or persistent defect still blocks completion.

For an accepted difference, record a representative normal-view comparison and
the reason it is expected. Group repeated differences with the same established
cause into one variation entry; do not catalog every changed pixel or demand a
new numeric proof for every pose. Stop precision investigation once the source
contract and intended operation are established, the difference is explained,
and normal-view/functional checks show no meaningful defect. Reopen it for a
new visible defect, functional error or contradictory source evidence.
If its cause is uncertain, investigate enough to exclude a wrong source layout,
transform, pointer, ordering or lifetime contract.
Screenshots establish appearance; boundary/oracle evidence establishes game and
memory behavior. Neither substitutes for the other.

## Architecture and invariants

- Keep resource loading/relocation, entity/animation/camera gameplay, collision,
  sound, palette-state updates, RNG, UI decisions and simulation timing lifted.
  Keep the recomp's existing frame cap/timing. Faster rendering is not a change
  to physics, RNG consumption or the game's update clock.
- Capture original model-space vertices/normals/triangles, posed locals, per-corner
  owners, signed UVs, materials, lighting inputs and a separate camera. The
  modern renderer owns visual transformation, visibility, projection, clipping,
  shading, fog, depth and ordered transparency. No old visible lists, generated
  clip lists or retail integer screen coordinates are its input.
- Preserve game-facing transform feedback, especially node `+0x4c` read by sound
  and line-of-sight, with original visitation/timing and skipped-subtree staleness.
  Use the retained transform helper for that contract. Visual pose/camera stay
  separate. Apply the camera once. Encoded source parent `1` resolves to the first
  model node, and the player model root lands at the recorded project spawn.
- Trace callbacks, scratch and node-box consumers at their callers. Preserve
  game and supported debug dependencies and collision separation in
  `GAME_DrawFrame`. Close their actual contracts; additional historical research
  needs a concrete rendering or boundary question.
- Shared rendering accepts host-owned data/resource IDs through the C-compatible
  interface. The recomp adapter owns guest addresses and ABI. Reuse the extracted
  core without a second SDL/sokol instance or the Viewer/ImGui/loader shell.
- Render the scene at drawable/internal resolution. Keep original vertical FOV
  for Hor+ widescreen and retain a 4:3 comparison setting. Center the original
  logical 4:3 UI, keep artwork proportions, and use that canvas for input mapping.
  Preserve movie aspect and apply output correction after 3D and UI composition.
- Keep recovered material/palette sampling, keyed transparency, fog intent,
  `GREATER` depth, deferred order and depth writes. Include texture/palette
  animation and environment-version timing where shipped content uses them.
  Reproduce observable behavior, not the historical Glide call API.
- All normal offscreen 3D uses the same direct renderer. Shadows remain GPU masks
  sampled later with correct palette-index packing; small imperceptible contour
  differences are allowed. Thumbnails are small GPU renders followed by the
  explicit packed export required by their file consumer. Neither presents.
- GPU 2D covers sprites, text, gauges, masked images, fills/bars, fades, captions,
  background capture/restore/dimming and movie placement. Keep integer operation,
  clipping/source-step, keyed-neighbor and metadata semantics. Preserve RGB555's
  high bit in raw copies/fills. Existing packed-pixel oracles remain valuable.
- Register GPU surfaces by range, allocation generation, dimensions, pitch,
  format, aliases and version. Snapshot CPU-mutated sources before reuse/free and
  retain GPU resources until completion. Preserve ordered overlapping operations;
  sample destinations through scratch/ping-pong copies, never an active output
  attachment. Ordinary RAM operations remain lifted.
- Main 3D, UI, saved backgrounds and shadows stay on GPU. There is one outer
  presentation owner. Execute dependent work in order, including menus/movies
  without 3D. No changed-pixel overlay, software Voodoo path or recurring CPU
  gauge/shadow/scene bridge is an implementation stage.

Readback is allowed for explicit CPU consumers such as thumbnail serialization
and requested captures, or oracle comparisons. Record call site, surface,
region, bytes and reason. Uploads/GPU copies are not readbacks. Compatibility
fallbacks are disabled for acceptance: unknown access reports and fails; it
cannot silently download stale surfaces or introduce a recurring CPU renderer.

## Work order: close Windows failures first

1. **Boundary/runtime blockers:** shaded callbacks, valid surface transfers and
   fills, memory ownership, renderer-related save/load faults, and ABI or metadata
   errors reached by supported game routes.
2. **Shipped-content coverage:** inventory the scene/model/movie data and dynamic
   call sites, exercise levels/effects/UI, and implement or fix missing rendering
   behavior demonstrated by those routes.
3. **Windows operation:** finish resize/fullscreen/input, thumbnails, transitions,
   reload/lifetime stability and measured frame pacing; finish the required
   Windows development/debug rendering routes and controls.
4. **Acceptance:** run the matrix below, fix remaining visible/functional defects,
   document accepted tiny differences, then enable direct by default.

Maintain one finite failure/coverage list. Each item has a route or source/caller,
expected behavior, observable consequence, owner and a concrete passing check.
Classify items as **Windows blocker**, **required route not yet exercised**,
**accepted visual variation**, or **deferred work**. Unknown usage needs a focused
caller/content check before speculative implementation.

Prioritize reproducible crashes, missing content, visible defects and unsafe
access. Do not repeatedly expand a numerical oracle after a visual-only
variation has been explained and accepted. Run affected tests during edits,
then the integrated suite per coherent feature checkpoint. Batch related Ghidra
reads and apply port-map updates with the code checkpoint.

R0–R4 remain progress labels, not separate architectures:

| Stage | Windows exit |
|---|---|
| R0 — boundary/ownership | Supported renderer calls preserve game-consumed effects and ABI; surface lifetimes/versions are sound; unclassified GPU-surface access is observable |
| R1 — direct slice | Boot/New Game/first scene/HUD/dialogue work through direct 3D, GPU UI and one present at native and widescreen sizes |
| R2 — required 3D | Shipped and supported diagnostic/obscure geometry/material modes render faithfully; lighting/fog/transparency/shadows, visibility and offscreen consumers work; no known visible or functional defect |
| R3 — regular 2D | Shipped sprite/text/gauge/menu/dialogue/movie/copy/fill routes work with correct layout, integer operations, versions and lifetimes |
| R4 — Windows acceptance | The complete game and development/debug matrix passes with strict auditing, bounded resources and acceptable pacing; remaining nonblocking/deferred items are explicit |

Function port coverage and owner review remain governed by
PORT_MAP.md. Windows delivery acceptance is not
proof that every historical branch of every mapped function is complete, nor
personal owner review. Keep partial/unverified rows honest and apply changed
maps to both Windows programs in Ghidra.

## Windows acceptance and definition of done

Record results on the Windows/D3D11 test machine, with direct rendering and
compatibility fallbacks disabled. Use `--mute`/`--headless` for unattended runs;
use normal visible playback for appearance, fullscreen and input review.

| Gate | Required evidence |
|---|---|
| Game/content coverage | Shipped level/asset and dynamic-route inventory; exercise boot/intro/New Game, level transitions, movement/combat/spells, normal options including real shadows, pause/inventory, changing gauges, dialogue/portraits and movies. No known missing required rendering route or unsupported-operation abort |
| Visible fidelity | Representative matched stills and movement for each distinct used rendering mode/effect; no obvious missing/wrong geometry, camera placement, material/light/fog style, ordering, shadow, UI or movie behavior. Accepted imperceptible differences have evidence and rationale |
| Development/debug rendering | Existing Windows debug controls, diagnostic collector, geometry/flat modes, lines/wireframes and overlays work without unsupported-path aborts or unsafe access. Output remains useful/readable; obscure supported modes have focused route/caller tests and declared visual differences |
| Game-facing correctness | Retained transform/ABI/callback/source-version checks on supported routes; pointer-1/spawn and cross-node checks; no renderer-induced change to gameplay state, sound/visibility feedback, collision separation, update timing or RNG |
| GPU ownership | Audited 3D/UI/background/shadow runs with zero routine scene readbacks, no unclassified surface access, no hidden CPU renderer and correctly ordered explicit exports |
| Windows display/input | Same direct implementation at 640x480 and 1920x1080, plus fullscreen and resizing; correct Hor+ view, centered UI/movie aspect and mouse hit mapping |
| Save/load and lifetimes | Thumbnail render/export and screen restoration; renderer state survives valid save/load and level transitions; ten consecutive reloads without stale resources, violations or unbounded growth |
| Pacing/resources | CPU preparation/upload/submission and GPU pass/copy costs, resource counts and explicit-readback totals recorded. Representative play sustains the existing default cap on the test machine without sustained renderer-induced pacing regression or noticeable rendering stalls |
| Release state | Windows blockers and required route gaps closed, useful regression tests pass, accepted variations/deferred work listed, docs/port map synchronized, and direct selected by default |

A level-load sweep alone does not establish combat, event, transition or movie
coverage. Isolated timings do not establish whole-game pacing. Exact pixel
mismatch counts alone neither fail visual acceptance nor prove it passes.
Non-Windows execution and Voodoo hardware captures are outside this Windows
acceptance gate. Supported Windows development/debug rendering is inside it.

## Current evidence and remaining work

### Session closure checkpoint — 2026-09-30

This checkpoint adds GPU debug lines and bounded collector output, prepared
lighting/UV metadata before frame callbacks, expanded guest/host memory auditing,
VM surface invalidation, exact-size/resize/mouse automation, optional nonblocking
profiling, and the existing shared adapter's grayscale/type-1 modes.
The HNM5 movie bridge retains original conversion in one CPU codec buffer and
uploads only the written rectangle; it performs no scene readback.

Evidence now includes two-dump line/ABI oracles, all five HNM5 conversion modes
through packed GPU comparisons, actual `CONTROLE.UBB` playback followed by
Project11 rendering/autosave, and controlled transitions through the original
loader into Project55 (configured lighting and type-1 geometry) and Project39
(HNS6 transition and animated texture). The asset inventory covers all 289
physical DSN/DAN files; the rebuilt native material corpus covers 150 projects
and 560 placed actors without binding/load-order gaps. These are scoped results,
not completion of the acceptance matrix.

**Known release blocker:** Project39 exposes one-world-unit gaps between authored
architecture faces more visibly than software rasterization. The experimental
source-edge correction is **disabled in live rendering** and only available by
explicit `WDSceneGpuTests --stitch` replay. Its visual review, bounded caching and
isolated pacing validation remain unfinished; do not enable it by default.

**Reload acceptance remains open.** Two full reloads passed. Longer attempts
uncovered caption/menu interleaving and then the existing 200-open diagnostic-log
limit, which hid subsequent save opens. Successful save opens now have an
uncapped `[save]` event, and the harness uses that event. Ten completed reloads
have not yet been established with this corrected observation path.

Next work is to validate/finalize the seam correction, rerun the ten-reload gate,
close remaining dynamic gameplay/debug/obscure-mode routes, and complete visible
fullscreen/input and isolated whole-game pacing acceptance. Keep all affected
function coverage partial and owner-review state honest. Detailed evidence and
commands are in [implementation.md](implementation.md),
[the acceptance runner](acceptance-runner.md), and the finite
[Windows coverage ledger](windows-coverage.tsv).

The live first-scene route, shared renderer/Viewer adapter, GPU UI arithmetic,
thumbnail export, flat and supported Gouraud lighting, ordered environment UV
versions, and the recorded GPU shadow oracle are implemented. Muted/headless
startup is available. These results are scoped evidence, not Windows completion.

The authoritative checkpoint details, remaining route gaps and reproduction
commands are in [implementation.md](implementation.md#stage-status).

Supporting research:

- [000: recomp](../000-the-recomp/spec.md), north star.
- [3D boundary](modern-cut.md), [2D map/contracts](2d-cut.md).
- [Retail layouts and lighting](debug-renderer-contracts.md).
- [Glide appearance rules](../../research/glide-renderer.md),
  [call inventory](../../research/glide-call-inventory.md), [engine](../../research/engine.md).
- [Archived backend investigation](backend-exploration.md).

The recorded shadow now matches all 65,536 oracle bytes, and existing 2D
fixtures have zero packed-pixel mismatches. Keep these fast regression checks.
Neither result creates a new requirement to chase every imperceptible numerical
difference in other poses, effects or GPU implementations.
