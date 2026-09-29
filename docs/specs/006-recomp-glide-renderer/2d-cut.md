# GPU 2D boundary for the modernized recomp

Date: 2026-09-29. Design exploration and isolated smoke prototypes; **not an
integrated renderer or a complete port**. Companion to [the 3D cut](modern-cut.md).

**The owner has selected this direct GPU architecture in [spec.md](spec.md).**
The evidence below supports it; the current implementation sequence and
acceptance policy come from that spec. Native-size or CPU-fallback experiments
are comparison tools, not required intermediate renderer architectures.

## Recommendation

The intended architecture should be **GPU 3D -> GPU sprites/text/UI -> present**.
The common CPU blends do not inherently need CPU pixels: they need the previous
destination colour, which a shader can sample. Background capture/restore can
be GPU copies. Decoded movies are CPU **sources**, not a reason to download the
3D scene. Keep readback at explicit CPU consumers or diagnosed compatibility
barriers. Regular-frame acceptance must pass with diagnostic fallbacks disabled.

Keep the lifted HUD, menu, text layout, dialogue and movie-control code. Replace
pixel-producing leaves and classify the direct memory fills/copies at their
call sites. Replacing only `SPR_BlitSprite` would miss the procedural gauge,
caption dimmer, masked thumbnails, movies, letterboxing and debug lines.

The smokes below establish exact packed-pixel equivalence for the tested
operations without readback during composition. They do **not** yet establish
a readback-free whole-game HUD: the pyramid gauge is an active uncovered leaf.

## Evidence and address map

Read-only Ghidra decompilation, instructions and cross-references were checked
against the existing feature export and two saved retail-process states.
There are **44 functions referring directly to the main/surface/saved-buffer
globals**, not all of which draw. The audit's **57 selected function extents
are byte-identical at the same addresses in WINDREAM and GDIDREAM**. Indirect
aliases can escape that inventory; it is not proof that every writer is found.

Addresses below are English Windows addresses. Hex-only entries have descriptive
roles here, not new registered names. No Ghidra names or port-map flags changed.

### Main interception points

