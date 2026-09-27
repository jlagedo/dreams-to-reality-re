# RE review: .BF archive access for 002

This review covers the original WINDREAM.EXE path that mounts UBIK .BF files
and reads their named members. It does not cover disc/CD lookup, cache
resolution, or the decoders that consume member data.

## Evidence method

I first interpreted the Ghidra function bodies and call graph, ignoring injected
[NAME] and [DOCS_SYNC] plate text. I checked the ambiguous member comparison
against the original x86 instructions. I then compared that reading with the
independent on-disc format measurements in [file-formats.md](../file-formats.md)
and [assets.md](../assets.md), plus the runtime use in
[sprites-ui-dialog.md](../sprites-ui-dialog.md). No Ghidra labels, registry
entries, project data, or shared documentation were changed.

The repository Ghidra project could not be locked by headless Ghidra. I copied
the project to %TEMP%\dreams-vfs-review and opened that copy with
-readOnly; the repository project remained untouched.

## Recovered path

| Function | Address | Observed job |
|---|---:|---|
| BF_Mount | 0x0043ad40 | Opens and parses one UBIK .BF, then registers all members. |
| VFS_AddArchiveEntries | 0x0043ac08 | Adds rows to the global member table; exact-name duplicates are replaced. |
| VFS_FindArchive | 0x0043ab96 | Finds an exact member name in that table. |
| VFS_OpenArchive | 0x0043af27 | Finds and initializes one member row; despite its name, it does not mount a .BF. |
| VFS_Open | 0x00409304 | Tries the OS file open first, then falls back to a registered member. |
| VFS_Read | 0x004093f0 | Reads a bounded slice through either a normal file descriptor or a member handle. |
| VFS_Seek | 0x00409634 | Seeks a normal file or updates/seeks the selected member. |
| VFS_Close | 0x0040985e | Closes OS descriptors; a negative member handle is only validated. |
| VFS_FreeArchives | 0x0043aefc | Frees the global table allocation. |

BF_Mount has one direct caller in the analyzed call graph: UI_InitIcons
(0x004341eb). It mounts DATA\ICONE\ICONES.BF, then SPR_LoadIconBanks
(0x00426c46) opens five fixed names from the executable's name table and loads
their contents. That caller does not enumerate arbitrary members. The recovered
VFS is therefore an icon-bank loading path, not a general asset browser.

## Container and runtime table

BF_Mount reads a 16-byte header, checks only the four-byte UBIK magic, seeks to
the u32 table offset at header +0x08, and reads the u32 entry count at +0x0c.
It does not validate the version at +0x04. Each on-disk directory row is 267
bytes:

| On-disk row offset | Field |
|---:|---|
| +0x000 | 259-byte member name |
| +0x103 | u32 absolute payload offset |
| +0x107 | u32 payload length |

The stream reads the name, offset, and length consecutively. The runtime uses an
aligned 284-byte (0x11c) record per member at global table pointer 0x004a2f94,
with count at 0x004a2f98:

| Runtime row offset | Field |
|---:|---|
| +0x000 | 259-byte name |
| +0x103 | alignment byte |
| +0x104 | u32 payload offset |
| +0x108 | u32 payload length |
| +0x10c | opened/usable flag |
| +0x110 | member-relative logical position |
| +0x114 | value copied from BF_Mount's path-selection flag; purpose in later code is unverified |
| +0x118 | backing VFS descriptor, shared by every member from this mount |

The raw 267-byte row layout agrees with the independent format description.
The difference between the 267-byte on-disk stride and the 284-byte runtime
stride is alignment and added VFS state; a native browser should parse the
on-disk rows directly and must not map them to a pointer-bearing host struct.

VFS_AddArchiveEntries copies the first batch into a newly allocated table. For
later batches, it calls VFS_FindArchive for every incoming row: a matching name
replaces the full existing 284-byte record in place; a new name is appended
after growing the allocation. Thus later mounted archives override earlier
members with the same exact name while retaining that table slot. The observed
table ordering is first-seen order, with new names appended.

VFS_FindArchive uppercases its search string and uses strcmp against stored
names. The asset samples use uppercase member names. Stored lowercase names
would not match unless some other path normalized them; that case is untested.

## Open, read, seek, close

VFS_Open first calls _uopen_. Only when that fails does it search the member
table, and the fallback rejects open modes with bits 0x01 or 0x20. On a match it
seeks the backing descriptor to member offset + logical position, sets the
current-member cache, and returns -(table index + 1). The negative value encodes
the member row; index zero therefore uses handle -1. A missing member reaches
the fatal-error path rather than returning a browser-friendly error.

VFS_OpenArchive uppercases the requested name and compares 259 bytes against
the row name. The instruction sequence at 0x0043af54–0x0043af70 sets the
strncmp length to 0x103; the NUL-terminated corpus names therefore require an
exact match, not a prefix match. It sets +0x10c to 1 and resets +0x110 to 0.
It returns the row index or -1.

