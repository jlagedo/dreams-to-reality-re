# Asset file formats

All magic numbers below were read directly off the discs. **[verified]**

Cryo used a consistent four-character tag convention, which is strong evidence of
a single shared chunk-IO layer under all asset loading.

## Magic number table

| Ext | Magic (ASCII) | Magic (hex) | Sample | Size | Contents |
|---|---|---|---|---|---|
| `.3DC` | `F3DC` | `46 33 44 43` | `ARC.3DC` | 22,112 | 3D geometry |
| `.3DM` | `F3DC` | `46 33 44 43` | `ESSAI.3DM` | 98,332 | **Different structure** — fixed 3×32 KB blocks, likely textures |
| `.DAN` | `DANF` | `44 41 4E 46` | `AR0.DAN` | 58,862 | **Character and prop models** + animation — see [models.md](models.md) |
| `.DSN` | `DSNF` | `44 53 4E 46` | `E01GROTT.DSN` | 1,771,734 | Scene / level |
| `.DRD` | `DRDF` | `44 52 44 46` | `DIALOG.DRD` | **24,592,952** | **Voice bank** — 178 WAVE clips (72.6% by size) plus 575 timed script lines |
| `.PAK` | `PAK0` | `50 41 4B 30` | `OBJET1.PAK` | 62,008 | Container of `F3DC` chunks |
| `.BF` | `UBIK` | `55 42 49 4B` | `ICONES.BF` | 372,358 | **Named-file container** — 267-byte table rows; disc 2 has 6 members, disc 1 five |
| `.UBB` | `UBB2` / `UBS2` | — | `ARENTRAD.UBB` | 1,715,124 | **Video** — HNM generation 5 |
| `.HNM` | `HNM4` / `HNM6` / `HNS6` | — | see `hnm-video.md` | — | Video |
| `.DIG` | `AIL3DIG` | `41 49 4C 33 44 49 47` | `SB16.DIG` | 2,853 | **Miles sound-card driver** |
| `.SPR` | none | — | `HI320.SPR` | 14,559 | **Sprite bundle** — 8-bit indexed + inline palette |
| `.ALP` | none | — | `SOUR.ALP` | 9,225 | Alpha map |
| `.ASC` | `Ambient light co...` | — | `CUBE.ASC` | 6,623 | **3D Studio ASCII export** |
| `.TGA` | standard Targa | `00 01 01` | `INSTALL2.TGA` | 8,870 | Colour-mapped Targa |
| `.ID` | plain text | — | `1CD.ID` | 5 | Disc marker, see `disc-layout.md` |
| `.BIN` | none | — | `REPLAY.BIN` | 316 | u32 array |

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

The largest structured data class on the discs: 98 files, **157 MB**.

The header is **fully decoded**, validated against **all 98 files**: **[verified]**

```
offset  0  u8[4]  "DSNF"
offset  4  u8     0x00
offset  5  u32    file size (unaligned)   — exact in 98/98
offset  9  u8     0x00
offset 10  u32    headerSpan  A           (38 .. 999)
offset 14  u16    objectCount B           (1 .. 32)
offset 16  char[11][B]   object-name table
offset 16+11B u32[5][B]  one 20-byte record per object
offset 16+31B ...        packed payload
```

**`A` is not an independent count.** In every one of the 98 files:

```
A = 31 * B + 7          body starts at 9 + A  ==  16 + 31*B
```

So the field is a **header span**, not a second count — it exists so the loader
can skip straight to the payload without walking the tables. Naming it "count A"
was a misreading.

Worked example, `E01GROTT.DSN`: `B` = 26, `A` = 813, body at `0x336`.

**[verified]** The offset comes from the loader, not from arithmetic.
`FUN_004175bc` in `WINDREAM.EXE` peeks 9, then 5, then 2 bytes of header, then
`B * 0xb` for the name table and `B * 0x14` for the records — **back to back,
with no gap**. An earlier pass read the body as starting at `24 + 31B` and
invented an 8-byte "scene-wide block" to account for the difference; there is no
such block, and every body-parsing attempt before this was starting 8 bytes
late. `body == 9 + A` is the same relation `.DAN` uses.

The 8-byte block and the 20-byte per-object records are confirmed present and
correctly sized in all 98 files; their **field meanings are [unverified]**.

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