| Address / existing name | Recovered input and output contract | Proposed treatment / evidence |
|---|---|---|
| `0x401935` `SPR_BlitSprite` | ESI -> `{x,y,flags,descriptor}`; AL divisor, BL half-X, BH half-Y, CL darken. Descriptor is `{paletteVA,w,h,originX,originY,unused,pixelsVA}` (28 bytes). Writes current `g_frameBuffer`; pitch `2*g_videoWidth`, clips against width/height. Preserves GPRs with PUSHAD/POPAD. | Primary sprite/ordinary-text cut. Flag priority and integer semantics below. Native/GPU tested for flags 0,1,2,4,8,16 including signed-high coverage; flag 4 uses the separate memory contract below. |
| `0x403bcd` `TEXT_BlitGlyphFaded` | Same ESI packet; AL divisor, BL/BH half scales. Indexed byte source, index zero skipped; coverage is `64/AL`, not AL. Writes current target. | Faded text cut. Anchors, clipped source addressing and alpha endpoints tested. Divisor zero is not a valid request. |
| `0x401524` `SPR_BlendPixel`; `0x4014d0` `SPR_BlendChannel` | EAX destination, EBX source; global alpha `0x5ecdfc`, format `0x49da1c`; pixel result in BX. Channel helper uses a 32x32 multiplication table. | Shader arithmetic specification, **not one host/GPU call per pixel**. Scratch writes are inside the drawing closure; preserve state used by any retained neighbours. |
| `0x40368b` `UI_DrawPyramidGauge` | ESI -> x,y and base/linked descriptor pointers; EAX/EBX/ECX meter thresholds, EDX state, EDI packed scale/mode bytes. Indexed/coverage texels; `ff/fe/fd` select linked layers or fills. Clips and blends against current target. | GPU marker/layer resolution is required for the completed HUD. A small-region CPU fallback is diagnostic only and cannot satisfy that milestone. |
| `0x427b8c` `SPR_DrawMasked64` | EAX -> 64x64 raw 16-bit pixels, EDX x, EBX y; mask bits at `0x49fd1a`, width/height-dependent sampling step 1 or 2. Raw masked copy, no format conversion and no general clip. | Masked quad. Controlled mask/source and destination switch tested. Bounds must follow caller contract. |
| `0x4018e4` | **Stack** `[entry ESP+4]` is starting y; full-width band, 16 rows when width>=640, otherwise 9. Each pair of pixels becomes `(wordPair & f7def7de)>>1`. No clipping. | Destination-sampling band pass. Both RGB565 and RGB555 retain this same literal mask. Tested. |
| `0x4368a1` `UI_DimCaptionBand` | No explicit args. y=`64/uiScaleY`; height=`132/uiScaleY`; channel-wise half brightness on all but last row, then one black row. Format-specific masks. | Destination-sampling pass plus fill. 640x480/scale-1 tested; other scales remain a coverage item. |
| `0x417f19` `VID_SaveSurfaceBackground` | Copy `0x5e548c` -> saved buffer `0x5e1090`, `W*H*2` bytes; lock/unlock; resets saved pointer to `0x5e1094`. | GPU copy/snapshot of the actual source alias, not an assumed front buffer. |
| `0x417f80` `VID_SaveBackground` | Same, current framebuffer -> saved buffer. Updates scratch `0x5e1098`; resets saved pointer. | GPU copy, tested interleaved with draws and dimming. |
| `0x417fe7` `VID_RestoreBackground` | Saved buffer -> current framebuffer, `W*H*2`; same pointer bookkeeping. | GPU copy, tested including drawing into the saved buffer before restoring it. |
| `0x418060` `VID_DrawDimmedBackground` | EAX level 0..3; reads saved image, writes current target. Integer masks/shifts, not an opacity fade. Level 0 still clears low bits. RGB565 level 3 uses UI-scaled dimensions; other branches use framebuffer dimensions. | Shader reads saved texture. All four levels in 565/555 tested with matching UI dimensions. |
| `0x417e18` / `0x417e7e` `VID_AllocBackground` / `VID_FreeBackground` | MEM-stack allocation/free and saved pointers. | Retain bookkeeping/failure behaviour; create/retire GPU companion by allocation generation. |
| `0x417eb9` / `0x417eee` | Save framebuffer pointer at `0x5e108c`, redirect to saved background, then restore. No incoming refs in current export. | Preserve alias changes even if dormant; exercised explicitly in the smoke. Commands capture target identity at enqueue time. |
| `0x4268ac` `VID_BlitHnm6Frame` | Calls `HNM6_DecompressFrame`, then copies from `0x60ce18` with mode-dependent placement/bars. At 640x480: **88 black rows + 304 copied rows + 88 black rows** (`0x5f000` copy bytes). | Keep decoder call/state; intercept placement after decode or its classified memory operations. Two decoded-frame fixtures tested. |
| `0x42665a` | HNM5 wrapper: source `0x60ce14`, target framebuffer, context `0x60ce1c`, resolution mode; stack call to `0x454de3` (Cryo movie helper). | Decode/place into a dedicated CPU movie surface then upload, or split the helper after tracing it. No GPU scene read is implied. Not GPU-tested here. |
| `0x45c278` `memcpy_`; `0x45fd36` `memset_` | Watcom EAX destination, EDX source/fill, EBX byte count. Not UI-specific. | Intercept only classified surface ranges/call sites. GPU copy/fill for GPU surfaces; keep ordinary RAM semantics elsewhere. Tests execute the original memory primitives. |

The three main glyph routes are `TEXT_DrawGlyph` `0x4257a0` -> sprite leaf,
`TEXT_DrawGlyphStyled` `0x425fd7` -> sprite leaf, and `TEXT_DrawGlyphFaded`
`0x425e74` -> faded leaf. Keep all cursor advances and formatting in lifted
code: `TEXT_PrintAt` `0x4258d4`, wrapping/scaled `0x4259bf`, centered
`0x425b4c`, faded `0x425f07`, styled `0x426073`, and debug string wrapper
`0x44d5c7`. The first two also invoke the **separate dim-band helper**.

