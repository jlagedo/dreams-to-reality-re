# 006 research archive — backend comparison and rendering alternatives

**Historical exploration, superseded as an implementation plan by
[spec.md](spec.md).** Preserved on 2026-09-29 when the owner selected the direct
3D + GPU 2D architecture. Evidence and unverified hypotheses below remain
research material. Alternative routes, candidate work items and open
architectural choices in this archive are not current requirements.

Status: **Exploration; modernized GPU 3D + GPU 2D is the target, no production integration.**
`DREAMS.EXE` imported into Ghidra;
the three builds compared with `re/tools/find_cut.py`; the cut tabulated; how 2D
is drawn over 3D traced in the retail code and the Glide 2 source; options for
2D composition, look, platforms and where the renderer lives surveyed, with
external practice. No game renderer implemented. Isolated CPU/GPU smoke tests
now exercise the earlier 3D boundary and a second GPU 2D boundary (linked below).
Remaining choices are listed under "Open questions".
Date: 2026-09-29
Depends on: [000 the recomp](../000-the-recomp/spec.md),
[glide-renderer.md](../../research/glide-renderer.md),
[glide-call-inventory.md](../../research/glide-call-inventory.md),
[engine.md](../../research/engine.md) ("Renderer backends", "Presentation and 2D"),
north-star.md

Follow-up: [modern renderer boundary](modern-cut.md) traces the earlier
function cluster needed for smooth projection and widescreen. It identifies
the frame entry points, the per-object cut before culling, the retail
transform-only helpers and the game-facing state that must survive. This is
a proposed boundary backed by read-only analysis and isolated smoke tests,
not an integrated game renderer.

Second follow-up: [GPU 2D boundary, contracts and smoke results](2d-cut.md)
maps sprite/text/compositor leaves and direct framebuffer bypasses. The intended
frame is **GPU 3D -> GPU sprites/text/UI -> present**, retaining lifted layout
and control logic. The covered drawing/copy operations compose without CPU
readback. Readback is a compatibility barrier for actual CPU consumers and
unported operations, not the assumed final architecture. The active pyramid
gauge still needs a GPU path before a whole-game readback-free claim is valid.

## Goal and boundary

The recomp runs `GDIDREAM.EXE`, whose software rasterizer draws every pixel
into a RAM frame that the SDL3 host uploads. `DREAMSFX.EXE`, the DOS 3dfx
build, draws the same scenes through Glide with bilinear filtering, a depth
buffer, fog and a deferred translucent pass. The goal is a recomp whose 3D goes
through sokol_gfx, driven by the rules of the Glide build, with a clear line
between retail-derived code and host glue, on Windows, Linux, macOS and the web
(WebGL2).

Cryo shipped three builds of one engine, so instead of guessing where to cut,
we find where **they** cut: code that is the same in all three is the engine,
and what differs is a backend.

| Build | Platform | 3D |
|---|---|---|
| `DREAMS.EXE` | DOS/4GW | software rasterizer on a VESA linear framebuffer (SciTech UniVBE) |
| `DREAMSFX.EXE` | DOS/4GW | Glide 2 (Voodoo) |
| `WINDREAM.EXE` / `GDIDREAM.EXE` | Win32 | software rasterizer on DirectDraw or a GDI DIB |

The two DOS builds differ only in the renderer, and they come from the same
compilers and flags, so their comparison is exact enough to show the renderer
cut. Comparing `DREAMS.EXE` with `WINDREAM.EXE` gives the platform cut (same
rasterizer, different OS), with the weaker matching that the Windows build's
unoptimized `-d2` code allows.

Like the rest of the recomp (spec 000), this is a research instrument. A
running implementation of the Glide contract inside the original game would
give OpenDreams a behavioural reference. north-star.md
rejects the Glide cut **for OpenDreams** because it keeps 4:3, the original FOV
and 640×480 culling. That limit also applies to a recomp using only the late
Glide hook. The modern 3D boundary moves before culling/projection, and the 2D
boundary makes GPU composition possible without a routine scene download.

## Method **[verified]**

1. `DREAMS.EXE` imported with the LE loader (`re/tools/ghidra_import.py
   --binaries …\DREAMS.EXE`, 57 s of auto-analysis). `ApplyWatcomSigs.java
   …\sigs\dreams.csv` named 284 runtime functions and labelled 2, with none
   unmapped. `ExportFunctionFeatures.java` exported 1,536 functions.
   `DREAMSFX.EXE` was re-exported (1,566 functions).
2. `re/tools/match_functions.py` on the three pairs:

   | Pair | Functions | Matched | Identical masked code |
   |---|---|---:|---:|
   | `DREAMS` ↔ `DREAMSFX` | 1,536 / 1,566 | 1,341 | 933 |
   | `DREAMS` ↔ `WINDREAM` | 1,536 / 1,944 | 692 | 262 |
   | `DREAMSFX` ↔ `WINDREAM` | 1,566 / 1,944 | 636 | 95 |

