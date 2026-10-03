# Guardian's Direct3D renderer against our direct renderer

The Direct3D 6 backend of *The Guardian of Darkness* (`game.d3d.exe`, 1999)
compared with the Dreams 3dfx contract (`docs/research/glide-renderer.md`) and
our direct renderer (`recomp/render/direct.*`,
`recomp/windream/host/render/render_*`). Read from the code; nothing here has
been run.

## How it was read

- **Junction.** `REND_DrawFrameEx` `0x42f57c` sets the face hook to
  `0x40d564`, runs `REND_DrawScene`, then the deferred pass `0x40e0cc`
  (README, "Where Direct3D joins the engine").
- **Ghidra.** `MakeFunction.java` (`re/guardian/ghidra_scripts/`) created
  `0x40d564`, which analysis had left as a label. Decompiled output:
  `out/guardian/render-decomp.c`.
- **Direct3D calls** were decoded by `re/guardian/tools/d3dcalls.py`
  (`out/guardian/d3dcalls.txt`).
  - It takes the `IDirect3DDevice3` method order and every enum value from
    the Windows SDK `d3d.h`/`d3dtypes.h`.
  - The device is inferred from the build querying `IID_IDirect3D3` and from
    offsets `0x24/0x28/0x58/0x70/0x98/0xa0` falling on `BeginScene`,
    `EndScene`, `SetRenderState`, `DrawPrimitive`, `SetTexture` and
    `SetTextureStageState`.
- **Face types in the data:** `re/guardian/tools/face_types.py` over the 1,258
  decodable `.3dc` files:

| Type | Faces | Files |
|---:|---:|---:|
| 3 | 174,860 | 849 |
| 2 | 33,409 | 219 |
| -3 | 10,372 | 56 |
| -21 | 8,586 | 132 |
| -8 | 156 | 11 |
| -6 | 133 | 2 |
| -4 | 12 | 1 |
| -5 | 10 | 1 |

No 1, 9, -7 or 0x16-0x18 faces occur.

## Device defaults

Set once by `0x4322f0`:

- `ZENABLE` and `ZWRITEENABLE` on;
- `FOGENABLE` off;
- `CULLMODE` none: the engine culls faces itself before the hook;
- Gouraud shading;
- dithering on, perspective correction on;
- bilinear `MAGFILTER`/`MINFILTER`;
- alpha blending off.

Two settings are never made anywhere in the executable:

- **Fog.** Fog is never enabled (whole-image scan of every `SetRenderState`).
- **Texture address modes.** No `ADDRESS`, `WRAP` or `ZFUNC` state is set, so
  Direct3D's defaults apply: wrap, and `LESSEQUAL`.
- The options menu toggles only filtering (`0x40d318`, linear or point) and
  dithering (`0x40d370`).

## Vertices

Every face is sent as `D3DTLVERTEX` (FVF `0x1c4`): pre-transformed and lit,
three vertices per face, `D3DPT_TRIANGLELIST`.

| Field | Guardian | Our renderer |
|---|---|---|
| position | screen x, y from the engine's projected vertex `+0x1c/+0x20` | object-space vertices, projected on the GPU |
| depth | `rhw = 1/(0.1*z)`, `z = 1 - rhw` (`0x4bacb4` = 0.1) | reversed Z, near 1, far 0 |
| UV | `signed * 2^-24` (`0x4bacb0`), the 256-wide page | `signed/(65536*256)`: same |
| colour | always white: `0xffffffff` opaque, `0x80ffffff` translucent, `0x00ffffff` other deferred types | brightness 1.0 except 0x16-0x18 |
| lighting | palette rows, one texture per (material, row) | palette rows, one texture per row: same |

Palette row selection is the same rule as ours:

- face shade byte `+0x40` of the block's first face when node `+0xc4 >= 1`;
- otherwise node `+0xd0` (`0x40d564`);
- row 15 for the translucent group.

Textures are uploaded at the full 256x256 (`0x40d158`/`0x433388`, arguments
`0x100, 0x100`), as 8-bit palettised or ARGB1555. A separate alpha map comes
from `0x40d25c`. Our renderer takes every second texel into 128x128.

## Per face type

