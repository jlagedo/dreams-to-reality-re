# 002 DREAMS.DAT port record

The shared implementation is `opendreams/shared/port/ddat.cpp`. Function
addresses, C++ symbols and adaptations live in `opendreams/port-map.tsv`;
`ApplyPortMap.java` regenerates the matching Ghidra Function Tags and plate
comments in the two checked Windows executables.

## Retail path and state

Read-only Ghidra decompilation shows `DDAT_Load` (`0x00448f5f`) reading up to
`0x25400` bytes of `DREAMS.DAT`, calling `DDAT_InitEmptyRecords`
(`0x00448c6d`) when the file cannot be opened, and unpacking the first indexed
record with `RLE_UnpackZeros` (`0x00448e25`). `DDAT_LoadRecord`
(`0x00449bf9`) searches 150 compressed record names, decodes a match into a
single mutable `0x2200` working buffer, and may fall back to the previous
active record name when no requested name matches. `RLE_UnpackZeros` copies
nonzero literals and expands `00 N` to N zero bytes.

`DdatBank` holds the index, compressed bytes, boot and working records, active
name, previous name and fallback flag for one selected source. The game changes
its active-record pointer outside `DDAT_LoadRecord`; the portable state exposes
`set_active_record` for that transition. A caller must copy the returned
working record before requesting another one. The viewer can check
`has_record` before loading each `ProjectN`, so its catalog never silently
uses the game's previous-record fallback.

## Boundary adaptations

`DDAT_Load` reads through the selected source's VFS context. It checks the 151
little-endian offsets, the `0x400` data start, monotonic record ranges and the
retail maximum bank size. The zero-run decoder rejects a trailing escape or
output beyond its caller's buffer. Each requested record must expand to exactly
`0x2200` bytes. Errors are recoverable and a failed byte-bank reload leaves the
previous valid bank intact. `DDAT_InitEmptyRecords` fills all 150 `EMPTY`
records with the retail link and object names; `has_source` distinguishes this
missing-file fallback from an actual disc bank.

## Validation

- Synthetic cases check zero runs, zero-length runs, truncated escape,
  output overflow, all 150 index entries, name lookup, mutable-buffer
  replacement, previous-name fallback, disabled fallback, bad index and the
  missing-file `EMPTY` template.
- The original Disc 1 and Disc 2 CUE images were read through `DDAT_Load` and
  `DDAT_LoadRecord`. All 150 records per disc had the expected name and
  `0x2200` output length. CRC32 of the concatenated decoded records was
  `f18ef135` and `fd8d3b99`, respectively, matching the independent Python
  `dreams.formats.project.records` decoder. The only differing record indices
  are 31, 41, 55, 69, 75 and 87.
- GCC and MSVC fixture tests passed, and both `ODViewer.html` and
  `ODRuntime.html` linked with the DDAT source under Emscripten.

The returned records contain the source bytes for subsequent catalog fields;
project references and asset classification are separate viewer work.
