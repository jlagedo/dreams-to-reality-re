# Shared animation subsystem

The viewer now uses one animation runtime for Duncan, scene NPCs, and model
inspection. Rendering and playback no longer depend on Duncan's hard-coded
27-node geometry order. Each instance has an independent clock, pose, vertex
buffers and optional bone overlay; immutable asset requests are shared.

## Export and run

```powershell
uv run dreams export-animations
# Optional destinations; models default to the animations directory's sibling models/:
uv run dreams export-animations --out E:/dreams-work/animations --models-out E:/dreams-work/models
npm --prefix web test
npm --prefix web run build
```

The command reads both configured discs, generates matching glTF models and
skin bindings, and writes `catalog.json`, `manifest.json`, individual clips,
`*_skin.json`, and `export-report.json`. Local generated content stays outside
Git. Run this command after decoder or export schema changes; the earlier
scratch-only export scripts produce incompatible data.

The corpus currently exports **159 rigs, 780 clips, 770 with verified directory
bindings**. The other 10 (`F03` and `ITO`) retain their data and a binding error;
the player refuses them instead of guessing a mapping. The 1,967 rig nodes
include all 81 nodes omitted by the geometry scanner. Every glTF primitive's
name and vertex count was checked against its exported bindings.

## Components

| File | Responsibility |
|---|---|
| `src/dreams/animation_export.py` | Complete rig, vertex bindings, library catalog, clip compatibility metadata, paired model export. |
| `web/src/animation/types.ts` | Schema 2 rig and clip contracts. Indices always refer to original directory slots. |
| `web/src/animation/controller.ts` | Clip validation, binary key lookup, SLERP sampling, independent clocks, loop/one-shot, seek/pause/speed, crossfade, playback snapshots, hierarchy evaluation. No Babylon dependency. |
| `web/src/animation/renderer.ts` | CPU mesh deformation, normals/bounds updates, bone visualization, disposal. |
| `web/src/animation/library.ts` | Cached catalog/rig/clip loading and animator creation for imported models. Failed requests can be retried. |
| `web/src/player.ts` | Duncan movement and provisional action selection; delegates animation to the shared runtime. |
| `web/src/viewer.ts` | Per-NPC instances, scene lifecycle, model previews, inspector target ownership. |

Rig parent indices need not precede their children. A validated traversal order
evaluates helper nodes before descendants. Missing rotation channels retain
bind matrices. Q15 products use division and floor rather than JavaScript's
32-bit bit shifts, which overflow on larger source coordinates.

Clips carry a model ID, rig fingerprint and explicit binding status. A foreign
rig, unresolved binding, malformed track, or vertex-count/group mismatch is
rejected before applying the pose. The fingerprint is an export compatibility
identifier, not a claim that all animation decoding is fully recovered.

## Inspector behavior

Animation & Bones lists all exported models. It targets the selected matching
scene entity, Duncan when active, an existing model preview, or a matching
scene NPC. If none exists, it loads that model for inspection. Play, pause,
scrub, speed and the named quaternion table now drive the visible target.

Inspection temporarily takes control of one actor. Closing the inspector
restores that actor's prior playback; other actors continue with independent
clocks. Scene changes dispose animators before their geometry. Pending loads
check the current selection/generation so obsolete loads cannot attach actors
to a new scene. Unsupported clips disable playback and show the reason.

Scene NPCs automatically preview `AN000` when available, otherwise the first
clip with a verified binding. **This is a preview policy, not recovered NPC AI
or a verified idle/action mapping.** Duncan retains its provisional action
assignments. Base speed is the recovered 30 frames/second.

## Verified and unfinished behavior

Python tests check complete helper-node export, unresolved bindings and paired
geometry contracts. TypeScript tests use the real runtime and Babylon's null
engine to check helper deformation, nonidentity bind matrices, large-coordinate
arithmetic, independent clocks, seek/restore, looping, crossfades, refusal of
invalid clips, actual vertex/bounds updates and disposal. Browser checks cover
Duncan, CH0, MHE, and a live `F07BLEU` scene instance.

The runtime currently animates **rotations**. Translation channels/root motion,
the original 60-byte-key spline/easing evaluator, actor speed effects and
gameplay state transitions remain separate work. Plain SLERP is still an
approximation between those stored keys. No joint-limit clamping is applied.
This is the shared playback foundation for that further work.
