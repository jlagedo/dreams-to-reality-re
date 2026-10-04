# Install, data roots and the disc swap

Checked 2026-10-01 against `WINDREAM.EXE` (read-only Ghidra decompilation),
both discs' `SETUP.INI`, `INST_SON.BAT`, `LISTL*.TXT` and `DREAMS.DAT`, and
the file log of a recomp run; updated 2026-10-03 (the cache purge deletes,
`FULL.ID` in a single tree, the merged root's disc number). Function names are in
[`re/names/WINDREAM.EXE.tsv`](../../re/names/WINDREAM.EXE.tsv). Disc contents
and the merge map are in [disc-layout.md](disc-layout.md); asset lookup inside
the files is in [asset-access.md](asset-access.md).

The swap prompt was exercised in the recomp with a patched project bank (see
[Tested in the recomp](#tested-in-the-recomp)); the real Project26 →
Project116 transition has not been played through. The plan for the port
built on this page is [spec 007](../specs/007-port-launcher/spec.md).

## What the installers copy

Everything comes from disc 1. The game hardcodes `X:\CRYO\DREAMS\` and patches
only the drive letter, so the install must be `\CRYO\DREAMS` at a drive root.
**[verified]**

| Installer | Copies |
|---|---|
| Windows mini (`SETUP.INI`) | `setup.exe`, `setup.ini`, `uninstal.exe`, `windream.exe`, `gdidream.exe`, `DATA\HD.ID`, `DATA\REPLAY.BIN`, `DATA\3DC\DIALOG.DRD` |
| Windows maxi | Mini plus `DATA\FULL.ID` and 20 common `.3DC`/`.3DM` files (list in [disc-layout.md](disc-layout.md)) |
| DOS (`INSTALL.BAT` → `INST_SON.BAT`) | The disc's root files, `data\hd.id`, `data\replay.bin`, `data\3dc\dialog.drd`, `data\3dc\*.3dc`, `data\3dc\*.3dm`, `data\sound\*.*`, `data\univbe\*.*`; then runs `setsound.exe` |

The DOS installer's "full" choice only runs
`copy data\hd.id data\full.id`. The installers create `DATA\GAME` and
`DATA\ANIM` empty; the game fills them (saves, cache).

### What `SETUP.EXE` does

From a full read-only decompilation of `SETUP.EXE` (a debug MSVC build that
statically links CryoLib, so the `GL_*` helpers keep their names; the
application functions are unnamed). **[verified]**

`SETUP.EXE` is installer, launcher and demo browser in one. Its main
function (`0x40d431`) branches on the registry key
`HKEY_CURRENT_USER\Software\Cryo\<AppName>\GameDirectory`, where `<AppName>`
is `SETUP.INI`'s `[AppName]`, "Dreams to Reality":

- **Key absent: install.**
  1. Dialog (`0x40bf66`): mini or maxi, and a drive chosen from the fixed and
     removable drives (`GetDriveTypeA` 3 or 2), preselecting the one with the
     most free space. The directory starts as `[GET DIR]` (`C:\CRYO\DREAMS`)
     with the chosen drive letter. Free space is checked against
     `[PLACE NECESSAIRE ...]`.
  2. `0x40c35c` creates the directory, creates the registry key and stores the
     install directory as its default value, then creates a Program Manager
     group named after the game with one item, `<dir>\<AppExe>`, over DDE
     (`[CreateGroup(%s)]`, `[AddItem(%s,%s)]`).
  3. `0x40c639` creates the `[DIRECTORY ...]` folders under the install
     directory and copies each `[FILES TO COPY ...]` entry from the current
     directory (the CD) to the same relative path, in 0x800-byte blocks, with
     no transformation.
  4. It then starts `<dir>\setup.exe` with `ShellExecuteA` and exits. A failed
     copy deletes the registry key and returns to the dialog.
- **Key present, started from a CD-ROM drive** (`GetDriveTypeA` of the current
  drive is 5): change to the install directory, start `<dir>\setup.exe`, exit.
- **Key present, started from the hard disk:** look on every CD-ROM drive
  (letters C to Z) for `[AppExe]` (`windream.exe`, which only disc 1 has),
  prompting "Insérer le CD 1." until found, then show the launcher screen.
  Its buttons: install DirectX (`[DIRECTX INSTALL]`), play, quit, demos
  (`DEMOS1`/`DEMOS2`, prompting "Insérer le CD 2." when a demo file is
  missing), help, extras (`[PlusDir]`), and the web page.
- **Play:** change drive and directory to the install directory and
  `ShellExecuteA` `[AppExe]` with no arguments.

So the launcher always starts `windream.exe` from the install directory, with
disc 1 in the drive. Nothing else is passed to the game: `WINDREAM.EXE` and
`GDIDREAM.EXE` import no registry functions (no `ADVAPI32`), take their paths
from the marker-file scan below, and reset the working directory themselves.
The registry key and the program group matter only to `SETUP.EXE` and
`UNINSTAL.EXE`.

The install dialog reads the directory back from a dialog control, so a user
may have been able to change it; whether that control is editable was not
checked. The game cannot run from anywhere but `\CRYO\DREAMS` at a drive
root, because it finds its install only through `X:\CRYO\DREAMS\DATA\HD.ID`.

Reproduce: `uv run python re/tools/ghidra_headless.py -process SETUP.EXE
-noanalysis -readOnly -postScript DecompileAll.java out/tmp/setup`, then
search the output for `GameDirectory`, `FILES_TO_COPY`, `GetDriveTypeA` and
`ShellExecuteA`.

## Startup: three roots

`CD_InitPaths` (`0x427d0e`), called from `GAME_Init`: **[verified]**

1. `CD_FindDrive` (`0x427f11`) takes the first drive from `Z:` down to `C:`
   where `X:\DATA\1CD.ID` or `X:\DATA\2CD.ID` opens. Either disc boots. None:
   fatal error.
2. `CD_FindCacheDrive` (`0x427fc7`) takes the first drive from `C:` up to
   `Z:`, then `A:`, `B:`, where `X:\CRYO\DREAMS\DATA\HD.ID` opens. None: fatal
   error.
3. `CD_CheckFullInstall` (`0x428752`) sets the full-install flag `0x4a0134`
   when `DATA\FULL.ID` opens at the install root.
4. `chdir` to the install root, `chdir` to the CD root, `_dos_setdrive_` to the
   CD drive. The working directory is the CD root from here on.

The marker files are only opened, never read.

| Root | Chosen by | Data |
|---|---|---|
| Install, always | `FILE_GetInstallRoot` (`0x4285be`) | `DATA\3DC\DIALOG.DRD` (`DRD_Open`), saves `data\game\game.dat`, `game<n>.dat`, `game<n>.ico`, `data\replay.bin`, `DATA\LEVEL.ID`, the level's animated texture `data\anim\<name>.HNM` (`SCENE_StartAnimTexture`, `GAME_TickFrame`) |
| Data root: CD on a mini install, install on a full one | `FILE_GetDataRoot` (`0x4285e9`) | `data\3dc\` scenes, models and raw resources (`ENT_LoadModel`, `ENT_LoadObject`, `RES_ReadFile`, and the base path set at `0x421345`) |
| Working directory = CD, always | relative paths | `dreams.dat`, `data\hnm\*.hnm` cutscenes, `data\sound\fsb.dat`, `data\icone\icones.bf`, `DATA\FONT\*.SPR`, `data\objet\*`; music is CD audio through MCI |

The first two rows are from the decompiled callers. The third is from the
recomp's file log (`[files] open` lines in `run/stderr.txt`), where those
names arrive without a root prefix; not every loader was decompiled for it.

Two consequences:

- Animated textures are opened from the install root only. A mini install has
  no `DATA\ANIM\*.HNM`, the open fails and the level runs without them. This
  is `README.TXT`'s "the maximum version also offers video-maps".
- `DDAT_Load` (`0x448f5f`) reads all of `dreams.dat` (0x25400 bytes) from the
  working directory at `GAME_Init` and again from `BOOT_Run`, not at a swap.
  The session keeps the records of the disc mounted at that moment. Six
  records differ between the discs (31, 41, 55, 69, 75, 87); none changes a
  level number.

## Level load: disc check and cache

`CD_PrepareLevel` (`0x427d64`) is the only caller of the swap prompt and of
the cache copy. `SCENE_LoadLevel` calls it on every level load. **[verified]**

```
n = current record +0x1fc            (SCENE_GetLevelNumber 0x41ad5a)
if n == 0: return
stop CD music                        (MGM 0x1f)
want = (n + 1) >> 1
have = 1 if <CD>\DATA\1CD.ID opens else 2      (CD_GetDiscNumber 0x4287fa)
if want != have: CD_PromptSwap(want)
if full install and LEVEL.ID != n:
    delete LEVEL.ID; CD_PurgeCache
    draw "Please wait while loading ..."
    CD_CopyFileList(<CD>\ListL<n>.txt, install root)
    CD_CopyFileList(<CD>\ListL0.txt,   install root)
    write n to LEVEL.ID
```

### Level numbers

`+0x1fc` is a section of the game, 0 to 4, not a project index. Both discs'
`DREAMS.DAT` agree:

| `+0x1fc` | Projects | Disc |
|---:|---:|---|
| 0 | 1 (Project10, `H03PAQUE.DSN`, the menu scene) | no check |
| 1 | 44 | 1 |
| 2 | 28 | 1 |
| 3 | 43 | 2 |
| 4 | 34 | 2 |

Every `.DSN`, `.DAN`, `.3DC`, animated-texture and cutscene file a project
names, where the file exists on either disc, is on that project's own disc.
**[verified]**

Only three links cross a level number, one of them a disc boundary:

| Link | Levels | Discs |
|---|---|---|
| Project84 (`E25_RIDE`) → Project44 | 1 → 2 | 1 → 1 |
| Project26 (`E01GROTT`) → Project116 | 2 → 3 | **1 → 2** |
| Project113 (`F38ARENE`) → Project135 | 3 → 4 | 2 → 2 |

### What triggers the swap

A level load whose `(n + 1) >> 1` differs from the mounted disc:

- the Project26 → Project116 link, once per playthrough;
- loading a save made on the other disc's half (`GAME_LoadGame` ends in a
  level load);
- a new game with disc 2 mounted (Project 0 is level 1).

A full install does not avoid it: the check runs before the cache branch, and
the disc still supplies `ListL<n>.txt`, the cutscenes and the music.

`CD_PromptSwap` (`0x428626`) draws "Please change to CD no %d", pauses CD
audio (MGM 0x22), then loops on `fopen` of `<CD>\DATA\1CD.ID` or `2CD.ID`
with no delay and no way out, on the drive letter found at startup. When it
opens it resumes audio (MGM 0x23).

### The full-install cache

- `CD_PurgeCache` (`0x4281d9`) walks `DATA\3DC\*.dsn`, `DATA\3DC\*.dan` and
  `DATA\ANIM\*.hnm` under the install root and deletes each: the callee
  `0x477ba8`, which Ghidra mislabels `chdir_`, is a `jmp 0x487844`, and
  `0x487844` calls `DeleteFileA` through the import slot `0x49c30c`.
  **[verified in code]** (2026-10-03)
- `CD_CopyFileList` (`0x428356`) reads the list with `fscanf`, one path per
  token, and calls `FILE_CopyIfMissing` (`0x428404`) for each. A missing list
  file is not an error.
- `FILE_CopyIfMissing` returns success without copying when the source does
  not open or the destination already opens for reading; otherwise it copies
  in 0x8000-byte blocks. A failed copy purges the cache, deletes `LEVEL.ID`
  and is fatal.

The lists are identical on both discs. Unique files and their size on the
level's own disc:

| List | Lines | Unique | Size |
|---|---:|---:|---:|
| `LISTL0.TXT` (always) | 6 | 6 | 3.8 MB |
| `LISTL1.TXT` | 241 | 115 | 66.8 MB |
| `LISTL2.TXT` | 166 | 79 | 48.7 MB |
| `LISTL3.TXT` | 222 | 94 | 71.4 MB |
| `LISTL4.TXT` | 164 | 69 | 41.3 MB |

Each list names three to seven files present on neither disc (for example
`EPEE.DAN`, `ARC.DAN`, `SUR.3DC`); the copy skips them. The sizes match the
installer's "100 MB" for maxi.

## Tested in the recomp

2026-10-01, `run.py --headless --tag <t> --seconds 45 --keys
2000:ESC,5000:RETURN,8000:ESC,20000:ESC`, with a `dreams.dat` in the tag's
sandbox whose Project0 record has `+0x1fc` set to 3 instead of 1 (the packed
record keeps its length). **[verified]**

| Read roots | Observed in `stderr.txt` |
|---|---|
| Default (disc 1, install tree) | After `DATA\1CD.ID` opens, `DATA\2CD.ID` is opened and fails for the rest of the run: the game is stuck in `CD_PromptSwap` |
| `--read-roots "<install parent>;<disc 2>"` | `DATA\2CD.ID` opens on the first try; then `LEVEL.ID` is read, `ListL3.txt` is opened and its files are copied into the sandbox (65 MB), as the full-install branch describes |

In the second run an "MCI Error" message box follows the prompt ("The
specified parameter is out of range"). `CD_PromptSwap` resumes CD audio with
MGM 0x23, an `MCI_PLAY` with no start position, and the host's MCI bridge
rejects it when no track is current. What a real drive answers there is
**[unverified]**.

## Seen in play

2026-10-01, the recomp in disc mode driven through the development control
channel (`tests/recomp/test_disc_play.py`). **[verified]**

- **The Project26 → Project116 link has no box** (`lo` = `hi` = 0). It fires
  when `SCENE_IsTriggerDone` (dword `0x6155e4`) is non-zero, that is when the
  level's fight is won. Project113 → Project135 is the same kind. Project84 →
  Project44 has a box and fires on entering it.
- **Order at a disc-changing link**: stop CD audio, try to open the
  destination's movie (`data\hnm\BIENMAL.HNM`, on disc 2 only), then
  `CD_PrepareLevel`. The movie is asked for while disc 1 is still in, fails
  and is skipped.
- **Saves are autosaves.** The in-game system page lists Load, Options and
  Quit: `MENU_HandleSystemPageInput` (`0x431603`) steps over entry 1 (Save).
  `GAME_StartLevel` writes a save on every level entry, in a slot named after
  the level. `game.dat` is 340 bytes, `game<n>.dat` 11,388, `game<n>.ico`
  8,192. `game<n>.dat` holds the project name at `+0x2884`; a load reads that
  record again by name, then goes through `CD_PrepareLevel`, so a save from
  the other disc's half triggers the swap.
- **In-game Quit ends the process**; there is no return to the main menu.
- **Music**: a level's track is the low byte of record `+0x11c` (editor
  label "Scene CD Track"; `GAME_StartLevel` masks it with `and edx, 0xff`
  at `0x42f1b4` before `CD_SetPlaylist`, which could take three tracks, one
  per byte). Project0 has 9, Project116 has 2, Project113 has 0 (no music).
  The numbers index the mounted disc's tracks.

## What a port needs

The original layout is two removable discs plus a hard-disk directory. On
Windows, macOS, Linux and the web the host can present all three roots from
one data set:

- **CD root**: both discs merged (conflicts in [disc-layout.md](disc-layout.md)),
  with both `1CD.ID` and `2CD.ID` present; without `2CD.ID` a level 3 or 4
  load hangs in the prompt, without `1CD.ID` a level 1 or 2 load does.
  `CD_GetDiscNumber` (`0x4287fa`) tests `1CD.ID` alone, so the merged root
  always reads as disc 1: every level 3 or 4 load enters `CD_PromptSwap(2)`,
  which draws "Please change to CD no 2" and presents it (`VID_Swap`) before
  its first `fopen` of `2CD.ID` succeeds (seen in the test above). The prompt
  is therefore on screen for one frame per disc-2 level load, where the real
  disc 2 shows nothing; avoiding it means answering the disc number from the
  record's `+0x1fc` (a host replacement of `0x4287fa`). **[verified in code;
  how long the frame stays visible unverified]** The merged root
  keeps disc 1's `DREAMS.DAT` (2026-10-03): it is the final bank (dated
  1997-10-29, the day `WINDREAM.EXE` was linked; disc 2's is 10-08), and
  since `DDAT_Load` reads the bank only at start, it is also the one a
  session started from disc 1 plays for every level. Reasons and the rule for
  the other differing files: [disc-layout.md](disc-layout.md#merging-both-discs),
  [spec 008](../specs/008-editor-restoration/spec.md#b1-decisions).
- **Install root**: a writable directory for `DATA\GAME` and `LEVEL.ID`, with
  `HD.ID`, `DIALOG.DRD` and `REPLAY.BIN` readable. For animated textures,
  `DATA\ANIM` must resolve under it.
- **`FULL.ID`**: must be hidden when one tree serves as both the CD root and
  the install root. Without it, level files come from the CD root, nothing is
  copied, and `LEVEL.ID` and `ListL*.txt` are never opened (`ListL*.txt` are
  read from the CD root, and only in full-install mode). With it,
  `CD_PrepareLevel` finds `LEVEL.ID` absent or different on the first level
  load (absent reads as 0) and runs `CD_PurgeCache` before the copy, which
  deletes `DATA\3DC\*.dsn`, `*.dan` and `DATA\ANIM\*.hnm` under the install
  root, that is, in the shared tree. The copy then reads the deleted files as
  its sources, and `FILE_CopyIfMissing` treats a missing source as success, so
  the tree loses its level files. **[verified in code; the end state of the
  load after the purge (fatal error or crash) unverified]** The browser build
  and disc mode already hide `FULL.ID` (`files.c`). (Corrected 2026-10-03: this
  entry said either way works.)
- **Music**: redbook tracks, 11 on disc 1 and 13 on disc 2, selected by track
  number. The host has to pick the track set from the level's disc.
- **Web**: `ListL<n>.txt` plus `ListL0.txt` is the per-section file set, a
  ready prefetch list; saves need persistent storage.

State of the recomp on 2026-10-01: `run.py` gives the guest disc 1's extracted
tree and the parent of `DREAMS_INSTALL_ROOT` as read roots and a sandbox for
writes. Disc 2 is not mounted, so a level 3 or 4 load would spin in
`CD_PromptSwap`.

## Reproducing

```sh
uv run python re/tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis -readOnly -postScript Decompile.java 00427d0e 00427d64 00427f11 00427fc7 004281d9 00428356 00428404 00428626 00428752 004285e9 004287fa
```

Level numbers per project (run with `uv run python`, for each disc):

```python
import struct
from collections import Counter
from dreams import paths
from dreams.formats import project

recs = project.records(paths.disc(1) / "DREAMS.DAT")
print(Counter(struct.unpack_from("<i", r, 0x1FC)[0] for r in recs))
```

Which root served each open in a recomp run:
`grep "^\[files\] open" out/recomp/windream/run/stderr.txt`.
