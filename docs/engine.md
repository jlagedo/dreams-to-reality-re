# Engine

> **Function names verified (2026-09-26).** Every `WINDREAM.EXE` function this page names is in [`re/names/WINDREAM.EXE.tsv`](../re/names/WINDREAM.EXE.tsv) with two independent sources (these docs and a blind review of the decompilation) and facts checked against the binary by `tools/check_names.py`.

## Summary

*Dreams to Reality* runs on Cryo Interactive's own in-house C/C++ engine. It has
no public or marketed name and is **not** the "Omni3D" engine Cryo branded on
their point-and-click adventures (Atlantis, Versailles). It is a real-time
behind-view 3D engine with a custom software rasterizer and an optional Glide
hardware path. **[verified]**

## Provenance marks

Strings recovered from the binaries: **[verified]**

| String | Found in | Meaning |
|---|---|---|
| `X:\CRYO\DREAMS\` | all builds | developer build root |
| `X:\CRYO\DREAMS\DATA\HD.ID` | `WINDREAM.EXE` | hardcoded install probe path |
| `C:\CRYO\DREAMS` | `SETUP.INI` | default install directory offered to the user |
| `CRYO 1997` | all builds | copyright |
| `Cryo Interactive © 1997` | `SETUP.INI` | installer copyright |
| `-Copyright CRYO-` | every HNM6 file | codec header field |
| `DAN_Load3DC` | `WINDREAM.EXE` | internal loader symbol |
| `Debordement dans DAN_Load3DC` | `WINDREAM.EXE` | untranslated French error, "overflow in" |
| `Error, not enough memory in DAN_Load3DC` | `WINDREAM.EXE` | English sibling of the above |
| `Dreams_MSS1` | `DREAMS.EXE` | Miles Sound System handle name |
| `Water surface` | `WINDREAM.EXE` | renderer feature |

The mixed French/English error strings and the leftover 3D Studio files in
`DATA\OBJET\` (`CUBE.ASC`, `3DS.BAK`) indicate the shipped discs were built
straight from the developer tree with no asset cleanup.

## Toolchain

**All four game executables were built with Watcom C/C++ 10.6 — including the
Windows ones.** This is the single most informative finding about the codebase.
The version is pinned down in [toolchain.md](toolchain.md), which also covers the
reference libraries and the runtime symbol names recovered from them.
**[verified]**

Evidence:

- `WINDREAM.EXE` contains the literal string `WATCOM C/C++32 Run-Time system. (c)
  Copyright by WATCOM International Corp. 1988-1995.`
- Its PE section names are `AUTO`, `.idata`, `DGROUP`, `.bss`, `.reloc`, `.rsrc`.
  `AUTO` and `DGROUP` are Watcom linker output. MSVC emits `.text` / `.data` /
  `.rdata`, which is what `SETUP.EXE` has — so the installer was built with a
  different toolchain from the game.
- `DREAMS.EXE` carries both the C/C++16 (1988-1994) and C/C++32 (1988-1995)
  runtime banners.

Watcom was the standard choice for DOS protected-mode games and also targeted
Win32. Cryo therefore maintained **one portable C core compiled three ways**,
with the platform-specific layer swapped underneath. This matters for any
reverse-engineering effort: the DOS and Windows builds should share most of
their logic, and the DOS builds have richer symbol residue.

## The four builds

| Build | Binary | Format | Video backend | Audio backend |
|---|---|---|---|---|
| DOS | `DREAMS.EXE` (1,176,454 B) | LE + DOS/4GW | SciTech UniVBE (VESA SVGA) | Miles Sound System |
| DOS 3dfx | `DREAMSFX.EXE` (1,025,154 B) | LE + DOS/4GW | Glide | Miles Sound System |
| Windows | `WINDREAM.EXE` (864,768 B) | Win32 PE, subsys 3.10 | DirectDraw | DirectSound + MCI |
| Windows GDI | `GDIDREAM.EXE` (864,768 B) | Win32 PE, subsys 3.10 | GDI (windowed) | DirectSound + MCI |

MD5: `WINDREAM.EXE` = `68BC8D526C57A2EBDC083E88325D4E64`,
`GDIDREAM.EXE` = `DA94096442CB66B4241341F20703C943`.

`WINDREAM.EXE` and `GDIDREAM.EXE` are **the same program**: identical byte size,
identical import table, different hash. Both link DirectDraw *and* the GDI path.
The two binaries differ only in which backend they default to. **[verified]**
Byte-for-byte they differ in 10 bytes, and only one is code: the immediate in
`MOV dword ptr [0x633b18], imm` at file offset `0x4543c`, inside `VID_Init`
(`0x446019`), is 0 (DirectDraw) in `WINDREAM.EXE` and 1 (GDI) in
`GDIDREAM.EXE`. The other nine are timestamps.

`DOS4GW.EXE` (265,396 B) is the Rational Systems DOS extender both LE builds
load. `GETKEY.EXE` (4,469 B) is a trivial keypress-to-errorlevel helper used by
the `.BAT` installers.

## Middleware

None of these is the engine — they are licensed components the engine calls into. **[verified]**

| Component | Vendor | Used by | Evidence |
|---|---|---|---|
| **Miles Sound System** | RAD Game Tools | DOS builds | `Copyright (C) 1991-97 RAD Game Tools, Inc.`, `Miles Sound System`, `Dreams Internal Error : can't create Dreams_MSS1`, `can't register Miles Timer` |
| **SciTech UniVBE / MGL** | SciTech Software | DOS build | `Copyright (C) 1993-97 SciTech Software Inc`, `data\univbe`, `Error: Univbe Driver cannot be started...` |
| **Glide** | 3dfx | DOS 3dfx build | `glide2x`, `_GRGLIDEINIT@0`, `_GRGLIDESHUTDOWN@0`, `_GRGLIDEGETVERSION@4`, `_GRGLIDESHAMELESSPLUG@4` |
| **DOS/4GW** | Rational Systems | both DOS builds | `DOS4GW.EXE`, LE binary format |

The Miles installation lives in `DATA\SOUND\` — `MSSDRVR.LST` is the stock Miles
driver-selection message file, and the `.DIG` files there are Miles **sound card
drivers**, not game audio (see `file-formats.md`).

## Renderer

The engine uses its **own software rasterizer**. Neither the DOS nor the Windows
path asks the OS for any 3D capability — both only acquire a linear framebuffer
and write pixels into it. **[verified]**

- Colour depth is **16-bit hi-color**. `.SPR` sprite data is packed RGB555
  (`FF 7F` = 0x7FFF = white), and every HNM6 video header reports `bpp = 16`.
- Supported resolutions are 320x200, 640x480 and 800x600 — all three appear as
  literal strings in `WINDREAM.EXE`. The 3dfx build is locked to 640x480. **[sourced]**
- **Glide is the only hardware-accelerated path in the entire product, and it
  exists only in the DOS 3dfx build.** There is no Direct3D anywhere.

### 3dfx path — Glide 2 in `DREAMSFX.EXE` **[verified]**

The LE import table is empty: nothing is statically linked. Glide comes from
3dfx's DOS import library, `glimport.asm` in the
[released Glide source](https://github.com/sezero/glide): 130 decorated names
(`_GRDRAWTRIANGLE@12` ... `_CONVERTANDDOWNLOADRLE@64`), 130 five-byte
`call __loadme` stubs at `0xb898c`, and `__dlltab` at `0xb8c18`. On the first
call `__loadme` (`0xac894`) finds `glide2x.ovl` (current directory, `PATH`,
`C:\WINDOWS\SYSTEM\`; string `LINEXE_LOADER`) and patches the stub. Miles is
linked in and loads its `.DIG`/`.MDI` driver from `DATA\SOUND\DIG.INI`.

`ghidra_scripts/ApplyGlideImports.java` names and types the stubs from the
Voodoo Graphics (`glide2x/sst1`) headers (see `re-setup.md`). The game calls 35
of the 130 functions, almost all from a thin backend at `0x670f0`–`0x68400`:

- **`GLIDE_Open`** (`0x670f0`): `grGlideInit`, `grSstQueryHardware`,
  `grSstSelect(0)`, `grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
  GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)` (double buffer plus one aux
  buffer for depth), then `grTexFilterMode(GR_TMU0, BILINEAR, BILINEAR)` and
  `grGammaCorrectionValue(0.8)`. The 640x480 lock is this call.
- **`GLIDE_DrawObjectFaces`** (`0x67568`) is the object hook (see *Renderer
  backends*). It walks the object's face list, fills three `GrVertex` per
  triangle and calls `grDrawTriangle`, or `guDrawTriangleWithClip` when
  clipping is needed. Its modes: `guColorCombineFunction(GR_COLORCOMBINE_DECAL_TEXTURE)`
  with `grChromakeyMode(ENABLE)` for key-colour transparency; texture ×
  iterated RGB (`grColorCombine(SCALE_OTHER, LOCAL, ITERATED, TEXTURE)`, i.e.
  Gouraud-lit textures); flat constant colour (`grColorCombine(LOCAL, ZERO,
  CONSTANT, CONSTANT)`); texture clamp toggled per face (`grTexClampMode`).
- `GLIDE_Clear`, `GLIDE_ClearToBackground` (`grBufferClear`), `GLIDE_Swap`
  (`grBufferSwap`) and `GLIDE_Close` (`grGlideShutdown`).

Also called elsewhere: `grTexDownloadMipMapLevel` and `grTexSource` (texture
upload), `grDepthBufferMode`/`grDepthBufferFunction`/`grDepthMask`,
`grAlphaBlendFunction`, `grFogMode`/`grFogTable`/`guFogGenerateExp`/
`grFogColorValue`, `grLfbLock`/`grLfbUnlock` (direct framebuffer writes) and
`grClipWindow`. Glide's constants are `#define`s, so the decompiler prints
them as numbers; the names above are from `glide.h`/`sst1vid.h`.

