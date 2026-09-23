# Translation curves, root movement, and blending

The shared player now samples translation channels as well as rotation, blends
both between clips, exposes root motion deltas, and implements the timing and
held-pose behavior of the engine's common transition path. This is a floating
point reconstruction; game action selection and collision/physics integration
are still separate work.

## Translation records [verified]

The track header's translation count is at `+0x1c` and array pointer at `+0x24`.
Add 20 bytes to that pointer to locate the array in the decompressed payload.
Linear rotation records (20 bytes) accompany 16-byte translation records;
spline rotation records (60 bytes) accompany 48-byte translation records.

| Offset in translation key | Meaning |
|---|---|
| `+0x00` | Signed frame timestamp |
| `+0x04` | Three signed integer position components in source units |
| `+0x10`, `+0x14` | Two float easing fields, spline records only |
| `+0x18` | Incoming XYZ tangent, spline records only |
| `+0x24` | Outgoing XYZ tangent, spline records only |

The spline evaluator `0045a03c`, specifically `0045a2ae` and `0045a2b5`, reads
the left key's outgoing tangent and the right key's incoming tangent. Matrix
`004aa710` is the cubic Hermite basis:

```text
 2 -2  1  1
-3  3 -2 -1
 0  0  1  0
 1  0  0  0
```

Positions are `h00*p0 + h01*p1 + h10*tangentOut + h11*tangentIn`. Tangents
are already scaled for the interval: the executable does not multiply them
by the frame gap again. Zero tangents are legitimate; they are not treated
like missing quaternion controls. Linear records interpolate positions linearly.
Easing uses the same recovered function as rotation curves.

All **82,784 translation keys in 780 clips** decode with their declared counts.
Start, midpoint and endpoint samples are finite. Keys beyond a track's declared
duration are retained, because a bounding curve key can lie beyond the playback
interval. The earlier strict research harness flagged those keys; they are not
silently removed by the production parser.

Example: `XH_AN020` root slot 0 at frame 25 is `(-17, -181, -92)`, versus bind
position `(-5, -182, 0)`. The inspector displays this as a converted offset of
`(-0.12, -0.01, -0.92) m`. The clip's action name is still unverified.

## Root displacement and physics [verified flow; integration pending]

`004062ad` and `004068be` save the previous root position at actor `+0x128`,
read the newly evaluated position into `+0x11c` via `00457a04`, and subtract
them using `0045b98c`. The result is transformed by actor orientation and
movement modifiers. The movement/collision path at `0043d83e` subsequently
writes the resolved actor position back through `00457aa0`.

That separation matters: applying the animated root offset to the mesh and
also moving its parent by the same amount would double the displacement.

The current runtime offers:

- **In place:** hold root slot 0's horizontal X/Z at its bind position; retain
  vertical animation and all other nodes' translation curves.
- **Follow animation:** display the complete stored root path in model space.
- **`rootMotionDelta`:** expose the source-space displacement over the latest
  update. Wrapping adds `cycles * (rootEnd-rootStart)` to compensate for timeline
  reset, including multiple cycles in a single update. Seeks and pauses produce
  no locomotion delta; pose transitions also suppress delta extraction.

The first two modes are inspector/rendering policies, not a claim to duplicate
every game physics flag. Deltas are available to a future movement consumer;
the present Duncan controller still handles input and collision movement.
Root slot 0 matches the engine's default object/root lookup. Assets with special
movement objects or alternate roots need separate gameplay mapping.

## Original blend path [verified common case]

`004599ec` handles two linear clip channels; `0045aa00` handles two spline-layout
channels. They sample two rotation/position poses, use quaternion interpolation
for rotation and a weighted sum for position. The blend range is 0–256.
The two-clip spline-layout routine uses linear interpolation of the primary
keys inside its channels; it is not identical to the single-clip SQUAD evaluator.

In `00405f1f`, the incoming frame at actor `+0x174` is set to 1 on each transition
update; the outgoing frame at `+0x170` is held. Weight `+0x164` increases by
effective engine delta times float **48.0**, at address `004c33e4`.
At normal speed this gives `256/(48*30) = 0.177777... seconds`.
`00405db4` commits the incoming clip/frame at completion. Clip flags can force
immediate completion; their full action mapping remains unrecovered.

The shared controller's `engineTransition` option holds the outgoing pose and
incoming start pose for that nominal duration. It then starts incoming playback.
Duncan uses this option instead of the previous guessed 0.125-second crossfade.
The implementation uses continuous weights and the current sampled outgoing
pose for continuity; it is not an exact emulation of the original integer-key,
weight-rounding, signed quaternion, or action-flag behavior.

## Inspector blend experiment

The inspector now exposes **Blend with**, **Second clip weight**, **Root movement**,
and **Loop preview**. Two selected clips have independent clocks. Weight zero
uses the first pose; weight one uses the second; translations blend as well as
rotations. Changing weight does not reset either clock. Scrubbing explicitly sets
both source frame counters, clamped to their respective durations. Reopening
the inspector restores its experiment; closing it restores the actor's prior
playback. A one-shot clip stops at its final frame and can be replayed.

This manual experiment uses the shared curve sampler for both clips, including
SQUAD/Hermite where available. It exposes the mechanics without claiming that
an arbitrary pair is an original game transition or a valid locomotion blend.

## Validation

```powershell
uv run dreams export-animations
uv run pytest tests/test_animation_translations.py
npm --prefix web test
```

Tests cover original Hermite coefficients and transition-rate constants,
translation-key decoding, exact stored positions, interval-scaled tangents,
root-motion loop continuity, multiple wraps, seeks/pauses without teleportation,
blend endpoints and independent clocks, snapshot restoration, and held-pose
transition timing. The browser checks confirm the root offset above and its
50%/100% blend with `XH_AN000` at frame 1.
