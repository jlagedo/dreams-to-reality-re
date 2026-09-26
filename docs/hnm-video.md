# HNM video in this game

Format specification lives in [hnm6-spec.md](hnm6-spec.md). This doc records what
is actually *on these discs*, and how to decode it.

> **The video problem is solved.** Every one of the 113 video files on both discs
> decodes with open-source tooling. This reverses this project's earlier
> conclusion that "no working HNM6 decoder exists" — two independent
> implementations were written while nobody was looking, and Cryo's own vendor
> DLLs turned out to be publicly archived as well.

## Decoding it today

### `na_game_tool` (NihAV) — the working route **[verified]**

A standalone Rust utility by **Kostya Shishkov**, GPLv3, no external
dependencies. <https://nihav.org/game_tool.html>

v0.4.0 (Jun 2025) added Cryo HNM 0/1/4; **v0.5.0 (Jan 2026) added HNM5 (UBB2) and
HNM6**. It ships a from-scratch HNM6 decoder — IDCT, zigzag, separate luma/chroma
quantisation matrices, quality scaling — plus the container and a Cryo archive
reader.

```bash
cargo build --release
na_game_tool -ifmt hnm6 DATA/HNM/ARENE.HNM -ofmt imgseq 'out%04d.ppm'
na_game_tool -ifmt hnm5 DATA/HNM/CONTROLE.UBB -ofmt avi out.avi
na_game_tool -ifmt hnm4 DATA/ANIM/E11_EAU.HNM -ofmt null null
```

Plugin names are `hnm4`, `hnm5` (this is what reads `.UBB`) and `hnm6`. Output is
PPM or BMP image sequences, AVI, or WAV.

**One patch is required.** Stock NihAV gates on the tag and rejects the `S`
spellings this game uses. `src/input/decoders/hnm.rs`:

```diff
-        (b"HNM4", 4) | (b"UBB2", 5) | (b"HNM6", 6)));
+        (b"HNM4", 4) | (b"UBB2", 5) | (b"HNM6", 6) | (b"HNS6", 6) | (b"UBS2", 5)));
```

With that line, **113 of 113 files decode**. Verified on this machine:
`ARENE.HNM` produced **113 frames at 640×304**, matching the frame count read
independently from its header, and the frames are coherent imagery rather than
noise.

The tool's own `FORMATS.md` cautions that for hnm6 "framerate and colours may be
wrong" — cross-check against the vendor DLL or ScummVM before trusting colour.
**[sourced]**

**[verified]** Audio did *not* come out of `na_game_tool`: `-ofmt wav` on an
HNS6 file reports "nothing was sent to output". That is a limitation of the
tool, **not of the format** — the audio is decodable.

**The `SD` payloads are table-driven 16-bit DPCM, not APC.** A per-file
256-entry signed delta table opens the first `SD` chunk (512 bytes), then raw
code bytes interleaved L/R, the predictor persisting across chunk boundaries
from zero. No adaptive index, no step table. 73 of 94 `.HNM` files carry
audio, 20,576 `SD` chunks in all; the 20 `HNM4` texture animations correctly
have none. APC is real but belongs to the `AA`/`BB` chunks this game does not
use. Decoded output checks out: zero-crossing rate 0.101, DC offset −1.3 and
**zero** clipped samples across 592,410 frames — a wrong predictor would
random-walk and saturate. Sample rate 22,050 Hz is inferred from 2,940 code
bytes per 15 fps block, not read from the container. **[unverified]**

### ScummVM — an independent second decoder **[sourced]**

`video/hnm_decoder.cpp` (1,215 lines), added by contributor **Le Philousophe** in
commit *"CRYOMNI3D: Add HNM6 image and video codec"*, 2022-08-29, still
maintained. Includes an `APCAudioTrack` and a standalone `audio/decoders/apc.cpp`.

