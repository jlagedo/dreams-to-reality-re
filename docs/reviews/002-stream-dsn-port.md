# 002 stream and DSN header port record

The implementations are `opendreams/shared/port/stream.cpp` and
`opendreams/shared/port/dsn.cpp`. Original addresses, checked names, C++ symbols
and adaptations are in `opendreams/port-map.tsv`. `ApplyPortMap.java` rebuilds
`PORT:adapted` Function Tags and `[PORT_MAP]` comments in both Windows Ghidra
programs.

## Retail call path

Read-only Ghidra decompilation covered `STRM_Create` (`0x00415201`),
`STRM_Open` (`0x004152f2`), `STRM_Fill` (`0x004153fe`), `STRM_Peek`
(`0x004154db`), `STRM_Commit` (`0x004155a2`), `STRM_Close`
(`0x004153bf`), `STRM_Free` (`0x004152be`), `DSN_InitState`
(`0x004174e7`), `DSN_ResetState` (`0x0041754e`) and `DSN_LoadHeader`
(`0x004175bc`). The stream constructor aligns its ring and wrap sizes to
the chunk size; `GAME_Init` supplies `0x57800`, `0x57800`, `0x8000`, yielding
a `0x58000`-byte ring. `STRM_Fill` reads a chunk when there is room and closes
the source on a short read while buffered bytes remain available. A peek
returns contiguous bytes, copying wrapped bytes into the slack area; commit
advances the cursor and clears the full flag.

`DSN_LoadHeader` resets state, opens a scene, fills the stream once, then
peeks and commits 9, 5 and 2 header bytes followed by `B * 11` name bytes and
`B * 20` object-record bytes. It reads the unaligned declared file size at
offset 5, takes `B` from the `u16` at offset 14 and leaves the stream at body
offset `16 + 31B`. The five `u32` words per object are copied from the retail
record bytes; their semantic labels remain a separate research question.

## Portable boundary

One `Stream` owns its ring state and uses a selected disc's VFS context for
the physical file. Source, ring and DSN loader state are explicit rather than
process globals. The port preserves buffered bytes after EOF and the reusable
stream across scene opens. Invalid sizes, counts, peeks and malformed headers
return recoverable errors instead of the retail fatal/exit paths. The DSN
port validates the declared size and the fixed header table bounds before
copying. `DSN_ResetState` preserves the stream reference; `DSN_LoadHeader`
keeps the stream positioned at the first packed-body tag on success.

## Validation

- Synthetic CUE/BIN fixtures exercise 32-byte chunk fills, ring wrap,
  peek/commit, short-buffer reporting, a two-object DSN header, the body
  position, malformed magic and an out-of-range object count.
- The original two CUE images yielded 52 and 46 physical `.DSN` headers, with
  1,171 and 974 named objects. CRC32 of sorted paths, header fields, all raw
  11-byte names and all raw 20-byte object records was `09a8c16d` and
  `071183a4`, matching the independent Python `read_dsn` results. Each
  declared size matched the selected ISO file's size.
- GCC fixtures and all five MSVC shared tests passed. Native ODViewer and
  ODRuntime linked, as did their Emscripten `.html` targets.

This ports the header and reusable stream state. The packed DSN body remains
for later resource/preview work; the viewer catalog still needs to call this
shared path and label any supplemental tag inventory separately.
