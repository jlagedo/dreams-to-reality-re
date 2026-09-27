# 002 — libcdio Windows image-access spike

Status: **tested locally, not integrated**. Date: 2026-09-26.

## Setup

- Upstream `libcdio-C` commit `68ea374f83d5e63122168e4e182b15ba10e0d2eb`
  (the checkout reported version 2.4.1), cloned under ignored `out/`.
- Windows x64, Visual Studio Community 2026/MSVC v145. The upstream
  `libcdio.sln` built `libcdio.lib` and `cd-info.exe` successfully with
  `/p:PlatformToolset=v145`. The prebuild `set_version.ps1` was blocked by this
  machine's PowerShell execution policy, so the version header was generated
  from `version.h.in` and the build used `/p:PreBuildEventUseInBuild=false`.
- The CUEs and BINs are the original split-track image sets recorded in
  [disc-layout.md](../disc-layout.md#where-the-images-live). Commands ran from
  each CUE's directory because the BIN paths otherwise resolved relative to the
  process working directory. `cd-info` was invoked with `--cue-file`,
  `--iso9660`, `--no-cddb`, `--no-vcd`, `--no-header`, and `--no-device-info`.

## Results

| Input | Disc 1 | Disc 2 |
|---|---|---|
| Original multi-BIN CUE | Opens and reports 12 tracks, but audio tracks 2–12 all start at LSN 150. Reports **unknown filesystem**. | Opens and reports 14 tracks, but audio tracks 2–4 and 6–14 start at LSN 150; track 5 starts at 151. Reports **unknown filesystem**. |
| `cdio_read_mode1_sector` at LSN 16 from original CUE | Returns success and eight zero bytes. | Returns success and eight zero bytes. |
| Temporary CUE naming only the original Track 1 BIN | Detects ISO 9660 and lists **1,219 files**. | Detects ISO 9660 and lists **409 files**. |
| `iso9660_open` on independently derived data-track ISO | Opens; exact `/DREAMS.DAT;1` has size 138,879. | Opens; exact `/DREAMS.DAT;1` has size 138,835. |

The first eight bytes at logical sector 16 of the real data-track BIN and the
derived ISO are both `01 43 44 30 30 31 01 00` (the ISO primary volume
descriptor), independently checked by direct bounded file reads. The original
CUE's zero-filled successful read is therefore incorrect. The original CUE
also produced repeated pregap warnings; Disc 2 additionally warned that track
6 starts before track 5's reported end. `cd-info` nevertheless exited 0 once
it found the backing files.

The upstream [BIN/CUE driver](https://github.com/libcdio/libcdio-C/blob/68ea374f83d5e63122168e4e182b15ba10e0d2eb/lib/driver/image/bincue.c)
stores a source name for each `FILE` directive, but its sector-read path uses
one current `data_source` and treats the parsed indexes as disc-relative.
That implementation is consistent with the observed failure on this release's
one-BIN-per-track CUEs, whose indexes are file-relative. The one-track checks
isolate the problem to CUE/BIN mapping; libcdio's ISO reader worked for both
data tracks.

## Decision for 002

The current libcdio BIN/CUE driver cannot serve these original multi-BIN
images correctly without a patch. Its ISO reader can be evaluated separately,
but a runtime integration must also address its GPLv3 license and build on all
OpenDreams targets. The next disc-access implementation should retain tests for
the original CUE layout, file-relative `INDEX 00/01`, the Disc 2 track-5
`00:02:01` exception, LSN 16's volume descriptor, and full per-disc file counts.
