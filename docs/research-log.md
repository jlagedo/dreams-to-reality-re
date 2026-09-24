# Research log

## Corrections

### `CARRE.3DC` was never broken — *carre* means square

The open questions asked why `CARRE` "fails to decode as a box". It decodes as
**4 vertices and 2 triangles**, which is a quad, and *carré* is French for
**square**. The decode was right and the expectation was wrong. With the node
struct applied to raw `F3DC`, `BOULE` reaches **0 boundary edges** (it was 18),
as do `EPEE` and `GUN`. Only `ARC` is still open, at 15.


### `.DAN` is not "just animation" — it is the model

Catalogued for two passes as *"Animation. Same container and LZ as `.DSN`;
payload meanings open."* Tag 1 is the **model**, built from the same
scene-graph node as `.DSN` tag 1, and tag 2 is its texture page. 159/159
models decode. The cost of the wrong label was that the character models sat
unread while effort went into `.3DC`. See [models.md](models.md).

### HNM audio is DPCM, not APC

This file and `AGENTS.md` both said *"Audio is Cryo **APC**"*, sourced from the
MultimediaWiki HNM6 page. For **this** release the `SD` payloads are
**table-driven 16-bit DPCM** — ScummVM's `DPCMAudioTrack` path — with a
per-file 256-entry signed delta table in the first `SD` chunk. APC is real, but
it belongs to the `AA`/`BB` chunks this game does not use. Verified by decoding:
zero-crossing rate 0.101, DC offset −1.3, **zero** clipped samples across
592,410 frames.

### `DIALOG.DRD` is a voice bank, not a text bundle

24.6 MB was never plausible for dialogue text, and it is not: **72.6% is 178
RIFF/WAVE clips** (mono, 11,025 Hz, 8-bit), 27.3% is type-4 binary, and only
**0.084%** is the script. Decoded in `dreams.formats.dialog`: 178 entries, 575 lines, every one printable ASCII, each carrying a timing field.
The 178 clips total 17,848,529 bytes and the entry chain walks exactly to EOF. A
table word is not a plain offset - its low byte is a bank and the upper
three bytes the offset, so a naive read breaks at entry 123.

### `ICONES.BF` is a named-file container

`UBIK` is not an image stream. It is a directory of named members with 267-byte
table rows. Disc 2's copy is larger than disc 1's because it adds
`TITRES.SPR` — a reason, where the merge table previously just said "keep the
larger one".

### The object behaviour record is not class-by-name

An earlier reading held that the 20-byte per-object record attaches handlers by
object-name family (`SOL`, `MUR`, `CIEL`, `HNM`). Counting refutes it: of 63
distinct non-zero `(word 0, word 1)` pairs, **36 span more than one family**,
and the 24 `HNM` surfaces split 6/18 across the two record forms. What *is*
exact is the variant rule — the pointer form of `word 3` occurs only when words
0 and 1 are both zero, in 2,059/2,059 records.

### The project record was read in its compressed form

Three wrong readings of `DREAMS.DAT`, one cause: the records are **zero-run
compressed** (`00 N` = N zero bytes), and they were being read as if the
compressed stream were the format.

- **`FLINK` and `DLINK` as key names** - listed in `file-formats.md` for
  months. The byte before a key is the run count that pads the previous slot;
  `F` is `0x46`, seventy zeros. 131 distinct bytes appear in that position,
  which should have been the clue long before it was.
- **A "type byte" after each key, and variable-width integers** - my own
  reading, one pass ago. `c4 09 00 02` is 2500 with two zeros run-length
  encoded, not a three-byte value with a tag.
- **`BOX5` as the floating island's trigger**, because its first coordinate
  matched the island's X. The triggers are the `LINK` entries themselves, which
  carry two corners; `BOX` is separate typed geometry.

Decompressed, every record is exactly `0x2200` bytes of fixed slots, 150 of
150. The engine's decoder is `FUN_00448e25`.

### "15,000 units up" was 15,000 units out

Project 0 places the floating island at (2500, 4000, -15000), and the last
component was taken as height because the number was large and the island
floats. The record uses the scene's own axes, up at negative Y: calibrated by
`LINK0`, whose box centres 165 units from the face tower `H18TETA1` under that
reading and 910 units from anything under the other. The island is past the
plateau's rim, at rim height. The creature-on-the-island reading survived the
correction; the picture of where the island *is* did not.

### Level geometry was never unsolved — the metric was

"The node decode does not give levels" rested on one number: composing world
transforms and comparing against tag 2's pool reached ≥99% in **8 of 95**
scenes, median 11.4%. From that came the conclusion that level graphs contain
**group nodes carrying a transform but no geometry**, and that finding them was
the remaining work.

The comparison was exact integer equality. World positions compose with an
integer `>> 15` at every level of the tree, so a node a few levels down lands a
unit or two away. At a tolerance of ±2 — one part in 50,000 of scene size — it
is **58 of 95 at ≥99%, median 100%**, and widening to ±8 or ±32 changes
nothing. A sharp step then a flat line is a fixed rounding offset, not a
misplaced object.

`H03PAQUE` has 24 nodes for its 24 named objects, 23 of 24 parents resolving
and none dangling: no missing group nodes in that scene at all.

The cost of the wrong metric was that levels exported through tag 2 as one
nameless merged triangle array, so every scene drew as a blank hull and no
level could be textured. 58 scenes now carry names and UVs where 5 did.

A decode was judged by a check that could only ever have passed by luck, and
the check's failure was attributed to the data. That is the same mistake as the
model UVs, where 100%-in-bounds was offered as proof of a mapping that was
pointing at the wrong bytes.

### `.DAN` does not carry two copies of the model

The second root, and the pair of names in every model, were read as "most
`.DAN` files carry two copies". They are **two texture groups over one mesh**.
A face block is named for the bank it samples — `XH_IMG_A` and `XH_IMG_B`,
image A and image B — and in **51 of 54** two-group models the groups share
vertex positions.

It mattered because a model has **two** tag-2 banks and **in 0 of the 57 models
that have two are they identical**. Texturing everything from bank 0 put the
character's chest on his back and a trainer under his arm. Reported by the
user looking at `XH_` in Blender, not by any check we had.

