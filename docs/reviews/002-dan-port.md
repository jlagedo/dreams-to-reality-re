# 002 DAN directory and animation-chunk port record

The implementation is `opendreams/shared/port/dan.cpp`. The checked retail
addresses and C++ locations are in `opendreams/port-map.tsv`; running
`ApplyPortMap.java` rebuilds their Ghidra Function Tags and plate comments.

## Retail path

Read-only Ghidra decompilation covered `DAN_OpenArchive` (`0x0040fff7`),
`DAN_ReadAnimChunks` (`0x00410435`), `DAN_GetAnimCount` (`0x0041069d`),
`DAN_GetAnimName` (`0x004106c9`) and `DAN_CloseArchive` (`0x0040ffb2`).
The opener closes the old archive, reads a nine-byte `DANF` preamble and a
five-byte span block, then two counted directories: 11-byte model/material
names and 13-byte animation labels. It allocates a `0x96000` work buffer.
`DAN_ReadAnimChunks` reads one type-3 chunk for each animation label into
that buffer and records its pointer and payload length. The count/name getters
read the directory state; close clears the archive state.

## Portable boundary

`DanArchive` keeps one selected VFS source, its handle, copied fixed-width
names, clip labels, chunk extents and work buffer. A clip label is an internal
`.3DA` identity under its physical `.DAN`, not a loose ISO file. The physical
source and original row index remain available for the catalog. The port
checks file size, directory span, count limits and chunk ranges before reads.
Errors return to the caller rather than leaving an unchecked retail buffer.

Retail calls `DAN_Read3DC` and `DAN_ReadTextureChunks` before
`DAN_ReadAnimChunks`, leaving the OS descriptor at the first type-3 chunk.
For metadata-only 002 indexing, `DAN_ReadAnimChunks` advances over type-1
and type-2 ranges using their five-byte tag/size headers, then performs the
retail type-3 payload reads and work-buffer bookkeeping. This preserves the
function boundary and output without decoding the earlier geometry or
texture payloads. Those deeper loaders remain conditional later ports.

## Validation

- A synthetic CUE/BIN archive exercises the two directories, two type-3
  chunks, fixed-width name getters, work-buffer contents, close/reset and a
  malformed chunk range.
- The original images supplied 111 and 80 physical `.DAN` archives. Their
  directories hold 170/142 names and 550/498 clip labels. Every declared
  label had exactly one type-3 chunk; the total compressed payload lengths
  were 1,939,827 and 1,754,479 bytes. CRC32 of sorted paths and raw directory
  bytes was `05c1a513` and `c9215b67`; CRC32 of all type-3 payloads was
  `38fec8d1` and `a25c1768`. These match the independent Python
  `read_dan`/`read_records` results.
- GCC fixtures and all five MSVC shared tests passed. Native ODViewer and
  ODRuntime linked, as did their Emscripten `.html` targets.

This provides the archive's logical material/clip children for the catalog.
Decoded model, texture and animation payloads belong to their later shared
loader paths.
