# Original animation timing [verified static trace]

The Windows engine's nominal base is **30 animation frames per second**.
It is an executable clock scale, not a field in `.DAN` or a per-clip key count.
The viewer's earlier 10 fps was a placeholder. At the recovered base, 1x is
30 frames/second, 0.5x is 15, and 2x is 60.

This trace uses Disc 1 `WINDREAM.EXE`, SHA-256:
`1635f530e1ce64237b978073252bc2c583ead5e2436563ba3a42be4dc1b2c761`.
Addresses below are virtual addresses with image base `0x400000`.

## Clock to frame counter

| Location | Observed behavior |
|---|---|
| `00415fd8`–`00415fe7` | Calls dispatcher command `0x0c` with `EDX=200`, `EBX=15`. |
| `0043a306` (`MGM_SendMessage`), command `0x0c` | Calls `00424b4f`, which forwards to timer initialization `00440802`. |
| `00440802` | Reads `timeGetTime`; stores `1000/200 = 5 ms` as the main counter period at `006309e8`. The other timer has a separate period. |
| `00440890` | Adds `floor(elapsed_ms/5)` to counter `006309e0` when at least one period elapsed. |
| `0043a306` (`MGM_SendMessage`), command `0x11` | Updates the timer and returns `006309e0`. |
| `004170a6`–`004170d7` | Reads that counter and subtracts the previous value to obtain elapsed ticks. |
| `00417171`–`0041717a` | Calculates `200 / elapsed_ticks`, an estimated render FPS. The double at `004c41c4` is `200.0`. |
| `004171b2`–`004171be` | Calculates `30 / estimated_fps` into `005e5388`. The double at `004c41cc` is `30.0`. |
| `00407026`–`00407038` | Advances actor float frame counter `+0x170` by actor speed `+0x178` times effective delta. |

Ignoring clamps and special modes, the two divisions reduce to:

```text
engine_delta_frames = 30 * elapsed_ticks / 200
animation_frame += engine_delta_frames * actor_animation_speed
```

This is 30 frames per second at speed 1.0 with a nominal 200 Hz counter.
`0040eadd`–`0040eb19` is a second update path with the same `200.0` and `30.0`
constants at `004c398c` and `004c3994`.

## Actor and state modifiers

`004058d5` selects or transitions animation actions and writes float **1.0**
to actor `+0x178` at `00405afa`, `00405b6f`, and `00405d8a`. It initializes
the new frame counter at 1.0. The constructor (`0041cb77`) initially puts
1.5 in the speed field, but the action transition replaces it; that constructor
value does not establish a universal 45 fps rate. Duncan's initialization
at `0041f699` selects action 0 and invokes this transition path.

The pose update routines `00405f1f`, `004062ad`, and `004068be` can modify
the effective delta:

- While actor timer `+0xf0` is active, multiply by actor `+0xf4`.
- Actor `+0x44`, when nonzero, can multiply playback speed. In `004068be`
  this multiplier is gated for action IDs 0–8 unless flag `+0xad & 8` is set.
- Stop/hold paths set `+0x178` to zero.
- There is no evidence in this trace for the viewer's previous hard-coded
  walk `1.1` or run `0.85` factors; those have been removed.

Raw clip inspection therefore uses the recovered 30 fps base. Reproducing
every gameplay effect requires the actor/state fields in addition to the clip.

## Runtime quirks and scope

The original timer discards the fractional remainder when advancing its
last-update timestamp. The main update clamps estimated FPS to at least 1,
then clamps its frame delta to **0.2–5.0** (`004171dc`–`0041721c`). Mode
overrides can force a delta of 1 or 2. These details mean actual speed can
depend on timer polling, render rate, and special modes.

The viewer uses continuous browser elapsed time at 30 frames/second. It does
not emulate the Windows timer's polling loss, minimum delta, or mode overrides.
The trace establishes the normal clock scale; it is not a stopwatch comparison
against a running original game, nor verification of the DOS executable.

The executable also imports `QueryPerformanceCounter`: `0042493b` sends that
counter via event `0x3d`. This corrects the earlier documentation claim that
`timeGetTime` was its only clock. The animation delta path traced above uses
dispatcher `0x11` and the `timeGetTime`-based counter.

## Reproduce and guard the finding

```powershell
. .\tools\dreams-env.ps1
& (Join-Path (Get-DreamsSetting DREAMS_GHIDRA_ROOT) 'support\analyzeHeadless.bat') `
  ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly `
  -scriptPath ghidra_scripts `
  -postScript Decompile.java 00440802 00440890 00424b4f 0043a306 004058d5 004068be `
  -postScript Inspect.java range:004170a6-004172a2 data:004c41c4 data:004c41cc `
  range:00407026-00407038

uv run pytest tests/test_animation_timing.py tests/test_animation.py tests/test_animation_harness.py
```

`Inspect.java` prints instructions, numeric constants, field references, and
call-site contexts without modifying the Ghidra project. The tests verify the
original executable's initialization operands, clock arithmetic instructions,
constants, speed reset, and frame accumulator; they also check that different
Duncan key counts do not change exported base rates. Unavailable assets skip
the binary checks rather than substituting fabricated evidence.
