# Asset file formats

> **Function names verified (2026-09-26).** Every `WINDREAM.EXE` function this page names is in [`re/names/WINDREAM.EXE.tsv`](../../re/names/WINDREAM.EXE.tsv) with two independent sources (these docs and a blind review of the decompilation) and facts checked against the binary by `re/tools/check_names.py`.

All magic numbers below were read directly off the discs. **[verified]**

Cryo used a consistent four-character tag convention, which is strong evidence of
a single shared chunk-IO layer under all asset loading.

## Magic number table

| Ext | Magic (ASCII) | Magic (hex) | Sample | Size | Contents |
|---|---|---|---|---|---|
| `.3DC` | `F3DC` | `46 33 44 43` | `ARC.3DC` | 22,112 | 3D geometry |
| `.3DM` | `F3DC` | `46 33 44 43` | `ESSAI.3DM` | 98,332 | **Texture bank** — 32-row RGB565 palette ramp + 256×256 indexed page |
| `.DAN` | `DANF` | `44 41 4E 46` | `AR0.DAN` | 58,862 | **Character and prop models** + animation — see [models.md](models.md) |
| `.DSN` | `DSNF` | `44 53 4E 46` | `E01GROTT.DSN` | 1,771,734 | Scene / level |
| `.DRD` | `DRDF` | `44 52 44 46` | `DIALOG.DRD` | **24,592,952** | **Voice bank** — 178 WAVE clips (72.6% by size) plus 589 timed script lines |
| `.PAK` | `PAK0` | `50 41 4B 30` | `OBJET1.PAK` | 62,008 | Container of `F3DC` chunks |
| `.BF` | `UBIK` | `55 42 49 4B` | `ICONES.BF` | 372,358 | **Named-file container** — 267-byte table rows; disc 2 has 6 members, disc 1 five |
| `.UBB` | `UBB2` / `UBS2` | — | `ARENTRAD.UBB` | 1,715,124 | **Video** — HNM generation 5 |
| `.HNM` | `HNM4` / `HNM6` / `HNS6` | — | see `hnm-video.md` | — | Video |
| `.DIG` | `AIL3DIG` | `41 49 4C 33 44 49 47` | `SB16.DIG` | 2,853 | **Miles sound-card driver** |
| `.SPR` | none | — | `HI320.SPR` | 14,559 | **Sprite bundle** — three flavours: indexed, font, menu `TABLE` |
| `.ALP` | none | — | `SOUR.ALP` | 9,225 | **Menu sprite bundle** — the `TABLE` family; `.ALP` is not an alpha map |
| `.ASC` | `Ambient light co...` | — | `CUBE.ASC` | 6,623 | **3D Studio ASCII export** |
| `.TGA` | standard Targa | `00 01 01` | `INSTALL2.TGA` | 8,870 | Colour-mapped Targa |
| `.ID` | plain text | — | `1CD.ID` | 5 | Disc marker, see `disc-layout.md` |
| `.BIN` | none | — | `REPLAY.BIN` | 316 | Three-frame demo recording in an older layout; [game-content.md](game-content.md#save-format-verified-2026-09-26) |

## Shared header convention

`DSNF` and `DANF` use an identical 9-byte preamble, with the file's own size
stored **unaligned at offset 5**: **[verified]**

```
offset 0  u8[4]  magic ("DSNF" / "DANF")
offset 4  u8     flag/version, always 0x00 in samples
offset 5  u32    total file size, little-endian    <- unaligned
```

Confirmed exactly against three scenes and three animations:

| File | Bytes 5-8 | Decoded | Actual size |
|---|---|---|---|
| `E01GROTT.DSN` | `D6 08 1B 00` | 1,771,734 | 1,771,734 |
| `E02ARAI0.DSN` | `F4 E3 1B 00` | 1,827,828 | 1,827,828 |
| `E03ARAI1.DSN` | `37 14 21 00` | 2,167,863 | 2,167,863 |
| `AR0.DAN` | `EE E5 00 00` | 58,862 | 58,862 |

The unaligned u32 is a strong tell: this was written by a struct-free
byte-at-a-time serialiser, or a packed struct on a compiler with no alignment
requirement — consistent with Watcom targeting x86.

`F3DC` does **not** follow this convention (offset 4 holds the constant `450`),
which fits it being a chunk type embedded in containers rather than a standalone
file format.

### `.DSN` — scene (`DSNF`)

The merged scene corpus has **95 unique filenames**, totaling **150.8 MiB**
with disc 2 taking precedence for duplicate names in this measurement.

The header boundaries are decoded and validated across **all 95 unique scenes**;
the per-object record fields below remain partly understood. **[verified]**

```
offset  0  u8[4]  "DSNF"
offset  4  u8     0x00
offset  5  u32    file size (unaligned)   — exact in 95/95
offset  9  u8     0x00
offset 10  u32    headerSpan  A           (38 .. 999)
offset 14  u16    objectCount B           (1 .. 32)
offset 16  char[11][B]   object-name table
offset 16+11B u32[5][B]  one 20-byte record per object
offset 16+31B ...        packed payload
```

**`A` is not an independent count.** In every one of the 95 unique scenes:

```
A = 31 * B + 7          body starts at 9 + A  ==  16 + 31*B
```

So the field is a **header span**, not a second count — it exists so the loader
can skip straight to the payload without walking the tables. Naming it "count A"
was a misreading.

Worked example, `E01GROTT.DSN`: `B` = 26, `A` = 813, body at `0x336`.

**[verified]** The offset comes from the loader, not from arithmetic.
`DSN_LoadHeader` (`0x4175bc`) in `WINDREAM.EXE` peeks 9, then 5, then 2 bytes of header, then
`B * 0xb` for the name table and `B * 0x14` for the records — **back to back,
with no gap**. An earlier pass read the body as starting at `24 + 31B` and
invented an 8-byte "scene-wide block" to account for the difference; there is no
such block, and every body-parsing attempt before this was starting 8 bytes
late. `body == 9 + A` is the same relation `.DAN` uses.

The 20-byte per-object records are present in all 95 scenes. Their observed
fields are listed below; there is no extra 8-byte block before the body.

The name table is **11-byte fixed-length records**, null-padded — the classic DOS
FCB 8+3 filename field, used here for object names: **[verified]**

```
E01GROTT.DSN:  E01_ME1  E01_ME2  E01_ME3  E01_ME4  E01_MN1  E01_MN2  E01_MN3 ...
E02ARAI0.DSN:  E02_25   E02_BAS  E02_CENT E02_COL1 E02_COL2 E02_COL3 E02_COL4 ...
E03ARAI1.DSN:  E03_B1   E03_B2   E03_CH   E03_COL1 E03_COL2 E03_COL3 E03_COL4 ...
```

Names carry the level prefix and a role suffix. Read as French, `M`+compass gives
`ME`/`MN`/`MO`/`MS` = Mur Est/Nord/Ouest/Sud (walls), `SOL` = floor, `P` =
plafond (ceiling), `COL` = colonne, `BAS` = base, `CENT` = centre. **[unverified]**

Some are now **[verified]**, by rendering the object or its own texture - which
the node decode made possible by carrying names through to the export:
`PELZ` = *pelouse* (lawn, and its texture is plainly turf), `RACIN` = *racine*
(root), `TETA`/`TET` = *tete* (head - the Bayon faces in `H18ANGKR`, the moai in
`H03PAQUE`), `DALE` = *dalle* (slab), `BRIK` = brick, `NUI` = *nuit* (a
starfield), `HNM` = a surface textured by a video, and `C_BK`/`C_FT`/`C_LF`/
`C_RT` = the four walls of a cube skybox.

Beware the single-letter `P` rule: it claimed `H18_PELZ` for *plafond* when the
object is a lawn. Any other `P...` name is suspect for the same reason.
but consistent across every sample. See [assets.md](assets.md).

**[verified]** `B` is exactly the number of name records — confirmed in 95/95.
Across the merged corpus there are **2,059 records, 1,642 distinct names**, 4–8 printable
characters each with 3–7 trailing NULs.

#### The 20-byte object records **[verified]**

Immediately following the $B \times 11$-byte name table at offset $16 + 11B$ are
$B \times 20$-byte records: 2,059 across the 95 unique scenes. **[verified]**
The parser exposes all five u32 words. A fresh merged-corpus count finds word 0
zero in 1,554 records, `0x004741A0` in 421 and other values elsewhere; word 2
is 3 in 1,719 records, not in every record.

Earlier labels such as allocation flag, compass normal, friction and footstep
sound were not established by a reader trace. Their semantics remain
**[unverified]**; the Python field names are legacy labels. Do not confuse
these header records with tag 1's node table or tag 2's decoded collision data.

#### The body is a chain of tagged records

**[verified]** The body is **not** an opaque blob. It is a record chain:

```
u8   tag
u32  size          <- includes this 5-byte header
...  payload       <- size - 5 bytes
```

Walking it reaches EOF **exactly** in 95/95 distinct scenes, zero slack. The
grammar came from `DSN_LoadTextures` (`0x417afd`) in `WINDREAM.EXE`, which checks the tag, takes
the `u32`, then hands each object `0x400` bytes of the payload. See
[dsn-loader.md](dsn-loader.md).

| tag | per file | size | contents |
|---|---|---|---|
| 1 | 1 | variable, 7,366–322,183 | packed; carries object and material names |
| 2 | 1 | variable, 3,964–328,027 | packed; no readable names |
| 3 | 1 | **exactly `5 + 1024*B`** | one 256-entry palette per object |
| 4 | **exactly 64** | **exactly `5 + 1024*B`** | one 32x32 tile per object |

`.DAN` uses the same framing — 129/129 distinct animations walk to EOF — with
tags 1, 2 and 3 only, all variable-size. One container, two formats.

#### Tags 3 and 4 are the level textures

**[verified]** Fixed-size records mean **uncompressed**, and tags 3+4 are about
**97% of the body by volume**. Each object gets:

```
tag 3    1024 B = 256 entries of (u16 zero, u16 RGB565)   <- palette
tag 4    1024 B x 64 records = 64 distinct 32x32 tiles    <- 8-bit indices
```

`u16 zero` is exact: the low half of every tag-3 entry is zero in 100% of
entries across all objects.

**The 64 planes interleave into one 256x256 surface per object.** **[verified]**
Recovered from the 16 handlers behind the jump table at `0x0040148a` in
`WINDREAM.EXE`, then implemented and rendered.

Each tag-4 record is a 32x32 **subsample** of the object's texture, not a tile
of it. Source pixel `(x, y)` of plane `r` is written at
`(col + 8x + dx, row + 8y + dy)` in the surface, where `(row, col)` is the
plane's own sub-position inside every 8x8 microcell:

```
row = ((r & 1) << 2) | ((r & 4) >> 1) | ((r & 0x10) >> 4)   even bits, reversed
col = ((r & 2) << 1) | ((r & 8) >> 2) | ((r & 0x20) >> 5)   odd bits, reversed
```

Those 64 pairs enumerate all 64 sub-positions exactly once, and the planes
together fill all 65,536 pixels with no gaps.

`dx`/`dy` span a `V x H` rectangle that starts coarse and refines: `V x H` is
8x8 for plane 0, then 4x8, 4x4, down to 1x1 from plane 16 on. Early planes
paint blocks and later ones overwrite them, so a partly-loaded texture is a
blocky preview rather than a hole - progressive loading, from a 1997 CD-ROM.

This is why the planes look near-identical in isolation: each is the same
image sampled every 8th pixel at a different offset. It is also why every
tile-grid arrangement produced diagonal smearing.

Extract them with `uv run dreams extract --only leveltex` — 2,059 sheets, 118 MB.

#### Packed records and their decoded meaning

Only tags 1 and 2 are LZ-packed, roughly 40 KB per scene against 1.7 MB of raw
texture. Both decode with `dreams.formats.lz`. **Tag 1 is the renderable scene
graph**, with node-local vertices, face/normal/UV pointers and named materials;
**tag 2 is the collision mesh**, with points and 96-byte triangles carrying
planes, edge tests and bounds. The renderer never reads tag 2.

The old search for a mapping from tag-1 faces into tag-2 vertices is obsolete.
See [scene-geometry.md](scene-geometry.md) for verified layouts and
[scene-placement.md](scene-placement.md) for the eleven former export fallbacks;
`dreams.formats.collision` reads the separate collision data.

Which scene belongs to which level is now fully mapped — see
[level-map.md](level-map.md).

### `.DAN` — character and prop models (`DANF`)

Tag 1 is the **model**, built from the same scene-graph node as `.DSN`
tag 1; tag 2 is a fixed 98,324-byte texture bank; tag 3 holds animation
clips whose rotations are keyframed unit quaternions. 159/159 models
decode — full detail in [models.md](models.md). The header below was
solved first and still stands.

**Header fully decoded**, validated against **all 191 files**. **[verified]**

```
0x00  char[4]   "DANF"
0x04  u8        0x00
0x05  u32       file size (unaligned)   — exact in 191/191
0x09  u8        0x00
0x0A  u32       headerSpan A            — A = bodyOffset - 9
0x0E  u16       objectCount N           — 1, 2, 3, 4 or 6
0x10  char[11][N]   object-name table
      u16       frameCount F            — 1 .. 49
      char[13][F]   ".3DA" source labels, FIXED 13-byte slots
body  u8        0x01                    — first payload byte, always
      ...       packed payload
```

The body offset is exact in every file:

```
16 + 11*N + 2 + 13*F  ==  9 + A
```

Name-count distribution: 100 files have 1 name, 68 have 2, 18 have 3, 4 have 4,
1 has 6.

The `.3DA` references are **fixed 13-byte slots** (12-char name + NUL), not
variable-length records — 1,048 references across 191 files, 552 distinct names.
**No `.3DA` file exists anywhere on either disc**, so the frames are embedded and
these are retained authoring labels for Tag 3 animation chunks. Their numbering is sparse
(`F28AN000`, `002`, `004`, `016`, `050`), representing key clips authored for the character or prop.

**[verified] Tag 3 animation payload structure:**
Each Tag 3 chunk corresponds to a declared `.3DA` name in Directory 2 in sequential order:
- **Header**: track count $N$ at `+0x14`, offsets for tracks $0 \dots N-1$ at `+0x18 + 4*i`. Tracks bind through the Tag 1 node directory, not geometry scan order. The first offset is a real track. `XH_` has 27 named nodes; `CH0` has 18, including zero-vertex `bassin01`. There is no established FPS field after the table: previously reported FPS values were root rotation-key counts.
- **Track records**: 40-byte header: duration at `+0x14`, rotation count $K$ at `+0x18`, translation count at `+0x1C`, and rotation/translation pointers at `+0x20`/`+0x24`. Add `0x14` to these pointers to obtain payload offsets. The first rotation key begins at `+0x28`; there is no separate rest quaternion there.
- **Keyframe layout**: each keyframe $k \in [0, K-1]$ is at `trk_off + 40 + k * stride` where `stride = (end_offset - start_offset) // K` (20 or 60 bytes). Word 0 is frame timestamp; words 1..4 are unit quaternion `[qx, qy, qz, qw]` in Q15 ($32768 = 1.0$). For stride 60, words 5..6 are curve fields, words 7..10 and 11..14 are candidate tangent quaternions. Verified across 15,084 tracks and 142,806 keyframes in 780 clips from 159 distinct models with 0 invalid strides or nonmonotonic timestamps.
- **Engine evaluation**: evaluated at runtime via Slerp (`MATH_QuatSlerp` (`0x45bf68`)) or Hermite spline, converted to local $3 \times 3$ rotation matrix (`MATH_QuatToMatrix` (`0x45bc28`)), and composed down the skeletal hierarchy (`REND_DrawObject` (`0x47e498`) via `MATH_MulMat3` (`0x45b86c`)). All 780 clips across 159 models extracted to `E:\dreams-work\animations/`.

**[verified]** The payload is packed: entropy 7.338–7.819, only 0.65–3.42% zero
bytes. `AR0.DAN` body begins `01 0A 2C 00 00 30 1C 63 B4 00 FD FF 01 BE FF FF`;
`F07.DAN` begins `01 B4 52 00 00 30 1C 63 B4 00 FD FF 01 BE FF FF` — a shared
prefix past the first two bytes.

**[verified] No `.DAN` references a `.3DC` by name.** Across all 191 files there
are zero `F3DC` tags and zero `.3DC` filename strings, and no `.DAN` object label
matches a `.3DC` object label. The `DAN_Load3DC` name is real — it appears in the
error text of both `DAN_OpenArchive` (`0x40fff7`) and `DAN_Read3DC` (`0x41020f`),
neither proven to be the function of that name — so the association must be made
at runtime rather than stored in the asset.

Content reuse is heavy: nine byte-identical `CAISSE` animations, eight identical
`MCHAPO`, four identical `MCLEF`, plus aliases like `TABLEAU1`/`TABLO1` and
`TALISMA1`/`TALISMAN`. 191 physical files hold 159 distinct logical names.

## Notes per format

### `.3DC` / `.3DM` — geometry and texture banks (`F3DC`)

**They share the `F3DC` tag but are structurally different formats.** An earlier
reading of `.3DM` as "the same container" was wrong. **[verified]** across all 16
unique `.3DC` and all 4 unique `.3DM`.

### `.3DC` — object/material directory

```
0x00  char[4]  "F3DC"
0x04  u32      450          revision, constant in every standalone file
0x08  u32      0
0x0C  u32      0
0x10  u32      1
0x14  u32      0
0x18  u32      0
0x1C  u32      object count      (1, 2, 17, 130 observed)
0x20  u32      absolute offset of the first lowercase texture-name slot
0x24  u32[n-1] descending offset table
      u32      2                 constant, immediately before material records
mat   char[16] "DEFAULT"         first material slot
mat+0x20 u32   0x3DEF3DEF        default material colour (RGB555 mid-grey, twice)
mat+0x2C char[16]  second name slot — GRILLE / ESSAI / SPRITE / OMBRE2 ...
mat+0x3C char[16]  lowercase texture name (== the address stored at 0x20)
mat+0x58 ...    repeated (count, absolute-offset) descriptor pairs
```

The geometry uses the shared scene-graph node, integer positions in 40-byte
vertex records and pointer-based faces. `dreams.formats.node.read_3dc` decodes
165 nodes across the 16 unique files. `CARRE` is a quad; `BOULE`, `EPEE` and
`GUN` have closed meshes. `ARC` still has 15 boundary edges to investigate.
See [models.md](models.md#3dc-props-and-weapons).

Object labels are readable and French: `archer`, `fleche`, `sword`, `feu`, `pan`,
`cube`, `tri`.

Loaded by the `DAN` (animation) module — animation data references geometry. Its
error strings name `DAN_Load3DC` (see [engine.md](engine.md)); that text appears in
both `DAN_OpenArchive` (`0x40fff7`) and `DAN_Read3DC` (`0x41020f`), and neither is
proven to be the function of that name.

### `.3DM` — texture bank **[verified]**

Every `.3DM` file is **exactly 98,332 bytes**: **[verified]** 8/8 physical copies.

```
0x00     char[4]  "F3DC"
0x04     u32      450
0x08     20 bytes        texture-bank header
0x1C     0x8000 bytes    32 palette rows × 256 entries × 4 bytes
0x801C   0x10000 bytes   256×256 8-bit indexed texture page
```

There is **no material/object directory** in any `.3DM`. Only four exist:
`ESSAI`, `GRILLE`, `OMBRE2`, `SPRITE` — and each name also exists as a `.3DC`
material name. `RES_ReadFile` (`0x41c666`) skips the first eight bytes;
`MDL_LoadMaterials` (`0x456038`) binds the page at bank `+0x8014`.
Each palette entry has a zero u16 followed by an **RGB565** u16. The ramp has
32 brightness rows; shade selects row `31 - shade`. This is the same bank
layout as `.DAN` tag 2, decoded by `dreams.formats.node.read_3dm`.
The older three-image/RGB555 interpretation is rejected. See
[assets.md](assets.md#textures--the-honest-position) for the decoded content.

### `.PAK` — geometry container (`PAK0`)

```
0x00  char[4]  "PAK0"
0x04  u32      62004     = file size - 4
0x08  u32      82995     unknown, constant
0x0C  char[4]  "F3DC"    embedded chunk starts here
0x10  u32      100       embedded revision - NOT the standalone 450
```

**[verified]** on both copies of `OBJET1.PAK` (they are byte-identical).

Three corrections to earlier notes:

- `F3DC` sits at offset **12** (`0x0C`), **not 16**.
- The file contains **exactly one** `F3DC` occurrence. No multi-chunk directory or
  index table was found, so calling `.PAK` "an archive wrapping multiple chunks"
  is not supported by the bytes — with only two `.PAK` files on the discs it may
  simply never carry more than one.
- The embedded chunk's revision field is **100**, so "offset 4 holds 450" is true
  only for *standalone* `.3DC`/`.3DM`.

It contains the string `Fem10`, a character model name. **[unverified]** whether
that is the only archived model.

### `.DRD` — dialog (`DRDF`)

`DATA\3DC\DIALOG.DRD` is **24.6 MB**. That is far too large for dialog text, so
it bundles voice audio. It is one of only eight files copied by even the
*minimum* install (see `disc-layout.md`). The runtime keeps the entry-offset
table and one reusable record buffer, then seeks and reads a requested entry;
it does not load the full bank into memory.

The header stores `DRDF`, the physical file size, and 178 entries. A five-byte
table-block header starts at `0x10` (tag zero, size `5 + 4*N`); the 178 ordinary
little-endian `u32` absolute offsets start at `0x15`. The first entry starts at
`0x2DD` and its RIFF payload at `0x2EB`. The older `0x14` table start was one
byte early and created a false 24-bit-wrap interpretation. The correctly
aligned offsets give all 178 adjacent records through EOF. Each record carries a RIFF/WAVE block,
timed text lines, and sometimes a tag-4 portrait. Sub-block sizes include their
own 5-byte header; tag 2's size is therefore five bytes longer than the WAVE
payload. The recovered parser finds 589 non-empty text lines total.

Tag 4 is not a keyframe or camera script. It is an indexed 2-byte-per-pixel
sprite: a 512-byte RGB555 palette, one 124×124 or 128×128 image (with five
nearby dimension variants), the `TABLE` marker, reserved capacity for 256
28-byte descriptors, and a trailing count of 1. Only the first descriptor is
populated. The engine loads the palette and that descriptor, then draws the
portrait beside the captions. **[verified]** 169 of 178 entries carry one; 9
have no portrait. Their payload bodies total 6,719,761 bytes (27.3% of the
archive): 107 images are 128×128, 57 are 124×124, and five have nearby size
variants.

At runtime, event `0x40` selects an entry index. The game loads the matching
record, submits its WAVE bytes to the sound buffer, and displays its timed
lines through the font renderer. Runtime line timing is scaled by `15/100`.
The decompiled call path and remaining open points are in
[sprites-ui-dialog.md](sprites-ui-dialog.md).

### `.BF` — `UBIK` bundle

**Fully decoded.** **[verified]** across all five generations of `ICONES`.

```
0x00  char[4]  "UBIK"
0x04  u32      2            container version
0x08  u32      table offset (== file size - 267*count)
0x0C  u32      entry count
0x10  ...      payload, entries back to back

table record, 267 bytes each:
  +0x000  char[259]  asset filename
  +0x103  u32        absolute payload offset
  +0x107  u32        payload length
```

The payload chain is exact: every `offset + length` equals the next entry's
offset, and the last entry ends precisely at the table offset.

The shipping disc-2 `ICONES.BF` holds six assets:

| Name | Offset | Length |
|---|--:|--:|
| `MAGIE.ALP` | 16 | 116,489 |
| `ANIM.ALP` | 116,505 | 40,457 |
| `PYRAM.ALP` | 156,962 | 94,761 |
| `TITRES.SPR` | 251,723 | 67,404 |
| `TOUCHES.SPR` | 319,127 | 14,025 |
| `INTERF.ALP` | 333,152 | 105,275 |

So `.BF` is a plain named-asset container holding `.SPR` and `.ALP` members —
all of them the **menu `TABLE` sprite family** below, now fully decoded. Disc 1
ships five members (no `TITRES.SPR`); the engine loads exactly those five by
name from its own table at `0x49dacc` in `WINDREAM.EXE`.

> **Correction.** "UBIK" here is **not** evidence of a shared Cryo authoring
> toolkit. `.UBB` turned out to be the HNM5 *video* codec, unrelated to this
> container beyond the borrowed name (Cryo also published a game called *Ubik*).
> The earlier inference linking `UBIK`, `UBB2` and `PLAYUBB.EXE` into one toolkit
> is withdrawn.

### `.UBB` — video, HNM generation 5

**[verified]** `.UBB` is **not** a "presentation bundle", a subtitle sidecar or an
audio sidecar, and there is no UBIK authoring toolkit. It is the **fifth
generation of Cryo's HNM video codec**, introduced with *MegaRace II* (1996).
NihAV reads it with a plugin it labels "Cryo UBB". This settles a long-standing
open question in this project.

The 19 game files are 3 × `UBB2` and 16 × `UBS2`, all 640×304, all self-contained:
UBB2 carries `IV` video and `PL` palette chunks, UBS2 adds `SD` sound chunks
internally. No game `.UBB` begins with `HNM6` — the single `HNM6`-tagged `.UBB` on
the discs is `DEMOS2\3MILL\3MILL.UBB`, which is demo content.

Full header layout and decoding instructions in [hnm-video.md](hnm-video.md).

`SETUP.INI` registers `demos2\playubb.exe` as "DemoPlayerUbb" — it is a video
player, which is consistent.

### `.DIG` — Miles drivers, NOT game audio

**This corrects an earlier assumption.** Every `.DIG` file begins `AIL3DIG` —
Audio Interface Library v3 digital driver, the Miles Sound System driver format.
The files in `DATA\SOUND\` are per-sound-card drivers: **[verified]**

```
ADRV688.DIG  SB16.DIG      SBLASTER.DIG  SBPRO.DIG    PROAUDIO.DIG
ULTRA.DIG    SNDSCAPE.DIG  SNDSYS.DIG    RAP10.DIG    NVDIG.DIG
IWAV.DIG     JAMMER.DIG
```

`ULTRA.DIG` is Gravis Ultrasound, `SB*.DIG` are Sound Blaster variants, and so
on. Their 1995-1996 timestamps predate the game and match stock Miles
distribution files.

The actual game audio bank is **`DATA\SOUND\FSB.DAT` (741,370 B)**. Music is not
in the filesystem at all — it is redbook CD audio on tracks 2+.

Also present: `MSSDRVR.LST` (stock Miles driver-selection message file, 20,434 B),
`SETSOUND.EXE` (120,295 B, the DOS sound configurator) and `MSSW95.EXE`
(8,029 B).

### `.SPR` — sprite bundles, **not** raw bitmaps

No magic. There are **three `.SPR` families**, and none is a raw
RGB555 image — an earlier description of them as such was wrong. **[verified]**

#### `DATA\OBJET\` — indexed sprite bundles (5 files)

```
0x000  u8[4][256]   VGA palette, RGBX, every channel <= 0x3F (6-bit DAC)
0x400  u32[256]     pointer table, offsets relative to 0x400
record +0x00  u32   width
record +0x04  u32   height
record +0x08  u32   placement/hotspot?        [unverified]
record +0x0C  u32   placement/hotspot?        [unverified]
record +0x10  u8[]  8-bit palette indices
record size  = round4(16 + width*height)
```

**[verified]** across all 232 payload records. Unique record counts:
`ALPHABET.SPR` 64, `ALPHABE2.SPR` 64, `PARTICL2.SPR` 64, `PARTICLE.SPR` 32,
`OBJET0.SPR` 8. Pointer slots `0x5000` and `0x100` are special/empty entries
whose meaning is **[unverified]**; several slots alias the same record.

So `ALPHABET`/`ALPHABE2` are bitmap fonts (64 glyphs each) and
`PARTICLE`/`PARTICL2` are particle sprite sheets.

How the July DOS blitter `_ZoomSpriteL16` (`0x1ca86`) reads them, with the
file loaded by `LoadFileSpr_` (which multiplies the 256 palette dwords by 4)
**[verified in code]** (spec 008 phase 1, where the port draws the editor's
sliders this way):

- Dword `0x400` is a header, not a record pointer; dword `0x404` (0 in
  `ALPHABE2.SPR`) allows mirrored drawing. Record `n` is at `0x400 +
  dword[0x408 + 4n]`, so pointer slot `k` of the table above is record
  `k - 2`. A sprite is named by a 16-bit code whose high byte is `n`; a
  negative code draws the record of its negation mirrored when `0x404` is
  set.
- Record `+0x08`, `+0x0C` are the hotspot: the sprite is drawn with that
  pixel at the given position, mirrored or not.
- Palette dwords convert to RGB565 as byte 2 → red, byte 1 → green, byte 0
  → blue (so the bytes are B, G, R, X); index 0 is transparent.
- `ALPHABE2.SPR` records `0x94` (17 × 2, hotspot 0,1) and `0x98` (7 × 11,
  hotspot 3,10) are the editor's slider track segment and knob.

#### `DATA\FONT\` — `HI320` / `HI480` / `HI640` (3 files)

These are **256-character, 8-bit indexed bitmap fonts**. **[verified]** 3/3 by
the file layout, descriptor offsets, and rendering in the executable.

```
0x000  u16[256]       RGB555 palette (512 bytes)
0x200  ...            1-byte palette-index glyph bitmaps; index 0 is transparent
len-0x1c04            256 × 28-byte descriptor table
len-4  u32 0x100      glyph count
```

Each descriptor is `u32[7]`: palette pointer `+0x00` (0 in file), width
`+0x04`, height `+0x08`, three additional fields at `+0x0c/+0x10/+0x14`,
and absolute bitmap offset `+0x18`. `TEXT_LoadFont` (`0x425c61`) loads each font
through the general sprite-set loader `SPR_LoadSet` (`0x425254`), which indexes all 256
records. `TEXT_LoadFont` computes per-character horizontal advance as
`width - s32(field_0c)`; it assigns the space advance from the `'0'` record.
`TEXT_BlitGlyphFaded` (`0x403bcd`) blits each nonzero glyph index through the palette
and blends the whole glyph with one alpha through `SPR_BlendPixel` (`0x401524`); it is
reached from `TEXT_PrintFaded` (`0x425f07`) via `0x425e74`.

The names identify display modes, not dimensions: `HI320`/`HI480`/`HI640`
are the 320-, 480-, and 640-wide modes. One `HI320` descriptor, codepoint
`%` (37), has invalid dimensions and cannot be rendered by the decoder; the
same glyph is valid in `HI480` and `HI640`. The runtime font renderer uses
formatted strings, so whether this anomalous slot is reachable as a literal
character remains open.

> **Correction.** `HI320.SPR` opening `1F 1C FF 7F` was previously read as a
> width/height header followed by white RGB555 pixels. It is not: these are
> the first two entries in its 256-word palette (`0x1C1F`, `0x7FFF`).

#### Menu `TABLE` bundles — `.ALP` and menu `.SPR` — **fully decoded**

The `ICONES.BF` members (`MAGIE`/`ANIM`/`PYRAM`/`TOUCHES`/`INTERF`, plus
disc 2's `TITRES.SPR`) and `DATA\OBJET\SOUR.ALP`. Loaders in `WINDREAM.EXE`:
`SPR_LoadIconBanks` (`0x426c46`) for the five `ICONES.BF` banks, and the general
sprite-set loader `SPR_LoadSet` (`0x425254`) for `SOUR.ALP` (and the fonts).
**[verified]** across all members on both discs; every member's
pixel data ends exactly at its `TABLE` marker.

```
0x000  u16[256]    palette, RGB555 (512 bytes)
0x200  ...         sprite pixel blobs, absolute offsets, tightly packed
...    "TABLE"     5-byte marker
+5     N x 28 B    descriptors:
         +0x00  u32  palette pointer (runtime; 0 in the file)
         +0x04  u32  width
         +0x08  u32  height
         +0x0C  u32  flag (small negatives on TITRES/INTERF)
         +0x10  u32  flag (-1 / -23 on some INTERF records)
         +0x14  u32  0
         +0x18  u32  absolute pixel offset
...    zero padding
end-8  [u32 0][u32 N]  footer (approximate; the loader re-derives the table)
```

Pixel layout is **per file**, recovered from the offset stride: one byte per
pixel is a palette index (`TOUCHES.SPR`, `TITRES.SPR`); two bytes per pixel
are a **palette index followed by an opacity/blend value** (the `.ALP` banks
and `SOUR.ALP`). These are not packed RGB555 pixels. The retail evidence is
the loader `SPR_LoadIconBanks` (`0x426c46`) plus its renderer `SPR_BlitSprite` (`0x401935`): the loader binds
the RGB555 palette to each descriptor and copies the two-byte texel stream
unchanged; the two-byte draw path uses the first byte to select a palette word
and the second as its blend amount. A zero index is skipped in both paths; in
the two-byte path coverage 0 is skipped and values >=63 are written opaque.
The engine always reads `w*h*2` bytes, which over-reads the one-byte files.

For coverage 1–62, `SPR_BlendPixel` (`0x401524`) uses `weight = coverage >> 1` and blends
each packed channel as `((31-weight)*destination + weight*source) >> 5`.
`SPR_InitMulTables` (`0x424f7e`) fills two multiply lookup tables, 32×32 and 64×64; the 32×32 one is used by `SPR_BlendChannel` (`0x4014d0`),
confirming the weight formula. The helper has separate RGB555 and RGB565
channel-packing paths.

The apparent zero-divisor concern was a branch mix-up. `SPR_Draw` (`0x4274b0`) sets
source flag `0x10`, which selects the raw two-byte coverage path at `0x401DAD`.
The divide at `0x401EBF` is under a different flag (`DAT_0049D12A=1`) and is
not selected by that menu wrapper.

The earlier byte-swapped-RGB555 interpretation was wrong: it treated the pair
as a color word and produced green/purple artifacts. It appeared to fit one
main-menu screenshot but contradicted the retail renderer's indexed lookup.
`menu_sprite_rgba` expands the palette index and maps the effective blend
weight to PNG alpha. This preserves the palette colors and transparency; a
straight-alpha PNG cannot reproduce the game's exact packed-channel weighted
sum. **[verified]** against the decompiled load/draw path and every `.ALP`
bank's pixel stream.

`PYRAM.ALP` has layer-reference markers in slots 0 and 5: coverage bytes
`0xff` and `0xfe` substitute pixels from linked descriptors; `0xfd` selects
another layer or a state-dependent fill. `UI_DrawPyramidGauge` (`0x40368b`) resolves them during
the animated pyramid UI. Standalone extractor sheets use false colors for
these commands because the final composite depends on live UI state.

Sprite **names are not in the files**. The engine carries a 72-entry name
table at `0x49db12` (stride 9) mapping to `(bank, slot)` at `0x49dd9a`; the
bank filenames live at `0x49dacc` (stride 13). The mapping is reproduced in
`dreams.formats.image.MENU_SPRITE_NAMES` for one primary label per slot; the
separate `MENU_SPRITE_ALIASES` preserves `infosta` as another name for MAGIE
slot 26. The complete retail table also has `block` at MAGIE slot 34, whose
shipping descriptor is empty. Decoded: **[verified]**

| Bank | Size | Contents |
|---|---|---|
| `MAGIE.ALP` | 40×40 ×34, 2-byte indexed + blend | 30 inventory/ability icons plus `infosne`/`mcombat`/`infosde`; the first 30 *name-table IDs* follow `[OBJECT]` order, while their pixel slots are indirect and non-linear |
| `ANIM.ALP` | 32×32 ×16, 2-byte indexed + blend | animation frames (name `nothing` → slot 0) |
| `PYRAM.ALP` | 64×84 ×9 + strips, 2-byte indexed + blend | the pyramid spell-selector UI — `pyrambo/vi/ma/ox` variants, `pyrcurs` cursor, `exprbor`/`exprlev` 64×4 strips, `replay`/`record` |
| `TOUCHES.SPR` | 24×24 ×11, 8bpp | key/joypad caps — `joy_up/dn/lf/rt/k1/k2/k3/sel/swi/bt0/bt1` (the F10 controls screen) |
| `INTERF.ALP` | 64×64 ×8 + panels, 2-byte indexed + blend | menu corner markers `DnRg/DnLf/UpLf/UpRg` + `…NA` inactive variants, `RubLf/RubRg` ribbons, `Desc1–4` panels |
| `TITRES.SPR` (disc 2) | ~128×42 ×12, 8bpp | the four title images in three render states (gold, red-highlight, third variant); **not in the engine's 5-file load list and not used for boot-menu labels** (drawn by font routine `TEXT_Print`, `0x426073`); no other runtime use found |
| `SOUR.ALP` (`DATA\OBJET`) | 16×24 ×2, 2-byte indexed + blend | the mouse cursor, two frames |

Rendered contact sheets confirmed the decode visually: TITRES shows the four
golden (and red-highlight) menu titles and TOUCHES the yellow direction arrows
and key caps. `PYRAM` slots 0 and 5 are animated layer composites; the static
sheet marks their linked-layer commands in diagnostic colors instead of
claiming to show the final in-game pyramid.

## File census

Excluding `DIRECTX\` and the demo directories, across both discs: **[verified]**

| Count | Ext | | MB | Ext |
|---|---|---|---|---|
| 191 | `.DAN` | | 281.0 | `.HNM` |
| 98 | `.DSN` | | 157.2 | `.DSN` |
| 94 | `.HNM` | | 45.7 | `.UBB` |
| 32 | `.3DC` | | 23.5 | `.DRD` |
| 24 | `.DIG` | | 23.4 | `.DAN` |
| 20 | `.TGA` | | 11.7 | `.TGA` |
| 19 | `.UBB` | | 6.8 | `.EXE` |
| 16 | `.SPR`, `.EXE` | | 1.7 | `.DAT` |
| 8 | `.3DM` | | 0.9 | `.SPR` |
| 7 | `.JPG` | | 0.8 | `.BF`, `.3DM` |

Two observations. **Animation dominates by count** (191 `.DAN` against 32 `.3DC`),
so `.DAN` is the primary per-object unit and geometry is shared/reused.
**Video dominates by volume** (281 MB, nearly half the total), with scenes second
at 157 MB.

### Developer leftovers

The retail disc was mastered from an uncleaned build tree. Beyond the
`ANTI-VIR.DAT` caches and `DESCRIPT.ION` files: **[verified]**

| File | Size | What it reveals |
|---|---|---|
| `DATA\ICONE\ICONES.OLI` | 452,435 | Older `UBIK` icon set |
| `DATA\ICONE\ICONES.BAK` | 440,029 | Another version — **same size as disc 2's shipping `ICONES.BF`** |
| `DATA\ICONE\OLD\ICONES.BAK` | 460,203 | A third |
| `DATA\ICONE\OLD\ICONES.OLD` | 431,423 | A fourth |
| `DATA\OBJET\3DS.BAK` | 2,538 | 3D Studio backup |
| `DATA\OBJET\CUBE.ASC` | 6,623 | 3D Studio ASCII scene export |
| `DATA\OBJET\STATUS.ME` | 886 | `@MULTI-EDIT VERS...` — **Multi-Edit** editor session file |
| `DATA\TGA\TEMP\*.JPG` | 7 files | Reference renders left in a `TEMP\` directory |

So **five generations of the icon file ship on the discs**, and the disc-2
"newer" `ICONES.BF` is byte-size-identical to disc 1's `.BAK`. The `TEMP\` JPEGs
are named by level (`E09_0000`, `E13_0001`, `F02_0002`, `H03_0000`, `L01_0003`,
`M01_0003`), matching the `.DSN` prefixes.

Tooling identified so far: **Autodesk 3D Studio** for models, **Multi-Edit** for
text, **Watcom C/C++** for the game, **Microsoft Visual C++** for CryoLib.

### `.ASC` and `.BAK` — developer leftovers

`DATA\OBJET\CUBE.ASC` opens with `Ambient light co` — it is a **3D Studio ASCII
scene export**, a plain-text intermediate file. `DATA\OBJET\3DS.BAK` (2,538 B) is
a 3D Studio backup. Neither is a shipping asset; both are residue from the
content pipeline, left on the retail disc.

This confirms Cryo authored models in Autodesk 3D Studio and converted to `F3DC`.
Anyone reversing the geometry format should diff `CUBE.ASC` against a
corresponding `.3DC` if one can be identified — a known-plaintext attack on the
format. **[unverified]** whether a matching `.3DC` for `CUBE` exists on disc.

### `.TGA`

Standard Truevision Targa, colour-mapped (`00 01 01`). Used for installer UI art
(`DEMO0\*.TGA`), the `CRYOPLUS` bonus gallery on disc 2, and `DATA\TGA\`.
`PLAYTGA.EXE` on disc 2 is a standalone viewer.

### Text manifests

`LISTL0.TXT` through `LISTL4.TXT` (byte-identical on both discs) are per-level
asset lists using backslash paths: **[verified]**

```
DATA\3DC\H03PAQUE.DSN
DATA\3DC\CH0.DAN
DATA\3DC\HOLO.DAN
DATA\ANIM\H03AN001.HNM
DATA\3DC\XH_.dan
DATA\3DC\MHE.dan
```

Note the inconsistent case (`.dan` vs `.DAN`) — another sign of an uncleaned
build. `LISTL0.TXT` has 6 entries; `LISTL1.TXT` has ~200. Entries repeat, so the
list is probably load-order rather than a set. With hard-disk caching,
`CD_PrepareLevel` (`0x427d64`) copies the files listed in `ListL<n>.txt` /
`ListL0.txt` to `X:\CRYO\DREAMS\` behind "Please wait while loading ..."
(`CD_CopyFileList` (`0x428356`)).

### `DREAMS.DAT` — the project bank

**[verified]** It is **self-indexing** — it indexes itself, not a companion blob.

```
0x000   u32[151]    offsets, relative to 0x400
0x25C   420 x 00    padding (verified all-zero on both discs)
0x400   150 records "ProjectN\0" + variable body, 132 .. 2,592 bytes each
```

`0x400 + offset[150]` equals the file size **exactly** on both discs — 138,879 on
disc 1, 138,835 on disc 2. Record bodies contain the strings `LINK0`, `OBJETn`,
`BOXn`, `LINKADVENTn`, so this is a project *graph*, not a
flat asset list. The decoded fixed record and verified fields are listed below;
fields without established semantics remain identified by offset.

> **Correction.** Describing this as "a monotonically increasing table of u32
> offsets" across the whole file was wrong. Only the first 151 entries are that
> table; everything past `0x400` is record payload, and reading it as offsets
> produces meaningless non-monotonic values.

**[verified]** The two discs' copies differ in exactly **six** project records —
P31, P41, P55, P69, P75, P87 — with identical embedded asset-name sets. Only
numeric fields and record lengths differ, and disc 1's copy is 44 bytes larger.
Disc 1's is the later snapshot: in five of the six records it moves away from
the value disc 2 shares with both July 1997 demo banks
([cryo-editor.md](cryo-editor.md), report D §4.4) **[verified in data; the
ordering is unverified]**. It is also the program disc, so it stays the
default.

The bank is the export of the game's built-in editor (`WORKS.C`), which named
every record `<Kind><slot>` and wrote this file with `WOR_SceneRLECompress_`;
the editor's menu labels are the field names used below. See
[cryo-editor.md](cryo-editor.md).

Joining these 150 records to `DREAMS.INI` gives the complete level map — see
[level-map.md](level-map.md).

#### The record codec and layout **[verified]**

Each of the 150 records is **zero-run compressed**: a nonzero byte is literal,
and `00 N` expands to `N` zero bytes. That is the engine's own loop,
`RLE_UnpackZeros` (`0x448e25`) in `WINDREAM.EXE`, applied per record by `DDAT_LoadRecord` (`0x449bf9`). Every
record decompresses to exactly **`0x2200` bytes** - 150 of 150, which is the
validation: a wrong codec does not land on a constant.

```
+0x0000  header         0x200   starts "ProjectN" + environment, camera, lights, spawn
+0x0200  Link[8]        0x80    name[12], destination[12], ..., i32 min[3] @+0x24, i32 max[3] @+0x30
+0x0600  Objet[16]      0xc0    name[12], asset[32], flags @+0x34, i32 pos[3] @+0x40, heading @+0x5c, behavior @+0x64
+0x1200  Box[12]        0x100   name[12], min[3] @+0x0c, max[3] @+0x18, i32 points[16][3] @+0x24, last @+0xe4, type @+0xf0
+0x1e00  LinkAdvent[16] 0x40    name[12], 8 x i32, asset[16] @+0x2c
```

A slot is live when its **in-use bit** is set, not when it has a name: bit 0
of header `+0x14`, LINK `+0x18`, OBJET `+0x34`, BOX `+0xEC` and LINKADVENT
`+0x28`. The editor sets the bit on create; delete clears it and at most the
first letter of the name (`INK0`, `BJET3` fragments), and a deleted BOX keeps
its whole name. **[verified in code and data]** Counts of named slots over
disc 1: `LINK` 244 (242 live, 239 naming a project), `OBJET` 711 (710 live),
`BOX` 420 (393 live), `LINKADVENT` 328. `OBJET0` is the project's scene in
150 of 150. Decoded by
[`dreams.formats.project`](../../src/dreams/formats/project.py), which still
decides liveness by the name (and has the other decoder faults listed in
[cryo-editor.md](cryo-editor.md#corrections-made-with-this-pass)).

##### Project Header fields (0x200 bytes) **[verified]**
Decompiled from `SCENE_LoadLevel` (`0x41f9db`) and `ENT_InstantiateFromObjet` (`0x41deb8`) in `WINDREAM.EXE`:
- `+0x000` `char[16]`: project identifier (e.g. `Project0\0`).
- `+0x018` `i32[3]`: Signed palette RGB base, used by `REND_TickPaletteLighting`.
- `+0x024` `i32[3]`: Signed palette RGB random variation; these triplets are not spatial directions. See [lighting.md](lighting.md).
- `+0x030` `i32[3]`: Ambient light RGB components (values in $0 \dots 255$, e.g. `(152, 168, 126)`).
The strings from `+0x03C` onward are 16-byte cells. The decompiled users
(2026-09-28) establish these meanings:

- `+0x03C` `char[16]`: optional full-screen movie under `data\hnm\` (`.HNM` or
  `.UBB`, e.g. `CASC2.HNM`), opened with MGM `0x17` (`0x416015`, `0x436841`,
  `0x420d13`). Project 0's string is empty; its `ETE_E~1.HNM` bytes begin at
  `+0x03D` after a NUL.
- `+0x04C` `char[16]`: scene material that receives the HNM4 animated texture
  (`0x42dae2`, e.g. `M01DRA` in Project 12).
- `+0x05C` `char[16]`: HNM4 file under `data\anim\` (`0x42dbc7`, e.g.
  `M01DRA.HNM`), opened once per level on the first game tick after DSN
  texture streaming ends.
- `+0x06C` `char[16]`: material for the slot-flag `0x20` software page effect
  (`0x42dea3`); not a video target.
- `+0x07C` `char[16]`: material for the slot-flag `0x40` page effect
  (`0x42dfa0`); it also retargets the HNM4 pixel pointer `0x5e5494`.
- `+0x08C` `char[16]`: replacement player model (editor label "Vehicule
  Mesh", e.g. `MOT.3DC`, `SUR.3DC`; 23 projects). `SCENE_InitLevel` loads it
  instead of `xh_.3dc`/`mhe.3dc` when the first byte is non-zero
  (`0x41f66a`). **[verified]**
- `+0x09C` `i32`: Player movement mode, copied to actor `+0x34` by `ENT_LoadObject` (`0x41d624`) when non-zero (4 → 3, 5 → 1 flying, 6 → 3 flying). **Not** a camera projection mode. **[verified]**
- `+0x0A0` `i32`: Player movement scale, copied as a float to actor `+0x104` (the animation step scale `ANIM_TickBlend` (`0x405f1f`) and `ANIM_TickClip` (`0x4068be`) multiply by). **Not** a near clip. **[verified]**
- `+0x0A4` `i32`: Player turn step, copied to actor `+0x108`; `ANIM_RequestState` (`0x405118`) turns by it (default `0x30` of 4096 per turn, ¾ of it outside combat stance `+0xac & 0x20`). Values 63–65 were read as a field of view; the real FOV is a constant 76.36° (engine.md, *Camera and projection*). **[verified]**
- `+0x0B4` `i32[3]`: Player canonical spawn coordinates `(x, y, z)` in scene units (negative Y is up).
- `+0x0C0/+0x0C4` `i32`: Actor-bound/other palette-row scales (defaults 8/2).
- `+0x0C8` `i32`: Enables actor effect lights.
- `+0x0CC` `i32`: Far-plane override, also the player's effect-light outer radius.
- `+0x0E0` `i32[4]`: underwater palette tint R/G/B and flicker (editor "Fluid
  Med R/G/B", "Fluid Ond RGB"); `REND_TickPaletteLighting` adds them only
  while the player is below the water level `+0x0D4` (flag `0x626310`, set
  by `ENT_TickPlayerStatus` `0x423452`). Formerly mislabeled fog.
- `+0x0F0` `i32[4]`: Palette interpolation target RGB and duration, formerly mislabeled sky.
- `+0x10C` `i32`: Player canonical spawn heading (12-bit angle, $0 \dots 4095 \equiv 360^\circ$).
- `+0x138` `i32`: colour of the level fade (editor "Fondu b n", *fondu
  blanc/noir*): 0 fades to white (palette biases added), 1 to black
  (subtracted, then the frame buffer cleared), other values (16 in P0/P108)
  neither. Read by `SCENE_LoadLevel` (`0x41fc67`), the level-exit fade in
  `GAME_Tick` (`0x424198`, `0x424258`, `0x424329`) and
  `MDL_BindActorPalette` (`0x42dccd`); `GAME_LoadGame` forces 1 (`0x40fc02`).
  **[verified]**
- `+0x1C0/+0x1C4/+0x1C8/+0x1CC` `i32`: Glide fog RGB and density input, with water override behavior; see [glide-renderer.md](glide-renderer.md).
- `+0x1E8/+0x1EC` `i32`: Optional palette contrast strength and material-name filter.
- `+0x11C` `i32`: CD music track of the disc the level is on (editor "Scene
  CD Track", 0..20; Project0 = 9, Project116 = 2, Project113 = 0 = none).
  `GAME_StartLevel` keeps only the low byte (`and edx, 0xff` at `0x42f1b4`)
  and sends it with MGM `0x1e`; `CD_SetPlaylist` could take three tracks,
  one per byte, but the record path passes one. Seen live 2026-10-01, see
  [install-and-discs.md](install-and-discs.md). **[verified]**
- `+0x1F8` `i32`: level-entry save mode, read by `GAME_StartLevel`
  (`0x42f1c9`, `0x42f1e1`) and `SCENE_RestoreLevelState` (`0x41b2b2`): 0
  autosave; 1 autosave and clear the hotkey slots of items not held; 2 no
  save; 3 no save and no re-entry restore. Not in the July editor's menu.
  **[verified]**

Further header fields, named by the July 1997 editor's menu labels (verbatim
in quotes) and checked against their retail readers (report D §2.1 in
[cryo-editor.md](cryo-editor.md)) **[verified]** unless marked:

| Offset | Editor label | Retail use |
|---|---|---|
| `+0x014` | "Flags Player" | bit 0 in use; byte `+0x16` bits 1/2/4 = GUN OK, SURF OK, FALL NO OK, copied to player `+0xb0` by `ENT_LoadObject` (`0x41d9b3`) |
| `+0x0A8` / `+0x0B0` | "Sphere collision" / "Sphere shoot" | player `+0x10c` (and half at `+0x110`) / `+0x118` (`0x41da00`, `0x41da3f`) |
| `+0x0AC` | "Sphere move" | no reader; 0 everywhere |
| `+0x0D0` | "Scene IA Strategic" | squad order table (`AI_ApplySquadOrderTable` `0x4120af`) |
| `+0x0D4` | "Fluid YPos Level" | water level `0x5e5490` (`SCENE_InitLevel` `0x41f5b2`); oxygen drains 150 units below it |
| `+0x0D8` / `+0x0DC` | "Move Inertie swim" / "fly" | swimming and flying movement scale |
| `+0x100..+0x108` | "Phys Gravite Const Vect X/Y/Z" | gravity override (`PHYS_InitForceFields`); 0 in every bank |
| `+0x114` | "Scene Dec Oxygen MUL 10" | oxygen −= v·Δt·0.01 below water (`0x4234bd`) |
| `+0x118` | "Scene Add Mana MUL 10" | magic += v·Δt·0.01, cap 120 (`0x4235c8`) |
| `+0x120..+0x134` | "Camera K_OBJ_TARGET_MIN/MAX", "K_OBJ_BACK_MIN/MAX", "Y_LOW/HIGH_FOLLOW_OBJ" | follow-camera preset 0 overrides when non-zero (`CAM_LoadPreset` `0x40b829`–`0x40b8bf`) |
| `+0x13C` | "Bruit pas" (footsteps) | no reader; set in 9 projects |
| `+0x140` | July "Camera Combat Angle" | retail: player `+0x44` = v/64 (`ENT_LoadObject` `0x41d974`); meaning changed |
| `+0x144` | July "Camera Combat back" | retail: 0 enables camera collision (`0x409d8c`, `0x40a988`); meaning changed |
| `+0x148..+0x1D4` | "Scene Particle" branch: volume min/max (×256), speeds, generation, turbulence, lifespans, force, attractors, quantity, player mana gain | `PART_LoadLevelParams` (`0x43b3c3`…) |
| `+0x1C0..+0x1CC` | July "Particle Four" attractor | retail Windows zeroes that attractor; the DOS Glide build reads the four words as fog (above). Project 66 still holds July attractor values |
| `+0x1D8` | "Camera Speed" | `CAM_LoadPreset` `0x40b80b` |
| `+0x1DC` | "Player Speed Move" | player `+0x180` = v/16 |
| `+0x1E0` | "Particle OK" | particles on (`PART_InitLevel` `0x43b726`) |
| `+0x1E4` | "Sky Speed" | sky rotation (`SCENE_RotateSky` `0x41d4a9`) |
| `+0x1F0` | "Sky Dead" | kill height: while the player is below it, 40 damage per tick (`0x4233ec`) |
| `+0x1F4` | "Perso Integration" | loaded by `ENT_AdaptActorColor` and overwritten before use: dead in retail |
| `+0x1FC` | — (October only) | chapter/disc group 0–4: the mastering lists and `CD_PrepareLevel` (`SCENE_GetLevelNumber` `0x41ad77`); see [cryo-editor.md](cryo-editor.md#the-mastering-step) |

##### `OBJET` fields (0xC0 bytes) **[verified]**
Decompiled from `ENT_InstantiateFromObjet` (`0x41deb8`) (entity instantiation) and `DBG_DrawObjectInfo` (`0x416606`) (the engine's developer debug HUD):
- `+0x00` `char[12]`: slot name (`OBJET0` .. `OBJET15`).
- `+0x0C` `char[16]`: asset filename (`.DSN` scene for slot 0; `.DAN` character or `.3DC` prop for slots 1..15).
- `+0x1C` `char[16]`: symbol file (`.SYM`, editor "Load Symb"), passed to
  `SYM_InitSymboleInObjet_` when non-empty; empty in every bank. **[verified]**
- `+0x2C` `i32`: "Life" → actor `+0x38` vitality (`0x41e335`). **[verified]**
- `+0x30` `i32`: "Mana" → actor `+0x3c` magic (`0x41e34a`). **[verified]**
- `+0x34` `u16`: flags. The editor labels each bit; the actor bits are the
  same in the July demo (`LoadSceneObjet2TableObjet3dS_`) and retail. Counts
  are named slots on disc 1. **[verified]**

  | Bit | Editor label | Runtime | Count |
  |---|---|---|---|
  | `0x0001` | (in-use bit) | spawned at level load only if set | 710 |
  | `0x0002` | "Flag NO ANI / ANI" | animation set loaded: characters and creatures (`XH_.DAN`, `F07BLEU.DAN`, `CH0.DAN`) | 411 |
  | `0x0004` | "Flag Ami / Enemi" | actor `+0xa9 \| 0x20` | 201 |
  | `0x0008` | "Flag NO Light / Light" | `+0xab \| 8` | 8 |
  | `0x0010` | "Flag NO Run / Run" | `+0xab \| 1` | 147 |
  | `0x0020` | "Flag NO DN Bless / Bless" | `+0xac \| 2` | 19 |
  | `0x0040` | "Flag NO See-Move / YES" | `+0xac \| 8`; exempts a source from the "Src NEAR" pickup | 57 |
  | `0x0080` | "Flag NO Hit-Link / YES" | `+0xac \| 0x10`; LINK flag 4 tests it | 4 |
  | `0x0100` | "Flag NO SHADOW / YES" | the record is skipped at load; **set in no bank**, and no event re-creates such a record (formerly read here as "dormant until triggered") | 0 |
  | `0x0200` | "Flag NO PosRand / YES" | `+0xad \| 1` | 75 |
  | `0x0400` | "Flag NO Follow / YES" | `+0xad \| 0x10` | 30 |
  | `0x0800` | "Flag NO Platf / YES" | `+0xad \| 8` | 7 |
  | `0x1000` | "Flag NO Fire / YES" | `+0xae \| 2` | 39 |
  | `0x2000` | "Flag YES Ami hit / NO" | `+0xae \| 4` | 35 |
  | `0x4000` | "Flag NO ManaCreat / YES" | `PART_SetEmitterActor` (`0x43b8aa`) makes the mana-particle spawn box follow this actor | 5 |
  | `0x8000` | "Flag YES Shock / NO" | `+0xaf \| 0x20` | 50 |
- `+0x38` `i32`: "Speed" → actor `+0x44` = v/64 (`0x41e377`). **[verified]**
- `+0x3C` `i32`: "Strenght", the hit strength: actor `+0x48`, ×2 above 256,
  100000.0 above 512 (`0x41e409`); `ENT_ApplyAttackHit` passes the
  attacker's `+0x48` to `ENT_ApplyDamage` as the damage (`0x4442b3`).
  **Not** a collision radius, as this page said before. **[verified]**
- `+0x40` `i32[3]`: spawn position `(x, y, z)` in scene units (negative Y is up).
- `+0x5C` `i32`: facing orientation / heading — a **12-bit fixed point angle** ($0 \dots 4095$ where $4096 = 360^\circ$ or $2\pi$).
- `+0x64` `i32`: movement/behavior selector copied to actor `+0x34`, with
  remapping and extra flags (5 → class 1, 6 → class 3). This alone does not
  establish faction or friendliness; see [ai-animation-runtime.md](ai-animation-runtime.md).
- `+0x68` `i32`: movement scale, converted to float at actor `+0x104` (default 16).
- `+0x6C` `i32`: turn step at actor `+0x108`, used by `ANIM_RequestState`.
- `+0x70` `i32`: actor `+0x10C`, the walker collision-sphere radius used by
  `PHYS_AttachActorCollider` (`0x40bedb`), also the attack facing-offset magnitude
  in `ENT_PlaceAtFacingOffset` (`0x442786`). Vitality is actor `+0x38`; dialogue
  event `0x40` gets its one-based entry ID from `LINKADVENT +0x1C`.
- `+0x74` .. `+0x98`: spawn stats copied into the actor by
  `ENT_InstantiateFromObjet` (`0x41e31d`–`0x41e400`), named by the editor;
  formerly misread here as animation state. **[verified]**

  | Offset | Editor label | Actor field |
  |---|---|---|
  | `+0x74` | "Sphere move" | `+0x114` (LINKADVENT action `0x20` sets 50000) |
  | `+0x78` | "Sphere see" | `+0x118` (action `0x40` sets 40000) |
  | `+0x7C` | "Masse" | `+0x250` (double), the mass in the physics step |
  | `+0x80` | "Courage" | `+0x188` = round(v·0.2) |
  | `+0x84` | "Defence" | `+0x190` |
  | `+0x88` | "Attack" | `+0x194` |
  | `+0x8C` | "Path" | `+0x198`, first BOX index `AI_FindPatrolBox` scans |
  | `+0x90` | "Speed Move" | `+0x180` = v/16 |
  | `+0x94` | "Shoot Impact /128" | `+0x58` = v/128 |
  | `+0x98` | "Scale" | every node scaled by v/32 |

The engine's debug HUD at `DBG_DrawObjectInfo` (`0x416606`) directly labels these fields:
`Project Name`, `Object Name`, `Object Pos`, `Object Speed`, `Object PHY Speed`,
`Object Flags`, `Object Angle`, `Object 3D Col`, `Object Anim 0`, `Object Anim 1`,
`Nombre d'objet`, `dernier objet`.

##### `LINK` fields (0x80 bytes) **[verified]**
- `+0x00` `char[12]`: link slot name (`LINK0` .. `LINK7`).
- `+0x0C` `char[12]`: destination project name (e.g. `Project134`).
- `+0x18` `u8`: flags, read by `SCENE_CheckExits` (`0x420b60`): 1 the
  editor's in-use bit (set in all 242 live links), 2 no living enemy left
  ("Flag NO EnemiDead"), 4 the actor touches an object with OBJET flag
  `0x80` ("Flag NO HitLink"; `0x420a44`), 8 invert the item test, `0x10` the
  level's trigger-completion flag ("Flag NO LinkAdvent"), `0x20` Ctrl
  pressed ("Flag NO Player Action"), `0x40` fire while the actor is
  **outside** the box ("Flag In Space / Out"; its own volume test at
  `0x420f22`–`0x4211b7`; set in no bank), `0x80` any actor may take it (else
  the player only; retail only). **[verified]** 2026-09-26, corrected
  2026-10-03
- `+0x24` `i32[3]`: bounding volume minimum `(minX, minY, minZ)`.
- `+0x30` `i32[3]`: bounding volume maximum `(maxX, maxY, maxZ)`.
- `+0x3C` `char[16]`: object that must be held (or, with flag 8, not held),
  e.g. `CLESOUFF.DAN`, `TALISMAN.DAN`, `O01FLUTE.DAN`. **[verified]**
Entering this axis-aligned 3D volume while the conditions hold triggers the
level transition to the target project; an all-zero volume (45 links) fires
on the conditions alone.

##### `BOX` fields (0x100 bytes) **[verified]**
- `+0x00` `char[12]`: box name (`BOX0` .. `BOX11`).
- `+0x0C` / `+0x18` `i32[3]`: volume min / max (editor "Box Pos Min/Max";
  the editor sorts them on save), read by `AI_FindPatrolBox` (XZ) and
  `ENT_ApplyZoneHazards` (3D). **[verified]**
- `+0x24` `i32[16][3]`: sequence of up to 16 3D waypoint coordinates `(x, y, z)`,
  recorded in the editor by walking the player and pressing Shift+4 at
  each point.
- `+0xE4` `i32`: index of the **last** point, −1 = none (the create default),
  16 = full; not a count. Retail readers disagree: kinds 2, 3, 6 and 7 loop
  `i < e4` and drop the last authored point, kinds 4 and 5 loop `i <= e4`,
  kind 1 draws `e4` segments. **[verified]**
- `+0xEC` `u32`: bit 0 in use (delete clears only this bit). **[verified]**
- `+0xF4` `i32`: intensity (editor "Mode 2 Phys Intensity"), read by the
  force-field, mana-pickup, prop, air-point and hazard-zone code
  (`0x41f034`, `0x4195f2`, `0x41893b`, `0x419296`, `0x420829`); down to −600
  in retail. **[verified]**
- `+0xF8` `i32`: written at run time by `PHYS_InitForceFields` (last force
  field index); non-zero in 92 shipped boxes because the editor saved the
  live record. **[verified]**
- `+0xF0` `i32`: path kind:
  - `0`: patrol regions/paths; `AI_FindPatrolBox` selects an active box by XZ bounds.
  - `1`: a force-field path (`PHYS_InitForceFields`, segments between the
    points; LINKADVENT actions switch it on or off), which also carries
    flight.
  - `2`: mana pickups; `3`: X01SOL props; `4`: random respawn points;
    `5`: re-entry points; `6`: air points; `7`: maintained spawn points;
    `8`: trigger zones; `9`: health-drain zones.

The 2–9 uses are traced from their readers in the name registry (2026-09-26);
they still need corpus and play validation. The kind-8 zone is separate from
the `LINKADVENT` condition/action table below.

##### `LINKADVENT` fields (0x40 bytes) **[verified]**
Each record is one rule of the level's event script: **if all selected
conditions hold, apply all selected actions**. `SCENE_InitTriggers`
(`0x4288c6`) copies the active records into 0x38-byte tasks and
`SCENE_TickTriggers` (`0x429061`) evaluates them every tick from
`GAME_Tick`. Conditions start true and are AND-ed; most actions clear the
task's active bit, so a rule fires once unless it has "Time Cycle".
**[verified]** 2026-10-03; this replaces the earlier "opcode / parameter"
reading of `+0x20`/`+0x24`.

- `+0x00` `char[12]`: slot name (`LINKADVENT0` .. `LINKADVENT15`).
- `+0x0C` `i32`: source `OBJET` ("Name Objet"), −1 = none.
- `+0x10` `i32`: source `BOX` ("Name Box"); −1 in every record of every bank
  and never read by the tick.
- `+0x14` `i32`: destination `OBJET` ("Link with Name Objet").
- `+0x18` `i32`: destination `BOX` ("Link with Name Box"; 6 retail records).
- `+0x1C` `i32`: one-based `DIALOG.DRD` entry ("Time Cut" in the July menu,
  where it was also the camera-cut length); when the rule fires without a
  camera action the runtime posts MGM `0x40` with `value - 1`. 166 retail
  records carry one, under 12 different condition masks; the 80 records
  formerly called "opcode `0x40`" are those whose only condition is
  "destination near". **[verified]**
- `+0x20` `u32`: condition bits (table below).
- `+0x24` `u32`: action bits (table below).
- `+0x28` `u32`: bit 0 in use; copied to the task as its active bit.
- `+0x2C` `char[16]`: cutscene movie ("Flag Element Dest HNM"), MGM `0x17`
  (11 entries, e.g. `AUTEL.HNM`, `ANGKOR.HNM`, `CASCADE.HNM`, `SHAMAN.HNM`,
  `GUARDIAN.UBB`).
- `+0x3C` `i32`: "Time", seconds; ×30 into a countdown in game ticks.

The source and destination indexes were written by the editor as positions
in its picker list, which equal the slot only when the used slots have no
gaps; in the July bank 24 of 189 references differ. How the engine reads
them is **[unverified]**.

Condition bits (`+0x20`), with the editor's labels and the retail test;
counts are disc 1 records. **[verified]**

| Bit | Editor label | Retail test | n |
|---|---|---|---|
| `0x0001` | "Flag Ele. Src NEAR" | player within 400 of the source; then the source is picked up (hidden, life 0) unless it has See-Move | 27 |
| `0x0004` | "Flag Ele. Src KILL" | source visible with life ≤ 0 | 21 |
| `0x0008` | "Flag All Ele Src KILL" | no living opposing fighter (`ENT_CheckSideCleared`) | 12 |
| `0x0010` | "Flag Time End" | the timer has run out (the rule then deactivates) | 115 |
| `0x0020` | "Flag Ele. Src In Invent." | the player holds the source's model | 41 |
| `0x0040` | "Flag Ele. Dest NEAR" | player within 400 (550 for spheres over 200) of the destination | 108 |
| `0x0080` | "Flag Time Cycle" | the timer has run out; it reloads | 0 |
| `0x0100` | "Flag Ele. Src SYM END." | July: the source's symbol animation ended; retail: source life equals its OBJET Life | 2 |
| `0x0200` | "Flag E. Src NOT In Invt." | the player does not hold the source | 34 |
| `0x0400` | "Flag Just One Freeze" | fewer than 3 hidden actors | 2 |
| `0x0800` | "Flag All UnFreeze" | fewer than 2 hidden actors | 1 |
| `0x1000` | "Flag Ele. D. NEAR src" | destination within range of the source | 14 |
| `0x2000` | — (retail only) | the player does not hold the destination | 4 |

Action bits (`+0x24`). **[verified]**

| Bit | Editor label | Retail effect | n |
|---|---|---|---|
| `0x00001` | "Flag Ele Dest OK" | destination shown at its spawn; a destination BOX gets its force fields. A destination of this action is **hidden at level load** until the rule fires (`SCENE_HideTriggerTarget` `0x428a50`, `SCENE_IsBoxUntriggered` `0x428b34`) | 89 |
| `0x00002` | "Flag all Ele Dest OK" | `ENT_RestoreRemoved` | 0 |
| `0x00004` | "Flag Ele Dest KILL" | destination hidden; a kind-1 destination BOX loses its force fields | 14 |
| `0x00008` | "Flag all Ele Dest KILL" | not implemented (July or retail) | 0 |
| `0x00010` | "Flag Ele Src KILL" | not implemented | 1 |
| `0x00020` | "Flag Ele Dest MOVE OK" | destination `+0x114` = 50000 (free to roam) | 19 |
| `0x00040` | "Flag Ele Dest SEE OK" | destination `+0x118` = 40000 | 5 |
| `0x00080` | "Flag A. Dest CHANGE" | destination changes side | 2 |
| `0x00100` | "Flag A. Dest Recharge" | destination recharged, life +10 | 0 |
| `0x00200` | "Flag all Dest Time Susp" | other actors' time scale 0 | 3 |
| `0x00400` | "Flag Ele Dest Near Src" | destination moved to the source | 30 |
| `0x00800` | "Flag all D. Time NO Susp" | time scales reset | 0 |
| `0x01000` | "Flag Camera Obj to Obj" | camera cut onto the destination, 90 frames | 5 |
| `0x02000` | "Flag Camera Pt to Obj" | camera cut from a point facing the destination | 25 |
| `0x04000` | "Link OK" | sets `0x6155e4` (`SCENE_IsTriggerDone`), opening LINK flag `0x10` exits | 44 |
| `0x08000` | "Flag A. Dest Follow" | destination follows (`+0xad \| 0x10`) | 3 |
| `0x10000` | "Flag A. Dest In INV" | destination's model added to the inventory | 16 |
| `0x20000` | "Light OK" | palette base +0x40 per channel | 1 |
| `0x40000` | — (retail only) | the player takes damage | 4 |

Common recipes on disc 1: "destination near" alone (59 records: talk to an
NPC, the dialogue is `+0x1C`); "time end" alone (43, timed narration);
"source near" → "dest OK" + "dest near source" (20, pick something up and
its reward appears in its place); "source in inventory" → "dest OK" (16, an
item unlocks something); "time end" → "Link OK" (13, the exit opens after a
delay).

Coordinates use **the scene's own axes** - `(x, y, z)`, up at negative Y.
Calibrated, not assumed: read that way, Project 0's `LINK0` box centres 165
units from `H18TETA1`, a face tower; read with the last component vertical it
is 910 units from anything.

**There is no `FLINK` or `DLINK`**, and no "type byte" after a key. Both came
from reading the *compressed* stream as if it were the format. The byte after
a name's terminator is a zero-run count, and so is the byte before the next
name - `F` is `0x46`, a run of seventy zeros. That is why 131 distinct bytes
appear in that position, and why `c4 09 00 02` looked like a three-byte integer
with a tag when it is just 2500.

### `.ANTI-VIR.DAT`

Not a game file. Central Point / Thunderbyte anti-virus checksum caches
(`Thunderbyte chec...`), accidentally mastered onto the disc from the developer
machines. Three copies exist in different directories. Ignore them entirely.
**[verified]**
