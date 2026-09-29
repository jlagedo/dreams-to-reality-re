# DREAMSFX.EXE Glide call inventory

Read-only audit of every **direct call from game code into the Glide 2 import
thunks** in `DREAMSFX.EXE`. `ExportFunctionFeatures.java` identifies 35 called
`gr*`/`gu*` entry points; `Inspect.java refs:<import-address>` finds 76 direct
call instructions. Addresses in the tables are call instructions in
`DREAMSFX.EXE`, not offsets in `glide2x.ovl`. The remaining Glide imports have
no direct game-code caller in this feature export. Calls made internally by
the Glide library are outside this audit. The larger rendering behavior is in
[glide-renderer.md](glide-renderer.md); the viewer implementation contract is
[Spec 003](specs/003-level-load-preview/spec.md).

Reproduce the inventory from the local project without writing to Ghidra:

```sh
uv run python tools/ghidra_headless.py -process DREAMSFX.EXE -noanalysis -readOnly -postScript Inspect.java refs:000b8a81 refs:000b8a90 refs:000b89dc refs:000b89be refs:000b89c8
```

The full audit used the same command with all 35 import addresses below and
cross-checked the callers against
`out/ghidra/features/DREAMSFX.EXE.json`. The import thunk names and prototypes
come from `ApplyGlideImports.java` and the local Glide 2 headers. Decompiled
arguments were checked against instructions where Watcom register passing
obscured them.

## Calls reachable from HNM5/HNM6 movie frames

The transitive call closure of the HNM5/HNM6 movie frame walkers (`0x4cf2c`,
`0x4d174`) reaches exactly **five** Glide API entries. HNM4's frame walker
(`0x4cd7c`) reaches none directly. The general renderer still uses some of
these five functions for other work.

| Glide API and import | Direct game call site | Input and effect | ODShared / sokol mapping |
|---|---|---|---|
| `grLfbLock` `0xb8a81` | `GLIDE_LockBackBuffer` `0x68047` | `WRITE_ONLY`, `BACKBUFFER`, RGB565, upper-left origin, pixel pipeline off, `GrLfbInfo*`. On success, wrapper loads the returned pixel pointer from `GrLfbInfo+4`. | Keep decoded pixels in owned CPU memory; upload a dynamic `sg_image`. |
| `grLfbUnlock` `0xb8a90` | `GLIDE_UnlockBackBuffer` `0x68065` | Unlocks write-only back buffer. | End the CPU frame update; no hardware LFB lifetime in the portable path. |
| `grClipWindow` `0xb89dc` | `GLIDE_SetClipWindow` `0x6720b` | Movie helpers pass `(0, 0, 640, 480)`. | Clip the video quad/canvas to its ImGui area or render-pass scissor. |
| `grBufferClear` `0xb89be` | `GLIDE_Clear` `0x674b6`; `GLIDE_ClearToBackground` `0x674a0`; `GLIDE_SetDepthWrite` `0x6754c` | Movie helper uses `GLIDE_Clear(0)`: black color, alpha zero, depth zero. Other callers clear background/depth. | Clear a black preview canvas; use normal sokol pass clears for scene depth. |
| `grBufferSwap` `0xb89c8` | `GLIDE_Swap` `0x674c7` | Movie path calls `GLIDE_Swap(0)` when the frame can present; wrapper also resets 256 texture update flags. Captions/outer callbacks can defer the swap. | Shell's `sg_commit`/platform present; explicit frame-ready event. Scene texture update flags stay in renderer state. |

`GLIDE_LockBackBuffer` writes `GrLfbInfo.size = 0x14`, calls `grLfbLock`,
returns the pointer in EAX, and `0x6801c` reads the **pitch in bytes** from
`GrLfbInfo+8` (`0x2b02d8`). Ghidra's C rendering of `0x6801c` as `return 0`
is contradicted by `MOV EAX,[0x002b02d8]` in the instructions. Both HNM6
`0x4b678` and HNM5 `0x4b8dc` use that pitch rather than assuming 1280 bytes
per destination row. Their 3dfx presentation writes 640 pixels for 300 rows
starting at screen row 90. Video payload decoding itself happens before the
LFB lock. Neither frame walker reaches `grTexSource`, a texture download API,
or `GLIDE_BindTexture`.

