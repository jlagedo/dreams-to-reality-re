# 003 — ODViewer asset previews and playback

Status: **CAI model, HNM5/HNM6 movie, and static image slices implemented; expanded viewer scope in progress**

Date: 2026-09-27

Depends on: [002 — retail asset access and game asset navigation](../002-disc-navigation/spec.md)

## Goal

ODViewer previews game assets directly from a selected original disc source:
models and project objects, scene geometry and materials, sprites/textures,
animated textures, movies, sound and dialogue. All previews occupy the
viewer's preview content region; playback controls and errors remain in that
pane. ODShared owns retail-derived loading, decoding and rendering state, so
ODViewer remains a selector and inspection UI. The selected row's physical
path and disc identity choose the source; equal names on different discs or
inside different archives cannot silently substitute for it.

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
| DAN model and project-object preview | Load the selected archive/object through shared resource and scene paths; render its materials and face modes. | CAI/Project 71 proof implemented; the current preview handles one material and face types 2/3. |
| Model animation | Select and play the bound DAN animation clips on the preview rig at the recovered 30 Hz rate, with pause/step and the selected source retained. | DAN chunk readers exist; rig evaluation and visual playback remain. |
| Scene and level preview | Render selected `.DSN`/project geometry and placed assets with the common renderer and a viewer camera; keep source identity. | Payload and Project 71 validation exist; general visual preview remains. |
| Sprites, fonts and static textures | Display indexed pixels, palette rows and transparency from shared decoded data. | Selected sprite slots, font glyphs, VGA sheets, standalone/DAN material banks and assembled DSN object textures render in ODViewer with a palette swatch and transparency control. Animated materials and exact dynamic sprite composition remain. |
| Animated textures | Decode and show HNM4/HNS4 frame sequences; reuse the GPU material update path when a scene binds them. | Header classification exists; frame decode remains. |
| Movies | Decode HNM5 (`UBB2`/`UBS2`) and HNM6 (`HNM6`/`HNS6`) from the selected disc; play, pause, step, restart and show captions. | HNM5/HNM6 playback, SD audio and ST captions are wired in ODViewer; all 95 physical HNM5/6 files decode to the end. Seek, audio-clock scheduling, full corpus pixel parity and HNM4 remain. |
| Sound and dialogue | Play the selected supported sample or dialogue with shared audio output; movie `SD` sound stays synchronized with video. | Movie SD audio uses SDL3; standalone sample/dialogue playback and audio-clock scheduling remain. |

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
NihAV and the existing extracted files are **validation oracles**, not
runtime dependencies. No game video belongs in the repository or app binary.

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

- **Models and animation:** expand the current one-material type-2/3 preview
  to the reached 3dfx face modes, P8 palette rows, clamp/wrap, chroma key,
  lighting and deferred translucent faces. Keep original face/UV data and
  model/archive identity. Bind DAN clips to the attached rig without
  embedding a gameplay loop in ODViewer.
- **Scenes/projects:** select a physical `.DSN` or project record, load through
  the same bounded scene/resource path, and show its geometry, materials and
  placed actors under a viewer camera. Original spawn and transforms remain
  available for checks; unsupported records do not silently disappear.
- **Sprites, fonts and textures:** expand the existing shared sprite/font and
  material readers into an indexed-pixel preview with the selected palette,
  transparency and source metadata. HNM4 frames can feed the same texture
  upload path when the material is animated.
- **Audio and dialogue:** selected supported FSB samples, voice/dialogue and
  disc audio tracks play through an SDL3 service with stop/pause and source
  lifetime. Dialogue timed text/portrait remains tied to its selected entry.

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
2. **Model/scene GPU path:** extend the existing CAI preview from one material
   and face types 2/3 through the face, palette, lighting and transparency
   modes reached by selected models, then use the same renderer for selected
   DSN/project scenes. Preserve the current CAI and Project 71 checks at
   every step.
3. **Static and animated assets:** add sprite/font/static-texture display,
   HNM4 frame decoding, DAN rig animation and the selected-source controls
   those previews need. Share material/texture upload code with the model
   and scene renderer.
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
RGBA8 sokol image per Shell frame, and queues 22050 Hz stereo PCM through SDL3.
ODViewer selects the physical row and disc, offers play/pause/step/restart,
shows `ST` text within the pane and closes source, audio and GPU state on a
selection or mount change. `--cue2 <path> --preview-movie` selects Disc 2's
`GENERIC.HNM` for repeatable startup checks.

The first four decoded `GENERIC.HNM` HNS6 and `HNMFR2.UBB` UBS2 frames match
the independent NihAV RGB output exactly after RGB565 quantization; the first
SD PCM batches match the Python oracle byte for byte. Both files also decode
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

This spec does not require a serialized synthetic `.DSN`, ODRuntime gameplay,
or original retail menu flow in ODViewer. Full level and media preview are
Spec 003 slices; `ODRuntime` gameplay milestones remain separate. During
incremental delivery, an unsupported DAN material/face mode or media codec
reports its limitation instead of displaying a different source's asset.

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
material and face types 2/3; it does not claim screenshot parity with Glide.