3. `re/tools/find_cut.py DREAMS.EXE DREAMSFX.EXE --third WINDREAM.EXE`:
   - **Link slots.** Watcom links each file's functions in source order. The
     longest run of matched pairs that are in the same order in both builds
     (1,322 of 1,341) therefore anchors the link order. Unmatched code between
     two anchors fills the same slot in both builds, so it is that build's
     implementation of the slot.
   - **Modified shared functions.** Matched pairs whose code differs, with the
     unmatched functions each side calls.
   - **Edges.** Calls and function-pointer stores from matched code into
     unmatched code.
   - Result: 54 slots differ (28 swaps, 14 `DREAMS`-only, 12 `DREAMSFX`-only),
     221 modified pairs and 86 edges.

   Outputs are `out/ghidra/cut/<a>--<b>.{slots,modified,edges}.tsv`, and the
   decompilations read are in `out/ghidra/cut/decomp/`.
4. The unnamed functions on each side of the video slots were decompiled and
   read. The roles below come from that code.

Against the DOS pair, the Windows link order is poor evidence: only 298 of its
692 pairs lie on one chain. Windows columns therefore come from matched callers
and the existing names, not from slots.

## The cut

`DREAMS.EXE` and `DREAMSFX.EXE` addresses are LE loader addresses. Names on
DOS-build addresses are those of their matched twin in the other builds;
`DREAMS.EXE` has no names of its own yet. `WINDREAM.EXE` addresses also hold
for `GDIDREAM.EXE` and are the hook points in the recomp.

### The frame pointer contract **[verified in code]**

Every build draws 2D (HUD, text, sprites, menus, movie frames) with the CPU into
a **current frame pointer**, with the frame's width, height and pitch in
globals. What changes by build is only what that pointer points at:

| | `DREAMS.EXE` | `DREAMSFX.EXE` | `WINDREAM.EXE` |
|---|---|---|---|
| back buffer pointer | `0x13fcf4`, into the VESA linear framebuffer `0x294044` | `0x159ff4`, set from `grLfbLock` | `g_frameBuffer` `0x5e549c`, RAM |
| set by | `VID_Swap` `0x10308`: toggle page `0x13fcf0`, VBE set-display-start `0x62bcc`, recompute back and front (`0x13fcdc`) pointers | `0x2025c`: toggle page, `GLIDE_LockBackBuffer`, pitch/2 → `0xf5988`, unlock | `VID_Swap` `0x4158e2` → `VID_Present` |

So the 2D code is shared, and on Glide it writes into the Voodoo back buffer
after the 3D scene has been rasterized there.

### Slots

| # | Slot | `DREAMS.EXE` (VESA) | `DREAMSFX.EXE` (Glide) | `WINDREAM.EXE` |
|---|---|---|---|---|
| 1 | Device open | `0x10248` from `GAME_Init`: `MGM_SendMessage(0x1a, "data\\univbe")`, find a 16-bit mode (then 15-bit) at the current resolution (`0xd608c`×`0xd6090`) with `0x56a9d`, set it with MGM `0x16`, swap twice | `0x67468` → `GLIDE_Open`, from `0x70d04` (the twin of `RES_InitArena`) with `GLIDE_SetDepthWrite` and `GLIDE_ResetTextureState`. `0x670e8`, called from `GAME_Init` and `MENU_HandleSystemPageInput`, is an empty stub | `VID_Init` `0x446019` |
| 2 | Device close | `0x10060` from `GAME_Shutdown`: restore the mode (`0x56a79`), `int 10h` mode 3 | `GLIDE_Close` from `GAME_Shutdown` `0x201b8` | `VID_ReleaseSurfaces` `0x4453ec` |
| 3 | Swap | `VID_Swap` `0x10308` (above) | `0x2025c` for the pointer, and `GLIDE_UnlockBackBuffer` + `GLIDE_Swap` inline at every caller | `VID_Swap` `0x4158e2` |
| 4 | 2D loops that present | `MENU_Tick`, `BOOT_TickFrame`, `CD_PrepareLevel`, `CD_PromptSwap`, `MENU_RunGameMenu`, `MENJ_PlayVoiceCaptions`, `0x4ce40` call `VID_Swap` | the same functions call Lock / GetLfbPitch / Unlock / Swap directly | the same functions call `VID_Swap`; Windows also wraps text and blit routines in `VID_Lock`/`VID_Unlock` (DirectDraw) |
| 5 | In-game frame | `0x1d3e8`: `REND_DrawFrame(back)`, letterbox bands, message | `0x2d458`: `GLIDE_Clear` (if `0xf5978`), `REND_DrawFrame(0x159ff4, …, 640, 480)`, reset clip, lock LFB, `UI_DrawHud(1)`, message, `GLIDE_Swap`, unlock | `GAME_DrawFrame` `0x423f60`: `PHYS_ResolveCollisions`, `VID_Lock`, `REND_DrawFrame(g_frameBuffer)`, letterbox, `GAME_DrawMessage`, `VID_Unlock` |
| 6 | Scene draw | `REND_DrawFrame` `0x624b0` / `…Ex` `0x62534`: software hooks, then span flush `0x78a60` (stored as a pointer by `0x62470`, the twin of `SW_SelectFlush`) | `REND_DrawFrame` `0x737e8` / `…Ex` `0x738d0`: `GLIDE_ClearToBackground`, hook = `GLIDE_DrawObjectFaces`, after the scene `GLIDE_DrawTranslucentFaces` | `REND_DrawFrame` `0x459320` / `…Ex` `0x4593a4`: `SW_DrawObjectFaces` + `SW_DrawObjectFacesPost`, flush through `0x4aa708` |
| 7 | Texture residency | none: the rasterizer reads RAM | `MDL_LoadMaterials` `0x7040c` → `GLIDE_AllocTexture`; `SCENE_LoadLevel` `0x2935c` → `GLIDE_UploadAllTextures`; `SCENE_StartAnimTexture` `0x3f290`, `SCENE_StartPageScroll` `0x3f508` and `SCENE_StartPageBlend` `0x3f5bc` → `GLIDE_SetTextureAnimated`; `0x70d04` → `GLIDE_ResetTextureState` | none (`MDL_LoadMaterials` `0x456038`, `SCENE_LoadLevel` `0x41f9db`, `SCENE_StartAnimTexture` `0x42dae2`, `SCENE_StartPageScroll` `0x42dea3`, `SCENE_StartPageBlend` `0x42dfa0`) |
| 8 | Fog | none | `SCENE_SetFog` `0x29288`, from `SCENE_LoadLevel` and from `0x2c904` (water-mode change) | none; the twin of `0x2c904` is `0x42315a` |
| 9 | Menu background | `0x52790`–`0x52914`: allocate, free, save front/back, restore, dim (565 and 555 variants) | `0x62a68` (clears the pointer `0x159ff4`), `0x62b14` (copies a saved frame into it); `MENU_RunGameMenu` calls `GLIDE_ClearToBackground` | `VID_AllocBackground` `0x417e18` … `VID_RestoreBackground` `0x417fe7`, `VID_DrawDimmedBackground` `0x418060` |
| 10 | Movie frame blit | `0x34f98` (HNM6), `0x3bbbc` (HNM5) | `VID_BlitHnm6Frame` `0x4b678`, `VID_BlitHnm5Frame` `0x4b8dc` (LFB pitch, rows 90–389) | `VID_BlitHnm6Frame` `0x4268ac`, `0x42665a` (HNM5) |
| 11 | Per-level material overrides | absent | `0x26e4c` from `SCENE_FindSkyNode` | `0x41ce67` via `0x41cdb8`: sets face type −7 (translucent) or 9 (wrapped decal) on named nodes of the current level |