**[verified]** `B` is exactly the number of name records — confirmed in 98/98.
Across all files there are **2,145 records, 1,642 distinct names**, 4–8 printable
characters each with 3–7 trailing NULs.

#### The 20-byte object records **[verified]**

Immediately following the $B \times 11$-byte name table at offset $16 + 11B$ are $B \times 20$-byte records, one per sub-object in the scene (2,145 records across 98 scenes):

- **Word 0 (`u32`)**: Allocation / relocation flag. When non-zero, holds `0x004741A0` (in 1,719 of 2,145 records), marking static world geometry loaded directly into memory arenas.
- **Word 1 (`u32`)**: Relocated address / runtime buffer offset (e.g. `0x0045C9xx`).
- **Word 2 (`u32`)**: Role / class ID — constant `3` across all 2,145 records on both discs.
- **Word 3 (`u32`)**: Compass normal / surface facing orientation. Directly correlates with French wall compass names: `0x202` for North walls (`MN`), `0x246` for East walls (`ME`), `0x286` for West walls (`MO`), `0x206` / `0x216` for slopes and diagonal slabs.
- **Word 4 (`u32`)**: Surface category and physics friction / footstep sound property (e.g. floor tile friction vs water vs stone).

#### The body is a chain of tagged records

**[verified]** The body is **not** an opaque blob. It is a record chain:

```
u8   tag
u32  size          <- includes this 5-byte header
...  payload       <- size - 5 bytes
```

Walking it reaches EOF **exactly** in 95/95 distinct scenes, zero slack. The
grammar came from `FUN_00417afd` in `WINDREAM.EXE`, which checks the tag, takes
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

#### What is left packed

Only tags 1 and 2: roughly 40 KB per scene against 1.7 MB of raw texture. Both
are genuinely packed — entropy 7.1–7.5, zlib -9 still needs 68–92%, and sizes do
not divide evenly by any small record width (only 19/95 are even divisible by 4).

Tag 1 contains a header object name in **90/95** files, plus `DEFAULT` (60/95)
and 3D Studio's `Object0` (21/95), so it holds the object/material directory.
Tag 2 contains an object name in **0/95** and looks more thoroughly packed.
Geometry is **[unverified]** but tag 1 is the likely home.

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
- **Header**: track count $N$ (matches scene-graph node count), table span $4*(N+1)$ at `+0x18`, relative offsets to tracks $0 \dots N-2$ at `+0x1C + 4*i`, total duration in frames at `+0x1C + 4*(N-1)`, and framerate (typically 10 fps). Track 0 begins at `+0x1C + 4*(N+1)`.
- **Track records**: 40-byte track header: duration at `+0x14`, key count $K$ at `+0x18`, interpolation type at `+0x1C` (1 = linear, 2 = Hermite spline), key start/end offsets at `+0x20`/`+0x24`, and rest unit quaternion $(0, 0, 0, 32768)$ at `+0x28`.
- **Keyframe layout**: each keyframe $k \in [0, K-1]$ is at `trk_off + 40 + k * stride` where `stride = (end_offset - start_offset) // K` (20 or 60 bytes). Word 0 is frame timestamp; words 1..4 are unit quaternion `[qx, qy, qz, qw]` in Q15 ($32768 = 1.0$). For stride 60, words 5..6 are curve flags, words 7..10 in-tangent quaternion, words 11..14 out-tangent quaternion. Verified across 10,127 tracks and 97,729 keyframes across all 159 models on both discs with 0 errors.
- **Engine evaluation**: evaluated at runtime via Slerp (`FUN_0045bf68`) or Hermite spline, converted to local $3 \times 3$ rotation matrix (`FUN_0045bc28`), and composed down the skeletal hierarchy (`FUN_0047e498` via `FUN_0045b86c`). All 780 clips across 159 models extracted to `E:\dreams-work\animations/`.

**[verified]** The payload is packed: entropy 7.338–7.819, only 0.65–3.42% zero
bytes. `AR0.DAN` body begins `01 0A 2C 00 00 30 1C 63 B4 00 FD FF 01 BE FF FF`;
`F07.DAN` begins `01 B4 52 00 00 30 1C 63 B4 00 FD FF 01 BE FF FF` — a shared
prefix past the first two bytes.

