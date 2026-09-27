# 002 FSB sound-bank port record

The implementation is `opendreams/shared/port/fsb.cpp`. The executable
addresses and C++ symbols are in `opendreams/port-map.tsv`, which
`ApplyPortMap.java` mirrors as Ghidra tags and implementation-location plate
comments.

Read-only Ghidra decompilation covered `FSB_Load` (`0x00426143`),
`FSB_GetSample` (`0x0042639d`) and `FSB_Free` (`0x00426340`). The loader opens
one `DREAMS FSB  ` bank, reads the 12-byte marker and `u32` clip count, allocates
an eight-byte `(pointer,size)` row per clip and one contiguous sample blob,
reads the `N` sizes followed by that blob, then closes the file. The sample
getter returns a clip pointer; free releases both allocations.

`FsbBank` retains the same contiguous bytes and clip ordering for one selected
VFS source. The returned sample view also includes the size needed by the
navigation catalog. The port checks table size, every clip range and exact
EOF before allocation; malformed or absent images return recoverable errors.
It does not play sound or repair an individual WAVE format tag.

Synthetic CUE/BIN tests cover two clips, pointer/size lookup, free/reset,
missing source and an oversized table entry. Both original discs contain the
same 741,370-byte bank. The port found 24 clips totaling 741,258 sample bytes
on each. CRC32 of the clip offsets/sizes is `6b8c8d72` and of every sample
byte is `0b02b576`, matching the independent Python `read_fsb` decoder on
both sources.
GCC fixtures and all five MSVC shared tests passed; native and Emscripten
ODViewer/ODRuntime targets linked.
