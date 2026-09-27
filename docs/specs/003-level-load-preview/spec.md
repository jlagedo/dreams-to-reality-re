# 003 — ODViewer level-load object preview

Status: **Implementation under validation**

Date: 2026-09-27

Depends on: [002 — retail asset access and game asset navigation](../002-disc-navigation/spec.md)

## Goal

Selecting a **Model name** inside a `.DAN` archive in ODViewer builds an
in-memory preview project and empty scene, runs an adapted C++
`SCENE_LoadLevel` path, and displays the archive's model in the preview pane.
The selected row's physical path and disc identity choose the source; equal
model names in other archives cannot silently substitute for it. The original
executables are research references; ODViewer invokes the shared C++ port.

The first proof is the **`CAISSE` Model name row inside Disc 2's
`DATA/3DC/CAI.DAN`**. A 0x2200-byte preview record contains `OBJET0` as an
empty viewer scene and active `OBJET1` as `CAI.DAN`; the adapted scene
initializer creates the camera root in memory. The loader then attaches the
crate's ten-face model. The separate real **Project 71 / `OBJET2`** path loads
`E29USINE.DSN` and validates the same model against its retail spawn record.
That checked chain is in [the CAISSE trace](../../reviews/cai-prop-retail-chain.md).

## Viewer flow

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

## Renderer boundary

The loader produces the same model node graph consumed by the retail scene
walk. The first preview reads that graph and the selected object's bound
materials, then draws its stored triangles with a viewer camera. The DOS 3dfx
build supplies the face-type, texture, palette, depth and transparency rules.
The preview does not port `SW_DrawObjectFaces`, `SW_FlushSpans` or the Windows
framebuffer path. It also does not require a gameplay tick, retail camera
behavior or a full port of `REND_DrawScene`/`REND_DrawObject` just to show CAI.

This is an initial visual slice of the final model-space renderer described in
[the north star](../../north-star.md#the-renderer-is-free). A later projection
test and original 3dfx screenshots establish fidelity beyond this first image.

## Acceptance

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
- Selecting another row or replacing/unmounting the source disc releases the
  old scene and GPU resources; an unavailable or unsupported selection reports
  a useful error without displaying stale content.
- Focused loader tests compare the reached project, model and scene data with
  the Python reference. Disc-dependent tests skip when the user has not
  configured the original images. Native builds and the Emscripten compile
  check continue to pass; browser disc-image opening remains deferred by 002.

## Port accounting and limits

For every retail function implemented in this slice, update
`opendreams/port-map.tsv` with the C++ change, run
`uv run python tools/check_port_map.py`, and apply the map to both Windows
programs in Ghidra under [the port-map policy](../../../opendreams/PORT_MAP.md).
Use `coverage=partial` wherever a retail branch, side effect or callee remains
deferred, and list it in `remaining_work`. The `reviewed` field remains `no`
until the project owner explicitly reviews that entry.

This spec does not require loose `.3DC` previews, a serialized synthetic
`.DSN`, animation playback, full-level rendering, gameplay, audio, or parity
across all 95 scenes. Unsupported DAN material/face modes report an error
instead of displaying a different archive's model.

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