Slot 11 is Glide-look data that the Windows build also has; the DOS software
build lacks it. **[verified in code; purpose inferred from the face types in
glide-renderer.md]**

The Glide `REND_DrawFrameEx` (`0x738d0`) shows how small the 3D swap is:
profiler start, `0x6f634`, `GLIDE_ClearToBackground`, second hook cleared, hook
= `GLIDE_DrawObjectFaces`, `REND_DrawScene`, `0x6b1b0`,
`GLIDE_DrawTranslucentFaces`, profiler stop. **[verified in code]**

### Not part of the cut

- `0x967c4`/`0x9680c` and their neighbours `0x96700`–`0x969b8`, in
  `DREAMSFX.EXE` only, are called from `PHYS_*`, `ANIM_*`, `REND_DrawFrame` and
  `RES_InitArena` in start/stop pairs. **[inferred]** This is the DOS 3dfx
  profiler (spec 005). `SW_CollectFaceTriangles` sits in the same block.
- Swaps of 3–15 bytes (`_dos_findclose_`, `_SetMaxPrec_`, `IF@DLOG`, near and
  far plane setters) are the same functions that the matcher left unpaired.
- `DREAMS.EXE` `0x625b0`–`0x63b54` (24 functions, 4.7 KB) and its runtime-area
  blocks are the VBE and DPMI support behind slots 1–3. `DREAMSFX.EXE`
  `0x81584`–`0x84070` and `DREAMS.EXE` `0x75404`–`0x78a60` are each build's
  software rasterizer. `DREAMSFX.EXE` keeps its rasterizer only as the object
  hook's initial value (engine.md).

## 2D over 3D

### The 2D code reads the pixels under it **[verified in code]**

- **50% sprites.** `SPR_BlitSprite` (Windows `0x401935`, Glide `0x2e085`)
  flag 2 writes `((src & 0xf7df) + (dst & 0xf7df)) >> 1`.
- **Per-pixel alpha sprites and faded text.** `SPR_BlitSprite` flag `0x10`
  and `TEXT_BlitGlyphFaded` (`0x403bcd`) read the destination and mix it
  through `SPR_BlendPixel` (`0x401524`) and `SPR_BlendChannel` (`0x4014d0`).
- **Dimmed menu background.** `VID_DrawDimmedBackground` (`0x418060`) redraws
  a saved copy of the finished frame, darkened.
- **Same code in every build.** Matched as identical code: `SPR_BlendPixel` and
  `SPR_BlendChannel` between `DREAMSFX` and `WINDREAM`; `SPR_BlitSprite`,
  `SPR_BlendPixel` and `TEXT_BlitGlyphFaded` between `DREAMS` and `WINDREAM`.