### A face block has to point at its own records

`u32 68` at `+0x20` and a plausible count were the whole test, and **17 models**
contain a byte run that passes both without being a face block. Their faces
even resolve to real geometry, so the validity check we trusted said yes; only
their UV references, which are small negative numbers, gave it away — as black
holes punched through the model. The block's pointer at `+0x14` rejects every
impostor and accepts every real block exactly.

Twice in one pass, a signature of two conditions was taken as sufficient
because it was clean on the file that was being looked at.

### A reference that resolves in range is not a reference that resolves

Model UVs were reported as "located, not solved": the references resolved,
**100% of 26,827 corners landed inside the pool**, and the result still
rendered as flat colour. That 100% was the whole of the evidence, and it was
worthless — it measured only that an address was in bounds, not that it was the
right address.

Two bugs, both ours:

1. The reference was read at `ref − 5`. The −5 is right for `.DSN`, where it is
   that format's relocation delta; a model's delta is positive, so the
   subtraction landed five bytes into the previous record.
2. `UV_SCALE` was `65536 * 255` instead of `65536 * 256`, dividing a texel by
   255 twice and flattening every coordinate to about 0.004.

The two reported symptoms — "flat colour blocks" and "no value exceeds 128 of a
256-wide atlas" — were measurements of the bug. Rasterising the model and
looking at it settled it in one step, which is why `dreams model --preview`
now exists and why the `models` group writes a picture per model.

### The player character is `XH_`, not `CH0`

`CH0.DAN` is in every level manifest, which made it look like the player. Its
internal names are `F14_M01`/`F14_M02` — a level-14 creature. The player is
`XH_.DAN`, on animation breadth: 8 files and 170 frames against `MHEROI`'s 2
and 80, the most frames of any model, and present in `MOT.DAN` beside
`EMOTO1`/`EMOTO2`. **Read the internal names, not the manifest count.**

### glTF Y was negated twice

`gltf.write` has always negated Y. The model exporter negated it again before
handing positions over, so every model came out upside down. The level path was
never affected because it goes through `from_scene`. A bug in our own code that
looked exactly like a format mystery.


Claims made earlier in this project that later evidence overturned. Recorded so
they are not re-asserted.

| Claim | Status | Correction |
|---|---|---|
| The `.bin`/`.cue` set is a PlayStation game | **wrong** | It is a PC mixed-mode CD. A PS1 port exists (*Dreams*, SLES-00714) but this is not it. Corrected by the folder name `Win_EN` and the PE/LE executables inside. |
| `.DIG` files are sound samples | **wrong** | They are Miles Sound System **sound-card drivers** — magic `AIL3DIG`, dated 1995-96. Game audio is `DATA\SOUND\FSB.DAT`. |
| Disc 2 has no executables | **imprecise** | It has no *game* executables, but ships `CRYO.DLL`, `PLAYUBB.EXE`, `PLAYTGA.EXE`, `SETSOUND.EXE`, `MSSW95.EXE`, `UVCONFIG.EXE`. |
| All `.HNM` files are HNM6 | **wrong** | 20 are **HNM4** (256x256 texture animations); 75 are HNM6/HNS6 (640x304 cutscenes). FFmpeg can already decode the HNM4 set. |
| `FULL.ID` is a "no disc needed" marker | **overstated** | `SETUP.INI` shows it is written only by the **maxi install**. It marks installation size, not disc independence. Its effect on disc checking is untested. |
| The Windows build was MSVC-built | **wrong** | Watcom C/C++, same as the DOS builds. `SETUP.EXE` is the only MSVC binary. |
| **No working HNM6 decoder exists** | **wrong** | **Two** independent open-source decoders exist: NihAV's `na_game_tool` (Rust, HNM6 added Jan 2026) and ScummVM's `video/hnm_decoder.cpp` (C++, added Aug 2022). All 113 video files on these discs decode. See [hnm-video.md](hnm-video.md). |
| ScummVM will not help | **overstated** | Its *engines* are point-and-click only and cannot run this game — but its **video layer** decodes this game's HNM format, and its `apc.cpp` decodes Cryo APC audio. |
| `.UBB` are UBIK presentation bundles | **wrong** | They are **video** — HNM generation 5, the codec from *MegaRace II* (1996). There is no UBIK authoring toolkit; the shared name is a coincidence with Cryo's game *Ubik*. |
| `.SPR` are raw RGB555 sprites | **wrong** | 8-bit **indexed** bundles with an inline 6-bit VGA palette and a 256-entry pointer table. `HI320.SPR`'s leading `1F 1C` is palette entry `0x1C1F`, not dimensions. |
| `.3DC` and `.3DM` share one container | **wrong** | Same `F3DC` tag, different structures. Every `.3DM` is exactly 98,332 B = 28-byte header + 3×32 KB blocks, with no material directory. |
| `PAK0` embeds `F3DC` at offset 16 | **wrong** | Offset **12** (`0x0C`), and there is exactly one embedded chunk, not several. Its revision field is 100, not 450. |
| `DREAMS.DAT` is a whole-file u32 offset table | **wrong** | Only the first 151 entries. It is self-indexing: table, 420 zero bytes, then 150 `ProjectN` records at `0x400`. |
| `.DSN` "count A" is a count | **wrong** | A derived header span: `A = 31·countB + 7` in all 98 files. |
| `HNS6`/`HNM6` marks audio multiplexing | **wrong** | The 73 `HNS6` files hold 20,576 `SD` sound chunks despite `audioflags = 0`; the one `HNM6` file has none. The letter does not encode audio. |
| `ESSAI.3DM` holds 16.16 fixed-point coordinates | **wrong** | `4669872` = `0x004741B0` is a **stale pointer**. The identical value recurs across `.3DM`, `.DSN` and `.DAN` — impossible for a coordinate. |
| The `.DSN` body is one opaque packed blob | **wrong** | It is a chain of `u8 tag, u32 size` records that walks to EOF exactly in 95/95 scenes. Tags 3 and 4 are fixed-size and **uncompressed**, and they are ~97% of the body. Only tags 1 and 2 (~40 KB per scene) are packed. |
| There are no level textures we can reach | **wrong** | Tag 3 is a 256-entry palette per object and tag 4 (exactly 64 records) is 64 distinct 32x32 8-bit tiles per object. 2,059 texture sheets extracted across 95 scenes. |
| The engine is RGB555 throughout | **imprecise** | `.DSN` texture palettes are **RGB565**. Read as 555 they produce impossible cyan speckles; as 565 they render rock, ice, lava and factory plate matching each scene's French name. `.3DC` material colours still read as 555. |
| `.DSN` and `.DAN` bodies do not share a container | **wrong** | Both use the same `u8 tag, u32 size` framing - 129/129 distinct `.DAN` files also walk to EOF exactly. `.DAN` uses tags 1-3 and never tag 4. |
| UV records start at the reference, minus one | **wrong** | UV references are `5 mod 8`, so their 8-byte record starts at `p - 5`. The `p - 1` rule holds for the vertex and edge classes but straddles two entries here, and textures render as diagonal streaks. Rasterising a wall both ways settles it. |
| The `.DSN` UVs take only three values | **wrong** | That came from eyeballing the first ten samples. Across `E01GROTT` there are 84 distinct u values and 57 distinct v, spread over 0..255. A ten-item peek drove a chunk of wrong reasoning about where the UVs lived. |
| The 64 tag-4 planes are tiles of an atlas | **wrong** | Each is a 32x32 **subsample** of one 256x256 texture, taken every 8th pixel at its own offset, and the loader interleaves all 64. Fill size starts at 8x8 and refines to 1x1 — progressive loading off a CD-ROM. |
| Edge sanity or degenerate-triangle rate can gate a mesh decode | **wrong** | Four scenes that render as debris score under 6% slivers, pass edge sanity, and have unremarkable bounding boxes. `L14_PETI` is a twisted ribbon at 0.8% slivers. Geometry can be locally plausible and globally wrong; render it instead. |
| The `u32` at tag 2 + 0x14 is not needed to gate a scene | **wrong** | Dropping it admitted four more single-run scenes, every one of which renders as debris. When the run is shorter than the pool, unreferenced entries make rank slip past the true index. All three conditions are required. |
| The `.DSN` body starts at `24 + 31B`, after an 8-byte scene block | **wrong** | It starts at **`16 + 31B` == `9 + A`**. `FUN_004175bc` reads the 11-byte and 20-byte tables back to back with no gap. There is no 8-byte block — those bytes are the body's own `01` + `u32` opening, the same as `.DAN`. Every body-parsing attempt before this began 8 bytes late. |
| `FUN_00454feb` gets the stream context; `FUN_00460d5f` refills from file | **wrong** | They are Watcom runtime: **`__CHK`** (stack probe) and **`printf_`**. Matched against the stock 10.6 libraries. The `CryoStream` struct built on that assumption is now marked unverified. |
| The `.DSN` body is one opaque compressed blob | **imprecise** | It is a **byte-oriented record stream**: a constant 7-byte preamble with its own `u16` version (180/181, 95/95 files), literal ASCII names surviving inside it (37/95 carry `DEFAULT` in the first 64 body bytes), a skewed byte histogram (`0x00` 6.6%) and markers `e0 fd 09` / `e0 fc 19` recurring roughly once per 170 bytes. |
| `GAME.DAT` / `GAME0.DAT` are zero-filled | **wrong** | `GAME.DAT` has `01` at `0x104`; `GAME0.DAT` has `Project0` at `0x2484` plus a structured tail. |

