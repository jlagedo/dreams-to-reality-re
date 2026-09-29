# Demo-informed retail renderer contracts

Date: 2026-09-29. Follow-up to the July demo discovery and the working direct
renderer checkpoint. This pass checks retail contracts, corrects the UV field
description, and adds type-2 **flat** lighting. Gouraud and environment/mirror
rendering are not completed by this pass.

## Evidence and replacement boundary

The demo's Watcom symbols identify `3DC_HIER.C` as the owner of the following
stages. Addresses below distinguish the July Glide demo from European Windows
retail; structural correspondence is not a claim that every body is identical.
`Update_Obj_` and `Compute_Face_Illum_` also have checked whole-body transfers
from the July software demo, documented in `wip-windows-transfers.md`.

| Original routine | July Glide demo | Retail Windows | Direct disposition |
|---|---|---|---|
| `Process_Obj_` | `0x95f9e` | `0x47e498` | Replace visual tail; call retail transform-only helper |
| `Update_Obj_` | `0x961f4` | `0x47e634` | Retain original transform arithmetic for game feedback |
| `Process_Hierarchie_` | `0x96337` | `0x47e700` | Retain traversal, hiding, scratch setup and light refresh |
| `Update_Hierarchie_` | `0x964cc` | `0x47e7ac` | Reference walker; not a wholesale replacement for the render walker |
| `Compute_Face_Illum_` | `0x92397` | `0x47b3d0` | Recover owner-local light vectors and their metadata writes |
| `Build_Obj_Lights_` | `0x92844` | `0x47b7e0` | Shared flat-light kernels; Gouraud remains open |
| `Build_Obj_Env_Mapping_` | `0x9593d` | `0x47e094` | Still open; updates UVs after the draw hook |

Fresh read-only decompilations confirm the same important ordering in both
builds: object transform, visual culling/vertex work, light transform,
projection, shading, draw hook, then environment UV update. The existing cut
remains appropriate. Replacing the entire render walker with the update walker
would omit render setup and change surrounding effects.

The retained Windows walk skips hidden subtrees. Re-running `render_smoke.py
--lifted` against two independent retail captures checks full/prefix/helper
composition, visitation, hidden-subtree staleness, ABI/shared-tail behavior and
three target destinations. All pass: 735 captured nodes, 229/224 visited nodes.
The scene-adapter replay still resolves encoded parent `1`, player spawn
(-319,-625,-3187), and 1,169 cross-node triangles in each capture. These tests
retain their original scope; they do not close every live callback or flag.

## Object and light layouts

The 40-byte demo `C3D_VERTEX` agrees with the retail consumed fields: flags at
0, integer local coordinates at +4, float transformed coordinates at +0x10,
screen coordinates at +0x1c and reciprocal depth at +0x24. The direct renderer
continues reading source coordinates and computing its own visual transforms.

The demo `_objet` identifies +0x84/+0x88 as `o_nbr_uvtext/o_ptr_uvtext`.
Retail `MDL_RelocNode` (`0x455d6c`) relocates +0x88; textured face records
reference 8-byte UV pairs at +0x34/+0x38/+0x3c, and the environment routine writes
through those pointers. `render_layout_smoke.py` validates **18,969 corner UV
references against 19,398 UV records in each of two retail captures**. In 729
nodes the UV array starts immediately after the vertex array. That adjacency
explains the old `vertices_end` label; it is not the pointer's type or purpose.
The retail header now names these fields `uv_count` and `uvs` (`MDL_UV *`).

The demo structure is 0xdc bytes, with virtual-vertex count/pointer at +0xd4/+0xd8.
Both words are zero in all 735 nodes in each examined retail capture. The
retail relocator does not relocate +0xd8, and `XForm_Free_Verts_`'s retail
counterpart (`0x478dac`) walks flagged entries in the normal +0x80 vertex pool.
Thus the production adapter keeps its **0xd4-byte consumed prefix**. This is
not an assertion that all retail objects are allocated as 0xd4 bytes, nor proof
that the extra fields are inactive in every asset. No extra pool is invented.