**[verified] No `.DAN` references a `.3DC` by name.** Across all 191 files there
are zero `F3DC` tags and zero `.3DC` filename strings, and no `.DAN` object label
matches a `.3DC` object label. The `DAN_Load3DC` symbol is real, so the
association must be made at runtime rather than stored in the asset.

Content reuse is heavy: nine byte-identical `CAISSE` animations, eight identical
`MCHAPO`, four identical `MCLEF`, plus aliases like `TABLEAU1`/`TABLO1` and
`TALISMA1`/`TALISMAN`. 191 physical files hold 159 distinct logical names.

## Notes per format

### `.3DC` / `.3DM` — geometry (`F3DC`)

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

**[verified]** in 16/16 files. The descriptor pairs are the way into the geometry:
`CARRE.3DC` holds `04 00 00 00 DD 00 00 00 06 00 00 00 7D 01 00 00` at `0xFC`.
First-pair counts across the 16 files are 3, 4, 6, 15, 26, 28, 44, 98; second-pair
counts are 3, 6, 12, 156, 216, 288, 576 — a spread consistent with vertex and
index counts. **[unverified]** that the pairs are vertices/indices specifically;
the target blocks do **not** decode as plain float32 or as simple u16/u32 indices.

Object labels are readable and French: `archer`, `fleche`, `sword`, `feu`, `pan`,
`cube`, `tri`.

Loaded by `DAN_Load3DC` (see [engine.md](engine.md)), which sits in the animation
module — animation data references geometry.

### `.3DM` — fixed three-block container

Every `.3DM` file is **exactly 98,332 bytes**: **[verified]** 8/8 physical copies.

```
0x00     char[4]  "F3DC"
0x04     u32      450
0x08     u32[5]   per-file header values, schema differs from .3DC
0x1C     0x8000 bytes   block 0
0x801C   0x8000 bytes   block 1
0x1001C  0x8000 bytes   block 2        -> 28 + 3*32768 = 98,332
```

There is **no material/object directory** in any `.3DM`. Only four exist:
`ESSAI`, `GRILLE`, `OMBRE2`, `SPRITE` — and each name also exists as a `.3DC`
material name, which suggests `.3DM` holds the *texture* for that material.

**[unverified] but well supported: the blocks are 128×128 RGB555 images.**
`128 × 128 × 2 = 32,768` exactly. Testing `ESSAI.3DM` as u16: blocks 1 and 2 have
the top bit set in **0 of 16,384 words** and block 0 in only 2.2% — precisely the
signature of RGB555's unused high bit. Distinct values per block are 945–1,134 out
of 16,384, the low colour diversity of a texture. This also matches the engine's
confirmed RGB555 format. Not yet rendered to an image for visual confirmation.

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
it must bundle voice audio or video. Notably it is one of only eight files copied
by even the *minimum* install (see `disc-layout.md`), meaning the engine needs it
resident on the hard disk rather than streamed from CD.

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
both formats documented above. Nothing about it is image-specific.

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

No magic. There are **two different `.SPR` families**, and neither is a raw
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

#### `DATA\FONT\` — `HI320` / `HI480` / `HI640` (3 files)

A different layout: a **16-entry RGB555 palette** in the first 32 bytes, then
glyph data, then a tail table of **8 × 28-byte descriptors** (absolute offset at
`+0x00`, width at `+0x08`, height at `+0x0C`), then an 8-byte footer
`[u32 data_end][u32 0x100]`. **[verified]** 3/3.

The names are display modes, not dimensions: `HI320`/`HI480`/`HI640` are the
320, 480 and 640-wide screen modes.

> **Correction.** `HI320.SPR` opening `1F 1C FF 7F` was previously read as a
> width/height header followed by white RGB555 pixels. It is not: `1F 1C` is
> palette entry `0x1C1F`, and `FF 7F` is entry `0x7FFF`. The file starts with its
> palette. The engine *is* RGB555 — that is independently confirmed by `bpp = 16`
> in every HNM6 header and by `0x3DEF3DEF` in `.3DC` — but not by these bytes.

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
list is probably load-order rather than a set.

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
flat asset list. Binary fields within a record are **[unverified]**.

