# 006 implementation record

Date: 2026-09-30. Authority: [spec 006](spec.md).

**Current delivery scope: Windows/D3D11 game and development/debug rendering.**
The owner accepts imperceptible CPU/GPU numerical and raster differences under
[the spec's fidelity policy](spec.md#fidelity-preserve-the-game-and-visible-result).
Historical exact-match numbers below are evidence and regression diagnostics,
not universal image-parity gates. Game-consumed state, ABI, ordering, lifetimes,
formats and unsafe access retain their correctness requirements. Other platform
execution/validation belongs to later work and cannot keep Windows 006 open.
Existing Windows development/debug rendering stays in 006; new tools and the
full native game migration remain separate.

Latest follow-up: [demo-informed contracts](debug-renderer-contracts.md) confirms
the hierarchy cut, corrects retail UV field names, and adds type-2 flat lighting.
Mixed-light retail oracles and a controlled live rotation/unbind run pass;
Lit 0x16/0x17 Gouraud now uses retail-checked corner kernels; 0x18 retains flat
lighting and stored corner brightness. Environment UV versions now preserve
opaque/deferred draw timing; mirror setup, other face modes and
the wider acceptance gates remain open.

This is an implementation in progress, not completed R0–R4 delivery. Software
remains the default reference. `run.py --renderer direct` now runs the live game
through the shared GPU scene/UI/presentation pipeline for the tested first-scene
route. Unsupported operations fail explicitly; they do not select software or
silently download the scene.

## Windows closure checkpoint (2026-09-30)

**Session closed at the owner's request with software still the default.** The
new boundary/movie/debug paths are retained; full spec acceptance is not claimed.
Source-edge stitching is an experimental replay option (`WDSceneGpuTests
--stitch`), disabled by default in SceneDraw and live rendering. The prototype
has CPU tests and preliminary replay evidence, but its visual review and
per-model cache/performance work were not completed. Its measured 17–19 ms
uncached preparation samples were contended and are not an acceptance result.

The Project39 defect is grounded in original source geometry: face1137/owner38
and face1900/owner57 contain matching edge endpoints at world Y=-102 and Y=-101,
with X=1117 and Z endpoints -915/-1158. Identity rotations/integer translations
exclude transform drift for this pair. Geometry-only GPU replay exposes the
gap, so it is not a black texture sample. Normal-view software/reference and
replays are under `run-software-projects-55-39-reference` and the corresponding
direct run. Do not replace the established black clear colour to conceal gaps.

The ten-reload harness is not yet accepted. Its earlier four-read ceiling was
caused by the host's general 200-open log budget, not proof that the fifth load
failed. Successful game save opens now emit an uncapped `[save]` event exactly
once. The harness parses only that event and retains real caption/controller
recovery. Rerun the ten-count check on the new executable before claiming it.

Closure validation: normal and `--render-audit` recomp builds, shared renderer
validation, ODViewer/ODRuntime and ODModelAdapterTests builds pass. The Python
suite reports **207 passed, 2 skipped, 9 deselected**; the separate codegen/host
suite reports **41 passed**. The shared GPU suite retains all 256 packed-pixel
checkpoints, and both line and HNM5 CPU/ABI/GPU oracles pass. Changed Python files
pass Ruff. Repository-wide Ruff still reports the pre-existing import-spacing
issue in `opendreams/tools/make_app_icons.py`, which this checkpoint does not edit.

Final `run-closure-software-006` boots/New Games and captures the original scene
through software rendering. `run-closure-direct-006` reaches the first scene and
dialogue, toggles the collector, draws over 2,000 collision lines, restores normal
rendering and continues with zero reported routine readbacks. Both smoke runs
are deliberately time-bounded; neither is whole-game acceptance. Final build and
validation logs use the `DREAMS_OUT/recomp/closure-*` prefix. The map checker
passes; changed maps and partial/review-pending tags are applied and inspected in
both Windows programs and DREAMSFX.

Windows acceptance remains **open**, and direct remains opt-in. The finite
[Windows coverage ledger](windows-coverage.tsv) tracks blockers, required routes,
provisional visual differences and deferrals. The
[acceptance runner](acceptance-runner.md) inventories configured original assets
and records reached checkpoints without treating timeout, controlled state or
an isolated oracle as completed gameplay acceptance.

The integrated first-scene checks at **640x480**, **1920x1080**, and
640x480 → 1920x1080 → 640x480 reached their declared checkpoints. Their initial
report is `DREAMS_OUT/recomp/acceptance/20260930T053202Z-da24dedb/results.json`.
`run.py` now supports exact `--width/--height`, timed `--resize` and `--mouse`
events. Logs record actual client/drawable sizes and logical mouse coordinates.
Scripted `KP1`–`KP5` invoke the same host debug controls as physical keypad keys;
scripted `F11` toggles fullscreen in visible runs. Headless remains hidden.
Native parsing rejects malformed/overflowing schedules instead of wrapping them.

The GPU line boundary retains lifted `Clip_Line_`, including its unusual edge
rules, and the original line function's stack argument side effects. Ordinary
RAM destinations still execute the original function. Registered surfaces use
GPU integer Bresenham coverage with raw packed colour, RGB555 high-bit retention
and the centered logical canvas. The line pass loads its target, discards
non-line fragments, and restricts work to a scissor; it never samples an active
attachment or downloads pixels. Two independent retail dumps supply **1,128**
original-x86 fixtures; the production-hook harness checks **2,256** owned/RAM
executions, stack cleanup, callee-saved registers, FPU state and end-y writes.
The GPU fixtures match. Evidence is under `DREAMS_OUT/recomp/render-line-smoke`.

The collector uses source geometry, per-corner owners and modern homogeneous
clipping. Its Windows branch deliberately performs **no clear or scene draw**,
preserving the previous image for collision lines. The compatibility arrays
receive at most 682 complete triangles (2,046 indices), within the actual
2,048-index capacity. Counters describe stored records; logs also report the
uncapped modern triangle count. This avoids retail's unbounded overwrite.
Modern clipping, counts and capacity are declared diagnostic differences, still
subject to the wider useful-output acceptance gate. Fresh Ghidra references
found no external consumers of these counters. The strict-audit
`run-debug-006` route toggles keypad 3, draws over 8,000 collision lines using
Backspace, then restores ordinary rendering; zero routine readbacks are reported.
`snap_00360_025364ms.png` shows the accumulated collision wireframe at native size.

Callback frames now capture immutable source inputs and prepare/publish lighting
and environment metadata **before** the lifted callback. GPU scene submission
occurs afterward, at the original flush position. A supplied preparation is
validated and consumed without recomputation. The production frame-hook harness
checks main/alternate, callback/no-callback and collector branches against the
recorded frame control flow; its callees are mocked, so this is orchestration
evidence. The controlled `render_light_live_smoke.py --oriented --gouraud
--callback` run installs the existing editor callback at `0x44d46d`, changes
144 corner shades, observes normal-dot updates after rotation, unbinds, and
continues with zero routine readbacks. Arbitrary callback mutations, shaded
shadow callbacks and complete metadata closure remain unverified.

Auditing now covers implicit stack operations (including optimized and host
PUSH/POP helpers), actually executed CMPS/SCAS iterations and production host
bridge transfers. Short file reads and conversions audit their actual output,
not unused destination capacity. CPU tests exercise zero counts, early exits,
backward traversal, POP stack addressing and real host transfers. Canonical
relifting activates the new generated probes. Deliberate crash/research readers
are outside the normal game transfer contract. Instrumented live-route success
still does not establish the entire acceptance matrix.

Successful VM decommit/release invalidates every overlapping surface registry
entry without performing GPU work on the freeing thread. Renderer-thread lookup
retires invalid GPU targets through the existing deferred release mechanism;
reused addresses receive new IDs. Failed frees preserve registry state.
Tests include real Windows decommit/recommit, partial pages, neighbours, stale
IDs and host failure. Ten full recomp reloads remain a separate acceptance check.

The existing ModelPreview adapter now retains source-block identity and original
corner shade bytes, supports unlit 0x16–0x18, and draws type-1 flat diagnostics
with per-block colour reset and modern visibility. This adds no Viewer control
or platform. Required active-light input and remaining obscure modes stay open.

`render_content_inventory.py` follows actual source block chains across all
289 physical DSN/DAN files (191 DAN, 98 DSN), with zero parsing failures. Static
types are `-7,-6,-5,-4,-3,1,2,3,9`, all implemented by SceneDraw; no source node
light binding or environment flag was observed. Unlike the earlier scan, this
includes 1,873 stored type-1 flat faces. Stored geometry is not automatically
visible: retail hides actor collision proxies such as ZZZZZ/BASSIN01 at runtime.
The O01EAU01 tunnel block (projects 55/138) is a nonproxy natural candidate.
The rebuilt native material corpus passes all 150 projects and 560 placed
actors with zero load-order, textured-binding or flat-binding gaps. Dynamic
setters/effects and gameplay triggers remain required evidence. Reports and
source hashes live under `DREAMS_OUT/recomp/render-content-inventory`.

Optional `WD_RENDER_PROFILE=1` records bounded rolling CPU/frame percentiles and
asynchronous D3D11 timestamp results for scene, upload, copy, UI and output work;
source preparation is CPU-only. Query polling never flushes or waits. Reports
include skipped/pending/disjoint/failed samples. Profiling does not alter the
25 FPS cap. Whole-game performance acceptance is still pending. Explicit output
captures now log caller, surface, region and byte count separately from thumbnail
exports and routine reads.

The shared validator retains the 256-checkpoint, 14,769,600-pixel packed corpus
and existing light/environment/shadow regressions. New and materially changed
port-map rows remain `partial`, `reviewed=no`; map validation and Ghidra tags
must accompany the checkpoint in both Windows programs (and the changed shared
Glide adapter row in DREAMSFX).

## Live integration checkpoint

Unattended runs now support `run.py --mute` (`WD_MUTE=1`) and `--headless`
(`WD_HEADLESS=1`, implies mute/focus/quiet). Muting selects the existing timer
mixer without opening an audio device, retaining DirectSound buffers/cursors
and CD completion state. Headless keeps the SDL window hidden. The controlled
environment smoke verifies native Win32 window visibility, mixer selection,
continued scene submission and zero routine readbacks. DirectSound initialization
still succeeds and the original cursor/completion polling continues.

The 45-second `run-direct-caption` run reached boot/New Game, a textured and
animated first scene, HUD/gauge, the initial voiced dialogue and scripted movement.
Its captures include `snap_00163_010033ms.png` and `snap_00852_040445ms.png` under
`out/recomp/windream/run-direct-caption`. This is a working first-scene slice,
not proof of the complete acceptance matrix below.

```powershell
uv run python recomp/windream/run.py --renderer direct
# Isolated unattended evidence run; default simulation cap remains 25 FPS.
uv run python recomp/windream/run.py --renderer direct --tag direct-check `
  --seconds 45 --keys 2000:ESC,5000:RETURN,8000:ESC,20000:ESC,28000:UP `
  --snap-ms 10000
```

Implemented in this slice:

- One SDL/sokol presentation owner; GPU scene colour, integer UI composition and
  final gamma. GDI snapshot requests capture the corrected GPU output explicitly.
- Original hierarchy traversal, transform-only feedback composition, callbacks
  and node-box maintenance remain lifted. The software post-order raster hook
  and span flush are bypassed in direct mode. Unknown diagnostic/offscreen paths
  remain explicit failures rather than hidden compatibility rendering.
- Immutable material page/palette snapshots, 128-square cache-material sampling,
  colour-key semantics, clamp/repeat and deferred alpha 128/255 with depth writes.
  Windows palette generation remains lifted; logical shade r selects its physical
  row 31-r. Palette binding retains the selected snapshot until the page changes.
  Supported radial and type-2 flat lights use shared kernels and metadata feedback;
  Lit 0x16/0x17 corners use the original normal-pool refresh and byte accumulation;
  0x18 takes retail's flat branch. Other face modes still fail explicitly.
- Native sprite/faded-text/gauge request normalization and exact-width shared
  scratch stores. Original fire generation and RNG continue on the guest side.
  The native UI test covers 75 pixel checkpoints (534,528 pixels) and compares
  shared scratch against original x86; the captured gauge is now GPU-tested.
- GPU-aware lifted REP copies/fills classify registered surfaces while ordinary
  RAM operations retain the original lifted implementation. Saved backgrounds
  and the proven dialogue capture at return address `0x436e84` remain GPU images.
  The dialogue temporary is retired with the original controller's lifetime.
- Mouse mapping uses the same centred logical canvas. Basic drawable-size changes
  recreate/resample main GPU targets; resize acceptance is still pending.

Scene capture initially cost about 1,225 ms per frame because each tiny read
called VirtualQuery. Permission ranges are now cached only inside one synchronous
read scope, never across guest execution/allocations. Sampled capture costs fell
to about 9–18 ms. Immutable material-content reuse and adjacent, ordered batching
reduced sampled steady scene submission to roughly 3–10 ms (about 67–90 batches
instead of thousands of individual triangle submissions). These are sampled
CPU timings, not the final R4 performance budget or cross-backend measurements.

Dialogue integration exposed a lifter ABI defect: default-size segment PUSH/POP
must reserve four stack bytes despite a 16-bit selector. The Windows lifter now
distinguishes that from the `0x66` two-byte form. Native Watcom versus recomp
`t_segments` passes all four cases, including the return-address marker.

The strict memory-audit run is a separate acceptance check. Its initial failure
found the CRT's unrolled memset helper touching GPU-owned framebuffer memory
outside REP STOS. GPU destinations now use a native memset boundary; ordinary
RAM retains the lifted function. The ordinary run's architecture/counters alone
do not prove complete scalar-access coverage.
After that fix, `run-direct-audit2` ran for 45 seconds through the same first-scene,
dialogue and movement route without a reported access violation. This proves only
the instrumented accesses on that route: implicit stack/host-transfer auditing,
other paths and the full acceptance corpus remain open.
R0–R4 remain open for the listed broader contracts, shadow coverage,
remaining exports, UI/material modes and platform execution.

## Implemented components

### Unlit Gouraud input and GPU interpolation checkpoint

The demo-identified face bytes `+0x41..+0x43` are now captured as independent
corner shades and recorded in WDS7 (older captures remain readable unless they
contain Gouraud faces whose corner bytes were omitted). The shared
scene shader interpolates each byte shifted left three for unlit Glide types
`0x16`..`0x18`, with clamp sampling. A controlled GPU test checks unequal corner
brightness after WDS7 round-trip. This initial checkpoint rejected lit faces;
the following WDS8 checkpoint implements the supported retail lighting branches
against vertex-normal and ordered metadata oracles. No static scene block
exercises this path. The initial checkpoint closes only the unlit input and
interpolation slice of the R2 material gate.
The 30-second `run-direct-gouraud-regression` strict-audit route reached 250
scene submissions with zero routine readbacks and no reported surface-access
violation; it exercised existing first-scene materials, not a Gouraud asset.

### Lit Gouraud and normal-pool checkpoint

Fresh Ghidra inspection of `REND_LightObject` (`0x47b7e0`) confirms that
`0x16`/`0x17` clear visible corner bytes, then visit bound lights in order,
refresh every owner vertex-normal dot at node `+0x8c/+0x90`, and accumulate
positive corner contributions with **byte wrapping**. Corner normal pointers
are face `+0x0c/+0x18/+0x24`; shared pointers retain identity. A corner outside
the refreshed pool consumes its retained dot, rather than inventing an owner.
`0x18` belongs to the flat branch: it updates `+0x40` and leaves the three
corner brightness bytes intact. The former blanket lit-Gouraud rejection is
removed for these supported modes.

WDS8 captures complete owner normal pools, corner pointers/XYZ and retained
dot fields. Older snapshots with lit `0x16`/`0x17` are rejected because they
omit that input. Source normal pools and a controlled Gouraud block are checked
against both independent retail dumps; camera, pointer-1/spawn and 1,169
cross-node triangles retain their existing checks.

`render_gouraud_light_smoke.py` passes 3,165 original-x86 comparisons (1,055
each for `0x16`, `0x17`, `0x18`) with zero shade or normal-dot mismatches,
including mixed radial/oriented lights, range edges, integer and byte overflow.
Another 128 adapter cases compare final metadata and ordered writes with
retail for shared/external normals, an unreferenced pool entry, zero/eight
lights, all culled-face combinations and flat/Gouraud block order. Adjacent
retail stores of a flat negative sum and its negated byte are compared as the
final byte; the adapter publishes it once. No consumer runs between those stores.

Production D3D11 tests pass WDS8 identity/scratch round trips, lit corner
interpolation and equality with a packet containing the oracle's baked corner
shades. `0x18` preserves its stored corner bytes. Missing lit corner normals
still fail explicitly. Reports live under `DREAMS_OUT/recomp/gouraud-lighting`
and `direct-render`; no retail data is committed.

This does not close natural Gouraud asset/effect coverage, other Gouraud or
specular face modes, lit callbacks, near-clip temporary metadata, full ABI or
fixed-point camera-chain equivalence.

The controlled `--oriented --gouraud` strict-audit child changes a 688-face
node's source blocks to `0x16`, binds a light aimed opposite a captured owner
normal, rotates/moves it and unbinds. It changes 144 corner shade bytes and
updates the pool dot after rotation, with zero routine readbacks and no reported
instrumented surface violation. This injects source state; it is not evidence
of a naturally occurring Gouraud asset. Its report is
`DREAMS_OUT/recomp/windream/run-direct-oriented-gouraud-light/results.json`.

```powershell
uv run python recomp/windream/debug/direct_render_validate.py --gpu
uv run --with unicorn python recomp/windream/debug/render_gouraud_light_smoke.py
uv run --with unicorn python recomp/windream/debug/render_light_live_smoke.py --oriented --gouraud
```

### Environment mapping and UV-version checkpoint

Fresh retail `REND_ComputeEnvMapUVs` (`0x47e094`, 567 identical bytes in both
Windows builds) and its caller confirm the post-draw update. Corner normals
are multiplied by the **parent's** camera-space Q15 rotation. Corner 0 retains
integer normal precision; corners 1/2 spill to float. Each truncated component
is shifted left eight with 32-bit wrapping and offset by `0x800000`.

WDS9 adds original integer local rotations, visual eligibility distinct from
hook submission, and exact UV pointer/value identity. The adapter recomposes
the integer camera chain solely for compatibility UV feedback. Modern geometry
still uses its independent float poses and camera; no old composed transform or
projected vertex field is read. Both original dumps pass poisoned-transform
capture checks and original hierarchy replay comparisons for all 229/224 visited
node rotations. Pointer-1/spawn and cross-node checks remain unchanged.

Opaque faces sample their source UV version at the object's draw, then visible
environment faces update shared UV pairs in original corner order. Hook-disabled
objects can update UVs; culled faces retain them. Later objects observe earlier
updates. Deferred Glide blocks hold pointers, so their draw samples the final
version after all object updates. Subsequent/offscreen passes begin with the
retained guest version. Source shading/UV metadata is returned in owner order.

Validation passes 3,309 original-x86 corner UV comparisons, 112 type/cull cases
and 1,155 original integer matrix multiplications with zero mismatches. GPU
tests cover WDS9 round-trip, shared pointers, opaque/deferred versions,
hook-disabled and culled objects, subsequent passes and a dedicated 64x64
offscreen target; rendered output equals explicitly baked source versions.

The muted, hidden strict-audit child enables environment mapping on a 688-face
node, changes a visible source normal, then disables it. It changes 144 of 2,064
UV pairs, changes those 144 again after a quarter-turn, and reports zero routine
readbacks. A Win32 window enumeration confirms that its windows remain hidden.
This injects source state rather than proving a natural environment asset route.

Per-owner indexing and UV/normal version tables restricted to active effects
avoid work for ordinary unlit scenes. Sampled live submission returns to roughly
4–10 ms after the change; this is not the full R4 performance budget.

Remaining contracts include natural environment assets, mirror setup, shaded
frame-callback sequencing, clip-generated metadata and broader allocation/alias
closure. Shaded callbacks still fail explicitly pending their timing proof.

```powershell
uv run python recomp/windream/debug/direct_render_validate.py --gpu
uv run --with unicorn python recomp/windream/debug/render_environment_smoke.py
uv run --with unicorn python recomp/windream/debug/render_environment_live_smoke.py
uv run python recomp/windream/run.py --headless --renderer direct --render-audit --seconds 30 --keys 2000:ESC,5000:RETURN,8000:ESC,20000:ESC
```

Reports and read-only Ghidra evidence remain under
`DREAMS_OUT/recomp/{environment-mapping,windream/run-direct-environment,gouraud-lighting}`.

### GPU real-shadow checkpoint

The alternate frame now recognizes the `OMBRE2` destination at `0x62b9a8` and
registers a GPU-owned P8 surface. It renders the temporarily reparented actor
through the shared renderer into a 128x256 index-0/1 mask. A GPU resolve samples
the even rows and maps the palette into the 128-square material LOD; horizontal
P8 pairs are accounted for without downloading the mask. Main-scene material
capture resolves this GPU resource instead of reading stale guest pixels.
Resolved versions are retained through submission and retired with their frame;
level reset retires the shadow surface. Shadow completion never presents.

The original actor reparenting, material substitution and viewport restoration
remain lifted. Material type `0x1b` temporarily retains its original 68-byte face
records, which the source adapter now accepts explicitly.

`ODDirectGpuTests` verifies GPU mask production, exact paired-P8/LOD sampling,
palette colour-key equality and immutable resolved versions. Live strict-audit
runs with `--poke 0x4a3168=1` reached the first scene and dialogue at the default
window size and fullscreen (3840x2160 on this machine), without an instrumented
CPU framebuffer access violation. These are route-specific results.

**Recorded silhouette mismatch resolved (2026-09-30):** the dedicated shared
mask path accepts original integer vertices, Q15 local rotations/translations,
source normals/planes and camera parameters. It derives poses and projection
inside the renderer. Camera inverse translation uses wrapped Q15 products and
arithmetic shifts; child translations retain retail truncation. Vertex owners
remain distinct from face owners for cross-node triangles. Stored-plane and
dynamic-normal culling precede projection.

`BT_Flat_` builds 12-bit edge steps, initializes each edge half a row behind its
start, then floors span endpoints after each advance. The new GPU fragment pass
implements that coverage using per-triangle edge constants and bounds. CPU work
prepares geometry and constants, never pixels or per-row span lists. The GPU
still produces and samples the paired index-0/1 mask. Normal 3D keeps its modern
float path. WDSA adds exact source translations, face-normal pool identity and
retained normal dots; older shadow snapshots lacking these inputs are rejected.

The strict original-x86 comparison now matches all **65,536 bytes**: CPU and GPU
both contain 4,320 set P8 bytes, with zero differences and intersection-over-union
1.0. The production projection helper independently matches all 25 recorded
node poses and 265 referenced camera/projected vertices. These are source-derived
calculations; neither old projected fields nor visible lists are renderer input.
A 45-second muted headless strict-audit run, including scripted movement/control,
records 315 shadow submissions/resolves by its 500-frame checkpoint, with zero
routine readbacks and no reported instrumented surface violation.

Coverage remains partial beyond this fixture: exercise additional Windows game
and debug shadow/clipping routes and close their game-consumed metadata/callback
contracts. The renderer-owned float near intersection has not been compared
against every retail temporary-face branch. Under the current fidelity policy,
that is not itself a demand for exhaustive numeric parity: visible missing/wrong
geometry or mask behavior and functional errors block; imperceptible contour
differences can be accepted with comparison evidence. Other backend execution
is later work. This fixture alone does not establish the full Windows R2 gate.

```powershell
uv run --with unicorn python recomp/windream/debug/render_shadow_smoke.py --require-parity
uv run python recomp/windream/run.py --renderer direct --render-audit `
  --headless --tag shadow-quantized --seconds 45 --poke 0x4a3168=1 `
  --keys 2000:ESC,5000:RETURN,8000:ESC,20000:ESC --snap-ms 10000
```

The shadow oracle uses the existing captured state identified by
`out/recomp/render-smoke/shadow-contract.json`. Its generated packets, masks and
metrics are under `DREAMS_OUT/recomp/shadow-adapter`. Its `.wds` captures source
geometry/state; explicit CPU export of a live GPU shadow image remains separate
unimplemented work.

### Explicit thumbnail export checkpoint

`GAME_SaveThumbnail` remains lifted. Its call to `REND_DrawFrame`, returning at
`0x40fe13`, now renders the original scene into a dedicated 64x64 GPU target at
guest `0x5d6b98`. This is not a main-window presentation. An explicit D3D11
staging-region export commits prior work, maps the result respecting row pitch,
packs RGB565/RGB555 before output gamma, and restores CPU ownership. The original
8,192-byte copy to `0x5d8b98`, file write and viewport restoration then execute.
Export counts, bytes and barrier time are logged separately from routine reads.
GL/Metal image export currently reports unsupported; these backends are unvalidated.

`ODDirectExportTests` exercises the production backend API with a 67-pixel source
and a 17x5 subregion, checks orientation and exact bytes, rejects invalid handles
and ranges without mutating the output, and verifies all 131,072 packed-format
values after GPU drawing and export. The native masked-menu-image adapter at
`0x427b8c` also passes the original-x86/GPU pixel suite (75 checkpoints, 534,528
pixels including other UI operations); full ABI and edge footprints remain open.

```powershell
uv run python recomp/windream/build.py --render-audit
uv run python recomp/windream/debug/render_thumbnail_smoke.py
# Also load the just-created save, then resume the first scene.
uv run python recomp/windream/debug/render_thumbnail_smoke.py --load
uv run python recomp/windream/debug/direct_render_validate.py --gpu
uv run --with unicorn python recomp/windream/debug/render_ui_smoke.py
```

The live thumbnail smoke deliberately changes the child process's project
autosave policy from 2 to 0 and rearms `GAME_StartLevel`, then restores the policy.
This is controlled-state integration evidence, not an unmodified autosave route.
Retail saving produces an 8,192-byte `game6.ico` that equals both guest buffers;
screen dimensions return to 640x480 and scene submission continues. The initial
measured export barrier was 0.420 ms (one sample, not a performance percentile).
Reports, logs, files and screenshots are under
`DREAMS_OUT/recomp/windream/run-direct-thumbnail-smoke`.
The generated icon is subsequently read by the lifted load-menu controller and
drawn through the GPU masked-image handler; the circular thumbnail is visible in
the 40-second capture. This display check supplements the pixel oracle.

The 60-second `--load` run reads the generated `game6.dat`, reloads the first
scene, and continues to 550 scene submissions with 41 live renderer resources.
It reports one thumbnail export (8,192 bytes) and zero routine readbacks, with no
reported instrumented surface-access violation. This proves one controlled
save/menu/reload route, not the complete save-state semantics or ten-reload gate.

The system-menu code skips the Save choice; actual thumbnail generation belongs
to level-start autosaving. A separate attempt to load a pre-existing installation
save (`run-direct-thumbnail-check`) faulted in lifted `GAME_LoadGame` at the copy
returning to `0x40fac8`, with a null source. That failure does not establish a
renderer error or compatible save/load support; full save/load acceptance remains
open and must use a newly generated save as well as valid external saves.

### Remaining sprite branches checkpoint

The live native adapter now supports flag 4 and signed-high coverage. Flag 4's
fixed clipping bounds, raw palette stores, unusual row stride and mode priority
come from the original instructions. Signed-high coverage samples an immutable
28,672-byte blend-table window on the GPU, including neighboring-memory values
and the table address that aliases the current coverage byte. Neither operation
downloads destination pixels. The original divided-alpha loop transition is
retained: only a signed-positive opaque quotient enters the direct-alpha loop.

`render_ui_smoke.py` compares the production native adapter and shared shaders
against original x86: **105 checkpoints, 5,055,744 pixels, zero mismatches**.
The suite covers both packed formats, all 256 coverage bytes, adjacent-memory
mutation, the scratch alias, signed divided-loop transitions, clipping/scaling,
overlap, target changes, fixed flag-4 clip limits, and nonstandard row pitch.
It checks the existing sprite/gauge shared-scratch ranges as well; full register,
flags and all other helper-side-effect closure remain unproven. Malformed
descriptors and stores outside a registered allocation remain explicit failures.

```powershell
uv run python recomp/windream/debug/direct_render_validate.py --gpu
uv run --with unicorn python recomp/windream/debug/render_ui_smoke.py
```

The new command kind `OD_DRAW_LOOKUP` takes a generation-checked immutable R32UI
table texture. Queued source/table handles retire after the frame; changing guest
memory cannot alter a submitted draw. The table snapshot is a CPU source upload,
not a framebuffer readback. These rare branches are established with controlled
inputs; the ordinary live first-scene route is a regression check, not evidence
that every retail caller has exercised them.
The subsequent 40-second `run-direct-sprite-branches` strict-audit run reached
the first scene, dialogue and movement without a reported instrumented access
violation; it recorded 450 scene submissions and zero routine readbacks. The
existing 256-checkpoint/14,769,600-pixel compositor corpus also still passes.

### Type-1 diagnostic 3D checkpoint

`SceneDraw::submit` now handles Glide type 1 with opaque constant colours. The
sequence starts at zero for each block and advances by `0x24bf9` per submitted
source triangle; the block's packed colour word is not used. It requires no
texture or palette binding, and node light count does not affect this mode.

The direct path derives geometric visibility from original corners, floating
point composed poses and the selected camera, using homogeneous six-plane
clipping and CCW winding. It never consults old projected coordinates, visible
lists or computed cull bits. The GPU still clips and rasterizes the original
triangle; the CPU visibility result only controls submission/diagnostic numbering.
This is not an occlusion or subpixel sample-coverage test.

`render_scene_mode_smoke.py` executes the original `DREAMSFX` hook bytes exported
read-only from Ghidra. Only its three external Glide calls are intercepted.
Two controlled blocks with one skipped face each produce colour calls
`0, 0x24bf9, 0, 0x24bf9`, four triangle submissions, the expected constant-colour
combiner and a balanced stack. The generated report and code hash are under
`DREAMS_OUT/recomp/scene-modes`.

`WDSceneModeTests` exercises the production adapter and GPU: exact constant
colours, block reset, back-facing/outside/far rejection, lighting-independent
diagnostics, and geometry becoming visible only in the Hor+ frustum. Math tests
also cover near/side-plane crossings and invalid inputs.

```powershell
uv run --with unicorn python recomp/windream/debug/render_scene_mode_smoke.py
uv run python recomp/windream/debug/direct_render_validate.py --gpu
```

**Still partial:** retail near-plane clipping can expand its temporary face list;
the direct path numbers surviving original triangles, so diagnostic colour
numbering across such splits is not claimed equal. Exact stored-plane threshold
and dynamic-normal culling comparisons remain open. No static type-1 block was
found in the checked corpus; controlled tests do not establish a live retail
caller. Other missing material, lighting and environment paths still fail
explicitly. The separate ModelPreview adapter has not gained this mode yet.
The 40-second `run-direct-scene-modes` strict-audit regression reached the first
scene and continued past 250 scene submissions, reporting zero routine readbacks
and no instrumented surface-access violation. This regression does not activate
diagnostic mode in ordinary gameplay.

### Fog control and composition checkpoint

The Windows no-op `SCENE_SetFog` slot at `0x41f9ba` is now a direct-render
replacement entry. Both Windows binaries have the same 33-byte body. Its callers
are `SCENE_LoadLevel` and the player-gated water-transition helper `0x42315a`,
matching DREAMSFX `0x29288` and its `0x2c904` caller. The original Windows stub
still runs for its stack-check/ABI effects. Host fog updates happen at those
calls, not on every rendered frame; game decisions and RNG remain lifted.

Inputs are the current project at `0x661e04`, double simulation delta at
`0x5e5388`, and player mode at `0x4fbaac` (player `0x4fba78 + 0x34`). The DOS
mode word `0x151d3c` corresponds to the same player-mode field: the HUD and
trigger comparisons match, as does the surrounding swimming controller.
The shared existing `port/fog.cpp` implementation now links through `ODRender`;
the viewer and recomp use one controller/table generator.

An original-x86 replay checks 61 combinations of project density, water height,
mode, delta and phase. Colour/density arguments and stored phase agree. It found
and fixed a phase-boundary error: retail stores the rounded float but compares
the still-unrounded x87 value against pi before wrapping. The old shared helper
compared the rounded float and could wrap early. The OpenDreams palette/lighting
tests also pass with that regression case.

The C scene packet carries explicit fog colour and 64 table bytes. GPU fog uses
camera W, runs after texture/key selection and before alpha blending, and does
not touch UI or shadow masks. Thumbnails receive scene fog before serialization;
final gamma remains exclusively at presentation. WDS3 captures serialize fog
state; WDS1/2 remain readable with fog disabled. New tests check WDS3 round-trip,
64 depth knots, 63 interpolants, disabled fog, alpha order and unfogged UI.

```powershell
uv run --with unicorn python recomp/windream/debug/render_fog_smoke.py
uv run python recomp/windream/debug/render_fog_live_smoke.py
uv run python recomp/windream/debug/direct_render_validate.py --gpu
uv run --with capstone --with pefile python recomp/windream/lift.py
uv run python recomp/windream/build.py --render-audit
```

The direct shader now uses the SST1 reciprocal-W selector, 8-bit wrapped table
deltas and fractional selector bits. Its fog blend factor includes the hardware
`(value+1)/256` bias, so an enabled all-zero table is distinct from disabled fog.
Scene RGB remains floating point until the RGBA8 target store; this does not
restore RGB565 rasterization or dithering. Two table patterns across all 65,536
selector buckets give 131,072 GPU samples within one RGBA8 channel value of an
independent fixed-point reference. The reference uses the SDK's packed table
pairs and the integer selector/blend rules in
[MAME d0c76bd, voodoo_render.cpp](https://github.com/mamedev/mame/blob/d0c76bd663cbf52adcce086cb3eacff612a2f77f/src/devices/video/voodoo_render.cpp).

The live controlled-water smoke changes project fog fields and the water-height
plane in its own child process. It never writes actor mode: lifted
`ENT_UpdateSwimming` must perform the transitions. Project fog `0x2a4c9e` at
density 0.0004, clearing on exit, water fog `0x006080` and clearing again all pass.
The run records exactly five updates including level load. During the four holds,
at least 75/25/100/25 additional scene submissions occur without another fog
update, proving that phase is not advanced every frame. All changed test inputs
are restored; routine readbacks remain zero. Reports and captures live under
`DREAMS_OUT/recomp/windream/run-direct-fog-transitions`.

**Still partial:** this is controlled integration evidence, not a naturally
played water level or a Voodoo hardware capture. Zero-density NaN table conversion
retains the existing explicit zero-table policy without a hardware oracle.
Subpixel reciprocal-W differences from the original rasterizer are allowed by
the modern geometry path but still need broader visual validation. The legacy
ModelPreview now uses the same shared fog shader through its adapter below.
The 40-second first-scene run records one zero-density update at level load;
it does not prove those other branches. A 24-second capture regression also
passes the launcher capture check; its WDS3 scene replays through the shared
renderer at 640x480 and 1920x1080. Both live runs report zero routine readbacks.
The output gamma API already converts correction
value 0.8 to exponent 1.25; the GPU gamma-order test passes and no gamma-policy
change was needed. Reference SDK source is pinned at
[2f226f0, gamma.c](https://github.com/sezero/glide/blob/2f226f0f9225ce8ee83e6a4a7042981e719d19ee/glide2x/sst1/init/initvg/gamma.c).

### ModelPreview adapter checkpoint

`ModelPreview` now links to and submits through `ODRender`; the separate
`model_preview.glsl`, its pipelines, mutable index/palette GPU textures and
fixed-layout vertex buffer have been removed. Neither adapter creates another
SDL window or sokol instance. The shell still owns commit and presentation;
the preview registers a commit listener to retire resources afterward.

The adapter snapshots `ModelGraph`, submits original vertices with per-corner
node ownership and local Q15-derived float matrices, and leaves visual hierarchy
composition to the core. A separate synthetic root performs **viewer framing
only** (source Y inversion, centering and scale); it does not add another actor
placement or camera transform. The existing fixed-point inspection/joint helpers
remain separate. Projection now uses the core's `GREATER` convention. Runtime
projection uses Hor+ relative to its 640x480 or 640x360 reference viewport.

Palette selection persists by cache-slot/page identity. The selected row is
snapshotted until another page binds, while pixel mutations create immutable
texture versions. Draw lists and material batches may change between poses,
including an empty draw list. Staged palette changes survive pose updates;
source buffers may be mutated/freed before queued commands execute. Gamma is a
separate shared output pass into an RGBA8 target, with fog W converted back to
source units after preview normalization. Draw/target failures are reported to
the caller, with no software fallback.

`ODModelAdapterTests` passes cross-node corners, dynamic material/face lists,
equal-depth rejection, palette retention/rebinding, source mutation, resize
with pending work, ten model reloads with stable live resource count, fog units,
final gamma and commit-listener shutdown. Real ODViewer captures of Project0,
Project39 and animated BA0, plus ODRuntime's first scene after New Game, are under
`DREAMS_OUT/recomp/model-adapter`. Animation controls previously consumed the
entire image area at the default window size; the animation pane now reserves
more height, and the CLI animation smoke requires actual scene submissions.
The existing shared compositor corpus still passes 256 checkpoints and
14,769,600 packed pixels. The subsequent 25-second strict-audit recomp run
`run-shared-preview-regression` reaches the first scene and 200 scene submissions
with zero routine readbacks and no reported instrumented surface-access error.

```powershell
cmake --build opendreams/build/win-msvc-x64-debug --target ODModelAdapterTests ODViewer ODRuntime
opendreams/build/win-msvc-x64-debug/ODModelAdapterTests.exe
opendreams/build/win-msvc-x64-debug/ODViewer.exe --cue1 $env:DREAMS_CUE1 --cue2 $env:DREAMS_CUE2 --preview-project Project0 --frames 12 --capture out/recomp/model-adapter/project0.png
opendreams/build/win-msvc-x64-debug/ODViewer.exe --cue1 $env:DREAMS_CUE1 --cue2 $env:DREAMS_CUE2 --preview-animation BA0 --frames 45 --capture out/recomp/model-adapter/animation.png
opendreams/build/win-msvc-x64-debug/ODRuntime.exe --cue1 $env:DREAMS_CUE1 --cue2 $env:DREAMS_CUE2 --skip-intro --start-new-game --frames 3600 --save-root out/recomp/model-adapter/runtime-save --capture out/recomp/model-adapter/runtime.png
```

These commands require the configured native compiler environment and CUE paths.
Use the absolute `DREAMS_OUT` equivalent if the local output root is overridden.
The ten reloads exercise the adapter's model/resource lifetime, **not** the still
required ten full recomp level reloads. Hidden-node metadata, type-1 viewer
diagnostics, active per-object lighting and grayscale modes remain incomplete;
the latter two fail explicitly. Viewer DSN inspection keeps its existing 256
LOD metadata, while the recomp uses the recovered 128 material LOD. Passing
captures does not establish whole-frame retail parity or other-backend execution.

### Radial lighting kernels and capture

Two shared C helpers now implement the recovered type-1 owner-local transform
and flat-shade calculation. `od_radial_light_local` applies the rotation transpose
to light-minus-owner position, retaining the original non-unit-matrix behavior.
`od_radial_flat_shade` preserves integer centroid division, normal-dot truncation,
32-bit wrap, float distance storage, the unusual `(outer-distance)/outer`
attenuation and byte output. Controlled original-x86 replay passes 1,033 shade
cases and 500 transform cases, including radius boundaries, zero distance,
multiple lights, rotations and non-unit matrices.

WDS4 introduced exact original integer vertices, the eight node light
indices and original world-space light records referenced by submitted nodes.
Generated view/node-space light scratch is deliberately excluded. The source
capture oracle verifies those bindings and produces identical packets after
poisoning the old light-transform scratch. Both retail captures still pass the
existing pose, camera, encoded-parent-1 and recorded-spawn checks. WDS1–3 remain
readable; old packets do not pretend to contain exact lighting source vertices.
An 18-second strict-audit first-scene run also produces a live WDS4 capture under
`run-lighting-source-capture`. Its zero-light route is a capture regression,
not evidence of live per-object lighting support.

```powershell
uv run python recomp/windream/debug/direct_render_validate.py --gpu
uv run --with unicorn python recomp/windream/debug/render_light_smoke.py
uv run --with unicorn python recomp/windream/debug/render_light_live_smoke.py
uv run --with unicorn python recomp/windream/debug/render_scene_mode_smoke.py
uv run --with unicorn python recomp/windream/debug/render_scene_smoke.py out/scratch/retail-gdidream-222659.dmp out/scratch/retail-gdidream-223423.dmp
```

`prepare_scene_lighting` (originally `prepare_radial_lighting`) runs for supported textured nodes. It composes
visual poses from source locals, transforms radial lights without reading old
view/node caches, and uses the new camera/frustum to identify drawable faces.
The adapter returns ordered light-vector, normal-dot and one-byte face-shade
writes. The recomp mirrors them to guest storage using entry-specific audit tags.
A culled face is not rewritten, so its previous shade survives naturally in its
own allocation; there is no address-keyed shade cache to outlive freed models.
The block palette request uses the head's resulting or retained shade, and the
existing page-identity palette cache still determines whether that request binds.

The original-x86 lit → culled → lit test yields shades `[31,30]`, `[31,14]`,
`[15,14]` and exactly matching normal-dot writes. GPU tests verify those palette
rows and a reused source address with a new initial shade. WDS5 added normal
addresses for feedback; WDS6 additionally records the global light-transform
prefix count. Earlier unlit captures remain readable, but cannot invent missing
lighting state. A controlled live run binds and moves one radial light on a
688-face submitted node, changes 35 face shades and then 106 normal-dot values,
and resumes after unbinding with zero routine readbacks. Its report/captures are
under `DREAMS_OUT/recomp/windream/run-direct-radial-light`. This manipulates light
inputs in an isolated child, not an unmodified gameplay effect-trigger route.

Type-2 flat lighting is now covered by the [demo-informed follow-up](debug-renderer-contracts.md).
**Still partial:** inactive bound slots, bindings outside the refreshed
prefix, unsupported Gouraud/specular modes and lit-frame end callbacks fail explicitly. Retail removal can
leave a live slot beyond the count used by its view-transform loop; this stale
view-state case must not be silently recomputed from current world inputs.
Near-plane temporary-face lighting, stored-normal versus new geometric culling,
full scratch-consumer/ABI closure, naturally triggered effects, shadow interleave
and the viewer's missing light-input adapter remain open. The supported path uses
original triangles with GPU clipping, not the old temporary near-clip face list.
The controlled passes do not close R0/R2 or imply whole-frame pixel parity.

### Shared renderer interfaces

- `ODRender` owns sokol graphics implementation, the C renderer API, host pose /
  projection helpers, generated shaders and GPU resources. `ODGraphics` contains
  the SDL graphics backend. ImGui's implementation remains in `ODShared`.
  The recomp imports its existing SDL build, enables C++17 and links the same
  graphics targets without linking ODShared, loaders or ImGui.
- `direct.h` accepts host-owned `od_scene_packet`, `od_draw_2d`, pose/corner
  arrays and generation-checked `od_render_id` handles. Guest pointers never
  cross this API. Calls consume inputs before returning and encode GPU commands
  in order. Uploads are immutable snapshots. Release invalidates the handle
  immediately and queues retirement until the host commits the frame.
- Targets have separate logical and drawable dimensions, ping-pong RGBA8 colour
  images and a depth attachment. The basic scene path composes row-major local
  affines in floating point, resolves each corner through its own node, projects
  on the GPU and uses `GREATER` depth with depth writes. It currently supports
  flat/RGBA-textured triangles, keyed alpha and ordered translucent draws;
  it is **not** the completed retail material/lighting pipeline.
- GPU 2D accepts already-normalized packed source/coverage texels, fills,
  half/integer blends, raw copies, caption/text bands, dimmed snapshots and movie
  placement. Copies are shader passes, not D3D-only native copy calls. No core
  composition operation reads a target back. Native sprite/faded-text/gauge
  normalization now supplies live operations and exact-width scratch stores;
  remaining special branches and the broader UI corpus are still open.
- `od_renderer_output` applies final gamma inside the host's presentation pass.
  It does not commit or present. The host remains the presentation owner.
  Source alpha is not a universal transparency convention: packed RGB555 uses
  alpha byte 1 to preserve its unused high bit through exact raw copies.
- `replacements.py` generates public entry wrappers plus `wd_original_VA`
  bodies. Direct calls and dispatcher entries use the same public symbol.
  `wd_call_reference` suppresses replacements through a nested reference scope.
  An original or replacement consumes the existing guest return address once.
  Registration happens before guest execution; no game handlers are installed
  by default.
- The native scene-input adapter reads original node/vertex/face records through
  a bounded reader shared by the live arena and retail-dump tests. It resolves
  every corner owner, retains hidden nodes as transform dependencies and separates
  the camera root from world poses. The inverse camera comes from its local
  eye/rotation, not the composed feedback fields. Original near/far, focal and
  centre values drive native and Hor+ projection.
- `--capture-scene` installs an explicit one-frame observer at `REND_DrawFrame`.
  It snapshots inputs before visual work, then calls the original frame unchanged
  through the reference scope. The `.wds` packet can be drawn by the shared core.
  This is diagnostic capture/replay, not a playable direct renderer or a planned
  software/readback stage in the selected architecture.
- `render_boundary` tracks surface ranges, allocation generations, dimensions,
  pitches, exact aliases and CPU/GPU authority. Ambiguous partial overlaps fail
  registration rather than inventing a layout. DIB create/delete participates
  in this registry. Stale IDs do not revive after reuse or registry reset.
- Generated `WD_RENDER_AUDIT` probes classify explicit memory operands and
  MOVS/STOS/LODS ranges, including backward bulk copies. Unknown access to a
  registered GPU-owned range reports instruction/address/direction and aborts
  unless a diagnostic callback is installed. GDI uploads/snapshots also check
  ownership. Audit coverage is incomplete: implicit stack operations, repeated
  comparison/scan accesses and other host `PTR()` transfers still need closure.

The shared core's scene and 2D APIs are usable now. Complete lighting/material
descriptors and the typed CPU export/readback request API remain to be added.
Current borrowed sokol views/images are for host integration and the test
oracle, not authorization for routine scene downloads.

## Stage status

| Stage | Current evidence | Required exit work |
|---|---|---|
| R0 | Live handlers installed; registry, transform replay and local-camera tests pass; native UI scratch matches tested x86 cases; segment-stack ABI defect fixed | Close supported Windows game/debug callback and ABI/metadata dependencies, alternate-target ownership, lifetimes and unclassified GPU-surface access |
| R1 | Live direct boot, first scene/HUD, dialogue and movement; shared ModelPreview adapter; 640x480, 1280x960 and fullscreen 3840x2160 routes exercised | Broader integrated acceptance and uncovered startup/UI variants remain |
| R2 | Textured 3D and depth/order primitives; source-derived GPU shadow matches recorded 65536-byte oracle; thumbnail export; diagnostic mode, fog, lit 0x16/0x17 corners, flat-lit 0x18 and ordered environment UV versions | Exercise additional Windows game/debug routes; fix visible shadow/clipping/material/light/fog defects, required obscure/mirror modes and game-consumed metadata/callback errors. Tiny visual-only numerical differences are accepted with evidence |
| R3 | Normalized sprite/text/copy/dim/movie streams match retail CPU checkpoints, including flag 4 and signed-high coverage | Close required Windows sprite/text/gauge/fire/menu/caption/movie/debug-line routes, helper effects, source versions/lifetimes and direct bypass writers |
| R4 | D3D11 standalone/export tests and direct audit first-scene routes pass; muted/headless runs preserve mixer state and verify hidden native windows; four dialects also generate | Windows game/debug acceptance matrix, required exports, resize/fullscreen/input/present, save/load, ten reloads, bounded resources and measured pacing; then direct becomes default |

No stage is marked complete. The new port-map rows cover shader arithmetic,
live main/alternate frame boundaries and UI handlers including `SPR_DrawMasked64`.
They are `adapted`, `partial`, `reviewed=no`, explicitly listing the absent
rendering/guest closure. Both Windows Ghidra programs carry the same map tags.

The table identifies Windows work/coverage gaps, not a requirement to prove every
historical numeric branch bit-for-bit. Convert a broad gap into a concrete route,
expected output/side effect and passing check before opening more implementation
or reverse-engineering work. Use the spec's blocker/coverage/variation/deferred
classification. Required development/debug modes remain genuine coverage work.

## Validation

Reproducible standalone build, without ODShared or ImGui:

```powershell
uv run python recomp/windream/debug/direct_render_validate.py --gpu `
  --fixtures out/recomp/render-2d-smoke `
  --fixtures out/recomp/render-2d-smoke/long-capture
```

The fixture directories are outputs of `render_2d_smoke.py` and are local,
game-derived artifacts. The runner checks their presence and records SHA-256
hashes; it does not silently substitute synthetic data. Its build/shaders/logs
and `results.json` live under `DREAMS_OUT/recomp/direct-render`.

Measured correctness evidence:

- 256 original-CPU checkpoints, 14,769,600 packed pixels: zero mismatches in the
  new RGBA8 shared compositor. This includes both recorded fixture runs; repeated
  controlled cases are not additional unique branch coverage.
- 131,072 CPU round trips and 131,072 GPU round trips: every 16-bit value in each
  format, including RGB555's high bit. GPU source mutation after upload leaves
  the uploaded version unchanged.
- Synthetic scene checks exercise independently transformed cross-node corners,
  `GREATER` overlap, modern colour retention, integer operations over the scene,
  final output correction, high-resolution canvas placement and stale handles.
- Surface checks cover aliases, pitch, crossing a range boundary, backward copies,
  zero-length operations, freed/reused allocations and nested reference calls.
- The production replacement dispatcher is used by the lifted replay of eleven
  original functions against two retail dumps. Both preserve the compared
  composed fields for all 735 nodes, including hidden subtrees and the tested
  stack/register/x87 relationships. This does not prove every live-frame flag,
  callback or metadata effect.
- OpenDreams viewer/runtime and the recomp build after the dependency split.
  Finite hidden-window application runs also produce viewer and runtime-menu
  captures after the split; these are regression smokes, not pixel parity proof.
  A 20-second software run produced snapshots; a 30-second audit build reached
  the first level and produced snapshots. GPU-owned game surfaces are not yet
  active, so this is regression evidence, **not zero-readback acceptance**.

The GPU oracle downloads checkpoints intentionally. The shared compositor uses
no readback; whole-game readback removal remains an integration gate. Performance
numbers from the historical isolated smoke must not be attributed to this core.
Other backend shader generation is not evidence of backend execution.

Rebuild/replay and audit commands:

```powershell
uv run --with capstone --with pefile python recomp/windream/lift.py
uv run python recomp/windream/build.py
uv run python recomp/windream/build.py --render-audit
uv run python recomp/windream/run.py --render-audit --seconds 30 `
  --keys 2000:ESC,5000:RETURN,8000:ESC --snap-ms 10000
uv run --with unicorn python recomp/windream/debug/render_smoke.py --lifted `
  out/scratch/retail-gdidream-222659.dmp out/scratch/retail-gdidream-223423.dmp
uv run python tools/check_port_map.py
```

## Scene adapter evidence

The native adapter captures **735 nodes, 4,852 original vertices and 6,439 faces**
from each independent retail dump, including **1,169 cross-node triangles**.
All corner pointers resolve to their recorded owners. Poisoning old composed
transforms, vertex flags/camera/projected fields, visible-list links and face/node
culling bits leaves its serialized output identical. It rejects inconsistent
parents, cycles and unresolved corners. Face bits 1/2 are explicitly removed:
`REND_CullFaces` resets surviving records with `flags &= 8`; those bits describe
old cull/near-clip results, not persistent hiding. Filtering bit 2 initially
dropped four source triangles in the first capture; the corrected diagnostic
submits them for fresh GPU clipping.
Camera-root visibility is handled separately from ordinary subtree hiding,
matching the hierarchy walk.

The camera's derived inverse rotation matches retail; inverse translation agrees
within one unit of its integer result. Direct float projection was compared with
fresh coordinates produced by original x86:

| Retail capture | Eligible projected vertices | Maximum difference | 95th percentile |
|---|---:|---:|---:|
| `222659` | 835 | 1.9342 px | 1.0750 px |
| `223423` | 627 | 2.3231 px | 1.1048 px |

These are measured discrepancies between modern float and original quantized
projection, not pixel parity. A fixed-capture regression envelope of 3 px maximum
/ 1.25 px p95 guards these fixtures; it is not a guarantee for arbitrary levels
or poses. Original pointer-1 relocation and recorded spawn `(-319,-625,-3187)`
checks still pass. After executing the original placement routine in a private
replay, recapture through the native adapter places the player's world-space
root exactly at that spawn; the camera remains outside the pose hierarchy.

The two packets draw 3,934 / 3,941 eligible original triangles at 640x480 and
1920x1080. A 30-second recomp run also captured its first full-sized 3D frame;
the same core drew 4,299 eligible triangles from that live packet at both sizes.
The GPU diagnostic uses opaque colours by primitive type and two-sided geometry.
It does **not** validate materials, lighting, fog, transparency, face culling,
shadow appearance, letterboxing or UI. Its PNG readbacks are explicit test exports.
The source adapter retains material slots, face normals and signed UVs for the
next integration work; it never uses old visible lists as geometry input.

```powershell
uv run python recomp/windream/debug/direct_render_validate.py
uv run --with unicorn python recomp/windream/debug/render_scene_smoke.py `
  out/scratch/retail-gdidream-222659.dmp out/scratch/retail-gdidream-223423.dmp
uv run python recomp/windream/run.py --capture-scene --seconds 30 `
  --keys 2000:ESC,5000:RETURN,8000:ESC
out/recomp/direct-render/build/WDSceneGpuTests.exe `
  out/recomp/windream/run/direct-scene.wds out/recomp/scene-adapter/live-first
```

Captures, metrics and diagnostic images stay under `DREAMS_OUT/recomp`. The snapshot
format writes explicit little-endian fields with bounded counts and validates
topology on read. Default rendering remains software.

## Remaining delivery sequence

Close R0's remaining native contracts and access audit through the installed
live handlers. Extend R1's working scene/UI/presentation route to the uncovered
startup, gauge and input/resize variants at native and widescreen resolutions.
Complete R2 and R3 around that same path; do not add CPU gauge/shadow/readback
bridges as prerequisites. R4 closes required Windows game/development/debug
exports, operational tests and measured pacing. Additional platform execution
is deferred; visual-only precision investigations need an observable problem
or an unresolved source contract under the spec's fidelity policy.

The explicit `direct`/`software` selection is available for the current slice.
Keep direct opt-in until Windows R4 acceptance, then make it the default. Keep
software as an explicit reference selection; unsupported direct operations must
report their contract instead of silently changing renderer.
