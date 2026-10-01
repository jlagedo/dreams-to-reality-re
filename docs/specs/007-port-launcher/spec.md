# 007 — Running the port: launcher, disc images and user data

Status: **Implemented on Windows, 2026-10-01: disc library, host reading from
the images, CD audio, device remapping, launcher, release script. Checked by
scripted runs and tests; play-through, audible music and the interactive
launcher are still to be checked by hand. Results: [plan.md](plan.md).**
Date: 2026-10-01
Depends on: [000 the recomp](../000-the-recomp/spec.md),
[install, data roots and the disc swap](../../research/install-and-discs.md)

## Goal and boundary

A user downloads one port executable for Windows, Linux or macOS, points it at
their own two disc images once, and plays. No installer, no extraction step,
no disc-swap prompt to answer, music included.

The port ships no game data. It reads everything from the user's images at run
time, including `GDIDREAM.EXE`, whose data sections the recompiled code needs.

Out of scope: the web build (its storage and fetching differ; separate spec),
image formats other than those listed, the DOS builds, and the retail
`SETUP.EXE` features (demos, DirectX install, extras gallery).

Evidence tags follow [the docs index](../../README.md#evidence-tags). Retail
facts cite [install-and-discs.md](../../research/install-and-discs.md).

## Decisions

| Question | Decision | Why |
|---|---|---|
| Installer | None. A first-run launcher inside the port executable | `SETUP.EXE` only copies files and writes a registry key the game never reads |
| What the user supplies | The two disc images as `.cue` files | Music is redbook audio; it exists only in the image, not in an ISO file tree |
| Copy or read in place | Read in place | Nothing to copy: the game never needs a file on the hard disk that the discs lack |
| Install size | Neither mini nor maxi: `DATA\FULL.ID` is never visible to the game | Maxi only adds a 41–71 MB copy per section; see below |
| Two discs | Both stay open; the host switches the active one when the game asks | Keeps each disc's own `DREAMS.DAT`, files and track list, as in retail; no merge decisions |
| User data location | `userdata\` beside the executable (owner decision); the per-user directory when that folder is not writable | Portable by default; the game's `\CRYO\DREAMS` is a guest path the host maps |
| Settings file | `dreams.ini` beside the executable, with the same fallback | Must be findable before any path is known |
| Platforms | Windows first (owner decision); the code stays portable, Linux and macOS are not gates of this spec | Linux builds today, macOS has never been built |
| Release | A local script that builds and zips (owner decision); no CI | The lifted C is not in the repository |
| Supported edition | European English only, checked by hash | The port is lifted from that edition's `GDIDREAM.EXE` |
| Launcher UI | A Dear ImGui window (owner decision, 2026-10-01) | A real window with disc status and settings; the OS file picker still browses for files |
| Launcher scope | Disc setup and port settings: only what retail does not have (owner decision) | Retail options stay in the game; the launcher never writes them |
| When it shows | Every start, with a Play button (owner decision) | As the retail `SETUP.EXE` launcher screen did |

## What was tested first

Two runs of the current recomp on 2026-10-01, with a `DREAMS.DAT` in the write
sandbox whose Project0 record has `+0x1fc` changed from 1 to 3 (so a new game
asks for disc 2), keys `2000:ESC,5000:RETURN,8000:ESC,20000:ESC`:
**[verified]**

| Read roots | Result |
|---|---|
| Disc 1 and the install tree (today's `run.py` default) | `CD_PromptSwap` opens `DATA\2CD.ID` without end; the game never continues |
| Disc 1, the install tree and disc 2 | The first open of `DATA\2CD.ID` succeeds and the load continues |

The second run also showed two things the design has to handle:

- With `FULL.ID` visible, the game copied the `ListL3.txt` files into the
  sandbox (65 MB in 40 s) before loading. This is the copy this spec avoids.
- Right after the prompt the game shows an "MCI Error" message box: "The
  specified parameter is out of range". `CD_PromptSwap` ends with MGM 0x23
  (`CD_ResumeAudio`: `MCI_PLAY` with no start position), and the host's
  `mciSendCommandA` rejects that when no track is current.

CD music is already implemented in the host (`host/sdl/winmm.c`): it plays the
`(Track NN).bin` files of one directory, found once at the first MCI call.

## Launcher

Runs in `main` before the guest's entry point and is shown on every start.

1. Find `dreams.ini` beside the executable; if that folder is not writable,
   in the per-user directory. Command-line options override it.
2. Validate the saved disc paths and open the launcher window.
3. Play is enabled once both discs validate. It saves `dreams.ini`, closes the
   launcher and starts the game. Closing the window exits.

### Window

One fixed-size window, usable with mouse, keyboard and gamepad (ImGui's
keyboard and gamepad navigation on).

| Area | Contents |
|---|---|
| Discs | For each of disc 1 and disc 2: the path, a Browse button (the OS picker, `SDL_ShowOpenFileDialog`, filtered to `.cue`/`.iso`), and a status line: found, missing, wrong disc, wrong edition, no music (`.iso`) |
| Port settings | Renderer, display, keyboard, gamepad and output sound: see [Port settings](#port-settings) |
| Data | The user data directory, with a button to open it in the file manager |
| Bottom | Play (default button, disabled until both discs validate) and Quit |

A picked image is validated at once; picking disc 2's cue in the disc 1 row
fills the disc 2 row instead. The first start is the same window with empty
disc rows.

### Port settings

The launcher configures only what the retail game does not: the port's own
additions. Retail settings stay where the game has them and are not shown,
copied or overridden here. (Owner decision, 2026-10-01.)

The rule that keeps the two apart: **a port setting changes only what the host
does with the game's output and the player's input. It never writes guest
memory, a retail file or a retail option.**

Retail settings, left to the game **[verified in code and `README.TXT`]**:

| Where | Setting |
|---|---|
| In-game Options | Real or 2D shadow, manual or automatic fight, volume max or min, cinemascope or full screen |
| Keys | F1–F6 game resolution, J joypad, K keyboard, Alt+5…0 cameras |

Port settings:

| Group | Setting | What the host does | Today |
|---|---|---|---|
| Renderer | Original (software) or new (GPU) | Presents the game's own software frame, or draws the scene with the GPU renderer of [spec 006](../006-recomp-glide-renderer/spec.md) | `WD_RENDERER=software\|direct` |
| Display | Windowed or fullscreen window, window scale, scaling filter, frame cap | Sizes and filters the presented image | `WD_FULLSCREEN`, `WD_SCALE`, `WD_FILTER`, `WD_FPS` |
| Keyboard | Key to key: which physical key stands for each key the game reads (for example W, A, S, D for the arrows) | Reports the game's key when the chosen key is pressed | fixed table in `host/sdl/user.c`; remapping is new |
| Gamepad | Mode; what gives direction (left stick, d-pad or both); which pad button stands for each joystick button or key; stick and trigger deadzones | Mode "game" exposes the pad as the joystick the retail code reads (J in game); mode "keys" turns the pad into key presses | `WD_PAD=winmm\|keys\|off`, `WD_DEADZONE`; direction source and button table are fixed, choosing them is new |
| Sound | Output mute | Silences the host mixer's output | `WD_MUTE` |

Names in the window avoid the retail ones they could be confused with:

- "Fullscreen window" is the desktop window mode. The game's "cinemascope /
  full screen" option is its letterbox and is unrelated.
- "Window scale" is the size of the presented image. F1–F6 still change the
  game's internal resolution.
- "Output mute" acts after the game's own volume option.

Remapping is at device level (owner decision, 2026-10-01): one physical input
stands in for another, and the host knows nothing about game actions.

- **Keyboard**: a table from physical key to the key the game sees. Mapping
  W, A, S, D to the arrows gives a modern layout; the game still reads the
  arrows. The launcher lists the keys the game uses (from `README.TXT`:
  arrows, Ctrl, Alt, Space, 1–3, 5–0, Esc, F1–F6, F10, J, K) with their retail
  meaning as a label only.
- **Gamepad**: the direction source (left stick, d-pad, or both) and a table
  from pad button to joystick button (mode "game") or to key (mode "keys").
  Choosing the d-pad makes it drive the same joystick axes or arrow keys the
  stick would.

With the default tables the port behaves as today. The retail build has no key
configuration of its own, so nothing is duplicated, and the J and K switch in
game keeps its retail meaning: it selects whether the game reads the joystick
or the keyboard, whatever the host feeds them from. One physical input cannot
stand for two targets; the launcher marks the duplicate and offers "reset to
defaults".

### How the launcher reaches the game

It does not reach the game at all. The recompiled game code sees only what it
saw in retail: files, input and sound devices. The launcher talks to the
**host**, through the interface the host already has.

The host takes every option from a `WD_*` environment variable today; that is
how `run.py` drives it. The launcher uses the same interface and nothing else:

```
launcher  ->  list of NAME=VALUE pairs  ->  process environment  ->  host
```

- The launcher's one entry point returns either "quit" or a list of `WD_*`
  name and value strings. It does not include a host header, call a host
  function or share a struct with the host.
- `main` puts each pair into the process environment, skipping names that are
  already set, then starts the host exactly as it starts today. This is the
  only code that knows both sides, and it is a loop over strings.
- The host does not know a launcher exists. Started by `run.py`, by a script
  or by the launcher, it reads the same variables.

So the contract between them is the list of `WD_*` names and their value
formats, which `recomp/README.md` already documents for `run.py`. This spec
adds to that list; it does not add a second mechanism:

| New variable | Value | Read by |
|---|---|---|
| `WD_DISC1`, `WD_DISC2` | Path of a `.cue`, `.iso` or extracted directory | `files.c`, `winmm.c`, `runtime.c` |
| `WD_DATA_DIR` | User data directory | `files.c` |
| `WD_KEYMAP` | `physical=game` key pairs, comma-separated | `user.c` |
| `WD_PADMAP` | `button=target` pairs, comma-separated | `winmm.c` |
| `WD_PAD_DIRECTION` | `stick`, `dpad` or `both` | `winmm.c` |

Consequences:

- Precedence falls out of the rule "skip names already set": a real
  environment variable beats `dreams.ini`.
- `dreams.ini` belongs to the launcher alone. The host never parses it, so its
  layout can change without touching the host.
- Removing the launcher from a build leaves a working program that is driven
  by environment variables, as now.
- Port settings are read once at start. Changing one means going back through
  the launcher; nothing is pushed into a running game.

One thing to check when implementing: the host reads some variables with C
`getenv` and others with `SDL_getenv`. On Windows these can see different
copies of the environment after a change made inside the process. The glue in
`main` must set the variables in a way both see, or the host's reads move to
one call. **[unverified]**

### Implementation

- **Dear ImGui**, pinned by commit in `recomp/render/cmake/Dependencies.cmake`
  beside SDL3 and sokol. The build is already C and C++17.
- **Isolated graphics stack** (owner decision, 2026-10-01). The launcher
  draws with SDL3's own 2D renderer (`SDL_Renderer`) through ImGui's
  `imgui_impl_sdlrenderer3` backend, with input through `imgui_impl_sdl3`. It
  shares nothing with the game's renderers: no `sokol_gfx`, no ODRender or
  ODGraphics, no shaders, no game window.
- **Why**: the launcher is where the player chooses the renderer, so it must
  open even when the GPU renderer cannot; a fault in either stack cannot take
  the other down; and the game's render code needs no launcher mode.
- **Font**: ImGui's built-in one. The launcher must draw before any disc is
  known, so it uses nothing from the game.
- **Lifetime**: the launcher creates its window, `SDL_Renderer` and ImGui
  context, and destroys all three before the game creates its window and
  renderer. Nothing is handed over except the validated paths and port
  settings. SDL itself stays initialised.
- **Source**: its own directory, `recomp/launcher/`, built as a static library
  that depends only on SDL3 and Dear ImGui and includes no host or render
  header. `main` calls its one function; see
  [How the launcher reaches the game](#how-the-launcher-reaches-the-game).
- The launcher reads and writes `dreams.ini` and never touches guest memory.

### Validation

For each image: parse the cue, read the ISO 9660 directory, and require
`DATA\1CD.ID` or `DATA\2CD.ID`. The marker decides which disc it is, so the
order the user gives them in does not matter. Both discs must be present, one
of each.

On disc 1, the SHA-256 of `GDIDREAM.EXE` must be
`b2f053bd26627eb618f034481fbeb49c2287bec834351787385a69d74db05001`. Anything
else is another edition (four retail editions exist, each with different
executables); the message names the supported one.

Saved paths are validated again on every start. An image that moved shows as
missing in its row, with the other path kept.

### `dreams.ini`

```ini
[discs]
disc1 = D:\games\dreams\Dreams to Reality (Europe) (Disc 1).cue
disc2 = D:\games\dreams\Dreams to Reality (Europe) (Disc 2).cue

[data]
dir =            ; empty: userdata beside the executable

[port]
renderer = software      ; software | gpu
fullscreen = 0
scale = 2
filter = pixelart
fps = 25
mute = 0

[gamepad]
mode = game              ; game | keys | off
direction = stick        ; stick | dpad | both
deadzone = 10,95
a = button1              ; pad button = joystick button (mode game) or key (mode keys)

[keyboard]
W = Up                   ; physical key = the key the game sees
```

Only port settings are stored here; the file holds no retail option. Defaults
are `run.py`'s. A `WD_*` environment variable or a command-line option wins
over the file for that run and is not saved.

### Command line and development use

`--disc1 <path> --disc2 <path> --data <dir>` set those values for one run and
are not saved. `--play` skips the launcher window and starts the game if the
discs validate, else prints the reason and exits non-zero; unattended runs and
shortcuts use it.

`run.py` keeps working: when `WD_READ_ROOTS` is set, or the first argument is
an executable path as today, the host uses the current directory-overlay
behaviour and the launcher is skipped. A disc path may also be an extracted
directory; music then needs the `(Track NN).bin` files as `WD_CD_DIR` does
today.

## Disc images

| Input | Support |
|---|---|
| `.cue` with one `.bin` per track (the Redump layout, as on archive.org) | yes |
| `.cue` with a single `.bin` | yes |
| Data track `MODE1/2352` or `MODE1/2048` | yes |
| `.iso` alone | yes, without music; the launcher says so |
| Extracted directory | yes, development only |
| `.zip`, `.chd`, `.mdf`, `.nrg` | no; the message says to unpack or convert |

New host code, none of it from the retail game:

- **Cue parser**: `FILE`, `TRACK`, `INDEX`, with `PREGAP` and `INDEX 00`
  honoured so audio starts at `INDEX 01`.
- **Sector reader**: for 2352-byte sectors the 2048 user bytes are at offset
  16.
- **ISO 9660 reader**: primary volume descriptor, directory records, names
  compared without case and without the `;1` version suffix. Read-only, with
  bounds checks on every extent.
- **Audio tracks**: raw 44.1 kHz 16-bit stereo, streamed from the image at the
  track's offset; the mixer already plays this format from the track files.

Check against the extracted trees: every file listed by the reader matches the
`DREAMS_DISC1`/`DREAMS_DISC2` tree in name, size and SHA-256.

## What the game sees

The guest keeps its retail view: a CD root and `\CRYO\DREAMS\`. The host's
file layer (`host/sdl/files.c`) resolves them as follows.

| Guest path | Resolved to |
|---|---|
| Relative, or at the CD root, for reading | User data directory, then the active disc |
| `CRYO\DREAMS\DATA\FULL.ID` | Never found |
| `CRYO\DREAMS\...` opened for reading | User data directory, then the active disc, then the other disc |
| Anything opened for writing, created or deleted | User data directory |

Why the read fallback: the game opens some files only under the install root.
`DATA\HD.ID` must exist or startup is fatal (both discs have it).
`DATA\3DC\DIALOG.DRD` is on disc 1 only, so the fallback must reach the other
disc. `data\anim\*.HNM` are the animated textures, which a retail mini install
lacks; the fallback gives every user what retail called "video-maps".

Two exceptions found when it first ran:

- **Saves never come from a disc.** Disc 2 ships `DATA\GAME\GAME.DAT`,
  `GAME0.DAT` and `GAME0.ICO`; its `GAME0.DAT` is 10,364 bytes where the
  retail game writes 11,388, the size that crashes `GAME_LoadGame`. Paths under `CRYO\DREAMS\DATA\GAME\` resolve in
  the user data directory only.
- **Markers and `FULL.ID` ignore the user data directory.** `DATA\1CD.ID` and
  `DATA\2CD.ID` are answered by the active disc alone, and
  `CRYO\DREAMS\DATA\FULL.ID` is not found even if such a file is put there.

Why no `FULL.ID`: it switches level files to the install root and makes the
game copy each section's file list there. Without it, level files come from the
CD root, `LEVEL.ID` is never used, and nothing is copied. Disc 1 has a
`DATA\FULL.ID`, so the fallback would find it; hence the explicit rule.

The user data directory is the host's existing write sandbox, moved: every
write goes there and every read looks there first, as today. It ends up
holding `CRYO\DREAMS\data\game\` (saves, save thumbnails) and whatever else
the game writes. A file placed there overrides the disc's.

### Active disc

Disc 1 is active at start (a new game starts on it, and only disc 1 has the
full `INTRO.HNM`).

The game asks about discs only by opening marker files
([level load](../../research/install-and-discs.md#level-load-disc-check-and-cache)):
`CD_GetDiscNumber` opens `DATA\1CD.ID` once to ask which disc is in, and
`CD_PromptSwap` opens the wanted disc's marker in a tight loop until it
succeeds. The host rule:

- A marker open succeeds only for the active disc's own marker.
- Two consecutive failed opens of the same marker, with no other file opened
  between them, switch the active disc; the second open then succeeds.

So the query sees the truth, and the prompt performs the swap. The game draws
"Please change to CD no %d" for one frame; that is accepted. **[verified]** in
scripted runs in both directions (plan.md, results):

- Disc 1 to 2: the query opens `1CD.ID` (found), the prompt's first poll of
  `2CD.ID` fails, its second switches and succeeds.
- Disc 2 to 1: the query's failed `1CD.ID` open is already the first miss, so
  the prompt's first poll switches.

`WD_DISC_ACTIVE=2` starts with disc 2 active; it exists for tests.

On a switch the CD audio device changes too: stop playback, take the new
image's track list. The game stops the music before the check and restarts it
after the load.

`DREAMS.DAT` needs nothing special: the game reads it from the active disc at
startup and on a new game, as in retail.

### CD audio changes

- Take tracks from the active image instead of scanning a directory once.
- `MCI_PLAY` without a start position and with no current track returns
  success and plays nothing, instead of the out-of-range error that produces
  the message box. What a real drive does here is not established; the retail
  code path exists in this edition only (later editions replaced it, see
  [binary-edition-comparison.md](../../research/binary-edition-comparison.md)).

## Platforms

Windows is the gate of this spec. User data is `userdata\` beside the
executable; the table gives the fallback when that folder is not writable.

| Platform | Fallback directory (`SDL_GetPrefPath`) | State |
|---|---|---|
| Windows | `%APPDATA%\<org>\<app>\` | recomp builds and runs |
| Linux | `~/.local/share/<app>/` | recomp builds and runs (WSL) |
| macOS | `~/Library/Application Support/<app>/` | build names exist (`-darwin`); never built **[unverified]** |

File names in the images are matched without case by the ISO reader, so the
case-walk in `files.c` is needed only for the user data directory.

## Work items

Milestones, checks, risks and open unknowns are in [plan.md](plan.md).

## Acceptance

- First start with no `dreams.ini`: the launcher shows empty disc rows and
  Play disabled; pick two cues in either order, press Play, reach the menu,
  music plays.
- Second start: the launcher shows both discs found; Play starts the game.
  `--play` starts it without the window.
- The launcher library links without the host and render libraries, and it
  opens and saves settings on a machine where the GPU renderer fails to start.
- The launcher can be driven with the keyboard alone and with a gamepad alone,
  apart from the OS file picker.
- A port setting changed in the launcher takes effect in that run and is there
  on the next start.
- With default port settings, the game receives the same key and joystick
  input as before this spec, and the four in-game Options and F1–F6, J, K
  behave as in retail under every port setting.
- With W, A, S, D mapped to the arrows, the game receives arrow keys from
  them. With direction set to d-pad, the d-pad moves the player and the stick
  does not.
- A moved image, a wrong edition, two copies of the same disc, and an `.iso`
  each give their own message.
- New game to the first level reads level files from disc 1 and writes nothing
  but `DATA\GAME\` in the user data directory.
- The Project26 → Project116 link (the one retail disc change) loads without
  waiting for input, and the music afterwards comes from disc 2's tracks.
- Loading a disc-2 save from the menu, and a disc-1 save while disc 2 is
  active, both load.
- Animated textures play in a level that has one (Project 12, `M01DRA`).

## Open questions

- Whether the one-frame "Please change to CD" text is visible enough to
  matter. If so, the host can switch on the first failed marker open made
  while that function is running, which needs a guest call-site check.
- The movie of a link that changes disc never plays. `SCENE_CheckExits` opens
  the destination's movie (`data\hnm\BIENMAL.HNM` for Project26 → Project116,
  on disc 2 only) before `CD_PrepareLevel` asks for the disc, so it is looked
  for on disc 1 and skipped; a new game started with disc 2 active skips
  `tete_e~1.hnm` the same way. By the decompiled order retail does the same.
  Letting `data\hnm\` reads fall back to the other disc would show these
  movies, which is a change from retail and an owner decision. Kept visible
  as a strict `xfail` in `tests/recomp/test_disc_play.py`.
- Which disc 1 pressings exist beyond the Redump European dump, and whether a
  second accepted hash is needed.
