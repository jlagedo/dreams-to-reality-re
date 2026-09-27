# 002 VFS and BF port record

The implementation is `opendreams/shared/port/vfs.cpp`. Each retail entry point
is recorded with its executable address, C++ symbol, status and adaptation in
`opendreams/port-map.tsv`. `ApplyPortMap.java` applied `PORT:adapted` Function
Tags and `[PORT_MAP]` location comments to the saved WINDREAM.EXE and
GDIDREAM.EXE Ghidra programs. The GDI twin passed `check_names.py --twin`.

## Retail behavior retained

- `VFS_Open` checks a loose file before falling back to a named BF member. Its
  positive handles are physical files; a member returns `-(table index + 1)`.
- `VFS_Read` bounds member reads to the row length. A member's cursor belongs to
  its table row, so reopening the same name resets that shared cursor.
- `VFS_Seek` keeps the retail `[0, length - 1]` clamp for nonempty members;
  physical file seeks use the requested origin and signed offset.
- `VFS_Close` validates a negative member handle without clearing its row.
- `BF_Mount` checks `UBIK`, reads the 16-byte header and 267-byte directory
  rows, then calls `VFS_AddArchiveEntries`. The first registration copies its
  rows; later registrations replace an exact same-name row in place and append
  new names. `VFS_FindMember` and `VFS_OpenMember` uppercase the request before
  comparing it with stored names. `VFS_FreeArchives` clears the table.

## Portable boundary and safety adaptations

`VfsContext` makes the original process-global handle and archive state
explicit for one selected `disc::Image`. Reads use the ISO file ID and checked
offset rather than an OS descriptor. Both retail root prefixes resolve inside
the selected image, with the longer prefix winning. The image is read-only;
unsupported write modes and missing files return errors instead of invoking
the retail fatal-error path. BF table and payload ranges are checked before
registration. The empty-member seek underflow is mapped to position zero.

`BF_Mount` takes an explicit disc path instead of the original OS alternate-root
search and closes its temporary positive VFS handle after parsing. Members
retain the owning ISO file ID, so no leaked backing descriptor is needed.
The returned row snapshot retains physical row indices even if the retail
lookup table replaces a duplicate name. A separate context is needed for each
mounted disc when cataloging both versions.

## Validation

- Read-only Ghidra decompilation covered all nine functions and their direct
  calls, globals and cleanup. The existing blind review is
  `docs/reviews/002-vfs-archives.md`.
- Synthetic CUE/BIN fixtures exercise loose-file reads across a sector
  boundary, root aliases, short reads, seek/close, two BF mounts with a
  same-name replacement, table order, member reads, independent contexts and
  an out-of-range BF table.
- MSVC and GCC `ODDiscFixtures` passed. The MSVC `ODDiscCorpus` passed with the
  two original CUE/BIN images and extracted references. It checked 1,219/409
  physical files, 27/26 directories, 12/14 tracks, the five/six BF member
  names, each row's offset and size against the independent Python
  `read_bundle` results, and every member payload byte against its owning
  archive range.
- Emscripten linked `ODViewer.html` and `ODRuntime.html` with the shared VFS
  source in a separate WSL build directory.

This is the file and archive layer for subsequent 002 asset ports. ODViewer
catalog and UI calls have not yet been connected.