The demo's 0x94-byte `_light` agrees with the inspected retail offsets:
flags/type +0, source position +4, source orientation +0x10, transformed
position +0x34, transformed matrix +0x40, temporary owner-local position
+0x64, owner-local direction +0x70, near/far +0x88/+0x8c and intensity +0x90.
The original field names do not independently prove coordinate space: the
writers and arithmetic establish the source/view/owner-local distinction.
The +0x7c direction field is not newly consumed by the adapter.

## Type-2 flat-light implementation

The shared kernel now accepts mixed radial (1) and oriented (2) lights.
For type 2, it transforms the orientation's third column by the owner's
rotation transpose, computes the Q15 normal dot, multiplies by intensity
with 32-bit wrapping, divides by 32768 with truncation toward zero, then
applies the recovered radial-range attenuation. The attenuation denominator
remains `far`, not `far-near`. Only negative contributions accumulate and
the sum clamps at -31 before storing the positive shade byte. Type 1 keeps
its original plane/distance calculation. No standard spotlight cone is added.

The adapter captures original source orientations already present in WDS6,
prepares local directions, and returns +0x70 direction writes alongside the
existing +0x64 position, normal-dot and face-shade feedback. Source shades
remain authoritative for culled list heads and allocation reuse. The helper
is now named `prepare_flat_lighting`; inactive/unsupported light types and
bindings outside the refreshed prefix still fail explicitly.

New original-x86 comparisons pass **1,063 mixed flat-light cases** (both shade
and normal-dot output) and **400 owner-local direction cases**, including
negative truncation boundaries, range edges, up to eight lights, rotations
and non-unit matrices. The existing 1,033 radial-shade and 500 local-position
cases continue to pass. GPU adapter tests check type-2 palette output and
direction metadata, and retain rejection of unsupported light type 3.

A controlled audit-build run binds a type-2 light to a 688-face node, changes
its position/orientation, unbinds, and resumes. It changes 42 face shades and
48 normal-dot values after movement/rotation, with zero routine readbacks.
This is deliberately injected source state, not a natural gameplay effect.
The first attempt exposed a test teardown race: clearing the source slot while
a frame held its binding. The harness now keeps that slot valid through child
termination. A separate error-reporting fix binds the error string by reference
instead of taking `c_str()` before a call that can invalidate its storage.

Full fixed-point camera-chain feedback equivalence, lit end callbacks,
near-clipped temporary faces, inactive slots and natural effect routes remain
partial. CPU float world preparation is the selected visual policy; the
isolated local-vector oracle is not proof of every composed-camera case.

## Next unresolved contracts

- Gouraud: retail types 0x16/0x17/0x19/0x1a use vertex-normal arrays and per-corner
  shade bytes (+0x41..+0x43), not the new flat kernel alone. Recover interpolation,
  palette selection and per-light accumulation with separate retail oracles.
- Environment mapping: capture original corner normals and UV identities, draw
  using the current UV version, then update the next version in original order.
  Shared UV pointers, culled faces and repeated/offscreen passes need tests.
- Mirror setup: trace the named `Build_Obj_Miror_`/`Update_Miror_` contracts
  independently before implementing reflection behavior.

## Reproduction

```powershell
uv run python recomp/windream/debug/direct_render_validate.py --gpu
uv run --with unicorn python recomp/windream/debug/render_oriented_light_smoke.py
uv run --with unicorn python recomp/windream/debug/render_light_smoke.py
uv run --with unicorn python recomp/windream/debug/render_layout_smoke.py out/scratch/retail-gdidream-222659.dmp out/scratch/retail-gdidream-223423.dmp
uv run --with unicorn python recomp/windream/debug/render_smoke.py --lifted out/scratch/retail-gdidream-222659.dmp out/scratch/retail-gdidream-223423.dmp
uv run --with unicorn python recomp/windream/debug/render_scene_smoke.py out/scratch/retail-gdidream-222659.dmp out/scratch/retail-gdidream-223423.dmp
uv run python recomp/windream/build.py --render-audit
uv run --with unicorn python recomp/windream/debug/render_light_live_smoke.py --oriented
```

Reports remain under `DREAMS_OUT/recomp/{debug-layout,oriented-lighting,lighting,
render-smoke,scene-adapter,windream/run-direct-oriented-light}`. Read-only
decompilations are `DREAMS_OUT/ghidra/cut/debug-contract-{retail,demo}.txt` and
`debug-layout-retail.txt`. No captured code or game data is committed.