For a negative handle, VFS_Read decodes the row index, checks +0x10c, and
clamps the requested byte count to member length - logical position. It updates
the member position by the bytes read. Members from one archive share the same
backing descriptor; a global last-member index at 0x0049d1c8 avoids redundant
seeks and causes a seek to member offset + position when switching rows.
Interleaved reads seek correctly when switching rows, but two simultaneous
opens of the same row share its logical position.

For a negative handle, VFS_Seek supports whence 0, 1, and 2, clamps positions
below zero to zero, and clamps positions at or beyond the member length to
length - 1; it then seeks the backing descriptor to member offset + position.
The upper clamp means seek-to-EOF is not represented normally, and a
zero-length member would clamp to -1. These are engine semantics/edge cases,
not desirable native-reader requirements.

VFS_Close on a member handle only checks +0x10c and returns zero. It does not
clear the flag or close the backing descriptor. VFS_FreeArchives frees only the
table pointer. In the reviewed path, the mount's base file descriptor is not
explicitly closed; its full lifetime is unresolved. A native browser should
instead own and close the archive source with the mount/file lifetime.

## Independent corpus validation and discrepancies

The configured disc extracts contain DATA\ICONE\ICONES.BF on both discs.
Direct parsing of the little-endian fields and 267-byte rows produced:

| Sample | Size | Version | Table offset | Count | Check |
|---|---:|---:|---:|---:|---|
| Disc 1 ICONES.BF | 372,358 | 2 | 371,023 | 5 | 372358 - 267×5 = 371023; payloads are contiguous through the table. |
| Disc 2 ICONES.BF | 440,029 | 2 | 438,427 | 6 | 440029 - 267×6 = 438427; payloads are contiguous through the table. |

The member names, offsets, and lengths match the tables in
[file-formats.md](../file-formats.md). MAGIE.ALP, ANIM.ALP, PYRAM.ALP,
TOUCHES.SPR, and INTERF.ALP occur on both discs; Disc 2 inserts TITRES.SPR
after PYRAM.ALP. The docs describe .BF as a named asset container, which
agrees with the code's table-driven member access.

One existing RE registry description conflicts with the instruction evidence:
re/names/WINDREAM.EXE.tsv calls VFS_OpenArchive a name-prefix search. The fixed
0x103-byte comparison shows exact matching for the corpus's NUL-terminated
names. This review records the correction; the registry was not edited under
this assignment.

The code accepts malformed offsets/counts without visible bounds checks and
does not check container version. The successful corpus checks establish the
expected layout for these game files, not safety for arbitrary user-supplied
files.

## Implications for 002

The current [002 draft](../specs/002-disc-navigation/spec.md) says .BF members
remain hidden until later format-decoder work. To make 002 an asset browser,
add an archive expansion action that:

1. Parses UBIK, the table offset/count, and each 267-byte name/offset/length row
   with checked 64-bit arithmetic and file-bound checks.
2. Shows member name, extension hint, byte size, and its containing disc path.
   Keep the outer .BF file as the parent so identical names in different
   archives or discs remain distinct.
3. Reads a selected member by bounding every requested slice to its declared
   length and translating the member-relative offset to outer file offset +
   member offset.
4. Lists members in stored row order and does not apply the game's global
   cross-archive overwrite rule. The browser's two disc mounts must retain
   separate provenance and conflicts.
5. Treats this as navigation and byte access; decoding .SPR/.ALP contents
   remains a later capability unless the spec explicitly expands its scope.

The recovered game methods provide the file layout and access semantics to
document. The browser should use bounded, owned native file access rather than
reproducing the engine's process-global table, negative handles, shared cursor,
or fatal-error behavior.

## Validation performed

- Read-only Ghidra decompilation completed for BF_Mount,
  VFS_AddArchiveEntries, VFS_FindArchive, VFS_OpenArchive, VFS_Open, VFS_Read,
  VFS_Seek, VFS_Close, VFS_FreeArchives, UI_InitIcons, and SPR_LoadIconBanks.
  From the repository root, the command was:

    . .\tools\dreams-env.ps1; $ghidra = Get-DreamsSetting DREAMS_GHIDRA_ROOT; & (Join-Path $ghidra 'support\analyzeHeadless.bat') (Join-Path $env:TEMP 'dreams-vfs-review') dreams -process WINDREAM.EXE -noanalysis -readOnly -scriptPath ghidra_scripts -postScript Decompile.java 0043ac08 0043ab96 0043af27 00409304 004093f0 00409634 0040985e 0043ad40 0043aefc 004341eb 00426c46

  Ghidra processed the read-only WINDREAM.EXE program and printed all requested
  function bodies and call graphs.
- Disassembled WINDREAM.EXE at 0x0043ab96 and 0x0043af27 from the Disc 1
  executable with Capstone; confirmed the strcmp exact find and fixed
  259-byte strncmp in VFS_OpenArchive.
- Parsed both configured disc samples directly; the header/table arithmetic
  and all 11 member extents passed. The parser read the whole file with
  PowerShell [IO.File]::ReadAllBytes, used [BitConverter]::ToUInt32 at header
  offsets 8 and 12, then stepped 267 bytes per row and checked every extent.
  No project test suite was run because this was a read-only reverse-engineering
  review, not an implementation change.
