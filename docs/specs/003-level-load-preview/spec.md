# 003 — ODViewer asset previews and playback

Status: **all viewer slices implemented, including HNM4 animated textures, audio-clocked movies with seek, and the Runtime/Viewer render-destination seam; scene lighting, fog and animated-material binding in progress**

Date: 2026-09-27

Depends on: [002 — retail asset access and game asset navigation](../002-disc-navigation/spec.md)

## Goal

ODViewer previews game assets directly from a selected original disc source:
models and project objects, scene geometry and materials, sprites/textures,
animated textures, movies, sound and dialogue. All previews occupy the
viewer's preview content region; playback controls and errors remain in that
pane. ODShared owns retail-derived loading, decoding and rendering state, so
ODViewer remains a selector and inspection UI. The selected row's physical
path and disc identity choose the primary source; equal names on different
discs or inside different archives cannot silently substitute for it. A project
object may use the other mounted disc only when absent from the primary source,
and the viewer labels that placement.

This spec is the single implementation contract for the **viewer side** of
those previews. The Windows programs are the game-flow, loader, movie and
render-traversal base. `DREAMSFX.EXE` supplies the GPU behavior at the cuts
where Windows uses software rendering or framebuffer presentation. The
original executables are research references; ODViewer calls the shared C++
port. ODRuntime gameplay and full-window rendering use the same renderer but
are validated in their own runtime milestones.

## Scope and delivery state

| Viewer capability | Spec 003 result | Current state |
|---|---|---|
| DAN model and project-object preview | Load the selected archive/object through shared resource and scene paths; render its materials and face modes. | Static preview composes the node hierarchy and draws multiple materials with DAN types 2/3/-5. All 191 physical DAN files load (50,910 faces) through retail material-directory binding. |
| Model animation | Select and play the bound DAN animation clips on the preview rig at the recovered 30 Hz rate, with pause/step and the selected source retained. | ODViewer plays selected linear and spline clips with pause, step, restart, scrub, loop and root-motion controls. All 1,048 physical clips decode and evaluate against the model node count. Eleven F03/ITO clips carry unused trailing tracks, now labeled in the viewer. Fixed-table precision, gameplay state selection and clip blending remain. |
| Scene and level preview | Render selected `.DSN`/project geometry and placed assets with the common renderer and a viewer camera; keep source identity. | All 95 distinct DSNs render their 157,433 source faces with 2,059 source texture pages. All 150 project records load, and all 560 active placements load in retail order with no material misses. The single per-file miss, `E21_RIDE.DSN:E21ARROW`, resolves through E22.DAN in Project 58. Viewer orbit/zoom/target controls work; dynamic palettes, fog and Runtime output remain. |
| Sprites, fonts and static textures | Display indexed pixels, palette rows and transparency from shared decoded data. | Selected sprite slots, font glyphs, VGA sheets, standalone/DAN material banks and assembled DSN object textures render in ODViewer with a palette swatch and transparency control. Animated materials and exact dynamic sprite composition remain. |
| Animated textures | Decode and show HNM4/HNS4 frame sequences; reuse the GPU material update path when a scene binds them. | The retail HNM4 walker and codec are ported. All 20 HNM4 files on both discs (3,033 frames) match NihAV's indices and palettes frame by frame, and ODViewer plays them. Binding frames to scene materials remains. |
| Movies | Decode HNM5 (`UBB2`/`UBS2`) and HNM6 (`HNM6`/`HNS6`) from the selected disc; play, pause, step, restart and show captions. | HNM5/HNM6 playback, SD audio and ST captions are wired in ODViewer, and all 95 physical HNM5/6 files decode to the end. Frames follow the played SD sample count, and a bounded seek replays from the start. Full corpus pixel parity remains. |
| Sound and dialogue | Play the selected supported sample or dialogue with shared audio output; movie `SD` sound stays synchronized with video. | FSB effects, DRD voices with timed captions/portraits, and selected CUE audio tracks play in ODViewer through SDL3. Retail spatial voice mixing, exact caption fades, hardware listening checks and movie audio-clock scheduling remain. |

Each row is a slice of Spec 003, not a separate proposal/spec. The completed
CAI slice stays available while later slices are added. A selected asset whose
decoder or face mode is not yet implemented reports that limitation in the
preview pane and leaves other sources and previews usable.

## First implemented proof: CAISSE

The first proof is the **`CAISSE` Model name row inside Disc 2's
`DATA/3DC/CAI.DAN`**. A 0x2200-byte preview record contains `OBJET0` as an
empty viewer scene and active `OBJET1` as `CAI.DAN`; the adapted scene
initializer creates the camera root in memory. The loader then attaches the
crate's ten-face model. The separate real **Project 71 / `OBJET2`** path loads
`E29USINE.DSN` and validates the same model against its retail spawn record.
That checked chain is in [the CAISSE trace](../../reviews/cai-prop-retail-chain.md).

## Existing model selection flow

1. Select a **Model name** row such as `CAISSE` under the physical `CAI.DAN`
   archive. Keep its selected disc and full physical path; the name alone is
   not a unique model identity. Selecting the archive row itself may preview
   the whole archive as well.
2. Build a viewer-owned 0x2200-byte preview project with an empty scene and
   one active object slot naming that archive. Set the ported equivalent of
   the current-project pointer (`0x00661e04`) and pending-load state
   (`0x00661e08 = 1`). Keep the selected disc's VFS context alive.
3. Invoke the adapted `SCENE_LoadLevel` entry once for this selection. Its
   preview scene initializer supplies an in-memory root; the object branch
   opens the selected DAN path, loads and relocates its model/material data,
   attaches its node, and applies the preview spawn transform. Do not reload
   on every display frame.
4. Keep the resulting graph and asset handles in the preview session. Draw the
   selected node to an offscreen GPU target and show it in the existing ImGui
   preview pane. Release the session on a new selection, source replacement or
   unmount. Errors remain visible in the pane.

The empty scene is an explicit viewer adaptation, so it does not pretend to
be a retail `.DSN` file. The real Project 71 path remains available as a
second load-chain test: it copies the retail record and reads its scene from
Disc 2.

## Minimum port for the first proof

