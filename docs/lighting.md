# Palette and object lighting

Recovered from WINDREAM, with DREAMSFX cross-checks, 2026-09-26. Independent
review and naming provenance: [renderer-gaps review](../re/reviews/renderer-gaps-20260926.md).
Glide submission/sampling is specified in [glide-renderer.md](glide-renderer.md).

## Project fields and palette state

Project `+0x18` and `+0x24` are **signed RGB base and variation triplets**,
not spatial light directions. `REND_TickPaletteLighting` (`0x42e5f2`) consumes
three calls to the original `rand_` and, per channel, computes:

```text
target = base + ((rand() & 127) * variation >> 7)
current += (target - current) >> 3
```

The underwater/effect branch adds another bias triplet and range term; it
still consumes exactly three draws. `0x42f024`, called by `GAME_Tick` before
the frame draw, dispatches this update when `0x4a0f74` is nonzero. That flag
starts at 1 and has no static writer. **These visual changes advance the same
RNG stream as AI and gameplay.** Keep their calls and ordering on the simulation
clock even when palette application moves to the renderer.

| Project field | Recovered use |
|---|---|
| `+0x18`, `+0x24` | RGB base and signed variation for the formula above |
| `+0x30` | RGB baseline for actor color adjustment |
| `+0xc0`, `+0xc4` | Actor-bound and other material palette-row scales; defaults 8 and 2 |
| `+0xc8` | Enables actor effect lights through actor `+0xab & 8` |
| `+0xcc` | Player effect-light outer radius when nonzero, otherwise 2000; also used as the camera far-plane override |
| `+0xe0..+0xec` | Additional palette-effect bias/range inputs; the legacy `fog` label is misleading |
| `+0xf0..+0xf8`, `+0xfc` | Target RGB triplet and duration used by palette interpolation initialization |
| `+0x138` | Exact mode 0/1 branches; keep the raw integer, including 16 |
| `+0x1e8`, `+0x1ec` | Optional contrast strength and material-prefix filter |

The 150 project records contain modes `{0: 70, 1: 78, 16: 2}`; mode 16 is
P0/P108. `SCENE_LoadLevel` writes global biases +128/+64 for mode 0 and
-128/-64 for mode 1; other modes bypass these assignments and the matching
transition interpolation. Actor palette binding separately treats every mode
except 1 as the +64 case. This branch behavior is known; mode 16's author
intent is not needed to preserve it and must not be invented as a day/night label.

## Texture-bank preparation

`DSN_Create3DM` (`0x417a07`) copies a 20-byte scene template, clears 0x8000
palette bytes and 0x10000 page bytes. `DSN_LoadTextures` (`0x417afd`) copies
tag 3 into **row 15**, then writes two tag-4 planes per call for 32 calls.
`.DAN` and `.3DM` already carry all 32 rows. Page address is bank `+0x8014`;
each palette entry is a zero u16 followed by the packed color.

`MDL_RegisterLightingMaterial` (`0x42d96d`) registers a material in a
64-entry table at `0x615ad8`, stride 0x420. Each record has a name at +8,
flags at +0x18, an actor pointer at +0x1c and 256 BGR0 source colors at +0x20.
`MDL_BuildPaletteColorTable` (`0x42e126`) reads row 15 at `page-0x4400` and
expands RGB565 using R/B ×8 and G ×4. Optional contrast is:

```text
channel = clamp(127 + trunc_toward_zero((channel - 127) * K / 64), 0, 255)
```

K is project `+0x1e8`. When `+0x1ec` is nonzero, a material-name prefix test
gates this adjustment; the complete intended filtering convention remains open.

`MDL_BindActorPalette` (`0x42dc42`) matches the actor name at +0x8c to material
names, sets slot flag 0x10 and writes the actor pointer. In WINDREAM it also
initializes actor RGB offsets at +0x98/+0x9c/+0xa0 to -64 in mode 1 or +64
otherwise, and fallback color +0xa4 to 0 or 0xffff. The paired DREAMSFX helper
`0x3f3c0` attaches the pointer/flag without these extra initialization stores.

`REND_UpdatePaletteRows` (`0x42e8b1`) uses those actor RGB offsets for flag-0x10
slots and the current global RGB state for other slots. Both paths push the
actual channel adjustments onto `REND_ApplyPaletteOffsets` (`0x4031c3`)'s
stack; missing decompiler arguments previously hid this connection.

- Row address is `page - (i+1)*0x400`, so i=0 targets row 31.
- During a full 32-row rebuild, adjustment is `RGB + ((i*scale)>>2)`.
- The rolling single-row update uses cursor+1 for **both** the address and
  step: `RGB + (((cursor+1)*scale)>>2)`. Preserve this difference.