## Dead ends

- **No source code or engine leak exists** for Cryo's real-time 3D engine.
  Searched leak indexes and RE communities; nothing.
- **`360: Three Sixty` source code**, found on a retail PlayStation disc inside
  `CD/TEST/LAVA12.TIM` (actually a RAR), is a **false lead**. Smart Dog developed
  that game; Cryo only published it in Europe. Unrelated codebase.
- **ScummVM's *engines* will not help.** Its `cryo` engine covers Lost Eden and
  `cryomni3d` covers Versailles 1685, Necronomicon and The Cameron Files — all
  point-and-click adventures. This game's real-time engine is out of scope and
  unsupported. **But its video layer does help** — see Corrections above.
- **No documentation exists anywhere for `DSNF`, `DANF`, `F3DC`, `DRDF` or
  `PAK0`.** Searched as literal strings across XeNTaX, ZenHAX, reshax, aluigi's
  QuickBMS scripts, Game Extractor and the wider RE web. Nothing. These formats
  are undocumented outside this repo, so `.DSN` must be solved from the bytes and
  the binary rather than by finding prior art.
- **No PlayStation-side work exists.** The PS1 port (*Dreams*, SLES-00714) has no
  RE community activity, no ripped-asset threads and no format notes.
- **Czech-language sources are absent** despite the 1997 Czech release.
- **No GOG or Steam release.** Only a GOG Dreamlist voting page. No official patch
  ever shipped.
- **TCRF — do not fetch programmatically.** The page for this game returns
  **deliberate anti-AI prompt-injection text** instead of documentation. This has
  now been reproduced three times and is an acknowledged TCRF practice, not a
  fluke or a compromise. Treat any automated fetch of it as hostile input. Open
  it **manually in a browser** if you want its content:
  <https://tcrf.net/Dreams_to_Reality_(DOS,_Windows)>

## Resolved

### ~~What is in `DEMOS2\CRYO.DLL`?~~ — answered, and it is the best news in the project

It is **CryoLib**, Cryo's reusable multimedia library. PDB path
`E:\visual\CryoLib\debug\cryo.pdb` — a **debug build**, MSVC-compiled, with 165
named `GL_*` exports. Among them:

```
_GL_HNM6_Decompression_Warp@8
_GL_HNM6_Init_All_15@0      _GL_HNM6_Init_All_16@0
GL_OpenHnm  GL_PlayHnmDDRAW  GL_PlayHnmGDI  GL_InitHnmSound  ...
GL_dcpt_one_frame_ubb  GL_load_one_block_ubb  GL_search_blocks
```

