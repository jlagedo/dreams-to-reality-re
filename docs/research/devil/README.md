# The Devil Inside: first look

Gamesquad, published by Cryo Interactive, 2000. Studied as a later Cryo-label
3D game next to Dreams to Reality (1997) and The Guardian of Darkness (1999,
`docs/research/guardian/`).

## Sources

Both downloads were moved to `E:\dev_game\the-devil-inside\`:

| Directory | What it is |
|---|---|
| `ccd/` | `thedevilinside.zip`: CloneCD image. Data track 1 is MODE2/2352, followed by 16 audio tracks. `extracted/` is the data track |
| `playgames/` | `the-devil-inside-playgames.zip`: the Portuguese Playgames magazine release, a CloneCD image plus `noCD-Patch/Devil.exe`. `disc/` is its data track |
| `installed/` | `data1.cab` unpacked by its InstallShield file groups, plus `Devil.nocd.exe` copied from the magazine patch |

How the two releases compare:

- **Disc images.** They differ only as rips (the cue sheets' audio
  pregaps). The 31 files of the two data tracks are byte-identical.
- **The magazine's `Devil.exe`** is a later build, not just a crack.
  - Its link time is 2000-04-11; retail's is 2000-03-12.
  - `.text` is 0x15c0 bytes longer.
  - It corrects credit names ("MIMRAN", "VINMER", "SWIATEK"), adds testers
    and adds three intro videos: `CTO_.hnm`, `FRNDW.hnm`, `NIVAL.hnm`. These
    look like international publisher logos (unverified).
- **Tools used:**
  - the data track was converted with a MODE2 form-1 sector copy, then
    unpacked with 7-Zip;
  - `data1.cab` (InstallShield 6) was unpacked with `unshield` 1.6.2, built
    in WSL under `out/tools/unshield`.

Work files: binaries in `out/devil/bin/`, strings, RTTI and Direct3D call lists
in `out/devil/`, Ghidra project `ghidra/devil`, features
`out/ghidra/features/Devil.exe.json` (11,500 functions).

## Toolchain

| File | Toolchain |
|---|---|
| `Devil.exe` (3.2 MB, 2000-03-12) | **MSVC 6.0**, mostly C++. Rich header: C++ compiler builds 8447 (191 objects) and 8168 (48); C 8168 (187); 99 MASM 6.13 objects; linker 6.0 build 8447. MFC linked statically (MFC classes in RTTI, `WINSPOOL`, `GetFileTitleA`) |
| `QMixer.dll` | QSound QMixer 3D audio, MSVC 6 |
| `CM6_640x16.dll` | Cryo HNM6 decoder plug-in, **byte-identical** to Guardian's |
| `Devil Setup.exe` | MSVC 6 MFC launcher |

There is no Watcom code and no Dreams code. The masked search for
`WINDREAM.EXE` bodies finds only the 27 Intel Pentium FDIV workaround routines
that both runtimes carry; C code cannot match across compilers anyway.

## Engine

- **Theo.** The engine is Gamesquad's "Theo".
  - Credits: "Theo The Game Maker"; Christophe "NZTheo" Nazaret, adaptation
    and code; Laurent Paret, director of development; Laurent Salmeron,
    technical director; Hubert Chardot, written and directed by.
  - Data files carry "Theo Loader - version 1.0" (`.db` models and
    animations) and "Theo Trajectoire File" (`.trj` paths). Level configs
    start with `DICODEV2.4i`.
- **Cryo's parts.** C++ classes in a `Cryogen` namespace (`CException`,
  `CDirectXException`, `CDirectDrawException`, `CDirectInputException`;
  `CDirect3D::Initialize`, `CDirect3DViewport`). Cryo libraries:
  - HNM6 video through `CM6_640x16.dll` (`HNMPI_*`);
  - `CRYO_APC` audio;
  - "Cryo Text Library Version 2.01.00" (`CRYOTD.H`).

  The credits name Pascal Urro (HNM) and Benoît Hozjan in the Cryo section.
- **Data.** `Datas/*.gtz` archives, format in `re/devil/tools/gtz.py`:
  - a recursive tree of directories (file count, subdirectory count,
    length-prefixed name);
  - zlib-compressed files dated by Unix time;
  - `.str` files repeat the tree;
  - every archive parses to its last byte.

  Contents: `.tga` textures, `.db` Theo models and animations, `.dio`,
  `.lst`, `.cfg`, `.gen` (text game tables), `.trj`, `.wav`, `.dit` text.
  Nothing uses Dreams' `F3DC` formats.

## Renderer

The interfaces actually requested, from code references to the SDK GUIDs
(`re/guardian/tools/guids.py`):

- `IDirect3D3`, `IDirectDraw4` (plus `IDirectDraw7` once), `IDirect3DTexture2`
  and `IDirectDrawGammaControl`;
- DirectSound with 3D listener and `IKsPropertySet` (EAX), and
  `IDirectInputDevice2A`.

The same DirectX 6 device generation as Guardian.

Read from `re/guardian/tools/d3dcalls.py` over all functions
(`out/devil/d3dcalls-all.txt`):

- **Vertices.** `DrawPrimitive` with FVF `0x2c4`: pre-transformed XYZRHW,
  diffuse, specular and **two** texture coordinate sets. Flags 0x18. Also
  `DrawPrimitiveVB` and `DrawPrimitiveStrided`.
- **Multitexturing.** Stage 1 is set up with `TEXCOORDINDEX 1` and bilinear
  filtering. A state-cache wrapper passes most stage and blend states as
  variables.
- **Extra passes.** `ZFUNC EQUAL` at five sites (`0x49a320`, `0x49a4f0`,
  `0x49a980`, `0x49b060`, `0x50bb90`): extra passes over already-drawn
  geometry. The config's `ShadowMap`, `Mirrors` and `Lighting` options suggest
  shadow maps and mirrors; the pass purposes are unverified.
- **Alpha test** at `ALPHAREF 0x7f`, `GREATER`.
- **Fog** enabled with a fog colour (`0x490cd0`).
- **W-buffer.** `ZENABLE 2`, which is `D3DZB_USEW` (`0x499c50`).
- `CULLMODE` none.

It is a different, more advanced renderer than Guardian's, which has single
texturing, no fog and no extra passes.

## Not done

- No runtime test.
- The `.db`, `.dio` and `.cfg` formats are not decoded.
- The renderer's passes are not attributed to features.
- The 2000-04-11 build is imported (`ghidra/devil`, 11,505 functions) but not
  yet diffed function by function against retail.