Likewise retain `SPR_Draw` `0x4274b0`, portrait `0x427432`, indexed variants
`0x42756a`/`0x427624`, faded `0x4276e1`, dark `0x42779c`, and icon selection.
Their decisions produce the draw packets; they are not rendering backends.

### Bypasses and CPU consumers

| Address / caller | Behaviour / proposed boundary |
|---|---|
| `GAME_DrawFrame` `0x423f60` | Two `memset_` calls after 3D make top/bottom bars of `H>>3` rows. Translate those calls, not the collision/frame-control function. |
| `GAME_Tick` `0x4240ba` | Transition completion clears main/surface buffers black or white, sometimes only the cinemascope interior. Earlier fade progression updates palette-lighting state. Keep the state/time/RNG updates; translate the fills. |
| `BOOT_Run` `0x436481` | Full-frame clears through `memset_`; same classified-fill boundary. |
| `MENJ_PlayVoiceCaptions` `0x436ab6` | Inline copies between current/surface frame and an allocated temporary, in addition to caption dimming, text and portraits. Register that temporary as a surface only for its proven lifetime; captures/restores remain ordered GPU copies. Do not replace dialogue timing/control. |
| `0x465c80` | Clipped integer line rasterizer: EAX destination, EDX/EBX x0/y0, ECX x1, stack y1/colour. Pitch from render-screen width, inclusive endpoint tests. Called by collision wireframe `0x45f2a0`, wrappers `0x41b9d5`, `0x458434`, and box/debug routines. Exact integer rasterization needs a point/coverage implementation; default GPU line rules are not proven equivalent. Unported fallback. |
| `0x42d465`, `0x42d4c2`, `0x42d51f` | Horizontal/vertical/point writers with an explicit destination, reached by legacy widget `0x42d562`; no incoming ref to that widget. Keep in inventory; signatures/edge rules not yet fully audited. |
| `0x403e93` | Red strip across first ten rows; caller `0x439574` has no incoming ref. No general clipping. |
| `0x40175e`, `0x4017e0`, `0x401862` | Old paired-row coloured gauges, via `0x423be1` (no incoming ref). Carry/rotate arithmetic must not be replaced by ordinary alpha if revived. |
| `0x403385` | Alternate clipped compositor, forces descriptor width=64/height=128 and reads destination; no incoming ref. Descriptor mutation is a side effect; unported. |
| `0x4016e3`, `0x401723` | Dormant full-frame pair blend and scan-dependent in-place smear. The latter depends on earlier output, so a parallel one-pass shader is not an equivalent substitution. |
| `0x4395be`, `0x4396b8` | Capture frame and warp it using a CPU-updated displacement field. Root `0x43a0d8` has no incoming ref. Can eventually sample a GPU snapshot; source bounds and interpolation still need audit. |
| `0x4479c7` | Editor TGA writer reads each framebuffer pixel and calls file I/O. **Explicit CPU-readable pixel consumer**; read back on export, not per frame. |
| `GAME_SaveThumbnail` `0x40fd54` | 64x64 3D render followed by CPU copy/file write. Keep the small explicit readback from the 3D design, or intercept file serialization separately. |
| `0x4020a8`, `0x4020ce` | Ambient-tint pixel reader/plot entry points are bare RETs in this build. Do not introduce a scene download for their unreachable bodies. |
| `0x402406` | Editor sprite entry is also a bare RET. Code at `0x402407` and its self-modifying blitters is not reached through that entry. Restoring that feature would be an enhancement, not required pixel parity. |
| `0x404091` | White point writer into an explicit destination, fixed 640x400 bounds; wrapper `0x41b987` has no incoming ref. |
| GDI/DirectDraw present; recomp snapshot hook | Replace the present upload with the GPU final image. Debug BMP/screenshot requests need explicit GPU capture if their output remains CPU serialized. |

Some debug drawing occurs during collision separation **before** 3D. Preserve
that order; do not move every debug operation to the end of the frame.
Static absence of incoming refs is evidence of dormancy, not permission to
silently omit a path reached through a future hook or an unresolved pointer.

## Pixel rules that the adapter must retain

