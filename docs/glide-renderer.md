# Glide renderer contract

Recovered from `DREAMSFX.EXE`, reviewed 2026-09-26. The docs-aware worker pass
and independent stripped-code review are recorded in
[re/reviews/renderer-gaps-20260926.md](../re/reviews/renderer-gaps-20260926.md).
This specifies the GPU port's faithful defaults; screenshot parity is not yet
tested. The software rasterizers are not the appearance specification.

## Submission and face types

`GLIDE_DrawObjectFaces` (`0x67568`) receives the already transformed node and
walks its block list at `+0xa4`. Block `+0x04` is the signed type, `+0x08` the
material binding, `+0x0c` its name. Each visible face has **three** vertex
pointers and three UV pointers. `grDrawTriangle` is used when the combined
vertex outcodes masked by `0x3f` are zero; otherwise `guDrawTriangleWithClip`.
There is no runtime quad split to recover: preserve the stored triangles.
`CARRE.3DC`, for example, already consists of two triangles.

| Type | Final color / sampling state | Static corpus use |
|---|---|---|
| `-7`, `-4`, `-3` | Deferred decal texture, wrap, alpha 128 | DSN and props |
| `-6`, `-5` | Decal texture, wrap, chroma key enabled around the block | DSN, models and props |
| `1` | Constant-color diagnostic triangles; starts at zero and increments color by `0x24bf9` per drawn face | Not observed |
| `2`, `3` | Texture × iterated RGB, RGB fixed at 255, clamp in both axes | Models and levels |
| `9` | Decal texture, wrap in both axes | Nine levels and `L14.DAN` |
| `0x16`–`0x18` | Texture × iterated grayscale, each corner shade byte at `+0x41..+0x43` shifted left 3, clamp | No static blocks observed |
| Other types | No draw in this hook | No other types in the checked corpus |

Clamp is set per **block**, not per triangle. Types 2/3 first set wrap but
overwrite it with clamp before submission. Unlike the other drawn paths,
types 2/3 do **not** initialize the three vertex alpha fields; their selected
RGB combiner does not use those fields. Do not infer alpha 255 from the RGB
assignments or claim fidelity for the unspecified alpha-channel result.

Coordinates are `oow = 1/z`, `ooz = oow * 128 * 65536`,
`sow = (signed_u / 65536) * oow`, and similarly for V. The source page is
256×256; the UV values can lie outside it. Preserve them before interpolation
and let the selected sampler clamp or wrap. In particular `F37.DAN` and
`H14.DAN` use type 2, while `L14.DAN` uses type 9. Their outlying coordinates
are valid records, not broken UV pointers.

The checked scene graph contains 157,433 triangles: 152,149 type 3, 4,282 type
9, 506 type -6, 80 type -3, 244 type -7 and 172 type -4. Every one of the 95
scenes contains type 3, but twelve also contain other types. The older claim
that all level blocks are type 3 is withdrawn.

## Deferred transparency

The main hook appends types -7/-4/-3 to a 256-block array at `0x2afed0`, count
`0x2b0384`; overflow exits with -11. `GLIDE_DrawTranslucentFaces` (`0x68128`)
walks that array in insertion order, then each visible-face list. There is no
depth sort. Both frame functions (`0x737e8`, `0x738d0`) call it after scene
traversal and node-box maintenance.

The pass selects decal texturing, wrap, iterated alpha, vertex alpha 128 and
RGB blend `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`; alpha blend is `ONE / ZERO`.
It binds palette row 15 and restores `ONE / ZERO` blend factors after each
block. **Depth testing and depth writes remain enabled.** The unsorted order
and retained writes are part of the original behavior.

## Textures and palette cache

