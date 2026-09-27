# Disc location and file paths in `WINDREAM.EXE`

Scope: the original Windows game's disc selection, data roots, level manifests,
and file-cache paths that may inform `002-disc-navigation`. This review does
not cover VFS/BF internals or the `RES_ReadFile` format dispatch.

## Method and source check

I interpreted `WINDREAM.EXE` decompilation and call graphs with embedded
`[NAME]` / `[DOCS_SYNC]` comment blocks stripped, then compared that reading to
[disc-layout.md](../disc-layout.md), [boot-sequence.md](../boot-sequence.md),
[re-setup.md](../re-setup.md), and the manifest section of
[file-formats.md](../file-formats.md). The second binary source was the shipped
Disc 1 `WINDREAM.EXE` string data, read at its mapped virtual addresses. No
function names or Ghidra annotations were changed.

The code agrees with the existing description of the CD markers, FULL install
marker, level-manifest cache, and cache root. The decompilation adds operational
details below, especially how the game decides between the two discs and how
cache paths are resolved.

## Path setup and disc selection

| Function | Call graph | Observed behavior |
|---|---|---|
| `CD_InitPaths` `0x00427d0e` | Caller: `GAME_Init` `0x004156bf`. Calls `CD_FindDrive`, `CD_FindCacheDrive`, `CD_CheckFullInstall`, `FUN_00477953` twice, `_dos_setdrive_`. | Finds the CD root and install root, checks `FULL.ID`, calls `SetCurrentDirectoryA` through `FUN_00477953` first with the install root and then with the CD root, then sets the current drive to the CD root's drive letter. Thus relative manifest entries are read from the CD root after startup succeeds. `FUN_00477953` is not renamed here; its decompilation directly calls the imported `SetCurrentDirectoryA` and reports a failure through `FUN_00478722`. |
| `CD_FindDrive` `0x00427f11` | Caller: `CD_InitPaths`. Calls `fopen_`, `fclose_`, `SYS_FatalError`. | Tries drive letters `Z` down through `C`. For each, it opens `X:\DATA\1CD.ID`; if that fails, it tries `X:\DATA\2CD.ID`. The first openable marker wins and its drive letter is written to the first byte of the CD-root buffer at `0x0049ff2c`. If no marker opens, it calls the fatal-error path. It checks marker-file existence, not marker contents. |
| `CD_FindCacheDrive` `0x00427fc7` | Caller: `CD_InitPaths`. Calls `fopen_`, `fclose_`, `SYS_FatalError`. | Searches for `X:\CRYO\DREAMS\DATA\HD.ID` on `C` through `Z`, then `A` and `B`. On success it patches that drive letter into the install-root string at `0x004a0030` (`X:\CRYO\DREAMS\`). It is locating the installed/cache root, separately from the CD root. |
| `CD_CheckFullInstall` `0x00428752` | Caller: `CD_InitPaths`. Calls `FILE_GetInstallRoot`, `fopen_`, `fclose_`. | Opens `<install-root>DATA\FULL.ID`. It sets global `0x004a0134` to 1 when the file exists and 0 otherwise. The code does not read the marker bytes. |
| `CD_GetDiscNumber` `0x004287fa` | Caller: `CD_PrepareLevel`. Calls `CD_GetRootPath`, `fopen_`, `fclose_`. | Opens `<CD-root>DATA\1CD.ID`; returns 1 if it opens and 2 if it does not. It never checks `2CD.ID`. Therefore “anything other than disc 1 is disc 2” is the game's fallback, not safe validation behavior for a browser. |
| `CD_PromptSwap` `0x00428626` | Caller: `CD_PrepareLevel`. Calls `CD_GetRootPath`, `fopen_`, message/render functions. | Builds `<CD-root>DATA\1CD.ID` or `<CD-root>DATA\2CD.ID` from the requested number and polls until that marker opens. It is an interactive OS-mounted-disc wait loop. |

The executable strings independently confirm these paths: `X:\DATA\1CD.ID` and
`X:\DATA\2CD.ID` (assembled by `CD_FindDrive`),
`X:\CRYO\DREAMS\DATA\HD.ID`, `X:\CRYO\DREAMS\`,
`DATA\1CD.ID` (`0x004c4dcf`), `DATA\2CD.ID` (`0x004c4ddb`), and
`DATA\FULL.ID` (`0x004c4de7`). The standalone root string `X:\` is at
`0x0049ff2c`; the install-root string is at `0x004a0030`.

## Data root and asset-file paths

| Function | Call graph | Observed behavior |
|---|---|---|
| `FILE_GetInstallRoot` `0x004285be` | Callers include `CD_CheckFullInstall` and `DRD_Open` `0x0041072c`, plus save/load routines. | Returns the mutable install-root string `X:\CRYO\DREAMS\`; `CD_FindCacheDrive` changes its drive letter. This getter always names the install root. |
| `FILE_GetDataRoot` `0x004285e9` | Callers: `RES_ReadFile` `0x0041c666`, `ENT_LoadObject` `0x0041d624`, `ENT_LoadModel` `0x0041da7d`, and `FUN_00421345`. | Returns the CD-root buffer while `0x004a0134 == 0`; returns the install-root string when `FULL.ID` set that flag. This is the game's data-root switch. The loaders that consume it are outside this review's format-dispatch scope. |
| `FUN_00421345` `0x00421345` | Caller: `GAME_Init`. Calls `FILE_GetDataRoot` and `RES_InitArena`. | Formats the returned root with the binary string `%sdata\3dc\` (`0x004c479d`) into a global path buffer. This confirms at least one asset-related directory prefix is rooted at `FILE_GetDataRoot`; the later consumer is not identified here. |

The behavior matches the existing note that `FULL.ID` marks the maxi/full
installation, rather than proving that no disc is needed: `CD_PrepareLevel`
checks the currently mounted disc before entering its `FULL.ID`-gated cache
branch.

## Level manifests and cache copies

`CD_PrepareLevel` `0x00427d64` is called by `SCENE_LoadLevel` `0x0041f9db`.
For a nonzero current level number `n` from `SCENE_GetLevelNumber`, it computes
the required disc as `(n + 1) >> 1`, compares that with `CD_GetDiscNumber`, and
calls `CD_PromptSwap` on a mismatch. Independently, if `FULL.ID` is present and
`<install-root>DATA\LEVEL.ID` does not contain `n`, it deletes that cache ID,
purges selected cached files, copies the level manifest and common manifest,
then writes `n` back as a four-byte cache ID.

The manifest paths are formed from the CD-root buffer and the strings
`%sListL%d.txt` (`0x004c4c3a`) and `%sListL0.txt` (`0x004c4c48`). The
function passes both manifest paths to `CD_CopyFileList` with the install root
as destination. `CD_CopyFileList` `0x00428356` is called only from
`CD_PrepareLevel`; it opens the manifest in `rt` mode (`0x004c4d33`), scans
whitespace-delimited `%s` path tokens (`0x004c4d36`), and calls
`FILE_CopyIfMissing(token, install-root)` for each token. A failed copy purges
the cache, removes `LEVEL.ID`, and takes the fatal-error path.

`FILE_CopyIfMissing` `0x00428404` is called only by `CD_CopyFileList`.
Its destination format is `%s\%s` (`0x004c4daf`): the install root plus the
manifest token. It opens the token itself as the source path, which resolves
relative to the CD-root working directory established by `CD_InitPaths`. If
the source is absent, or if the destination already exists, it returns success
without copying. Otherwise it creates the destination and copies in blocks of
at most `0x8000` bytes; a failed/short read or write returns failure. Existing
destination bytes are not compared or refreshed.

On a cache miss, `CD_PurgeCache` `0x004281d9` removes matching
`<install-root>DATA\3DC\*.dsn`, `*.dan`, and `<install-root>DATA\ANIM\*.hnm`
files. The cache functions use `<install-root>DATA\LEVEL.ID` (format string
`%s\DATA\LEVEL.ID` at `0x004c4cc9`) to remember which level's manifest set was
copied. This is a selective level-asset cache, not a complete file inventory.

These findings agree with the existing manifest description: `LISTL0..4.TXT`
contain backslash-separated paths rooted at `DATA\`, and the loader copies
`ListL<n>.txt` plus `ListL0.txt` for a level. They add that the original uses
the manifest as a prefetch list, skips absent sources and existing destinations,
and does not use those lists as its general file index.

## Implications for spec 002

1. **Keep each image as an explicit disc root.** Recognize the known marker paths
   `DATA/1CD.ID` and `DATA/2CD.ID` case-insensitively. For browser identification,
   inspect both entries and report neither/both as unknown or ambiguous; do not
   copy `CD_GetDiscNumber`'s “missing 1 marker means Disc 2” fallback. File
   existence is the runtime signal; the game does not read marker contents.
2. **Build the browser tree from ISO directory records.** `LISTL<n>.TXT` and
   `LISTL0.TXT` enumerate only files the game chooses to prefetch for a level.
   They can support a future “used by level” grouping, but cannot replace the
   complete per-disc listing required by 002.
3. **Resolve game paths portably.** Preserve image spelling for display and
   diagnostics, while normalizing `/` and `\\` plus case for lookup. Existing
   manifests use paths such as `DATA\\3DC\\H03PAQUE.DSN`, and the strings in the
   executable show data paths rooted below `DATA`.
4. **Treat file access as explicit `(disc, path)` reads.** These functions rely
   on Windows drive probing, `SetCurrentDirectoryA`, `_dos_setdrive_`, and
   `fopen_`; they do not parse CUE files, enumerate an ISO, expose extents, or
   define bounded reads. CUE/raw-sector/ISO navigation in ODShared remains new
   portability code, as spec 002 already says.
5. **Do not port install-cache policy into 002.** `HD.ID`, `FULL.ID`,
   `FILE_GetDataRoot`, and the level cache explain where the original game
   reads files after installation. The browser's stated job is to inspect each
   source image, so neither Windows drive scanning nor cache installation is a
   prerequisite. Preserve the two roots and source-disc identity.
6. **Audio is outside these methods.** They select data roots and files; they
   do not locate audio tracks. The CUE track inventory remains the correct
   source for the metadata-only track list in 002.

The existing spec already covers two separate disc roots, marker-based identity,
case-insensitive lookup, preserved source names, and full ISO traversal. The
main spec detail to carry forward from this review is explicit handling of
unknown/ambiguous marker states and an optional manifest-based level grouping
that never substitutes for ISO enumeration.

## Validation record

- Read-only decompilation and call graphs, using the repository's
  `ghidra_scripts/Decompile.java` on `WINDREAM.EXE`; all headless invocations
  completed successfully with exit code 0. Command shape:
  `analyzeHeadless.bat ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly -scriptPath ghidra_scripts -postScript Decompile.java <addresses>`.
- Direct shipped-binary string checks used `llvm-strings.exe` and a PE RVA-to-file
  offset read against the configured Disc 1 `WINDREAM.EXE`. Confirmed exact
  strings at `0x004a0030` (`X:\CRYO\DREAMS\`), `0x0049ff2c` (`X:\`),
  `0x004c4daf` (`%s\%s`), `0x004c4c3a` (`%sListL%d.txt`),
  `0x004c4c48` (`%sListL0.txt`), `0x004c4dcf` (`DATA\1CD.ID`),
  `0x004c4ddb` (`DATA\2CD.ID`), `0x004c4de7` (`DATA\FULL.ID`), and
  `0x004c4cc9` (`%s\DATA\LEVEL.ID`).
- Compared with `docs/disc-layout.md`, `docs/boot-sequence.md`,
  `docs/re-setup.md`, `docs/file-formats.md`, and the 002 draft. No contradiction
  found. This was a static code/string review; no game execution or new runtime
  behavior was tested.
