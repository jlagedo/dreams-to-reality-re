# ODViewer disc browser implementation

Status: implementation and corpus validation, 2026-09-27.

`opendreams/shared/inspect/catalog.h` and `.cpp` define `od::inspect`, the
read-only navigation layer. It is **viewer inspection code**, not reconstructed
game runtime code and has no fabricated retail address or Ghidra tag. The
retail code remains in `opendreams/shared/port/` and is available to both
ODViewer and ODRuntime. The catalog owns two independent mounted sources,
snapshots loader results, groups them for search and resolves references by
listing every matching source candidate. ODViewer owns the ImGui selection,
filters, source tree, preview space and details panels. Selecting a physical
model or a project prop reference updates the preview label. Ambiguous prop
references ask the user to choose a source copy in Details. The 3D canvas is a
reserved space; model rendering remains a later milestone.

| Catalog field | Origin |
|---|---|
| CUE tracks, ISO paths, extents, marker identity | New portable `od::disc::Image`; no retail CUE/ISO reader exists |
| BF rows and icon slots | Retail `BF_Mount`, `SPR_LoadIconBanks`, `ICON_NameTable`; extra member slots use `SPR_LoadSet` |
| Project record bytes | Retail `DDAT_Load`, `DDAT_LoadRecord`, `RLE_UnpackZeros` |
| Project `OBJET`, `LINK`, `LINKADVENT` and music reference labels | Viewer-derived fixed field offsets in the loaded record; unresolved and multiple candidates retain explicit status |
| DSN names and object records | Retail `DSN_LoadHeader` |
| DSN tag headers and bounded physical extents | Viewer-derived five-byte tag walk after `DSN_LoadHeader`, through retail `VFS_Open`/`VFS_Read` |
| DAN names and animation chunks | Retail `DAN_OpenArchive`, `DAN_GetAnimName`, `DAN_ReadAnimChunks` |
| Dialogue and effect clips | Retail `DRD_Open`/`DRD_LoadEntry`, `FSB_Load` |
| Standalone UI sprites and fonts | Retail `SPR_LoadSet`, `TEXT_LoadFont` |
| `DATA/OBJET/*.SPR` unique VGA records | Viewer-derived pointer-table index through retail VFS reads; there is no identified retail reader for these sheets. Slot aliases and sentinel pointers do not become duplicate records. |
| Video family | Retail `VID_Open` magic dispatch |
| Video width, height, frame count and declared size | Viewer-derived fixed header fields from `VID_Open` state |

The browser indexes ISO entries immediately and processes format jobs a few
files per frame. Search results show partial status until the jobs complete;
they include indexed children even when their source tree branches are closed.
Each row keeps its mount lifetime, physical path, parent, internal key,
availability and provenance. A failed replacement preserves the previous
mount. Unmounting one source leaves the other intact. The app starts without
images, accepts native `.cue` selection or editable UTF-8 paths, and optionally
accepts `--cue1 path --cue2 path` for development. Its WebAssembly build shows
the empty browser with disc opening disabled, as specified for 002.

Validation: `ODCatalogCorpus` mounts the English two-disc CUEs in reverse
selection order and indexes both. It checks 409/1,219 physical files, 150
projects per disc, 6/5 BF members, 13/11 audio tracks, 191 physical DANs, 98
physical DSNs, all 98 geometry tags and 6,272 texture tile tags, and 232
viewer-derived VGA sprite records per disc. It also checks two `CAI.DAN`
sources, both Project71 records, failed replacement and independent unmount.
All physical format-index jobs completed without a loader error on this corpus.
The existing `ODDiscCorpus` and `ODDdatCorpus` provide independent byte and
record comparisons; the catalog test adds cross-format navigation checks.

Remaining 002 work: inspect a mounted browser window interactively; compare
source reveal, filters and selection invalidation with user actions; add
synthetic navigation tests for those UI transitions; and audit tag and
reference labels beyond the checked corpus. Standalone `.3DC`/`.3DM` and
`OBJET1.PAK` stay physical, selectable **unindexed** files until a retail path
or documented viewer extension supplies their internal inventory. Media
preview and playback remain outside 002.