### Renderer backends — a link-time choice with one hook **[verified]**

There is no runtime switch between Glide, DirectDraw and software. Each
executable links one output path; the shared scene code reaches it through a
single **object hook**, a global function pointer called once per visible
object.

| | `DREAMSFX.EXE` | `WINDREAM.EXE` |
|---|---|---|
| per-object caller | `REND_DrawObject` `0x96178` | `REND_DrawObject` `0x47e498` |
| object hook / second hook | `0x105004` / `0x105008` | `0x4ac8cc` / `0x4ac8d0` |
| hook during a frame | `GLIDE_DrawObjectFaces`, second hook `NULL` | `SW_DrawObjectFaces` `0x473014`, `SW_DrawObjectFacesPost` `0x4731b8` |
| after the scene | direct `GLIDE_Swap` etc. | `(*0x4aa708)()`: `SW_FlushSpans` `0x4768cc` or `SW_FlushSpansScaled` `0x476dec`, chosen by `SW_SelectFlush` `0x4592e0` from a caller argument |
| frame functions | `REND_DrawFrame` `0x737e8`, `REND_DrawFrameEx` `0x738d0` | `REND_DrawFrame` `0x459320`, `REND_DrawFrameEx` `0x4593a4` |

- `REND_DrawObject` calls `(*hook)(object)` unless object flag `+0x0d & 0x10`
  is set. Every hook walks the object's face list at `+0xa4` and switches on
  the face type (`0x16`, `0x17`, `0x1c`, … and negative codes). Node `+0xc4`
  is the light count (`+0xc8` holds the light indices); when it is 0,
  `SW_DrawObjectFaces` rewrites face types `0x16`/`0x19` to `0x18`. An earlier
  revision called `+0xc4` and `+0xd0` vertex arrays; the vertex array is at
  `+0x80` (count `+0x7c`, `0x28` bytes each), and the `+0xd0` read in the
  rasterizers is off a register whose structure is not identified.
- **Software:** `SW_DrawObjectFaces` sends each face to a per-type rasterizer
  that fills per-scanline edge lists (`0x66e6b8`, `0x66f6b8`, `0x6706b8`);
  `SW_FlushSpans` walks them scanline by scanline into the framebuffer;
  `SW_FlushSpansScaled` flushes through a line buffer and repeats rows (the
  scaled output path). The second hook, `SW_DrawObjectFacesPost`, draws the
  object's second face list (`+0xa8`) as a post-order hook. What selects the
  scaled flush is not yet known.
- **Glide:** the frame function points the hook at `GLIDE_DrawObjectFaces`
  and clears the second hook; the Voodoo rasterizes. Clear, swap, open and
  close are direct calls, not dispatched. `DREAMSFX.EXE` still carries the
  software hooks: the hook's initialised value is `SW_DrawObjectFaces`
  (`0x81258`), overwritten every frame.
- **DirectDraw is not a peer of Glide.** It only supplies and presents the
  surface the software rasterizer writes; the GDI path (default in
  `GDIDREAM.EXE`, present in both Windows binaries) does the same with a DIB
  section. Neither Windows binary contains Glide code. DirectDraw versus GDI
  *is* a runtime switch, on flag `0x633b18`; see *Presentation and 2D* below.
- **Dead third branch, both builds.** The frame functions test a byte flag
  (`0x10501c` / `0x4ac8c8`); when set, the hook would be `SW_CollectFaceTriangles`
  (`0x96a20` / `0x478800`). Nothing writes either flag and both initialise to
  0, which is why the decompiler drops the branch as unreachable. The hook
  collects face triangles into the buffer at `0x6808e4`/`0x6808e8` instead of
  rasterizing them.