> **Correction.** Describing this as "a monotonically increasing table of u32
> offsets" across the whole file was wrong. Only the first 151 entries are that
> table; everything past `0x400` is record payload, and reading it as offsets
> produces meaningless non-monotonic values.

**[verified]** The two discs' copies differ in exactly **six** project records —
P31, P41, P55, P69, P75, P87 — with identical embedded asset-name sets. Only
numeric fields and record lengths differ, and disc 1's copy is 44 bytes larger.
Neither is demonstrably authoritative; disc 1's is the safer default as the
program disc.

Joining these 150 records to `DREAMS.INI` gives the complete level map — see
[level-map.md](level-map.md).

#### The record codec and layout **[verified]**

Each of the 150 records is **zero-run compressed**: a nonzero byte is literal,
and `00 N` expands to `N` zero bytes. That is the engine's own loop,
`FUN_00448e25` in `WINDREAM.EXE`, applied per record by `FUN_00449bf9`. Every
record decompresses to exactly **`0x2200` bytes** - 150 of 150, which is the
validation: a wrong codec does not land on a constant.

```
+0x0000  header         0x200   starts "ProjectN" + environment, camera, lights, spawn
+0x0200  Link[8]        0x80    name[12], destination[12], ..., i32 min[3] @+0x24, i32 max[3] @+0x30
+0x0600  Objet[16]      0xc0    name[12], asset[32], flags @+0x34, i32 pos[3] @+0x40, heading @+0x5c, behavior @+0x64
+0x1200  Box[12]        0x100   name[12], i32 points[16][3] @+0x24, count @+0xe4, type @+0xf0
+0x1e00  LinkAdvent[16] 0x40    name[12], 8 x i32, asset[16] @+0x2c
```

An active slot starts with its family name; an all-zero slot is unused. Counts
over the corpus: `LINK` 244 (239 naming a project), `OBJET` 711, `BOX` 420,
`LINKADVENT` 328. `OBJET0` is the project's scene in 150 of 150. Decoded by
[`dreams.formats.project`](../src/dreams/formats/project.py).

##### Project Header fields (0x200 bytes) **[verified]**
Decompiled from `FUN_0041f9db` and `FUN_0041deb8` in `WINDREAM.EXE`:
- `+0x000` `char[16]`: project identifier (e.g. `Project0\0`).
- `+0x018` `i32[3]`: Directional light 1 orientation vector `(x, y, z)`.
- `+0x024` `i32[3]`: Directional light 2 orientation vector `(x, y, z)`.
- `+0x030` `i32[3]`: Ambient light RGB components (values in $0 \dots 255$, e.g. `(152, 168, 126)`).
- `+0x03C` `char[32]`: Primary animated video filename (`.HNM` or `.UBB`, e.g. `ETE_E~1.HNM`, `CASC2.HNM`).
- `+0x05C` `char[32]`: Secondary animated video filename (e.g. `M01DRA.HNM` in Project 12).
- `+0x06C` `char[32]`: Target scene material name receiving primary video texture (e.g. `F02_EAUP`).
- `+0x08C` `char[32]`: Target scene material name receiving secondary video texture (e.g. `M01DRA`).
- `+0x09C` `i32`: Camera projection mode.
- `+0x0A0` `i32`: Camera near clip plane distance / height.
- `+0x0A4` `i32`: Camera Field of View in degrees (typically 63–65°).
- `+0x0B4` `i32[3]`: Player canonical spawn coordinates `(x, y, z)` in scene units (negative Y is up).
- `+0x0E0` `i32[4]`: Depth fog parameters: start distance, end distance, density, fog color.
- `+0x0F0` `i32[4]`: Clear color / Sky color RGB components.
- `+0x10C` `i32`: Player canonical spawn heading (12-bit angle, $0 \dots 4095 \equiv 360^\circ$).
- `+0x138` `i32`: Lighting mode: `0` = Day (positive ambient bias `+0x40`/`+0x80`), `1` = Night (dark negative bias `0xFFFFFFC0`/`0xFFFFFF80`).
- `+0x1F8` `i32`: Redbook CD audio track number (matches audio tracks 2..14).