| Type | Guardian Direct3D (`0x40d564`, deferred `0x40de48`) | Dreams Glide | Our renderer |
|---|---|---|---|
| 2, 3 | Opaque. `MODULATE(TEXTURE, DIFFUSE)` with white diffuse, bilinear, address default (wrap) | Texture x RGB 255, **clamp** | Opaque, clamp |
| 9 | As 2/3 | Decal, wrap | Opaque, wrap |
| -6, -5 | Chroma key: `COLORKEYENABLE`, `ALPHABLENDENABLE`, `ALPHAFUNC ALWAYS`, `TEXTUREMAPBLEND DECAL`, **point filtering for these faces**, then linear restored. A colour key is set on the texture surface (`0x40d410` to `0x433ad8`, which calls offset `0x74`, read as `IDirectDrawSurface4::SetColorKey`, inferred) | Decal, wrap, chroma key | Wrap, bilinear, discard where filtered alpha < 0.5 |
| -7, -4, -3 | Deferred: palette row 15, vertex alpha 128, `BLENDTEXTUREALPHA(TEXTURE, DIFFUSE)`, alpha = diffuse, `SRCALPHA/INVSRCALPHA` | Deferred, row 15, alpha 128, `SRC_ALPHA/ONE_MINUS_SRC_ALPHA` | Deferred, row 15, alpha 128, same blend |
| -9, -8 | Deferred: `MODULATE`, alpha from the texture, `SRCALPHA/INVSRCALPHA` | not handled | not drawn (logged) |
| -13, -12 | Deferred: as -7/-4/-3 but vertex alpha 0 | not handled | not drawn |
| -21 | Deferred: **additive** `ONE/ONE`, `MODULATE`, alpha from the texture | not handled | not drawn |
| 1 | Untextured diagnostic: first vertex green `0xff00ff00`, the others cyan `0xff00ffff`, `rhw = 1/z` without the 0.1 factor | Constant colour, +`0x24bf9` per face | Flat colour, +`0x24bf9` per face |
| 0x16-0x18 | **Not drawn**: the loop over the faces has an empty body | Texture x grayscale shade | Texture x shade `min(shade*8,255)` |
| others | Not drawn | Not drawn | Not drawn, logged; unknown types abort |

## Deferred pass

- **Queue.**
  - The hook queues blocks of types -3, -4, -7, -8, -9, -12, -13 and -21 (up
    to 0x1000, with node `+0xd0`).
  - `0x40e0cc` turns them into per-triangle records (0x14 bytes: palette row,
    vertex pointer, mean depth, material, type).
- **Sort.** The records are sorted by mean depth: `0x40ee80` and `0x40ef00`
  have the shape of STL `std::sort`.
  - The insertion step (`0x40f0d0`) orders by ascending `1 - rhw`, so the
    nearest triangle comes first.
  - The direction comes from that one comparator and is unverified.
- **Draw.** `0x40de48` draws each triangle on its own (`DrawPrimitive` with
  3 vertices). Before each one it sets:
  - the texture for its row;
  - `ALPHABLENDENABLE` on;
  - **`ZWRITEENABLE` off**;
  - the blend for its type.

  Afterwards it restores blending off, `ONE/ZERO`, decal and depth writes on.
- **Against Glide and ours.** The Glide pass and ours draw in insertion order,
  with no sort, and **keep depth writes on**.

## Differences that matter for our renderer

1. **Depth writes and order for translucency.**
   - Guardian sorts and turns depth writes off.
   - Glide, and our renderer following it, keep writes on and do not sort.
   - Guardian's choice is the conventional fix and shows a Cryo team changed
     it a year later. It is not evidence about Dreams' intended look, which
     the 3dfx captures fix.
2. **Chroma-key faces use point sampling** in Guardian, which avoids
   bilinear fringes around the key colour. Ours filters and discards below
   0.5. Worth comparing against the 3dfx captures on the few -6/-5 faces
   Dreams has (506 + 80 in the scenes, glide-renderer.md).
3. **Texture resolution.** Guardian keeps 256x256 pages; ours halves them to
   128x128 to match the Glide path. A modernised mode could keep the full
   page.
4. **Clamp on types 2/3.** Guardian never sets clamp. Glide clamps and ours
   follows Glide. Guardian's data may simply not need it, so this is not
   evidence against clamping.
5. **No fog in Guardian's Direct3D build.** Fog remains Glide-only evidence.
6. **Types 0x16-0x18.** Guardian leaves them undrawn. Neither game's data has
   them, so ours drawing them is untested either way.
7. **New types.** -21 (additive), -8/-9 (texture alpha) and -12/-13 are
   Guardian's own. Dreams' corpus has none of them.

## Not established

- The exact branches of the type ranges inside `0x40de48`: groups were read
  from the decompiler's comparisons on the `ushort` type.
- Whether -13/-12 with vertex alpha 0 are visible at all.
- How the deferred records pick their palette row for types other than
  -7/-4/-3.
- The texture cache layout (0x3c-byte records at `0x5a93b8`, looked up by
  material at `+8`).
- Any runtime behaviour: none of this has been run.