## Device and shared state calls

| Glide API and import | Direct game call sites | Role / modern replacement |
|---|---|---|
| `grGlideInit` `0xb8a5e` | `GLIDE_Open` `0x670f7` | Initialize Glide; SDL3 window and sokol backend setup. |
| `grSstQueryHardware` `0xb8ad1` | `GLIDE_Open` `0x67101` | Detect Voodoo hardware; replaced by backend/device capability discovery. |
| `grSstSelect` `0xb8ae5` | `GLIDE_Open` `0x6712d` | Select SST0; replaced by selected graphics backend. |
| `grSstWinOpen` `0xb8b03` | `GLIDE_Open` `0x67160` | Open 640×480/60 Hz, two color buffers and one depth auxiliary buffer; shell window, swapchain and depth attachment. |
| `grTexFilterMode` `0xb8b3a` | `GLIDE_Open` `0x67190` | Bilinear min/mag on TMU0; sokol sampler filtering. Not a movie decoder call. |
| `grGammaCorrectionValue` `0xb8a4f` | `GLIDE_Open` `0x6719a` | Gamma 0.8; renderer output transform/parity work, not movie parsing. |
| `grGlideShutdown` `0xb8a6d` | `GLIDE_Open` failure paths `0x6711c`, `0x6717b`; `GLIDE_Close` `0x67479` | Release Glide; shell/backend shutdown and failed-init cleanup. |

## Texture allocation, upload and sampling calls

These sit behind `GLIDE_AllocTexture`, `GLIDE_UploadTexture`,
`GLIDE_UploadAllTextures`, `GLIDE_BindTexture` and face submission. They are
**not** reached by the full-screen movie frame walkers. An HNM4 animated
material can later flow into this general texture path when the scene renders;
that is a separate consumption step from HNM4 chunk decoding.

| Glide API and import | Direct game call sites | Role / modern replacement |
|---|---|---|
| `grTexTextureMemRequired` `0xb8b67` | `GLIDE_AllocTexture` `0x66da5` | Measure TMU allocation; size/check an `sg_image` instead of TMU addresses. |
| `grTexMaxAddress` `0xb8b44` | `GLIDE_AllocTexture` `0x66dba`, `0x66dd3` | Bound TMU memory addresses; use GPU resource limits/allocations. |
| `grTexDownloadMipMapLevel` `0xb8b26` | `GLIDE_UploadTexture` `0x66b92`, `0x66bcf`; `GLIDE_AllocTexture` `0x66ec2`, `0x66eff`; `GLIDE_UploadAllTextures` `0x67015`, `0x67050` | Upload/refresh a material texture level; `sg_make_image`/`sg_update_image`, retaining retail 128×128 subsampling where selected. |
| `grTexDownloadTable` `0xb8b30` | `GLIDE_BindTexture` `0x67383` | Upload P8 palette row; portable palette expansion/shader palette resource, retaining cache semantics. |
| `grTexSource` `0xb8b62` | `GLIDE_BindTexture` `0x67340` | Bind a resident TMU texture; sokol texture view/binding. |
| `grTexClampMode` `0xb8b0d` | `GLIDE_DrawObjectFaces` `0x6774f`, `0x67770`, `0x6777b`, `0x67970`, `0x67b6f`, `0x67dbc`; `GLIDE_DrawTranslucentFaces` `0x6818d` | Per-block wrap/clamp; choose matching sokol sampler. |
| `grTexCombineFunction` `0xb8b17` | `GLIDE_DrawObjectFaces` `0x67744`, `0x67965`, `0x67b64`; `GLIDE_DrawTranslucentFaces` `0x68182` | Texture combine mode; material shader/pipeline variant. |

## Scene color, transparency, depth and fog calls

