# Models, textures, animation, sound

Where the engine's actual content lives, how it is packed, and what is still
unknown. All measurements taken directly from the discs. **[verified]** unless
marked otherwise.

## Summary

| Asset | Lives in | Files | Volume | Packed? | Status |
|---|---|---|---|---|---|
| **Sound effects** | `DATA\SOUND\FSB.DAT` | 1 bank / 24 clips | 741 KB | no | **format fully decoded** |
| **Voice** | `DATA\3DC\DIALOG.DRD` | 1 bank / 178 clips | 23.5 MB | no | **format fully decoded** |
| **Music** | redbook CD audio tracks | 11 + 13 tracks | ~700 MB | n/a | mount the `.cue` |
| **Scenes / levels** | `DATA\3DC\*.DSN` | 98 | **157 MB** | ~3% | **header + record chain + textures decoded**; only tags 1-2 packed |
| **Models + animation** | `DATA\3DC\*.DAN` | 191 | 23.4 MB | **yes** | **solved** — 159 models, 1,886 parts, 41,614 triangles, texture pages; animation partly read. [models.md](models.md) |
| **Props / weapons** | `DATA\3DC\*.3DC` | 32 | ~0.5 MB | no | **geometry solved** — raw `F3DC`, same node, 165 nodes over 16 files; only `ARC` still open |
| **Shading LUTs** | `DATA\3DC\*.3DM` | 8 | 0.8 MB | no | 3×32 KB blocks — **not** textures, see below |
| **Model archive** | `DATA\OBJET\*.PAK` | 2 | 62 KB | no | one `F3DC` chunk at `0x0C` |
| **Sprites / fonts** | `*.SPR` | 16 | 0.9 MB | no | **pixel data decoded** — indexed sheets, 256-glyph fonts, and menu `TABLE` sprites |
| **Menu/cursor sprites** | `ICONES.BF` members, `OBJET\SOUR.ALP` | 5/6 banks + 2 cursor copies | ~0.5 MB | no | **decoded** — indexed colors with per-pixel blend; `.ALP` is not an alpha-map format |
| **Icons** | `ICONE\ICONES.BF` | 1 + 4 old copies | 372 KB | no | **fully decoded** — named-asset container |
| **Video** | 113 × HNM4/5/6 | 113 | ~300 MB | yes | **all decodable** — see `hnm-video.md` |

There is no standalone texture file anywhere on either disc — **because the
level textures are inside the `.DSN` scene files, and they are now decoded.**
Each object owns a 256-entry RGB565 palette plus 64 distinct 32x32 8-bit tiles,
stored uncompressed as fixed-size records. See
[file-formats.md](file-formats.md); extract with
`uv run dreams extract --only leveltex`.

---

## Sound — solved

### `FSB.DAT` — sound effects bank

741,370 bytes. Format completely decoded: **[verified]**

```
char[12]      "DREAMS FSB  "
u32           count               = 24
u32[count]    block sizes         (55938, 31982, 63790, 8022, 9194, ...)
--- 0x70 ---
RIFF WAVE     × count, back to back
```

Header is `12 + 4 + 24*4 = 112 = 0x70`, which is exactly where the first `RIFF`
sits. Each stored size is the RIFF payload + 8 (the `RIFF` tag and its length
field). RIFF payloads account for 99% of the file — no padding, no compression.

Every clip: **PCM, mono, 11025 Hz, 16-bit** (`codec=1`).

Extraction is trivial — walk the size table and `open(f'{i}.wav','wb').write(...)`.

### `DIALOG.DRD` — voice bank

24,592,952 bytes, and copied by even the *minimum* install. **[verified]** The
retail loader keeps the offset table and a reusable entry buffer, then seeks
and reads a requested entry on demand; it does not preload the full bank into
memory.

```
char[4]   "DRDF"
u32       total file size   = 0x01774238 = 24,592,952   (exact, at offset 4)
u32       count             = 178
0x014  u32[178] packed entry offsets
--- 0x2eb ---
entry records × 178: WAVE, timed text, optional auxiliary block
```

The size field sits at offset **4**, unlike `DSNF`/`DANF` which place it at
offset 5. Each offset word packs a 24-bit offset field in its upper three
bytes and a low byte of unknown purpose. The parser reconstructs absolute
positions by detecting wraps of that 24-bit field; all 178 records then abut
exactly through EOF. At runtime `FUN_00410928` reads one selected record,
`FUN_00446e01` submits its WAVE data to the sound buffer, and the same entry's
589 total text lines are drawn on the timed-caption path. See
[`sprites-ui-dialog.md`](sprites-ui-dialog.md).

