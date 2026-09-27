# DOS VFS member lookup — independent review

## Blind DREAMSFX reading

I first read the `DREAMSFX.EXE` decompilation and disassembly for `0x00058ec4`
and its caller `0x00058740`, with Ghidra `[NAME]` / `[DOCS_SYNC]` plate-comment
blocks removed. I formed the interpretation below before checking the current
registry or documentation.

`0x00058ec4` scans a table of fixed-stride records: table base
`0x000fe7d0`, record count `0x000fe7d4`, stride `0x11c`. For each record, the
function compares the caller's requested path against the string at the record
start. A match marks fields at record offsets `+0x10c` and `+0x110` as `1`
and `0`, respectively, and returns that record index. Exhausting the count
returns `-1`.

The direct instructions establish the comparison:

| Address | Instruction | Evidence |
|---|---|---|
| `0x00058ec9` | `MOV EDI,EAX` | Keeps the requested name while walking the table. |
| `0x00058ecb`–`0x00058ed5` | Load count from `0x000fe7d4`; loop while positive. | Bounds the scan by the number of records. |
| `0x00058ed9` | `MOV EBX,0x103` | Supplies the fixed comparison bound, `0x103` bytes. |
| `0x00058ede`–`0x00058ee6` | Load table base `0x000fe7d0`, add the current record offset, restore request to `EAX`. | Compares the request to the current record's leading name field. The record offset advances by `0x11c` at `0x00058f21`. |
| `0x00058ee8` | `CALL 0x00077581` (`_strupr_`) | Uppercases the requested name in place. |
| `0x00058eed`–`0x00058ef4` | `CALL 0x00076b6a` (`strncmp_`); test return for zero. | Uses bounded string comparison; zero means a match. |
| `0x00058efb`–`0x00058f0b` | Store `1` at record `+0x10c`, `0` at `+0x110`. | A match opens/resets the logical member record; this is not only a search that returns an index. |

The runtime implementation at `0x00076b6a` compares bytes until a mismatch,
NUL, or the supplied count. Therefore this is not a prefix-search algorithm:
for ordinary NUL-terminated names shorter than 259 bytes, a shorter name does
not match a longer name because the NUL is compared with the next character.
The fixed count is `0x103`, however, so a request longer than 259 bytes whose
first 259 bytes equal a stored name could match without checking its remaining
suffix. I found no evidence that such overlong requests occur. The precise
claim supported by the code is **bounded equality over the 0x103-byte member
name key**, not an unbounded exact-string comparison.

`VFS_Open` at `0x00058740` supplies the caller context. At `0x00058747` it
first calls `open_` on the requested path. Only after that fails (`CMP EAX,-1`
at `0x0005874f`) does it call `VFS_OpenMember` at `0x00058756`. On a table
match, it rejects requests with mode bits `0x1` (`TEST DL,0x1` at
`0x00058762`) or `0x20` (`TEST DL,0x20` at `0x00058767`) set, computes the
record address using the same `0x11c` stride, and seeks the handle at `+0x118`
to the member base offset at
`+0x104` plus the logical cursor at `+0x110` (`0x00058781`–`0x00058797`). It
returns a negative index-derived handle for that archive-backed open. The
call-graph output identifies `VFS_Open` as the sole caller of `0x00058ec4`;
`VFS_Open` is called by `BF_Mount` `0x00058cc0` and `SPR_LoadIconBanks`
`0x00058f80`.

**Blind conclusion:** the object searched is a VFS record keyed by a member
name, consumed as an archive-backed file by `VFS_Open`; on match the function
marks the record usable and resets its cursor. The comparison is a fixed-width
bounded equality check, not a general prefix match. The request is uppercased
before comparison. This function does not itself mount the archive.

## Checked Windows counterpart comparison

After forming that conclusion, I checked the existing Windows counterpart at
`WINDREAM.EXE:0x0043af27` and its caller `VFS_Open` at `0x00409304`. The
comment-stripped Windows decompilation also iterates `0x11c`-byte records,
uppercases the request with `strupr_`, compares with `strncmp_` using `0x103`,
sets record offsets `+0x10c` / `+0x110` to `1` / `0`, and returns the matched
index or `-1`. Its caller first tries `_uopen_`, then performs the member
lookup and seeks the archive handle to `+0x104 + +0x110`.

The Windows helper `strupr_` at `0x0045fbfb` also uppercases the request
in place. This agrees with the checked entries in
[`re/names/WINDREAM.EXE.tsv`](../../re/names/WINDREAM.EXE.tsv) for
`VFS_OpenMember` (`0x0043af27`) and
[`re/names/DREAMSFX.EXE.tsv`](../../re/names/DREAMSFX.EXE.tsv) for the mapped
DOS function `VFS_OpenMember` (`0x00058ec4`). It independently supports the
member-object interpretation and the distinction from prefix matching. The
DOS-specific instructions provide the second direct build's evidence; no names,
comments, registries, or Ghidra project data were edited.

## Uncertainty

The DOS body proves the input is uppercased but this follow-up did not inspect
the DOS archive-table population routine, so that body alone does not prove the
stored keys' case. The Windows counterpart's lookup body also uppercases the
request; the checked registry describes the stored keys as normalized member
names. The VFS caller and returned record offsets show archive-backed member
access; this follow-up did not independently inspect the DOS `BF_Mount` table
population path. The `0x103`-byte bound leaves the overlong-request edge case
described above; the observed use supports equality for valid member-name
lengths, not behavior for arbitrary paths longer than the key width.

## Validation

Read-only headless commands completed successfully with exit code 0:

```powershell
& (Join-Path $g 'support\analyzeHeadless.bat') ghidra dreams -process DREAMSFX.EXE -noanalysis -readOnly -scriptPath ghidra_scripts -postScript Decompile.java 00058ec4 00058740
& (Join-Path $g 'support\analyzeHeadless.bat') ghidra dreams -process DREAMSFX.EXE -noanalysis -readOnly -scriptPath ghidra_scripts -postScript Decompile.java 00076b6a 00077581
& (Join-Path $g 'support\analyzeHeadless.bat') ghidra dreams -process DREAMSFX.EXE -noanalysis -readOnly -scriptPath ghidra_scripts -postScript Inspect.java code:00058ec4 calls:00058ec4 code:00058740
& (Join-Path $g 'support\analyzeHeadless.bat') ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly -scriptPath ghidra_scripts -postScript Decompile.java 0043af27 00409304
& (Join-Path $g 'support\analyzeHeadless.bat') ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly -scriptPath ghidra_scripts -postScript Decompile.java 0045fbfb
```

The PowerShell wrapper used `tools/dreams-env.ps1` to resolve
`DREAMS_GHIDRA_ROOT`. This is static decompilation/instruction review; no
program was executed.
