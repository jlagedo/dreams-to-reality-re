# How the retail game locates assets

Current synthesis, checked 2026-09-26. The three focused reviews retain the
read-only decompilation, call-graph and corpus evidence:
disc paths,
VFS and BF, and
asset dispatch. Function names below
are checked in [`re/names/WINDREAM.EXE.tsv`](../../re/names/WINDREAM.EXE.tsv).
The asset-dispatch review stripped Ghidra comments before interpreting code but
had seen prior documentation; its existing function names rely on their earlier
two-source registry evidence, not a new blind naming claim.

## The boundary: game file access and disc images

The Windows game reads OS-mounted files through Watcom/Win32 calls. It has no
CUE parser, raw-sector reader or ISO 9660 directory walker. Those are new
portable `ODShared` facilities for opening the user's original images.
`CD_FindDrive` (`0x00427f11`) searches `Z:` through `C:` for
`DATA\1CD.ID` or `DATA\2CD.ID`; `CD_FindCacheDrive` (`0x00427fc7`)
separately finds `X:\CRYO\DREAMS\DATA\HD.ID`. `CD_InitPaths`
(`0x00427d0e`) establishes the install and CD roots and changes the process
working directory. None of that drive scanning belongs in the browser.

`CD_CheckFullInstall` (`0x00428752`) tests `DATA\FULL.ID` at the install root.
`FILE_GetDataRoot` (`0x004285e9`) selects the CD root unless that marker sets
the full-install flag, then selects the install root. `FILE_GetInstallRoot`
(`0x004285be`) always returns the patched install-root path. The marker tests
open files but do not inspect their bytes. `CD_GetDiscNumber` (`0x004287fa`)
returns 1 if the CD-root `1CD.ID` opens and 2 otherwise; it does not verify
`2CD.ID`. A source browser must instead check both markers and report a missing
or ambiguous identity.

`CD_PrepareLevel` (`0x00427d64`) checks the disc requested by a level and may
copy a selective set of files listed in `ListL<n>.txt` and `ListL0.txt` to the
install cache through `CD_CopyFileList` (`0x00428356`) and
`FILE_CopyIfMissing` (`0x00428404`). These manifests are prefetch lists, not
a complete disc index. ODViewer must enumerate ISO records directly and keep
the two source roots separate. The cache and fatal disc-swap loop are runtime
policy, not browser navigation requirements.

## UBIK member access

`UI_InitIcons` (`0x004341eb`) calls `BF_Mount` (`0x0043ad40`) on
`DATA\ICONE\ICONES.BF`, then `SPR_LoadIconBanks` (`0x00426c46`) loads five
fixed member names. The Disc 2 archive has a sixth member, `TITRES.SPR`, so
the executable's load list is not a complete archive inventory.

`BF_Mount` checks `UBIK`, reads a table offset and count from its 16-byte
header, then reads 267-byte rows: 259 name bytes, `u32` payload offset and
`u32` payload length. The runtime expands these into 0x11c-byte records with
cursor and handle state. The on-disc rows are the browser's index; native code
must check table, name and extent bounds instead of copying a runtime struct.
Direct parsing of both original `ICONES.BF` files found 5 and 6 valid rows,
respectively, whose extents fit their files.

`VFS_AddArchiveEntries` (`0x0043ac08`) adds members to a process-global table.
`VFS_FindMember` (`0x0043ab96`) finds an exact name for replacement, so a later
archive can overwrite an earlier member in that game table.
`VFS_OpenMember` (`0x0043af27`) uppercases a requested name and compares it
with a 0x103-byte bound, then resets that member's logical cursor. The
previous registry descriptions called these archive lookups and called the
latter a prefix match. The disassembly's 0x103-length `strncmp_` and the
NUL-terminated corpus names establish exact member matching for these files.
A request longer than 259 bytes sharing a stored 259-byte prefix could match;
the DOS independent review records that
unobserved edge case.

`VFS_Open` (`0x00409304`) tries an OS file first and only falls back to a
registered member on an eligible failed read-only open; the member handle is
`-(index + 1)`. `VFS_Read` (`0x004093f0`) clamps member reads to the declared
length, and `VFS_Seek` (`0x00409634`) maintains a member-relative cursor and
seeks the shared backing descriptor. `VFS_Close` (`0x0040985e`) validates a
member handle without closing that descriptor. The game's global table,
overwrite precedence, shared cursor, unusual EOF clamp and fatal errors are
not suitable for independent browse selections. ODViewer can preserve the
useful contract: enumerate each source `.BF`, give each member a stable
`(disc, archive path, row index)` identity with its stored name, and bound
reads to its extent. The row index distinguishes duplicate names if present.

## Project records and contextual resources

`DDAT_Load` (`0x00448f5f`) opens `DREAMS.DAT`; `DDAT_LoadRecord`
(`0x00449bf9`) selects one of 150 `ProjectN` records and calls
`RLE_UnpackZeros` (`0x00448e25`) to obtain its 0x2200-byte record. Each disc
has its own bank, and six corresponding records differ between the discs.
`OBJET0 +0x0c` names the scene; later `OBJET` slots name actors or props.
`LINK` records refer to other projects. A `LINKADVENT` opcode `0x40` uses
`+0x1c` as a one-based dialogue ID; the runtime subtracts one to select a
zero-based DRD entry. `+0x2c` can hold a cutscene filename. The browser can
index these references per disc without choosing a global winner between the
two project banks. Resolve a referenced filename against both mounted source
roots and report all candidate matches with their own provenance; neither
disc's copy silently wins.