Every clip: **PCM, mono, 11025 Hz, 8-bit**.

So voice is 8-bit and sound effects are 16-bit — the usual 1997 trade, spending
the bit depth on effects and the volume on dialogue.

### What is *not* audio content

`DATA\SOUND\*.DIG` are **Miles Sound System sound-card drivers** (`AIL3DIG`
magic), not samples. See [file-formats.md](file-formats.md).

---

## Models

### `.3DC` / `.3DM` — `F3DC`

Only 40 files totalling 1.3 MB — these are **shared props and system models**,
not level geometry. `SETUP.INI`'s maxi-install list names them plainly: `ARC`
(bow), `EPEE` (sword), `GUN`, `BOULE` (ball), `CARRE` (square), `MANA`,
`OMBRE`/`OMBRE2` (shadow), `PARTICL2`, `GRILLE` (grid), `SPRITE`, `ESSAI` (test).

```
char[4]   "F3DC"
u32       450            version — constant across every sample
...
```

Entropy 2.55, zlib 22%, 74% zeros → **raw and sparse**, built from fixed-size
records with generous null padding.

`ARC.3DC` carries readable material-name slots, null-padded:

```
DEFAULT ... GRILLE ... grille ... archer ... fleche ... GRILLE
```

Uppercase and lowercase variants of the same word appear — plausibly material
name versus texture name. At offset 0x4c sits `ef 3d ef 3d` = **0x3DEF twice**,
which in RGB555 is mid-grey (R=G=B=15) — a default material colour, and
independent confirmation of the RGB555 colour format.

`ESSAI.3DM` stores `4669872` and `4573748` in fixed header slots. Reading them
as **16.16 fixed-point** coordinates (`71.257`, `69.790`) was **wrong**. In hex
they are `0x004741B0` and `0x0045CA34`, and the byte-identical values recur
across `.3DM`, `.DSN` *and* `.DAN` — `0x004741A0` appears in all three. A
coordinate cannot be bit-identical in a shadow LUT, an animation and a cave.
These are **stale pointers** into a `0x400000`-based address space, written out
by the exporter and fixed up at load. **[verified]**

### `.PAK` — `PAK0`

```
char[4]   "PAK0"
u32       62004          = file size - 4
u32       ?
--- 0x0C ---
"F3DC" ...               embedded model chunk
```

A thin container wrapping `F3DC` chunks. 78% zeros, zlib 18%. `OBJET1.PAK`
contains a string `Fem10` — a character model name.

Parsing `PAK0`'s index is the cheapest entry point into `F3DC`, since it gives
you chunk boundaries for free.

---

## Animation — `.DAN`

**191 files, the most numerous asset class**, 23.4 MB. Against only 40 model
files, so animation is the primary per-object unit and geometry is heavily reused.

```
char[4]   "DANF"
u8        0x00
u32       file size              (58862 for AR0.DAN — exact)
...
0x10      char[11]  "YARAIN1"    object name, null-padded
...
          u16 frame count = 5
          null-terminated source filenames:
            F28AN000.3DA  F28AN002.3DA  F28AN004.3DA
            F28AN016.3DA  F28AN050.3DA
--- 0x5e ---
          packed payload
```

Two things matter here.

**`.3DA` files do not exist anywhere on either disc.** The `.DAN` embeds its
animation frames and retains the original authoring filenames as labels. The
numbering (`000, 002, 004, 016, 050`) is sparse, so these are **keyframes**
selected out of a longer authored sequence.

**The payload is packed.** Entropy 7.64, zlib 88%, only 1% zeros — this is
compressed or tightly bit-packed data, unlike the sparse `.3DC` files.

`AR0.DAN`, `AR1.DAN` and `AR3.DAN` are byte-identical in size and header —
variants sharing a skeleton.

Also present: material references `DEFAULTP`, `yarain`, `bass`.

---

## Scenes — `.DSN`, where the levels actually are

**98 files, 157 MB — the largest non-video asset class by far.** A typical scene
is 1.8 MB.