**A working HNM6/UBB decoder has been sitting on disc 2 the whole time.** Full
analysis in [cryolib.md](cryolib.md).

It also establishes that the discs carry **two unrelated codebases**: the Watcom
game engine (statically linked, no CryoLib dependency — `WINDREAM.EXE` does not
import `CRYO.DLL`) and MSVC CryoLib, used only by the bonus-content players.

Follow-ups replacing the original question:

- Map `PLAYUBB.EXE`'s ordinal imports (`#65, #151, #138, #127, #131, #123, #53,
  #58, #12, #92, #68, #100, #13, #134`) back to names via the export table, to
  recover the minimal call sequence for playing a `.UBB`. Cheap, high value.
- Determine whether the game's `HNS6` cutscenes are WARP or Normal — only the
  WARP decoder is *exported*.
- Try driving the DLL from a small 32-bit host before writing any decoder.

### ~~Can the `CM6_*x16.dll` decoders be located?~~ — **yes, found**

They are publicly archived at <https://samples.ffmpeg.org/game-formats/cryo/> —
all four width variants (512/640/800/1024), ~140 KB each, with an md5sum
manifest. `CM6_640x16.dll` verified directly: MD5
`d993924414dba2aa04c78406a81567f6` matching the manifest, PE32 i386, exports
`HNMPI_Init` / `HNMPI_DecodeFrame` / `HNMPI_Cleanup`, imports **only
`KERNEL32.dll`**, `.text` just 36 KB.

Zero-dependency and pure computation, so it is both a decode oracle and a far
cleaner RE target than `CRYO.DLL`. The `640` build matches this game's 640×304
cutscenes exactly.

### ~~Is there a working HNM6 decoder?~~ — **yes, two**

**All 113 video files on both discs now decode.** NihAV's `na_game_tool` does it
after a one-line patch adding the `HNS6`/`UBS2` tags; ScummVM has a second,
independently written implementation. Verified end to end on this machine —
`ARENE.HNM` produced 113 coherent 640×304 frames.

This was the project's largest open problem and it is closed. Full instructions
in [hnm-video.md](hnm-video.md).

### ~~What are `.UBB` files?~~ — **video, generation 5**

Not subtitles, not audio sidecars, not a presentation toolkit. `.UBB` is Cryo's
**HNM5** video container, the generation introduced with *MegaRace II* (1996).
The 19 game files are 3 × `UBB2` (video only) and 16 × `UBS2` (video + embedded
`SD` sound), all 640×304 and all self-contained. NihAV reads them with a plugin
it labels "Cryo UBB".

There is **no UBIK authoring toolkit**. `.BF`'s `UBIK` tag is a separate
named-asset container that happens to share the name.

### ~~Does `HNS6` vs `HNM6` mark the audio-muxed variant?~~ — **no**

Settled by chunk census. The 73 `HNS6` files contain **20,576 `SD` sound chunks**
despite every one reporting `audioflags = 0`, while the single `HNM6` file
(`ABAL_USI.HNM`) has `IX` video chunks and no `SD`. The two tags are structurally
identical — same header, same 640×304, same `0x5F000` frame size — which is why a
one-line tag patch was enough to decode both.

### ~~Can `CUBE.ASC` be used as a known-plaintext attack on `F3DC`?~~ — **tried, failed**

**Negative result, worth keeping so nobody repeats it.** None of the 120 XYZ
values in `CUBE.ASC` appears in any of the 16 unique `.3DC`, 4 unique `.3DM` or
either `.PAK` — searched as IEEE-754 float32 **and** as 16.16 fixed point. No
`CUBE.3DC` exists on either disc.

So `F3DC` coordinates are stored packed, quantised or integer — not as a plain
float or 16.16 array. `3DS.BAK` was also parsed (a real 3D Studio binary scene:
`0x4D4D` root, 8 vertices, 12 faces, `Object01`/`Camera01`) and is a different,
simpler scene than the five-mesh `CUBE.ASC`. Both look like unrelated developer
tests rather than shipped content.

### ~~What is `DREAMS.DAT`?~~ — structure solved

Self-indexing: `u32[151]` offsets relative to `0x400`, 420 zero bytes, then 150
`ProjectN` records. `0x400 + offset[150]` equals the file size exactly on both
discs. Joining it to `DREAMS.INI` produced the complete
[level map](level-map.md).

Which copy is authoritative is **still open but low-stakes**: exactly six records
differ (P31, P41, P55, P69, P75, P87), with identical asset names and only
numeric fields changed.

### ~~What is the `.DSN` header?~~ — partially answered

`DSNF`/`DANF` share a 9-byte preamble with the file size stored **unaligned at
offset 5**, followed in `.DSN` by two counts and an **11-byte fixed-length name
table** (DOS FCB 8+3 style) holding object names. Verified against six files.
See [file-formats.md](file-formats.md).

### ~~Where are the textures and sounds?~~ — answered

Audio is **fully solved**: `FSB.DAT` (24 SFX, PCM mono 11025 Hz 16-bit) and
`DIALOG.DRD` (178 voice clips, PCM mono 11025 Hz 8-bit) are plain RIFF WAVE
banks with decoded index headers. Music is redbook CD audio.

Textures have **no standalone files** — level textures are packed inside the 98
`.DSN` scene files (157 MB, zlib 72%, a density comparable to compressed video).
Sprites, icons and alpha maps (`.SPR`, `.ALP`, `.BF`) are raw and uncompressed.

Full analysis in [assets.md](assets.md).

## Open questions

### Current list

1. **Map a model's UVs onto its texture page.** The references resolve — 100%
   of 26,827 corners land inside the record, 85 distinct `u`, 78 distinct `v`,
   at `ref - 5 + delta` — but rasterising gives flat colour blocks and no
   value exceeds 128 of a 256-wide atlas. See [models.md](models.md).
