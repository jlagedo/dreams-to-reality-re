# Animation hypotheses and validation

Run from the repository root:

```powershell
uv run python -m dreams.animation_harness
uv run pytest tests/test_animation_harness.py tests/test_animation.py
```

The harness reads the configured Disc 1 `XH_.DAN` directly and writes
`out/animation-harness/report.json` and `report.md`. Pass a model path and
`--out <directory>` to choose other inputs/outputs. The humanoid joint names
and bend directions currently target Duncan; another model can have no
matching limits. Reports retain the source SHA-256, named hierarchy, exact
thresholds, raw quaternion norms, per-joint maxima, and worst clip/frame
examples. Game assets remain outside version control.

## Evidence order

1. **Explicit directory and executable behavior.** Read actual node bindings
   before trying to infer them from poses.
2. **Independent translation fingerprints.** Compare the first stored local
   translation in each channel with the corresponding named node's rest
   translation. Exclude roots because root motion is legitimate. A match is
   within one source unit; mismatches are retained, not discarded.
3. **Humanoid sanity checks.** Measure signed elbow/knee bend and departure
   from a candidate bending plane at every stored rotation key. These are
   loose diagnostic envelopes, not medical limits. They do not clamp poses.

The envelopes are elbows −15° to 165°, knees −15° to 170°, and up to 35°
outside each candidate bend plane. The child offset defines the rest limb
direction. Forward +X for elbows and backward −X for knees are explicit
anatomical hypotheses in source coordinates. Tests include synthetic forward,
backward, and sideways bends so a broken scorer cannot simply accept all poses.

## Named directory [verified]

Tag 1 has a node count at `+0x14` and a table at `+0x18`, with offsets to
serialized node headers. Each node's original name is in the 12-byte field
at header `+0x14` (runtime node `+0x00`). Its hierarchy starts at header
`+0x24`. The animation evaluator at `WINDREAM.EXE:00459d60` walks the model
and clip slot tables together. Physical record order is not slot order.

Examples from Duncan:

| Track slot | Geometry scan index | Original node |
|---:|---:|---|
| 0 | 0 | `bassin` |
| 1 | 2 | `ZZZZZ` |
| 2 | 10 | `avbras-d` |
| 3 | 16 | `avbras-g` |
| 4 | 1 | `bassin01` |
| 15 | 22 | `mollet-d` |
| 16 | 25 | `mollet-g` |
| 17–20 | 5–8 | `nat01`–`nat04` |
| 25 | 4 | `tete` |
| 26 | 3 | `torse` |

The directory resolves in all 159 distinct `.DAN` models: 1,967 slots versus
1,886 geometry nodes. The other 81 slots have no geometry recognized by the
vertex scanner. For example `CH0`'s eighteenth slot is zero-vertex `bassin01`.
Not every clip count equals its model count (`F03` and `ITO` need separate
analysis); the exporter leaves such bindings unresolved instead of guessing.

JSON retains `nodeIndex` as the original track slot, adds `boneName`, and adds
`meshNodeIndex` for the existing geometry bindings. `-1` means the named slot
has no scanned geometry; null means the mapping is unresolved. The Duncan
player uses `meshNodeIndex`; the monitor uses original names and track slots.

## Duncan results, 49 clips [measured]

| Mapping | Child translation matches | Mean position error |
|---|---:|---:|
| Explicit directory | 1,258 / 1,274 (98.74%) | 0.1908 units |
| Physical geometry order, previous viewer behavior | 0 / 1,274 | 65.7439 units |
| Alphabetical names | 1,116 / 1,274 (87.60%) | 24.4558 units |

Alphabetical sorting almost works because much of the stored directory is
alphabetical, but it fails around the special root/helper slots. The explicit
directory provides the binding without inventing an ordering rule.

| Rotation hypothesis | Keys outside elbow/knee envelope |
|---|---:|
| Directory, XYZW, local | 65 / 2,950 (2.20%) |
| Previous physical order, XYZW, local | 811 / 2,749 (29.50%) |
| Directory, conjugated rotations, local | 1,951 / 2,950 (66.14%) |
| Directory, WXYZ, local | 2,160 / 2,950 (73.22%) |
| Directory, XYZW interpreted as world rotation | 694 / 2,950 (23.53%) |

Counts differ because each mapping sends different stored tracks, with
different key densities, to the measured joints. This is diagnostic evidence,
not a statistical confidence estimate. The directory and translation
fingerprint establish the mapping independently of these limits.

With directory/XYZW/local, both knees have zero violations across 2,122 stored
keys. Measured bends range −11.72° to 148.01° on the right and −7.12° to 148.09°
on the left. The 65 remaining flags are elbows, including `XH_AN019` frame 9
and `XH_AN054` frame 9. These could reflect authored poses or limitations of
the assumed bending plane; a flag alone does not identify a decoder bug.

All 12,949 Duncan rotation keys have **raw**, unnormalized quaternion norms
between 0.9999501 and 1.0000004 after division by 32768. Normalization alone
would conceal malformed data, so this check precedes plausibility testing.

## Further corrections and open tests

- The alleged FPS field was the root rotation-key count. The subsequent
  [timing trace](animation-timing.md) recovered a 30 frames/second engine
  base and replaced the temporary 10 fps preview rate. Actor/state modifiers
  remain separate from the raw clip rate.
- Track `+0x1c` is translation-key count. `+0x24` points to translations,
  which follow rotations. Both pointers are relative to payload `+0x14`.
  Engine evaluators `00459808` and `0045a03c` use rotation/translation strides
  20/16 and 60/48 respectively. All 49 Duncan clips pass the harness's
  translation bounds and timestamps checks. Several other models have keys
  outside the declared duration; the strict harness reports those cases.
- The viewer still uses static node translations and SLERP between rotation
  keys. The original 60-byte-key evaluator also uses control quaternions and
  easing; matching stored keys does not validate interpolation between them.
- These hinge checks do not measure axial twist, shoulders, hips, torso,
  fingers, braid collisions, or consistency with original-game footage.
- All Duncan bind matrices are identity, so this asset cannot distinguish
  replacement from multiplication by bind rotation. A consistent global
  reflection also preserves anatomical angles; use the face, braid, and
  renderer transforms to check facing.

The next discriminating tests are the flagged elbow frames against original
game poses, translation playback, and the original spline evaluator. Adding
more guessed joint limits alone cannot settle those questions.
