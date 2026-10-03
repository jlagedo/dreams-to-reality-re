# The Guardian of Darkness: first look

*Le Gardien des Ténèbres*, Cryo Interactive, 1999 (readme: version 110, 17
February 1999; `DATA.TAG`: Release Candidate 13.6). Studied because its 3D
library and data formats come from the Dreams to Reality engine.

Work files: tools in `re/guardian/tools/`, Ghidra project `ghidra/guardian`
(`game.d3d.exe`, `game.dx.exe`), output in `out/guardian/` (binaries copied to
an ASCII path, strings, Watcom signatures, extracted archives). The source
install was `C:\Users\João Amaro\Downloads\guardian_202305\Guardian`.

## Executables and toolchains

| File | Role | Toolchain |
|---|---|---|
| `game.d3d.exe` | game, Direct3D (DirectX 5 HAL) | Watcom C/C++ 11.0 |
| `game.dx.exe` | game, DirectDraw software renderer | Watcom C/C++ 11.0 |
| `guardian.exe` | front-end launcher | Borland C++Builder 3 (VCL, linker 2.25, `vcltest3.dll`) |
| `cryo.exe` | autorun HTML browser | MSVC 6.0 (Rich header build 8168) |
| `CM6_640x16.dll` | HNM6 decoder plug-in (`HNMPI_Init`, `HNMPI_DecodeFrame`, `HNMPI_Cleanup`) | MSVC 5 |
| `SETUP.EXE`, `*.cab` | installer | InstallShield 5 |

Same toolchain as `WINDREAM.EXE`:

- WLINK version field 2.18 and the same `AUTO .idata DGROUP .bss .reloc
  .rsrc` layout.
- Linked runtime is 11.0, not 11.0a. `re/matchdecomp/libversion.py` finds 178
  functions only in 11.0, 2 only in 10.6 (both ambiguous), 0 only in 11.0a.
- Unoptimised and stack-checked: 565 `push N; call __CHK` sites.
- Compile flags are not yet proven by a byte-exact compile.
- Build string: "Le Gardien des Tenebres © 1999, Cryo Interactive (built Feb 18
  1999 at 13:50:08)". `game.dx.exe` was built about two minutes later.

## Shared code with Dreams

`match_identical.py WINDREAM.EXE game.d3d.exe` (features from
`ExportFunctionFeatures.java`, Watcom names from `ApplyWatcomSigs.java`) gives
309 byte-identical functions after masking relocations and call offsets
(`out/ghidra/match/WINDREAM.EXE--game.d3d.exe.identical.tsv`).

- Most are Watcom runtime.
- About 50 are the BEN11 3D library that the July 1997 demo symbols place in
  `C:\SOURCES\BEN11\3DC_MEM.C` and `3DC_SL.C`:
  - names: `MDL_RelocNode`, `MDL_RelocCollision`, `Kill_Obj_`, `UnLink_Obj_`,
    `Get_World_Matrix_`, `REND_SetViewportFov`, `REND_UpdateFrustum`,
    `Init_Mip_Map_Level_`, ...;
  - Dreams `0x45525c-0x4583dc` maps to Guardian `0x42b240-0x42e560`, at a
    nearly constant offset (`-0x29fb8` drifting to `-0x29e7c`), so the module
    was linked as one block with small internal changes.
- Other library functions match only at the start (`RES_Duplicate` 85%,
  `REND_SetViewportFocal` 53%, `RES_Relocate` 25%); these are where the
  library changed.
- None of Dreams' game code (AI, ENT, CAM, MENU, SCENE, DRD) matches. Guardian
  has its own game layer:
  - "Poltergeist I.A. Manager 2.05 RC7" (`EM_*`);
  - "Poltergeist Camera Manager 1.01 RC2" (`CM_*`);
  - the ESCOM 2.02.0 strategy-script language;
  - sources `plt_3d.c`, `plt_fx.c`, `plt_load.c`.
