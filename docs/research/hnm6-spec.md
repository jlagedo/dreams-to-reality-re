# HNM6 format specification

Mirrored from [MultimediaWiki: HNM6](https://wiki.multimedia.cx/index.php/HNM6)
(last edited 13 October 2012). Reproduced here so the project does not depend on
an external wiki staying up. **[sourced]** — all content in this file is the
wiki's, not original research.

For what is actually on the *Dreams to Reality* discs, see [hnm-video.md](hnm-video.md).

- Extension: `hnm`
- Company: CRYO Interactive Entertainment
- Categorised by the wiki under "Formats missing in FFmpeg"

HNM6 is the latest variant of the HNM video format by Cryo. Unlike previous
versions it supports hi-color video.

> **Note for this project:** the discs here predominantly use the signature
> `HNS6` rather than the documented `HNM6`. See `hnm-video.md`.

---

## Container

File consists of a main header followed by frame chunks. Each frame chunk
consists of individual audio and video chunks. **All numbers are little-endian.**

### Main header (64 bytes)

```
u8   sig[4]           -- file signature, "HNM6"
u8   reserved[2]      -- usually 0
u8   audioflags       -- nonzero indicates APC sound (not authoritative)
                            bit 7    : stereo
                            bits 6..5: frequency in units of 11025 Hz
u8   bpp
u16  width
u16  height
u32  filesize
u16  frames           -- number of frames
u16  frames2          -- upper part of frame counter, ignored by some implementations
u32  reserved3        -- usually 0, sometimes "V107", "V108" - version?
u16  speed            -- playback speed in fps, may be zero (assume 15 fps then?)
u16  maxbuffer        -- number of frame buffers used
u32  maxchunk         -- max frame chunk size
u8   note[16]
u8   copyright[16]    -- "-Copyright CRYO-"
```

### Frame chunks

Each frame chunk begins with a `u32` chunk size (including the size field
itself), then contains the frame's individual chunks:

```
u32  chunksize       -- chunk size including this field, excluding padding
u16  chunkid         -- TWOCC chunk id
u16  reserved
u8   data[]
u8   padding[]       -- pads chunk to 4-byte boundary
```

| Chunk ID | Contents |
|---|---|
| `AA` | APC audio. See [CRYO APC](https://wiki.multimedia.cx/index.php/CRYO_APC) |
| `BB` | Audio continuation |
| `IW` | Video frame, WARP format |
| `IX` | Video frame, Normal format |

### Format modification (Riverworld)

At least one game (Riverworld) uses a slightly different container, most likely
due to different sound encoding. Not backward compatible. The standard header
continues with an additional audio description header:

```
u32  freq
u32  bits
u32  channels
u32  ?
u32  ?
u8   copyright[28]  -- "HNMS 1.1 by SARRET Hubert\0\0\0"
u32  ?              -- some initial audio samples?
u32  ?
u32  ?
u32  ?
```

Each frame chunk is followed by an audio chunk whose size is **not** counted by
the frame's chunk size field:

```
u32  sig[4]          -- "SOUN"
u32  chunksize       -- including this preamble
u8   data[]
```

---

## Video codec

Compression uses **key-blocks** encoded with a JPEG-like algorithm, plus **motion
blocks** derived from previously drawn blocks. Frames are encoded in 8x8 or
smaller blocks. Key-blocks are always 8x8 and directly encoded; motion blocks
rely on divide-and-conquer splitting and block transformations.

The codec is closely related to the **4XM** and **Mobiclip** codecs — written by
the same people. Both have FFmpeg implementations worth reading.

Two variants exist, sharing an identical bitstream layout:

- **WARP** — prototype, simplified. Used in a very small number of games circa 1997.
- **Normal** — fully featured, widely used. (That is genuinely the internal name.)

### Frame data header

```
s32  quality            -- JPEG quality index; negative = keyframe
u32  bitbuffer          -- offset of bitbuffer
u32  motionbuffer       -- offset of motion vector buffer
u32  shortmotionbuffer  -- offset of short motion vector buffer
u32  jpegbuffer         -- offset of jpeg data buffer
u32  jpegend            -- offset of jpeg data buffer end
```

All offsets are little-endian and **relative to this header**.

- `quality` is a standard JPEG quality value (0..100). A negative value marks a
  keyframe (Normal codec only).
- `bitbuffer` points to VLC-encoded macroblocks. Read with a bit-reader using a
  32-bit internal queue, **MSB first**.
- `motionbuffer` points to an array of `u16` motion vectors.
- `shortmotionbuffer` entries are **12 bits** each, accessed in **LSB** order.
- `jpegbuffer` points to an array of encoded JPEG macroblocks.

### Key-block decoding

Key-blocks are JPEG-encoded in **YUV 4:4:4**. Requantization, IDCT and colorspace
conversion are identical to standard JPEG. Standard zigzag order
(0,1,8,16,9,...) and standard quantization tables (16,11,10,16,24,... and
17,18,24,47,99,...) are used.

The only custom feature is coefficient encoding: **RLE instead of Huffman**.

Macroblock JPEG data is read as 4-bit nibbles, **lower nibble of each byte
first**. Primitives:

- **half** — half of a nibble, upper part first then lower (read a nibble,
  process its upper part on first use, lower part on second)
- **s2** — signed 2-bit two's-complement value of a half, no zero point
  (`0b00 -> 1`, `0b01 -> 2`, `0b10 -> -2`, `0b11 -> -1`)
- **s4** — signed 4-bit two's-complement value of a nibble, no zero point
- **s44** — signed 8-bit value of two nibbles, with zero point
  (`(signextend(nibble1) << 4) | nibble2`)

For each plane of 8x8 coefficients the first coefficient is **s44**, then read
nibbles and fill the rest until the plane is complete. Repeat for Y, U and V.

| Code | Action |
|---|---|
| 0 | fill remainder of plane with zeros |
| 1 | `0, 0, 0, 0` |
| 2 | `0, 1` |
| 3 | `0, -1` |
| 4 | `0, 0, 1` |
| 5 | `0, 0, -1` |
| 6 | `0, 0, 0, 1` |
| 7 | `0, 0, 0, -1` |
| 8 | `zeros = half`, `tail = half`; fill `zeros+1` coefficients with 0; fill `tail+2` coefficients with **s2** |
| 9 | `zeros = half`, `tail = half`; fill `zeros+1` coefficients with 0; fill `tail+1` coefficients with **s4** |
| 10 | `s2, s2` |
| 11 | `s4` |
| 12 | `s4, s4` |
| 13 | `s4, s4, s4` |
| 14 | `0` |
| 15 | `s44` |

### WARP decoding

| Block type | 8x8 | 4x4 |
|---|---|---|
| Keyblock | `11` | n/a |
| Motion | `10` | `0` |
| CrossCut | `0` | `1` |

- **Keyblock** — a JPEG-encoded macroblock.
- **CrossCut** — split the block into 4 smaller subblocks, process each
  recursively with the same scheme.
- **Motion** — copy the block from elsewhere in the frame. Read the next entry of
  the motion buffer, then:
  - transformation mode = next 2 bits from bitbuffer (upper part) combined with
    bit 15 of motion (lower bit)
  - `xmotion = 128 - (bits 7..14 of motion)`
  - `ymotion = (4 if block is not 8x8 else 0) - (bits 0..6 of motion)`

2x2 blocks are **always** short-motion coded, using no extra VLC:

- transformation mode = bits 1..3 of short motion
- motion index = `bit 0 || bits 4..7 || bits 8..11` (9 bits total)
- decode the index:

```c
if (index < 12 * 8) {
    xmotion = -2 - index % 12;
    ymotion =  6 - index / 12;
} else {
    index -= 12 * 8;
    xmotion = 19 - index % 32;
    ymotion = -2 - index / 32;
    if (ymotion <= -8)
        ymotion = ymotion - 1;
}
```

Source coordinates are the sum of the **current 8x8 macroblock's** coordinates
(not the subblock's) and `xmotion`/`ymotion`. If the x-coordinate goes out of
frame it **wraps around the line**. Copy the block, applying the transformation.

### Normal decoding

Normal compression adds interframes, more split modes, and different short-motion
encoding.

**Keyframe:**

| Block type | 8x8 | 4x8 | 8x4 | 4x4 | 2x4 | 4x2 | 2x2 |
|---|---|---|---|---|---|---|---|
| Keyblock | `110` | n/a | n/a | n/a | n/a | n/a | n/a |
| HorizontalCut | `00` | `0` | n/a | `01` | `1` | n/a | n/a |
| VerticalCut | `01` | n/a | `0` | `10` | n/a | `1` | n/a |
| CrossCut | `10` | n/a | n/a | `11` | n/a | n/a | n/a |
| Motion | `111` | `1` | `1` | `00` | `0` | `0` | n/a |

**Interframe:**

| Block type | 8x8 | 4x8 | 8x4 | 4x4 | 2x4 | 4x2 | 2x2 |
|---|---|---|---|---|---|---|---|
| Keyblock | `011` | n/a | n/a | n/a | n/a | n/a | n/a |
| HorizontalCut | `100` | `10` | n/a | `101` | `111` | n/a | n/a |
| VerticalCut | `101` | n/a | `10` | `110` | n/a | `111` | n/a |
| CrossCut | `1110` | n/a | n/a | `111` | n/a | n/a | n/a |
| Skip | `110` | `11` | `11` | `100` | `110` | `110` | `11` |
| ShortMotion | `00` | `00` | `00` | `00` | `0` | `0` | `0` |
| Motion | `010` | `01` | `01` | `01` | `10` | `10` | `10` |

- **HorizontalCut** splits the block horizontally into two subblocks of smaller
  height; each processed recursively.
- **VerticalCut** is identical but splits vertically.
- **Skip** copies the block from the previous frame.
- **ShortMotion** copies from the previous frame using spiral-encoded motion.
  Unlike regular motion, short motion is relative to the **subblock's**
  coordinates, not the macroblock's.
- **Motion** as in WARP, with these changes:
  - *Keyframe*, blocks smaller than 4x4:
    - transformation mode = bits 12..14 of motion
    - `xmotion = 63 - (bits 0..6 of motion)`
    - `ymotion = 6 - (bits 7..11 of motion)`
    - keyframe 2x2 blocks are always motion-coded this way
  - *Interframe*, blocks 4x4 and larger:
    - transformation mode = next 3 bits from bitbuffer
    - if bit 15 of motion is zero the source is the **previous** frame, otherwise
      the **current** frame
    - `xmotion = 128 - (bits 7..14 of motion)`
    - `ymotion = (64 if from previous frame else (4 if block is not 8x8 else 0)) - (bits 0..6 of motion)`
  - *Interframe*, blocks smaller than 4x4:
    - if bit 15 of motion is zero the source is the previous frame, otherwise current
    - from current frame: same as keyframe above
    - from previous frame:
      - transformation mode = next 3 bits from bitbuffer
      - `xmotion = 31 - (bits 0..5 of motion)`
      - `ymotion = 31 - (bits 6..11 of motion)`
  - Always wrap x motion around the line if it goes out of frame.

### ShortMotion spiral

The 12-bit short-motion index maps to a relative `(x, y)` offset by walking a
spiral outward from the current subblock. Reconstruction of the wiki's
`HNM6Spiral.png` figure:

```
              x=-1    x=0     x=1     x=2
    y=-1  |    9   |   8   |   7   |   6   |
    y= 0  |   10   |   1   |   0   |   5   |
    y= 1  |   11   |   2   |   3   |   4   |
```

Index 0 sits at `(1, 0)`. The walk proceeds `0 (1,0)` → `1 (0,0)` → `2 (0,1)` →
`3 (1,1)` → `4 (2,1)` → `5 (2,0)` → `6 (2,-1)` → `7 (1,-1)` → `8 (0,-1)` →
`9 (-1,-1)` → `10 (-1,0)` → `11 (-1,1)` → and onward in widening rings.

The full index space is 12 bits (0..4095), covering a **64x64 search window**
spanning `x` and `y` in `-31..32`. Corner indices, from the figure:

| Index | Offset |
|---|---|
| 3969 | `(-31, -31)` |
| 3906 | `(32, -31)` |
| 4032 | `(-31, 32)` |
| 4095 | `(32, 32)` |

### Block transformations

Applied during block copying.

| Mode | Name | Effect |
|---|---|---|
| 0 | Normal copy | copy as-is |
| 1 | Horizontal flip | swap left/right: `abc -> cba` |
| 2 | Vertical flip | swap up/down |
| 3 | Cross flip | swap both axes |
| 4 | Forward flip | mirror around `/` |
| 5 | Forward rotate | rotate 90 degrees clockwise |
| 6 | Backward rotate | rotate 90 degrees counter-clockwise |
| 7 | Backward flip | mirror around `\` |

```
2 - Vertical flip       3 - Cross flip      4 - Forward flip
  a       c               ab      fe          ab  ->  db
  b  ->   b               cd  ->  dc          cd      ca
  c       a               ef      ba

5 - Forward rotate                6 - Backward rotate
  ab      eca      fe               ab      bdf      fe
  cd  ->  fdb  ->  dc               cd  ->  ace  ->  dc
  ef               ba               ef               ba

7 - Backward flip
  ab  ->  ac
  cd      bd
```

### Bitexact decoding

Stock JPEG routines will decode key-blocks adequately, but bitexact output
requires reimplementing the specific arithmetic used by the original.