`ENT_LoadModel` (`0x0041da7d`) probes the stem of a requested object under
`DATA\3DC` for `.DSN` and `.DAN`. `RES_Load` (`0x00456e24`) allocates an engine
handle, calls `RES_ReadFile` (`0x0041c666`), and relocates the result.
`RES_ReadFile` changes meaning with its active container:

| Active context | Logical request | Retail reader | Browser-visible metadata |
|---|---|---|---|
| DSN | `.3DC`, `.3DI` | `DSN_LoadMaterialsAndFaces` (`0x004177f5`), `DSN_LoadVertexPool` (`0x004178fe`) | Tag 1 render graph and tag 2 collision payload |
| DSN | named `.3DM` | `DSN_Create3DM` (`0x00417a07`), later `DSN_LoadTextures` (`0x00417afd`) | Names in the DSN's fixed 11-byte map directory; no standalone texture file |
| DAN | `.3DC` | `DAN_Read3DC` (`0x0041020f`) | Type-1 model chunk |
| DAN | named `.3DM` | `DAN_Load3DM` (`0x0041053e`) | Names in the DAN's 11-byte directory |
| DAN | named `.3DA` | `DAN_Load3DA` (`0x004105eb`) | Labels in its 13-byte animation directory; no shipped physical `.3DA` files |
| no matching container route | physical path | raw branch of `RES_ReadFile` | Source file path, independent of the logical names above |

`DAN_OpenArchive` (`0x0040fff7`) reads its two name directories, and
`DAN_ReadAnimChunks` (`0x00410435`) reads the type-3 animation chunks.
`ANIM_LoadEntitySet` (`0x00404d98`) maps clip labels to runtime slots. These
methods demonstrate why a flat extension list cannot locate every asset:
logical resource names are resolved inside the currently open DSN or DAN.
In 002, show the container, names, tag/type and source provenance; resource
decompression, relocation and preview are later loader work.

## Dialogue and sound banks

`DRD_Open` (`0x0041072c`) opens `DATA\3DC\DIALOG.DRD`, checks `DRDF`, and
keeps its offset table. `DRD_LoadEntry` (`0x00410928`) reads one selected entry
on demand. The bank has 178 zero-based entries. Each can contain tag 2 WAVE,
tag 3 timed caption lines and tag 4 portrait data. Direct source bytes confirm
the first entry header starts at `0x2dd`; its RIFF payload starts at `0x2eb`.
Older format summaries incorrectly used `0x2eb` as the entry start. The table
starts at `0x15`, after a five-byte tag/size block header, and stores ordinary
little-endian `u32` absolute offsets. Reading from `0x14` caused the earlier
false 24-bit-wrap interpretation.

`FSB_Load` (`0x00426143`) opens `DATA\SOUND\FSB.DAT`, checks the 12-byte
`DREAMS FSB  ` marker, reads the clip count and size table, then reads the
contiguous WAVE blob. `FSB_GetSample` (`0x0042639d`) indexes its runtime clip
table. The disc bank has 24 clips. A browser needs only validated clip extents
and numbers for 002; decoding or playing the PCM belongs to later previews.
Music is separate: it lives in CUE audio tracks rather than ISO files.

## Contract for spec 002

Keep the raw per-disc ISO tree as the complete source inventory. Add a per-disc
logical catalog beneath each physical container: UBIK members, project records
and their references, DSN/DAN names and tag/type entries, DRD entries, and FSB
clips. A browser entry identifies its disc, physical path, and indexed internal
key (not just a possibly duplicated name);
an absent or ambiguous target stays visible as unresolved. Internal names must
not be presented as physical files. List metadata and perform bounded reads
on demand; no image, model, audio or video preview is implied by indexing.

The original routines are behavioral evidence, not byte-for-byte portable
interfaces. A native read-only implementation needs owned source lifetimes,
64-bit bounds checks and recoverable errors. Adapted ports of recovered
functions are recorded in `opendreams/port-map.tsv` when implemented; the new
CUE/raw-sector/ISO layer has no original function address.

## Validation and remaining limits

The three reviews used read-only Ghidra decompilation and call graphs. The VFS
review also checked the exact-compare instruction bytes and parsed both
original icon banks; the disc-path review checked executable strings; the
asset-dispatch review checked project, DAN, DRD and BF data against existing
Python readers. A direct byte check of Disc 1 `DIALOG.DRD` found entry 0's
tag-1 header at `0x2dd` and `RIFF` at `0x2eb`. After the VFS name correction,
`uv run python re/tools/check_names.py WINDREAM.EXE --renames --twin GDIDREAM.EXE`
passes 691 names and confirms these two function bodies match the GDI build.
The corresponding DOS/3dfx `DREAMSFX.EXE` function at `0x00058ec4` was checked
separately in the blind DOS review:
instructions `0x58ed9`–`0x58eed` set `EBX=0x103` and call
`strncmp_`, then `0x58efb`/`0x58f0b` set the member-open flag and zero its
cursor. Its checked name is also `VFS_OpenMember`.
These are static and corpus checks; no native asset-browser implementation or
original-game execution was validated by this review.

The full lifetime of the retail BF backing
descriptor, and source-level boundaries of some neighboring loaders remain
unverified. They do not block the 002 metadata index because it can check
file extents directly, own its sources and report unsupported records.
