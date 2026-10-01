# 007 — Implementation plan

Date: 2026-10-01. Plan for [spec 007](spec.md). Scope set by the owner:
**Windows first** (the code stays portable; Linux and macOS are not acceptance
gates here), user data **beside the executable**, and a **local release
script** (no CI).

## What is settled

Checked on this machine before planning. **[verified]** unless marked.

| Question | Answer | How |
|---|---|---|
| Is the disc file system simple enough for a small reader? | Yes. Both discs have one primary volume descriptor, no Joliet, 2048-byte blocks, every file one contiguous extent, no interleaving | A prototype reader over the `(Track 01).bin` files lists 1219 and 409 files, the same names and sizes as the extracted trees; `GDIDREAM.EXE`, `DREAMS.DAT`, `INTRO.HNM`, `DIALOG.DRD` and the marker files match byte for byte |
| Data track layout | `MODE1/2352`, user data at offset 16 of each sector, LBA 0 at the start of the file | Same prototype |
| Audio track layout | One `.bin` per track, `INDEX 00` at 0 and `INDEX 01` at 00:02:00: each audio file starts with a 2-second pregap | The cue files |
| Does the game continue once the other disc's marker opens? | Yes | Recomp run with a patched `DREAMS.DAT`, spec section "What was tested first" |
| Does CD music exist in the host? | Yes, from track files in one directory, opened by path | `host/sdl/winmm.c`, `mixer_cd_play` in `dsound.c` |
| Can Dear ImGui's SDL3 backends be used with the pinned SDL? | Yes: the pin is SDL 3.4.16, built static with default subsystems (render and dialog included) | `SDL_version.h`, `recomp_env.ensure_sdl3` |
| Do in-process environment changes reach the host? | The host reads through `host_env` (C `getenv`) in most places and `SDL_getenv` in `files.c`. `SDL_setenv_unsafe` updates SDL's copy and the C runtime's. Checked by reading SDL's source, not by a run **[unverified]** | `SDL_getenv.c`, `host/sdl/user.c` |
| How does the host load the executable? | `load_image` reads the whole file into a buffer from a host path, then copies sections. Feeding it a buffer from the image is a small change | `host/core/runtime.c` |
| Where does the d-pad go today? | Mode "game": the joystick POV hat. Mode "keys": arrow keys. The stick drives the axes | `winmm.c` |

## Design adjustments from the spec draft

Found while reading the host code; the spec is updated to match.

