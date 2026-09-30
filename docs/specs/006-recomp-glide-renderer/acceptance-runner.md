# Windows acceptance runner

`acceptance-routes.json` is a finite checklist seeded from spec 006, not a completion claim. The main failure/coverage ledger remains owned by the integration lead. Empty `args` means a required route has no automated scenario yet. Add a real route and semantic checkpoint before changing that status; a scene-load sweep cannot close dynamic gameplay coverage.

Run inventory and bounded checks from the repository root:

```powershell
uv run python recomp/windream/debug/render_acceptance.py --inventory
uv run python recomp/windream/debug/render_acceptance.py --run first-scene-native --run first-scene-wide --run resize
```

Build the audit executable once before running. The runner performs no builds and runs scenarios serially. Reports live in a unique `DREAMS_OUT/recomp/acceptance/<session>` directory; each child has a unique run/save sandbox. Inventory lists configured original-disc assets and the documented project/scene mapping. It does not claim every asset is loaded or exercised.

The runner records literal reached checkpoints, failure lines, sampled resource counts, routine-readback counters and explicit exports. `checkpoints-reached` means only the named observations occurred; final acceptance stays pending until semantic, visual and strict boundary evidence is reviewed. A killed timeout is never itself a passing checkpoint. Reported zero readbacks does not fill gaps in memory-audit instrumentation. Current first-scene checks do not establish dialogue, combat or full-game pacing.

`run.py --width W --height H` requests an exact initial SDL client size, overriding scale. Record the actual drawable size, especially with DPI scaling. `--resize ms:WxH,...` schedules client-size changes. `--mouse ms:move:x:y,ms:left-down:x:y,ms:left-up:x:y,...` schedules client coordinates through the normal host mouse path; right-down/right-up are also supported. Schedules are ordered, limited to 128 events each, and measured from the same host clock as keys. Headless/fullscreen cannot be combined. Use visible playback for fullscreen, appearance and real input review.

Do not overlap shared builds, same-script fixed output directories, or GPU performance runs. Existing controlled lighting/shadow/thumbnail smokes remain useful focused evidence, but they do not replace natural effect triggers or ten full recomp reloads. Keep captures out of measured pacing intervals because their explicit exports alter timing.

`--render-profile` enables bounded CPU/frame percentiles and nonblocking D3D11
query sampling. Use the normal build for pacing and the audit build separately
for access correctness. `[render-profile]` JSON reports every 250 presents;
pending, skipped, failed and disjoint samples remain explicit. No query waits,
scene readbacks or frame-cap changes are introduced. A single first-scene timing
run is a measurement sample, not whole-game performance acceptance.

Focused closure checks:

```powershell
uv run --with unicorn python recomp/windream/debug/render_line_smoke.py --hook --gpu
uv run --with unicorn python recomp/windream/debug/render_light_live_smoke.py --oriented --gouraud --callback
uv run python recomp/windream/debug/render_thumbnail_smoke.py --reload-count 10 --snap-ms 0
```

The callback check uses controlled lighting and the existing editor callback.
The reload check uses a controlled initial autosave followed by real load-menu
operations, requires an actual save read and resumed scene after each reload,
and reports failure if input navigation does not reach those checkpoints.
It counts the uncapped `[save] open R` events; the general `[files]` log stops
after 200 opens and cannot establish repeated-load coverage. Ten-reload
acceptance is still pending a run using this corrected observation path.
