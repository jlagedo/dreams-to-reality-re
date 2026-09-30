# HNM5 conversion boundary

Date: 2026-09-30. This is scoped contract evidence, not full movie acceptance.

## Required route and original contract

The shipped inventory contains project movies including `ARENTRAD.UBB`
(project 8), `CONTROLE.UBB` (11), and `HNMFR5.UBB` (1), plus LINKADVENT
consumers such as `GUARDIAN.UBB` (94). These are UBB2/UBS2 game assets;
unreferenced disc demo movies are not the evidence for this route.

`VID_Open` selects kind 2 for UBB2/UBS2. `VID_DecodeFrame` dispatches to
`VID_DecodeHnm5Frame` (`0x408b4e`), which processes codec/palette/audio chunks,
swaps decoded source buffers, then calls `Remap256to16bits_` (`0x42665a`)
before the normal presentation call. Source buffer `0x60ce14` and the packed
lookup table at `0x60ce1c` remain lifted. `0x42652a` updates that table from
palette chunks; each 16-bit source pair indexes one packed 32-bit output pair.

The wrapper saves EBX/ECX/EDX/ESI/EDI/EBP, calls the compiler stack guard and
the leaf helper `0x454de3`, and consumes its existing return address once.
The helper receives four caller-cleaned stack arguments: source, destination,
lookup table, and mode. Its only output besides pixels is the loop-counter
global `0x4a4b70` in modes 0/2/4. It does not read destination pixels or call
guest callbacks. The wrapper returns the source pointer in EAX, not the output
buffer pointer.

| Logical dimensions | Mode | Destination footprint |
|---|---:|---|
| 800×600 | 0 | Rectangle `(80,60,640,480)`; surrounding pixels untouched |
| 640×400 | 1 | Entire target: 50 black rows, 300 converted rows, 50 black rows |
| 320×400 | 2 | Entire target: 50 black rows, 300 horizontally subsampled rows, 50 black rows |
| 320×200 | 4 | Entire target: 25 black rows, 150 rows subsampled in both axes, 25 black rows |
| 640×480 | 5 | Entire target: 90 black rows, 300 converted rows, 90 black rows |
| Other dimensions | −1 | No destination writes |

Read-only Ghidra evidence: `DREAMS_OUT/recomp/hnm5-contract-006.txt` and
`hnm5-callers-006.txt`; source references are in
`DREAMS_OUT/recomp/render-content-inventory/inventory.json`.

## Adapter

`render_movie.c` retains the original wrapper/helper. For a GPU-owned target,
it temporarily redirects the framebuffer global to one bounded 960,000-byte
guest CPU movie buffer, executes the original conversion, restores the global,
and uploads exactly the written rectangle with the original pitch. Mode 0
does not upload its untouched border. Ordinary RAM and unknown-dimension
no-draw cases execute the original wrapper unchanged. Renderer shutdown frees
the sole movie buffer.

This is CPU codec output conversion followed by upload. It reads no GPU scene
pixels and does not invoke a software scene rasterizer. Codec state, palette
updates, source-buffer swaps, audio and presentation timing remain lifted.

## Verification and remaining acceptance

```powershell
uv run --with unicorn python recomp/windream/debug/render_movie_smoke.py
# When the Windows GPU validation slot is free:
uv run --with unicorn python recomp/windream/debug/render_movie_smoke.py --gpu
```

The oracle executes original x86 from two independent retail dumps with
synthetic index and packed-palette inputs. Only the compiler stack guard is
stubbed consistently in original and native replay. All five supported modes
and an unknown-dimension no-draw case pass with two palettes: 24 original-x86
cases and 48 production bridge paths. Checks cover output bytes, all returned
GPRs/ESP, FPU control/top, defined arithmetic flags, loop metadata, restored
framebuffer global, output guards, untouched GPU-owned guest pixels and one
allocation/release across the whole run.

Each run also generates 20 standard D2D1 fixtures for the existing
`ODDirectGpuTests` executable. Uploaded rectangle pixels come from the native
bridge; expected full frames come independently from original x86. The suite
covers both packed formats and initializes RGB555 targets with high-bit-set
`0xfb7b`, checking preservation of untouched mode-0 borders as well as uploaded
pixels and black bars. `--gpu` records the D3D11 result in `results-gpu.json`;
all 20 fixtures executed successfully with zero packed-pixel mismatches.

The strict direct Windows run `run-direct-hnm5-project11-006` exercised a
transition into project 11 and actual `CONTROLE.UBB` playback at native
640×480. Its log records `hnm5_frames=1`; captures at 27.783 and 30.820 seconds
show the movie. Project 11 subsequently loaded and resumed scene submission,
with one normal autosave thumbnail export (8,192 bytes) and no reported
instrumented surface-access failure. This is a game movie consumer and
transition checkpoint, not only a synthetic converter invocation.

Reports and generated lifted closure remain under
`DREAMS_OUT/recomp/render-movie-smoke`. This establishes the conversion/bridge
contract and the recorded native playback/transition checkpoint. Other logical
resolutions have GPU-oracle coverage but still require live playback checks;
palette-animation changes during playback, remaining shipped UBB consumers,
repeated movie/resource lifetimes and whole-game pacing remain acceptance work.
