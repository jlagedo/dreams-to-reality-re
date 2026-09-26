# Reverse-engineering status

Current synthesis, checked 2026-09-26. [north-star.md](north-star.md) owns the
engine's decisions; this page tracks evidence and remaining work. Detailed
layouts and algorithms live in the linked subsystem pages. Dated entries in
[research-log.md](research-log.md) retain earlier findings and rejected readings.

## What is established

| Area | Recovered and checked | Still to establish or validate |
|---|---|---|
| Scenes | 150 projects reference **95 unique scene names**. All **95/95** now export through the original tag-1 render graph; all 472,299 checked face pointers resolve. | Specific animated mover identities and poses still need gameplay validation. Collision-position voting is diagnostic and no longer discards valid visual geometry. [scene-placement.md](scene-placement.md) |
| Models and animation | Models, skeletons, texture pages, clips, linear/spline evaluation, root-motion flow and common transition timing are recovered. | Exact action flags, integer/weight rounding, special roots and pose comparisons. Floating-point viewer playback is not proof of simulation parity. [models.md](models.md), [animation-root-blending.md](animation-root-blending.md) |
| Camera and renderer boundary | Projection, follow presets, camera modes and collision are traced. Horizontal FOV is 76.36 degrees. Simulation must preserve the one-tick-old root camera-space position used by sound and line-of-sight checks. | Remaining orbit expressions and trigger payloads; projection comparison with the future GPU path. [engine.md](engine.md#camera-and-projection-verified) |
| Physics | Collision data, sweep-and-prune, sphere/triangle tests, wall slide, floor finding, entity separation, platforms and integration are documented. | Port and compare behavior, including remaining constants and edge cases. The segment-query iterator is named, but its full behavioral specification is not written. [engine.md](engine.md#collision-and-physics-verified) |
| Timing | Gameplay delta is clamped to 0.2–5.0; recording forces **1.0**. Integration and damping depend on frame count. OpenDreams uses **30 steps/s with delta 1.0**. | Original demo playback timing and recorded-input comparisons. [engine.md](engine.md#the-fixed-step-verified) |
| Gameplay | Player controllers, AI scheduling/steering, action events, weapons, inventory, damage, exits, visited-level state and mana particles have substantial code traces. | Complete trigger condition/action tables, spell/effect identities, actor flags and special cases; corpus and original-game checks across all projects. [ai-animation-runtime.md](ai-animation-runtime.md), [name registry](../re/names/WINDREAM.EXE.tsv) |
| Appearance and media | Glide face dispatch, samplers, deferred alpha/depth, palette cache, fog branches and palette generation are documented. Signed UVs and face types survive the Python/glTF path. | Original-game visual comparisons, zero-density fog behavior, some scene/video descriptors and unused type-2 light details. Native renderer and media implementation remain. [glide-renderer.md](glide-renderer.md), [lighting.md](lighting.md) |
| Saves and language | Retail save index, world/player/inventory payload and thumbnail layouts are traced. The English executable skips its INI parser and uses compiled-in English strings. | Save/load round trips and native integration. French INI labels are research metadata, not the English runtime's text source. [game-content.md](game-content.md) |

A decoded format, a named function, a documented algorithm and behavior
reproduced in a running port are separate claims. There is currently no
`opendreams/` implementation; the Python toolkit and Babylon viewer are the
existing test and inspection tools.

## Naming coverage

The registries contain **691 WINDREAM names** and **278 DREAMSFX names** as of
this check. `tools/check_names.py` passes all their stored assertions against
the local function-feature exports. WINDREAM includes 71 `PHYS_*`, 85 `ENT_*`,
29 `AI_*`, 28 `CAM_*` and 22 `ANIM_*` entries.

The builds overlap and their registries include platform and library code.
These totals are not a percentage of the game ported. The earlier “43% of the
game's own code” estimate has no maintained denominator here and is retired as
a progress measure. Software rasterization and obsolete platform plumbing do
not need a full port or naming sweep.

## Remaining work, in dependency order

1. **Native asset browser and renderer (milestones 2–3).** Implement the
   recovered render graph, signed UVs, material/state and palette contracts
   against the Python oracle. ARC's open edges are source topology. Compare
   original-game screenshots and settle the bounded runtime questions in
   [glide-renderer.md](glide-renderer.md) and [lighting.md](lighting.md).
2. **First playable level (milestone 4).** Port the recovered player,
   animation/root-motion, physics, collision and camera sequence at delta 1.0.
   Trace unresolved arithmetic, flags and helper calls reached by that slice.
   Capture original-game movement, fall, attack and camera traces alongside
   implementation, including the renderer-derived position dependency.
3. **Progression (milestones 5–6).** Complete trigger and effect semantics,
   special actors, spawning and persistence. Check the executable's tables
   against all 150 project records, then validate playthroughs and save/load
   round trips. A connected level graph alone does not prove playability.
4. **Broader fidelity validation.** Extend input traces, pose comparisons and
   faithful 640x480 Glide screenshots as each system lands. Format checks and
   mathematical models do not substitute for original-game behavior traces.

Web delivery remains parked until the desktop game plays. The platform
skeleton and compile checks can start now; they do not depend on finishing RE.

## Field corrections to carry into the port

| Storage | Current meaning | Evidence |
|---|---|---|
| Project `+0x18`, `+0x24` | Signed palette RGB base and variation; three shared RNG draws per update | [lighting.md](lighting.md) |
| Project `+0x9c`, `+0xa0`, `+0xa4` | Player movement-mode selector, movement scale, turn step | `ENT_LoadObject`, `ANIM_RequestState`; [engine.md](engine.md#camera-and-projection-verified) |
| `OBJET +0x6c` → actor `+0x108` | Turn step, not a `BOX` route index | [ai-animation-runtime.md](ai-animation-runtime.md#objet-fields-verified-runtime-use-and-limits) |
| `OBJET +0x70` → actor `+0x10c` | Walker collision-sphere radius; also an attack placement offset | `PHYS_AttachActorCollider`, `ENT_PlaceAtFacingOffset` |
| `OBJET` flag `0x4000` | Actor followed by the mana-particle emitter | `PART_SetEmitterActor`; [research-log.md](research-log.md) |
| Node `+0xc4`, `+0xc8`, `+0xd0` | Light count, light indices, unlit shade | [scene-geometry.md](scene-geometry.md#the-records-from-the-code-that-reads-them) |

Python accessors `camera_fov`, `route_index` and `health`, and baked project
key `fov`, retain legacy names. Their meanings above are authoritative;
`fov` currently serializes the player's turn step. Offset keys such as `x6c`
can also hold now-understood fields. [pipeline.md](pipeline.md) records this
existing viewer contract; new native code must use the recovered semantics.

The extractor's `tiles` group still writes the old `.3DM` RGB555 byte-view
experiment. Its metadata now marks that interpretation obsolete; decoded
texture art comes from `dreams.formats.node.read_3dm`. Scene-header word names
are also legacy labels: their friction/compass interpretations remain unverified.

## Reproduce the checks

From the repository root, with local feature exports and discs configured:

```powershell
uv run python tools/check_names.py WINDREAM.EXE
uv run python tools/check_names.py DREAMSFX.EXE
uv run pytest -o addopts=-q tests/test_fixed_step.py tests/test_formats.py::test_every_face_vertex_pointer_lands_on_a_vertex_record tests/test_formats.py::test_scene_route_counts tests/test_formats.py::test_collision_triangles_carry_planes_edges_and_boxes
```

The corpus checks validate pointers, 95/95 render routes and collision records.
The fixed-step tests model recovered integration and damage formulas; they do
not execute the original game or a native simulation.
