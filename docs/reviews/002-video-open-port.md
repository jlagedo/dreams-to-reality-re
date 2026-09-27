# 002 video open/close port record

The implementation is `opendreams/shared/port/video.cpp`. `VID_Open`
(`0x004085dc`) and `VID_Close` (`0x0040887b`) are mapped to their C++ symbols
in `opendreams/port-map.tsv`, then tagged and commented in both checked
Windows Ghidra programs by `ApplyPortMap.java`.

Read-only Ghidra decompilation shows `VID_Open` closing an existing video,
opening the retail `STRM_*` stream, filling it up to 24 times, peeking `0x44`
bytes, copying a 64-byte header and the following four bytes, then committing
the peek. It dispatches `HNM4`/`HNS4` to family 1, `UBB2`/`UBS2` to family 2,
and `HNM6`/`HNS6` to family 4. The S variants add kind bit 8 when sound is
enabled, making the return value 2; otherwise successful open returns 1.
Unsupported or incomplete headers return 0. `VID_Close` invokes a
family-specific closer, stops sound, closes the stream and clears the kind.

`VideoState` keeps the source-scoped raw header, following four bytes,
selected retail kind bits, source size and stream. The 002 port preserves
the magic dispatch and the open/close state transitions. It does not start
frame decoders or DirectSound playback, which are outside this metadata-only
milestone. The logical S-variant sound selection is cleared on close; no
audio channel is opened. Errors also close the selected stream, so a malformed
file does not prevent opening the next source. Width, frame and other header
fields can be labeled as viewer-derived metadata from the raw bytes.

Synthetic CUE/BIN cases cover all six magic values, the 0/1/2 return
convention, sound disabled on an S variant, repeated open/close, bad magic
and a file shorter than 68 bytes. Both original images passed: Disc 1 has
55 physical video files (6 HNM4, 3 UBB2, 8 UBS2, 38 HNS6); Disc 2 has
60 (14 HNM4, 1 UBB2, 8 UBS2, 2 HNM6, 35 HNS6). CRC32 of sorted source
paths, file sizes and every 68-byte opened header was `d048626f` and
`155c2fe9`, matching the independent Python `read_header` inventory.
GCC fixtures and all five MSVC shared tests passed; native and Emscripten
ODViewer/ODRuntime targets linked.
