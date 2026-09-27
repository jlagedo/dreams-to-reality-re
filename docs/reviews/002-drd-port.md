# 002 dialogue-bank port and offset-table correction

The implementation is `opendreams/shared/port/drd.cpp`. The checked retail
addresses, C++ symbols and adaptations are in `opendreams/port-map.tsv`;
`ApplyPortMap.java` generates `PORT:adapted` Function Tags and C++ location
comments in both Windows Ghidra programs.

## Retail path

Read-only Ghidra decompilation covered `DRD_Open` (`0x0041072c`), `DRD_Close`
(`0x004108c7`), `DRD_LoadEntry` (`0x00410928`), `DRD_GetPortrait`
(`0x00410b8c`) and `DRD_GetLineCount` (`0x00410bc7`). The opener combines the
install root and supplied path, opens `DRDF`, reads a 16-byte header, then a
five-byte table block and `N` offsets. It allocates one buffer sized by the
maximum-entry field and sets the current entry to -1. `DRD_LoadEntry` seeks
to one zero-based entry, reads its nine-byte header and the remaining bytes
into that reusable buffer. A request for the current entry returns without
rereading. Tag 2 holds WAVE, tag 3 holds timed lines (centiseconds multiplied
by `15/100` to get 15-Hz ticks), and optional tag 4 holds a portrait.

## Offset-table correction

The five-byte block starts at `0x10`: tag zero followed by size `5 + 4*N`.
The actual table begins at `0x15`, with ordinary little-endian **absolute
`u32` offsets**. Earlier research and the Python parser began at `0x14`, one
byte early. Shifting each misaligned word by eight bits and counting apparent
24-bit wraps happened to reconstruct the right positions, but invented a
nonexistent packed-offset scheme. The first retail table entry is `0x2dd`;
entries 122 and 123 are `16,838,184` and `16,956,475`. All 178 aligned
offsets are strictly increasing, every entry's size reaches the next, and
the final one ends exactly at the 24,592,952-byte EOF. The Python decoder,
format docs and 002 spec now use the aligned table.

## Portable boundary and validation

`DrdBank` owns one selected-source VFS handle, the copied absolute-offset
table, one reusable entry buffer and the current entry's WAVE, line and
portrait extents. It uses the adapted `FILE_GetInstallRoot` path through VFS.
Callers snapshot entry data before loading the next one. Malformed table,
entry or sub-block ranges return errors instead of permitting unchecked
reads; Disc 2 reports the bank absent without affecting Disc 1.

Synthetic CUE/BIN fixtures exercise two entries, the same-entry shortcut,
buffer reuse, WAVE and caption reads, optional portrait, close/reset and an
invalid offset table. On the original Disc 1 image, all 178 entries matched
the independent Python decoder: 589 caption lines, 169 portraits,
17,848,529 WAVE bytes and 6,719,761 portrait bytes. CRC32 values for the
offset/size pairs, WAVE payloads, portrait payloads and timed text were
`ac7173e2`, `290bc860`, `cdf34aa1` and `0c29112a`, respectively.
GCC fixtures, all five MSVC shared tests and the Python format suite passed;
native and Emscripten ODViewer/ODRuntime targets linked.