- Selected paths additionally refresh row 16 at `page-0x4000` with unstepped RGB.

`REND_ApplyPaletteOffsets` clamps deltas to [-127,127] and looks up
`clamp(channel + 2*delta, 0, 255)` in the 64 KB table initialized by
`REND_InitAddClampTable` (`0x4021e1`). It packs RGB555 when `0x49da18 == 1`,
otherwise RGB565, into the high word of each output entry. Actual palette
updates are conditional on effect-light state, loading and transition state;
do not replace the original selective updates with a timeless generic ramp.

## Actor adjustment and the renderer boundary

`ENT_AdaptActorColor` (`0x41c0a6`), reached through `0x42dd54`, projects points
near eligible actors into a 640×400 sampling region, locks the surface and
calls the nominal pixel helper. **That helper at `0x4020a8` and the debug
marker at `0x4020ce` are one-byte RET stubs in WINDREAM.** The helper preserves
the caller's register value, which is then treated as RGB565. A real pixel
load or runtime replacement has not been established; an additional framebuffer
feedback dependency must not be inferred from the caller's apparent intent.

The resulting actor RGB offsets are read by the actor-bound palette path.
Keep the confirmed simulation-visible camera-space position dependency from
[engine.md](engine.md#what-the-game-reads-from-the-renderer-verified). Separately,
preserve the palette update's shared RNG draws and timing. No new GPU-to-game
pixel-readback requirement is imposed by the static evidence here.

## Light records and object shading

`REND_AddLight` (`0x477cb0`) allocates among 100 records of 0x94 bytes at
`0x672700`; `REND_RemoveLight` (`0x477d78`) clears the type, and
`REND_MoveLight` (`0x477da0`) copies a new position. Type 1 is position-only
with radial attenuation. Type 2 adds a transformed orientation axis; the
previous “1 directional, 2 point” labels are withdrawn.

`SCENE_AddActorEffectLights` (`0x42ee5e`) creates type-1 lights for actors
with +0xab bit 8: inner radius 0, outer radius 2000 (player override at project
+0xcc), intensity 31. The static caller search did not find a type-2 creator.
Type 2's precise angular-cutoff convention remains open.

`REND_TransformLightsToView` (`0x477ee0`) and `REND_TransformLights` (`0x47b3d0`)
bring indexed lights into camera and node space. `REND_LightObject` (`0x47b7e0`)
uses face/corner normals, full strength inside the inner radius, zero at/after
the outer radius and attenuation between them. Flat contributions accumulate
negatively, clamp at -31 and are negated into face shade; Gouraud paths write
the corner shade bytes. Software selects row `31-shade`; Glide uses the
separate palette/iterated-RGB contract. Project RGB triplets are not direction
vectors passed to this normal calculation.

The direct-renderer oracle now verifies the radial flat branch against original
Windows x86 in 1,033 cases. It uses integer centroid division, truncated normal
dot/distance quotients and wrapped 32-bit products/accumulation. Outside the inner
radius, the multiplier is `(outer-distance)/outer`, **not** a normalization by
`outer-inner`; the discontinuity at a nonzero inner radius is observable. At a
zero-length vector, x87 integer-indefinite conversion and accumulation wrapping
also affect the final byte. Another 500 cases verify that the light-to-node step
uses the rotation **transpose**, including non-unit matrices, rather than a
general matrix inverse. The recomp adapter now uses these kernels for supported
radial-lit textured nodes, including shade/normal-dot feedback and stale-head
palette selection. The demo-informed follow-up adds type-2 flat lighting:
1,063 mixed shade/normal cases and 400 direction-transform cases match retail
x86, and controlled live rotation/unbinding passes. Gouraud and full camera-chain
feedback remain partial. See [the contract and evidence](specs/006-recomp-glide-renderer/debug-renderer-contracts.md).

The shade used for a lit Glide block comes from the **head of its face list**.
That entry may have the culled flag set: `REND_LightObject` skips it and retains
its earlier shade. Original-code replays show a culled head retaining 19 while
the next drawable face becomes 31, and `GLIDE_DrawObjectFaces` requesting row 19.
Do not replace this with the first newly shaded/drawable face. See the current
[lighting implementation evidence](specs/006-recomp-glide-renderer/implementation.md#radial-lighting-kernels-and-capture).

## Validation

Checked all 150 raw project records; 95 scene palettes (527,104 RGB565 entries);
261 texture banks in 159 models; and four unique `.3DM` banks. Palette math
was checked across all 65,280 channel/delta combinations for [-127,127].
These checks validate data and arithmetic, not original-game screenshot parity.
