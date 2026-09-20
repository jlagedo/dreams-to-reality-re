# Dreams to Reality (1997) — Research Repo

Knowledge base for reverse-engineering, preserving and running Cryo Interactive's
*Dreams to Reality* (1997, DOS/Windows). This folder holds notes and tools only —
the disc images live elsewhere (see **Assets on disk**).

Everything below is marked **[verified]** (observed directly in the binaries/files
on this machine), **[sourced]** (from external documentation, linked) or
**[unverified]** (inference, not yet tested).

## Detailed docs

This file is the summary. The detail lives in [`docs/`](docs/README.md):

| Doc | Contents |
|---|---|
| [docs/re-setup.md](docs/re-setup.md) | **Ghidra + MCP setup**, RE repo layout, order of attack |
| [docs/engine.md](docs/engine.md) | Engine architecture, Watcom toolchain, the four binaries, Windows video API |
| [docs/toolchain.md](docs/toolchain.md) | **Watcom C/C++ 10.6** pinned down, reference material, recovered runtime symbols |
| [docs/cryolib.md](docs/cryolib.md) | `CRYO.DLL` = CryoLib: 165 exports including a working **HNM6 decoder** |
| [docs/game-content.md](docs/game-content.md) | 150 levels, 30 inventory items, save system, from `DREAMS.INI` |
| [docs/level-map.md](docs/level-map.md) | **Complete project → scene map** — all 150 projects to 98 `.DSN` files |
| [docs/dsn-loader.md](docs/dsn-loader.md) | **`.DSN` loader decompiled** — stream API, header reader, `__watcall` blocker |
| [docs/assets.md](docs/assets.md) | **Models, textures, animation, sound** — where content lives and how it's packed |
| [docs/file-formats.md](docs/file-formats.md) | Asset format catalogue with verified magic numbers |
| [docs/hnm-video.md](docs/hnm-video.md) | HNM inventory — HNM4 vs HNM6/HNS6, resolutions, frame counts |
| [docs/hnm6-spec.md](docs/hnm6-spec.md) | Full HNM6 container + codec spec (MultimediaWiki, mirrored) |
| [docs/disc-layout.md](docs/disc-layout.md) | Both discs inventoried, install manifest, disc check, merge map |
| [docs/running.md](docs/running.md) | How to run it, ranked by difficulty, with DOSBox config |
| [docs/research-log.md](docs/research-log.md) | Corrections, dead ends, resolved items, open questions |

---

## 1. Game identity

| | |
|---|---|
| Title | Dreams to Reality |
| Alt titles | *Dreams* (PlayStation port, SLES-00714); Мечты во сне и наяву (RU) |
| Year | 1997 (NL release 1998) |
| Developer | Cryo Interactive Entertainment |
| Publishers | Cryo Interactive, R&P Electronic Media, Interplay Entertainment |
| Platforms | DOS, Windows, PlayStation |
| Genre | Action / adventure, behind-view 3D, fantasy, puzzle elements, real-time |
| Releases | Czechia, France, Germany (1997), Netherlands (1998) |
| DRM | Disc check — requires the CD in the drive |

