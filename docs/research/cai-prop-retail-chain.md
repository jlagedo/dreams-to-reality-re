# CAISSE crate: retail scene load to draw

This is a concrete load-to-render trace for a prop placed by a project record,
not merely a standalone `.DAN` preview. The example is Project 71 on Disc 2:

| Source | Value |
|---|---|
| `DREAMS.DAT` project | `Project71` |
| `OBJET0` scene | `E29USINE.DSN` |
| `OBJET2` asset | `CAI.DAN` |
| `OBJET2` flags | `0x0001`: active on level entry; character bit `0x0002` clear |
| `OBJET2` position | `(7312, 3187, 124)` in engine units |
| `OBJET2` heading | `2226` on the 0..4095 angle circle |
| `CAI.DAN` internal name | `CAISSE` (crate), one named animation `CAIAN000.3DA` |

`E29USINE.DSN` and `CAI.DAN` are both on Disc 2. There is no `CAI.DSN`, so
the DAN branch described below applies. The two discs' `CAI.DAN` files have
the same SHA-256. The independently validated Python model decoder exports one
textured part with 10 faces from this file; that is a visual oracle, not the
ODViewer load path.

## Retail Windows function chain

`SCENE_LoadLevel` takes no explicit project argument in the checked call sites.
It tests the pending-load global at `0x00661e08` for `1` and reads the current
0x2200-byte project record through the pointer at `0x00661e04`. One caller at
`0x0044cb5f` sets that pointer to the working record at `0x0065fb04`, sets the
pending flag, then calls `SCENE_LoadLevel` at `0x0044cb73`. Feeding it a
synthetic project therefore means preparing its expected shared state, not
passing a record as a function parameter.

1. The selected `DREAMS.DAT` record is the working project when
   `SCENE_LoadLevel` (`0x0041f9db`) runs. Its loop checks `OBJET1` through
   `OBJET15` for the active bit and calls `ENT_InstantiateFromObjet`
   (`0x0041deb8`) on Project 71's `OBJET2`.
2. `ENT_InstantiateFromObjet` allocates/initializes an actor and calls
   `ENT_LoadModel` (`0x0041da7d`) for a fresh model. It also copies the
   record's position and heading to the actor. A same-name clone path exists;
   Project 71 has no earlier `CAI.DAN` slot, so the fresh-load path is expected.
3. `ENT_LoadModel` derives a logical `CAI.3DC` resource name from `CAI.DAN`.
   It probes for `CAI.DSN` first, then opens the existing `CAI.DAN` with
   `DAN_OpenArchive` (`0x0040fff7`). `RES_Load` (`0x00456e24`) calls
   `RES_ReadFile` (`0x0041c666`), which reaches `DAN_Read3DC`
   (`0x0041020f`) and `LZ_Unpack` (`0x0049afd1`) for the embedded model data.
4. `RES_Relocate` (`0x00456368`) turns file offsets into the model's node,
   primitive and material pointers. `MDL_LoadMaterials` (`0x00456038`) requests
   the `.3DM` texture resource, resolved from the open DAN archive through
   `DAN_Load3DM` (`0x0041053e`). `ENT_LoadModel` attaches the model root to
   the scene graph with `MDL_AttachNode` (`0x00457328`). It also calls
   `DAN_ReadAnimChunks`; `OBJET2`'s clear character bit means it does not
   enter the `ANIM_LoadEntitySet` branch.
5. Later in `SCENE_LoadLevel`, each active actor is passed to
   `ENT_ResetToSpawn` (`0x0041ec38`). This reads its `OBJET` position and
   heading, calls `MATH_EulerToMat3` (`0x0045b194`), then applies the matrix
   and position to its model handle with `MDL_SetNodeRotation`
   (`0x00457a38`) and `MDL_SetNodePosition` (`0x00457aa0`). This is the
   checked link from project placement to the render node.
6. A Windows frame reaches `GAME_DrawFrame` (`0x00423f60`) →
   `REND_DrawFrame` (`0x00459320`) → `REND_DrawScene` (`0x0047e700`) →
   `REND_DrawObject` (`0x0047e498`). The scene walk finds the attached model;
   the object step composes node transforms, culls, lights and projects its
   faces. `SW_DrawObjectFaces` (`0x00473014`) submits visible faces to the
   software rasterizers. The selected flush (`SW_FlushSpans` at `0x004768cc`
   or its scaled variant) writes spans to the framebuffer. The DOS Glide
   build uses a different face hook.

## Viewer boundary

The map's `OBJET2` record supplies source identity and placement. A viewer
inspection loader can request `CAI.DAN`, decode its model and materials through
shared format code, and return a stable model snapshot plus that placement.
ODViewer can draw the snapshot with its own GPU renderer. Actor allocation,
physics, animation startup and the retail software rasterizer are not needed
to display this placed static crate.

This chain is checked from the Project 71 record, the two physical disc files,
Ghidra decompilation of the named functions above, and the model reference
decoder. The C++ port currently covers disc/VFS access and DAN directory
metadata; model payload decode, relocation/material binding and mesh drawing
remain implementation work.
