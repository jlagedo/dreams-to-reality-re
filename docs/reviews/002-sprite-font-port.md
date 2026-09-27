# 002 sprite, icon and font loader port record

The implementation is `opendreams/shared/port/sprite.cpp`. The ten checked
retail functions are recorded in `opendreams/port-map.tsv`; `ApplyPortMap.java`
rebuilds their `PORT:adapted` Function Tags and C++ location comments in both
Windows Ghidra programs.

## Retail paths

Read-only Ghidra decompilation covered `SPR_InitMulTables`, `SPR_LoadSet`,
`SPR_GetDescriptor`, `SPR_FreeSet`, `SPR_LoadIconBanks`,
`SPR_FreeIconBank`, `SPR_FreeIconBanks`, `ICON_FindByName`,
`TEXT_LoadFont` and `TEXT_FreeFont`. Both sprite loaders read a 512-byte
RGB555 palette, seek to `file_size - 0x1c04`, copy 256 28-byte descriptors,
then read the footer count and each nonzero-offset pixel record. The general
loader reads one byte per font `.SPR` pixel and two per `.ALP` pixel. The
five fixed `ICONES.BF` members are loaded through VFS and the icon path
requests two bytes per pixel even for `TOUCHES.SPR`, an 8-bit indexed source.
Both loaders reserve another `0x400` bytes for the two-byte path. The
16-bit display path converts each palette word from RGB555 to RGB565.

`TEXT_LoadFont` calls the general sprite loader, reads every descriptor,
computes each advance as signed `width - field_0c`, and takes the space
advance from the `'0'` descriptor. `ICON_FindByName` searches 72
case-insensitive executable names and returns the table index plus bank and
slot. The exact recovered table includes `infosta` as an alias of `piece`
at MAGIE slot 26 and `block` at slot 34, which is empty in the shipping
bank. `SPR_InitMulTables` preserves both multiply tables, including the
retail 64-row table's stride of 63.

## Portable boundary

`SpriteState` scopes the ten general sets, five icon banks and ten fonts to
one selected VFS source. A new `VFS_GetSize` support call reports physical
or BF member length without `VFS_Seek`'s retail member EOF clamp. The port
retains raw palette/descriptor bytes, per-slot pixel data, fixed index and
source path. A descriptor with invalid dimensions or extent remains visible
with `invalid` status while other slots load. This matters for codepoint
`%` in `HI320.SPR`: it has a nonzero offset but impossible dimensions and
would otherwise request an enormous allocation. The other 255 glyphs remain
usable. An alias or name pointing at an empty slot is reported as such;
the loader does not invent a sprite for it.

The original BF call order is preserved: `BF_Mount` registers members before
`SPR_LoadIconBanks`. Disc 2's extra `TITRES.SPR` is not in the retail fixed
five-bank list; the shared general loader can inspect its member bytes for
the source-disc catalog. Drawing and blend evaluation remain later runtime
work.

## Validation

- Synthetic CUE/BIN fixtures check a two-slot `.ALP`, palette conversion,
  footer/descriptor bounds, pixel reads, table multiplication, icon lookup,
  free and a malformed slot that leaves its neighbor available.
- Both original discs matched independent Python palette, 256-descriptor and
  physical pixel-stream CRCs for `SOUR.ALP`, all three `HI*.SPR` fonts and
  the five retail BF icon members. Disc 2's extra `TITRES.SPR` also matched.
  Every retail icon read returned `width * height * 2` bytes; the loader kept
  the actual one-byte source payload extent for `TOUCHES.SPR` and `TITRES.SPR`.
  The three 256-entry font advance CRCs matched Python's signed-field
  calculation. The complete 72-entry executable icon-name table matched
  CRC32 `fe4b8254` from `WINDREAM.EXE` at `0x49db12`/`0x49dd9a`.
- GCC fixtures, all five MSVC shared tests and the Python format suite
  passed. Native and Emscripten ODViewer/ODRuntime targets linked.