- The build pairs above were first made by hand; `tools/match_functions.py`
  (see `re-setup.md`) recovers five of the six independently, including
  `REND_DrawFrame`/`REND_DrawFrameEx` by call-slot alignment. `SW_CollectFaceTriangles`
  is only ever stored as a pointer, so the call graph cannot reach it.
  `REND_DrawFrameEx` takes an object handle and, in `WINDREAM.EXE`, resolves
  it through `0x455358` before the common frame path; the 3dfx build's extra
  call to `0x6f634` is not yet traced.

### What the game reads from the renderer **[verified]**

Traced 2026-09-26 (static; `out/scratch/sim-reads-projection.md` has the
full table of outputs and readers). The render stage in `WINDREAM.EXE`
writes, per node: composed camera-space rotation (`+0x58..+0x78`) and
translation (`+0x4c..+0x54`) in `REND_DrawObject` (`0x47e498`); cull bits in
`+0x0c` (`0x478980`); per-vertex camera-space xyz (`+0x10`), screen x/y
(`+0x1c`/`+0x20`), `K/z` (`+0x24`) and outcode flags (`+0x00`) in
`REND_TransformClipVertices` (`0x478c2c`), `REND_ProjectVertices`
(`0x47b228`, the main projection of `0x40`-flagged vertices) and
`REND_ProjectSharedVertices` (`0x478dac`, the parent's `0x80`-flagged shared
vertices used by bridging children); clipped polygons, lighting and
environment-map UVs.

Nothing outside the render path reads the vertex data, the cull bits, the
clipped polygons, the lighting, or the dead third branch's triangle buffers
(`0x6808e4`/`0x6808e8`). The per-node box records built after the scene walk
(`0x460de0`) are read only by box-collision routines (`0x4616ac`–`0x464188`)
that nothing calls.

**One render output is read by the game: node `+0x4c`, the object's position
composed down to the camera root, i.e. in camera space.** Readers:

| Reader | Called from | Use |
|---|---|---|
| `0x4477d9` | `DSOUND_PlaySound` (`0x446654`) via `0x447390` | distance to the camera sets the volume, camera-space x the pan |
| `0x4145c3` | `AI_TickCombat` | line of sight: converts back to world space with the current camera (`0x457c3c`), tests the segment against collision planes |
| `0x444b04` | `ENT_TickAttackObject` | same line-of-sight check |

Both line-of-sight checks run in `GAME_Tick` before this tick's render, so
they see the previous frame's value after `CAM_CompCameraPos` has moved the
camera; their reconstructed world position drifts while the camera moves.
Only the entity's root node is involved. The original renders from
`0x423f60` after the collision separation `0x40bff8`, so a reimplementation
must compute this value at `REND_DrawFrame`/`REND_DrawFrameEx` time.

`CAM_CompCameraPos` works in world space and uses no renderer output. Which
objects are hidden is a game decision (`+0x0c` bits 1 and 4), not the frustum
cull. The only other world-to-screen code reachable from the tick is an
ambient-tint sampler (`0x41c0a6`) whose pixel reader `0x4020a8` is a bare
`RET` in this build, and the debug collision wireframe (`0x45f2a0`). Open:
whether the portrait render (`0x43eb67`, a second camera at `0x62b9a4`) can
re-parent an entity; and whether the pan call in `0x4477d9` overwrites the
volume, since both use the `SetVolume` slot (`+0x3c`). **[unverified]**

### Presentation and 2D — Glide mapped onto DirectDraw/GDI **[verified]**

Every Glide call site in `DREAMSFX.EXE` sits in a small backend
(`0x66ac8`–`0x68400`). Pairing its callers with their `WINDREAM.EXE` twins and
aligning the two call sequences shows what the Windows build does in the same
slot:

| Role | `DREAMSFX.EXE` (Glide) | `WINDREAM.EXE` (DirectDraw/GDI) |
|---|---|---|
| init | `GLIDE_Open` `0x670f0`: 640x480, 60 Hz, 2 colour + 1 aux buffer, bilinear, gamma 0.8 | `VID_Init` `0x446019`: 640x480, mode flag `0x633b18`; `DDRAW_SetMode` `0x4455ad` or `GDI_CreateDIB` `0x445c84` |
| shutdown | `GLIDE_Close` `0x67474` | `VID_ReleaseSurfaces` `0x4453ec` |
| present | `GLIDE_Swap` `0x674c0` (`grBufferSwap`) | `VID_Swap` `0x4158e2`: toggle buffer index, `VID_Present` `0x44549b` → `DDRAW_Present` `0x445a57` (Lock back surface, copy the software frame by pitch, Unlock, `Flip`; `Restore` on `DDERR_SURFACELOST`) or `GDI_Present` `0x4458bb` (`StretchBlt`, `SRCCOPY`) |
| framebuffer access | `GLIDE_LockBackBuffer` `0x68028` (`grLfbLock` write-only, back buffer, 565), `GLIDE_UnlockBackBuffer` `0x6805c`, around the call | `VID_Lock` `0x445bf2` (`IDirectDrawSurface::Lock`, `DDLOCK_WAIT \| DDLOCK_WRITEONLY`), `VID_Unlock` `0x445c3f`, inside the drawing routine; both no-ops in GDI mode |
| background | `GLIDE_ClearToBackground` `0x67490` (`grBufferClear`) | `VID_RestoreBackground` `0x417fe7`: copy the saved background (w×h×2) into the framebuffer |
| 3D faces | `GLIDE_DrawObjectFaces` `0x67568` | `SW_DrawObjectFaces` `0x473014` + `SW_FlushSpans` `0x4768cc` |
| translucent faces | `GLIDE_DrawTranslucentFaces` `0x68128`, deferred after the scene: face types −7/−4/−3, decal texture, alpha 128, `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA` | inside the software rasterizer (no separate pass) |
| texture upload | `GLIDE_BindTexture` `0x672a8`, `GLIDE_TexUpload` `0x66ac8`, `GLIDE_TexAllocUpload` `0x66d58`, `GLIDE_TexUploadScene` `0x66f24` | none: the rasterizer reads textures from RAM |
| fog, depth, clip | `GLIDE_SetFog` `0x671d0`, `GLIDE_SetDepthMode` `0x67500`, `GLIDE_SetClipWindow` `0x67204` | no counterpart at these call sites |
| text | `TEXT_PrintCentered` `0x4e20c`, `TEXT_GetWidth` `0x4ded4`, `TEXT_DrawGlyph` `0x4de34`, `TEXT_Print` `0x4e688` | `TEXT_PrintCentered` `0x425b4c`, `TEXT_GetWidth` `0x425838`, `TEXT_DrawGlyph` `0x4257a0`, `TEXT_Print` `0x426073` |
| sprites, menu | `SPR_Draw` `0x595f4`, `MENU_Draw` `0x5bdb4` | `SPR_Draw` `0x4274b0`, `MENU_Draw` `0x435ea0` |

