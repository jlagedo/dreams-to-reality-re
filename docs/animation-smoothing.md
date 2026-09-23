# Animation interpolation and loop boundaries

The original engine interpolates animation keys. The first shared viewer
runtime used SLERP everywhere and did not export the quaternion spline
controls. Its inspector also looped through frame zero. These were viewer
limitations, not evidence that the original game lacked smoothing.

## Loop correction [verified]

Action initialization starts its frame counter at **1**, as traced in
[animation-timing.md](animation-timing.md). Exported clips now declare
`playbackStart: 1` (clamped for zero-duration clips). Frame zero remains
seekable as an inspection pose, but normal playback and wrapping exclude it.

On `XH_AN055.3DA`, the maximum bone-angle difference between the final pose
and frame zero is **84.329 degrees**. Final pose versus frame one is zero.
With spline sampling, comparing `duration - 0.01` with `1.01` gives a maximum
difference of **0.4205 degrees**. The regression tests check this distinction
without inserting an artificial blend across the loop boundary.

Not every clip is authored as a seamless cycle. For example, `XH_AN018` has
different first/last poses. The generic preview loop policy does not establish
the original gameplay action's looping/one-shot behavior.

## Quaternion splines [verified structure, floating-point implementation]

`WINDREAM.EXE:0045a03c` evaluates 60-byte rotation keys. The instruction
sequence at `0045a16a`–`0045a1e0` establishes this structure:

```text
t = (frame - left.time) / (right.time - left.time)
u = ease(t, left.field_14, right.field_18)
primary = slerp(left.rotation, right.rotation, u)
control = slerp(left.outControl, right.inControl, u)
rotation = slerp(primary, control, 2*u*(1-u))
```

This is SQUAD interpolation. Control quaternions are not angular derivatives.
The left key's outgoing control is at `+0x1c`; the right key's incoming
control is at `+0x2c`. The engine's quaternion routine does not perform the
hemisphere flip used by the viewer's ordinary shortest-path SLERP; the spline
path therefore preserves authored quaternion signs. Crossfades still use
shortest-path interpolation.

The easing function at `00459ec0` is piecewise quadratic/linear. With
`start = right.field_18`, `end = left.field_14`, normalize both by their sum
if it exceeds one, then set `k = 1/(2-start-end)`:

```text
u = t                              if both fields are zero
u = k*t*t/start                    if t < start
u = 1 - k*(1-t)*(1-t)/end           if t >= 1-end
u = k*(2*t-start)                   otherwise
```

The unusual field ordering comes from the caller's argument pushes and the
assembly comparisons, rather than guessed authoring labels. All easing fields
in the current 23,013 stored spline keys are zero.

## Missing controls and precision

Some stored control quaternions are zero. There are 1,868 zero control operands
in 18,771 adjacent spline segments whose right timestamp exceeds frame one.
Examples occur at final keys in MHE. Normalizing those zeros as identity would
invent a rotation. Exports retain them as null; those segments use continuous
SLERP until their original handling is understood. Twenty-byte keys use SLERP.

The implementation follows the recovered spline structure in floating point.
It does not reproduce the original 1/256 interpolation-weight quantization,
trigonometric lookup tables, or every fixed-point rounding step. Unit tests
check endpoints, a known nonlinear midpoint, easing continuity, missing-control
fallback, and frame-zero exclusion. Binary tests check the control offsets and
constants in the original executable.

Translation curves and root-delta extraction have since been implemented; see
[animation-root-blending.md](animation-root-blending.md). Game-state action
selection and physics consumption of those deltas remain open.
The mesh renderer still uses source integer transform rounding. This work does
not claim every remaining jerk is fixed or that every preview clip should loop.

```powershell
uv run dreams export-animations
uv run pytest tests/test_animation_splines.py tests/test_animation_timing.py
npm --prefix web test
```