- The platform layer is Realtech's RLX, statically linked and Watcom-compiled
  like the rest; Dreams has no trace of it.
  - Its static defaults (`0x4c5d6e`) are still in the data: window title
    "Default", window class "RLX3", company "Realtech", resource file
    `.\resource.vmx`, `rlxreg.ini`, install path `c:\rt\`.
  - Guardian's startup `0x409070` overwrites them with `c:\cryo\`, the game
    name and `app/x-vnd.Cryo.Guardian`. The BeOS signature therefore fills
    RLX's portable "application name" field, which becomes the Win32 window
    class (`0x401d15`).
  - Visible RLX parts: the window/`WinMain` stub (`STUB_ICON`), the sound
    layer ("RLX/SND Layer (DirectSound 5)", `0x4011e8`), the resource-file
    system ("Adding resource file %s...", `0x40143f`), the video memory
    allocator (`gx_alloc.c`, `0x42fb30`), and CPU and registry configuration.
  - Not established: whether the `.res` format is RLX's, whether the
    DirectDraw/Direct3D code (French messages) is RLX's or Cryo's, and how
    much of the image RLX accounts for.
- Libraries: IJG libjpeg 6b (27-Mar-1998), zlib.
- `app/x-vnd.Cryo.Guardian` is a BeOS application signature.

## Renderers

Both executables link the whole DirectX 6 `dxguid` GUID table, so
`guids.py` alone proves nothing; the code references show what is used
(`xref_imm.py` on the GUID addresses):

- **`game.d3d.exe`** uses the DirectX 6 interfaces:
  - `IDirect3D3` (`0x431ba4`), `IDirectDraw4`, `IDirect3DTexture2` (texture
    upload `0x433388`);
  - device by index in `0x431c38`: 0 HAL, 1 RGB, 2 MMX, 3 reference. Probably
    `config.ini [Video] Device`, unverified;
  - z-buffer, bilinear filtering and dithering options;
  - texture kinds "8bits - palette", "argb1555" and "Alpha";
  - needs 640x480 in 16-bit colour. Its "requires DirectX 5" message is
    stale.
- **`game.dx.exe`** has DirectDraw only and draws through the BEN11 software
  rasteriser, the family of Dreams' Windows renderer.

### Where Direct3D joins the engine

`match_functions.py` and `find_cut.py game.dx.exe game.d3d.exe --third
WINDREAM.EXE` (`out/ghidra/cut/game.dx.exe--game.d3d.exe.*`): 1,869 of
1,950/1,997 functions matched; 24 swapped slots, 7 D3D-only, 3 DX-only.

- **Pipeline.** Dreams' 3D pipeline is in both builds, from `REND_DrawFrameEx`
  through `REND_DrawScene`, `REND_DrawObject`, cull, transform, near clip,
  project and light, to the `SW_DrawObjectFaces` hook.
  - `game.dx.exe` twins mostly score 1.00 against `WINDREAM`.
  - `game.d3d.exe` twins score 0.4-0.7, so the D3D build changed them.
- **Junction.** `REND_DrawFrameEx` (DX `0x42cdac`, D3D `0x42f57c`) is the
  junction, the same one as Dreams' Glide build (backend-exploration.md,
  slot 6).
  - Both builds keep the software hooks for the shadow branch (flag
    `0x4c963c`).
  - The D3D main branch clears the second hook and sets the face hook to
    `0x40d564`.
  - It then runs `REND_DrawScene` (`0x4778a7`), the post-scene `0x478037`,
    and a deferred pass `0x40e0cc`.
  - That is the order of Dreams' Glide `REND_DrawFrameEx` `0x738d0`: hook =
    `GLIDE_DrawObjectFaces`, `REND_DrawScene`, then
    `GLIDE_DrawTranslucentFaces`.
- **The face hook `0x40d564`.**
  - It walks the node's block list at `+0xa4`, with the signed block type at
    `+0x04` and the next block at `+0x00`: the layout `GLIDE_DrawObjectFaces`
    walks.
  - It branches on wider type ranges (`>= -4`, `-9..-5`, `-13..-10`, `-21`).
  - Type -21 blocks are queued (up to 0x1000, with node `+0xd0`) for the
    deferred pass, where the Glide hook queues -7/-4/-3.
  - Vertices get `rhw = 1/z` and go out in batches through a COM call at
    vtable `+0x70`, with arguments (4, `0x1c4`, `0x5b4bc0`, count, 0).
  - Calls at `+0x24` and `+0x28` bracket the hook.
  - Read as `IDirect3DDevice3` (the build queries `IID_IDirect3D3`), these
    are `BeginScene`, `EndScene` and `DrawPrimitive(D3DPT_TRIANGLELIST,
    D3DFVF_TLVERTEX, ...)`: pre-transformed, screen-space vertices like
    Glide's. The device type is inferred from the vtable offsets.
- **Link slots.** The 11.8 KB D3D-only block (slot 1103, `0x46b76c`) fills
  the link slot after the `SW_DrawObjectFacesPost` twin. In Dreams that slot
  holds the software scanline code (`Init_ScanLine2_`, `Insere_SSL_`). The
  DX-only side drops `SW_FlushSpansScaled`.
  - D3D-only slot 152 (30 functions, about 6 KB, from `0x40c51b`) holds the
    face hook and the texture loaders.
  - D3D-only slot 616 (28 functions, about 6 KB, from `0x431481`) holds
    device and texture management: `IDirect3D3` at `0x431ba4`, devices at
    `0x431c38`, `IDirect3DTexture2` upload at `0x433388`.
- **Physics and maths.** The PHYS and MATH slots differ too: the D3D twins are
  larger (`MATH_AddVec3` group 99 to 270 bytes). Not yet read.

Dreams, by comparison:

- `WINDREAM` (DirectDraw) and `GDIDREAM` (GDI) are software only;
- `WINDREAM` references only `IDirectDraw`/`IDirectDraw2` and surfaces up to 3;
- its only hardware path is the DOS 3dfx build (Glide).

The larger `.3dm` files are 65,536 or 196,608 bytes longer than a Dreams
bank, which fits an extra 256x256 alpha map or 16-bit page. That fit is
unverified.

## Data

Four archives, format in `re/guardian/tools/res.py`:

- a 16-byte header (negated header+table size, entry count);
- 40-byte entries (32-byte path, size, offset from the end of the table).

| Archive | Entries | Contents |
|---|---|---|
| `game.res` | 6,019 | `.3dm` 2,264 (225 MB), `.3da` 1,531, `.3dc` 1,271 (78 MB), `.3di` 224, `.png`, `.eff`, `.cgs`, `.cam`, `.mor`, ... |
| `audio.res` | 1,049 | `.wav` 993, `.snp` 56 |
| `music.res` | 20 | `.snp` streams (`CRYO_APC` compressed audio) |
| `voice.res` | 988 | `.snp` 842, subtitle `.txt` 141, two fonts |

Dreams' readers decode Guardian's data unchanged (`probe_3d.py`):

- every `.3dc/.3dm/.3da/.3di` file starts with `F3DC`;
- `dreams.formats.node.read_3dc` finds the Dreams scene-graph node in 1,258 of
  1,271 `.3dc` files (8,071 nodes, 227,538 faces);
- 2,247 of 2,264 `.3dm` files are exactly the Dreams texture-bank size
  (98,324 bytes after the 8-byte prefix); 17 are larger (163,860, 294,932, ...)
  and not yet examined.

Video is HNM6 (`video/eguardian.hns`, "Pascal URRO R&D" header, `CRYO_APC1.20`
audio).

## Is it a debug build?

No.

- It is a release candidate without debug information: no overlay, no debug
  directory, no symbol table.
- It is compiled the same unoptimised, stack-checked way as Dreams' retail
  build.

Development facilities are still in it:

- **Fatal-error log.** `0x40b674` formats the message, appends it to
  `debug.txt` (`0x40b5ec`: `fopen`, `fprintf`, `OutputDebugStringA`) and
  `longjmp`s out. It has 30 callers.
- **AI and camera traces, switched off.**
  - Every `EM_*` and `CM_*` function logs "X()" / "End Of X()" through
    `0x41ca38` / `0x428b5c` into the same log.
  - The traces are gated by `0x4c7488` / `0x4c74d4`.
  - Startup (`0x408ddc`) calls `EM_SetDebug(0)` (`0x41c970`) and
    `CM_SetDebug(0)` (`0x428ac0`) with a literal `xor eax, eax`.
  - Turning them on needs a patch (bit 2 = trace, bit 1 = second flag).
- **Texture loader messages** to `OutputDebugStringA`: "charge une map
  8bits - palette", "argb1555", "Alpha".
- **Developer mode from `config.ini`.**
  - `0x40882c` reads `[Game] Option`; the value 4 enables the mode, and only
    then is `[Game] Mission` read.
  - The shipped `config.ini` still has `Mission=21` but no `Option`.
- **Cheat keys in developer mode** (`0x4293b8`).
  - They need left Ctrl held (`DIK 0x1d`, keyboard state at `+0x20` of the
    input object `0x4ccac4`).
  - They are edge-triggered DirectInput codes. The mnemonic letters confirm
    the mapping:

| Key | Effect | Message |
|---|---|---|
| F | toggles bit 4 of `0x6bbc28` | "Turbo Fire" |
| V | toggles bit 1 of `0x6bbc28` | "Vision" |
| D | `0x45a260` | "Die!" |
| G | | "G.O.D. mode" |
| J | | "Mana Mania" |
| K | `0x45afe4` | "Kill'em" |
| L, F4 | `0x429324(0x14)`, `(0x11)` | game-state switch |
| F9 | `0x44c454` | ends the level (advances the level counter) |
| N | sets `0x6bfe22 = 5` | |

- **On-screen debug overlays** (`0x429c98`, developer mode). The overlay bits
  are in `0x4c74dc`, drawn from `0x41b1f0`:
  - `D` toggles bit 2, the state dump `0x44e498`: resource counts ("Audio:%d
    Data:%d Music%d Voice:%d"), "maxCase %d MetaCouche:%d", message traffic,
    the current room ("Salle :%s(%d) (3DC:%s 3DI:%s) %d doors"), tendrils and
    characters (immortel/mortel, agressif/neutre, visible/invisible);
  - `M` toggles bit 4, a memory-block list (`0x411fe8`, "0x%p Sz %d Type
    0x%d ...").
  - The arrow keys shift two offsets (`0x6bbc89`, `0x6bbc8d`) in steps of 16.

Not yet checked:

- what gates `0x429c98` besides the mode;
- whether these keys work in the shipped build;
- `profile.lst` (`0x40bf98`, multi-file handle cache).

None of this has been run; it is read from the code.