2. **Finish `.DAN` animation.** Tag 3 is one clip per record and the
   rotations are **keyframed unit quaternions**, Q15 `[x,y,z,w]` at `+0x2c` —
   norm 32768 in **14,304 of 14,304** records across all 159 models, 14,104
   of them exactly identity. How a frame composes onto the node's rest
   transform is not established, and several codec variants reuse the record
   differently. Not implemented in the exporter. See [models.md](models.md).
3. **`ARC.3DC`.** `.3DC` geometry is otherwise solved — it is a raw `F3DC`
   blob carrying the same node, no LZ, 165 nodes across 16 files, and `BOULE`,
   `EPEE` and `GUN` all close to **0 boundary edges**. `ARC` still has 15.
   `.3DC` UVs are unverified, as for models.
4. **Name the object behaviour classes.** Every `.DSN` object carries a
   20-byte record whose first word is a shared handler pointer with only five
   distinct values; 505 of 2,059 objects carry one. The 63 distinct
   `(word 0, word 1)` pairs do **not** align with object-name families, so the
   classes stay unnamed.
5. Does a merged install with `FULL.ID` present actually suppress disc swapping?
6. Why is disc 2's `HD.ID` binary (`01 00 00 00`) when disc 1's is text (`toto`)?

### Earlier list

Kept for context, ordered by expected value at the time. Items 1–3 below are
since resolved — see Corrections above and [scene-geometry.md](scene-geometry.md),
[models.md](models.md).

### 1. Unpack the `.DSN` body