```
char[4]   "DSNF"
u8        0x00
u32       file size                 (1,771,734 for E01GROTT — exact)
u8        0x00
u32       headerSpan A              (38 .. 999)   A = 31*nameCount + 7
u16       nameCount                 (1 .. 32)
--- 0x10 ---
char[11][nameCount]                 object names, null-padded, DOS FCB style
--- 0x10 + 11*n ---
u32[5][nameCount]                   20-byte record per object
--- 0x10 + 31*n  ( == 9 + A ) ---
          packed body
```

`nameCount` is confirmed exact in all 98 files, and `A` turned out to be a
**derived header span**, not a second count: `A = 31·nameCount + 7` holds in
98/98, putting the body at `9 + A == 16 + 31·nameCount` — confirmed against the
loader in `WINDREAM.EXE`. For `E01GROTT.DSN` that is `0x336`. Full detail in
[file-formats.md](file-formats.md).

### The naming scheme decodes to room construction

`E01GROTT.DSN` (grotte = cave) lists:

```
E01_ME1..ME4   E01_MN1..MN4   E01_MO1..MO4   E01_MS1..MS4
E01_P1         E01_SOL1..SOL9
```

Read as French: **`M` + compass letter** — `ME` = Mur Est, `MN` = Mur Nord,
`MO` = Mur Ouest, `MS` = Mur Sud (east/north/west/south walls), `SOL` = floor,
`P` = plafond (ceiling). Four walls × four segments, nine floor tiles, one
ceiling. **[unverified]** but the fit is hard to argue with, and `E02ARAI0.DSN`
is consistent (`E02_COL1..COL4` = colonnes/columns, `E02_BAS` = base,
`E02_CENT` = centre).

So a scene is a room assembled from named directional surface pieces.

### The body is packed

A 64 KB-bucket map across all 28 buckets of `E01GROTT.DSN` is flat: every bucket
reports 12-15% zeros and 31-33% printable-range bytes. Entropy 6.43, zlib 72%.

For comparison the `INTRO.HNM` video — known-compressed — measures **69%**, and
raw control files (`.TGA`, `.3DC`, `.SPR`) all sit at 18-31%.

**The `.DSN` body is therefore packed, at a density comparable to compressed
video.** It is homogeneous end to end, with no distinct raw sections. Autocorrel-
ation finds no image stride, only small power-of-two record alignment (32/64/128
bytes).

This is where the level textures are, and they are not stored raw.

---

## Textures — the honest position