**Verified by instructions and packed-pixel comparisons:**

- SPR/ordinary font sources are indexed bytes, or `(index,coverage)` pairs.
  The loaded palette contains 16-bit colours. Index zero is the transparency
  test, not equality with palette[0]'s colour. Flag 1 writes index zero too.
- Flag 2 uses `((src & 0xf7df)+(dst & 0xf7df))>>1`. Darkening and the 555
  conversion happen in their retail order; they cannot be folded arbitrarily.
- Flag `0x10` uses coverage directly; flag 8 divides it by AL. Zero skips;
  ordinary positive coverage >=63 copies; intermediate coverage blends.
  **Coverage >=128 follows signed comparisons in the original.** The shared
  adapter now snapshots the addressed blend-table memory and evaluates the
  destination-dependent lookup on the GPU; it does not clamp these bytes to
  opaque alpha. Pyramid markers require their own interpretation.
- For blend weight `a=coverage>>1`, each five-bit component is
  `((31-a)*dst+a*src)>>5`. In RGB565 the helper extracts green with `>>6`,
  and writes it with `<<6`: **green's low bit is discarded**. In RGB555 it
  uses `>>5`/`<<5`. White over white at intermediate coverage becomes `0xf79e`
  in RGB565; standard alpha-over would remain `0xffff`.
- The flag-zero keyed branch writes a **DWORD**, colour in the low half and
  a right-neighbour value in the high half (normally `0x19e7`; zero after
  certain conversion/darkening paths). A later source pixel can overwrite that
  neighbour. Right-edge stores can wrap into the next memory row. This was
  verified in instructions and tested; clipping an ordinary textured quad
  alone loses the effect. A store beyond the entire registered allocation is
  still an uncovered boundary case.
- Clipping with half scales and faded glyphs has literal source-step quirks:
  leading and row skips are not a generic normalized-UV crop. The smoke
  normalizes from the request/source addressing, never from CPU output pixels.
- Flag 4 has highest priority, ignores scale/darken/divisor, clips against
  500x450, and writes raw palette words (including RGB555 padding). At
  `0x40208d`, `SUB EAX,0xfffffe0c` adds 500, so its net destination row stride is
  **videoWidth+1000 pixels**, not videoWidth. The adapter represents the actual
  linear-memory footprint, including row wrapping, and fails explicitly if an
  actual store leaves the registered allocation. Only clipping scratch is
  changed; the ordinary sprite-mode scratch remains unchanged.
- Signed-high blend weights address table indices -3072 through 4095 relative
  to `0x5e94bc`. The 28,672-byte window is snapshotted per affected draw. Values
  are added with 32-bit wrap, shifted unsigned, then combined and truncated to
  the original 16-bit store. Index 3664 aliases coverage scratch `0x5ecdfc`;
  the GPU lookup replaces that word's low byte with the current pixel's coverage
  before evaluating channels. This preserves the actual memory-dependent
  behavior rather than extending the positive-alpha formula into negative weights.
- Background dim levels are distinct mask/shift formulas. Level zero is not
  an identity copy. Caption and text-band dimming are different operations.

Keep palette updates/conversions in their original CPU control path, and
version their results for GPU sampling. Treat the compositor's stored values
as packed framebuffer colours, not automatically linear-light colours. The
Glide gamma correction belongs at final display output, **after** 3D, blends
and 2D. The smoke compares packed values before gamma; display conversion and
gamma parity remain a separate check.

The original preview shader applied gamma in its material fragment shader.
That shader has now been removed; both adapters use the shared core's separate
output pass. The frame architecture moves display correction
after composition; otherwise 2D blends sample already-corrected 3D while their
source palettes remain uncorrected. Keep a correction-free intermediate target.

### Non-pixel side effects

