# HNM video in this game

Format specification lives in [hnm6-spec.md](hnm6-spec.md). This doc records what
is actually *on these discs*, parsed with that spec.

## Two generations coexist

The game ships **both** HNM4 and HNM6 content, used for different purposes. This
is the single most practically useful finding here, because FFmpeg already
decodes HNM4. **[verified]**

| Magic | Count | Extension | Role |
|---|---|---|---|
| `HNM4` | 20 | `.HNM` | 256x256 in-game animated textures / effects |
| `HNS6` | 73 | `.HNM` | 640x304 full-motion cutscenes |
| `HNM6` | 2 | `.HNM`, `.UBB` | same as HNS6 |
| `UBS2` | 16 | `.UBB` | UBIK presentation bundles |
| `UBB2` | 4 | `.UBB` | UBIK presentation bundles |

115 files parsed across both discs.

### The `HNS6` vs `HNM6` signature

The MultimediaWiki spec documents the signature as `HNM6`. On these discs **73 of
75 sixth-generation files use `HNS6` instead**, with only 2 using the documented
`HNM6`. The same `M`/`S` alternation appears in the UBIK bundles (`UBB2` vs
`UBS2`), so the letter is clearly a systematic variant marker, not corruption.

**[unverified] hypothesis:** `S` denotes the audio-multiplexed variant and `M`
the standalone/video-only variant, matching the wiki's note that HNM6 exists both
as an "A/V multiplex" and as a "stand-alone" form that references external APC
files. A decoder should accept both tags and branch on the presence of `AA`/`BB`
audio chunks rather than trusting the signature.

Test to settle this: scan frame chunks of one `HNS6` and one `HNM6` file for `AA`
chunk IDs. Not yet done.

## Cutscene inventory (HNS6/HNM6)

All are **640x304, 16 bpp**, copyright field `-Copyright CRYO-`, version field
empty (not `V107`/`V108`), `speed` field **0** — so per the spec a decoder should
assume 15 fps. **[verified]**

| File | Frames | Bytes | Notes |
|---|---|---|---|
| `INTRO.HNM` | 2,781 | 38,164,312 | disc 1. Disc 2 carries a 1.7 MB stub of the same name |
| `F_JEU.HNM` | 2,938 | 27,536,956 | largest by frame count |
| `SHAMAN.HNM` | 1,285 | 17,808,364 | |
| `TGA1.HNM` | 740 | 10,143,252 | |
| `FCAMUSI.HNM` | 441 | 6,717,356 | |
| `FIN_CAM.HNM` | 485 | 6,632,228 | ending |
| `RONALD.HNM` | 445 | 6,237,324 | |
| `ARAI_FCA.HNM` | 450 | 6,078,244 | |
| `VISIONF.HNM` | 445 | 6,008,084 | |
| `CELLULE.HNM` | 459 | 5,641,148 | |
| `PROJECT.HNM` | 389 | 5,621,792 | |
| `TUNEL_C1.HNM` | 300 | 4,552,952 | |
| `TETE_E~1.HNM` | 313 | 4,533,544 | 8.3 truncated name |
| `MEDIAS.HNM` | 289 | 4,136,000 | |
| `ARAI_FIN.HNM` | 320 | 4,033,488 | |
| `DEBUT_~1.HNM` | 281 | 3,714,172 | 8.3 truncated name |
| `ROC.HNM` | 254 | 3,517,552 | |
| `FINAL.HNM` | 227 | 3,297,704 | |

Total HNM content is ~297 MB across both discs — by far the largest asset class,
larger than all geometry combined.

At 15 fps, `INTRO.HNM`'s 2,781 frames run about **3 minutes 5 seconds**.

640x304 is an unusual, deliberately letterboxed frame (2.1:1) — it is not a crop
of 640x480. Cutscenes were authored widescreen and composited into the 640x480
display mode with black bars.

## Texture animations (HNM4)