- 2D (text, sprites, menus, the CD-swap screen) is drawn by the CPU with the
  same text and sprite code in both builds. On Glide it writes the Voodoo's
  back buffer through `grLfbLock`.
- The Windows build always renders into its own RAM frame (`0x5e549c`) and
  copies it out at present time; DirectDraw is a blit target with `Flip`, not
  a drawing API. `VID_Lock` does not keep the pointer `Lock` returns (the
  `DDSURFACEDESC` is a local), so around 2D drawing it only synchronises with
  the surface.
- `DDRAW_Present` indexes the `IDirectDrawSurface` vtable: `+0x2c` `Flip`,
  `+0x64` `Lock`, `+0x6c` `Restore`, `+0x80` `Unlock`.

The same pairing turned up five real function names in error strings
(*unknown message type in X*): `MGM_SendMessage`, `MGM_DispatchMessages`,
`CTRL_Dispatcher`, `MENJ_Dispatcher`, `CAM_CompCameraPos`, identical in both
builds. Two different functions both report *in DAN_Load3DC* (`DAN_OpenArchive` (`0x40fff7`),
`DAN_Read3DC` (`0x41020f`)), so that name is left unassigned.

### Windows video API — DirectDraw only

Complete import table of `WINDREAM.EXE` / `GDIDREAM.EXE`: **[verified]**

```
DDRAW.dll     DirectDrawCreate
DSOUND.dll    DirectSoundCreate
GDI32.dll     CreateCompatibleDC, CreateDIBSection, DeleteDC, DeleteObject,
              GetStockObject, SelectObject, SetBkMode, StretchBlt
WINMM.dll     joyGetDevCapsA, joyGetNumDevs, joyGetPosEx,
              mciGetErrorStringA, mciSendCommandA, timeGetTime
USER32.dll    (22 functions)
KERNEL32.dll  (71 functions)
```

Only `DirectDrawCreate` is imported by name; everything past creation goes
through COM vtables, so the import table understates the usage. The error
strings reveal how deep it goes:

```
Can't start Direct Draw
Can't go to Direct Draw 2
Can't set DirectDraw mode
Can't create primary surface under DirectDraw
DIRECT DRAW ERROR
```

`Can't go to Direct Draw 2` means the engine `QueryInterface`s for
**IDirectDraw2** — the DirectX 2 interface — even though `README.TXT` instructs
the user to install DirectX 5. DirectDraw is used purely for 2D surface
management and mode setting; the software rasterizer does all the drawing.

With the COM interfaces typed (`re/structs/directx.h`, globals in
`re/structs/windream-globals.tsv`) the setup reads directly: **[verified]**

- `DDRAW_Init` (`0x445955`): `DirectDrawCreate(NULL, &g_DirectDraw)`,
  `QueryInterface` → `g_DirectDraw2`, `SetCooperativeLevel(g_hWnd,
  DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN)`, allocate the 0x96000-byte RAM frame
  `g_frameBuffer`, then `VID_SetMode(640, 480)`.
- `VID_SetMode` (`0x4454d1`) stores the size and dispatches to the DirectDraw
  or GDI (`0x44554b`) back end. `DDRAW_SetMode` (`0x4455ad`): pick the
  640x480 entry of a 10-slot display-mode table, `SetDisplayMode(640, 480,
  16, refresh, 0)`, `GetDisplayMode` to read the pixel format (565 →
  `0x49da18 = 0`, 555 → `1`), `CreateSurface` with `DDSCAPS_PRIMARYSURFACE |
  FLIP | COMPLEX` and one back buffer (`g_ddsPrimary`), `GetAttachedSurface` →
  `g_ddsBack`.
- `DSOUND_Init` (`0x4463e9`): `DirectSoundCreate`, `SetCooperativeLevel(
  DSSCL_PRIORITY)`; `DSOUND_CreatePrimary` sets the primary buffer to
  **22,050 Hz 16-bit stereo**; `DSOUND_CreateChannels` creates secondary
  buffers with `CTRLFREQUENCY|CTRLPAN|CTRLVOLUME`: up to five 11,025 Hz
  16-bit mono voices, one 22,050 Hz 16-bit stereo 2-second buffer and one
  11,025 Hz 8-bit mono buffer, each the first field of a 0x18-byte channel
  record.

The GDI path is the classic `CreateCompatibleDC` + `CreateDIBSection` +
`StretchBlt` windowed blit, which is why `GDIDREAM.EXE` runs in a window.

PE subsystem version is **3.10**, i.e. targeting Windows NT 3.1 / Win32s era
minimums.

### Consequences for modern systems

Three properties of this import table explain every symptom reported by the
community, and each has a different fix: **[verified]** observation,
**[sourced]** symptom reports.

1. **`mciSendCommandA` on the `cdaudio` device** is how music plays — straight
   off the redbook audio tracks. The `MCI Error` users see is the game's own
   string, raised when no CD audio device is present. Mounting a `.cue` (not a
   `.bin`, not an `.iso`) is what satisfies it.
2. **The animation/simulation delta uses a `timeGetTime`-based counter.**
   The executable also imports `QueryPerformanceCounter`; the earlier claim
   that `timeGetTime` was its only clock was incorrect. Its nominal animation
   scale is 30 frames/second, with timer quantization and delta clamps; see
   [animation-timing.md](animation-timing.md). Community reports describe
   harmful landing damage at high render rates. DOSBox `cycles` throttling or
   DxWnd emulated vsync are reported workarounds.
3. **Exclusive-fullscreen DirectDraw mode-setting** is what breaks on
   Windows 11, producing `Can't set DirectDraw mode` and `Can't create primary
   surface under DirectDraw`. Correction: an earlier revision blamed an 8-bit
   palettized mode. The code asks for **640x480 at 16 bpp** and accepts only
   565 or 555 RGB surfaces (`VID_SetMode`, `DDRAW_SetMode`,
   **[verified]**); there is no palette. Why exactly modern Windows refuses the
   mode or the flip chain is not established. **[unverified]**

### Input — no mouse anywhere, polled keyboard **[verified]**

There is **no mouse-look and no mouse input of any kind**:

- The WndProc (`0x44627b`, window class `"Dreams to Reality"` registered in
  `VID_CreateWindow` (`0x4460cf`)) handles exactly five messages: `WM_DESTROY`,
  `WM_SYSKEYDOWN`/`WM_SYSKEYUP` — only to test `wParam == VK_MENU` so holding
  Alt does not open the Windows system menu — and `WM_SYSCOMMAND` (blocks
  `SC_KEYMENU`). Everything else goes straight to `DefWindowProc`. No
  `WM_MOUSEMOVE`, no button messages, and not even `WM_KEYDOWN`.
- The keyboard is **polled**: `INPUT_PollKeyboard` (`0x440757`) calls `GetAsyncKeyState(0..255)`
  once per frame into a 256-byte state array at `0x6308d8` (bit 0 = held,
  bit 1 = press edge, bit 2 = release edge); `INPUT_PostEvents` (`0x42493b`) (called from the
  frame pump before the event queues) posts each new key press as event
  `0x33` (it also posts `0x3b` and `0x3d`). Posting goes through
  `MGM_PostMessage` (`0x43b31a`), which pushes a 12-byte `{u8 type, u32, u32}`
  message onto a ring queue (count/capacity at `+0x10`/`+0x14`) that
  `MGM_DispatchMessages` drains.
- The only analog devices are **joysticks**: two polled devices (4-axis and
  3-axis) through the `joyGetPosEx` imports, posted as events `0x39`/`0x3a`
  on change.
- The Windows cursor is hidden immediately after window creation
  (`ShowCursor(0)` in `VID_CreateWindow` (`0x4460cf`)).

#### Joystick path **[verified]**

Only the Windows builds read a joystick; the DOS builds have no joystick code
(below).

1. **Startup.** `INPUT_Init` (`0x43a169`) calls `JOY_InitPovDevices` (`0x44091d`) and `JOY_InitDevices` (`0x440bc2`), creates a
   message queue and returns success or failure. Each joystick init counts devices
   with `joyGetNumDevs`, reads `wNumButtons` (and `JOYCAPS_HASPOV` in
   `JOY_InitPovDevices`) via `joyGetDevCapsA`, then **stores the current X/Y as the
   centre**. There is no calibration screen; the stick must be at rest when
   the game starts.
2. **`J` / `K`.** The key handler `GAME_HandleHotkeys` (`0x415aa7`) maps VK `0x4a` (`J`) to dispatcher
   command 10, input mode 3 (`INPUT_SetDeviceMode` (`0x40ec46`)) and the on-screen string `Joystick`;
   VK `0x4b` (`K`) to command 4, mode 0 and `Keyboard`.
3. **Enable flags.** In the dispatcher `MGM_SendMessage` (`0x43a306`), commands 7/8 set/clear
   `0x5e54bb` (via `JOY_EnablePov` (`0x424c5f`)/`JOY_DisablePov` (`0x424c87`)) and commands 10/11 set/clear
   `0x5e54ba` (`JOY_Enable` (`0x424caf`)/`JOY_Disable` (`0x424cd7`)). Both enables are gated on `0x626f70`,
   set during startup detection.
4. **Per frame.** `INPUT_PostEvents` (`0x42493b`) polls only enabled devices: `JOY_PollPov` (`0x440ad3`) (X/Y minus
   centre, POV, buttons) posts event `0x39`; `JOY_Poll` (`0x440d3d`) (X/Y minus centre,
   buttons, no POV) posts event `0x3a`. Both only on change.

So `J` drives the `0x3a` path: X/Y plus buttons, no hat. No call site with a
literal command 7 was found, so what enables the `0x39`/POV path is open, as
is the consumer that turns event `0x3a` into player movement.

#### DOS builds: no joystick code **[verified]**

`DREAMSFX.EXE`, loaded with the LE loader (`re-setup.md`), was scanned for
every `IN`/`OUT` and `INT`. Nothing reads game port `0x201` and nothing calls
BIOS `INT 15h AH=84h`, the only two ways a DOS program reads an analog
joystick. The `0x201` constants that exist are DPMI `INT 31h` function
`0x0201` (set real-mode interrupt vector). A raw byte-level scan of
`DREAMS.EXE` found the same. The `J` key, `Joystick` string and `TOUCHES.SPR`
joypad caps are shared UI, so the DOS builds show a joypad mode they cannot
drive.

What the DOS executable does touch: PIT `0x40/0x43` and `INT 21h` vector 08
(timer), port `0x60`, `INT 16h` and vector 09 (keyboard), PIC `0x20`, VGA
`0x3da` and SVGA chipset probes, `INT 2Fh` `1684/1686` (Windows/DPMI
detection) and real-mode **`INT 66h`, the Miles Sound System (AIL 3) driver
call**: `AIL_API_call_driver` (`0x9b323`) issues it through DPMI `INT 31h`
function `0300h`. Its only caller, `AIL_call_driver` (`0x8f3c0`), prints the
`AIL_call_driver(...)` trace string. The 54 Miles wrappers in `DREAMSFX.EXE`
are named from their own trace strings (`re/names/DREAMSFX.EXE.tsv`), and
`DATA\SOUND\MSSDRVR.LST` is headed
"Miles Sound System from RAD Software". The `.DIG`/`.MDI` files there are Miles
drivers (`AIL3DIG`, `AIL3MDI`), not DIGPAK.

Controls are therefore arrows + Ctrl/Alt/Space + number keys, or a joypad on
the Windows builds only —
exactly as `README.TXT` §6 documents (also in-game F10 help). Camera views
are `Alt+5..0`, not mouse-driven. See [boot-sequence.md](boot-sequence.md)
for the frame pump that drives all of this.

## Camera and projection **[verified]**

Traced 2026-09-26 in `WINDREAM.EXE` (names in `re/names/WINDREAM.EXE.tsv`,
all with blind review). Enough to rebuild the original camera.

### Projection

- **FOV is a constant, 76.36° horizontal** (float `0x4298b852`), passed by
  every caller of `REND_SetViewportFov` (`0x456b94`): `SCENE_InitLevel` (`0x41f42e`), `VID_SetResolution` (`0x41592c`),
  the icon renderer `0x40fd54`. The level record's `+0xa4` is **not** the
  FOV (see *Corrections* in [research-log.md](research-log.md)).
- Focal length in pixels: `K = 0.5 + (w/2) / tan(fov/2)` (the code uses
  π = 3.14159 and `fov·π/360`). At 640 wide K = 407.44; vertical focal is
  `K · aspect`, aspect = `h·4 / (w·3)` (`REND_SetScreenSize` (`0x4569cc`)), so 1.0 on 4:3 and the
  vertical FOV is 61.0°.
- Screen centre `(x + w/2, y + h/2)`. Projection is `sx = cx + K·x/z`,
  `sy = cy + K·aspect·y/z` with `K/z` kept per vertex (`MDL_Vertex +0x24`).