| Boundary | Required work |
|---|---|
| Existing 002 foundation | Reuse the selected-source VFS and `DAN_OpenArchive`/`DAN_ReadAnimChunks`; the real Project 71 validation path also reuses `DDAT_LoadRecord` and `DSN_LoadHeader`. Keep source and lifetime rules. |
| Model and scene payload | Port the reached `LZ_Unpack`, `DAN_Read3DC`, and `DAN_Load3DM` payloads. The real Project 71 validation path also uses DSN tag-1/2 readers. Compare CAI and `E29USINE` data with the Python oracle. Scene texture application follows when the level itself is drawn. |
| Resource and node path | Port the reached `RES_ReadFile`, `RES_Load`, `RES_Relocate`, material load/binding, `MDL_AttachNode`, and node rotation/position setters. Preserve the original face pointers, signed UVs, block types and material-name binding. |
| Level entry | Port the selected DAN branch through `SCENE_InitLevel`, `SCENE_LoadLevel`, `ENT_InstantiateFromObjet`, `ENT_LoadModel`, and `ENT_ResetToSpawn`, including the transitive calls necessary to produce an attached node. The synthetic scene branch is explicitly viewer-specific. Keep Project 71 as an independent real-record check. |
| Preview renderer | Add a model-space GPU triangle/material path using the [3dfx contract](../../glide-renderer.md) for faithful face modes. Render into an offscreen target in ODViewer. No software span filler or DirectDraw presentation is needed. |

`SCENE_LoadLevel` is not a small wrapper. Its direct calls include CD
preparation, level initialization, object instantiation, texture loading,
palette setup, force fields and AI setup; `SCENE_InitLevel` reaches further
camera, animation, physics, trigger, particle and shadow code. The preview
may adapt or defer game-only effects only after checking their impact on the
selected load path. Each bypass must be named at its original call site and
recorded as remaining work. Calling the entry point does not make its retail
behavior complete.

## One renderer and two destinations

The common renderer takes an explicit destination per frame. **Runtime** draws
over the entire drawable SDL window or fullscreen swapchain; **Viewer** draws
into a color/depth offscreen target sized to the ImGui preview content region
in physical pixels. Viewer closes that pass before the main ImGui pass and
shows the resulting `sg_view` through `simgui_imtextureid`, following the
existing `ModelPreview` pattern. Resize recreates the offscreen attachments;
the preview never draws into the source list or details pane. Both modes use
the same scene, material, movie and lighting rules, with the retail horizontal
FOV recomputed for the target aspect. A 640×480 movie canvas scales within
either destination under one aspect policy.

`Shell::iterate()` owns `sg_setup`, swapchain acquisition, the main pass,
`sg_commit` and platform presentation. The runtime renderer must be invoked
inside that pass; the viewer offscreen pass runs before `ImGui::Image`. Neither
renderer mode creates another SDL window or commits independently. SDL3 owns
window/events, clocks and audio; sokol owns GPU textures, passes and drawing.