##### `OBJET` fields (0xC0 bytes) **[verified]**
Decompiled from `FUN_0041deb8` (entity instantiation) and `FUN_00416606` (the engine's developer debug HUD):
- `+0x00` `char[12]`: slot name (`OBJET0` .. `OBJET15`).
- `+0x0C` `char[16]`: asset filename (`.DSN` scene for slot 0; `.DAN` character or `.3DC` prop for slots 1..15).
- `+0x1C` `char[16]`: secondary instance identifier or label.
- `+0x34` `u16`: entity bitfield flags:
  - bit 0 (`0x01`): active / spawn immediately on level entry.
  - bit 1 (`0x02`): dynamic character / creature entity (`XH_.DAN`, `F07BLEU.DAN`, `CH0.DAN`).
  - bit 8 (`0x0100`): dormant / disabled entity (kept inactive until triggered by adventure script).
  - bit 14 (`0x4000`): triggers special AI initialization routine (`FUN_0043b8aa`).
- `+0x3C` `i32`: bounding / collision radius (scaled by the engine if $> 256$ or $> 512$).
- `+0x40` `i32[3]`: spawn position `(x, y, z)` in scene units (negative Y is up).
- `+0x5C` `i32`: facing orientation / heading — a **12-bit fixed point angle** ($0 \dots 4095$ where $4096 = 360^\circ$ or $2\pi$).
- `+0x64` `i32`: AI / behavior archetype:
  - `1`: static obstacle or prop.
  - `3`: hostile creature / attack behavior (`CH0.DAN`, `F59.DAN`).
  - `5`: friendly NPC / patrol behavior (`F07BLEU.DAN`, `F07ORIG.DAN`).
  - `6`: aerial waypoint flight behavior.
- `+0x68` `i32`: movement speed / velocity multiplier (default `16.0f`).
- `+0x6C` `i32`: waypoint route target index (indexes into `BOX0` .. `BOX11`).
- `+0x70` `i32`: health / hitpoints or dialogue bank speech index.
- `+0x74` .. `+0x98`: animation playback state, secondary action timers, and sub-object visibility masks.

The engine's debug HUD at `FUN_00416606` directly labels these fields:
`Project Name`, `Object Name`, `Object Pos`, `Object Speed`, `Object PHY Speed`,
`Object Flags`, `Object Angle`, `Object 3D Col`, `Object Anim 0`, `Object Anim 1`,
`Nombre d'objet`, `dernier objet`.

##### `LINK` fields (0x80 bytes) **[verified]**
- `+0x00` `char[12]`: link slot name (`LINK0` .. `LINK7`).
- `+0x0C` `char[12]`: destination project name (e.g. `Project134`).
- `+0x24` `i32[3]`: bounding volume minimum `(minX, minY, minZ)`.
- `+0x30` `i32[3]`: bounding volume maximum `(maxX, maxY, maxZ)`.
Entering this axis-aligned 3D volume triggers the level transition to the target project.

##### `BOX` fields (0x100 bytes) **[verified]**
- `+0x00` `char[12]`: box name (`BOX0` .. `BOX11`).
- `+0x24` `i32[16][3]`: sequence of up to 16 3D waypoint coordinates `(x, y, z)`.
- `+0xE4` `i32`: number of points used ($0 \dots 16$).
- `+0xF0` `i32`: path kind:
  - `0`: ground patrol routes (used by walking NPCs like gnomes).
  - `1`: aerial flight waypoints (used by floating/flying creatures).

##### `LINKADVENT` fields (0x40 bytes) **[verified]**
- `+0x00` `char[12]`: advent slot name (`LINKADVENT0` .. `LINKADVENT15`).
- `+0x14` `i32`: target `OBJET` slot index ($0 \dots 15$) bound to this adventure event condition.
- `+0x1C` `i32`: quest progression / storyline milestone stage ($0 \dots 176$).
- `+0x20` `i32`: event condition opcode (e.g. proximity trigger, item delivery, interaction).
- `+0x24` `i32`: action parameter / destination event.
- `+0x2C` `char[16]`: cutscene video filename (11 entries carry a `.HNM`/`.UBB` cutscene movie, e.g. `AUTEL.HNM`, `ANGKOR.HNM`, `CASCADE.HNM`, `SHAMAN.HNM`, `GUARDIAN.UBB`).

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