- **Letterbox**: when `0x49d9f8` is set the 3D viewport is `h·3/4` tall at
  `y = h/8`, same FOV.
- **Near plane 140** (`REND_SetNearPlane` (`0x456cc4`)`(0x8c)`, twice, at level start and on
  resolution change; the viewport setters first reset it to 128).
  **Far plane `0xfffff`** unless the level record's `+0xcc` is non-zero
  (`REND_SetFarPlane` (`0x456ccc`)); the setters' own default is 65,000.
  `REND_UpdateFrustum` (`0x456cd4`) derives the four side planes from K and the viewport.
- The portrait render (`0x43eb67`) uses `REND_SetViewportFocal` (`0x456a58`) instead: an explicit
  focal length scaled by `w/640`.

### The camera is node 0, driven by messages

`CAM_CompCameraPos` (`0x4099d2`) runs **once per frame** from `GAME_Tick` (`0x4240ba`) (and once at level
start). It first drains the camera message ring at `0x52c850` (12-byte
entries: type byte, payload at `+4`), posted through `CAM_PostMessage` (`0x409998`) by
triggers, entity code and the level start. Otherwise it updates the current
mode (`0x52c874`). Every mode ends by writing **scene-graph node 0**:
position with `MDL_SetNodePosition` (`0x457aa0`), rotation with `MDL_SetNodeRotation` (`0x457a38`).

| Msg | Mode | Starts / updates | Payload | Behaviour |
|---|---|---|---|---|
| `0x2b` | 0 follow | `CAM_StartFollow` (`0x409d27`) / `CAM_TickFollow` (`0x409d6c`) | actor | chase camera (below); a snapped update on entry |
| `0x2c` | 2 fixed | `CAM_StartFixed` (`0x40af10`) / `CAM_TickFixed` (`0x40af8e`) | eye xyz, target xyz, duration | static shot |
| `0x2d` | 3 track | `CAM_StartTrack` (`0x40afc0`) / `CAM_TickTrack` (`0x40b045`) | eye xyz, entity, duration | fixed eye; target eases to the entity by ½ of the gap per frame |
| `0x2e` | 4 pair | `CAM_StartEntityPair` (`0x40b0ef`) / `CAM_TickEntityPair` (`0x40b156`) | entity A, entity B, duration | eye on A, target on B |
| `0x2f` | 5 ride | `CAM_StartRideLook` (`0x40b1b9`) / `CAM_TickRideLook` (`0x40b225`) | entity, target xyz, duration | eye carried by the entity, looking at a point |
| `0x30` | 6 overhead | `CAM_ToggleOverhead` (`0x40b274`) / `CAM_TickOverhead` (`0x40b2c7`) | actor | toggle; eye at actor + (10, −1500, 10) looking down, rolled by heading + ¼ turn |
| `0x31` | 7 free | `CAM_ToggleFree` (`0x40b386`) / `CAM_TickFree` (`0x40b429`) | actor | toggle; debug fly-camera from actor − 300 x, keys turn and move it, ×½ ×¼ ×2 ×4 modifiers |

Timed modes count a float `+0x30` down by the frame delta `0x5e5388`
(`CAM_TickTimer` (`0x40aea2`)) and post `0x2b` when it goes negative. Camera states live at
`0x52c61c` (follow), `0x52c6a4`, `0x52c6e8`, `0x52c72c`, `0x52c770`,
`0x52c7b4`, `0x52c7f8`: `+0` eye, `+0xc` target, `+0x1c` actor or entity,
`+0x28/+0x2c` follow distances, `+0x30` timer.

### Follow mode (mode 0)

Parameters come from **six view presets** (`CAM_LoadPreset` (`0x40b729`), table `0x49d1f8`,
32 bytes each), chosen with Alt + number keys in `GAME_HandleHotkeys` (`0x415aa7`)
("Camera", "Camera 1".."Camera 5"). Units are scene units; Y is down.

| Preset | lead min/max | eye dist min/max | eye height lo/hi | eye ease | fixed yaw |
|---|---|---|---|---|---|
| 0 (default) | 1024 / 1024 | 528 / 528 | −80 / −80 | 4 | no |
| 1 | 1024 / 1024 | 336 / 336 | −32 / −32 | 4 | no |
| 2 | 1024 / 1024 | 528 / 528 | −80 / −80 | 4 | no |
| 3 | 336 / 336 | 768 / 768 | −448 / −448 | 4 | no |
| 4 | 226 / 226 | 968 / 968 | −800 / −800 | 4 | no |
| 5 | 336 / 336 | 768 / 768 | −248 / −248 | 4 | yes |

Preset 0 takes non-zero per-level overrides from the level record:
`+0x120/+0x124` lead, `+0x128/+0x12c` eye distance, `+0x130/+0x134` eye height,
`+0x1d8` eye ease.

Each frame (`CAM_UpdateFollowPos` (`0x40a866`)):

1. **Distances.** eye distance `D = (min+max)/2`, lead `L = (min+max)/2`.
2. **Mode.** Actor mode `+0x34` = 1 (ground) uses `CAM_ComputeChasePos` (`0x409de2`); modes 2/3
   (swim/fly) use `CAM_ComputeOrbitPos` (`0x40a4a5`) with D and L + 100 and the eye 30 higher. Ground
   actors in flying state (`+0xac & 0x20`), action `0x2e`, or actions
   `0x19/0x29/0x2a` with `+0x278 & 1` also orbit, with +300 (+150 when flying)
   and the eye 50 (40) higher.
3. **Chase (ground).** Yaw = `(0x1000 − heading) · 2π/4096`, heading actor
   `+0x5c`; the eye sits at yaw + π (behind), the target ahead. Target =
   actor + L along the heading; eye = actor + D behind, at the preset height.
   Preset 5 forces yaw 0 (world-aligned).
4. **Easing, per frame.** target += (goal − target) / 2 (`0x49d1f4`);
   eye.x,z += gap / 4 and eye.y += gap / 8 (divisor `0x49d1f0` = preset
   ease; 2 or 1 when squeezed, below). Then the eye height is clamped to
   `[target.y + hi, target.y + lo]` unless look input is active.
5. **Dead zone.** A move whose squared length is under 20 (`0x49d1ec`) is
   dropped, for both eye and target.
6. **Look keys.** While held (`CAM_EnableLookInput` (`0x40b6a8`), per frame), two angles change by
   `3° · Δt`: a yaw offset clamped to ±45°, and a pitch clamped to
   [−88.5°, +45°] that also moves the eye vertically. Released, both reset to 0.
