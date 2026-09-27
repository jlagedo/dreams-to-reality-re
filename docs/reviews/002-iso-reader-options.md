# 002 — permissive ISO-reader options

Status: **local spike, no ODShared integration**. Date: 2026-09-26.
This follows the [libcdio Windows spike](002-libcdio-windows-spike.md), which
found that its BIN/CUE driver cannot map the original split-track images as-is.

## lib9660 on the original data-track BINs

The small C [lib9660](https://github.com/erincandescent/lib9660) checkout was
`17704f87e833f3de46b1546864d47ef4a6d12b97`. Its license is an ISC variant.
It compiled with MSVC v145 as `lib9660.c` plus a local test harness. The harness
provided a checked sector callback: logical sector `n` reads 2,048 bytes at
`n * 2352 + 16` in the original Track 1 BIN using a 64-bit host seek. No
intermediate ISO or game data was written into the repository.

| Result | Disc 1 | Disc 2 |
|---|---:|---:|
| BIN bytes / complete raw sectors | 382,134,144 / 162,472 | 435,646,848 / 185,224 |
| Listed files / directories | **1,219 / 27** | **409 / 26** |
| `DREAMS.DAT` size | 138,879 | 138,835 |
| Disc marker reached by opening `DATA` then its file | `1CD.ID`, 5 bytes | `2CD.ID`, 5 bytes |

The same markers failed when passed as one multi-component path to
`l9660_openat` (`DATA/1CD.ID` or `DATA/2CD.ID` returned `L9660_ENOENT`). This
matches a path-segmentation error in the current source; component-by-component
lookup worked. Its `l9660_openfs` descriptor loop also does not advance the
sector number if sector 16 is neither a primary nor terminator descriptor, and
`l9660_readdir` does not validate record length against the remaining sector
and directory bytes before interpreting fields. These are source-review findings
that require fixes and malformed-image tests before this code reads arbitrary
user-selected images. The public file seek takes a signed 32-bit offset; the
host-file callback must retain checked 64-bit offsets.

## libarchive on derived ISOs

Windows `bsdtar 3.8.8`, which uses [libarchive](https://github.com/libarchive/libarchive),
listed both independently derived data-track ISOs with no errors: 1,247 rows
for Disc 1 and 436 for Disc 2, including the root and directories. Subtracting
those entries gives the same 1,219/409 files. This verifies libarchive's ISO
reader against the corpus, but does not test its callback API on the original
2352-byte BINs or a game-facing random-access file interface. Its
[ISO reader presents entries as an archive](https://github.com/libarchive/libarchive/wiki/FormatISO9660);
we would still need the CUE/sector layer and a way to serve repeated bounded
reads by disc and path.

## Recommendation

`lib9660` is the closest match to ODShared's sector callback and file-read
needs, conditional on a small audited fork or upstream fixes for the path and
malformed-input issues, followed by synthetic and full-corpus tests. It does
not parse CUEs or expose audio tracks; those remain a separate narrow layer.
`libarchive` is a mature alternative for listing ISO entries, with a larger
integration surface and a less direct path to the retail VFS read boundary.
Neither candidate is ready to add to ODShared without the stated checks.