All entries in this table belong to the general 3D face/fog path, not to a
movie frame. The modern renderer mapping must preserve the type-specific
rules in [glide-renderer.md](glide-renderer.md) when scene fidelity is tackled.

| Glide API and import | Direct game call sites | Role / modern replacement |
|---|---|---|
| `grChromakeyMode` `0xb89d2` | `GLIDE_DrawObjectFaces` `0x67b76`, `0x67ba4`, `0x67d66` | Enable/disable keyed P8 blocks; shader discard/alpha mask. |
| `grChromakeyValue` `0xb89d7` | `GLIDE_BindTexture` `0x67373` | Palette entry zero as key color; material palette/alpha handling. |
| `grColorCombine` `0xb89e1` | `GLIDE_DrawObjectFaces` `0x675ff`, `0x67765`, `0x67db1` | Flat, texture and lit color combiners; shader variant. |
| `guColorCombineFunction` `0xb8b85` | `GLIDE_DrawObjectFaces` `0x6773b`, `0x67756`, `0x6795c`, `0x67b5b`, `0x67da2`; `GLIDE_DrawTranslucentFaces` `0x68179` | Utility color combine selection; shader variant. |
| `grConstantColorValue` `0xb89f0` | `GLIDE_DrawObjectFaces` `0x67704` | Flat diagnostic face color; shader uniform. |
| `grAlphaBlendFunction` `0xb89a5` | `GLIDE_DrawTranslucentFaces` `0x681a1`, `0x68368` | Alpha blend state and restoration; sokol blend pipeline. |
| `guAlphaSource` `0xb8b80` | `GLIDE_DrawTranslucentFaces` `0x68194` | Iterated alpha source; vertex/shader alpha. |
| `grDepthBufferMode` `0xb8a04` | `GLIDE_SetDepthWrite` `0x6750c`, `0x6752e` | Z-buffer mode; depth attachment/pipeline. |
| `grDepthBufferFunction` `0xb89ff` | `GLIDE_SetDepthWrite` `0x67513`, `0x67535` | `GREATER` test; depth compare state. |
| `grDepthMask` `0xb8a09` | `GLIDE_SetDepthWrite` `0x6751a`, `0x6753c` | Depth write enable/disable; pipeline depth state. |
| `grFogMode` `0xb8a45` | `GLIDE_SetFog` `0x671da` | Table fog enable; shader fog mode. |
| `grFogColorValue` `0xb8a40` | `GLIDE_SetFog` `0x671e0` | Packed fog color; shader uniform. |
| `guFogGenerateExp` `0xb8ba8` | `GLIDE_SetFog` `0x671ed` | Build exponential fog table; CPU LUT or equivalent shader function. |
| `grFogTable` `0xb8a4a` | `GLIDE_SetFog` `0x671f7` | Load 64-entry fog table; shader LUT/uniform data. |

## Triangle submission calls

| Glide API and import | Direct game call sites | Role / modern replacement |
|---|---|---|
| `grDrawTriangle` `0xb8a36` | `GLIDE_DrawObjectFaces` `0x6792f`, `0x67b2e`, `0x67d3f`, `0x67fb6`; `GLIDE_DrawTranslucentFaces` `0x6833f` | Draw un-clipped triangles; sokol vertex/index buffers and pipeline draw. |
| `guDrawTriangleWithClip` `0xb8b8f` | `GLIDE_DrawObjectFaces` `0x67724`, `0x67945`, `0x67b44`, `0x67d55`, `0x67fcc`; `GLIDE_DrawTranslucentFaces` `0x68352` | Draw clipped triangles; modern GPU clip stage with equivalent coordinates. |

This is **35 imported APIs and 76 direct call instructions** in total. The
movie subsystem's five entries are in the first table. Replacing those five
at the presentation boundary does not imply that the other 30 have been
ported, or that `GLIDE_BindTexture` belongs inside movie decoding. New retail
function ports still require the Windows checked-name/coverage mapping and
Ghidra tags described in [PORT_MAP.md](../opendreams/PORT_MAP.md).
