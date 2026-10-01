# Loader/renderer review, 2026-09-26

This is the durable adjudication record for the three independent worker batches.
Workers used separate Ghidra snapshots with `-noanalysis -readOnly`, wrote only
task-local evidence/patch files, and did not rename functions or edit the shared
registry. Root retained naming and main-project write-back responsibility.

## Sources and review order

Source one was each worker's docs-aware binary/corpus analysis. Source two for
accepted names was root's interpretation of numerical-address decompilation
packets with project names and documentation comments stripped, recorded before
opening the associated report or candidate list. Supplemental instruction reads
resolved missing Watcom arguments and decompiler constant folding.

Local reproducibility artifacts are under `out/re-gap-pass-20260926/`:

- `glide/`: primary/context and fog/alpha blind packets, address/code digests,
  report and corpus type scanner.
- `lighting/`: primary/supplemental blind packets, instruction logs, corpus and
  palette arithmetic checks.
- `placement/`: numerical-address loader/render/platform packets, per-scene
  displacement inventory, raw face/UV counts and ARC topology.
- `orchestrator/`: independent review notes, baseline snapshot, extra instruction
  checks and binary provenance. All three binaries match the SHA-256 headers
  of their checked-in symbol exports.

These ignored files contain binary-derived working evidence; the current
conclusions are maintained in `docs/research/glide-renderer.md`, `docs/research/lighting.md` and
`docs/research/scene-placement.md`. No authored game asset is included in this record.

## Accepted naming evidence

| Addresses | Independently observed behavior |
|---|---|
| FX 66ac8, 66d58, 66f24, 66c54 | Refresh one texture, allocate/upload a texture, upload populated slots, reset texture/cache state |
| FX 66cb0, 672a8 | Convert selected RGB565 palette row; bind descriptor and update palette on page change |
| FX 670f0, 671d0, 67204, 67474, 67490, 674c0, 67500 | Open, fog-table setup, clip window, shutdown, background clear, swap/cache flags, depth-write selection |
| FX 67568, 68128, 68028, 6805c | Object triangle dispatch, queued translucent pass, LFB lock/unlock |
| FX 29288, 6f800 | Select project/water fog; bind materials to named face blocks |
| WIN 42d824, 42d96d, 42e126, 42e8b1, 42e5f2, 4031c3 | Palette-state initialization, named material registration, row-15 source colors/contrast, conditional row updates, shared-RNG RGB variation, saturated packed-color conversion |
| WIN 42dc42, 41c0a6 | Bind actor to material slot; update actor color offsets through the legacy projected-sample path |
| WIN 42ee5e, 477cb0, 477d78, 477da0 | Create actor-requested effect lights; allocate, remove and move light records |

Names in the registries carry machine-checkable calls, constants, references and
body lengths. Where a worker proposed a fact that was not literally present in
the instruction export, root replaced it with an observed assertion: e.g. light
record fields expressed as an address calculation instead of an immediate
`0x88`, or project +0xc8 gating a caller rather than the effect-light helper.
The semantic conclusion was checked separately; passing a weak constant alone
was not treated as proof.

The proposed software-span name at WIN 46fe20 was displayed before root's blind
review and is **not accepted as a two-source result**. No new software rasterizer
name is added by this pass. Existing registry names are reused for placement.

## Corrections made during adjudication

- FX fog C omitted live instructions: nonzero density is multiplied by
  0.00000625; the water branch uses delta×0.02, wrapped phase and
  `sin(phase)*0.00004+0.00018`, color 0x6080. The corrected worker packet and
  root's raw constant reads agree. Root also checked the delta producer's
  200/elapsed → 30/fps and 0.2–5 clamps.
- Type 2/3 vertex alpha is unassigned, not 255. Root found the mismatch;
  the worker checked the actual stack slots. RGB remains explicitly 255.
- Light type labels were reversed/overstated in old docs: type 1 is
  position-only/radial; type 2 adds orientation. Type-2 creation and exact
  angular threshold remain unverified.
- Slot +0x1c is an actor pointer, written at WIN 42dc42. Assembly confirms
  both actor and global RGB adjustments reach the palette converter stack.
- Full palette rebuilds use `i*scale >>2`; rolling updates use
  `(cursor+1)*scale >>2`. Root distinguished the branches in the instructions
  after the worker's initial summary generalized them.
- WIN pixel-fetch/marker helpers 4020a8/4020ce are single RETs. Apparent
  sampling intent is not proof of actual framebuffer feedback. Shared RNG
  consumption by palette updates is real and must remain simulation-clocked.
- All 95 scene graphs decode. Collision voting was an inappropriate export
  gate; no guessed transforms are applied. Hardware type 2 clamps and type 9
  wraps, so the valid signed UVs must survive decoding.
- ARC's 15 open edges are in its 75 stored triangles. Do not cap them or
  invent six model faces to reconcile an older aggregate count.

Selected Glide function bodies ended prematurely at API calls. A read-only
preview of `RebuildFunctionBodies.java` recovered their existing instruction
tails (including swap's 256-slot reset). Final feature assertions must use the
refreshed bodies, not the old truncated lengths.

## Applied and validated

Root registered 12 WINDREAM and 19 DREAMSFX entries after the independent
reviews. All **691 WIN / 278 FX** registry entries pass against refreshed
feature exports. **690 WIN entries** pass the byte-identical GDIDREAM transfer;
VID_Init remains excluded because its default-backend byte differs.

Names, current documentation comments and the checked C layouts were applied
to the main project and saved. `re/symbols/` was exported for WINDREAM,
GDIDREAM and DREAMSFX; every checked name is present. Historical research-log
and explicitly marked investigation sections are excluded from live comments.
The pre-write-back project snapshot is retained under the local orchestrator area.

Validation: **177 pytest tests passed**, plus all three full-corpus checks for
scene face pointers, **95/95 tag-1 routes**, and collision triangles. The
targeted signed-UV/sampler/preview checks passed after the final preview edit.
Clang's 32-bit Windows target passed **282 layout assertions**; Ghidra imported
all 26 structures. Ruff and local documentation-link checks pass. A fresh
model recount found 41,608 faces, 12,373 bridges and no same-name material group
with mixed primitive types across the 159 models.

## Limits

No original-game screenshot or native-renderer parity run was performed. Zero
density behavior in the Glide utility, some scene/video texture descriptors,
type-2 light details, specific mover identities and the old model-count delta
remain explicitly bounded uncertainties. Mode 16's code branches are known;
an invented author-intent label is unnecessary. These limits do not block
using the original render graph, preserving UVs or implementing the documented
renderer/material paths.
