# Research log

## Corrections

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

Ordered by expected value.

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

### 6. Are the `.3DM` blocks 128×128 RGB555 textures?

Each `.3DM` is exactly 28 bytes + 3×32,768, and `128×128×2 = 32,768`. In
`ESSAI.3DM` the RGB555 unused high bit is clear in all 16,384 words of blocks 1
and 2. All four `.3DM` names are also `.3DC` material names. Render one and find
out — ten minutes of work, and it would give the project its first standalone
texture.

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