It accepts exactly `HNM4`, `UBB2`, `HNM6` — **not** `HNS6`, the same gap NihAV
had. Valuable mainly as a second, independently written implementation to
cross-check colour and timing against.

> This corrects an earlier claim in this project that "ScummVM will not help."
> Its *engines* (`cryo`, `cryomni3d`) are indeed point-and-click only and cannot
> run this game — but its **video layer** decodes this game's format.

### Cryo's own `CM6_*x16.dll` — found **[verified]**

This closes the long-standing open question. The vendor decoder DLLs are
publicly archived at <https://samples.ffmpeg.org/game-formats/cryo/>:
`CM6_512x16.dll`, `CM6_640x16.dll`, `CM6_800x16.dll`, `CM6_1024x16.dll`,
~140 KB each, dated 2006-02-28, with an md5sum manifest.

`CM6_640x16.dll` analysed directly: 142,848 bytes, MD5
`d993924414dba2aa04c78406a81567f6` (matches the published manifest), PE32 i386,
MSVC. It exports exactly three symbols and imports **only `KERNEL32.dll`** — no
DirectX, no COM:

```
HNMPI_Init         (ordinal 3)
HNMPI_DecodeFrame  (ordinal 2)
HNMPI_Cleanup      (ordinal 1)
```

The game's own HNM6 decoder is CryoLib's assembly, rebuilt for the 640×304
frame. `HNM6_DecompressFrame` is at `0x45c2c0` in `WINDREAM.EXE`; see
[cryolib.md](cryolib.md), *The game carries CryoLib's HNM6 decoder*.

`.text` is only 36,368 bytes; the ~100 KB of `.data` is almost certainly
quantisation and Huffman tables. That makes it both a **decode oracle** (callable
from a ~50-line C harness, or under Wine) and a **cleaner RE target than
`CRYO.DLL`**. The width is compiled in — `640` matches this game's cutscenes
exactly.

The accompanying `readme-hnm.txt` says the samples come from *Atlantis 2*,
*Odyssée* and *China: The Forbidden City* — not this game, but the same codec
family. **[sourced]**

## What is on the discs

**[verified]** 113 video files across both discs:

| Magic | Count | Extension | Generation | Role |
|---|--:|---|---|---|
| `HNS6` | 73 | `.HNM` | 6 | 640×304 cutscenes |
| `HNM4` | 20 | `.HNM` | 4 | 256×256 animated textures |
| `UBS2` | 16 | `.UBB` | 5 | 640×304 movies, with sound chunks |
| `UBB2` | 3 | `.UBB` | 5 | 640×304 movies, video only |
| `HNM6` | 1 | `.HNM` | 6 | `ABAL_USI.HNM` — the only true `HNM6` game file |

One further `HNM6` file exists — `DEMOS2\3MILL\3MILL.UBB` — but it is demo
content, not this game.

### The `M` / `S` variant letter

**[verified]** `HNS6` is structurally identical to `HNM6`: same header layout,
same 640×304, same `framesize = 0x5F000 = 640×304×2`, same strings. `UBS2`
likewise behaves as `UBB2`. That is why the one-line tag patch is sufficient.

**The earlier hypothesis that `S` marks the audio-multiplexed variant is
disproven.** **[verified]** The 73 `HNS6` files contain **20,576 `SD` sound
chunks** between them despite every one of them reporting `audioflags = 0`, while
the single `HNM6` file `ABAL_USI.HNM` has `IX` video chunks and **no** `SD` at
all. So the letter does not encode audio presence, and `audioflags = 0` does not
mean "no audio" in this release.

Aggregate body chunk counts across the 74 sixth-generation files: `IX` = 19,717,
`SD` = 20,576. No `AA` or `BB` chunks occur.

### `.UBB` is video — question settled

**[verified]** `.UBB` files are **not** subtitle or audio sidecars, and there is no
"UBIK presentation toolkit". They are **HNM generation 5** movie containers — the
codec generation introduced with *MegaRace II* (1996). NihAV reads them with its
`hnm5` plugin, which it labels "Cryo UBB".

