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

## Dead ends

- **No source code or engine leak exists** for Cryo's real-time 3D engine.
  Searched leak indexes and RE communities; nothing.
- **`360: Three Sixty` source code**, found on a retail PlayStation disc inside
  `CD/TEST/LAVA12.TIM` (actually a RAR), is a **false lead**. Smart Dog developed
  that game; Cryo only published it in Europe. Unrelated codebase.
- **ScummVM will not help.** Its `cryo` engine covers Lost Eden and `cryomni3d`
  covers Versailles 1685, Necronomicon and The Cameron Files — all point-and-click
  adventures. This game's real-time engine is out of scope and unsupported.
- **No GOG or Steam release.** Only a GOG Dreamlist voting page. No official patch
  ever shipped.
- **TCRF** has a page for the game but it could not be read through automated
  fetch — the fetched content came back as prompt-injection text rather than
  documentation. Open it manually:
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

### ~~Can the `CM6_*x16.dll` decoders be located?~~ — superseded

Still absent from these discs, but `CRYO.DLL` makes it unnecessary as a first
resort. Keep as a cross-check if CryoLib's decoder proves demo-specific.

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

### 2. Does `HNS6` vs `HNM6` mark the audio-muxed variant?

73 files use `HNS6`, 2 use `HNM6`; `.UBB` shows the same `UBS2`/`UBB2` split. The
wiki documents both an A/V-multiplexed and a stand-alone HNM6 form.

Test: scan frame chunks of one file of each signature for `AA`/`BB` chunk IDs.
Cheap to do and would settle it.

### 3. Which `DREAMS.DAT` is authoritative?

Disc 1 has 138,879 bytes, disc 2 has 138,835. It is a monotonically increasing
table of little-endian u32 offsets. Picking the wrong copy in a merged install
could break asset lookup. Diff the two tables and identify what they index.

### 4. Does a merged install actually suppress disc swapping?

The marker-file scheme (`disc-layout.md`) suggests the game tests for
`DATA\1CD.ID` and `DATA\2CD.ID`. If both exist in one directory alongside
`FULL.ID`, disc prompts may vanish. Entirely untested — and complicated by the
fact that `FULL.ID` turns out to be the maxi-install marker.

### 5. Why is disc 2's `HD.ID` binary?

Disc 1: `74 6F 74 6F 0D 0A` (`toto\r\n`). Disc 2: `01 00 00 00`. Different length,
different type. Unexplained.

### 6. Can `CUBE.ASC` be used as a known-plaintext attack on `F3DC`?

`DATA\OBJET\CUBE.ASC` is a plain-text 3D Studio ASCII export left on the disc
(alongside `3DS.BAK`). If a corresponding `.3DC` exists, diffing a readable scene
description against its binary form would crack the geometry format quickly.
Check whether a `CUBE.3DC` ships anywhere.

### 7. What are `.UBB` files structurally?

Three magics (`UBB2`, `UBS2`, `HNM6`). `PLAYUBB.EXE` plays them. Some are simply
HNM6 videos renamed. "UBIK" also tags `.BF` files, suggesting a shared internal
Cryo toolkit.

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
- **`PAK0` embeds `F3DC` at offset 16**, giving a cheap entry point into the
  geometry container.

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