Not on GOG or Steam. Only a [GOG Dreamlist vote page](https://www.gog.com/dreamlist/game/dreams-to-reality-1997)
exists. No official re-release or patch has ever shipped. **[sourced]**

---

## 2. Assets on disk

Nothing here is inside this repo. Paths on this machine:

```
E:\dev_game\
├── Dreams-to-Reality_Manual_Win_de.pdf                    3.6 MB  German manual
├── Dreams-to-Reality_Win_EN_Disc-Image-Disk-1.zip       548.9 MB  TorrentZip
├── Dreams-to-Reality_Win_EN_Disc-Image-Disk-2.zip       568.0 MB  TorrentZip
├── Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\
│   ├── Dreams to Reality (Europe) (Disc 1).cue
│   ├── ... (Track 01).bin   382,134,144 B   MODE1/2352 data
│   ├── ... (Track 02..12).bin                11 CD-audio tracks
│   ├── disc1.iso                             derived, 2048 B/sector
│   └── extracted\                            1219 files, 27 folders, 319 MB
└── Dreams-to-Reality_Win_EN_Disc-Image-Disk-2\
    ├── Dreams to Reality (Europe) (Disc 2).cue
    ├── ... (Track 01).bin   435,646,848 B   MODE1/2352 data
    ├── ... (Track 02..14).bin                13 CD-audio tracks
    ├── disc2.iso                             derived, 2048 B/sector
    └── extracted\                            409 files, 26 folders, 362 MB
```

Redump-style dumps: raw 2352-byte sectors, mixed-mode CD. Track 1 is the ISO9660
filesystem; all remaining tracks are **redbook audio — this is the game's music**,
and it does not appear as files in the extracted tree. **[verified]**

### Scratch space

Analysis scratch — agent briefs, worker transcripts, extracted samples, one-off
dumps — goes in **`E:\dreams-work\`**, outside the repo:

```
E:\dreams-work\
├── codex\<slug>\     brief.md, events.jsonl, last.md per worker run
└── websweep\         web research findings
```

Never write scratch into the repo: `tools/re-checkpoint.ps1` refuses to commit
if anything matching a game-asset extension appears in the working tree, and the
discs' contents must never enter git. Never write it into another project's work
directory either — `E:\elysium-work\` is a different project.

### Reproducing the extraction

`.bin` track 1 is raw 2352 B/sector. Strip the 16-byte sync/header and the
288-byte ECC tail to get a mountable 2048 B/sector ISO:

```python
with open(src, "rb") as f, open(dst, "wb") as o:
    while True:
        s = f.read(2352)
        if len(s) < 2352:
            break
        o.write(s[16 : 16 + 2048])
```

Then `7z x disc2.iso -oextracted`.

---

## 3. Disc contents

### Disc 1 — program disc

All *game* executables live here. **[verified]**

| File | Size | Notes |
|---|---|---|
| `SETUP.EXE` | 258,320 | Windows installer (MSVC-built, normal PE sections) |
| `WINDREAM.EXE` | 864,768 | Windows game, DirectDraw default. MD5 `68BC8D52...` |
| `GDIDREAM.EXE` | 864,768 | Same build, GDI/windowed default. MD5 `DA940964...` |
| `DREAMS.EXE` | 1,176,454 | DOS build, LE + DOS/4GW |
| `DREAMSFX.EXE` | 1,025,154 | DOS 3dfx/Glide build, LE + DOS/4GW |
| `DOS4GW.EXE` | 265,396 | Rational DOS extender |
| `UNINSTAL.EXE` | 35,328 | |
| `GETKEY.EXE` | 4,469 | keypress helper for the DOS `.BAT` installers |
| `INSTALL.BAT` / `INSTALFX.BAT` | | DOS and DOS-3dfx installers |
| `INST_SON.BAT` / `INST_SFX.BAT` | | called by the above |
| `SETUP.INI` | 5,091 | file manifest for mini vs maxi install |
| `README.TXT` | 5,322 | official install/controls doc |
| `3DFX\` | | Glide runtime; `GRTVGR.EXE` is a PKZIP archive containing `glide2x.ovl` |
| `DIRECTX\` | 43 MB | DirectX 5 redistributable |
| `DEMO0\`, `DEMOS1\` | | demos of other Cryo games, not needed |

`DATA\` on disc 1 (268 MB): `3DC` 185 files / 121.6 MB, `HNM` 49 / 136 MB,
`ANIM` 6 / 6.8 MB, `SOUND` 17, `OBJET` 9, `FONT` 3, `LANG` 2, `UNIVBE` 2,
`ICONE` 1, and empty `GAME`, `SYM`, `TGA`.

### Disc 2 — data disc

No *game* executables — but it ships **`DEMOS2\CRYO.DLL`**, which turned out to be
**CryoLib**, a debug-built MSVC library exporting a working **HNM6 decoder**
(`_GL_HNM6_Decompression_Warp@8`). See [docs/cryolib.md](docs/cryolib.md).
Also `PLAYUBB.EXE`, `PLAYTGA.EXE`, `SETSOUND.EXE`, `MSSW95.EXE`, `UVCONFIG.EXE`.

`DATA\` 273 MB: `HNM` 44 files / 161 MB, `3DC` 147 / 85 MB,
`ANIM` 14 / 24 MB, `ICONE` 5 / 3 MB, `TGA` 7, `OBJET` 10, `SOUND` 17,
plus `GAME\` (`GAME.DAT`, `GAME0.DAT`, `GAME0.ICO`) which is **empty on disc 1**.
Extras: `CRYOPLUS\` (20 TGA images, 12 MB) and `DEMOS2\` (75 MB of demos). **[verified]**

---

## 4. Engine

**No third-party engine.** Cryo's own in-house C/C++ codebase, with no public or
marketed name — distinct from the "Omni3D" branding Cryo used on their
point-and-click adventures. **[verified]**

Identifying marks found in the binaries:

- Build path `X:\CRYO\DREAMS\`, copyright string `CRYO 1997`
- Internal symbols leak: `DAN_Load3DC`, `Dreams_MSS1`, `Water surface`
- Untranslated French error: `Debordement dans DAN_Load3DC` ("overflow in...")

### Toolchain

All four game executables were built with **Watcom C/C++ 10.6** (August 1996),
including the Windows ones. `WINDREAM.EXE` contains `WATCOM C/C++32 Run-Time
system` and has Watcom's telltale PE section names `AUTO` / `DGROUP` instead of
MSVC's `.text` / `.data`. So this is **one portable C core compiled three ways**
with swappable platform backends. **[verified]**

The version was established by matching the binaries against the runtime
libraries of 10.5, 10.6 and 11.0c; the same libraries then recovered **287
named C runtime functions in `DREAMS.EXE`** and 119 in `WINDREAM.EXE`, via
`python -m dreams.watcom`. Full method, reference-material inventory and the
"is there source / are there PDBs" answer: [docs/toolchain.md](docs/toolchain.md).
Reference material lives outside the repo, in `E:\dev_game\watcom\`.

| Build | Binary format | Video backend | Audio backend |
|---|---|---|---|
| DOS | LE + DOS/4GW | SciTech **UniVBE** (VESA SVGA) | **Miles Sound System** (RAD) |
| DOS 3dfx | LE + DOS/4GW | **Glide** (`grGlideInit`, `glide2x`) | Miles Sound System |
| Windows | Win32 PE, subsystem 3.10 | **DirectDraw**, GDI fallback | DirectSound + MCI |

Miles, UniVBE and Glide are middleware, not the engine. The renderer proper is
Cryo's **own software rasterizer** — the DOS and Windows paths only ever ask the
OS for a linear framebuffer to draw into. **Glide is the sole hardware-accelerated
path, and it exists only in DOS.**

### Windows video API — DirectDraw only

No Direct3D, no OpenGL, no Glide in the Windows build. Import table: **[verified]**

```
DDRAW.dll     DirectDrawCreate
DSOUND.dll    DirectSoundCreate
GDI32.dll     CreateDIBSection, CreateCompatibleDC, StretchBlt, SelectObject, ...
WINMM.dll     mciSendCommandA, mciGetErrorStringA, timeGetTime, joyGetPosEx
```

Only `DirectDrawCreate` is imported; everything after goes through COM vtables.
The string `Can't go to Direct Draw 2` shows it `QueryInterface`s for
**IDirectDraw2** (DirectX 2 era), even though the readme asks for DirectX 5.
DirectDraw does 2D surface management only — the GPU renders none of this game.

`WINDREAM.EXE` and `GDIDREAM.EXE` are the **same program**: identical size,
identical imports, different hash. Both link DirectDraw *and* the GDI path
(`CreateDIBSection` + `StretchBlt`); the binaries just default to different
backends.

Three consequences that explain every reported symptom:

- **`mciSendCommandA` on the `cdaudio` device** plays the music straight off the
  redbook tracks. The `MCI Error` users report is the game's own string, fired
  when that device is missing.
- **`timeGetTime` is the only clock.** An uncapped loop on modern hardware drives
  the 30+ FPS physics bug that kills you on landing.
- **Palettized DirectDraw mode-setting** is what breaks on Windows 11 — the DWM
  has no true 8-bit exclusive-fullscreen palette path. Produces
  `Can't set DirectDraw mode` / `Can't create primary surface under DirectDraw`.

---

## 5. Asset formats

All proprietary Cryo formats. **[verified]** unless noted.

| Ext | Contents |
|---|---|
| Ext | Magic | Contents |
|---|---|---|
| `.HNM` | `HNM4` / `HNS6` / `HNM6` | Video. 20 files are HNM4 256x256 texture animations; 75 are 640x304 cutscenes |
| `.UBB` | `UBB2` / `UBS2` | **Video** — HNM generation 5, played by `PLAYUBB.EXE` |
| `.3DC` / `.3DM` | `F3DC` | Geometry and (probably) textures — **same tag, different structures** |
| `.DAN` | `DANF` | Animation |
| `.DSN` | `DSNF` | Scene / level definition |
| `.PAK` | `PAK0` | Container of `F3DC` chunks |
| `.BF` | `UBIK` | Icon/bitmap bundle (`ICONE\ICONES.BF`) |
| `.DRD` | `DRDF` | Dialog bundle — `DIALOG.DRD` is **24.6 MB** |
| `.DIG` | `AIL3DIG` | **Miles sound-card drivers, not game audio.** Audio bank is `SOUND\FSB.DAT` |
| `.SPR` / `.ALP` | none | Sprite bundles — 8-bit indexed + inline palette; alpha maps |
| `.ASC` / `.BAK` | text | **3D Studio developer leftovers** shipped on the retail disc |
| `.TGA` | Targa | Truevision Targa — standard |

Full detail and header dumps in [docs/file-formats.md](docs/file-formats.md).

### HNM6 video

Documented on [MultimediaWiki](https://wiki.multimedia.cx/index.php/HNM6):
hi-color, JPEG-like key blocks plus motion blocks derived from previously drawn
blocks, 8x8 or smaller. Audio is Cryo **APC**, either muxed into the `.HNM` or in
sidecar files. **[sourced]**

**Working decoders exist and all 113 video files on these discs decode.**
**[verified]**

- **[NihAV `na_game_tool`](https://nihav.org/game_tool.html)** (Rust, GPLv3) added
  HNM5 and HNM6 in Jan 2026. It needs a **one-line patch** to accept this game's
  `HNS6`/`UBS2` tag spellings, after which 113/113 files decode. Verified here:
  `ARENE.HNM` → 113 coherent 640×304 frames.
- **ScummVM** `video/hnm_decoder.cpp` is a second, independently written decoder
  (added 2022), plus `audio/decoders/apc.cpp` for Cryo APC audio.
- **Cryo's own `CM6_*x16.dll`** vendor decoders are publicly archived at
  <https://samples.ffmpeg.org/game-formats/cryo/> — MD5-verified, exporting
  `HNMPI_Init` / `HNMPI_DecodeFrame` / `HNMPI_Cleanup`, importing only
  `KERNEL32`. 36 KB of `.text` makes this a cleaner RE target than `CRYO.DLL`.

Video is solved; **audio extraction is not** — the `SD` chunks are located but not
yet decoded. Every sixth-generation header credits `Pascal URRO  R&D`.

See [docs/hnm-video.md](docs/hnm-video.md).

### Level manifests

`LISTL0.TXT` ... `LISTL4.TXT` (identical on both discs) list the assets per level,
e.g. `DATA\3DC\H03PAQUE.DSN`, `DATA\ANIM\H03AN001.HNM`. `DREAMS.DAT` is a binary
offset table (little-endian u32 offsets starting at 0). **[verified]**

---

## 6. The disc check

The game identifies its location by probing for tiny plaintext marker files in
`DATA\`: **[verified]**

```
DATA\1CD.ID    "kjk\r\n"        present only on disc 1
DATA\2CD.ID    "kjk\r\n"        present only on disc 2
DATA\FULL.ID   "toto\r\n"       disc 1 only — "full install" marker
DATA\HD.ID     "toto\r\n"       on disc 1
DATA\HD.ID     01 00 00 00      on disc 2 (binary, differs!)
```

`WINDREAM.EXE` hardcodes the probe path `X:\CRYO\DREAMS\DATA\HD.ID`.

`SETUP.INI` shows `FULL.ID` is written **only by the maxi install**, so it marks
installation size, not disc independence — an earlier reading of it as a
"no-disc-needed" flag was overstated. `1CD.ID` / `2CD.ID` identify the mounted
disc; `HD.ID` marks a hard-disk install.

**[unverified]** These are still the likely "two files requiring special handling"
the DxWnd thread cites when merging both CDs, and a merged directory containing
both `1CD.ID` and `2CD.ID` may suppress disc prompts. Not tested by running the game.

---

## 7. Merging both discs

Of 409 disc-2 files, 170 names collide with disc 1 and only **10 differ in
content**: **[verified]**

| File | Disc 1 | Disc 2 | Resolution |
|---|---|---|---|
| `DATA\HNM\INTRO.HNM` | 38.1 MB | 1.7 MB | **keep disc 1** — full intro |
| `DATA\ICONE\ICONES.BF` | 372,358 | 440,029 | **keep disc 2** — larger |
| `DREAMS.DAT` | 138,879 | 138,835 | **unresolved** — diff before choosing |
| `DATA\UNIVBE\UVCONFIG.EXE` | 309,382 | 269,682 | keep disc 1 (DOS-only) |
| `DATA\HD.ID` | `toto` | `01 00 00 00` | **keep disc 1** — see §6 |
| `DATA\3DC\DESCRIPT.ION` | 723 | 386 | junk (4DOS descriptions) |
| `ANTI-VIR.DAT` x3 | 192/256/192 | same sizes | junk (Central Point AV checksums) |
| `DEMO0\SETUP.GID` | 8,628 | 8,628 | junk (Windows help index) |

Merged size is roughly **480 MB** excluding demos and the DirectX redistributable,
matching the readme's 500 MB minimum / 1 GB maximum install.

---

## 8. Running it

Ranked easiest to hardest. None verified on this machine yet. **[sourced]**

### 1. DOS build (`DREAMS.EXE`) in DOSBox — easiest

DOSBox solves three problems for free:

- `imgmount d "...(Disc 1).cue" -t iso` mounts the data track **and** all audio
  tracks as a real CD, so the disc check passes, redbook music plays, and there
  are no MCI errors.
- `cycles` throttling is the clean fix for the frame-rate physics bug.
- No DirectDraw at all.

The remaining issue is VESA/UniVBE. Reported working with `machine=svga_s3`.
Use **DOSBox-X** first; **DOSBox Daum** is the known-good fallback (it's what
eXoDOS ships for this title, and it carries fixes not in upstream or Staging).

### 2. DOS 3dfx build (`DREAMSFX.EXE`) — best looking

The only hardware-accelerated path. VOGONS reports it "runs very well" with
gulikoza's patched DOSBox plus dgVoodoo. Needs `glide2x.ovl` extracted from
`3DFX\GRTVGR.EXE` (a PKZIP archive — `7z x` it, or
`pkunzip d:\3dfx\grtvgr.exe glide\drivers\voodoo\dos\glide2x.ovl`).
Locked to 640x480.

### 3. Windows build — hardest

Every finding in §4 works against you. Needs DxWnd with CD-directory emulation
and emulated vsync, plus both discs merged into one folder. The DxWnd thread
author called it "a total mess." `GDIDREAM.EXE` windowed is the least-bad native
variant. The 32-bit exe may need the **LAA flag** on modern machines.

### Known behaviours (PCGamingWiki)

- Resolutions 320x200 to 800x600; 3dfx locked to 640x480; GDI mode is windowed
- **Above 30 FPS breaks physics** — a simple landing can cost most of your health or kill you
- Autosaves on entering each new section, into a separate slot. No manual saving.
- Game options reset on every run
- Hold **F10** in game to view controls

### System requirements (original)

Minimum: Windows NT 4.0 or DOS 6.22, Pentium, 16 MB RAM, 500 MB disk, SVGA/VESA/Glide.
Recommended: Windows 95, 32 MB RAM, 1 GB disk, Direct3D/Glide GPU.

---

## 9. Research status

- **No source code or engine leak exists** for Cryo's real-time 3D engine. **[sourced]**
- The `360: Three Sixty` source found on a retail PlayStation disc is a **false
  lead** — Smart Dog developed that game and Cryo only published it. Different codebase.
- **ScummVM's engines will not help** — `cryo` (Lost Eden) and `cryomni3d`
  (Versailles, Necronomicon, The Cameron Files) cover the point-and-click titles
  only. **Its video layer does help**, though: it decodes this game's HNM format.
- **Cryo's engine code may physically survive.** Stéphane Petit, co-founder of
  Kheops Studio, said in a 2023 interview that he and Benoît Hozjan — both
  credited on this game's R&D — took Cryo's engine and tools with them when Kheops
  was founded in 2003. **[sourced]**
- **Do not fetch the TCRF page programmatically.** It returns deliberate anti-AI
  prompt-injection text instead of documentation — reproduced three times, and an
  acknowledged TCRF practice. **Open manually in a browser**:
  <https://tcrf.net/Dreams_to_Reality_(DOS,_Windows)>

### Open questions

1. **Unpack the `.DSN` body** — 157 MB of level geometry and textures. The body
   offset is now exact (`24 + 31·countB`, verified 98/98), so there is a precise
   starting byte for the first time.
2. **Decode the `.3DC` geometry payload.** The descriptor pairs are mapped; vertex
   and index semantics are not, and the `CUBE.ASC` shortcut failed.
3. **Extract HNM audio.** Video decodes; the `SD` chunks do not yet.
4. Does a merged install with `FULL.ID` present actually suppress disc swapping?
5. Why is disc 2's `HD.ID` binary (`01 00 00 00`) when disc 1's is text (`toto`)?

Resolved since the first pass: the HNM6 decoder problem, `CM6_*x16.dll`'s
location, what `.UBB` files are, `DREAMS.DAT`'s structure, the `HNS6`/`HNM6`
distinction, and the complete level map. See
[docs/research-log.md](docs/research-log.md).

### References

- [PCGamingWiki](https://www.pcgamingwiki.com/wiki/Dreams_to_Reality)
- [DxWnd thread — running the Windows build](https://sourceforge.net/p/dxwnd/discussion/general/thread/a2ddabcd22/)
- [VOGONS — how to successfully run it](http://www.vogons.org/viewtopic.php?t=23325)
- [VOGONS — install problems](http://www.vogons.org/viewtopic.php?t=29775)
- [DOSBox Staging issue #2888 — Daum fork dependency](https://github.com/dosbox-staging/dosbox-staging/issues/2888)
- [MultimediaWiki HNM6](https://wiki.multimedia.cx/index.php/HNM6)
- [Internet Archive copy](https://archive.org/details/msdos_DREAMS_to_Reality_1997)
- [MobyGames](https://www.mobygames.com/game/6882/dreams-to-reality/)