7. **Close range** (`CAM_ApplyCloseRange` (`0x409ba0`)). If the horizontal eye-to-actor
   distance `d` is below `D` for a ground actor: L scales by `d/D`, eye
   easing becomes 2 below `0.7·D` and 1 below `0.5·D`, `d` is floored at
   `0.2·D`, and eye.y = actor.y + lo − 1.2·(D − d) (80 higher when flying).
8. **Collision** (`CAM_CollideEye` (`0x40d5e4`), when `0x52c84c` is set by `CAM_EnableCollision` (`0x40b6d3`)): a
   sphere (radius `0x49d2dc`) swept from the previous eye; if the push-out is
   below √`0x49d2e4` the eye is moved out.
9. **Orbit (swim/fly).** As the chase but on a sphere using the actor's pitch
   `+0x6c`; pitch in (80°, 90°] snaps to 80°, (90°, 100°) to 100°, and the
   same around 270°, so the camera never looks straight up or down.

**Orientation** (`CAM_ApplyLookAt` (`0x40ab4b`)): direction = normalise(target − eye), then
`MATH_BuildOrientMatrix` (`0x45b2d0`)`(dir, 0xfff − roll)`. Roll is 0 except: swimming
(actor mode 2) adds a sway from two phases advancing 0.02 and 0.05 rad per
Δt; flying eases roll toward the actor's bank `+0x64` by ¼ per frame, at
`bank · rate / 0x118` (rate 256, halved when `+0xad & 0x20`).

**Timing.** Everything above runs once per rendered frame; the easing
fractions are per frame, not per physics tick, so the original camera's lag
depends on frame rate. Δt is `0x5e5388` (see *The fixed step*).

Open: the exact float expressions hidden behind `__CHP` in the orbit mode;
the free camera's key map; which triggers post `0x2c`-`0x2f` and with what
payloads (they come from the level scripts, `SCENE_TickTriggers` (`0x429061`) and entity code).

## Collision and physics **[verified]**

Traced 2026-09-26 in `WINDREAM.EXE`; all names below are in the registry
with blind review. Data layouts are in `re/structs/windream.h`
(`COLL_Triangle`, `COLL_Mesh`, `PHYS_Collider`, `PHYS_ForceField`); the level
mesh decodes with [`dreams.formats.collision`](../src/dreams/formats/collision.py).

### Frame order

`GAME_Tick` (`0x4240ba`): player input (`0x423767`) → `CAM_CompCameraPos` (`0x4099d2`) → AI squads →
`ENT_TickAll` (`0x407089`) (animation, then `PHYS_TickEntity` (`0x43d83e`) integrates each entity) → level exits
→ `0x423f60`: **`PHYS_ResolveCollisions` (`0x40bff8`)** → `REND_DrawFrame` (`0x459320`). So movement is
integrated first and collision resolved once per frame just before drawing;
the camera sees the previous frame's positions.

### The level collision mesh

`.DSN` tag 2 is loaded as `.3DI`, resource type 5, and added to a single
**collision world** (`0x66e01c`) with `PHYS_AddMesh` (`0x45f450`). It is a subset of the rendered
faces plus invisible proxies (skies, video surfaces and water are left out):

```
point count, points (i32 xyz), triangle count, triangles (96 bytes),
normal count, normals (i32 xyz, Q15, one per triangle)

triangle  +0x00 3 point pointers   +0x0c normal pointer   +0x10 plane d
          +0x14 3 inward edge-plane normals (Q15)          +0x38 3 edge constants
          +0x44 owning mesh        +0x48 bbox min          +0x54 bbox max
```

Over all 152,536 triangles: unit normals 99.5%, exact boxes 99.2%, plane
distance within 2 units 97.6%. **[verified]** against the disc data by
`tests/test_formats.py`.

**Broadphase.** `PHYS_InsertMeshTriangles` (`0x45cf2c`) puts each triangle's min and max into three
per-axis sorted endpoint arrays; `PHYS_SweepAxis` (`0x45d420`) is an incremental
sweep-and-prune that moves each collider's `centre ± radius` interval through
them and keeps two candidate lists per collider (walls `+0x30`, floors
`+0x34`).

**Narrow phase.** `PHYS_CollideSphereTriangle` (`0x45e724`): signed plane distance `n·c >> 15 − d`
(filtered by the collider's side flags: 1 front, 2 back), then the three edge
distances (`PHYS_GetEdgeDistance` (`0x45ddec`)); inside all three is a face contact (closest point =
projection, returns ±2), otherwise the closest point on an edge or vertex
(`PHYS_FindClosestOnSegment` (`0x45e260`), returns ±1) within the radius. The sign is the side of the
plane.

### Colliders and radii

Every entity gets one sphere at level load (`PHYS_InitEntity` (`0x43d62c`)):

- **walkers** (`+0xa9 & 2`): `PHYS_AttachActorCollider` (`0x40bedb`), both sides, radius actor
  `+0x10c`; the model's `ZZZZZ` node is the foot marker (`+0x84`) and is hidden
  along with `BASSIN01`;
- **free objects** (`+0xa9 & 8`): `PHYS_AttachObjectCollider` (`0x40be46`), radius **50**, mass 10;
- the camera eye gets its own (`CAM_CollideEye` (`0x40d5e4`), radius `0x49d2dc` = 100).

At most 30 registered (`PHYS_RegisterCollider` (`0x40bd2b`)), 255 per world. Mass defaults to 1.0.

### Integration (`PHYS_IntegrateMotion` (`0x43d360`), per entity per frame)

```
k      = Δt · (1/30) · 10                       Δt = 0x5e5388 (see The fixed step)
F      = Σ fields(pos) + own force (+0x260..+0x270)
v     += k · F / m                               v = +0x238..+0x248, m = +0x250
v     *= damp · +0x27c                           damp 0.99 flying/airborne, 0.9 ground, 0.7 swimming
step   = round(root_motion(+0x18..+0x20) + v + k · G / m)    G = Σ drift fields
pos   += step                                    step kept at +0x2a0, |step| at +0x2b8
```

Velocity is added to the position **without** a Δt factor: the step is `v`
per call.

**Force fields** (`PHYS_AddForceField` (`0x43ce37`), 0x60 bytes, at most 128): type 0 uniform
(`vector · m`), 1 box (uniform inside `+0x20..+0x48`), 2 radial inverse-square
(`d · m · strength / |d|³`), 3 drift (added to the step, not the velocity).
**Gravity** is a uniform field `(0, 9.81, 0)` created at level start by
`0x41ee09` (Y is down); the level record's `+0x100/+0x104/+0x108` override it
(`+0x104` negated), and each box record with a path adds a type-1 field
along it.

**Walking** (mode 1): while grounded the actor's own force is `(0, −9.81·m, 0)`
and vertical velocity is zeroed, so gravity cancels; airborne (`+0x278` bit 0),
the own force is 0 and gravity acts. **Swimming** (mode 2) and **flying** (mode 3)
cancel gravity on entry and ramp the entry vertical speed linearly to 0 over
15 and 30 Δt units (`PHYS_TickSwimVertical` (`0x43dd3a`), `PHYS_TickFlyVertical` (`0x43dc4f`)).

### Collision response (`PHYS_ResolveCollisions` (`0x40bff8`), once per frame)

1. **Walkers** (`PHYS_MoveWalker` (`0x40c42c`)): `PHYS_SweepCollider` (`0x40ca00`) moves the sphere from last
   frame's position (`+0x2ac`) to the new one in steps no longer than its
   radius, gathering contacts by priority (front face, front edge, back face,
   back edge) and summing one push-out per distinct normal
   (`PHYS_AccumulatePush` (`0x40c8c8`): radius − distance along the normal). The push is subtracted
   from the position: that is the **wall slide** — only the component into the
   wall is removed. On contact X/Z velocity halves (`0.5`), vertical velocity
   is zeroed if grounded, and `+0x278` bit 1 is set. A nearly still walker
   (speed < 2) ignores sideways pushes under 20 units, which stops jitter.
2. **Ground** (`PHYS_FindGround` (`0x40cfc5`)): among the floor candidates (walls, with normal Y
   0, are skipped) take the nearest floor below the feet, else the nearest
   surface above. Feet = the `ZZZZZ` node, or `+0x110` below the origin.
   Within the **step height of 100** (`0x49d2e0`) the walker snaps to 5 units
   above the floor and stays grounded; further below it becomes airborne;
   no floor at all sets `+0x278` bit 0x20 and keeps it grounded. It records the
   floor triangle (`+0x7c`), floor Y (`+0x88`) and the slope along the heading
   (`+0x298`).
3. **Free objects** (`PHYS_MoveBouncer` (`0x40c70c`)): same sweep, then the velocity reflects,
   `v − 2(v·n)n`.
4. **Entity pairs** (`PHYS_CollideEntities` (`0x40d2dc`)): spheres of `max(radius/2, |step|)`;
   overlap is split by mass ratio; both record the impact (`+0x294`, clamped
   127), the other entity (`+0x29c`) and `+0x278` bit 0x10.
5. **Platforms** (`PHYS_CollidePlatforms` (`0x43e32e`)): an entity flagged `+0xad & 8` turns each
   child node into a platform (at most 64, "too many PLT"; `PHYS_BuildPlatforms` (`0x43df1f`)).
   A walker inside a platform's box rides it (`+0x278` bit 0x40; the platform's
   frame delta is added and its top becomes the floor); otherwise it is
   pushed out of the platform's cylinder by the overlap + 5.