- **The Glide build blends against the framebuffer too.** Its `SPR_BlitSprite`
  (`0x2e085`) reads the destination through `0x159ff4`, the locked LFB pointer.
- **Frame order in Windows.** `GAME_DrawFrame` (`0x423f60`) runs
  `REND_DrawFrame(g_frameBuffer)` first; the letterbox bands and
  `GAME_DrawMessage` write afterwards. `REND_DrawFrame` (`0x459320`) and
  `REND_DrawFrameEx` (`0x4593a4`) end with the span flush through `0x4aa708`.
- **The "real shadow" is a second 3D render.** `ENT_RenderShadowTexture`
  (`0x43eb67`, Glide twin `0x5736c`) sets `REND_SetScreenSize(0x80, 0x100)`,
  swaps material 2 for `0x1b`, calls `REND_DrawFrameEx` and restores the
  viewport. Where its pixels land, and how the shadow texture is read from
  them, is **[unverified]**.

So an overlay that does not know the 3D pixel under each 2D pixel cannot
reproduce these blends. This is a requirement for **destination access**, not
necessarily CPU access: the [2D smoke](2d-cut.md) reproduces the tested packed
integer operations by sampling a previous GPU target into another attachment.

### How the 3dfx build composes a frame **[verified in code]**

In-game (`0x2d458`):

1. **3D on the card.** `GLIDE_Clear` when `0xf5978` is set, then
   `REND_DrawFrame`, whose Glide version draws the scene and the deferred
   translucent pass into the back buffer with depth and fog.
2. **Lock.** `GLIDE_LockBackBuffer` calls `grLfbLock(WRITE_ONLY, BACKBUFFER,
   565, UPPER_LEFT, pixel pipeline off)`. The game points `0x159ff4` at the
   returned address and sets the width from the pitch.
3. **2D.** `UI_DrawHud(1)`, the message and the FPS text run the shared CPU
   code into the card's memory. With the pixel pipeline off the writes are raw:
   no depth test, fog or blending. They replace the 3D pixels they touch.
4. **Present.** `GLIDE_Swap(0)`, then `GLIDE_UnlockBackBuffer`.

Screens without 3D (menus, boot, CD swap, captions) lock, draw, unlock and
swap. Movies write their 640×300 frame into rows 90–389 the same way.

### What its blends read on a Voodoo 1 **[inferred from the Glide 2 SST-1 source]**

From the Glide 2 source (`E:/tools/src/glide`, `glide2x/sst1`):

- Read and write locks return the same LFB window, `gc->lfb_ptr`
  (`glide/src/glfb.c:204` and `:313`).
- A write lock changes only the write-buffer select, origin, format and pixel
  pipeline bits of `lfbMode` (`glfb.c:241`). The read-buffer select is left
  as it was.
- Glide starts `lfbMode` at 0 (`glide/src/gsst.c:842`); read select 0 is the
  **front** buffer (`incsrc/sst.h:109`).
- A lock idles the chip unless `GR_LFB_NOIDLE` is passed (`glfb.c:151`).
- Dreams takes no read lock. `grLfbLock` is called only from
  `GLIDE_LockBackBuffer`, write-only, back buffer
  ([glide-call-inventory.md](../../research/glide-call-inventory.md)).

So on a Voodoo 1 the Glide build's blends would read the **front** buffer,
which holds the previous finished frame. Translucent 2D would blend against
last frame's image, and a static 50% sprite would converge toward opaque over a
few frames. Not observed on hardware. The Voodoo Rush (SST-96) Glide takes
another path, and Glide wrappers and emulators each choose their own
behaviour.

### Options for the recomp

| # | Option | How | Assessment |
|---|---|---|---|
| 1 | Colour-key overlay | 2D draws into a buffer cleared to a marker colour; non-marker pixels go over the GPU 3D | Blends read the marker instead of the scene: fringes on 50% sprites and faded text, wrong dimmed menu. A real pixel equal to the marker disappears |
| 2 | Written-pixel tracking | Instrument frame stores or guard pages to know which pixels 2D touched | Knows where, not what was underneath; blends still wrong on its own |
| 3 | 2D as GPU draws | Keep lifted control/layout; intercept pixel leaves and classified surface copies/fills; use quads, integer destination-sampling passes and GPU copies | Recommended modern direction. [Concrete map and smokes](2d-cut.md): tested common paths match original CPU pixels. Gauge, rare/debug paths and full integration remain; unknown CPU consumers need an explicit compatibility barrier. Standard alpha-over is insufficient. |
| 4 | Readback | Host `REND_DrawFrame` renders with sokol at 640×480, reads the image back and converts it to 565 in `g_frameBuffer`; the lifted 2D and present run unchanged | Keeps `REND_DrawFrame`'s software-build contract; blends, dimming, letterbox exact. 3D capped at 640×480 (as on the Voodoo); one CPU wait per frame (about 1.2 MB); per-backend readback code; a 565 conversion rule to choose (truncate or Voodoo-like dither) |
| 5 | Readback + changed-pixel overlay | As 4, and keep the pre-2D copy. At present, pixels that differ from it are 2D (blends included); show the high-res GPU 3D with only those pixels on top | High-res 3D where no 2D is drawn. A 2D pixel written with the 3D's own value shows the high-res 3D (looks the same). Black bands over black 3D count as unchanged, so the bands come from the letterbox globals (`0x49d9f8`). One 600 KB compare per frame |
| 6 | CPU Voodoo rasterizer | A host Glide backend that rasterizes on the CPU with Voodoo rules into `g_frameBuffer` (DOSBox Staging's approach; MAME's Voodoo code is a possible source, licence unchecked) | Most authentic (dithering, the front-buffer read quirk if wanted, exact filtering); no GPU sync; the 2D problem disappears. CPU cost; 640×480 only; separate from any high-res path |