The material loader at `0x7040c` selects P8 (`format = 5`), square aspect,
and a single 128×128 LOD. `GLIDE_AllocTexture` (`0x66d58`) allocates TMU memory
and samples every second texel from the source 256×256 page for that upload.
`GLIDE_UploadTexture` (`0x66ac8`) refreshes a slot; `GLIDE_UploadAllTextures`
(`0x66f24`) visits populated slots after level loading. This model-material
LOD selection should not be generalized to every scene/video descriptor
without checking its stored `GrTexInfo`.

`GLIDE_BindTexture` (`0x672a8`) binds the descriptor and, for P8, downloads a
palette when the **page pointer** changes. `GLIDE_ConvertPalette` (`0x66cb0`)
reads 256 entries at `page - 0x8000 + row*0x400`, taking the high RGB565 word
of each four-byte slot. Expansion is R/B ×8 and G ×4, without bit replication;
the output alpha byte is zero. Palette entry zero supplies the chroma-key
color. Ordinary blocks pass node `+0xd0`, or the first visible face's `+0x40`
when the node has lights. The deferred pass passes 15.

The cache key **does not include the row**. A new shade on the same page does
not itself trigger a palette download. `GLIDE_ResetTextureState` (`0x66c54`)
clears the cache and allocation state; `GLIDE_Swap` (`0x674c0`) clears only
per-slot update flags. Preserve this behavior in the reference path before
considering a cache fix. Dynamic shade/page sharing still needs runtime QA.

## Device state and fog

- `GLIDE_Open` (`0x670f0`): SST0, 640×480, 60 Hz, ARGB, upper-left origin,
  two color buffers and one auxiliary buffer; bilinear min/mag, gamma 0.8.
- `GLIDE_SetDepthWrite` (`0x67500`): Z buffer, `GREATER`; argument 1 enables
  writes and other values disable writes. Both branches retain depth testing
  and clear. `GLIDE_ClearToBackground` (`0x67490`) clears color/alpha/depth
  to zero: its color global `0x102dc8` is initialized zero with no static writer.
- `GLIDE_LockBackBuffer` / `GLIDE_UnlockBackBuffer` (`0x68028` / `0x6805c`):
  write-only back-buffer LFB access, RGB565, upper-left origin.
- `GLIDE_SetFog` (`0x671d0`): table fog, packed color, a 64-entry exponential
  table at `0x2ada90`. The density is a stack argument omitted from the C
  decompiler's displayed prototype; the following rules come from instructions.

`SCENE_SetFog` (`0x29288`) has three branches, using the current project record:

| Condition | Color | Density |
|---|---|---|
| `+0x1cc != 0` | RGB from `+0x1c0/+0x1c4/+0x1c8` | signed integer `+0x1cc * 0.00000625` |
| Otherwise, water height `+0xd4 != 0` and player mode is 2 | `0x006080` | `sin(phase) * 0.00004 + 0.00018` |
| Otherwise | 0 | 0 |

The water branch adds `delta * 0.02` to phase, resetting values above pi to
the float -pi. `0x159e20` is the frame delta in 30 Hz units, confirmed from
the 200/elapsed → 30/fps calculation and 0.2–5 clamps at `0x218d0..0x21985`.
Fog is set after load and on player water-mode transitions through `0x2c904`;
the latter checks actor `+0xa8 & 4`. This is not an unconditional per-frame
fog animation, despite the sine expression.

The zero-density branch still selects table fog. The reference Glide utility
normalizes by `1-exp(-density*wmax)`, which is zero at density zero. Actual
Glide/wrapper behavior must be observed before calling this branch “fog off.”

## Validation and limits

API enums were checked against local Glide 2 SST1 headers/source, revision
`2f226f0f9225ce8ee83e6a4a7042981e719d19ee`. The independent corpus passes agree
on types and signed UV records. The Python reader now retains the type and
signed UVs; glTF writes the type in material extras and the verified samplers.
These inspection exports do not emulate the whole contract: deferred ordering,
depth writes under blending, chroma-key palettes, 128×128 uploads, palette
animation, gamma and fog still belong to the native renderer.