`+0x278` flags: 1 airborne, 2 wall contact, 4 entering flight, 8 entering
water, 0x10 entity contact, 0x20 no floor, 0x40 on a platform.

Shadows are blobs from `ombre.3dc`/`ombre2.3dc` (`ENT_InitShadows` (`0x43e55c`)), placed 10 units
above the floor Y and aligned to the floor triangle (`ENT_UpdateShadow` (`0x43e9aa`)).

Open: what `+0x258` (1.0) and `+0x280` (1000.0) are for outside the swim/fly
ramps; the hidden float in the pair radius; the line-of-sight segment test
(`0x45f654`) used by AI and projectiles.

## Subsystem naming

Recovered symbol fragments suggest a `<MODULE>_<Verb><Type>` convention:

- `DAN_Load3DC` — the `DAN` (animation) module loading a `3DC` geometry chunk.
  Note the cross-module call: animation data references geometry data.
- `Dreams_MSS1` — the audio module's Miles handle.

Format tags follow a matching four-character convention (`F3DC`, `DANF`, `DSNF`,
`DRDF`, `PAK0`, `UBIK`), which is consistent with a single shared chunk-IO layer
underneath all asset loading. See `file-formats.md`.

## External comparison leads [sourced]

- [ScummVM's CryOmni3D detection tables](https://github.com/scummvm/scummvm/blob/master/engines/cryomni3d/detection_tables.h)
  cover *Versailles 1685* and *Atlantis: The Lost Tales* assets such as HNM/UBB.
  This is useful for Cryo media comparison, but does not establish that their
  panoramic adventure engine shares *Dreams*' real-time `.DSN`/`.DAN` runtime.
- [NihAV Game Tool](https://nihav.org/game_tool.html) decodes Cryo HNM variants
  and extracts some Cryo archives. Use it as a reference for shared media
  formats; keep the game's 3D and skeletal formats as separate questions.
- The [1997 Génération 4 issue 100 archive](https://www.abandonware-magazines.org/affiche_mag.php?album=oui&mag=27&num=491)
  indexes its *Dreams to Reality* preview at page 148. Period previews and the
  [original manual archive](https://www.abandonware-france.org/ltf_abandon/ltf_jeu.php?fic=liens&id=1965)
  are useful for validating controls and observed animation states.
- In a [first-hand DxWnd investigation](https://sourceforge.net/p/dxwnd/discussion/general/thread/a2ddabcd22/),
  its maintainer reports that `WINDREAM.EXE` and `GDIDREAM.EXE` use different
  presentation paths and that emulated vsync prevents erroneous landing damage
  on modern hardware. The same thread flags `DATA/3DC/DESCRIPT.ION`; the local
  copies contain filename-level character hints, documented in `models.md`.
- A [2026 first-hand patch report](https://www.abandonware-forums.org/forum/forum-ltf-abandonware-france/aide-de-jeux-probl%C3%A8mes-techniques/919445-dreams-to-reality-%E2%80%93-restauration-am%C3%A9lioration-de-l-ombre-anim%C3%A9e-en-mode-3dfx)
  describes Duncan's animated ground shadow as following his limbs and braid.
  If reproduced in an original build, the shadow could help compare our
  skeleton poses frame by frame. The report does not supply a `.DAN` decoder.

This search did not verify another game using this exact `.DSN`/`.DAN` engine.
Compare format magic, binary imports, and node structures before treating
another Cryo title as an engine sibling.

## Reproducing this analysis

Import table and section dump:

```bash
python - "WINDREAM.EXE" << 'EOF'
# parse e_lfanew -> PE header -> data directory[1] (imports)
# walk IMAGE_IMPORT_DESCRIPTOR chain, resolve RVA->file offset via section table
EOF
```

String mining:

```bash
python -c "
import re,sys
t=open(sys.argv[1],'rb').read().decode('latin1')
for m in sorted(set(re.findall(r'[ -~]{6,}',t))):
    if re.search(r'WATCOM|Glide|Direct|CRYO|Miles|UniVBE',m): print(m)
" WINDREAM.EXE
```