Under options 4 and 5 the in-game frame function (slot 5) and the Windows
present path could stay lifted, so only slots 6–8 would change and no emulated
LFB would be needed. The blends would match the software builds, not the
Voodoo 1 front-buffer behaviour: the game uses one pointer for reads and
writes, so previous-frame reads cannot be reproduced without changing the
blitters.

The investigation now favours option 3 for the requested modernized frame.
Options 4/5 remain useful compatibility/reference techniques. The 2D prototype
does not depend on a scene readback; its readbacks exist only to compare test
results. Saved backgrounds and caption copies can remain on the GPU.

### CPU access to GPU memory

The CPU can write GPU memory: uploads (`sg_update_image`), shared memory on
integrated GPUs and Apple Silicon, Resizable BAR (D3D12 GPU-upload heaps,
Vulkan device-local host-visible memory), `VK_EXT_host_image_copy`. What it
cannot portably do is what the Voodoo LFB offered, a linear pointer into the
live render target for in-place reads and writes. Render targets are usually
tiled or compressed, the GPU runs a frame or more behind the CPU, reads from
mapped video memory are uncached, and sokol exposes only uploads. Option 4 is
the portable form of an LFB lock: lock is "wait and copy down", unlock is
"upload".

### How other projects do it **[sourced]**

| Approach | Who | Mechanism |
|---|---|---|
| Colour-key overlay | OpenGlide | Write lock returns a CPU buffer filled with `BLUE_SCREEN` `0x07FF`; unlock uploads pixels that differ and draws them with an alpha test; read locks use `glReadPixels` |
| Colour-key overlay | dethrace (Carmageddon), "3dfx mode" through BRender's OpenGL driver `glrend` | Locked pixels start as magenta `BR_COLOUR_565(31,0,31)`; flush uploads them to an overlay texture and draws it full-screen "ignoring purple pixels", then refills |
| Readback to CPU memory | Glide64 / GLideN64 (N64), Dolphin (GameCube EFB peeks) | GLideN64 blits the high-res image down to native resolution and reads it, synchronously (exact) or asynchronously (one frame late); Dolphin reads 64×64 tiles into a cache on GL, synchronised per peek on D3D11 |
| Keep buffers on the GPU | GLideN64 hardware framebuffer emulation, RT64 | Track which memory is a framebuffer and keep it as a GPU texture; copy only when the game samples it |
| CPU drawing over GPU | GLideN64 | Upload the CPU-drawn area and alpha-blend it over the frame, clearing it each frame |
| 2D as GPU operations | BRender `glrend` (pixelmap copies as texture uploads), GLQuake-style ports | Every 2D path goes through the driver |

Predicted, not tested: `DREAMSFX.EXE` under a colour-key wrapper such as
OpenGlide should show marker-coloured fringes on its blended 2D. The combination
in option 5 (readback as the key) was not found in these projects.

Not established: dgVoodoo 2's mechanism (its Glide readme advertises "perfect
lfb access" and "true PCI access" emulation but could not be fetched; a VOGONS
post lists LFB modes full, read, write and none). nGlide is closed source.

