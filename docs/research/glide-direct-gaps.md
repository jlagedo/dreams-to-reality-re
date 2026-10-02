# Glide authority: gaps between DREAMSFX.EXE, the Windows build and the direct renderer

Read-only trace, 2026-10-01, for the
[owner decisions of 2026-10-01](../specs/006-recomp-glide-renderer/spec.md#owner-decisions--2026-10-01)
("Glide wins for 3D output", "implement every 3dfx feature"). It answers seven
questions and ends each with the change the direct renderer needs. It does not
repeat [glide-renderer.md](glide-renderer.md), [lighting.md](lighting.md) or
[debug-renderer-contracts.md](../specs/006-recomp-glide-renderer/debug-renderer-contracts.md).

Every claim is tagged:

- **[verified]**: read from instructions or decompilation in this pass, or
  counted from the disc data.
- **[inferred]**: follows from verified facts but was not read directly.

Addresses without a program name are `DREAMSFX.EXE` (image base 0). Windows
addresses are `WINDREAM.EXE`, the same bytes as `GDIDREAM.EXE`. "Direct" is
`recomp/windream/host/render/` plus `recomp/render/direct.*`.

## Summary

| # | Question | DOS 3dfx contract | Code needed |
|---|---|---|---|
| 1 | Corner shades of 0x16/0x17/0x18 | 0x16/0x17: Gouraud branch of `REND_LightObject`. 0x18: the **flat** branch, as in Windows; its corner bytes are never written and come from the stored face record | No lighting change. Close the ledger item with a fixture |
| 2 | Light types and modes | Same dispatch and formulas as Windows. Inactive slot: flat branch re-adds the previous contribution, Gouraud skips. Slot past the prefix: stale view-space transform | Yes: replace three explicit failures |
| 3 | Mirror | Five functions, all unreferenced in both builds; flag `0x1000` is in no shipped node | No renderer change; optional host twins of two UV writers |
| 4 | Deferred list overflow | `exit(-11)`: the process ends. Shipped worst case is tens of blocks | Counter and a decided overflow policy |
| 5 | Types Windows draws and Glide does not | Glide draws nothing for them. **DOS also retypes 39 named scene nodes to -7 or 9 at level load; Windows has the table but never calls it** | Yes: call the dormant Windows table |
| 6 | Shadows | Blob only (`OMBRE.3DC`, type -5). The real shadow cannot be enabled and would draw nothing | Yes, for the Windows real shadow: non-mask faces and lit shadow frames |
| 7 | Environment UVs and animated textures | Same arithmetic and order as Windows | No |

Two findings outside the seven questions, the palette row Glide binds for a
shade and the DOS defaults of the palette-row scales, are traced and
implemented in [section 8](#8-palette-rows-and-the-0xc8-levels), with the far
plane.

## 1. Corner shades of types 0x16, 0x17 and 0x18

### What DREAMSFX.EXE does

`REND_LightObject` (`0x93cf0`) is the only routine on the draw path that
writes face `+0x41..+0x43`. **[verified]** (`Inspect.java field:41/42/43`: the
other writers are the software rasterizers `0x7a2f4` and `0x7b6c0`, which the
3dfx build never installs as the hook; see question 5.)

It returns at once when node `+0xc4` (light count) is zero (`0x93d06`), so an
unlit node keeps every shade byte it had. **[verified]**

Its type dispatch (`0x93d24`–`0x93dbc`), read from the compare chain:
**[verified]**

| Block type | Branch | Writes |
|---|---|---|
| `1` | flat, 56-byte record | `+0x34` |
| `-15..-3`, `2`, `3`, `6`, `9`, `0xb`, `0xc`, `0x12`, `0x14`, `0x15`, **`0x18`**, `0x1c`, `0x1d` | flat (`0x9420c`) | `+0x40` |
| `0x16`, `0x17`, `0x19`, `0x1a` | Gouraud (`0x9463b`) | `+0x41..+0x43`, pool dots |
| `0x1e`, `0x1f` | specular Gouraud (`0x94f3d`) | `+0x41..+0x43`, pool dots |
| anything else, including `0x1b` and `0x13` | none | nothing |

The Windows switch at `0x47b81a` has exactly the same four sets. **[verified]**

**Types 0x16 and 0x17** (Gouraud branch). **[verified]**

1. For every face of the block's visible list (`block +0x24`, next at face
   `+0x04`) with flag bit 0 clear: `+0x41 = +0x42 = +0x43 = 0`.
2. For each bound light `i` in order (`node +0xc8 + i`), type read from
   `0x2c1170 + index*0x94`:
   - type 0 or type 3 and above: skipped entirely.
   - type 1: for every normal `n` of the node pool (`+0x8c` count, `+0x90`
     base, 16 bytes each): `n[3] = trunc((n.x*L.x + n.y*L.y + n.z*L.z) * 2^-15)`,
     `L` = light record `+0x64` (owner-local position). Then for each visible
     face and each corner `c` with vertex `v` (face `+0x08/+0x14/+0x20`, local
     ints at `v+4`) and normal `n` (face `+0x0c/+0x18/+0x24`):
     - `D = v - L`, `dist = sqrtf(D.x² + D.y² + D.z²)` stored as float32;
     - `pd = (n.x*v.x + n.y*v.y + n.z*v.z) >> 15` (32-bit wrapping products,
       arithmetic shift);
     - `k = trunc(intensity * (pd - n[3]) / dist)`, `intensity` = light `+0x90`,
       the product wrapping in 32 bits;
     - if `near < dist` (light `+0x88`): `k = 0` when `dist >= far` (`+0x8c`),
       else `k = trunc(k * (far - dist) / far)`;
     - `corner_byte += (uint8_t)max(-k, 0)` (byte add, wraps).
   - type 2: pool refresh uses the owner-local direction `A` at light `+0x70`
     instead of `L`. Per corner `k = (n[3] * intensity) / 32768` truncated
     toward zero, then the same range rule (distance still from `+0x64`) and
     the same byte add.

This is the contract the direct renderer already implements and checks against
Windows x86 (`render_gouraud_light_smoke.py`).

**Type 0x18.** The flat branch. Per visible face, per bound light:
**[verified]** (`0x9420c`–`0x9462b`)

- type 1: `N[3] = trunc((N·L) * 2^-15)` for the face normal `N` (face `+0x2c`);
  centroid `C = (v0 + v1 + v2) / 3` per component with truncating `IDIV`;
  `dist = sqrtf(|C - L|²)`; `k = trunc(intensity * (plane - N[3]) / dist)` with
  `plane` = face `+0x30`; same range rule.
- type 2: `N[3] = trunc((N·A) * 2^-15)`; `k = (N[3] * intensity) / 32768`
  toward zero; same range rule.
- `k = min(k, 0)`; `sum += k`. After the lights: `sum = max(sum, -31)`;
  `+0x40 = (uint8_t)(-sum)`.

It never touches `+0x41..+0x43`. For a 0x18 block those bytes are whatever the
68-byte face record holds: the bytes stored in the file (the loader relocates
records in place), or values left by an earlier lit pass if the block was 0x16
before. **[verified for "no writer"; the file origin is inferred]**

**Draw** (`GLIDE_DrawObjectFaces`, calls from `0x67da2` to `0x67fcc`), the
same code for all three types: **[verified]**

- `guColorCombineFunction(6)`, `grColorCombine(3,1,0,1,0)`: texture × iterated
  RGB; `grTexClampMode(0,1,1)`: clamp.
- Palette row = `(uint8_t)head_face[+0x40]` when node `+0xc4 >= 1`, else node
  `+0xd0`. `head_face` is `block +0x24`, the first record, drawable or not.
- Per corner `r = g = b = (float)(byte << 3)`, `a = 255`. The byte is **not**
  clamped to 31, so a multi-light sum can exceed 248.

### What the Windows build does

- `SW_DrawObjectFaces` (`0x473014`): types `0x16`/`0x19` with node light count
  zero are **rewritten to `0x18` in the block** and drawn by `BT_Linear_`; with
  lights they go to `BT_Linear_G_`. `0x17`/`0x1a` go to `BT_Perspective_G_`.
  `0x18` goes to `BT_Linear_`: affine texture with one shade per face, corner
  bytes ignored. **[verified]**
- So the builds differ in the **draw**, not in the lighting: Windows 0x18 is
  flat-shaded, Glide 0x18 is texture × stored corner greys.
- The 3dfx build never performs the `0x16 -> 0x18` rewrite (its hook is not
  the software one). An unlit 0x16 block stays 0x16 and is drawn with its
  retained corner bytes. **[verified]**

### What the direct renderer does today

`prepare_scene_lighting` (`render_scene_draw.cpp`) runs the Gouraud kernel for
lit `0x16`/`0x17`, the flat kernel for lit `0x18` (leaving
`result.corner_shades` at the captured source bytes), and nothing for unlit
nodes. `SceneDraw::submit` then sets `use_corner_brightness` for `0x16..0x18`
with `min(byte * 8, 255)` and takes the palette row from the head face's
`+0x40` (lit) or node `+0xd0` (unlit). That is the DOS contract above for all
three types. The ledger entry `glide-gray-0x18` ("lighting takes the Windows
flat branch") describes a difference that does not exist: DOS takes the same
flat branch.

### Required change in the direct renderer

**No change to the lighting or draw path.** Add a controlled fixture for a lit
0x18 block that checks (a) corner bytes are untouched, (b) `+0x40` is written
from the flat kernel, (c) the palette row follows the head face, and close
`glide-gray-0x18` in `windows-coverage.tsv` with this trace. Keep the
`min(byte*8, 255)` clamp in `SceneDraw::submit`; the unclamped Glide value is
listed under [Unverified](#unverified). In direct mode the software hook does
not run, so the Windows `0x16 -> 0x18` rewrite is already absent, as in DOS.

## 2. Light types and lighting modes

### Light records and management

Both builds: 100 records of `0x94` bytes (`0x2c1170` DOS, `0x672700` Windows),
type at `+0`, source position `+4`, source orientation `+0x10`, view position
`+0x34`, view matrix `+0x40`, owner-local position `+0x64`, owner-local
direction `+0x70`, near `+0x88`, far `+0x8c`, intensity `+0x90`. **[verified]**

| Routine | DOS | Windows | Behaviour |
|---|---|---|---|
| add | `0x90cb0` | `REND_AddLight` `0x477cb0` | Fails with -1 at 100 lights; takes the first slot whose type is 0; stores the caller's type unchecked; `intensity = arg % 32`; `count++` |
| remove | `0x90d74` | `REND_RemoveLight` `0x477d78` | `count--`; `type = 0` |
| bind | `0x90e20` | `Link_Light_To_Obj_` `0x477e14` | Appends the index to node `+0xc8`; silently ignored when node `+0xc4 == 8` |
| unbind | `0x90e48` | `Remove_Light_From_Obj_` `0x477e3c` | Replaces the entry with the last one; `+0xc4--` |
| to view space | `0x90ef4` | `REND_TransformLightsToView` `0x477ee0` | For slots `0 .. count-1` whose type is 1 or 2: `view_matrix = R_cam * orientation`, `view_pos = R_cam * pos + T_cam` |
| to owner space | `0x93a7c` | `REND_TransformLights` `0x47b3d0` | Per bound light: type 0 or >= 3 writes nothing; type 2 writes `+0x70`; types 1 and 2 write `+0x64` |

All **[verified]**. The count (`0x105000` DOS, `0x4ac758` Windows) is the
number of live lights, not the highest used slot, so after a non-LIFO removal
a live slot can sit at an index `>= count`.

**Light types the game creates.** Every creator passes type 1. DOS: `0x56224`
is the only caller of the add routine, itself called from `0x42d4c` and
`0x44ca2` (attack object spawn and tick) with `EDX = 1`, far `= 1000`.
Windows: `ENT_AddEffectLight` `0x42bcb9`, five call sites (`0x42eedc`,
`0x42eef0`, `0x42ef04`, `0x442db4`, `0x445186`), all `EDX = 1`. **[verified]**
Type 2 is handled by the lighting code in both builds but nothing creates it:
`Set_Light_Orient_` (`0x477dc4`) has no reference of any kind in either
Windows executable. **[verified]**

The DOS build has no counterpart of `SCENE_AddActorEffectLights`: its add
routine has one caller. Actor effect lights exist only in Windows. The game
state follows Windows; the direct renderer leaves those lights out of the
picture ([section 8](#8-palette-rows-and-the-0xc8-levels)). **[verified]**

### Per-face modes

The dispatch table of question 1 is the complete list. In both builds:

| Mode | Types | Status in direct |
|---|---|---|
| flat, `+0x34` | `1` | Not computed (type 1 is the diagnostic colour path; its shade is not read by the Glide draw) |
| flat, `+0x40` | `-15..-3`, `2`, `3`, `6`, `9`, `0xb`, `0xc`, `0x12`, `0x14`, `0x15`, `0x18`, `0x1c`, `0x1d` | Computed for the drawn types only (`textured_mode`) |
| Gouraud | `0x16`, `0x17`, `0x19`, `0x1a` | `0x16`/`0x17` only |
| specular Gouraud | `0x1e`, `0x1f` | Not implemented |

**Specular contract** (types `0x1e`/`0x1f`, `0x94f3d`–`0x95d27`; corner 0 read
from instructions, corners 1 and 2 from the decompilation, which repeats the
same code): **[verified]**

1. Clear the three corner bytes of every visible face.
2. Per bound light, type 0 or >= 3 skipped. Type 2 is the plain Gouraud type-2
   code (no specular term).
3. Type 1: refresh the node's normal-pool dots as in Gouraud. Compute the eye
   position in node space once per light, from the node's composed rotation
   `M` (`+0x58..+0x78`) and translation `T` (`+0x4c..+0x54`):
   `E.x = trunc(-(M[0]*T.x + M[3]*T.y + M[6]*T.z) * 2^-15)`,
   `E.y` with `M[1], M[4], M[7]`, `E.z` with `M[2], M[5], M[8]`
   (`M[k]` = dword at `+0x58 + 4k`).
4. Per visible face and corner (vertex `v`, normal `n`):
   - `D = v - L`; `s = (float)((D·n) * 2^-30)` (`0x763e0` returns the integer
     dot; its internal shift was not traced, see [Unverified](#unverified));
   - if `s >= 0`: `spec = 1.0`; else `m = trunc(n * s)` per component,
     `R = normalize(D - 2m)`, `V = normalize(E - v)`,
     `c = (R·V) * 2^-30`, `spec = c*c*c*c` (float32 products);
   - `dist`, `pd` and `k` exactly as in Gouraud type 1, then
     `k = trunc(k * spec)`, then the range rule, then
     `corner_byte += (uint8_t)max(-k, 0)`.

Glide draws neither `0x1e` nor `0x1f`, and Windows draws only `0x1e`
(`BT_Linear_G_`); see question 5.

### Inactive bound slot

A bound index whose record has type 0, or a type other than 1 and 2:

- **Transforms**: `0x93a7c` / `0x47b3d0` write nothing for it. **[verified]**
- **Flat branches** (`+0x34` and `+0x40`): the per-light contribution variable
  is **not reset**. The code falls through to `k = min(k, 0); sum += k` with
  `k` still holding the previous light's clamped contribution (DOS `0x945df`,
  Windows `0x47bd1f`). The variable lives for the whole call, so on the first
  light of a face it holds the last contribution of the previous face, and on
  the first face of the node it is an uninitialised stack slot. **[verified]**
- **Gouraud and specular branches**: the light is skipped; no pool refresh, no
  contribution. **[verified]**

### Binding beyond the refreshed prefix

`0x90ef4` / `0x477ee0` refresh `+0x34` and `+0x40` only for slots below the
live count. A live slot at or above it keeps the view-space position and matrix
of the last frame in which it was below the count. `REND_TransformLights` then
builds `+0x64`/`+0x70` from those stale values with this frame's node
transform, and the lighting proceeds normally. Nothing rejects the binding.
**[verified]**

Retail computes `local = R_ovᵀ * (view_pos - T_ov)` with the owner's composed
camera-space transform. With a stale `view_pos` this equals lighting the node
from the world point `C_now⁻¹ * stale_view_pos`, where `C_now` is this frame's
camera transform. **[inferred from the two transforms]**

### Required change in the direct renderer

In `prepare_scene_lighting` (`render_scene_draw.cpp`), replace three explicit
failures:

1. **"unsupported or inactive bound light"**: do not fail. Mark the light
   inactive: emit no `+0x64`/`+0x70` writes for it; in the Gouraud loop skip
   it (no pool refresh, no contribution). For the flat branch the shared
   kernel `od_flat_light_shade` (`recomp/render/direct.h`) must take a carry:
   per node, keep `carry` across faces and blocks in retail order; an inactive
   light adds `carry` to the sum; an active light sets
   `carry = min(contribution, 0)`. Initialise `carry` to 0 at node entry and
   record that as a declared deviation (retail reads an uninitialised slot).
2. **"bound light lies beyond refreshed light prefix"**: do not fail. In
   `capture_scene` (`render_scene.cpp`), for a needed slot with
   `index >= light_transform_count`, also read guest `+0x34..+0x3c` and
   `+0x40..+0x60`. In `prepare_scene_lighting`, replace that light's world
   position by `C_now⁻¹ * (+0x34)` and, for type 2, its world orientation by
   `R_nowᵀ * (+0x40)`, then run the existing path.
3. **Lit `0x19`/`0x1a`, `0x1e`/`0x1f`** (only if the owner decides they are
   drawn; see question 5): `0x19`/`0x1a` reuse the `0x16`/`0x17` branch
   unchanged; `0x1e`/`0x1f` need a new kernel in `direct.h` implementing the
   specular contract above, plus capture of the node's composed `+0x4c` and
   `+0x58` (or the eye recomputed from the float camera) for `E`.

Retail also writes shades for lit blocks that Glide never draws (type 1 at
`+0x34`, and the flat types outside `textured_mode`). Direct writes none of
them; nothing reads them, so this stays as it is unless question 5 changes
which types are drawn.

Fixtures: an 8-light node with slots 0, 3 and 7 inactive (flat and Gouraud);
a slot above the count after removing a lower one; original-x86 comparison in
the style of `render_gouraud_light_smoke.py` against Windows `0x47b7e0`, whose
branches are the same.

## 3. Mirror

### The functions, in both builds

| Original name | Windows | DOS | What it does |
|---|---|---|---|
| `Miror_Obj_(handle, on)` | `0x4574d0` | `0x71a0c` | Sets or clears node byte `+0x0d` bit `0x10` (dword flag `0x1000`) |
| `Get_Miror_Pos_(handle, in, out)` | `0x457d24` | `0x72350` | Reflects a point across the mirror node's plane |
| `Get_Miror_Matrix_(handle, in, out)` | `0x457e10` | `0x72414` | Reflects the three columns of a 3x3 matrix the same way and negates the first column |
| `Build_Obj_Miror_(node)` | `0x47e384` | `0x95ff4` | Writes projected-screen UVs for every visible face |
| `Update_Miror_(target, source)` | `0x47e824` | `0x965b4` | Writes six UV pairs of the target from four projected vertices of the source |

The DOS addresses were found by position in the same source modules
(`3DC_MEM.C`, `3DC_HIER.C`) and matching code; Ghidra has no function there
because nothing references them. **[verified]**

**`Get_Miror_Pos_`** (DOS `0x72350`): `N` = node dwords `+0x30, +0x3c, +0x48`
(third column of the local rotation, Q15), `O` = node `+0x1c..+0x24` (local
position). `D = O - P`; `d = (D·N) * 2^-30`; `m = trunc(N * d)`;
`out = O - (D - 2m)`, that is `P + 2 n̂ ((O - P)·n̂)`. **[verified]**

**`Build_Obj_Miror_`** (DOS `0x95ff4`, Windows `0x47e384`): for block types
`-15..-3`, `2`, `3`, `6`, `9`, `0xb`, `0xc`, `0x12`, `0x14..0x1a`,
`0x1c..0x1e`, each visible face, each corner with vertex `v` and UV pointer
`uv` (face `+0x34/+0x38/+0x3c`): **[verified]**

```text
uv[0] = trunc(v.screen_x * 255.0 * 65536.0 * 0.0015625)     ; 1/640
uv[1] = trunc(v.screen_y * 255.0 * 65536.0 * 0.0020833334)  ; 1/480
```

`screen_x`/`screen_y` are the integers at vertex `+0x1c`/`+0x20`. The four
constants are the same in both builds (`0xf4c74..0xf4c80`,
`0x4c6418..0x4c6424`). The result maps the 640x480 screen onto texels 0..255
of a page.

**`Update_Miror_`** (DOS `0x965b4`, Windows `0x47e824`): with `src` = source
node `+0x80` (vertex array, 40-byte records) and `dst` = target node `+0x88`
(UV array): the six UV pairs `dst[0..5]` are `(screen_x << 16, screen_y << 16)`
of source vertices 0, 2, 3, 0, 3, 1 in that order. Returns -1 when either
handle does not resolve. **[verified]**

**The flag.** `REND_DrawObject` calls the draw hook only when node `+0x0d`
bit `0x10` is clear (DOS test at `0x96309`, hook call at `0x96311`; Windows
`0x47e498` has the same test). Lighting and the environment update still run. That is the only place the flag is read on the
draw path. **[verified]**

### Callers

None, in either build:

- Ghidra references to all five entries: none in `DREAMSFX.EXE`, `WINDREAM.EXE`.
  **[verified]**
- Raw scan of both Windows executables (every section) for `CALL`/`JMP rel32`
  and for the absolute dword of `0x47e824`, `0x47e384`, `0x4574d0`, `0x457d24`,
  `0x457e10`: none. The same scan finds the six known callers of
  `MDL_ReplaceMaterial`, so it works. **[verified]**
- Raw scan of the `DREAMSFX.EXE` code object `0x20000..0xb8e1f` for the same
  two forms: none. The DOS data object was scanned from `0xf0000` up; the range
  `0xb9000..0xeffff` could not be read (see [Unverified](#unverified)).
- Neither build's `REND_DrawObject` calls `Build_Obj_Miror_`. **[verified]**

### Shipped content

No stored node has flag `0x1000`: the nodes of the 289 DSN/DAN files on both
discs carry only bits `0x10` and `0x20`, and the 165 nodes of the 16 `.3DC`
props carry none. The one setter is unreferenced. **Shipped content cannot reach the
mirror path.** **[verified]**

There is also no code in either build that renders the scene from a reflected
camera into a texture page; only the helpers above exist. The intended effect
(reflect the camera with `Get_Miror_*`, render, map the result in screen space
with `Build_Obj_Miror_` or `Update_Miror_`, hide the mirror with `Miror_Obj_`)
is **[inferred]** from the helpers and was never assembled in retail code.

### What the direct renderer does today

`capture_scene` already treats flag `0x1000` as "hook not called": the node is
not `submitted` but stays `visual_active`, so its lighting and environment
metadata are still produced (`render_scene.cpp`, the `SceneNode` initialiser).
That is the whole reachable contract.

`Build_Obj_Miror_` and `Update_Miror_` are in the lift (`bounds.csv`
`47e384,47e498` and `47e824,47e951`). Called in direct mode they would read
vertex `+0x1c`/`+0x20`, which the direct path never fills: `REND_DrawObject`
is replaced by the transform-only helper `0x47e634`.

### Required change in the direct renderer

**No change needed for shipped content or for the Glide draw**: the mirror has
no Glide code; it is five CPU helpers that write node flags and UVs, and the
flag is already honoured.

To make the two UV writers usable under direct rendering (the "implement every
3dfx feature" reading), add host replacements in `render_hooks.c` for
`0x47e384` and `0x47e824` that compute `screen_x`/`screen_y` from the adapter's
own projection in the 640x480 logical viewport and write the UV dwords through
the existing metadata-write path (`wd_render_write_arena_at`), with the
formulas above. Test with a fixture that sets flag `0x1000`, calls both, and
compares the UV dwords with the lifted functions run on a software-rendered
frame. Mark it "no shipped content". `Get_Miror_Pos_`/`Get_Miror_Matrix_` read
only local node fields and already work lifted.

## 4. Deferred translucent list overflow

### What DREAMSFX.EXE does

`GLIDE_DrawObjectFaces` at `0x67d70`: **[verified]**

```text
if (count < 256) { list[count] = block; count++; }   ; 0x2afed0, 0x2b0384
else exit_(-11);
```

`exit_` is the Watcom runtime exit: the process ends, inside the scene walk,
before any swap. No message is printed, nothing is dropped and no frame is
skipped: **the game quits to DOS with status -11**. **[verified for the call;
what the Voodoo shows afterwards is not established]**

What is counted: one entry per block of type `-7`, `-4` or `-3`, per node for
which the hook is called, per frame, whether or not any of its faces is
visible. The hook is called for a node that is not hidden, passes the sphere
cull and has flag `0x1000` clear. The count is reset to 0 at the end of
`GLIDE_DrawTranslucentFaces` (`0x6837c`), which both frame functions call, and
by `GLIDE_ResetTextureState` (`0x66c73`). **[verified]**

Windows has no list and no limit: translucent types are drawn inside the
software rasterizer.

### How close shipped content gets

Sources of deferred blocks: **[verified unless noted]**

| Source | Blocks |
|---|---|
| Types stored in scene files | 5 blocks in the whole corpus, at most 2 in one file (`M03DORM.DSN`) |
| DOS level-load retyping (question 5) | at most 29 in one scene (`E98ARAI1`, node `ARAI`); 15 in `E08_END`; 7 in `H18PUIT`; fewer elsewhere |
| `BOULE.3DC`, `MANA.3DC` (stored type -3) | 1 block per visible instance |
| Clone: `2 -> -3` on the copied actor model (DOS `0x5792b`, Windows `0x42c9e2`) | the model's type-2 blocks; the largest actor model has 63 blocks |
| Attack target: `2 -> -3` while flagged (DOS `0x42cb2`, restored at `0x447c9`; Windows `0x442ce4`/`0x444c45`) | the target model's type-2 blocks |

No single source reaches 256, and a scene would need several whole actor
models translucent at once on top of the largest retyped scene. **No shipped
route is shown to approach the limit; a runtime maximum was not measured.**
**[inferred from the static counts]**

### What the direct renderer does today

`SceneDraw::submit` collects translucent blocks in `transparent_order` with no
limit and draws them all.

### Required change in the direct renderer

In `SceneDraw::submit` (`render_scene_draw.cpp`), count deferred entries by the
DOS rule: one per captured block of type `-7`/`-4`/`-3` whose owner node is
`submitted`, including blocks with no visible face. Blocks reach `blocks[]`
only through their faces, so a block with zero records needs no entry; a block
whose faces are all culled is already present. When the count would pass 256,
apply the decided policy and log the count. The faithful result is process
exit with status -11; the alternative is to keep drawing and report. This is
an owner decision (see [Unverified](#unverified)); until it is made, log once
per level and keep drawing. Fixture: a packet with 257 single-face translucent
blocks, recorded as "no shipped content".

## 5. Face types the Windows dispatch draws and the Glide hook does not

### The two dispatches

Glide hook `0x67568`, read from the compare chain: draws `1`, `2`, `3`, `9`,
`0x16`, `0x17`, `0x18`, `-6`, `-5`; queues `-7`, `-4`, `-3`; every other value
falls through to the next block. **[verified]**

Windows `SW_DrawObjectFaces` `0x473014` (rasterizer names are the original
Watcom symbols): **[verified for the dispatch; the rasterizers' insides were
not traced]**

| Type | Windows rasterizer | Glide hook | In shipped data |
|---|---|---|---|
| `-15`, `-14`, `-11`, `-10`, `-8` | `BT_Linear_` (affine texture, one shade per face) | no draw | no |
| `-13` | `BT_Linear_TVAR_` | no draw | no |
| `-12`, `-9` | `BT_Perspective_` (perspective texture, one shade per face) | no draw | no |
| `-7`, `-4` | `BT_Perspective_` | deferred, alpha 128 | yes (244 and 172 faces) |
| `-6` | `BT_Perspective_` | chroma key | yes (506 faces; `OMBRE2.3DC`) |
| `-5` | `BT_Linear_` | chroma key | yes (props; set at run time for the gun) |
| `-3` | `BT_Linear_` | deferred, alpha 128 | yes (80 faces; `BOULE`, `MANA`; clones) |
| `-2` | `BT_Flat_` (solid colour) | no draw | no |
| `1` | `BT_Flat_` | constant colour, +`0x24bf9` per face | yes (1,873 faces, proxy boxes) |
| `2` | `BT_Linear_` | texture × iterated RGB at 255, clamp | yes |
| `3` | `BT_Perspective_` | same as 2 | yes |
| `4`, `5`, `7`, `8`, `10`, `0x11` | `BT_Flat_` | no draw | no |
| `6`, `0xb`, `0x12`, `0x15` | `BT_Linear_` | no draw | no |
| `9` | `BT_Perspective_` | decal, wrap | yes (5,146 faces) |
| `0xc`, `0x14`, `0x1d` | `BT_Perspective_` | no draw | no |
| `0x16` | `BT_Linear_G_` if the node has lights, else retyped to `0x18` and `BT_Linear_` | texture × corner grey, clamp | no |
| `0x17` | `BT_Perspective_G_` | texture × corner grey, clamp | no |
| `0x18` | `BT_Linear_` | texture × corner grey, clamp | no |
| `0x19` | as `0x16` | no draw | no |
| `0x1a` | `BT_Perspective_G_` | no draw | no |
| `0x1b` | `BT_Flat_` | no draw | run time only (Windows real shadow) |
| `0x1c` | `BT_Mapping_Flat_` | no draw | no |
| `0x1e` | `BT_Linear_G_` | no draw | no |
| `0x1f` | no draw (lit, never drawn) | no draw | no |
| `0`, `-1`, `0xd..0x10`, `0x13`, `>= 0x20`, `<= -16` | no draw | no draw | no |

"In shipped data" counts stored blocks in the 289 DSN/DAN files and 16 `.3DC`
props, plus the run-time setters named.

### Does the DOS build handle the others anywhere else?

No. **[verified]**

- `REND_DrawFrame` (`0x737e8`) and `REND_DrawFrameEx` (`0x738d0`) choose the
  hook from byte `0x10501c`: zero selects `GLIDE_DrawObjectFaces`; non-zero
  selects the collector `0x96a20` and restores the software hooks
  (`0x81258`, `0x813fc`) afterwards. `0x10501c` is zero in the image and has no
  writer, so the Glide hook is the only hook that ever runs and the software
  rasterizers still linked into `DREAMSFX.EXE` are never called.
- The second hook is set to `NULL` (`0x7385e`, `0x73949`), so the node's second
  block list at `+0xa8` is never drawn (Windows draws it post-order through
  `SW_DrawObjectFacesPost` `0x4731b8` and the `CK_*` rasterizers).
- The LFB (`grLfbLock`) is used only for 2D and movies; no 3D face goes there.

The DOS lighting and environment routines still accept the wider type set, so
a block of, say, type `0x1d` gets its shade and UVs updated and is then not
drawn.

### DOS retypes scene nodes at level load; Windows does not

`0x26e4c` is called at the top of the DOS `SCENE_FindSkyNode` (`0x27259`),
which `SCENE_LoadLevel` calls (`0x29531`). It compares the scene actor's name
(actor `+0x8c`) and, for a match, finds a node by exact name and sets **every
block** of that node (both lists) to a new type through `0x71c18`
(`Change_All_Classes_`). **[verified]**

Windows has the identical routine, `0x41ce67`, with the identical 39 rows, and
**no reference to it anywhere** in `WINDREAM.EXE` or `GDIDREAM.EXE` (Ghidra and
raw scan); the Windows `SCENE_FindSkyNode` (`0x41d2cf`) does not call it.
**[verified]**

| Scene (actor `+0x8c`) | Node | New type |
|---|---|---|
| `E02ARAI0` | `TUBECENTRE` | -7 |
| `E02ARAI0` | `B-CENTRE` | 9 |
| `E03ARAI1` | `!NRJ` | -7 |
| `E03ARAI1` | `!CENTRE` | 9 |
| `E04ARAI2` | `!NRJ` | -7 |
| `E05GAUDI`, `E05GAUD2` | `Para/01` .. `Para/04` | -7 |
| `E11_ANGK` | `@CONT_PISC`, `@SOL_PISC` | 9 |
| `E09COL` | `!Ciel` | 9 |
| `E25_RIDE` | whole model (handle actor `+0x74`) | 9 |
| `E08_END` | `Pt1` .. `Pt7` | -7 |
| `E98ARAI1` | `ARAI` | -7 |
| `E99ARAI2` | `!NRJ` (twice), `!H-CENTRE` | -7 |
| `F02_CASC` | `eaup` | -7 |
| `F05CAB` | `lampe` | -7 |
| `H18PUIT` | `H18_Oeil` | -7 |
| `M05AUTEL` | `m05mom01` .. `m05mom03` | -7 |
| `M07LACM` | `eau_d`, `eau_g`, `rocher01`, `rocher02` | -7 |
| `M08ENT` | whole model | 9 |

Every named node exists in the shipped scene of that name and is stored as
type 3 (opaque, clamp), except `M08ENT`, which already has four type-9 blocks.
**[verified against the disc files]** So in the 3dfx build these water
surfaces, energy beams, parachutes, the lamp and the eye are 50% translucent,
and the listed type-9 nodes repeat their texture; in the Windows build, and in
the direct renderer today, they are opaque and clamped. This is a 3dfx draw
behaviour that the Windows build dropped.

### What the direct renderer does today

`textured_mode` accepts exactly the Glide set and `SceneDraw::submit` fails
with "unimplemented scene face type" for any other type in a submitted node.
It reads block types from guest memory, so it shows the Windows (un-retyped)
scenes.

### Required change in the direct renderer

1. **Retyping (needed for Glide parity on shipped levels).** In
   `render_hooks.c`, add a replacement for `0x41d2cf` (`SCENE_FindSkyNode`)
   that saves `EAX` (the scene actor), calls the lifted `0x41ce67` with it,
   restores `EAX`, then runs the original body
   (`recomp_lookup_reference(0x41d2cf)`). `0x41ce67` is in the lift
   (`bounds.csv` `41ce67,41d2cf`), takes the actor in `EAX` and preserves the
   other registers. This reproduces the DOS call order exactly and needs no
   host table. It changes guest block types, which nothing but drawing and
   `MDL_ReplaceMaterial(…, 2, …)` reads; the retyped blocks are type 3, so
   that match is unaffected. Install it only when the direct renderer is
   selected, so the software reference keeps the Windows behaviour. Check
   with `game_nav.boot_into` on `E02ARAI0`, `F02_CASC` and `M07LACM`: the
   `[direct] source_mode=-7` line appears and the named nodes blend.
2. **Types Glide does not draw.** The faithful Glide result is no draw. In
   `SceneDraw::submit`, replace the "unimplemented scene face type" failure by
   a skip for the types in the table whose Glide column is "no draw", logging
   each new type once (the existing `source_mode=` line does that). Keep the
   failure for values outside the table. Whether any of them should instead be
   drawn as Windows draws them is the open owner question recorded in the
   spec; the table above is the evidence it asked for. No shipped block has
   any of these types.
3. `environment_type()` already matches the DOS set
   (`0x95d75`–`0x95ddb`): no change.

## 6. Shadows

### What DREAMSFX.EXE does

Shadow slots: 16 records of `0x48` bytes at `0x1ff4e0`; byte `+0` kind,
`+4` blob instance (`OMBRE.3DC`), `+8` projected instance (`OMBRE2.3DC`),
`+0xc` the shadowed model handle. **[verified]**

- `0x57144` (the DOS `ENT_AttachShadow`) is called from `PHYS_InitEntity` with
  kind 2 only when the actor is the player record **and** dword `0xf635c` is
  non-zero; otherwise kind 1. `0xf635c` is zero in the image and has **no
  writer** (two readers: `0x40b2b` and a menu draw routine at `0x5f504`). So
  every slot is kind 1. **[verified]**
- `ENT_UpdateShadow` (`0x57204`), kind 1: hide `+8`, show `+4`, place it 10
  units above the floor, orient it to the floor normal and heading, then run
  the UV callback `0x57530` over the blob's UV array (`0x77e38`). **[verified]**
- The blob is `OMBRE.3DC`: one node, 4 faces, block type **-5**, material
  `GRILLE`. Glide draws it as a chroma-keyed decal, wrap, vertex colour 255,
  palette row from node `+0xd0`. **[verified]**

So **the 3dfx build renders shadows as an ordinary keyed decal in the main
hook**. There is no shadow mask, no LFB access and no separate Glide path.

If kind 2 were forced, `0x5736c` would run the same sequence as Windows
(128x256 view, reparent the actor under the shadow camera, retype `2 -> 0x1b`,
`REND_DrawFrameEx`, restore). `REND_DrawFrameEx` clears the Glide back buffer
and depth, draws nothing for `0x1b`, draws any other block type of that
subtree into the back buffer, and never uses its destination argument for
pixels (it is only passed to the statistics overlay). The `OMBRE2` page is
never written. **[verified]** The real shadow is a Windows-only feature.

### What the Windows build does

`ENT_RenderShadowTexture` (`0x43eb67`), enabled by `0x4a3168` (toggled in
`MENU_HandleOptionsInput` and by a hotkey, read by `PHYS_InitEntity`
`0x43d7b3`): as described in
[implementation.md](../specs/006-recomp-glide-renderer/implementation.md#gpu-real-shadow-checkpoint).
`MDL_ReplaceMaterial` (`0x4577cc`) retypes only the blocks of the nodes that
belong to the resource of the given handle (`handle >> 16` selects the
resource; its node list is walked). **[verified]**

**"Shaded shadow callback"** is this repository's name for one case, not a
retail feature: in `wd_render_prepare_callback` (`render_live.cpp`), when the
frame callback pointer (`0x4aa704`) is non-zero and the frame is the shadow
frame, the adapter requires that no node of the shadow scene has a light bound
or flag `0x800`, and fails with "shaded shadow callback metadata is not
validated" otherwise.

- The frame callback is called after `REND_DrawScene` and before the node-box
  walk in both frame functions (Windows `0x459355`, `0x45938d`, `0x4593d6`,
  `0x459408`; DOS `0x7382b`, `0x73878`, `0x73919`, `0x73963`). **[verified]**
- **Nothing sets it.** Windows: `0x4aa704` is zero in the image; its only
  writer is `0x45842c`, which has no reference of any kind in either
  executable. DOS: `0x103cb0` is zero and has no writer. **[verified]** The
  callback case is reachable only by a test that pokes the pointer.
- The retail contract for the case is simply the normal one: the shadow
  frame's `REND_DrawScene` runs `REND_TransformLights`, `REND_LightObject` and
  the environment update for every visited node, exactly as in the main frame,
  before the callback. Type `0x1b` has no lighting branch, so the retyped
  blocks get no shade writes; blocks of other types in the subtree do.
  **[verified from the shared walker]**

### What the direct renderer still fails on

1. **The callback case above** (explicit failure, test-only route).
2. **Non-mask faces in the shadow scene.** `prepare_shadow_input`
   (`render_scene_draw.cpp`) fails with "shadow contains a non-mask material
   mode" for any submitted face whose type is not `0x1b`. An attached weapon
   is a separate resource (`Gun.3dc` is loaded on its own, retyped `2 -> -5`
   and attached to the hand node, DOS `0x41b1c`, Windows `0x441af4`), so
   `MDL_ReplaceMaterial(actor, 2, 0x1b)` does not retype it, and it is drawn in
   the shadow frame whenever it is shown. Nine of the 159 actor models also
   carry blocks that are not type 2 (seven with type 3, one with -5, one with
   9). **[inferred: the route was not run]** Windows rasterizes such faces
   into the 128x256 target with their normal textured rasterizers; what that
   leaves in the 8-bit `OMBRE2` page was not traced.
3. **Lighting metadata in the non-callback shadow frame.** `wd_render_scene`
   goes straight to `draw_shadow` and publishes no lighting writes. Retail
   writes flat shades for lit non-`0x1b` blocks during the shadow frame. A
   face that the shadow camera sees and the main camera culls keeps that
   shade into the main frame, and it selects the block's palette row when it
   is the head face. **[inferred from the culled-head rule]**

### Required change in the direct renderer

There is no DOS contract to follow for the real shadow; it is kept as a
Windows feature (spec scope). The blob shadow needs **no change**: type -5 is
drawn.

1. `wd_render_prepare_callback` and the shadow branch of `wd_render_scene`
   (`render_live.cpp`): build the shadow camera's view-projection
   (`SceneSnapshot::view_projection(128, 256, false, …)`), call
   `prepare_scene_lighting` with it and publish its writes, in both the
   callback and the non-callback path; remove the `none_of` requirement.
   `0x1b` is not in `textured_mode`, so retyped blocks produce no writes, as
   in retail.
2. `prepare_shadow_input`: stop failing on non-`0x1b` faces. The policy for
   those faces is an owner decision (see [Unverified](#unverified)); the two
   candidates are "leave them out of the mask" and "add them to the mask as
   index 1". Until decided, leave them out and log the type once.
3. Fixture: the recorded shadow capture with one actor node bound to a type-1
   light and with a shown type-2 child resource; live check with real shadows
   on (`--poke 0x4a3168=1`) while the player holds a weapon.

## 7. Environment mapping and texture animation in DOS

### Environment UVs

`REND_ComputeEnvMapUVs` DOS `0x95d54`, called from `REND_DrawObject` after the
hook when node `+0x0d` bit 8 is set (test `0x96317`, call `0x9631f`).
**[verified]**

- Type set: `-15..-3`, `2`, `3`, `6`, `9`, `0xb`, `0xc`, `0x12`, `0x14..0x1a`,
  `0x1c..0x1e`: the Windows set.
- Per visible face and corner, with `n` the corner normal and `P` the
  **parent's** composed rotation (`[node +0x10] +0x58..`):
  `u = (trunc((n.x*P[0] + n.y*P[1] + n.z*P[2]) * 2^-15) << 8) + 0x800000`,
  `v = (trunc((n.x*P[3] + n.y*P[4] + n.z*P[5]) * 2^-15) << 8) + 0x800000`.
  The third row is computed and discarded.
- Corner 0 multiplies the integer normal directly; corners 1 and 2 first
  store each normal component as float32. This is the same compiler artefact
  the Windows oracle records.
- Order: transform, cull, light transform, projection, lighting, hook,
  environment update, as in Windows. Deferred blocks hold pointers and
  therefore sample the UVs left after every object's update.

**Same computation, same order.** The flag setter `Env_Mapping_Obj_` (DOS
`0x719e4`, Windows `0x4574b4`) is unreferenced in both builds and no stored
node has flag `0x800`, so no shipped content reaches it. **[verified]**

### Animated textures

DOS marks a texture animated with `GLIDE_SetTextureAnimated` (`0x66be0`) from
three level-start routines: `SCENE_StartAnimTexture` (`0x3f290`, project
`+0x4c` material fed by the HNM4 of project `+0x5c`), `0x3f508` (project
`+0x6c`, slot flag `0x20`, page scroll) and `0x3f5bc` (project `+0x7c`, slot
flag `0x40`, page blend). The game-side work is the same as in Windows
(`SCENE_StartAnimTexture` `0x42dae2`, `SCENE_StartPageScroll` `0x42dea3`,
`SCENE_StartPageBlend` `0x42dfa0`); the Glide call is the only addition.
**[verified]**

`GLIDE_BindTexture` (`0x672a8`) re-uploads a texture when its animated field
(`descriptor +0x1c`) is 1 and its per-frame flag (`+0x20`) is 0, then sets the
flag; `GLIDE_Swap` (`0x674c0`) clears all 256 flags. The upload
(`GLIDE_UploadTexture` `0x66ac8`) point-samples texel `(2x, 2y)` of the
256x256 page into 128x128. So an animated page is uploaded **once per
presented frame, at its first bind**, from the page as it is then.
**[verified]** A texture that is not marked animated is never uploaded again,
even if its page changes.

Windows reads the page at raster time. The direct renderer keys each texture
on the captured page contents and palette, so a changed page produces a new
128x128 expansion on the frame it is captured: the same image as the DOS
first-bind upload, provided pages change in the tick and not during the draw.
**[inferred]**

### Required change in the direct renderer

**No change needed.** The environment arithmetic and timing are the Windows
ones already implemented, and the per-frame content-keyed upload reproduces the
DOS animated upload. The one DOS-only behaviour (unmarked pages never refresh)
has no shipped consumer: the only page changed outside those three effects is
the Windows real-shadow page, which DOS never writes.

## 8. Palette rows and the +0xc8 levels

Traced 2026-10-01 after `E29USINE` (projects 14, 71, 140) showed an evenly
lit level in the 3dfx build, a dark level with a lit patch around the player
in the Windows software build, and black walls in the direct renderer. The
3dfx captures referred to are the DOSBox Staging set under
`DREAMS_OUT/recomp/reference-3dfx/`.

### The chain, side by side

| Step | DOS 3dfx (`DREAMSFX.EXE`) | Windows (`WINDREAM.EXE`) |
|---|---|---|
| Add-clamp table | `0x2e952`: `table[(d << 8) \| c] = clamp(c + 2 * int8(d), 0, 255)` | `REND_InitAddClampTable` `0x4021e1`, the same table |
| Apply one row | `0x2f933`: offsets clamped to `[-127, 127]`, 256 BGR0 source colours, RGB565 into the high word. The RGB555 flag `0xf5998` is only ever written 0 | `REND_ApplyPaletteOffsets` `0x4031c3`, RGB555 when `0x49da18 == 1` |
| Source colours | slot `+0x20`, built from row 15 (`page - 0x4400`, `0x3f6dd`) | the same (`0x42e152`) |
| Scales | `SCENE_InitLevel` `0x28f09`: actor `0x19f710` and scene `0x19f718` start at **0**, replaced by project `+0xc0` / `+0xc4` when non-zero | `0x41f539`: start at **8** and **2**, same replacement (`0x6262e8`, `0x626300`) |
| Offsets | `0xf632c..0xf6334`: `-128` or `+128` at level load by project `+0x138`, then per tick `cur += (base + ((rand & 127) * var >> 7) - cur) >> 3` (`0x3fa84`) | `0x4a0fac..0x4a0fb4`, the same code (`0x41fcd8`, `0x42e5f2`) |
| Callers of the row update | level start `0x3f078`, tick `0x3fa84`, HNM4 palette chunk `0x3ffc0` | `0x42d824`, `0x42e5f2`, `0x42ed30`; a fourth, `0x42ec5b`, has no caller |
| Level start | `0x3f078` ends after `0x3f470` | `SCENE_InitPaletteLighting` also calls **`SCENE_AddActorEffectLights` `0x42ee5e`** |
| Effect-light flag | `0xfe790`: set by `0x56224`, whose two callers are the attack code | `0x4a0f64`: set by `ENT_AddEffectLight` `0x42bcb9`, called by the attack code (2 sites) and by `0x42ee5e` (3 sites) |
| Scene binding of an effect light | `0x56164`: nodes of actors with `+0xa9 & 2`, and the scene actor **only if `0x19f718 != 0`** | `0x42bb85`: the same test on `0x626300`, which is never 0 |
| Row bound for a block | `GLIDE_BindTexture` `0x672a8` → `GLIDE_ConvertPalette` `0x66cb0`: physical row = shade; converted only when the page differs from the last one bound (`0x102dc0`) | software rasterizer: row `31 - shade` ([lighting.md](lighting.md)) |

All **[verified]** from instructions, except the Windows row direction, which
is quoted.

`REND_UpdatePaletteRows` (DOS `0x3fca4`, Windows `0x42e8b1`), with `c` =
cursor + 1 (1..32), row `32 - c` at `page - c * 0x400`, and `step(n) = n *
scale >> 2`: **[verified]**

| Slot | Effect light | DOS writes | Windows writes |
|---|---|---|---|
| scene (flag `0x10` clear) | none | if scale ≠ 0: row `32-c` = offsets + `step(c)`. Then **row 15**: offsets, or with project `+0xc8` ≠ 0 **`(offsets + 12 * scale) >> 2`** per channel | if scale ≠ 0: the same rolling row. Then **row 16** = offsets. No `+0xc8` test |
| scene | present | nothing if scale = 0; else all 32 rows (`row 31-i` = offsets + `step(i)`) while loading or during the exit countdown, otherwise the rolling row | the same |
| actor (flag `0x10`) | none | rolling row from the actor's offsets (`+0x98..+0xa0`) and the actor scale, then **row 15** = actor offsets | the same with **row 16** |
| actor | present | all 32 rows or the rolling row, as for the scene | the same |

The rolling update and the full rebuild use different step indices for the
same row (`c` and `i = c - 1`), in both builds.

### What a block shows

- **Unlit block** (node `+0xc4 = 0`, `+0xd0 = 15` in every stored node).
  DOS binds row 15, Windows row 16. With no effect light both rows hold the
  source colours plus the current offsets, so the builds agree; that is every
  project whose `+0xc8` is 0. **[verified]**
- **Project `+0xc8` ≠ 0** (22 of 150 projects, among them `E29USINE` 14, 71,
  140). DOS creates no light and row 15 takes `(offsets + 12 * scale) >> 2`:
  with `E29USINE`'s settled offsets of -32 and scale 4 that is +4, where the
  plain rule gives -32. The level is evenly lit and mildly brightened.
  Windows instead creates a radial light on each actor with `+0xab & 8`
  (the player; outer radius project `+0xcc` or 2000), binds it to the scene,
  and from then on its scene slots take the effect-light branch: no row 16
  refresh, rows `31 - s` = offsets + `step(s + 1)`, so a face is darkest
  where the light does not reach. **[verified]**
- **Lit block in DOS** (attack lights only). The block binds row = head
  face shade `s`, whose contents are offsets + `step(32 - s)`: the ramp runs
  the other way from the Windows one, a brighter shade selecting a row with a
  smaller step. With the default scale 0 the scene is not bound at all and no
  scene row is rewritten while the light exists. **[verified from the code;
  not observed in a capture]**
- **A page no slot updates** (a material loaded after level start) keeps
  its file rows in both builds; DOS binds row 15 of them, Windows row 16.
  **[inferred]**

### The far plane

The black walls were a second, separate difference. `E29USINE` sets project
`+0xcc = 8125`, which both builds store as the camera far plane (`0x19f70c` →
`0x712e4`; `0x6262e4` → `0x456ccc`). `REND_CullObjectSphere` (Windows
`0x478980`) uses it for **whole nodes**: a bounding sphere entirely at or
beyond the far plane sets cull bit 8 (unless node flag `0x80`), and
`REND_DrawObject` then returns before lighting and the draw hook, except for a
flag-`0x10` node whose parent is not culled. **[verified]** Nothing clips a
face at the far plane: the 3dfx captures and the software build (with the
light's radius raised) both draw the far walls of a node that reaches across
it, and neither draws the nodes wholly behind it. **[verified by observation;
the DOS routine was not read]** The direct renderer ignored the cull bits and
let the GPU clip at the far plane, which removed the walls, the far flames and
the second spiral, and in other projects the sky and far terrain (45, 84, 31,
109).

### What the direct renderer did

`capture_scene` copied the 32 rows below the page from guest memory, the
Windows bank, and `SceneDraw::submit` bound row `31 - shade`. With the Windows
actor light stubbed out (an earlier attempt), that was row 16 with the plain
offsets: -32, where DOS has +4. That stub also removed three `rand_` calls per
tick (the light's jitter, `0x42ef2e` → `0x42bda3`), changing the shared random
stream.

### What it does now

1. **Host copy of the DOS rows.** `od_dos_palette_apply` and
   `od_dos_palette_rows` (`recomp/render/direct_math.cpp`) are ports of
   `0x2f933` and `0x3fca4`. The hook on `0x42e8b1` (`palette_rows`,
   `render_hooks.c`) passes the slot, the page (from the routine's own
   `MDL_FindMaterial` lookup) and the three offsets to
   `wd_render_palette_rows` (`render_live.cpp`) and then runs the lifted
   routine unchanged. The host keeps one 32-row bank per page, seeded from
   guest memory at the first call for that page (the file rows) and cleared at
   `SCENE_LoadLevel`. Scales and the `+0xc8` test read the project record;
   the cursor, the loading flag (`0x5df49c`) and the exit countdown
   (`0x5e5480`) read the Windows globals. Guest memory is not written.
2. **Row = shade.** `capture_scene` takes a page's rows from the host bank
   when there is one and marks the snapshot `dos_palette` (file magic `WDSC`);
   `SceneDraw::submit` then binds row = shade. A snapshot without the mark
   (older captures, dumps of the Windows build) keeps `31 - shade`.
3. **Lights the DOS build does not have.** `ENT_AddEffectLight` stores its
   owner in the effect-light slot (`0x6155f4 + i * 0x18`, owner at `+0x14`).
   The attack callers pass a record of the 16-entry attack pool
   (`0x630db8`, `0x2d0` bytes each: `ENT_SpawnAttackObject` walks it and
   the per-tick loop at `0x4070b5` steps through records of that size); `0x42ee5e` passes an
   actor of the list at `0x4fb728`. **[verified]** So a slot whose owner lies
   outside the attack pool holds an actor light. At capture the adapter drops
   those light indices from every node's list, and, when project `+0xc4` is 0
   and the scene actor lacks `+0xa9 & 2`, drops attack lights from the scene
   actor's nodes (the DOS binder's rule). A light that is in no effect slot
   (test fixtures) is kept. The same rule gives the DOS effect-light flag.
   `0x42ee5e` runs as lifted, so Windows creates, binds and moves its light,
   and the random stream is the Windows one.
4. **Far plane.** `capture_scene` applies the far part of
   `REND_CullObjectSphere` to each node from its composed transform
   (`+0x54`, `+0x70..+0x78`, sphere `+0xb0..+0xbc`) and
   `SceneSnapshot::view_projection` no longer clips at the project's far plane
   (it uses the game's default, `0xfffff`).

Guest-visible effects: for a node whose lights are dropped, the adapter writes
neither the flat or corner shades and normal dots nor the light's owner-local
scratch (`+0x64`, `+0x70`). The render path alone reads those
([engine.md](engine.md#what-the-game-reads-from-the-renderer-verified)).

### Evidence

- `render_dos_palette_smoke.py`: 4,000 random cases of `0x3fca4` replayed
  under Unicorn with the build's own `0x2f933` and table, every branch of the
  table above, 32 rows compared: all equal.
- `E29USINE` floor, mean RGB of the same screen region before gamma: 3dfx
  `(178.4, 145.6, 116.7)` once its offsets have settled, direct
  `(178.9, 146.2, 117.7)`; before the change direct had `(107, 75, 46)`.
- Mean colour of the 3D view against the 3dfx capture for the 17 projects
  listed as different: within 4 levels per channel after the change
  (`DREAMS_OUT/recomp/acc-logs/palette/`).
- Rand calls per 25 frames in project 14, direct against software from the
  same seed: the same count at 7 of 12 checkpoints and 3 apart at the others
  (one tick's group falling either side of the frame boundary), with no drift
  over 300 frames.

### Not established

- The DOS twin of `REND_CullObjectSphere` was not read; the far rule is the
  Windows routine plus the two observations above.
- Lit blocks under the DOS rows (attack lights in the 39 projects with
  `+0xc4` ≠ 0) follow the code; no 3dfx capture of an attack was compared.
- `MDL_BindActorPalette` in Windows initialises the actor offsets to ±64 and
  the DOS helper `0x3f3c0` does not. The host rows use the guest's values;
  the unlit actor rows agreed with the captures compared, and the difference
  was not traced further.
- The host bank is seeded at the first update of a page. A page that survives
  a level change without being reloaded would be seeded from rows the Windows
  routine already rewrote; row 15 is rewritten on the next update either way.

## Unverified

- **Palette row for a shade.** Resolved in
  [section 8](#8-palette-rows-and-the-0xc8-levels): Glide binds physical row =
  shade and the DOS build refreshes row 15 where Windows refreshes row 16, so
  for an unlit block the two agree whenever project `+0xc8` is 0. The direct
  renderer now keeps the DOS rows on the host and binds row = shade.
- **Palette-row scales.** Resolved in the same section: the DOS defaults are 0
  (Windows 8 and 2). With scale 0 the DOS build neither rewrites the lit rows
  nor binds an attack light to the scene, so the scales matter only in the 41
  projects that name one, and there the DOS rows are the ones now drawn. What
  remains open is listed under "Not established" there.
- **Overflow policy (question 4).** Whether the direct renderer should end the
  process at the 257th deferred block, as DOS does, or report and continue.
- **Non-mask faces in the real-shadow frame (question 6).** What the Windows
  textured rasterizers leave in the 8-bit `OMBRE2` page was not traced, and no
  DOS contract exists. The weapon route was derived from code, not run.
- **Types Glide does not draw (question 5).** The spec's open decision. The
  rasterizers behind the Windows column (`BT_Linear_TVAR_`, `BT_Mapping_Flat_`,
  the keyed and translucent handling of negative types) were identified by
  name and dispatch only.
- **Corner grey above 248.** Glide receives `byte << 3` as a float up to 2040
  when several lights sum past 31. What a Voodoo does with an iterated colour
  above 255 was not established; direct clamps to 255.
- **Uninitialised carry.** The first inactive bound light of a node's first
  lit face reads an uninitialised stack slot in both builds; its value was not
  determined.
- **`0x763e0`** (DOS integer dot product used by the specular branch and
  `Get_Miror_*`): its internal shift was not read. The specular formula is
  therefore exact in structure and constants (`2^-30` at `0xf4c64`, `2^-15` at
  `0xf4c5c`) but unchecked against x86.
- **DOS data range `0xb9000..0xeffff`** could not be read through Ghidra
  (uninitialised block), so a function-pointer table there that names a mirror
  routine is not excluded. The code object and `0xf0000..0x105fff` are clean.
- **Second block list at node `+0xa8`.** DOS never draws it (second hook
  `NULL`); whether any shipped node has a non-empty one was not counted.
  [scene-geometry.md](scene-geometry.md) describes `+0xa8` as the edge block,
  while `MDL_ReplaceMaterial` and the post hook walk it as a block list.
- **Near-clip temporary faces.** `REND_ClipFaceNear` (`0x9209c`) was not
  traced; how it fills corner shades of generated faces is unknown. The GPU
  clips and interpolates instead.
- **DOS `PHYS_InitEntity` versus Windows.** The Windows condition for a kind-2
  shadow (`0x43d7b3` and the three `ENT_AttachShadow` calls) was not decoded;
  "player only" is taken from the DOS twin.

## Reproduction

All calls are read-only and were run one at a time.

```sh
uv run python re/tools/ghidra_headless.py -process DREAMSFX.EXE -noanalysis -readOnly -postScript Decompile.java 00093cf0 00093a7c 00090ef4 00096178 000963fc 00095d54 000737e8 000738d0 00067568 00068128 00056f20 00057204
uv run python re/tools/ghidra_headless.py -process DREAMSFX.EXE -noanalysis -readOnly -postScript Inspect.java code:00093cf0 code:000737e8 code:000738d0 code:00095d54 code:00026e4c field:41 refs:00105004 refs:0010501c refs:00103cb0 refs:000f635c bytes:00095ff2-00096177 bytes:0009651f-000966ff
uv run python re/tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis -readOnly -postScript Decompile.java 0047b7e0 0047e824 0047e384 004574d0 00457d24 00457e10 0043eb67 0043e9aa 00473014 004731b8
uv run python re/tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis -readOnly -postScript Inspect.java code:0041ce67 code:0047e384 calls:004577cc calls:0042bcb9 refs:004aa704 refs:004a3168
```

Section 8 adds the Unicorn replay of the DOS palette routine (it fetches its
own `bytes:` dump on first use):

```sh
uv run python recomp/windream/verify/direct_render_validate.py
uv run --with unicorn python recomp/windream/verify/render_dos_palette_smoke.py
```

Unreferenced DOS code was disassembled with capstone from `bytes:` dumps, and
the raw reference scans used `pefile` on the two Windows executables and the
`bytes:` dump of `0x20000..0xb8fff` for `DREAMSFX.EXE`. Content counts come
from `DREAMS_OUT/recomp/render-content-inventory/inventory.json`
(`render_content_inventory.py`) and `dreams.formats.node` / `project` on disc 1.
