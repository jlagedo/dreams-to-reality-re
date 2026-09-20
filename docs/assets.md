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
| **Scenes / levels** | `DATA\3DC\*.DSN` | 98 | **157 MB** | **yes** | header decoded, body packed |
| **Animation** | `DATA\3DC\*.DAN` | 191 | 23.4 MB | **yes** | header decoded, body packed |
| **Models** | `DATA\3DC\*.3DC`, `*.3DM` | 40 | 1.3 MB | no | raw, sparse, partly readable |
| **Model archive** | `DATA\OBJET\*.PAK` | 2 | 62 KB | no | wraps `F3DC` chunks |
| **Sprites / fonts** | `*.SPR` | 16 | 0.9 MB | no | raw, palette + pixels |
| **Alpha maps** | `*.ALP` | 2 | small | no | raw, very sparse |
| **Icons** | `ICONE\ICONES.BF` | 1 + 4 old copies | 372 KB | no | `UBIK` container, 5 entries |
| **Animated textures** | 20 × `HNM4` | 20 | ~25 MB | yes | 256×256 video, FFmpeg-decodable |

There is **no standalone texture file anywhere on either disc.** That is the
central structural fact: level textures are inside the `.DSN` scene files.

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

24,592,952 bytes, and the reason it is copied by even the *minimum* install: the
engine needs all speech resident rather than streamed from CD. **[verified]**

```
char[4]   "DRDF"
u32       total file size   = 0x01774238 = 24,592,952   (exact, at offset 4)
u32       count             = 178
...       offset/size index
--- 0x2eb ---
RIFF WAVE × 178
```

Note the size field sits at offset **4** here, unlike `DSNF`/`DANF` which place
it at offset 5. The index layout between offset 12 and 0x2eb is not fully pinned
down — 747 bytes for 178 entries does not divide evenly, so there is either a
trailing block or a wider record. **[unverified]**

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

`ESSAI.3DM` stores what look like **16.16 fixed-point** values: `4669872 / 65536
= 71.257`, `4573748 / 65536 = 69.790`. Fixed point rather than float is the
expected choice for a 1997 software rasterizer targeting a Pentium. **[unverified]**
that these specific fields are coordinates.

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
u32       countA                    (813 / 782 / 906 across samples)
u16       nameCount                 (26 / 25 / 29)
--- 0x10 ---
char[11][nameCount]                 object names, null-padded, DOS FCB style
--- 0x12e for E01GROTT ---
          16.16 fixed-point values, then packed body
```

`nameCount` is confirmed exact: `E01GROTT.DSN` declares 26 and the table holds
exactly 26 records before the data begins.

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

**No texture files exist on either disc.** Not TGA, not PCX, not a texture
directory. The `DATA\TGA\` folder holds only 7 leftover JPEGs in a `TEMP\`
subdirectory — reference renders, not assets.

What this leaves:

1. **Level textures are packed inside the `.DSN` bodies.** 157 MB of packed data
   for 98 rooms, sitting right after a table of wall/floor/ceiling names, is
   textures plus geometry. Unpacking `.DSN` is the single highest-value target
   in the whole project.
2. **Animated textures are the 20 HNM4 videos** — 256×256, named for what they
   are (`E11_EAU`/`E12_EAU` water, `M05FEU_H` fire, `FD_SOUFL` bellows). These are
   decodable **today** with FFmpeg.
3. **UI and sprite art is separate and raw.** `.SPR`, `.ALP` and `.BF` all
   compress to 12-26%, so they are uncompressed.

### What is known about the raw sprite formats

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

`SOUR.ALP` is 84% zeros with a slow monotonic ramp — an alpha gradient table.

### `ICONES.BF` is not cracked

`UBIK` container, version 2, **5 entries**, 372,358 bytes. Entropy 5.20 and zlib
26% prove it is **raw imagery**, not compressed. Autocorrelation gives a clean
128-byte stride (and its 256-byte multiple), the strongest periodicity of any
file tested.

But rendering it at that stride as RGB555, RGB565 and 8-bit greyscale all produce
structured noise rather than recognisable icons. The container declares 5 entries,
so **the per-entry sub-headers carry the real dimensions and pixel format, and
guessing is the wrong approach** — parse them instead. Not yet done.

Four older generations of this file also ship (`.OLI`, `.BAK`, `OLD\.BAK`,
`OLD\.OLD`), which makes it an unusually good format-diffing target.

---

## Priority targets

1. **Unpack `.DSN`.** 157 MB, all the level geometry and textures, packed with an
   unknown scheme. Everything visual depends on this.
2. **Parse `ICONES.BF` sub-headers.** Smallest, fully-raw imagery with five
   versions available to diff — the easiest way to learn Cryo's image conventions
   before tackling `.DSN`.
3. **Extract the audio banks.** Both formats are decoded; this is an afternoon's
   scripting for 202 playable WAV files.
4. **`CUBE.ASC` known-plaintext attack on `F3DC`.** A 3D Studio ASCII export
   shipped on the disc; if a matching `.3DC` exists, the geometry format falls out
   of the diff. See [research-log.md](research-log.md).
5. **Check whether CryoLib's `GL_UnpackLZW` matches `.DSN`.** CryoLib carries an
   `LZWCRYO` signature and a full LZW API — but no `LZWCRYO` tag appears inside
   any `.DSN`, so if LZW is used it is headerless. **[unverified]**

## Scratch output

Test renders written during this analysis live in `E:\dev_game\_png\` — derived
data, deliberately kept out of this repo. Safe to delete.