1. **The user data directory is the host's existing write sandbox.** Today
   every write goes to `WD_WRITE_ROOT` and every read looks there first. The
   plan keeps that rule and only changes where the directory is. Saves end up
   in `<data>\CRYO\DREAMS\data\game\`. No new write-routing rules, and a file
   placed in the data directory overrides the disc's (which the swap test
   needs, and which makes user patches possible).
2. **A shared disc library.** The launcher must validate images and the host
   must read them, but the launcher may not include host headers. The cue,
   sector, ISO and SHA-256 code goes in `recomp/disc/`, a C library with no
   dependency on the host or the launcher. Both link it.
3. **Data layout beside the executable**: `dreams.ini` and `userdata\` next to
   the executable. If that directory is not writable, both go to the per-user
   directory (`SDL_GetPrefPath`) instead, and the launcher shows which is in
   use.

## Milestones

Each one ends in something that runs and is checked. M1–M4 are testable from
`run.py` with no launcher.

### M1 — Disc library (`recomp/disc/`)

- `cue.c`: `FILE`/`TRACK`/`INDEX`/`PREGAP`; per-track and single-file bins;
  result is a track table (file, byte offset of `INDEX 01`, length, mode).
- `image.c`: open a `.cue`, `.iso` or directory as one "disc"; sector reads
  for 2352 and 2048 layouts; ISO 9660 directory walk with bounds checks; names
  compared without case, `;1` stripped; file open returns extent and size.
- `sha256.c`, checked against the standard test vectors.
- `disc_list` command-line tool: prints name, size and SHA-256 of every file,
  and the track table.

Check: a pytest in `tests/recomp/` runs `disc_list` on both cues and compares
with the extracted trees (skipped when the discs are not configured). A
synthetic single-bin cue, made by concatenating the track files, gives the
same output. Truncated and corrupt inputs fail with a message, not a crash.

### M2 — Host reads from the images

- `files.c`: a read source is a directory (today's behaviour) or a disc from
  M1. A guest file on a disc is an `SDL_IOStream` over the extent, with its
  own handle on the `.bin` so guest threads do not share a file position.
  `FindFirstFileA` and `GetFileAttributesA` answer from the ISO directory.
- New inputs `WD_DISC1`, `WD_DISC2`, `WD_DATA_DIR`. When `WD_DISC1` is unset
  the current `WD_READ_ROOTS` behaviour is untouched.
- Resolution: data directory first; then the active disc; for paths under
  `CRYO\DREAMS\`, the prefix is dropped and both discs are searched, active
  first; `CRYO\DREAMS\DATA\FULL.ID` is never found.
- Active disc and the marker rule from the spec. One log line per switch.
- `runtime.c`: load `GDIDREAM.EXE` from disc 1 when started without an
  executable path.
- `run.py --discs`: passes the two cue paths from `DREAMS_DISC1`/`DREAMS_DISC2`'s
  parent folders, for all the checks below.

Checks:
- Boot to the first level from the images, headless; the snapshots match a
  run from the extracted tree with the same key script.
- The swap script: patched `DREAMS.DAT` (Project0 at level 3) in the data
  directory; the log shows one query, the prompt's second poll switching to
  disc 2, level files read from disc 2, nothing written but saves.
- A second patched bank with Project0 at level 4, started with disc 2
  active, to cover "already on the right disc".
- No file under the data directory other than `data\game\*` after a session.

### M3 — CD audio from the images

- `mixer_cd_play` takes a file, a start offset and a length instead of a path;
  the track table comes from the active disc.
- The track list follows the active disc on a switch.
- `MCI_PLAY` with no start position and no current track: success, silent.
- The 2-second pregap: play from `INDEX 01`. Today the host plays the whole
  file, pregap included; this changes when music starts by 2 seconds, which is
  what a real drive does.

Check: the `[cd] play track` log lines in the M2 runs name the expected track
and disc; the swap script shows no message box; a manual listen on the menu
and the first level.

### M4 — Device remapping

- `WD_KEYMAP` in `user.c`: applied where a scancode becomes a virtual key.
- `WD_PADMAP` and `WD_PAD_DIRECTION` in `winmm.c`: direction source for the
  joystick axes (mode "game") or arrow keys (mode "keys"); button table.
- Unset variables give today's tables exactly.

Check: scripted runs cannot press physical keys through the remap (the `WD_KEYS`
script injects virtual keys), so the check is a small native test of the two
parsing and lookup functions plus a manual run with WASD and with the d-pad.

### M5 — Launcher (`recomp/launcher/`)

- Dear ImGui pinned by commit in `recomp/render/cmake/Dependencies.cmake`;
  `imgui_impl_sdl3` and `imgui_impl_sdlrenderer3`.
- `dreams.ini` read and write; the window from the spec; validation through
  the M1 library (marker, edition hash, music present); browse through
  `SDL_ShowOpenFileDialog`; a file dropped on the window fills the row too.
- Entry point returns "quit" or `WD_*` pairs. `main` sets them with
  `SDL_setenv_unsafe`, skipping names already set, and continues as today.
- `--play`, `--disc1`, `--disc2`, `--data`. With an executable path as the
  first argument (what `run.py` passes) the launcher is skipped.

Checks:
- The library links in a test target without the host and render libraries.
- First start, second start, moved image, wrong edition (a copy of disc 1's
  tree with one byte of `GDIDREAM.EXE` changed, as a directory source), two
  copies of one disc, `.iso` only: each shows its status.
- A setting changed in the window is visible in the host's start-up log lines.
- A user name and a disc path with non-ASCII characters.
- Keyboard-only and gamepad-only use.

### M6 — Release script

- `recomp/windream/release.py`: build, collect the executable and a short
  `README.txt`, zip. Fails if the folder holds anything else.
- Windows subsystem for the release executable (no console window); the
  host's stderr goes to `userdata\log.txt`, crash dumps beside it.
- Same compiler flags as the development build to begin with. See risk 4.

Check: unzip into an empty folder on this machine, run, set up, play; unzip
into a read-only folder and confirm the per-user fallback.

### M7 — Documentation

`recomp/README.md` (new variables, `--discs`, the launcher), `AGENTS.md`
(repository map rows for `recomp/disc/`, `recomp/launcher/`, the commands),
spec status and evidence tags, `install-and-discs.md` where a run confirmed a
tagged item.

## Risks

| # | Risk | Likelihood | What happens | Mitigation |
|---:|---|---|---|---|
| 1 | The marker rule misfires: something other than the swap prompt opens the same marker twice in a row | Low; only three functions open markers and their loops are known | A wrong disc switch | The switch is logged; M2 checks the open sequence. Fallback: decide by the guest return address of the open |
| 2 | Disc 2 levels have never run in the recomp (disc 2 was never mounted) | High that something turns up | Acceptance on disc 2 stalls on bugs unrelated to this spec | Treat them as recomp bugs under spec 000; this plan's gate is "the load reaches the level", not "the second half is playable" |
| 3 | Something else in the game expects a real install directory | Low; the install-root callers are enumerated | A missing-file fatal error at a point not yet reached | The fallback covers every disc file; M2 logs every failed open under `CRYO\DREAMS\` for review |
| 4 | An optimized build of the lifted C misbehaves | Unknown; the project has only built unoptimized | Wrong behaviour that tests may not show | Release with development flags first. Optimizing is a separate step, gated by `render_acceptance.py` and the difftests |
| 5 | Non-ASCII paths: environment strings and `fopen` use the ANSI code page on Windows, SDL uses UTF-8 | Likely to bite; `files.c` already notes it for read roots | An image under a user folder with accents does not open | Paths travel as UTF-8 and are opened only through SDL; M5 tests a non-ASCII path. `mixer_cd_play` moves off `fopen` in M3 |
| 6 | In-process environment changes not seen by one of the two read paths | Low | A launcher setting is ignored | M5 check reads back each variable through both `getenv` and `SDL_getenv` at start-up and logs a mismatch |
| 7 | ImGui's built-in font is small on high-DPI screens | Certain on 4K | A tiny launcher | Scale by the window's display scale; accept slight blur, or embed a scalable free font later |
| 8 | Playing audio from `INDEX 01` shifts music timing against today's runs | Certain, by 2 seconds | Snapshot comparisons with older runs differ where they depend on music; none known | Compare M2 snapshots before M3 lands |
| 9 | Single-bin cues exist in the wild but none is on this machine | Certain | Untested format | The synthetic cue in M1 |
| 10 | The executable's folder is not writable | Occasional | Settings and saves cannot be written | The per-user fallback, shown in the launcher |

## Unknowns that stay open

- What a real drive answers to the resume after a swap (the message-box path).
  The host change makes it silent either way.
- The real Project26 → Project116 transition, until a save near it exists.
- Whether other pressings of the European disc have a different
  `GDIDREAM.EXE`. The first user report decides.

## Order and size

M1 → M2 → M3 are the core and remove today's dependency on extracted trees.
M4 is independent and can go at any point. M5 needs M1 only. M6 needs M5.

Rough size, in new or changed lines: M1 700, M2 400, M3 80, M4 120, M5 700
plus the ImGui pin, M6 100.

## Results (2026-10-01)

All milestones are implemented on Windows. Nothing below was checked by a
person playing; it was checked by tests and by driving the game through the
development control channel.

| Milestone | Result | Evidence |
|---|---|---|
| M1 disc library | done | `tests/recomp/test_disc.py`: both cues list the same names, sizes and SHA-256 as the extracted trees; single-bin cue, `.iso`, directory sources; corrupt input refused |
| M2 host reads from the images | done | `test_disc_mode.py`, `test_disc_play.py`: boot to the first level from the cues; nine disc 2 projects load and run; the Project26 → Project116 link switches 1 → 2; return 2 → 1 by save, by link and by new game |
| M3 CD audio | done | `test_disc_play.py`: the mixer dump equals track 9 of disc 1 sample for sample, starts at INDEX 01, comes from disc 2's tracks after the switch, stops at the track's end in a shared `.bin` |
| M4 device remapping | done | `test_input_map.py`; physical keys and pads not pressed |
| M5 launcher | done | `test_launcher.py`, `test_launcher_ui.py`: the window driven by a test-only event script (dialog results, drops, key capture, conflicts, all tabs, keyboard-only and virtual-gamepad navigation, Play, Quit) |
| M6 release script | done | `test_release.py`; `release.py` builds `DreamsToReality-windows-x64.zip`; GUI subsystem, static C runtime, log in `userdata\log.txt`; fails if development or test code is in the executable |

Saves: the game writes them itself on every level entry (there is no Save
command in the retail menu). They land in `userdata\CRYO\DREAMS\data\game\`
with the retail sizes and load in the same session, in a new process and
across discs.

### Added beyond the plan

- `recomp/windream/devtools/`: a development-only control channel in the
  host (keys, wait-until conditions, memory, screenshots, pause and step,
  event log, audio dump), behind `WD_DEVTOOLS` and inert without `WD_CTL`.
  Client `recomp/windream/debug/wdctl.py`; MCP server `wd_mcp.py` registered
  in `.mcp.json`. Its header lists what to delete to remove it.
- `recomp/windream/debug/bank_patch.py` (writes a modified `DREAMS.DAT` under
  `out/` to start a new game in any project) and `cd_audio_check.py`
  (aligns a mixer dump with a track of the image).
- The launcher's test script and test-only variables, behind
  `DREAMS_LAUNCHER_TESTING`.
- `release.py` refuses an executable that contains either.

### Risks, as they turned out

| # | Outcome |
|---:|---|
| 1 | No misfire seen. Going 2 → 1 the switch happens on the prompt's first poll, because the query's failed probe already counts |
| 2 | Did not happen: nine disc 2 projects of level numbers 3 and 4 loaded and ran with no crash |
| 3 | One case: disc 2 ships old save files under `DATA\GAME\`, which the install fallback picked up. Saves now resolve in the user data directory only |
| 4 | Not tried; `release.py --optimize` exists and is unverified |
| 5 | Tested: non-ASCII cue path, data directory and per-user fallback all work |
| 6 | Real: `SDL_setenv_unsafe` alone is not seen by C `getenv` on Windows. `main` sets both and checks each value both ways at start |
| 7 | Seen at display scale 1.5 only |
| 8 | Confirmed by the audio check; no snapshot comparison depended on it |
| 9 | Covered by synthetic cues in the tests and one game run from one |
| 10 | Tested with a write-denied folder: settings and saves went to the per-user directory |

### Not verified

- Sound through a real audio device, a real gamepad, physical key presses
  through the remap, the operating system's file dialog, "Open folder".
- Reaching Project26's exit by playing its fight; the tests set the trigger
  flag (`0x6155e4`) instead.
- A real crash in a release run (dump location follows from the directory
  change, not from a dump).
- Linux beyond a boot to the first level; macOS at all.
- The `--vm shadow` and `--vm win32` builds were not rebuilt.