The replacement must preserve the leaf's guest ABI and observable metadata.
The sprite/faded/gauge routines share clipping/scale scratch at
`0x49d0e8..0x49d162`, and alpha at `0x5ecdfc`; notably the gauge reads the
sprite path's `0x49d126` flag. Do not wipe the block with wide stores:
`UI_TickGaugeFire` has adjacent PRNG state at `0x49d163`.
`SPR_BlendPixel` writes component/result scratch at `0x49d020..0x49d040`;
the audit found its uses inside the drawing arithmetic, not a live gameplay
pixel consumer. Preserve/replace the whole closure rather than invoking it
as a scalar GPU service. Full side-effect parity of a production host hook
has not been established by pixel-only tests.

Retain `UI_TickGaugeFire` `0x403b60` and `UI_CopyFireToGauge` `0x427ae9` as CPU
state/source updates. Their PRNG progression and source mutations must not run
on the display clock. The gauge draw packet needs a snapshot of the selected
descriptors, masks, thresholds, palette and frame state after those updates.

## GPU adapter design

1. **Surface registry.** Resolve guest byte ranges to surface ID, allocation
   generation, logical dimensions, byte pitch, format and content version.
   Main frame, saved background, caption temporary, thumbnail, movie source
   and shadow mask are distinct roles. Honour aliases: `0x5e548c` may equal
   `g_frameBuffer`; it is not necessarily a separate front buffer. The 3D
   shadow mask has its own P8 contract, not this RGB565 colour contract.
2. **Command capture at pixel leaves.** Resolve target and offsets immediately;
   keep layout/formatting/selection in lifted callers. Commands include sprite,
   glyph, masked image, fill, integer blend/dim and copy/snapshot. Capture
   clipping and scale inputs as they were at the call, including row-wrap
   footprint where required. A later framebuffer redirect must not retarget
   an earlier command.
3. **Source versions.** CPU sprite/palette/movie data is mutable. Copy its
   relevant source bytes or upload an immutable version before returning from
   interception. Pointer identity alone is insufficient. Retain GPU resources
   until their commands finish; freeing a portrait or reusing an arena address
   must not change an already queued draw. Dirty/version tracking can replace
   repeated snapshots after correctness is established.
4. **Execution in original order.** Submit before any dependent copy, new 3D
   pass, CPU-read barrier or present. Opaque non-overlapping draws may batch;
   overlapping destination-dependent operations require the updated result
   of the preceding draw. `VID_Lock` is not automatically a download: the
   intercepted operation determines whether CPU access is actually required.
5. **Destination-dependent GPU operations.** Use a previous-target texture
   and a different output attachment; never sample the active attachment.
   The prototype ping-pongs whole integer targets. Production can copy just
   an affected rectangle/tile to scratch and render the effect back, provided
   untouched pixels and inter-draw ordering are preserved. Standard alpha
   blending is insufficient for the proven integer rules.
6. **Copies and CPU authority.** Saved backgrounds/caption snapshots remain
   GPU-resident until a real CPU reader appears. Translate only proven copies
   involving registered surfaces; ordinary RAM memcpy/memset stay lifted.
   Literal fills can update CPU shadow bytes without downloading old pixels.
   A GPU-produced region is CPU-stale until explicitly materialized. Debug
   builds need access auditing around unported closures to detect missed aliases.
7. **Present.** The existing present boundary consumes the final GPU surface.
   It is independent of completing a thumbnail/shadow target, and menus/movies
   can present without a 3D pass. Do not force a CPU DIB upload at StretchBlt
   merely because the lifted caller still uses the old presentation API.

For oracle tests, a packed integer target is a straightforward exact reference.
The same direct renderer handles high-resolution targets and places native
UI artwork on the original logical canvas. Destination sampling/quantization
policy beneath enlarged UI pixels must be explicit: sampling the high-res
destination per output pixel is the selected **enhancement**, not pixel-identical to
blending one 640x480 destination sample. Do not make a whole-scene CPU download
the mechanism for that policy. Applying gamma early or an implicit sRGB
conversion during the integer blend would also change the result.

## Smoke tests and results

Sources:
[`render_2d_smoke.py`](../../../recomp/windream/debug/render_2d_smoke.py) and
[`render_2d_gpu_smoke.cpp`](../../../recomp/windream/debug/render_2d_gpu_smoke.cpp).
Original x86 runs in Unicorn, using retail-process memory for code, palettes
and descriptors plus controlled scratch destinations. Original lock/unlock
are replaced by no-ops (no pixel work in the tested GDI path). Original
`memcpy_`/`memset_` execute; flat segment descriptors are provided because
Watcom memcpy reloads ES. HNM6 decoding is replaced by an already-decoded
source; the original placement/copy/bar code executes.