**Located and decompiled: `FUN_004175bc`.** It is the *header* reader, not the
unpacker — it validates `DSNF`, reads the `u32` at offset 5 (confirming our
hand-derived layout from the binary's own code), the `u8` flag and the `u16`
count, memcpy's two blocks and returns. Reads go through a **peek/commit ring
buffer stream**, so scenes are streamed, never fully resident. Full analysis in
[dsn-loader.md](dsn-loader.md).

**Blocker found:** Ghidra is decompiling with the wrong calling convention.
Watcom uses `__watcall` (args in EAX/EDX/EBX/ECX); every signature is currently
wrong and arguments show up as `extraout_*`/`unaff_*`. Fix before going deeper.

Original note: Found by
searching for references to the `DSNF` string literal at `0x004c421e` — the
binary compares tags with memcmp, not immediates (zero LE-int forms exist).
Decompiling that function is the direct route to the packing scheme. See
[re-setup.md](re-setup.md) for the full parser map.

Original framing:

157 MB across 98 scenes — all level geometry and textures, packed with an unknown
scheme. The header and object-name table are decoded; the body is not. Everything
visual depends on this. Check whether CryoLib's `GL_UnpackLZW` / `LZWCRYO`
applies — no `LZWCRYO` tag appears inside any `.DSN`, so headerless if so.

### 2. Decode the `.3DC` geometry payload

The container is mapped — object count at `0x1C`, material/texture name slots,
and repeated `(count, absolute-offset)` descriptor pairs whose counts (3, 4, 6,
15, 26, 28, 44, 98 / 3, 6, 12, 156, 216, 288, 576) look like vertices and
indices. What the pointed-to blocks actually contain is unknown, and they do
**not** decode as float32 or as plain u16/u32 index arrays. The `CUBE.ASC`
shortcut is closed, so this has to come from the loader in `WINDREAM.EXE`.

### 3. Extract HNM audio

Video is solved; audio is not. The `SD` chunks are located and counted (20,576
across the HNS6 files, flags word `0x8400`) but `na_game_tool -ofmt wav` emits
nothing for them. ScummVM's `audio/decoders/apc.cpp` is the reference
implementation for Cryo APC. Also worth settling NihAV's own caveat that hnm6
"framerate and colours may be wrong" by A/B-ing against `CM6_640x16.dll`.

### 4. Does a merged install actually suppress disc swapping?

The marker-file scheme (`disc-layout.md`) suggests the game tests for
`DATA\1CD.ID` and `DATA\2CD.ID`. If both exist in one directory alongside
`FULL.ID`, disc prompts may vanish. Entirely untested — and complicated by the
fact that `FULL.ID` turns out to be the maxi-install marker.

### 5. Why is disc 2's `HD.ID` binary?

Disc 1: `74 6F 74 6F 0D 0A` (`toto\r\n`). Disc 2: `01 00 00 00`. Different length,
different type. Unexplained.

### 6. Are the `.3DM` blocks 128×128 RGB555 textures? — **answered: no**

Each `.3DM` is exactly 28 bytes + 3×32,768, and `128×128×2 = 32,768`, which
is what made the idea attractive. Rendered, it is noise.

The RGB555 half of the claim is also dead, and instructively so. It rested on
the unused high bit being clear in all 16,384 words of blocks 1 and 2 of
`ESSAI.3DM`. Across all four files **8 of the 12 blocks have bit 15 set**:
`GRILLE` 5,202/3,385/3,208 and `SPRITE` 5,297/4,150/6,621, against `ESSAI`
366/0/0 and `OMBRE2` 32/0/0. **The two blocks that were sampled are the only
clean ones in the corpus.** A property checked on one file, in the file where
it happens to hold.

### 7. Contact an ex-Cryo developer

Several are alive and findable, and one route is unusually promising: **Stéphane
Petit**, co-founder of Kheops Studio, stated in a 2023 French interview that he
and Benoît Hozjan — both credited on this game's R&D — **took Cryo's engine and
tools code with them** when Kheops was founded in 2003. So the codebase may
physically survive in private hands.

Also identified: **Emmanuel Chriqui** (main programmer, has a personal site) and
**Hubert Nguyen** (R&D, later 3dfx/NVIDIA) — the better contact for engine
internals. **Pascal Urro**, who wrote the HNM codec, could not be traced.

This is a social rather than technical lead, and it is the only one that could
plausibly produce source code.

## Useful properties discovered

Things that make future work easier:

- **The build was never cleaned.** 3D Studio exports (`CUBE.ASC`), backup files
  (`3DS.BAK`), anti-virus checksum caches (`ANTI-VIR.DAT`), 4DOS descriptions
  (`DESCRIPT.ION`) and inconsistent filename casing all shipped on the retail
  disc. Expect more developer residue.
- **French error strings survive in the English release** (`Debordement dans
  DAN_Load3DC`, the entire `SETUP.INI`), so the localisation was partial and
  original symbol names are intact.
- **One codebase, three targets, all Watcom.** DOS and Windows builds should share
  most logic; the DOS builds carry richer symbol residue and are the better
  reversing target.
- **Consistent four-character format tags** (`F3DC`, `DANF`, `DSNF`, `DRDF`,
  `PAK0`, `UBIK`, `HNM4`/`HNS6`, `UBB2`) point to a single shared chunk-IO layer.
  Reverse one loader and the pattern likely generalises.
- **`PAK0` embeds `F3DC` at offset 12** (`0x0C`) — one chunk, not several.
- **Header fields are derived, not just stored.** Both `.DSN` and `.DAN` carry a
  u32 "span" field that is an exact function of the record counts
  (`A = 31·B + 7` for `.DSN`; `A = bodyOffset − 9` for `.DAN`). When a field
  looks like a mysterious count, test it against the counts you already know.
- **Cryo reused one video codec family across their whole catalogue.** HNM
  generations 0/1/4/5/6 span Dune to Atlantis 2, which is why third-party
  decoders written for *other* Cryo games decode this one.
- **`DREAMS.DAT` entity headings are 12-bit fixed point angles.** Offset `+0x5C`
  in the 192-byte `OBJET` record holds an integer $0 \dots 4095$ representing
  yaw where $4096 = 360^\circ$ ($2\pi$). All 520 positioned scene entities fall
  strictly in this range.
- **`XH_.DAN` model axes face $+X$ in rest pose.** 3D Studio models were
  authored with the character facing $+X$, ponytail pointing backwards along
  $-X$, and wingspan outstretched along $\pm Z$. Aligning with $+Z$ forward in
  glTF / Babylon.js requires a $+90^\circ$ vertical rotation.
- **The engine's developer debug HUD in `WINDREAM.EXE` (`FUN_00416606`) reveals the exact runtime object struct fields:**
  Direct string labels from the developers: `Project Name`, `Object Name`,
  `Object Pos` $(x, y, z)$, `Object Speed`, `Object PHY Speed`, `Object Flags`,
  `Object Angle` (pitch, yaw, roll, heading), `Object 3D Col` (collision state),
  `Object Anim 0` & `Object Anim 1` (dual animation track blending), `Nombre d'objet`,
  `dernier objet`.
- **Runtime entity instantiation (`FUN_0041deb8`) and project loader (`FUN_0041f9db`) decoded:**
  - `Project` header: directional lights at `+0x18`/`+0x24`, ambient RGB at `+0x30`,
    animated video textures at `+0x3C`/`+0x5C`, target materials at `+0x6C`/`+0x8C`,
    camera FOV at `+0xA4`, canonical player spawn `(x, y, z)` at `+0xB4`, fog
    parameters at `+0xE0`, sky/clear color at `+0xF0`, spawn heading at `+0x10C`,
    day/night mode at `+0x138`, CD audio track at `+0x1F8`.
  - `OBJET` records: bitfield flags at `+0x34` (bit 0 = active on start, bit 1 = dynamic
    character, bit 8 = dormant/hidden), AI archetype at `+0x64` (1 = prop, 3 = creature
    attack, 5 = gnome patrol, 6 = flying aerial), velocity multiplier at `+0x68`, patrol
    route box index at `+0x6C`, health/dialogue at `+0x70`.
  - `.DSN` 20-byte records at `16 + 11*B`: word 0 allocation flag (`0x004741A0` vs `0x0`),
    word 1 relocated pointer, word 2 constant 3, word 3 compass normal (`0x202` North,
    `0x246` East, `0x286` West), word 4 surface friction/sound category.
- **`.DAN` Tag 3 skeletal animation decoded [verified]:**
  The `.3DA` entries in Directory 2 index sequential Tag 3 chunks decompressed by `FUN_004105eb`
  via Cryo's LZ codec. Track $i$ corresponds to slot $i$ of the model's explicit directory,
  **not** the geometry scanner's node $i$ (see the later harness correction below). Each track
  specifies duration, rotation count $K$, translation count, and Q15 keyframe rotations.
  The keyframe stride is 20 or 60 bytes and is determined from its start/end offsets.
  Composed hierarchically in `WINDREAM.EXE` (`FUN_0047e498`). Plain SLERP is the viewer's
  approximation; 60-byte keys use additional spline controls in the engine. Dual
  concurrent tracks (`Object Anim 0` / `Object Anim 1`) support motion blending. 780 clips extracted
  across 159 character and prop models to `E:\dreams-work\animations/`.
- **Correction — Keyframe offsets in Tag 3 [verified]:**
  An initial decode hypothesis assumed Hermite spline tracks (stride 60) had asymmetric key 0 layout
  starting at `+40` and `+80`, which inadvertently read tangent vectors instead of primary quaternions.
  Decompilation of `FUN_00459808` in `WINDREAM.EXE` revealed that each track has a 40-byte header
  followed by uniformly spaced keyframes: every key $k \in [0, K-1]$ begins at exact offset
  `trk_off + 40 + k * stride` (where `stride` is 20 or 60). Word 0 is frame timestamp; words 1..4 are
  the unit quaternion `[qx, qy, qz, qw]` in Q15 ($32768 = 1.0$); words 7..10 and 11..14 are Hermite
  candidate tangents. With the full offset table, validated **15,084 tracks and 142,806 keyframes
  in 780 clips from 159 distinct models**, with 0 invalid strides or nonmonotonic timestamps.
- **Correction — track 0 was omitted from every decoded clip:**
  The dword at payload `+0x18` is the offset of track 0, not a table span. The previous parser began
  at `+0x1C` and read only $N-1$ offsets, shifting every animation track onto the preceding model
  node. The executable iterates the complete track table, and raw track 0 records have valid keys.
  `XH_` now yields 27 tracks for 27 model nodes; its corrected pose no longer has the severe
  head/arm distortion seen in the earlier viewer.
- **Animation names are numeric runtime IDs, not action labels:**
  All 780 embedded `.3DA` names follow an `AN###` pattern. `FUN_00404d98` parses
  that suffix with `FUN_004551b5` and fills the corresponding runtime slot;
  `FUN_00455278` retrieves slots by number. The executable has “Run”/“Walk”/
  “Jump” control text, but no discovered string table translates those actions
  into clip IDs. See `docs/models.md` for the lookup path.
- **Correction — no rest quaternion in the 40-byte track header:**
  Offset `+0x28` is the first keyframe's timestamp. Reading four integers there as a rest quaternion
  mixes that timestamp with three quaternion components. The parser now uses key 0 as its fallback
  rotation. This metadata correction does not change sampled poses for tracks with keys.
- **Engine matrix and skinning evaluation in `WINDREAM.EXE` [verified]:**
  - `FUN_0045bc28`: converts Q15 quaternion `[qx, qy, qz, qw]` to a $3 \times 3$ row-major rotation matrix.
  - `FUN_0047e498`: forward kinematics down the scene graph:
    $$R_{world} = (R_{parent} \times R_{child}) \gg 15$$
    $$T_{world} = ((R_{parent} \times T_{child}) \gg 15) + T_{parent}$$
  - `FUN_00478dac`: vertex skinning evaluating:
    $$V_{world} = ((R_{world} \times V_{local}) \gg 15) + T_{world}$$
  - Cyclic animation loops: for all action cycles, $t=0$ holds the identity rest pose $(0, 0, 0, 1.0)$,
    while frames $1 \dots \text{duration}$ form the seamless repeating motion loop where $Q(1) \equiv Q(\text{duration})$.

## 2026-09-23 — named rig and animation hypothesis harness

`uv run python -m dreams.animation_harness` now compares explicit directory,
physical scan, and alphabetical mappings, XYZW/WXYZ/conjugate conventions,
and local versus world rotation. It measures loose elbow/knee limits at stored
keys, keeps outlier clip/frame references, and checks unnormalized Q15 data.

The stronger independent check is the translation channel: first local
translation keys match named child rest positions in **1,258/1,274** cases
using the explicit directory, versus **0/1,274** in physical geometry order.
Elbow/knee flags fall from **29.50% to 2.20%**; both knees have zero flags
under the stated envelopes. The earlier claim that restoring track zero
fully settled the track mapping was premature.

Names are stored at serialized node `+0x14`, length 12 bytes. The Tag 1 slot
table at `+0x18` resolves 1,967 named nodes across 159 models, including 81
not found by the geometry scanner. `CH0`'s extra slot is `bassin01` with zero
vertices. The player now binds through the table; the inspector shows names.

Two further interpretations were corrected: track `+0x1c` is translation-key
count, and the alleged FPS after the slot table was the root rotation-key
count. The UI now labels 10 fps as a preview rate. Static translations and
approximate spline playback remain limitations, not validated engine behavior.

Details, repeatable commands, and residual cases are in
[animation-validation.md](animation-validation.md).

## 2026-09-23 — original animation clock recovered

Followed `timeGetTime` through `00440802` / `00440890`, dispatcher command
`0x11`, and the main loop at `004170a6`. The timer initializes at 200 Hz
(`00415fdd`); the main loop computes `200 / elapsed_ticks` and then
`30 / measured_fps`. Actor frame `+0x170` advances by that delta times
speed `+0x178`, which action transitions reset to 1.0. This establishes a
**30 frames/second nominal base**, with separate actor/state modifiers.

Updated the viewer from the temporary 10 fps rate to 30 fps at 1x, removed
the guessed walk/run multipliers, and regenerated the 780 local clip exports.
New binary-evidence tests pin the initialization, arithmetic constants and
instructions, speed reset, and frame increment to the shipped executable.

Also corrected the earlier assertion that the executable has only one clock:
it imports `QueryPerformanceCounter`, but the animation path examined uses
the `timeGetTime` counter. Timer rounding and the original 0.2–5.0 delta
clamp are documented and are not copied into the browser player.
See [animation-timing.md](animation-timing.md) for the complete trace and limits.

## 2026-09-23 — translation curves and common animation blend path

Recovered the translation Hermite basis at `004aa710` and left-outgoing /
right-incoming tangent reads in `0045a03c`. The decoder now preserves 82,784
translation keys across 780 clips; the shared player evaluates and blends them.
The inspector can display full root travel or hold horizontal motion in place,
mix two clips, and play clips once. The controller exposes loop-corrected root
deltas without interpreting seeks or pose transitions as locomotion.

`00405f1f` holds outgoing and incoming-start poses while weight `+0x164`
advances by `48 * engineDelta`; the ordinary duration is `256/(48*30)` seconds.
`00405db4` commits the incoming channel. This common transition mode now
replaces Duncan's guessed crossfade time. Root displacement flows through
movement/collision code before `0043d83e` writes the resolved root position;
that original physics consumption is not yet integrated in the viewer.

See [animation-root-blending.md](animation-root-blending.md) for offsets,
precision differences, runtime controls, and regression tests.

## 2026-09-24 — boot sequence decompiled, videos and menus named

Followed the player-visible boot flow through `WINDREAM.EXE` end to end:
`entry 00465538` → `main 0048646d` → `Game_Run 0041745e` →
`BootScreen 00436481`. The observed sequence — intro movie, short animation,
menu, new game, one more animation, first map — maps to: **`INTRO.HNM`**
(2781 frames, skippable, event `0x17` on the dispatcher at `0043a306`), then
**`GENERIC.HNM`** (101 frames — a light-speed tunnel; frames decoded and
inspected), which runs as the animated background of the **2×2 main menu**
whose label table sits at `0x4a2ed5`: `NEW GAME` / `LOAD A GAME` / `OPTIONS`
/ `QUIT`, drawn over `data\tga\menu.tga` with `icones.bf` icons and the
`UpLf/UpRg/DnLf/DnRg` corner markers. The lowercase `Load`/`Options`/`Quit`
strings belong to the separate in-game pause menu (`004337c0` → `00432b45`).

Two data corrections came out of it. First, the new-game project video is
**data-driven** — each decompressed `DREAMS.DAT` project record names its
video (in-memory at `+0x3c`), and Project 0's record names
**`ETE_E~1.HNM`, a file that exists on neither disc**; the open fails and the
play is skipped. What ships instead is hardcoded in `Transition_Tick
004240ba`: a one-shot latch (`DAT_0049da28`) that plays
**`data\hnm\tete_e~1.hnm`** — the bearded elder's 313-frame talking-head
briefing — immediately before `Scene_SpawnProjectEntities 0041f9db` loads
the map through the `Please wait while loading ...` / CD-swap screen
(`00427d64`, `LISTL%d.txt` manifests). Second, between menu confirm and the
briefing there is a rendered, non-video **15-second in-engine transition**
(`_DAT_005e5480 = 15.0` armed on new game, counted down in `004240ba`).

`LISTL0.TXT` is confirmed as the always-resident universe
(`H03PAQUE.DSN` + `CH0/HOLO/XH_/MHE`), and `LISTL1.TXT` opens with
`H18ANGKR.DSN` — the first map is reached through it. Full function map,
event table, and reproduction commands are in
[boot-sequence.md](boot-sequence.md).

## 2026-09-24 — menu sprites decoded: the `TABLE` family

The `ICONES.BF` members — the actual menu sprites — are a third sprite family,
now fully decoded (loader `FUN_00426c46`): 512-byte RGB555 palette, pixel
blobs, a `"TABLE"` marker, then 28-byte descriptors (width `+4`, height `+8`,
absolute pixel offset `+0x18`). Pixel depth is per file, recovered from the
offset stride: 8-bit indexed (`TOUCHES.SPR`, `TITRES.SPR`) or direct RGB555
(`MAGIE`/`ANIM`/`PYRAM`/`INTERF` `.ALP`, and `DATA\OBJET\SOUR.ALP`, the
two-frame mouse cursor). Every member's pixel data ends exactly at its TABLE
marker — the format reading is exact, not fitted. Rendered contact sheets
confirm it visually: golden `NEW GAME`/`LOAD A GAME`/`OPTIONS`/`QUIT` titles
in three states (TITRES, disc 2 only), wireframe spell-selector pyramids,
yellow joypad key caps, 40×40 inventory icons.

Sprite names are **not** in the files: the engine resolves a 72-entry name
table at `0x49db12` → `(bank, slot)` at `0x49dd9a`, bank filenames at
`0x49dacc`. The map labels every sprite — inventory items occupy MAGIE slots
0–30 in exactly the `DREAMS.INI` order, the menu corner markers are INTERF
slots 0–7, joypad caps TOUCHES 0–10. Decoder: `dreams.formats.image.
read_menu_sheet` + tests in `tests/test_menu_sprites.py`.

Two corrections follow. `.ALP` is **not** an "alpha map" — it is this sprite
bundle format (`.SPR`/`.ALP` extensions carry the same layout here). And
`data\tga\menu.tga`, which boot code tries to load, **exists on neither disc**
— `DATA\TGA\` is empty on disc 1 — so the shipped menu background is the
looping `GENERIC.HNM` video, not a TGA.

## 2026-09-24 — menu sprite pixels are byte-swapped RGB555

The first render of the `ICONES.BF` 16-bit sprites was wrong in colour: the
main menu's corner brackets came out green. Against a real in-game screenshot
the brackets are blue/cyan, and the sprite's brightness ramp lives entirely
in bits [9:5] — green under a little-endian RGB555 read, blue under a
**byte-swapped** one. Only the swapped read reproduces the screenshot's
colours (`0x0200` ramp → blue, `0x3F10` → saturated blue, `0x397B` → cyan).
Palette-based files are unaffected — TITRES renders gold/red/dark-red title
states on a blue-violet glow, coherent across all three variants — so the
palette stays little-endian and only the 2-byte pixels swap.
`menu_sprite_rgba` now swaps 2-byte pixels; a regression test pins the UpLf
bracket to a blue-dominant histogram. The engine-side blit that reconciles
the stored byte order with the framebuffer is not yet located (candidates
`FUN_00436ab6` region checked were the credits scroller and the single-sprite
loader `FUN_00427020`).

## Sources

- [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Dreams_to_Reality)
- [DxWnd thread — running the Windows build](https://sourceforge.net/p/dxwnd/discussion/general/thread/a2ddabcd22/)
- [VOGONS — how to successfully run it](http://www.vogons.org/viewtopic.php?t=23325)
- [VOGONS — install problems](http://www.vogons.org/viewtopic.php?t=29775)
- [VOGONS — general thread](https://www.vogons.org/viewtopic.php?t=15033)
- [DOSBox Staging issue #2888](https://github.com/dosbox-staging/dosbox-staging/issues/2888)
- [MultimediaWiki HNM6](https://wiki.multimedia.cx/index.php/HNM6) — mirrored in `hnm6-spec.md`
- [MultimediaWiki CRYO APC](https://wiki.multimedia.cx/index.php/CRYO_APC)
- [ZenHAX — HNM6 tool request](https://zenhax.com/viewtopic.php@t=15513.html)
- [GameMediaFormats DirectShow filters](https://github.com/ValeryAnisimovsky/GameMediaFormatsCoreFilters)
- [Internet Archive copy](https://archive.org/details/msdos_DREAMS_to_Reality_1997)
- [MobyGames](https://www.mobygames.com/game/6882/dreams-to-reality/)
- [Redump verification thread](http://forum.redump.org/viewtopic.php?pid=139515)
- [NihAV `na_game_tool`](https://nihav.org/game_tool.html) — working HNM4/5/6 decoder, GPLv3
- [codecs.multimedia.cx](https://codecs.multimedia.cx/2025/12/) — Kostya Shishkov's HNM5/HNM6 development notes
- [ScummVM `video/hnm_decoder.cpp`](https://github.com/scummvm/scummvm) — second independent HNM6 decoder
- [samples.ffmpeg.org Cryo archive](https://samples.ffmpeg.org/game-formats/cryo/) — the `CM6_*x16.dll` vendor decoders
