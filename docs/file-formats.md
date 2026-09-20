# Asset file formats

All magic numbers below were read directly off the discs. **[verified]**

Cryo used a consistent four-character tag convention, which is strong evidence of
a single shared chunk-IO layer under all asset loading.

## Magic number table

| Ext | Magic (ASCII) | Magic (hex) | Sample | Size | Contents |
|---|---|---|---|---|---|
| `.3DC` | `F3DC` | `46 33 44 43` | `ARC.3DC` | 22,112 | 3D geometry |
| `.3DM` | `F3DC` | `46 33 44 43` | `ESSAI.3DM` | 98,332 | **Same container as `.3DC`** |
| `.DAN` | `DANF` | `44 41 4E 46` | `AR0.DAN` | 58,862 | Animation |
| `.DSN` | `DSNF` | `44 53 4E 46` | `E01GROTT.DSN` | 1,771,734 | Scene / level |
| `.DRD` | `DRDF` | `44 52 44 46` | `DIALOG.DRD` | **24,592,952** | Dialog bundle |
| `.PAK` | `PAK0` | `50 41 4B 30` | `OBJET1.PAK` | 62,008 | Container of `F3DC` chunks |
| `.BF` | `UBIK` | `55 42 49 4B` | `ICONES.BF` | 372,358 | Icon/bitmap bundle |
| `.UBB` | `UBB2` / `UBS2` / `HNM6` | — | `ARENTRAD.UBB` | 1,715,124 | UBIK presentation bundle |
| `.HNM` | `HNM4` / `HNM6` / `HNS6` | — | see `hnm-video.md` | — | Video |
| `.DIG` | `AIL3DIG` | `41 49 4C 33 44 49 47` | `SB16.DIG` | 2,853 | **Miles sound-card driver** |
| `.SPR` | none | — | `HI320.SPR` | 14,559 | Raw RGB555 sprite |
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

After the 9-byte preamble:

```
offset  9  u8     0x00
offset 10  u32    count A     (813 / 782 / 906 in samples)
offset 14  u16    count B     (26 / 25 / 29 in samples)
offset 16  ...    name table
```

The name table is **11-byte fixed-length records**, null-padded — the classic DOS
FCB 8+3 filename field, used here for object names: **[verified]**

```
E01GROTT.DSN:  E01_ME1  E01_ME2  E01_ME3  E01_ME4  E01_MN1  E01_MN2  E01_MN3 ...
E02ARAI0.DSN:  E02_25   E02_BAS  E02_CENT E02_COL1 E02_COL2 E02_COL3 E02_COL4 ...
E03ARAI1.DSN:  E03_B1   E03_B2   E03_CH   E03_COL1 E03_COL2 E03_COL3 E03_COL4 ...
```

Names carry the level prefix and a role suffix (`COL` = colonne/column, `BAS` =
base, `CENT` = centre, `ME`/`MN` = mesh types?). `count B` is plausibly the number
of name records. **[unverified]**

Parsing this table is the obvious first step into the scene format.

## Notes per format

### `.3DC` / `.3DM` — geometry (`F3DC`)

Both extensions share the `F3DC` tag, so `.3DM` is not a separate format —
likely a naming convention for a subtype (model vs chunk). Header after the tag:

```
ARC.3DC    46 33 44 43  C2 01 00 00  00 00 00 00  00 00 00 00
ESSAI.3DM  46 33 44 43  C2 01 00 00  B0 41 47 00  34 CA 45 00
```

`C2 01` = 450 appears in both at offset 4 — a version or record-count field that
is constant across samples. The `.3DM` sample carries non-zero data at offsets 8
and 12 where the `.3DC` sample has zeros. **[unverified]** interpretation.

Loaded by `DAN_Load3DC` (see `engine.md`), which is in the animation module —
animation data references geometry.

### `.PAK` — geometry container (`PAK0`)

`OBJET1.PAK` begins `PAK0`, then at offset 16 contains a literal `F3DC` tag. So
`.PAK` is an archive wrapping multiple `F3DC` chunks. Parsing the `PAK0` index
is the cheapest route into the geometry format. **[verified]** that it embeds
`F3DC`; **[unverified]** as to index structure.

### `.DRD` — dialog (`DRDF`)

`DATA\3DC\DIALOG.DRD` is **24.6 MB**. That is far too large for dialog text, so
it must bundle voice audio or video. Notably it is one of only eight files copied
by even the *minimum* install (see `disc-layout.md`), meaning the engine needs it
resident on the hard disk rather than streamed from CD.

### `.BF` — `UBIK` bundle

`ICONES.BF` starts with the ASCII tag `UBIK`. "UBIK" is also the name of a Cryo
game (a demo of it ships on disc 1) and appears to double as the name of an
internal Cryo toolkit, since `.UBB` files use the related `UBB2`/`UBS2` tags and
disc 2 ships `PLAYUBB.EXE` as a standalone player. **[unverified]** that UBIK is a
shared toolkit rather than a coincidence.

Header: `55 42 49 4B  02 00 00 00  4F A9 05 00  05 00 00 00` — tag, version 2,
a size-like u32, then count 5.

### `.UBB` — UBIK presentation bundle

Three different magics appear across `.UBB` files: `UBB2`, `UBS2` and `HNM6`.
The `B`/`S` pairing mirrors the `HNM`/`HNS` pairing in the video files, which
suggests the letter encodes a variant (see `hnm-video.md`). Some `.UBB` files are
simply HNM6 videos with a different extension. `SETUP.INI` registers
`demos2\playubb.exe` as the "DemoPlayerUbb", so `.UBB` is the format Cryo used
for the interactive demo slideshows on disc 2.

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

### `.SPR` — sprites, RGB555

No magic. `HI320.SPR` begins `1F 1C` then a run of `FF 7F` = 0x7FFF, which is
white in RGB555. This pins the engine's colour format at **16-bit hi-color
RGB555** and is corroborated by `bpp = 16` in every HNM6 header. **[verified]**

`DATA\OBJET\` holds `ALPHABET.SPR` and `ALPHABE2.SPR` (bitmap fonts),
`OBJET0.SPR`, `PARTICLE.SPR` and `PARTICL2.SPR` (particle systems). `DATA\FONT\`
holds three more `.SPR` files including `HI320.SPR` — the `HI` prefix and `320`
suggest hi-color assets for the 320x200 mode.

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

`DREAMS.DAT` (138,879 B on disc 1, 138,835 B on disc 2) is a binary table of
little-endian u32 offsets beginning at 0: `00000000 00000844 00000E74 00001280
0000157E ...`. Monotonically increasing, so it is an index into a companion blob.
**[unverified]** which blob it indexes, and which disc's copy is authoritative.

### `.ANTI-VIR.DAT`

Not a game file. Central Point / Thunderbyte anti-virus checksum caches
(`Thunderbyte chec...`), accidentally mastered onto the disc from the developer
machines. Three copies exist in different directories. Ignore them entirely.
**[verified]**