The GPU prototype uses sokol on hardware D3D11, integer R32UI targets and
destination-sampling fragment passes. It snapshots palette-expanded source
texels on the CPU, uploads them, and composes pixels **on the GPU**. This is a
small correctness prototype, not a final atlas/cache implementation.

Baseline run: **128 checkpoints, 7,384,800 packed pixels compared, zero
mismatches**. Comparison is after each command, not only the final image:

| Stream | Checks | What it exercises |
|---|---:|---|
| RGB565 | 35 | Opaque/index-zero, keyed neighbour stores, half blend, coverage 0..63, divided alpha, faded glyphs, darkening, overlap, top/left/right/bottom and scaled clipping, right-edge wrap, bands, four dim levels, capture/restore, saved-buffer redirect, masked image, two offscreen destinations, pixel/palette mutation at reused addresses |
| RGB555 | 35 | Same operations with retail conversion and 555 blend/dim rules |
| CPU pitch 124 bytes | 35 | Width 62; GPU staging pitch is 256 bytes for R32UI, so neither CPU nor readback pitch can be assumed from the other |
| Controlled movie | 4 | 640x304 decoded pattern placed in 640x480, capture/dim/restore |
| Decoded GENERIC frame | 5 | Existing `generic-first.rgb565` fixture, caption band, capture/dim/restore; input path/hash recorded, codec not retested |
| Captured HUD/font sources | 14 | Four actual sprite requests captured while executing original x86 `UI_DrawHud`; keyed and faded 'A' in four loaded fonts, caption and text bands |

The HUD write trace identified **1,146 sprite stores and 2,942 gauge stores**
in that call. The gauge was inventoried but is **not** included in the GPU
parity claim. Movie tests likewise cover HNM6 placement, not HNM5 decoding or
all video modes. The second retail snapshot also passed 128 checkpoints with
zero mismatches, under `out/recomp/render-2d-smoke/long-capture/`. Across both
runs: **256 checkpoints and 14,769,600 compared pixels**. This repeats the
controlled cases and adds a second captured state; it is not 256 distinct
retail behaviours or a whole-disc coverage claim.

Representative baseline CPU timings (20 measured repetitions after warmup):

| Stream | Source bytes uploaded per replay | Upload median | Submission median | Benchmark-only GPU-fence wait median |
|---|---:|---:|---:|---:|
| RGB565, 35 operations | 9,376 | 0.029 ms | 0.060 ms | 0.116 ms |
| Captured HUD/fonts, 14 operations | 16,480 | 0.022 ms | 0.054 ms | 0.256 ms |
| Movie/caption, 5 operations | 778,240 | 0.073 ms | 0.027 ms | 0.358 ms |

Upload timing is `sg_update_image` of already-normalized source data; it
excludes CPU source normalization and initial resource creation. Many tiny
dynamic resources initially cost several milliseconds, reinforcing the need
for caching/atlases. The movie prototype expands RGB565 to R32UI, so its
upload size is twice the packed frame size. Timings are for this Windows GPU,
synthetic/isolated streams, not a full-game or cross-platform benchmark.

Composition performs **zero target readbacks**. Pixel validation deliberately
does one readback per checkpoint; the separate submission/upload benchmark
has no maps/readbacks and waits on one GPU event solely to measure completion.
GPU copies and pass dependencies still exist; eliminating CPU readback does
not eliminate GPU bandwidth or synchronization between draws.

Discrepancies found during development were resolved against instructions:
the text-band helper uses a stack argument, not EAX; keyed fonts have the
neighbour write; green is five-bit in the blend helper; dim level zero changes
bits; clipped source offsets are not generic UV clipping. The first band
prototype failed at the first band checkpoint and propagated into background
captures, demonstrating why interleaved checkpoints are necessary.

Run from the repository root:

