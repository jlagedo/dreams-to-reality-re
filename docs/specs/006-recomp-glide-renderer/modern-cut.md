# Modern renderer boundary in the Windows recomp

Date: 2026-09-29. Follow-up to [spec 006](spec.md), for the owner's question:
where can a modern renderer replace a cluster of functions rather than only
the Glide calls?

**Implementation direction is now selected in [spec.md](spec.md): direct 3D
and GPU 2D from the first integrated renderer.** This document preserves the
boundary evidence and smoke results. Compatibility composition remains on the
game side; visual transforms/projection and offscreen 3D belong to the renderer.

**Acceptance scope updated 2026-09-30:** Windows/D3D11 game and supported
development/debug rendering close in 006. Imperceptible visual CPU/GPU numerical
differences are allowed; game-consumed effects and memory contracts remain
protected. Historical clipping/parity and other-platform work lists below do
not override [the current spec](spec.md).

**Finding:** the engine has a compact rendering front end, entered through
`REND_DrawFrame` / `REND_DrawFrameEx`. Within it, the useful geometry boundary
is **after node transform composition and before object culling in
`REND_DrawObject`**. The existing rasterizer hook is later than this boundary.
Keep the retail hierarchy traversal initially, replace per-object rendering,
and adapt the frame wrappers to the GPU backend and destination contract.