Sources:
[OpenGlide `grguLfb.cpp`](https://github.com/voyageur/openglide/blob/master/grguLfb.cpp);
[BRender v1.3.2 `drivers/glrend/devpixmp.c`](https://github.com/dethrace-labs/BRender-v1.3.2)
and [dethrace PR #434](https://github.com/dethrace-labs/dethrace/pull/434);
[GLideN64 frame buffer emulation I](http://gliden64.blogspot.com/2013/11/frame-buffer-emulation-part-i.html),
[II](http://gliden64.blogspot.com/2014/01/frame-buffer-emulation-part-ii.html),
[New Public Release III](http://gliden64.blogspot.com/2016/11/new-public-release-part-iii.html);
[Dolphin: The New Era of Video Backends](https://dolphin-emu.org/blog/2019/04/01/the-new-era-of-video-backends/),
[`FramebufferManager.cpp`](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/FramebufferManager.cpp);
[RT64](https://github.com/rt64/rt64);
[dgVoodoo2 Glide readme](https://dege.freeweb.hu/dgVoodoo2/ReadmeGlide/),
[VOGONS dgVoodoo thread](https://www.vogons.org/viewtopic.php?t=34931&start=80);
[nGlide](https://www.zeus-software.com/downloads/nglide).

### DOSBox **[sourced]**

| Path | Where | Mechanism |
|---|---|---|
| Low-level software Voodoo | DOSBox Staging (only mode); DOSBox-X software mode | The card is emulated on the CPU (code from MAME's Voodoo emulator; Staging spreads it over up to 16 threads). The framebuffer is host memory; triangles are rasterized into it; LFB reads and writes follow the card's `lfbMode`, so the front-buffer read should be reproduced; only the finished frame goes to the host GPU. No readback. Native resolution |
| OpenGL Voodoo | DOSBox-X `voodoo_opengl.cpp` | Registers emulated, triangles drawn with OpenGL at window resolution. LFB writes become `GL_POINTS` blocks (`voodoo_ogl_draw_pixel`); reads are `glReadPixels` of a whole scanline, cached per row, from the front or back buffer as `lfbMode` selects (`voodoo_ogl_read_pixel`) |
| Glide pass-through | DOSBox-X `glide=true` | Glide calls go to a host wrapper (OpenGlide, nGlide, dgVoodoo), which decides LFB handling |

DOSBox Staging, modelling the hardware rather than a wrapper, is the candidate
reference for parity screenshots of `DREAMSFX.EXE`.

Sources:
[DOSBox Staging 3dfx Voodoo manual](https://github.com/dosbox-staging/dosbox-staging/blob/main/website/docs/0.83/manual/graphics/3dfx-voodoo.md),
[DOSBox Staging `voodoo.cpp`](https://github.com/dosbox-staging/dosbox-staging/blob/main/src/hardware/video/voodoo.cpp),
[DOSBox-X `voodoo_opengl.cpp`](https://github.com/joncampbell123/dosbox-x/blob/master/src/hardware/voodoo_opengl.cpp),
[DOSBox-X Voodoo guide](https://github.com/joncampbell123/dosbox-x/wiki/Guide:Setting-up-3dfx-Voodoo-in-DOSBox%E2%80%90X).

## Look

### Screen-space vertices are whole pixels **[verified in code]**

`GLIDE_DrawObjectFaces` (`0x67568`) fills each `GrVertex` from integers the
engine computed at 640×480, and `oow` from the float camera-space z at
`+0x18`:

```c
GStack_128.x   = (float)*(int *)(vertex + 0x1c);
GStack_128.y   = (float)*(int *)(vertex + 0x20);
GStack_128.oow = 1.0 / *(float *)(vertex + 0x18);
```

Rendering these at a higher resolution sharpens textures, filtering, fog and
edges, but every vertex stays on the 640×480 grid, so edges step and shimmer
by up to half an original pixel as the camera moves. The vertex record also
holds camera-space x/y/z (`+0x10`); projecting from those in floating point
would remove the snapping. Nothing outside the render path reads vertex data
(engine.md), so that would change only the picture, by up to half an original
pixel: an enhancement, not the faithful look.

### Assessment by look

| Combination | Look | Cost |
|---|---|---|
| Option 5 + float projection | High-res 3D with smooth geometry; 2D at its native 640×480 art with correct blends; translucent 2D blends against the downsampled 3D (visible only under semi-transparent HUD and caption pixels) | Readback, compare, projection change in the host renderer |
| Option 3 | As above, with translucent 2D blending against the high-res 3D | Rewrite of every 2D writer, for a few pixels under translucent text |
| Option 5 alone | High-res textures and edges, snapped geometry | Readback, compare |
| Option 4 | The Voodoo look: everything 640×480, scaled to the window | Smallest |
| Option 6 | The Voodoo look including dithering and, optionally, the front-buffer blend | CPU rasterizer |
| Option 1 | As option 5 where it works; fringes on every blended sprite and caption | — |

In a high-res mode the 2D stays 640×480 art. Nearest-neighbour keeps it crisp;
a pixel-art scaler on the overlay alone would soften the contrast with the 3D.

## Platforms: Windows, Linux, macOS, WebGL2

sokol exposes the native texture behind an image on each backend
(`sg_d3d11_query_image_info`, `sg_mtl_query_image_info`,
`sg_gl_query_image_info`, `sg_wgpu_query_image_info`; `sokol_gfx.h`
5758–5813 in the pinned copy), so readback can be written beside sokol without
forking it. sokol itself has no readback call. **[verified in the header]**

| Option | Windows (D3D11) | Linux (GL core) | macOS (Metal) | Web: WebGL2 | Web: WebGPU |
|---|---|---|---|---|---|
| 4 Readback | staging texture + `Map` | `glReadPixels` / PBO | blit to a shared `MTLBuffer`, `waitUntilCompleted` | synchronous `readPixels`; stalls the pipeline; cost at 640×480 unmeasured | `mapAsync` is promise-only: no synchronous readback without yielding mid-frame |
| 5 Readback + overlay | yes | yes | yes | desktop browsers; heavy on mobile | as above |
| 6 CPU Voodoo rasterizer | plain C | plain C | plain C | slower in wasm; SIMD and threads help | works (upload only) |
| 3 2D as GPU draws | yes | yes | yes | yes | yes, no readback needed |
| 1 Colour key | runs everywhere; blends wrong | | | | |

OpenDreams already builds for all four targets with SDL3 + sokol (D3D11,
Metal, GL core, GLES3/WebGL2; `opendreams/CMakeLists.txt`).

The web target's larger problems are in the recomp runtime, not the renderer
(candidates for spec 000):

1. The game runs its own `PeekMessage` loop and never yields. ASYNCIFY over
   675,000 lines of generated C would be costly; running the game in a worker
   (`-sPROXY_TO_PTHREAD`) with an OffscreenCanvas would let it block, which
   also makes synchronous `readPixels` usable.
2. The game uses threads (`host/sdl/threads.c`); wasm threads need
   `SharedArrayBuffer`, hence COOP/COEP headers.
3. Guest memory is one arena at `g_mem_base` (16 MB plus a 768 MB heap,
   `runtime/imports.h:30-35`). Portable and within wasm32, but committed up
   front in a browser.
4. KERNEL32 still calls Win32 (spec 000).
5. If lifted x87 code relies on 80-bit `long double`, arm64 macOS and wasm
   have only 64-bit doubles; spec 000's x87 differential-test failures make
   this worth checking.

## Where the renderer could live

### What OpenDreams already has **[verified in port-map.tsv]**

| Retail function | OpenDreams port | Status |
|---|---|---|
| `GLIDE_DrawObjectFaces` | `shared/port/glide_model.cpp`: GPU batches for face types −7/−6/−5/−4/−3/2/3/9, signed UVs, clamp/wrap/chroma, deferred translucent ordering; checked on 95 scene graphs (157,433 faces) and 191 DAN graphs (50,910 faces) | adapted, partial |
| `GLIDE_BindTexture`, `GLIDE_ConvertPalette`, `GLIDE_SetTextureAnimated` | `shared/render/model_preview.cpp`: palette texture, per-batch palette row, dynamic pages | replaced |
| `GLIDE_AllocTexture` | `shared/port/glide_model.cpp` `model_texture_lod` (128×128 LOD) | adapted, partial |
| `SCENE_SetFog`, `GLIDE_SetFog` | `shared/port/fog.cpp`, fog table in the shader | adapted / replaced |
| gamma 0.8 | `ModelPreview::set_output_gamma` | in use |
| `GLIDE_LockBackBuffer`, `…Unlock`, `GLIDE_Clear`, `GLIDE_Swap`, `GLIDE_SetClipWindow` (movies) | `shared/render/glide_compat.cpp` (`od::GlideCompat`) | adapted, partial |

Differences from what the recomp would need:

- `GLIDE_DrawObjectFaces(const ModelGraph&, …)` reads OpenDreams' own parsed
  model data. The recomp has the live engine structures in guest memory: face
  list at node `+0xa4`, 40-byte vertices at `+0x80`, camera-space coordinates
  at `+0x10` recomputed each frame.
- `ModelPreview` projects on the GPU in floating point (`ModelView`), the
  "one stage earlier" cut from north-star.md.
- It is a preview renderer (one target), with parts still partial. Depth
  `GREATER`, readback and 2D composition are not there.

### Placements

| | Placement | Notes |
|---|---|---|
| A | Recomp-only: a C Glide layer in `recomp/windream/runtime` (the first plan: ports of the `DREAMSFX` Glide file plus the 35 `gr*`/`gu*` calls on sokol) | Self-contained; duplicates what ODShared already implements |
| B | Shared: factor a renderer out of `ModelPreview` in ODShared (per-frame batches of position, UV, material, palette row and face mode; material pages; fog; gamma; optional readback to RGB565), split the face-type rules in `GLIDE_DrawObjectFaces` from the `ModelGraph` walk, give it a C API, and add a recomp adapter at the `REND_DrawFrame` cut (guest face walk, texture cache keyed by guest page pointer, readback into `g_frameBuffer`) | One renderer for ODRuntime and the recomp; the recomp becomes a whole-game test of it (spec 000's instrument role, W5's hybrid runs). The recomp becomes a C/C++ build linked to ODShared with one SDL3 and sokol. Factoring changes existing port-map entries (`PORT_MAP.md` rules apply; `reviewed` is the owner's) |
| C | Either of the above, after or alongside the portable recomp runtime (KERNEL32, web) | The runtime work is needed for Linux, macOS and the web regardless of the renderer |

## Candidate work items

None is scheduled. Which apply depends on the open questions.

- **R0 — replacement hooks in `lift.py`.** Today `HOOKS` are read-only and
  `CALLS` insert calls before an instruction. Replacing `REND_DrawFrame` (and,
  in some options, slot 5) needs a lifted function replaced by host code that
  can still call lifted functions (a `REPLACE` table). Spec 000 W5 needs the
  same. Common to options 3–6.
  The 2D ABI also needs ESI/EDI, ECX and stack arguments: the current four-value
  diagnostic hook signature cannot describe these boundaries. Preserve the
  original return/register contract and shared scratch state used by retained
  neighbours; pixel parity alone is insufficient.
- **R1 — present through sokol.** Replace the SDL texture present in
  `host/sdl/gdi.c` with a sokol pass that draws the RAM frame.
- **R2 — renderer.** Placement A: Glide on sokol (the 35 calls). Placement B:
  the factored ODShared renderer with a C API.
- **R3 — scene.** Replace `REND_DrawFrame`/`…Ex` (slot 6) at the Windows
  addresses; texture residency (slot 7) at the Windows call sites; per-level
  overrides (slot 11) already run in the lifted code.
- **R4 — fog (slot 8)** at `SCENE_LoadLevel` and `0x42315a`.
- **R5 — 2D over 3D** by the chosen option (readback per backend, overlay,
  CPU rasterizer or GPU 2D).
  The [GPU 2D stages](2d-cut.md#staged-implementation) now provide the concrete
  plan: surface registry/audit, tested primitives, gauge and regular movie/menu
  completion, exceptional CPU consumers, then high-resolution/backend checks.
- **R6 — parity** against reference screenshots at fixed points.

## Measurements that would inform the choices

- **M1** In the current software recomp, snapshot `g_frameBuffer` when
  `REND_DrawFrame` returns and at `GDI_Present`; log changed pixels and their
  extent in play, menus and cutscenes. Confirms all 2D falls in that window and
  sizes option 5's overlay.
- **M2** Where the 128×256 shadow render of `ENT_RenderShadowTexture` writes,
  and how the shadow texture is read from it.
- **M3** Whether anything drawn before `REND_DrawFrame` in a frame is expected
  to survive under the 3D (menus, `VID_RestoreBackground`).
- **M4** Synchronous `readPixels` cost at 640×480 in desktop and mobile
  browsers.
- **M5** Reference screenshots of `DREAMSFX.EXE` under DOSBox Staging at fixed
  points (and whether its blends show the front-buffer effect).
- **M6** Optional: `DREAMSFX.EXE` under a colour-key wrapper, to confirm the
  predicted fringes.

## Exploration still to do

- **E1** `find_modules.py` for LE (data references from the feature dump,
  runtime start mapped through the LE page table as `ApplyWatcomSigs.java`
  does). It would give file boundaries inside each build, beyond the slots.
- **E2** Names for the `DREAMS.EXE` video functions above and for `0x41ce67`,
  through the registry and the two-source rule (`re/names/`; `DREAMS.EXE` has
  no registry yet).
- **E3** Slot 9 on Glide: what `0x62b14` restores and what the in-game menu
  shows behind it.
- **E4** Boundary pass on `DREAMS.EXE` (`ReportBoundaries.java`). Its
  functions come from auto-analysis alone, so pointer-only functions may be
  missing.
- **E5** Where the Windows build calls `UI_DrawHud` relative to
  `GAME_DrawFrame` (the Glide build calls it inside `0x2d458`).
- **E6** Licence of MAME's Voodoo code, if option 6 is considered.

## Open questions

1. **Where the renderer lives:** placement A, B or C.
2. **2D over 3D:** GPU 2D is the modern target; settle the remaining gauge,
   debug/legacy coverage and CPU-consumer fallback boundaries from the new audit.
3. **Modes:** a faithful mode only, or also an enhanced mode (high internal
   resolution, option 5, float projection); which is the default.
4. **Voodoo 1 blend behaviour:** reproduce the front-buffer read (possible only
   with option 6) or produce the software builds' blends.
5. **565 conversion** of the readback: truncation or a Voodoo-like dither.
6. **2D upscaling** in a high-res mode: nearest or a pixel-art scaler.
7. **Web backend:** WebGL2 or WebGPU, given WebGPU's asynchronous readback;
   worker thread or ASYNCIFY for the blocking game loop.
   Removing routine scene readback relaxes the renderer constraint; exceptional
   CPU exports and the runtime's blocking model still need a platform design.
8. **Parity reference:** DOSBox Staging, real hardware, or both.
9. **Where to present.** Glide presents at slot 5 or 4 through `GLIDE_Swap`,
   and Windows through `VID_Swap`. The recomp has to own exactly one present
   per frame.
10. **Palette animation.** The Glide cache key ignores the palette row
    (glide-renderer.md): keep the quirk or not, and check it at runtime.
11. **Relation to north-star.md:** whether a Glide-cut renderer in the recomp
    stays a research instrument or feeds ODRuntime's renderer (placement B
    blurs the line).

## File map

| Path | Content |
|---|---|
| `re/tools/find_cut.py` | Link-slot alignment, modified-caller and edge report for two builds, with an optional third |
| `out/ghidra/features/DREAMS.EXE.json` | Feature dump of the DOS software build |
| `out/ghidra/match/DREAMS.EXE--{DREAMSFX,WINDREAM}.EXE.tsv` | Pairwise matches |
| `out/ghidra/cut/` | `find_cut.py` output; `decomp/` holds the decompilations read (`dreams-video.txt`, `dreamsfx-video.txt`, `dreamsfx-2d.txt`) |
| `E:/tools/src/glide` | Glide 2 source checkout (outside the repo) used for the LFB findings |