UBB2 carries `IV` video and `PL` palette chunks; UBS2 adds `SD` sound chunks
internally. Aggregates: UBB2 = 294 `IV` + 3 `PL`; UBS2 = 2,381 `IV` + 2,605 `SD`
+ 16 `PL`.

## Header layouts

### HNS6 / HNM6 — **[verified]** 74/74

```
0x00  char[4]  "HNS6" | "HNM6"
0x04  u16      variant word   HNS6 = 0x0200, HNM6 = 0x0000
0x06  u8       audioflags     0 in every file (NOT authoritative - see above)
0x07  u8       0x10           16-bit video
0x08  u16      640            width
0x0A  u16      304            height
0x0C  u32      file size      exact in 74/74
0x10  u32      frame count    48 .. 2938
0x14  u32      0              reserved/version
0x18  u16      0              speed  (decoders assume 15 fps)
0x1A  u16      2              max buffer
0x1C  u32      0x0005F000     max chunk = 640*304*2
0x20  char[16] "Pascal URRO  R&D"
0x30  char[16] "-Copyright CRYO-"
```

Body: outer `u32` frame-chunk size (self-inclusive), then inner chunks of
`u32 size`, `u16 id` (`IX` video / `SD` sound), `u16 flags` (`IX`=0,
`SD`=0x8400). Terminated by four zero bytes.

### HNM4 — **[verified]** 20/20

A different, shorter layout. Do not apply the generation-6 parse to it.

```
0x00  char[4]  "HNM4"
0x04  u8[4]    00 01 00 08
0x08  u16      256            width
0x0A  u16      256            height
0x0C  u32      file size
0x10  u32      frame count    15 .. 851
0x14  u32      footer/TAB region offset
0x18  u16      0x0010         sample bits
0x1A  u16      0x0002         channels
0x1C  u32      0x00010000     frame allocation = 256*256
0x30  char[16] "-Copyright CRYO-"
0x40  ...      24-bit size prefixed chunks: PL, IZ, IU
```

Timebase is **24 fps** for HNM4 — reported consistently by `ffprobe`, and not
stored anywhere in the visible header.

### UBB2 / UBS2 — **[verified]** 19/19

Same 64-byte shape as generation 6, with `0x04` = `0x0140` (UBB2) or `0x0340`
(UBS2), `0x1C` = 194,560 = 640×304, and per-file metadata at `0x20` ending
`-URRO P. 95-`.

## Pascal Urro

Every sixth-generation header carries `Pascal URRO  R&D` at offset `0x20`, and
the UBB headers carry `-URRO P. 95-`. **[verified]** He is Cryo's HNM codec
author; a ScummVM developer independently attributes the algorithm to him in IRC
("it's from another guy… Pascal Urro"). **[sourced]** Dating the UBB string to
1995 puts generation 5 two years before this game shipped.

## Cutscene inventory

All 640×304, 16 bpp. Durations below assume the 15 fps decoder default.

| File | Frames | Bytes | Notes |
|---|--:|--:|---|
| `F_JEU.HNM` | 2,938 | 27,536,956 | largest by frame count · disc 2 |
| `INTRO.HNM` | 2,781 | 38,164,312 | **disc 1** — the real intro, ~3 min 5 s |
| `SHAMAN.HNM` | 1,285 | 17,808,364 | |
| `TGA1.HNM` | 740 | 10,143,252 | |
| `FIN_CAM.HNM` | 485 | 6,632,228 | ending |
| `CELLULE.HNM` | 459 | 5,641,148 | |
| `ARAI_FCA.HNM` | 450 | 6,078,244 | |
| `RONALD.HNM` | 445 | 6,237,324 | |
| `VISIONF.HNM` | 445 | 6,008,084 | |
| `FCAMUSI.HNM` | 441 | 6,717,356 | |
| `PROJECT.HNM` | 389 | 5,621,792 | |
| `ARAI_FIN.HNM` | 320 | 4,033,488 | |
| `TETE_E~1.HNM` | 313 | 4,533,544 | 8.3-truncated name |
| `TUNEL_C1.HNM` | 300 | 4,552,952 | |
| `MEDIAS.HNM` | 289 | 4,136,000 | |
| `DEBUT_~1.HNM` | 281 | 3,714,172 | 8.3-truncated name |
| `ROC.HNM` | 254 | 3,517,552 | |
| `FINAL.HNM` | 227 | 3,297,704 | |