All 20 are **256x256** and live in `DATA\ANIM\` and `DATA\HNM\`. Their names make
the purpose obvious — these are looping animated textures and effects, not
cutscenes: **[verified]**

```
E11_EAU.HNM   E12_EAU.HNM    water ("eau")
M05FEU_H.HNM                 fire ("feu")
FD_SOUFL.HNM                 bellows/breath ("souffle")
CYB1_TR.HNM   CYB2_TR.HNM   CYB3_TR.HNM
ORGA_01.HNM   TF_ALL.HNM    MOTEURB.HNM
M01DRA.HNM    PAS_JOHN.HNM  END_BIL.HNM
H02HNM01.HNM  H03AN001.HNM  H04AN001.HNM
L12_HNM1.HNM  L14_HNM1.HNM
```

Largest is `TF_ALL.HNM` at 8,332,452 bytes.

**These 20 files are decodable today.** FFmpeg implements an HNM4 demuxer and
video decoder (`hnm4video.c`). Try:

```bash
ffmpeg -i "DATA/ANIM/E11_EAU.HNM" -c:v png frames/%05d.png
```

Untested here, but the format matches what FFmpeg supports. **[unverified]** that
Cryo's specific HNM4 revision is the one FFmpeg handles.

## Decoder status for HNM6

**No public decoder exists.** MultimediaWiki categorises HNM6 under "Formats
missing in FFmpeg". A [ZenHAX thread](https://zenhax.com/viewtopic.php@t=15513.html)
requests an HNM6-to-AVI converter and none was produced. **[sourced]**

The format *is* fully documented, though — see [hnm6-spec.md](hnm6-spec.md), which
covers the container, both codec variants (WARP and Normal), the JPEG-like
key-block coding with its custom RLE coefficient scheme, the VLC macroblock
tables, the spiral short-motion encoding and all eight block transformations.
Writing a decoder is a bounded project, not a research problem.

### Leads

- **Cryo's own decoder DLLs.** The
  [GameMediaFormats DirectShow package](https://github.com/ValeryAnisimovsky/GameMediaFormatsCoreFilters)
  notes that HNM/HNS playback was provided by `CM6_*x16.dll` — "CM6" = Cryo Movie
  6, matching the `HNS6`/`HNM6` tag. Locating those DLLs would give a reference
  implementation to validate against. **[sourced]**
- **`DEMOS2\CRYO.DLL`** (644,608 B) is on disc 2 and is registered in `SETUP.INI`
  as `[PlusTest] demos2\cryo.dll`. It is a strong candidate for containing the
  playback code used by `PLAYUBB.EXE`. **Not yet examined — do this first.**
- The codec is described as closely related to **4XM and Mobiclip**, written by
  the same people. Both have FFmpeg implementations worth reading for structure.

## Audio

HNM6 audio is Cryo **APC**, either muxed into the `.HNM` as `AA`/`BB` chunks or
stored in sidecar files. The `audioflags` byte encodes stereo in bit 7 and
frequency in bits 6-5 as a multiple of 11025 Hz.

On these discs **every parsed cutscene reports `audioflags = 0`**, which the spec
says is a non-authoritative indication of no APC sound. Combined with the
redbook CD-audio music tracks and the 24.6 MB `DIALOG.DRD`, the likely
arrangement is: video in HNM, voice in `DIALOG.DRD`, music on CD audio.
**[unverified]** — confirm by scanning for `AA` chunk IDs.

## Reproducing the header parse

```python
import struct

b = open(path, "rb").read(64)
sig = b[:4]  # HNM4 | HNM6 | HNS6
audioflags = b[6]  # bit7 stereo, bits6-5 freq/11025
bpp = b[7]
w, h = struct.unpack_from("<HH", b, 8)
(filesize,) = struct.unpack_from("<I", b, 12)
frames, frames2 = struct.unpack_from("<HH", b, 16)
version = b[20:24]  # b'' | V107 | V108
speed, maxbuffer = struct.unpack_from("<HH", b, 24)
(maxchunk,) = struct.unpack_from("<I", b, 28)
note = b[32:48]
copyright = b[48:64]  # '-Copyright CRYO-'
```

Header is 64 bytes. HNM4 uses a different, shorter layout — do not apply this to
`HNM4` files.