**No conventional texture files exist on either disc.** Not TGA, not PCX, not a
texture directory. The `DATA\TGA\` folder holds only 7 leftover JPEGs in a
`TEMP\` subdirectory — reference renders, not assets.

**The `.3DM` texture hypothesis was tested and killed.** Rendered as 128×128
RGB555 it is noise; the files are shading lookup tables, and all four names are
`.3DC` material names including `OMBRE` (shadow).

**They are not RGB555 data either.** The support for that reading was a clear
unused top bit in `ESSAI.3DM`, and it does not generalise: across all four
files **8 of the 12 blocks have bit 15 set** — `GRILLE` at 5,202/3,385/3,208
words and `SPRITE` at 5,297/4,150/6,621, against 366/0/0 for `ESSAI` and
32/0/0 for `OMBRE2`. The two clean blocks are the ones that were sampled. So
the PNGs the `tiles` group writes are a diagnostic rendering of bytes, not an
image in any format we have established. **[verified]**

What this leaves:

1. **Level textures live inside the `.DSN` bodies, and are now decoded.** Each
   object owns one **256×256** 8-bit surface interleaved from 64 subsampled
   32×32 planes, indexing a 256-entry **RGB565** palette. Uncompressed, and
   about 97% of a scene file by volume. Extract with
   `uv run dreams extract --only leveltex`; full layout in
   [file-formats.md](file-formats.md).
2. **Animated textures are the 20 HNM4 videos** — 256×256, named for what they
   are (`E11_EAU`/`E12_EAU` water, `M05FEU_H` fire, `FD_SOUFL` bellows). These —
   and now **all 113 video files**, including the HNM6 cutscenes — are decodable
   today. See [hnm-video.md](hnm-video.md).
3. **UI and sprite art is separate and raw.** `.SPR`, `.ALP` and `.BF` all
   compress to 12-26%, so they are uncompressed.

### The raw sprite formats — now decoded

The palette inference below was right, and parsing outward from it cracked the
whole format. Full layout in [file-formats.md](file-formats.md): a 256-entry
6-bit palette, a 256-entry pointer table at `0x400`, then records of
`u32 width, u32 height, 2×u32, 8-bit indices`, padded to `round4(16 + w*h)`.
**[verified]** across all 232 payload records.

The three `DATA\FONT\` files use a different layout: a 256-entry RGB555
palette, indexed glyph bitmaps, and a tail table of 256×28-byte descriptors.
The final `u32` is the glyph count (`256`). The retail loader confirms that all
256 codepoints are addressable; `HI320.SPR` has one malformed-looking
descriptor at `%` (code 37), while the corresponding `HI480` and `HI640` glyphs
are valid.

Original evidence, which still stands:

`ALPHABET.SPR` opens with 4-byte records whose values never exceed 0x3F:

```
3f 00 00 00 | 3d 3c 3f 00 | 3c 39 3e 00 | 3a 36 3e 00 | 38 33 3d 00
```

That is a **6-bit VGA palette in RGBX records** (0-63 per channel), the standard
DAC format — so at least some sprite assets are 8-bit paletted with an inline
palette.

`HI320.SPR` (the `HI` prefix and `320` suggesting hi-color assets for the 320×200
mode) instead opens with a descending **RGB555 grey ramp**: `0x7FFF, 0x7FFF,
0x7BDE, 0x4210, 0x3DEF, 0x35AD, 0x318C, 0x2D6B, 0x2529, 0x2108, 0x18C6, 0x14A5`.

`SOUR.ALP` is the two-frame 16×24 mouse cursor, using the same palette-index
plus opacity layout as the menu `.ALP` banks.

### `ICONES.BF` — cracked

The guess-the-stride approach was the wrong one, as suspected. Parsing the
container instead solves it outright: `UBIK` is a **named-asset container** with a
267-byte record per entry (259-byte filename, u32 offset, u32 length) in a table
at the end of the file. Full layout in [file-formats.md](file-formats.md).

**[verified]** across all five generations. The members are ordinary `.SPR` and
`.ALP` files — both now decoded — so `.BF` carries no image format of its own.

| Generation | Size | Entries | Difference |
|---|--:|--:|---|
| D1 `ICONES.BF` | 372,358 | 5 | no `TITRES.SPR` |
| D2 `ICONES.BF` | 440,029 | 6 | shipping version |
| D2 `ICONES.BAK` | 440,029 | 6 | byte-identical to the shipping `.BF` |
| D2 `ICONES.OLI` | 452,435 | 6 | larger `INTERF.ALP` |
| D2 `OLD\ICONES.BAK` | 460,203 | 6 | larger `MAGIE.ALP` and `INTERF.ALP` |
| D2 `OLD\ICONES.OLD` | 431,423 | 7 | also has `MENU.ALP`; smaller `INTERF.ALP` |

So the generations differ by **which assets they contain**, not by container
version — and an earlier `MENU.ALP` was dropped before release.

---

## Priority targets

1. **Resolve multi-run vertex references.** The `.DSN` body is fully decoded
   and 4 of 95 scenes export to glTF; the other 91 split their vertex
   references across several runs with unrelated bases and nothing yet maps a
   run to its place in the pool. This is the single blocker on every remaining
   level. See [scene-geometry.md](scene-geometry.md).
2. **Finish the `.3DC` mesh decode.** `BOULE` verifies as a sphere (94 vertices
   on a shell, radius SD 2.6% of mean) and `EPEE` as a sword, but `BOULE` has
   18 boundary edges, `CARRE` does not decode as a box, and `ARC`/`GUN` use an
   unsolved compact UV variant.
5. **Try NihAV's Cryo archive reader** (`src/input/archives/cryo.rs`) against
   `.PAK`, `ICONES.BF` and `DREAMS.DAT`. Untested, and it already understands a
   Cryo "BigFile" layout.
6. **Check whether CryoLib's `GL_UnpackLZW` matches `.DSN`.** CryoLib carries an
   `LZWCRYO` signature and a full LZW API — but no `LZWCRYO` tag appears inside
   any `.DSN`, so if LZW is used it is headerless. **[unverified]**

> **Dropped from this list:** parsing `ICONES.BF` (done) and the `CUBE.ASC`
> known-plaintext attack, which was **tried and failed** — see
> [research-log.md](research-log.md).

## Scratch output

Test renders written during this analysis live in `E:\dev_game\_png\` — derived
data, deliberately kept out of this repo. Safe to delete.