This is a proposed implementation boundary supported by static evidence and
the [CPU/GPU smoke results](#smoke-results-2026-09-29) below. An isolated
replacement-wrapper experiment exists; the running game's renderer has not
been replaced. The tests establish the listed contracts on captured states,
not whole-game rendering parity or owner review.

## Evidence for a real cluster

Fresh read-only Ghidra instruction and cross-reference inspection confirms:

- `REND_DrawFrame` (`0x459320`) and `REND_DrawFrameEx` (`0x4593a4`) are the
  only recorded callers of `REND_DrawScene` (`0x47e700`).
- That walker is the only recorded caller of `REND_DrawObject` (`0x47e498`).
- Object culling, vertex transforms, normal dots, face culling, projection,
  object-light transforms, lighting and environment mapping are called from
  `REND_DrawObject`; the near-clip helpers are below face culling.
- The feature export contains **17 function entries, totaling 23,535 body
  bytes**, in `0x478800..0x47e7ab`. This includes the triangle collector and
  inactive helpers, not just the live pipeline. External entry edges in the
  export are the two frame wrappers (scene calls and collector pointer
  stores), plus the unused update-only walker calling its transform helper.
  The software rasterizer and light-management API are outside this range.
- The 17 bodies, both frame wrappers and the update-only walker are
  **byte-identical at the same addresses in WINDREAM and GDIDREAM** (20/20
  extents compared directly from the configured disc executables).

There is also independent source-organization evidence: the Dutch and Spanish
executables retain a partial OMF renderer object containing Cryo's original
`Process_Obj_`, `Process_Hierarchie_`, `Update_Obj_`, `Update_Hierarchie_`,
`Build_Obj_Lights_`, `Build_Obj_Env_Mapping_` and mirror functions. See
[localized-build-symbols.md](../../localized-build-symbols.md). Its source
filename is unknown; the following `3DC_MATH.C` object is a separate object.
The 17-entry address range above is a functional grouping, not a claim that
all 17 belonged to that same source file.

Cross-references are static evidence: computed references not recovered by
Ghidra could be missed. Runtime traces remain the check before bypassing code.

## Exact boundary

```text
GAME_DrawFrame 0x423f60                 keep collision separation and 2D
  REND_DrawFrame 0x459320               adapt frame setup/output
    REND_DrawScene 0x47e700             retain hierarchy walk initially
      REND_DrawObject 0x47e498
        parent/local composition       preserve retail arithmetic and writes
        ---------------------------    proposed geometry boundary
        object cull                    replace
        vertex transform / face cull   replace
        projection / lighting          replace or adapt from retail rules
        SW/Glide face hook              replace
    callback / node-box update         retain or explicitly account for
    software span flush                replace with GPU target completion
  letterbox / message / later HUD       retain control; GPU pixel leaves (2d-cut.md)
```

The composed translation's final store is at `0x47e55e`; the old object cull
is called at `0x47e58e`. Between them are software edge-list initialization
and the explicit no-draw flag check. The rasterizer hook at `0x47e5fb` is
**after** culling, near clipping, integer projection and lighting. Replacing
that hook alone cannot recover geometry already rejected for the old view.

Use a function replacement at `0x47e498`, with the preserved composition
step and a new submission step. The interior addresses identify the semantic
boundary; they are not a recommendation to jump into a function with its
register saves still on the guest stack.

| Role | Windows address | Treatment |
|---|---|---|
| Main frame | `0x459320` | Adapt; EAX supplies the CPU destination and the root pointer is at `0x661ee8` |
| Alternate-root frame | `0x4593a4` | Adapt; EAX is resolved by `MDL_GetNode`, EDX is retained for the span destination |
| Hierarchy walk | `0x47e700` | Keep traversal order and subtree hiding initially |
| Per-object processing | `0x47e498` | Keep composition, replace the visual tail |
| Sphere culling | `0x478980` | New frustum/visibility calculation |
| Vertex transformation / shared vertices | `0x478c2c`, `0x478dac` | New geometry path, including vertices owned by another node |
| Normal dots / face culling / near clipping | `0x478f80`, `0x47b0bc`, `0x479320`, `0x47907c` | New visibility and GPU clipping |
| Integer projection | `0x47b228` | GPU floating-point projection |
| Object-light transformation / lighting | `0x47b3d0`, `0x47b7e0` | Preserve the lighting rules in the new path; existing functions consume old visible lists |
| Environment UVs | `0x47e094` | Account for the original update order; currently called after face submission |
| Software hooks | `0x473014`, `0x4731b8` | Bypass in GPU mode; clear/adapt the post-order hook too |
| Software flush | pointer `0x4aa708` | Replace; keep the original path available for comparisons |

The host renderer should receive geometry/material descriptions from a guest
adapter, not pointers cast to native `MDL_Node` structs: guest pointers are
32-bit and the host layouts differ.

## Retail already has a transform-only path

`0x47e634`, identified as `Update_Obj_` by the OMF evidence, performs the
matrix/translation composition without culling, projection or drawing. The
instructions use the same matrix multiply and float-to-integer helper as
`REND_DrawObject`. Its scale constant at `0x4c6430` equals the draw path's
constant at `0x4c6428`: both are exactly `1/32768`.

`0x47e7ac` (`Update_Hierarchie_`) resolves a root handle and walks the hierarchy
with the same `flags & 1` subtree test, calling that transform-only helper. It has
no recorded caller. It also jumps to the shared epilogue at `0x47e7a0` inside
the scene walk. Reusing this function therefore requires checking the lifter's
shared-tail handling; it is not a proven drop-in replacement for the frame.

The first implementation can preserve the live composition prefix directly,
or validate `Update_Obj_` as an equivalent helper. Compare composed node
memory and the guest calling convention on captured retail states before
choosing. There is no need to invent new transform math for the recomp.

## What crosses the boundary

Inputs already exist in guest memory after loading and animation:

- hierarchy links, local translation and Q15 rotation; the camera root;
- model-space vertex positions at vertex `+4` (40-byte records), normals,
  original face records, UVs and current material types;
- current texture pages, palettes, shade/light inputs and visibility flags;
- the requested viewport, near/far planes and output destination.

Read original block `records/count/stride`, not just the old `visible` list:
the latter contains the old camera's culling and generated clip results.
Resolve each face corner against its owning vertex array; bridging faces can
reference another node. Retain the original triangle definitions and explicit
hidden/skip flags while recomputing frustum visibility for the modern camera.

The investigation considered using the composed camera-space transforms as
an initial render input. The selected direct-renderer interface instead takes
local/posed geometry and camera state separately. The original composed fields
remain game-facing compatibility state, not the authoritative visual pose.
This also avoids imposing a guest-memory interface on OpenDreams' renderer.

The important output back to the game is **node `+0x4c` in camera space**.
Sound and line-of-sight code read the previous render's value. Preserve its
arithmetic, hierarchy visitation and timing, including leaving hidden
subtrees stale. Do not feed enhanced-camera values or interpolated display
poses back into these fields. The previous static consumer audit is in
[engine.md](../../engine.md#what-the-game-reads-from-the-renderer-verified).

Keep resource loading, attachment, animation, collision, camera gameplay and
palette-state updates on the game side. Palette effects use the shared RNG;
moving those updates to an independent render clock would change the game.
The adapter consumes the live relocated graph: it should not reconstruct a
second scene placement system. Validation must include the player model's
encoded source pointer `1` resolving to its first node and the composed model
root matching the recorded spawn, as required by `AGENTS.md`.

## The frame boundary includes more than the window

| Caller | Observed render request |
|---|---|
| `GAME_DrawFrame` `0x423f60` | Main framebuffer, after collision separation |
| `GAME_SaveThumbnail` `0x40fd54` | 64x64 into guest `0x5d6b98`, then copies/writes `0x2000` bytes |
| `ENT_RenderShadowTexture` `0x43eb67` | 128x256 with another camera; temporarily reparents the actor, substitutes material `0x1b`, then restores it |
| `0x423dc8` | Another direct caller of the main frame; no incoming reference found |

The shadow frame's destination comes from `MDL_FindMaterial("OMBRE2")` during
shadow initialization. The smoke replay below confirms a 65,536-byte mask of
palette indices, with each horizontal pair equal, written through the 128x256
16-bit raster destination. It is not window RGB565 colour. General sampling
parity still needs validation. Save thumbnails also prove that always writing
to `g_frameBuffer` would be wrong.

The frame callback at `0x4aa704`, node-box maintenance, the diagnostic
triangle-collection branch and original hook restoration must each remain
explicit in the wrapper contract. They are not automatically removed because
the pixel backend changes. Normal presentation stays at the existing present
boundary; completing a shadow or thumbnail target must not present a window.

HUD, text, sprite, movie and menu writers are another cluster, outside this
3D boundary. Their CPU framebuffer access still needs the composition solution
discussed in spec 006. Finding the 3D cut does not resolve that separate issue.

## Implementation proof to do

1. Record baseline calls/arguments, destinations and composed-node outputs for
   gameplay, a save thumbnail and the real-shadow mode.
2. Prove the replacement ABI and preserved transform step against those
   captures; account for shared tails, flags, x87 and direct/indirect calls.
3. Keep the hierarchy walk, substitute per-object submission and target
   completion, and compare game-facing memory effects with the software path.
4. Validate all material modes, cross-node corners, animated pages, hiding,
   near-plane crossings and lighting before enabling the enlarged view.
5. Validate direct floating projection and visibility together at high
   resolution and Hor+ widescreen, leaving game-facing transform updates
   unchanged. This capability is part of the first direct slice, not a later
   replacement of an integer-screen-space renderer.

## Smoke results, 2026-09-29

The reusable harness is
[`render_smoke.py`](../../../recomp/windream/debug/render_smoke.py), with
[`render_smoke_host.c`](../../../recomp/windream/debug/render_smoke_host.c)
and [`render_gpu_smoke.cpp`](../../../recomp/windream/debug/render_gpu_smoke.cpp).
Generated lifted C, inputs and reports stay under
`out/recomp/render-smoke/`.

The CPU reference executes original x86 instructions in Unicorn on two
previously captured **retail-process** dumps. Its full-front-end mode skips
only the pixel rasterizer hooks. It is compared against an early return after
the original composition prefix and against redirecting object processing to
the original transform-only helper. The compiled-C experiment extracts 11
functions from the current lift and replaces per-object processing with a
hand-written wrapper that invokes the lifted transform helper. The production
lift/runtime are not patched.

| Check | Result | Scope |
|---|---|---|
| Original front end vs preserved prefix vs transform-only helper | Identical 48-byte composed position/matrix fields on all **735 nodes** in each dump; **229 / 224** visited nodes | Two snapshots, not every level/pose |
| Lifted C vs original x86 | All registers checked at scene return, ESP, x87 control word/empty-stack balance and composed fields match in **six cases** | Main traversal, update-only shared tail, hidden subtree, for both dumps |
| Subtree hiding | Same visits and unchanged skipped-node state; forced cases leave **224 / 197** visits | The original `flags & 1` contract |
| Face input mapping | **6,439 faces**, **1,169 cross-node triangles**, no unresolved corners in each snapshot; 56- and 68-byte records present | Reading every corner through its owner is mandatory |
| Player source pointer `1` | Four `XH_.DAN` parent links resolve to the first live model node | Original source file checked against the retail loaded graph |
| Spawn contract | Original `ENT_MoveToPlayerSpawn` places the model root at **(-319, -625, -3187)**; both traversal paths then agree | Project record and original instructions, no extra authored-root translation |
| Frame destinations | Main, 64x64 thumbnail and 128x256 alternate-root arguments reach the requested destination | Original wrapper execution with pixel flush intercepted |
| Real shadow | Original software rasterizer visits **27 nodes** and overwrites a sentinel-filled **65,536-byte** target with indices 0/1; horizontal pairs match | Shadow-enabled recomp capture, executed with original x86, separate from the retail-dump evidence |
| Shadow state restoration | Original and substituted object path leave identical composed fields and restore the viewport to 640x480 | Full `ENT_RenderShadowTexture` call, including temporary reparent/material changes |
| sokol/D3D11 | Four offscreen sizes, `GREATER` depth with writes, upper/lower image orientation, RGBA8 readback and RGB565 conversion pass | Synthetic overlapping triangles, no window or present |

The update-only hierarchy's shared epilogue works with the current lifter.
The test extractor initially omitted its **numeric indirect tail target**;
including `0x47e7a0` in the closure fixed the harness. This was not a lifter
defect. A production replacement mechanism must account for direct calls,
indirect calls and tail transfers, not only function-name references.

The native wrapper also exposes a calling-convention rule: directly invoking
the lifted transform helper consumes the existing guest return address. The
wrapper must not pop it a second time. Host callbacks that push their own
return address have a different contract.

GPU timings on this machine, 30 measured samples after five warmups, include
three flat triangles, synchronous readback and CPU RGB565 conversion:

| Target | Median | p95 |
|---|---:|---:|
| 640x480 | 0.475 ms | 0.544 ms |
| 64x64 | 0.028 ms | 0.037 ms |
| 128x256 | 0.063 ms | 0.067 ms |
| 1920x1080 | 2.298 ms | 2.465 ms |

These are feasibility measurements, not game-frame timings, and do not
predict Metal, GL or browser costs. The GPU smoke tests truncation to RGB565,
not Voodoo dithering, fog, palette filtering or final gamma accuracy.

Run from the repository root (the dumps are local, game-derived artifacts):

```powershell
uv run --with unicorn python recomp/windream/debug/render_smoke.py --lifted --gpu out/scratch/retail-gdidream-222659.dmp out/scratch/retail-gdidream-223423.dmp
```

`--gpu` defaults to the existing debug build's pinned sokol checkout; use
`--sokol-dir` for another existing copy. `--lifted` requires the current
generated recomp and Windows clang-cl toolchain. CPU-only Unicorn replay does
not require a compiler. Add `--shadow-dump <full-dump> --shadow-arena <base>`
for a capture with real shadow enabled and Duncan in shadow slot zero. The
arena is zero for a retail-process dump, or the host base printed by the
recomp. The replay never modifies that dump or a running process.

The shadow capture used the existing recomp in an isolated write sandbox,
SDL dummy video/audio and `WD_POKE=0x4a3168=1`, with scripted New Game keys.
An initial attempt to wait for present 750 did not produce its intentional
crash capture within 45 seconds and was stopped. A second run was suspended
at 22 seconds, dumped with `MiniDumpWriteDump`, resumed and terminated. It
contained a live real-shadow slot; this is a fixture capture, not a sustained
playthrough or a passing whole-game integration test.

## Design refined by the smokes

Use three layers with explicit ownership:

1. **Guest adapter and frame scope.** Preserve the original wrappers' call
   ordering, hierarchy visitation and transform writes. Read the live
   relocated data while the game thread is at the render boundary. Carry the
   caller's target kind, destination, pitch, viewport and near/far settings.
   Save/restore hook state around each scope, including nested/special passes.
2. **Owned render input.** Copy or version the required geometry, corner-owner
   transforms, material modes, palette/page contents and lighting state.
   Do not let asynchronous GPU work follow mutable guest pointers. Resource
   identity needs a level/load generation as well as an address; invalidation
   must cover animated pages, palette changes and reloads.
3. **Shared GPU renderer and target completion.** Accept host geometry and
   material descriptions without understanding guest VAs or resource handles.
   Float projection and the enlarged frustum belong here. Return CPU pixels
   only when the target contract needs them; window presentation remains a
   separate outer operation.

The target kinds need distinct completion paths:

- **Main colour:** a GPU target consumed by GPU 2D composition, with display
  gamma after composition. Old guest framebuffer addresses identify surfaces;
  CPU materialization is required only at explicit export/compatibility barriers.
  High-resolution direct rendering is part of the first integrated slice.
- **Save thumbnail:** finish the requested small RGB565 image before the
  lifted caller copies/writes it. It must not present a window.
- **Shadow mask:** preserve the observed palette-index packing on the GPU and
  ensure later texture sampling sees the updated page. The software pass is
  an oracle or explicit diagnostic fallback, not an implementation stage or
  accepted completed path. Simply dropping type
  `0x1b` because the Glide face-mode table has no branch for it loses this
  Windows feature.

The tests support retaining the existing hierarchy walker and invoking the
validated composition helper from a replacement per-object function. Rewriting
the hierarchy walk is unnecessary for the first integration. The native
transform state and any enhanced/interpolated display state must stay separate.

At this investigation's checkpoint, live integration, material/light/env-map
timing, shadow sampling and high-resolution UI still needed work. Later results
are recorded in [implementation.md](implementation.md#stage-status). Windows
acceptance now follows the spec's visual/functional policy; exact raster parity
and other-platform execution are not blanket 006 gates. No port-map coverage or
owner-review flags were changed by this original investigation.

## Local audit outputs

All generated/disassembly output remains under `out/ghidra/cut/`:

- `modern-cut-inspect.txt`: live frame/walker/object instructions and xrefs.
- `modern-cut-contract.txt`: transform-only walker, handle resolver, constants,
  hooks, thumbnail and shadow callers.
- `modern-cut-edges.txt`: live xrefs to the front-end helpers.
- `modern-cut-audit.json`: function inventory, incoming feature-export edges,
  byte counts and Windows twin comparisons.

The Ghidra reads used `tools/ghidra_headless.py -process WINDREAM.EXE
-noanalysis -readOnly` with `Inspect.java` and `Decompile.java`. No function
was renamed, ported, marked complete or marked owner-reviewed in this audit.