The **clear renderer cuts** follow the matched Windows/3dfx functions in
[engine.md](../../engine.md#renderer-backends--a-link-time-choice-with-one-hook-verified):

| Windows base | 3dfx reference | Spec 003 renderer boundary |
|---|---|---|
| `REND_DrawFrame` `0x459320`/`REND_DrawFrameEx` `0x4593a4` and `REND_DrawObject` `0x47e498` | Paired `0x737e8`/`0x738d0` and `REND_DrawObject` `0x96178` | Keep Windows traversal, transforms, culling and game-visible node output. |
| Hook `0x4ac8cc` → `SW_DrawObjectFaces` `0x473014`; post hook `0x4ac8d0` → `SW_DrawObjectFacesPost` `0x4731b8` | Hook `0x105004` → `GLIDE_DrawObjectFaces` `0x67568`; post hook is null | Install the modern GPU face hook using 3dfx face/material rules. Audit the Windows post-list behavior before dropping it. |
| `SW_FlushSpans` `0x4768cc`/scaled `0x476dec`, then `VID_Swap`/`VID_Present` | GPU triangles, deferred `GLIDE_DrawTranslucentFaces` `0x68128`, `GLIDE_Swap` `0x674c0` | Flush GPU work and deferred faces into the destination; Shell presents. No software span filler. |
| `VID_DecodeHnm6Frame` `0x408df2` → RAM blit `VID_BlitHnm6Frame` `0x4268ac` | Paired walker `0x4d174` → RGB565 LFB blit `0x4b678` | Keep the Windows stream/codec path and replace only the pixel-output boundary. |

The model slice does not need a gameplay tick or retail camera behavior. A
viewer camera is an explicit adaptation. The 3dfx face-type, P8 texture,
palette, clamp/wrap, chroma key, depth, fog and deferred translucency contract
is in [glide-renderer.md](../../glide-renderer.md), with every direct Glide
call site in [glide-call-inventory.md](../../glide-call-inventory.md).

### Glide-contract adapter

An explicit `GlideCompat` context in `shared/render/` isolates the 35 Glide
API behaviors the 3dfx build calls. This is **new portable infrastructure**;
retail `GLIDE_*` wrappers remain traceable when adapted. The first supported
set is the five calls reachable from HNM5/HNM6 movie frames:
`grLfbLock`, `grLfbUnlock`, `grClipWindow`, `grBufferClear` and
`grBufferSwap`. Scene texture, face, blend, depth and fog calls are added as
their preview slices reach them. Unsupported calls report an error during
development instead of silently claiming success.

The movie LFB contract is write-only RGB565, back buffer, upper-left origin,
pixel pipeline off. Lock returns a host-sized `{pixels, pitch_bytes}` whose
pointer remains valid until unlock. The original pitch getter at `0x6801c`
reads `GrLfbInfo+8`; Ghidra's apparent `return 0` for it is wrong. A CPU
640×480 RGB565 staging surface allows the original `lock → clip → clear →
write → unlock → swap` order. `swap` freezes that image; a later lock cannot
change a frame awaiting upload. Render-thread conversion to RGBA8 updates a
dynamic `sg_image` **once per sokol frame**, which the shared renderer draws
into the selected runtime/viewer destination. `swap` does not itself call
`sg_commit`.

This buffer transfers movie/2D pixels; it is not a software 3D rasterizer.
Partial LFB writes over a GPU scene need explicit written regions or overlay
commands so untouched CPU pixels do not erase GPU pixels. The first movie
slice uses the verified full-screen black clear and copy. General 2D/3D
composition remains an acceptance item before claiming full Glide behavior.

## Movie preview and audio path

Selecting a physical `.HNM`/`.UBB` row creates a source-scoped playback
session rather than using `Catalog`'s short-lived header-classification
`VideoState`. It keeps the selected `shared_ptr<Image>`, `VfsContext`, stream,
codec buffers, audio state and captions alive until selection changes or the
source unmounts. Logical project/video references resolve to a physical row
without losing disc provenance. `INTRO.HNM` on Disc 1 is not interchangeable
with the different Disc 2 file of the same name.

The Windows-base entry points are `VID_Open` `0x4085dc`, `VID_Close`
`0x40887b`, `VID_DecodeFrame` `0x408816`, HNM6 walker `0x408df2`, HNM5 walker
`0x408b4e`, HNM4 walker `0x408939`, and `VID_IsFrameDue` `0x40910a`.
`VID_Open` already peeks and commits 68 bytes: the 64-byte header and the
next four bytes. For HNM5/6 that saved word is the first outer superchunk
size; `step()` must not consume it twice. Walkers validate outer/inner sizes,
padding, frame counts and EOF before handing `IX`/`IV`, `PL`, `SD` or `ST`
payloads to family-specific code. The decoder and container live in
`shared/port/` with no sokol or SDL rendering dependency.

HNM6 needs `HNM6_DecompressFrame` `0x45c2c0` and its reachable block,
coefficient, IDCT, color and double-frame state (22 DOS functions in the
saved direct call graph). HNM5 needs the large `HNM5_DecodeFrame640`
`0x44e9b0`, indexed double buffers, `PL` palette updates and RGB565
expansion. HNM4 supplies animated 256×256 textures, with a separate codec
and palette. [cryolib.md](../../cryolib.md), [hnm-video.md](../../hnm-video.md)
and [hnm6-spec.md](../../hnm6-spec.md) contain codec/container evidence;
NihAV and the existing extracted files are independent structural and motion
cross-checks, not runtime dependencies. HNM6 color must follow the retail
`HNM6_StoreBlockRGB16` formula, as recorded below. No game video belongs in
the repository or app binary.

The 3dfx movie call audit supplies the output contract without changing the
Windows-base dispatcher:

| Stage | `DREAMSFX.EXE` direct path | Relevant inputs/state |
|---|---|---|
| Open/close | `0x4caf0` / `0x4cd04`; family init `0x4cd64`, `0x4cf10`, `0x4d13c`; family close `0x4cae8`, `0x4b804`, `0x4b664` | Open by selected path, classify six magic values, keep 68-byte cursor handoff, allocate family buffers, stop sound/close stream on replacement. |
| Step | `0x4ccc8` → HNM4 `0x4cd7c`, HNM5 `0x4cf2c`, HNM6 `0x4d174` | Low kind bits 1/2/4 dispatch; process `IX`/`IV`, `PL`, `SD`, `ST`; decrement frame count, post end event. |
| HNM6 frame | `0x4d174` → `0x4b678` → `HNM6_DecompressFrame` `0x90040`; frame-pointer swap `0x4b77c` | `IX` payload, previous RGB16 frame, destination RGB16 frame and zero are passed to the decoder. The presentation helper writes 640×300 at row 90 using LFB pitch. |
| HNM5 frame | `0x4cf2c` → `HNM5_DecodeFrame640` `0x44df0`; `PL` `0x4b828`; pointer swap `0x4b96c`; blit `0x4b8dc` → `0x4b343` | `IV` payload; two 640×304 indexed frames and a 256×256 expanded RGB565 pair lookup. Blit passes indexed source, LFB destination, lookup and mode 6. |
| HNM4 frame | `0x4cd7c` → `0x4ba07` and `0x4ba36`/`0x4baa6`/`0x4bb26`/`0x4c75c`; palette `0x3ffc0` | Animated texture decode; no direct Glide LFB call in this frame walker. |
| Sound/caption | `SD` `0x4d364` → DPCM `0x9033a`; Miles setup `0x4d534`/readiness `0x4d5a4`; `ST` inline → overlay `0x4d654` | First sound chunk's 512-byte delta lookup, two persistent predictors, 2,940 codes → 5,880 PCM bytes. Overlay may defer unlock/swap. |

The HNM6 frame decoder's 22-function direct call closure is `0x90040`,
`0xa51c4`, `0xa52fc`, `0xa53b3`, `0xa5c44`, `0xa5ec4`, `0xa5f50`,
`0xa66f4`, `0xa676c`, `0xa6c20`, `0xa6ed4`, `0xa71a0`, `0xa7404`,
`0xa7568`, `0xa76e8`, `0xa7804`, `0xa7878`, `0xa7994`, `0xa7a3c`,
`0xa7ae4`, `0xa7cd0`, `0xa7da8`; initialization additionally reaches
`0x90020`/`0x90030`, `0xa4fa0`, `0xa5078`/`0xa511c` and `0xa65f0`.
These are call-graph boundaries, not complete function-port claims; indirect
branches, data tables and error paths still require an audit. Reproduce
individual bodies with `ghidra_scripts/Decompile.java` on read-only
`DREAMSFX.EXE` and compare against `WINDREAM.EXE` before fixing C++
signatures. Watcom register arguments and Ghidra's guessed prototypes are
not authoritative.

The Windows frame pump posts silent-video frames from its 15 Hz timer. For
sound-enabled video it calls `VID_IsFrameDue` against the 200 Hz counter
(14-tick threshold, 13-tick advance). The 3dfx edition instead consults
Miles buffer readiness for sound video; that is comparison evidence, not a
replacement for the Windows-base scheduler. The `SD` payload uses a 256-entry
signed delta table and two persistent 16-bit stereo predictors; compare PCM
against `src/dreams/formats/video.py::extract_sd_audio` before connecting an
SDL3 audio stream. `ST` text becomes a caption overlay within the target.
Playback offers play/pause, single-frame step, restart, progress and bounded
seek by replay from a known keyframe or start. Decode work must not monopolize
the UI thread; GPU image updates stay on the render thread.

The decoded HNM6 picture is 640×304. The 3dfx blitter writes 640×300 rows
at screen row 90 into a black 640×480 canvas; the Windows 640×480 blit uses
304 rows at row 88. Keep the complete decoded frame for codec checks and
make the chosen retail framing consistent in Runtime and Viewer. The initial
GPU output can preserve the 3dfx 300/90 presentation while comparing both
retail outputs. `GENERIC.HNM` is the first short HNS6 test; follow with the
real Disc 1 `INTRO.HNM`, an `UBS2` sound movie and an HNM4 texture.

## Other viewer preview slices

- **Models and animation:** the static DAN preview now handles multiple
  materials, P8 palette rows, clamp/wrap, chroma key and parent transforms.
  Complete lighting, diagnostic faces and deferred transparency against retail
  observations. Bind DAN clips to the attached rig without embedding a
  gameplay loop in ODViewer.
- **Scenes/projects:** select a physical `.DSN` or project record, load through
  the same bounded scene/resource path, and show its geometry, materials and
  placed actors under a viewer camera. Original spawn and transforms remain
  available for checks; unsupported records do not silently disappear.
- **Sprites, fonts and textures:** expand the existing shared sprite/font and
  material readers into an indexed-pixel preview with the selected palette,
  transparency and source metadata. HNM4 frames can feed the same texture
  upload path when the material is animated.
- **Audio and dialogue:** the implemented viewer service plays selected FSB
  samples, DRD voice with its own timed text/portrait and disc audio tracks
  through SDL3. Runtime spatial mixing, retail text fading and movie audio-clock
  scheduling remain separate work.

The viewer controls and diagnostics are new UI code. Their data and behavior
come from shared ports wherever retail has a counterpart. Each slice should
arrive with one representative original-disc case, a bounded malformed-input
case where needed, and a comparison against an independent Python or retail
observation.

## Implementation order

1. **Renderer destination seam:** add an explicit swapchain/offscreen target
   and pixel viewport to the shared renderer. Prove full-drawable Runtime and
   preview-region Viewer output with the same test scene/frame at matching
   aspect; keep Shell's one commit/present owner. The Runtime proof does not
   introduce gameplay into this spec.
2. **Model/scene GPU path:** the common preview path now handles static DAN
   models, all physical DSNs and placed project objects. Finish lighting,
   transparency parity, dynamic materials and the Runtime destination seam.
   Preserve the CAI and Project 71 checks at every step.
3. **Static and animated assets:** sprite/font/static-texture display and DAN
   rig playback now use the selected-source viewer path. Add HNM4 frame decoding
   and share animated material updates with the model and scene renderer.
4. **HNM6 movie slice:** extend `VideoState` into a persistent session, port
   the `IX` decoder state and the five movie-reachable Glide contracts, then
   show `GENERIC.HNM` with silent 15 Hz playback, pause/step and source-safe
   cleanup in the Viewer target. Compare complete decoded pixels before
   validating the 3dfx 300/90 presentation crop.
5. **Movie sound and other families:** add `SD` PCM through SDL3, `ST`
   captions, HNM5 `IV`/`PL` movies, then corpus sweeps across all HNM/UBB
   files. Add standalone sound/dialogue and CD audio output through the same
   selected-source lifetime rules.

Each slice lands as a working ODViewer capability with its shared ODShared
implementation and focused validation. A reader or metadata label alone is
not a completed preview. Do not add runtime-only game behavior to make the
Viewer slice look complete.

## Acceptance by slice

### Existing CAI regression gate

- With Disc 2 mounted, selecting the `CAISSE` **Model name** row whose path is
  `DATA/3DC/CAI.DAN` loads once through the shared C++ `SCENE_LoadLevel` entry
  and shows the textured crate in ODViewer. Another `CAISSE` row retains its
  own disc/archive identity.
- The synthetic preview session has one in-memory scene root and active
  `OBJET1`, an attached CAI node, ten stored faces and a bound material.
  The separate Project 71 test retains spawn `(7312, 3187, 124)` and heading
  `2226` from its real `OBJET2` record.
- The preview uses a GPU offscreen target. It performs no software span flush,
  DirectDraw call, or original executable invocation.

### Remaining Spec 003 acceptance

- The same renderer can draw a representative scene or test frame to the full
  ODRuntime drawable and to an ODViewer offscreen target bounded by the
  preview content region. Resizing and high-DPI scaling keep the image inside
  that region; identical target aspect/camera produces equivalent content.
- A selected supported DAN model draws its original materials and reached
  face modes without silently changing archive, disc or material. Animation
  controls advance a bound clip at the recovered rate; scene/project preview
  retains its source scene and object transforms. The level-viewer slice
  covers all 95 distinct scene names from the configured corpus, or reports
  an explicit per-scene unsupported reason while that slice is in progress.
- Selected sprites, fonts and textures show their decoded pixels, palette and
  transparency. HNM4 playback changes frames without changing a scene's
  source/material identity.
- A selected `GENERIC.HNM` produces coherent consecutive HNS6 frames in the
  preview pane with pause/step/restart and correct framing. HNM5/UBS2 and
  HNM4 each have a representative decoded playback case. Sound-enabled
  movies produce PCM matching the Python oracle, and `ST` text is visible
  without leaking outside the preview target. Missing/short/corrupt chunks
  fail with a bounded error rather than a stale image or busy refill loop.
  A corpus sweep accounts for every physical HNM/UBB file on both discs,
  including duplicate filenames with different source bytes.
- Selected supported standalone audio/dialogue plays and stops; replacing or
  unmounting its source closes the stream and releases its output channel.
- Selecting another row or replacing/unmounting the source disc releases the
  old preview session and GPU/audio resources; an unavailable or unsupported
  selection reports a useful error without displaying stale content.
- Focused loader tests compare the reached project, model and scene data with
  the Python reference. Movie frame checks compare codec pixels before the
  3dfx/Windows presentation crop, then compare framed output separately.
  Disc-dependent tests skip when the user has not configured the original
  images. Native builds and the Emscripten compile check continue to pass;
  browser disc-image opening remains deferred by 002.

## Port accounting and limits

For every retail function implemented in any Spec 003 slice, update
`opendreams/port-map.tsv` with the C++ change, run
`uv run python tools/check_port_map.py`, and apply the map to both Windows
programs in Ghidra under [the port-map policy](../../../opendreams/PORT_MAP.md).
Use `coverage=partial` wherever a retail branch, side effect or callee remains
deferred, and list it in `remaining_work`. The `reviewed` field remains `no`
until the project owner explicitly reviews that entry.

`GlideCompat` and ODViewer controls are new infrastructure/UI. Their mapped
retail `GLIDE_*` entry points have `DREAMSFX.EXE` rows in the port map; the
checker validates those against the DOS name registry without inventing a
Windows twin. Use 3dfx
function calls and [the full inventory](../../glide-call-inventory.md) as
behavioral evidence for the Windows-base GPU replacement. No separate Glide
library, original executable, `na_game_tool` or ffmpeg is required at runtime.

## Movie implementation record — 2026-09-27

`shared/port/video.cpp` keeps the retail 68-byte stream handoff and steps
HNM5/6 outer and inner chunks with bounded reads. `hnm5.cpp` ports indexed
strip/motion frames and palette updates; `hnm6.cpp` ports the quality, DCT,
motion and double-frame path. `video_audio.cpp` holds the SD delta table and
two persistent 16-bit stereo predictors. The decoder has no SDL, sokol,
original-binary or external-tool dependency.

`render/glide_compat.cpp` implements the five movie-reachable Glide operations
on a 640×480 RGB565 staging surface. `render/video_preview.cpp` copies the
retail 3dfx 640×300 picture at row 90, freezes the frame on swap, uploads one
RGBA8 sokol image per Shell frame, and queues 22050 Hz stereo PCM through the
shared SDL `AudioOutput` service.
ODViewer selects the physical row and disc, offers play/pause/step/restart,
shows `ST` text within the pane and closes source, audio and GPU state on a
selection or mount change. `--cue2 <path> --preview-movie` selects Disc 2's
`GENERIC.HNM` for repeatable startup checks.

The HNM5 `HNMFR2.UBB` frames match the independent NihAV RGB output after
RGB565 quantization. The HNM6 container and block decode previously matched
NihAV RGB for the first four `GENERIC.HNM` frames, but that color agreement
was not retail color proof: direct review of `HNM6_StoreBlockRGB16` in both
Windows (`0x47f814`) and `DREAMSFX.EXE` (`0xa5c44`) found that its blue term
uses `U_raw >> 3`, where the old C++ conversion used the smaller `U/3` green
term. The shared HNM6 port now follows the retail store formula; its current
frame fingerprint is a regression check, while exact low-bit IDCT color parity
still needs a raw retail-frame comparison. The first SD PCM batches match the
Python oracle byte for byte. Both files also decode
through their final frame using the selected-disc VFS. A two-disc corpus sweep
decodes all 95 HNM5/6 files and 25,450 frames with no failures; the 20 HNM4
animated textures are separately identified. The demo variants at 512×408
and 640×480 use their header dimensions and are centered in the viewer canvas.
Native viewer
smokes for `--preview-movie` and the existing `--preview-cai` path exit cleanly.
The retail function map and Ghidra tags record the remaining coverage audits.
HNM4 animated textures, bounded seek, audio-clock scheduling, full corpus
pixel parity and hardware-speaker listening checks are still open. The current
viewer draws the movie image as an ImGui texture within the preview pane; the
shared renderer's general destination seam remains separate work.

## Static image implementation record — 2026-09-27

`inspect/still_preview.cpp` selects one physical source and uses the existing
`SPR_LoadSet`, `TEXT_LoadFont`, `DAN_ReadTextureChunks`/`DAN_Load3DM`, and VFS
ports. The loose `.3DM` branch of `RES_ReadFile` now skips its two original
header words and returns the texture bank. `DSN_LoadTextures` and
`DSN_BlitTileToPage` assemble a selected scene object's 256×256 page from tag
3 and all 64 tag-4 planes in one viewer call. Their map entries remain partial
or unverified where retail progressive scheduling, bank registration and the
assembly tile-dispatch closure are not yet accounted for.

The catalog exposes DAN material and DSN object texture rows. ODViewer shows
the selected sprite, glyph or texture within the preview pane with a 256-color
source palette, nearest sampling, a checkerboard behind transparency, sibling
navigation, and a palette-row slider for 32-row texture banks. PYRAM's live
layer references have distinct diagnostic colors; their final gameplay
composition is still a separate UI behavior. Index-0 transparency for the
viewer-derived VGA sprite family remains an explicit toggle because its
retail draw semantics have not been established.

Seven representative images match the independent Python RGBA results byte
for byte: HI640 glyph 65, SOUR.ALP slot 0, MAGIE.ALP slot 0, ALPHABET.SPR slot
2, ESSAI.3DM, CAISSE's DAN material, and E29USINE's first scene texture.
Another 186 first/last scene and DAN texture selections render across Disc 2;
the malformed HI320 glyph 37 stays an explicit error. All six image types
also pass ODViewer GPU startup smokes. HNM4 animated textures, scene palette
lighting updates, a shared runtime/viewer render target and whole-bank visual
galleries remain separate slices.

## Sound and dialogue implementation record — 2026-09-27

`FSB_Load`/`FSB_GetSample`, `DRD_Open`/`DRD_SelectEntry`/`DRD_GetPortrait`
remain the source-scoped retail entry points. `DSOUND_LoadWav` now validates
their RIFF chunks and supplies PCM to the SDL output service. The known FSB
clip-12 IEEE-float/16-bit header error is interpreted as PCM, as retail's
44-byte-skip playback path does. `DSOUND_PlaySound`, `DSOUND_PlayVoice`,
`DRD_PlayVoice`/`DRD_StopVoice`, and `CD_PlayTrack`/`CD_StopAudio` are portable
viewer adaptations with their remaining runtime behaviors recorded in the
port map. `DRD_LoadEntry` now captures the original 15 Hz WAVE duration, and
`DRD_GetLineDuration` uses the recovered differences between caption starts.
`SPR_LoadPortrait` reads the selected entry's TABLE palette, descriptor and
two-byte indexed texels; the existing still-image GPU path shows it beside
the voice transcript.

The `AudioOutput` service plays FSB and DRD PCM in their source format,
accepts movie SD PCM, and streams raw 44.1 kHz stereo CD-DA from a bounded
INDEX 01 range of the selected CUE track. ODViewer offers play/pause, stop,
restart, progress,
effect waveforms and the selected dialogue's timed text. Replacing or
unmounting a source closes the SDL stream and releases the source snapshot.

The two-disc source remains distinct. A Disc 1 corpus check parses all 24 FSB
clips, 178 DRD voices and 169 portraits. The first effect PCM, first voice
PCM, portrait RGBA, 15 Hz entry duration and distinct non-silent CD track 2
sectors from both discs match the independent Python/backing-file oracle; the known malformed WAVE
and truncated portrait fail bounded checks. SDL dummy-device and native
viewer startup checks cover effect, repaired effect, dialogue and track
selection. Physical-speaker listening, exact DirectSound voice allocation,
positional pan/volume and retail caption fade presentation are not claimed.

This spec does not require a serialized synthetic `.DSN`, ODRuntime gameplay,
or original retail menu flow in ODViewer. Full level and media preview are
Spec 003 slices; `ODRuntime` gameplay milestones remain separate. During
incremental delivery, an unsupported DAN material/face mode or media codec
reports its limitation instead of displaying a different source's asset.

## Render seam, movie clock, seek and HNM4 — 2026-09-28

**Destination seam.** `ModelPreview` now owns an explicit, resizable
color/depth destination (`resize_target`). ODViewer sizes it to the preview
content region in physical pixels and fits its orbit framing to the shorter
side. ODRuntime sizes it to the whole drawable in physical pixels and keeps
the retail 640-wide horizontal focal term, deriving the vertical one from the
target aspect (`with_horizontal_focal`). With retail's default Cinemascope
option, the runtime scene occupies the middle 3/4 between h/8 black bands.
Shell still owns the single swapchain pass, `sg_commit` and present, and
neither mode creates another window. The swapchain has no depth buffer on any
backend, so the Runtime draws through a full-drawable offscreen target rather
than directly into the swapchain pass. Both apps accept `--capture` for
hidden-window checks. Captures of the E29USINE scene pane and of the Project 0
runtime frame confirm the seam.

**Movie clock.** Retail paces sound movies from the 200 Hz timer
(`VID_IsFrameDue`: 14-tick threshold, 13-tick advance) and never reads the
DirectSound play position. `MovieClock` instead follows the played SD sample
count. Every HNM5/6 file's first superchunk preloads 15 SD chunks, and each
later superchunk carries one chunk of 1,470 stereo frames (1/15 s). Frame k is
therefore decoded when the device has played 1470·k samples. The clock falls
back to accumulated 15 Hz host time only while less than one frame of audio
is queued. SDL's resampler history is flushed when the stream ends. Results:

- INTRO.HNM: 2,781 frames in 185.444 s (185.400 s nominal);
- GENERIC.HNM: 6.739–6.742 s per 6.733 s loop.

**Seek.** The viewer's frame slider performs a bounded seek. Retail has no
seek, so this is new viewer UI:

1. Reopen from the start.
2. Decode up to 30 frames per tick without presenting them.
3. Keep the SD audio decoded on the way, since the file leads by one second.
4. Queue audio from the target frame's first sample, then resume.

`--preview-seek N` checks it.

**HNM4.** `VID_DecodeHnm4Frame` (`0x408939`) and its codec (`IZ` LZ key
frames, the 16-mode `IU` inter frames, deinterlace and the `PL` palette parse
at `0x42ed30`) are ported to `shared/port/hnm4.cpp`. Output follows retail: a
256×256 index texture plus a palette of `v<<2` channels. All 20 HNM4 files on
both discs (3,033 frames) match NihAV's indices and palettes frame by frame
(`ODHnm4Tests`). The test also covers truncated, overflowing and malformed
payloads with bounded errors. ODViewer plays them at the silent-video 15 Hz
pump (`--preview-hnm4 E11_EAU.HNM`). Binding frames into a scene's animated
material is the remaining animated-texture step.

## Retail material binding — 2026-09-28

Earlier previews used a face block's name as its texture name, which left 13
DANs and 54 scene material names unmatched. Retail binds in two steps
(`out/dev/research-materials.md`):

- Each tag-1 model carries a 44-byte material directory:
  `name[16] | file[16] | colour | 8 zero bytes`.
- `MDL_LoadMaterials` loads `<file>.3DM` through the DSN, DAN or raw route
  into a **level-scoped, refcounted 256-entry cache**. Name matching is an
  exact `strcmp`, and the first live entry wins; `RES_InitArena` resets the
  cache.
- `MDL_BindFaceMaterials` gives textured blocks a page and flat types
  `-2/1/4/0x11/0x1b` their colour word.

Projects and the runtime load in retail order: shadows, player, shot
models, OBJET1..15, then the OBJET0 scene. Results:

- All 191 DAN and 98 DSN files bind through their own directories, except
  E21_RIDE's `E21ARROW`. In Project 58, E22.DAN supplies it.
- All 150 projects bind with no misses, and the 64 lighting slots follow the
  true cache order.
- Cross-file first-loaded-wins cases are pinned in `ODMaterialBindingCorpus`:
  - P65's `H02ROUT*` use L14.DAN's banks;
  - P34's F15SOUFF faces use F91.DAN's;
  - every `GRILLE` uses OMBRE.3DC's.

Flat blocks, all `DEFAULT` marker boxes and shadow quads, stay bound but
undrawn until actor node hiding (`PHYS_AttachActorCollider`) is ported;
DREAMSFX draws type 1 with its own colour counter. Standalone file previews
seed the cache only from their own file. Cache release and
duplicate-relocation paths remain.

## Palette lighting, fog and animated materials — 2026-09-28

Retail has no vertex lighting for level or actor faces. Types 2/3 use
texture × a fixed iterated white, and the Gouraud types 0x16–0x18 are unused on
the discs. Colour comes from per-material palette rows that are regenerated
every game tick. Research is in `out/dev/research-lighting.md` and
`research-animtex.md`; the port is in `shared/port/palette_lighting.*`,
`random.*`, `fog.*` and `level_materials.*`.

- **Lighting model.** The ported pieces are:
  - Material slots (64-slot cap), registered by `MDL_RegisterLightingMaterial`
    and seeded by `MDL_BuildPaletteColorTable` from DSN row 15 with the project
    contrast.
  - `SCENE_InitPaletteLighting` and the timed base target (`0x42f024`).
  - `REND_TickPaletteLighting`: three Watcom `rand_` calls per tick and a 1/8
    ease that rounds down.
  - `REND_UpdatePaletteRows`: the 3dfx variant with neutral row 15 and the
    `+0xc8` rule; the Windows variant with row 16 is kept for comparison.
  - `REND_ApplyPaletteOffsets`, `MDL_BindActorPalette` and
    `ENT_AdaptActorColor`.
  - The mode 0/1 start biases of ±128.

  A fixed 30 Hz tick drives the model: in ODRuntime, and in the viewer as a
  preview tick.
- **GPU path.** Each material is an 8-bit index texture (the 128 DAN LOD or
  a 256 DSN page), plus one palette texture holding every slot's 32 rows. The
  row is selected per face block, as `GLIDE_DrawObjectFaces` does. The chroma
  key compares against row entry 0's colour. Changed pages and rows upload at
  most once per frame.
- **Fog and gamma (3dfx only).** `SCENE_SetFog` and `GLIDE_SetFog` build
  Glide's 64-entry exponential table from project `+0x1c0..+0x1cc`, keeping
  the unmasked colour packing (Project 66 packs to `0xb5b27c00`). The shader
  applies it by camera-space depth, then gamma 0.8 (`out = in^1.25`). The
  viewer offers fog as a toggle.
- **Animated materials.** Project `+0x4c` names the material and `+0x5c` the
  HNM4 under `data\anim\`; 27 projects animate. The port:
  - opens the file on the scene's disc;
  - decodes at 15 Hz, at most one frame per host frame;
  - copies each frame into the material page;
  - applies `PL` runs over the seeded slot palette;
  - reopens the file at its end, as retail does.

  Project 39's E11_EAU pool and Project 13's F03HNM1 match an independent
  decode (`ODPaletteLightingDisc`).

Adaptations and remaining work:

- Adaptations:
  - fixed-step ticks;
  - per-session random streams;
  - gamma applied before blending;
  - linear interpolation between fog-table entries;
  - no 32-step texture-streaming delay before HNM4 opens.
- Page effects, ported 2026-09-28:
  - the `+0x6c` page scroll (`SCENE_StartPageScroll`, tick `0x403fb9`)
    moves the page up two rows per tick;
  - the `+0x7c` page blend (`SCENE_StartPageBlend`, seed `0x403f3b`, step
    `0x403f68`/`0x403ef0`) averages two rolling copies with retail's 32-bit
    carry, and takes over the HNM4 pixel pointer;
  - the palette-lighting flags `0x4a0f68`/`0x4a0f6c`/`0x4a0f70` gate
    `SCENE_StartAnimTexture` and `SCENE_HasAnimTexture`;
  - `ODPaletteLighting` checks these against a separate byte model;
    `ODPaletteLightingDisc` checks Project36 `M04FEU` (scroll) and Project9
    `E04_NRJ` (blend);
  - in Project51, ESSAI is resident through the `boule.3dc` shot model, so
    the blend overwrites each decoded HNM4 frame before it is drawn, as
    retail does.
- Remaining:
  - effect lights (the `+0xc8` projects and OBJET record `+0x34&8`) and the
    Windows/3dfx lit-row inversion; research notes are in `out/dev/fx/`,
    not ported. The player's light needs the player in the level;
  - the other colour writers (exit transition, hit flashes, transforms,
    triggers, save restore);
  - the underwater driver: set only by `ENT_TickPlayerStatus` from the
    player's height against the water level `+0xd4`, and on 3dfx refogged by
    `0x2c904` on the water-mode change. It needs the player;
  - screenshot parity against a retail capture.

## Caption fades and retail trig tables — 2026-09-28

**Voice captions.** `MENJ_VoiceCaptionsAt` ports the caption timeline of
`MENJ_PlayVoiceCaptions` (`0x436ab6`):

- Every slot starts at fade 20 (hidden); line 0 starts opaque.
- On each change of the 15 Hz counter, the page of four lines is printed
  with its current fades. The current line's fade then halves (10, 5, 2, 1),
  or the next line becomes current once the running sum of
  `DRD_GetLineDuration` is reached.
- `TEXT_BlitGlyphFaded` draws a glyph with coverage `0x40/fade`: at 63 or
  more it is an opaque copy; otherwise `SPR_BlendPixel` mixes in
  `(coverage >> 1)/32` of the glyph colour.

ODViewer shows the retail page with those weights. It drives the counter
from the voice position; retail counts its own 15 Hz flag from
`DRD_PlayVoice`. `ODMenjTests` checks:

- the opaque first line;
- the two-tick delay before a new line appears;
- the 10/5/2/1 sequence;
- paging and the finish tick.

The runtime's portrait, `UI_DimCaptionBand` and Again prompt belong with
in-game dialogue.

**Trig tables.** Retail builds its cos/sin and acos tables at startup
(`0x45b040`, called from `RES_InitArena`): three × 4096 ints in `.bss`,
4096 units per turn. The C++ generator reproduces all 12,288 entries
exactly; this was verified against an instruction-level x87 replay. The
following now use those tables with retail's integer rounding:

- `MATH_QuatSlerp`: no normalisation, no sign flip, table index
  `(dot+0x8000)>>4`, and the near-parallel/opposite branches;
- `MATH_EulerToMat3`: all three axes;
- the linear and spline rotation weights (truncated).

`ODMath` checks the tables, slerp branches and Euler cases against an
independent model. `ODMathRetailExe` checks the generator constants and code
bytes in both Windows executables when the extracted Disc 1 tree is
configured. The XH_ and BA0 animation poses in `ODAnimation` are unchanged.

## Implementation record — 2026-09-27

ODShared now has bounded LZ and DAN/DSN payload readers, a checked host-side
type-1 node/face relocation, the CAI resource/material path, and a partial
`SCENE_LoadLevel` path for both the selected DAN archive and real Project 71
object. ODViewer loads once on selection and draws the crate into a sokol
color/depth target shown in its preview pane. `--cue2 <path> --preview-cai`
selects the Disc 2 `CAISSE` row under `CAI.DAN` for repeatable visual QA. A
local screenshot shows the crate with legible `HAUT` and `BAS` texture marks.

The Python-oracle CRCs match for CAI tag 1/2 and E29USINE tag 1/2; the latter
relocates to 55 nodes and 2,225 faces. Both preview modes load ten CAI faces
and one bound `CAISSE` material; the real Project 71 mode retains its recorded
spawn transform. Windows native tests
pass 12/12 with both original images; Windows, WSL/Linux and Emscripten builds
pass. The port map marks the reached retail functions `partial` or `unverified`
and leaves owner review unchanged. The GPU preview currently supports one
material and face types 2/3 at that checkpoint; it did not claim screenshot parity with Glide.

### Static DAN models — 2026-09-27

The model preview now uses `MATH_MulMat3` and `MATH_MulMat3Vec3` Q15 arithmetic
to compose parent-first node transforms, including zero-vertex connector nodes.
The adapted `GLIDE_DrawObjectFaces` path emits GPU batches with original signed
UVs, per-material palette selection and the 3dfx clamp/wrap/chroma/deferred
ordering rules. `GLIDE_ConvertPalette` expands the RGB565 rows; the modern GPU
upload uses the original 128×128 model LOD sampled from every second texel.
ODViewer retains its source-scoped load path and uses separate clamp, wrap and
alpha GPU state in the preview target. `--preview-model F74` checks the keyed
face mode; `--preview-model BA0` checks multiple materials; `--preview-cai`
remains the crate regression case.

The two original disc images contain 191 physical DAN files. The bounded C++
loader and draw preparation accept 178, covering 46,091 physical faces. The
other 13 instances have face/material names absent from their own DAN texture
directory; ODViewer reports the exact missing name and does not substitute a
texture. The 56-byte type-1 diagnostic blocks are validated but not submitted
in this slice. The only DAN using type 9, `L14.DAN`, is among the 13 unmatched
material cases, so its wrap mode has synthetic coverage but no disc GPU QA yet.
Exact lighting, fog, diagnostic faces and retail screenshot parity remain open.

### Static scenes and project placements — 2026-09-27

`SCENE_InitLevel` now loads a physical DSN or a project's `OBJET0` through the
ported tag-1/2 readers. `DSN_LoadTextures` consumes tag 3 and all 64 tag-4
planes once for every named object. The adapted `DSN_Create3DM` copies each
20-byte source template, writes its palette into row 15 and assembles its
256×256 page. The shared GPU path binds scene pages at 256×256 while DAN
materials retain their 128×128 Glide LOD. Physical scene and project rows draw
inside ODViewer's preview pane with orbit, zoom and target controls.

All 95 distinct DSNs load and submit 157,433 tag-1 faces with 2,059 source
texture pages, matching the independent Python graph count. Fourteen DSNs also
reference 54 full material names absent from their own texture directories.
Those faces use a visible magenta diagnostic pattern; the viewer lists every
missing name. No alternate source texture is silently assigned.

Across all 150 projects, the preview loads 531 of 560 active `OBJET1..15`
placements with their stored position and heading. This includes 15 loose or
DAN-backed logical `.3DC` placements and four actors fetched from the other
mounted disc when the primary lacks their DAN. The viewer labels secondary
sources and lists the remaining 29 failures by slot; all 29 are DAN archives
whose face material names do not match their own texture directory. Project 71
draws `E29USINE.DSN`, `MI0.DAN` and `CAI.DAN` together. Corpus and GPU startup
checks cover E29USINE, rare deferred/keyed scene modes and Project 114's
cross-disc object. Dynamic palette updates, scene fog, exact deferred/depth
behavior, animated materials and screenshot parity remain open.

### DAN rig animation — 2026-09-27

The selected DAN model keeps its source-scoped archive open for clip selection.
`DAN_Load3DA` and the `.3DA` branch of `RES_ReadFile` choose and decompress a
tag-3 clip. The adapted `ANIM_RelocLinearTracks` and
`ANIM_RelocSplineTracks` turn its original track/key pointers into bounded host
records. The evaluator uses the original node-directory order, including
zero-vertex connectors; it applies stored linear keys or spline controls and
Hermite tangents through the ported `ANIM_EvalTrack*`, `ANIM_ApplyModel*`,
`MATH_QuatToMatrix`, `MATH_QuatSlerp` and `ANIM_ApplyEase` cuts. Sokol updates
only the vertex buffer after the first pose, retaining the source materials
and a stable preview camera frame.

ODViewer exposes clip selection, play/pause, one-frame step, restart, scrub,
loop and in-place/follow-root display at the recovered 30 Hz base. All 191
physical DAN archives yield 1,048 clips, 21,050 tracks, 197,102 rotation keys
and 110,418 translation keys, exactly matching the independent Python decoder.
All 1,048 clips evaluate at start, midpoint and end. Nine clips in `F03.DAN`
have 16 tracks for 14 model nodes; `ITOAN050.3DA` has 12 tracks for 11 nodes
on each disc, making 11 physical extra-track cases. Ghidra's
`ANIM_ApplyModelLinear` and `ANIM_ApplyModelSpline` loops take their bound from
the model node directory and consume that many clip entries. The native port
now follows that retail contract and labels unused trailing tracks in the
viewer. Many F03/ITO prefix translations differ from their model bind pose,
so the original asset-authoring reason remains unverified; no replacement
bone names or remapping are inferred.

Stored and interpolated XH_ spline poses and BA0 linear poses have focused
Python comparisons; finite-frame GPU playback smokes cover both families.

The existing 13 DAN archives with unmatched material names still cannot open
the visual model preview, even though their animation clips parse. Viewer
root-motion retargeting is an inspection policy. Runtime action selection,
transition blending, fixed lookup-table quaternion precision, clip-specific
one-shot behavior, physics consumption and screenshot parity remain open.

### Face facing and bone inspection — 2026-09-27

Ghidra's `REND_CullFaces` rejects back-facing triangles before the Glide object
hook. Faces with source flag 8 recompute facing from their posed corners; all
225 of XH_'s bridge faces have that flag. The native preview now enables
counter-clockwise GPU back-face culling after converting retail Y-down
coordinates. An independent scan of 254 distinct DAN/DSN render graphs found
173,341 nondegenerate stored-normal faces aligned with source triangle winding,
25,103 dynamic-facing faces and no reversed stored normals. XH_AN006 at frame
9.5 was compared with culling on/off at the same viewer camera. The extra rear
joint faces disappear with culling.

ODViewer's optional **Bones** overlay projects the same Q15 parent-composed
node origins as the mesh. It draws links and selectable joints, shows local and
world coordinates, and can label names. Nonrendering helper nodes are hidden by
default and exposed by **Helpers**; XH_'s distant `ZZZZZ` node has no rendered
faces. The remaining very dark shorts/waist pixels in this frame correspond to
type-2 source faces whose original `XH_IMG_B` palette and UVs include black or
near-black texels. Their exact appearance in a running retail frame has not
been compared, so the port does not replace those authored colors or move joints
to conceal them.