```powershell
uv run --with unicorn python recomp/windream/debug/render_2d_smoke.py --gpu
uv run --with unicorn python recomp/windream/debug/render_2d_smoke.py --gpu --tag long-capture --dump out/scratch/retail-gdidream-223423.dmp
```

The native GPU portion requires Windows clang-cl and the existing pinned
sokol debug-build checkout. Dumps and the decoded movie fixture are local
game-derived inputs, not repository fixtures. Generated binary streams,
checkpoints, logs and reports remain under `DREAMS_OUT/recomp/render-2d-smoke`.

## Readback boundaries and remaining coverage

| Operation | Final design needs CPU readback? | Present prototype / compatibility path |
|---|---|---|
| Covered sprites, fonts, dimming, bars, masked copies | No | GPU integer operations proved on the listed cases |
| Menu/background/caption capture and restore | No, if the captured buffer is only copied or drawn | GPU snapshots; source/target alias and lifetime tracking required |
| HNM6 movie playback | No scene readback | CPU decoded image uploads; control and delta-decoder state stay lifted |
| Pyramid gauge | No in the selected completed design | A CPU read/modify/upload would be a recurring HUD fallback; allow it only for explicit diagnostics, not as the planned first path or milestone completion |
| Debug lines / unusual legacy effects | Not inherently | Per-operation fallback until coverage and exact raster/order semantics are known |
| Save thumbnail / TGA / requested BMP capture | Yes at CPU file serialization under the retained contract | Explicit small-target or screenshot readback; can later move serialization to host code |
| Any unrecognized CPU reader of GPU-owned pixels | Yes for the required region | Flush earlier commands, materialize, run original code, upload CPU writes |

Uncovered branches: arbitrary malformed
descriptors, beyond-allocation neighbour writes, all gauge marker/state/scale
combinations, other movie placement modes, HNM5, exact debug-line coverage,
dormant carry-dependent effects, and unrestricted target-layout changes while
retaining the same byte allocation. The prototype switches destinations and
tests different pitches, but does not implement a universal aliased-memory
surface system. GL/Metal/WebGL2 shader/resource support and costs are untested.

For high-res CPU fallbacks, a colour-difference mask is not proof of write
coverage. Instrument actual writes or explicitly accept native-resolution
replacement of the fallback region as an approximation. Otherwise unchanged
CPU values can conceal real writes over different high-res samples.

## Staged implementation

1. Add a diagnostic surface/call-site registry and trace CPU reads/writes in
   gameplay, pause/menu, captions, boot and movies. Preserve an all-lifted
   reference run. Unknown access fails direct-path acceptance; an explicitly
   enabled diagnostic fallback may materialize it, never silently use stale RAM.
   Establish the remaining metadata/ABI side effects.
2. Integrate the tested sprite/text/fill/copy/dim primitives, retain lifted
   layout/control, and present the GPU result directly. Include saved/caption
   surface lifetimes and source-version snapshots. Connect the direct 3D target
   and logical UI transform in this first slice, including high resolution and
   widescreen. Use native-size packed-pixel settings of the same renderer for
   oracle comparisons; they are not an intermediate architecture.
3. Implement the gauge's marker compositor and finish regular menu/movie
   paths. Keep fire generation and mutation timing lifted. Prove no unhandled
   main-frame access in the intended steady-state corpus before declaring
   routine scene readback removed.
4. Complete debug/rare compatibility paths and explicit CPU exports; measure
   fallback frequency, upload bytes and GPU pass costs. Optimize copies to
   regions/batches only after preserving overlapping-draw equivalence.
5. Complete high-resolution sampling coverage, verify gamma ordering and the
   logical UI mapping, then validate the
   other native backends and WebGL2. The boundary does not require synchronous
   GPU->CPU access for its covered normal drawing operations.

Raw static evidence: `out/ghidra/cut/2d-{contracts,extra,bypasses,last-contracts}.txt`,
`2d-address-audit.json`, and stripped extracts in `2d-functions/`. These are
research artifacts. The diagnostic shader is not a completed retail-function
port and does not change `port-map.tsv` coverage or owner review.