### Boot-flow roles **[verified]**

Three of these have named places in the startup sequence (decompiled; see
[boot-sequence.md](boot-sequence.md)):

| File | Role at boot |
|---|---|
| `INTRO.HNM` | the first movie, played via dispatcher event `0x17` before anything else; skippable |
| `GENERIC.HNM` | played right after the intro — the 101-frame light-speed tunnel that runs as the animated main-menu background |
| `TETE_E~1.HNM` | hardcoded one-shot for entering Project 0 — the bearded elder's talking-head briefing, played just before the first map loads |

The general per-level mechanism is data-driven: each project record in
`DREAMS.DAT` names its intro video (in-memory at `+0x3c`), and the engine
plays `data\hnm\<name>` on entry. Project 0's record names `ETE_E~1.HNM`,
which **exists on neither disc** — that play fails open-and-skip, and the
hardcoded `TETE_E~1.HNM` is what actually shows.

### The disc-2 `INTRO.HNM` mystery — solved

**[verified]** Disc 2's 1,698,736-byte `INTRO.HNM` is **byte-identical to
`GENERIC.HNM`**. It was never a truncated or stub copy of the intro; it is a
different movie that happens to share the filename. Disc 1's 38 MB file is the
only intro. This settles the disc-merge question for that file.

640×304 is a deliberate 2.1:1 letterbox, not a crop of 640×480 — cutscenes were
authored widescreen and composited into the display mode with black bars.

## Texture animations (HNM4)

All 20 are 256×256, in `DATA\ANIM\` and `DATA\HNM\`, and their names say what
they are: **[verified]**

| File | Frames | Effect |
|---|--:|---|
| `TF_ALL.HNM` | 851 | largest HNM4, 8,332,452 B |
| `ORGA_01.HNM` | 367 | |
| `CYB1_TR.HNM` | 220 | transition |
| `FD_SOUFL.HNM` | 201 | souffle — bellows/breath |
| `M01DRA.HNM` | 111 | |
| `END_BIL.HNM` | 101 | |
| `E11_EAU.HNM` | 75 | eau — water |
| `E12_EAU.HNM` | 65 | eau — water |
| `M05FEU_H.HNM` | 15 | feu — fire |

Plus `CYB2_TR`, `CYB3_TR`, `H02HNM01`, `H03AN001`, `H04AN001`, `L12_HNM1`,
`L14_HNM1`, `MOTEURB`, `PAS_JOHN`.

Level-prefixed names (`H03AN001`, `L12_HNM1`) tie these to the scenes in
[level-map.md](level-map.md).

## Remaining work

1. **Colour and framerate validation.** NihAV warns its hnm6 output "may be
   wrong". A/B a frame against `CM6_640x16.dll` or ScummVM to settle it.
2. **Audio.** `SD` chunks are present and counted but were not extracted. The
   `SD` flags word is `0x8400`; ScummVM's `apc.cpp` is the reference.
3. **Upstream the patch.** NihAV is actively maintained and its author appears
   not to have had `HNS6`/`UBS2` samples.
4. **Try NihAV's Cryo archive reader** (`src/input/archives/cryo.rs`, "BigFile
   1.00" plus a headerless older layout) against `DREAMS.DAT`, `.PAK` and
   `ICONES.BF`. Untested.
