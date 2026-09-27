# 002 — Retail asset access and game asset navigation

Status: **Implementation under validation**
Date: 2026-09-26  
Depends on: [001 — project initialization](../001-project-init/spec.md)

Current browser implementation and its retail/viewer-derived boundary are
recorded in [the implementation review](../../reviews/002-viewer-browser.md).

## Goal

Open the two original Dreams to Reality disc images together in **ODViewer** and
browse the game's assets using the SDL3, sokol and Dear ImGui foundation from
spec 001. The user supplies cue/bin files; no extraction, installation or Python
bake step is needed to navigate them. One disc remains useful when its partner
is unavailable, but both mounted at once is the normal completed setup.

**This is a navigation-only game asset browser.** A searchable, filterable
catalog organized by asset kind is the primary view. A separate source-disc
view exposes every original directory and file, audio track, and supported
container entry. Both views show provenance and relationships; neither renders
or plays media. Parsing a container index or a project record is part of
navigation, not a claim that its media payload can be previewed.

**The browser is a consumer and validation harness for the retail-function
port.** Game-format and asset-lookup behavior with a retail counterpart comes
from the same ODShared functions reconstructed through Ghidra that ODRuntime
uses. ODViewer may add read-only decoding or display of information the retail
functions do not provide, after obtaining the asset through those shared paths.
It must not replace a retail function with a second implementation. Porting the
required retail call paths is a deliverable of 002, not work to postpone until
gameplay.

001 supplies two runnable Hello World applications. This spec gives ODViewer
its first content-facing capability and adds reusable disc access to ODShared.
ODRuntime continues to build and run its foundation screen; game startup and
level loading are not part of 002.

## Scope

| Included in 002 | Deferred to later specs |
|---|---|
| Select/open the original `.cue` files and resolve their referenced `.bin` files | Downloading, obtaining or distributing game data |
| Read supported CUE track descriptions and index offsets | General-purpose support for every disc-image format |
| Read the ISO 9660 filesystem directly from the data track | Extracting or baking a data directory for the application |
| Mount both discs, with independent status and source identity | Resolving every conflict in a unified game-data namespace |
| Search and filter a type-first catalog spanning both mounted sources | Search inside undecoded media payloads or executable code |
| Navigate physical files and inspect basic metadata | Rendering sprites, textures, models or maps |
| Port and call retail lookup/loader paths for supported assets; enrich loaded results with viewer-only metadata where retail has no function | Full animation evaluation, presentation and gameplay loading |
| List audio tracks as disc metadata | Music, sound, dialogue or video playback |
| Bounded byte reads for physical files and indexed, uncompressed member extents | Media previews, animation and thumbnails |
| Remount/unmount and useful error messages | Editing, exporting or writing game files |
| Keep native builds and Emscripten compilation working | Browser image upload, HTTP packs, caching and delivery |

Keep each container as a physical file and show entries exposed by ported retail
functions or documented viewer extensions beneath it. In 002, port the required
BF, DDAT, DSN/DAN, DRD, FSB, sprite/font and video lookup or open paths, plus
their dependencies. The catalog
uses their existing fields to enumerate, classify and link assets, including
project records decoded by the ported zero-run routine. Preserve any reads or
state changes required by those retail functions. Additional viewer inspection
may decode already loaded bytes or structures only for details that have no
retail counterpart; identify those details as viewer-derived. Media previews,
playback and frame presentation are later features. Extensions alone do not
establish a type or preview support. Executables are listed, never run.

## Evidence and supported starting point

The initial corpus is the configured two-disc English-labelled release described
in [disc-layout.md](../../disc-layout.md). Its CUE files were inspected when
drafting this spec; observations are input coverage, not universal assumptions.

| Observed input | Consequence |
|---|---|
| Each track has its own `FILE "...bin" BINARY` entry. | Support this split-track cue/bin layout first. Resolve relative filenames against the CUE's directory. |
| Track 1 on each disc is `MODE1/2352`, with `INDEX 01 00:00:00`. | Read the ISO payload through the raw-sector reader, without creating an intermediate `.iso`. |
| Disc 1 has 11 audio tracks; disc 2 has 13. | Audio tracks are separate from filesystem files and need their own metadata listing. These counts are corpus checks, not hard-coded acceptance rules for every release. |
| Audio tracks contain both `INDEX 00` and `INDEX 01`. Disc 2 track 5 starts at `00:02:01`, while the inspected neighbouring tracks use `00:02:00`. | Preserve parsed offsets. Do not hard-code a two-second pregap or assume all track starts are identical. |
| `DATA/1CD.ID` and `DATA/2CD.ID` distinguish the original discs. | Use known markers to label/validate the mounted game discs; do not identify them solely by host filename or image size. |
| Some paths exist on both discs with different contents. | Preserve disc provenance and both entries instead of overwriting one during indexing. |
| Disc 1/2 `ICONES.BF` have 5/6 named members; both `DREAMS.DAT` files have 150 records. | These are per-source regression references, not global merged inventories. |

The configured extracted reference trees contain 1,219 files on Disc 1 and 409
on Disc 2. Across them are 98 physical `.DSN` files representing 95 distinct
scene names, 191 physical `.DAN` files, 16 `.SPR` files, and 11 + 13 CD audio
tracks. These measurements help validate coverage; they are not hard-coded
limits or reasons to collapse distinct source copies.

The observed `E:\games\dreams\CRYO\DREAMS\` install/cache tree is also not a
disc inventory: [190 installed files were compared](../../disc-layout.md#observed-installed-tree-versus-the-discs),
of which 178 matched disc files byte-for-byte, three shipped save files had
changed locally, and nine had no same-path loose file on either disc. Build the
source catalog from ISO records, not the installed tree or the game's selective
`LISTL` cache manifests.

The Python implementation in
[`src/dreams/formats/disc.py`](../../../src/dreams/formats/disc.py) provides
raw-sector conversion and a simple track-list parser.
[`tests/test_formats.py`](../../../tests/test_formats.py) has a small CUE test.
The current `parse_cue` returns track number, mode and filename only: it does
**not** parse indexes or implement an ISO directory reader. It is a useful
partial reference, not a complete correctness oracle for the new reader.

Single-BIN/multiple-track layouts, MODE2, compressed image formats and alternate
filesystem extensions are not promised by this draft. Encountering an unsupported
layout must produce an explicit result, not guessed offsets. Additional layouts
can be admitted with evidence and tests without changing the navigation scope.

The recovered retail access path and validation are documented in
[asset-access.md](../../asset-access.md). Its source is the checked Ghidra name
registry, comment-stripped decompilation and on-disc format measurements.
The game accessed OS-mounted files: it did not parse CUE/BIN or ISO 9660.
Its drive probing, install cache and fatal file errors are evidence for locating
assets, not interfaces to copy into ODViewer.

## Asset taxonomy and identity

The left browser starts with **Game assets** grouped by use, then **Source
discs** grouped by physical location. Every recognized game asset has a primary
kind; the same entry can also appear through a contextual query (for example,
a DAN clip in Animations and beneath its model) without becoming a second
source. The source-disc tree is the complete inventory, including extras,
unsupported formats, empty directories and install files.

| Left-menu group | Assets and source evidence |
|---|---|
| **Projects** | `Project0`–`Project149` from each `DREAMS.DAT`, with placements, links and referenced assets from the ported DDAT path. Labels from the shipped French `DREAMS.INI`, if shown, are attributed to that source and do not imply the English executable displays them. |
| **Scenes** | Physical `.DSN` levels, named scene parts and known geometry/collision/texture tag records. |
| **Models & props** | `.DAN` models and standalone `.3DC` geometry reached through ported model/resource loaders. `OBJET1.PAK` remains a physical source file; its known chunk may be indexed by a viewer extension after reading it through the shared file path if no retail PAK reader is found. |
| **Animations** | Clip labels inside each `.DAN`, retaining their parent archive and internal index. `.3DA` labels do not imply physical `.3DA` files. |
| **Textures** | Standalone `.3DM` banks, contextual DSN/DAN texture resources and HNM4 animated textures. Do not invent independent file extents for derived resources. |
| **Video** | HNS6/HNM6 cutscenes and UBB2/UBS2 movies. Classify by checked header magic, not `.HNM`/`.UBB` alone. |
| **Audio** | CD audio tracks and indexed effects in `FSB.DAT`. Music tracks are not ISO files. |
| **Dialogue** | DRD entries, showing voice, timed-caption and portrait tag presence together, plus links from projects. No caption transcription or playback is required. |
| **Interface** | BF icon-bank members, `.SPR`/`.ALP` sprite slots, font glyph descriptors and the cursor where ported sprite loaders expose them. Use recovered retail name tables where available; otherwise show a stable slot number. |
| **Game data** | Language text, `LISTL` asset manifests, replay data and shipped save examples. List their physical files and use ported readers where present; supplemental read-only structure is allowed after shared loading where retail has no reader. |

Bonus gallery images, reference renders, development leftovers, demos,
installers, drivers and unclassified files remain under Source discs and are
findable with an **Extras** filter. The semantic catalog must not suggest that
every file is game content. A recognized physical file may have several roles:
for example, one DAN supplies a model, clips and texture records, and one DRD
entry supplies voice, captions and possibly a portrait.

A source entry is identified by mount lifetime, disc identity, normalized
physical ISO path, and an internal kind/index or key when it is a child. A
logical grouping such as `Project62` or a scene filename may gather both discs'
source entries, but never replaces them. Show a source count separately from a
logical-name count. Do not call two sources identical merely because their names
or sizes match; confirm equality from bytes when needed. Missing, conflicting
and malformed targets remain represented as results with a reason.

## User flow and browser layout

1. Launch ODViewer without requiring game data. Show an empty asset browser and
   **Open game discs** setup with two `.cue` selectors. Mount both supplied images
   in one setup action and assign Disc 1/2 from their marker files, not from
   selection order or host filenames. A single selected disc can still mount.
2. Show an always-visible source strip with Disc 1 and Disc 2 mount/index status,
   replacement controls and errors. An unidentified or duplicate-identity image
   stays a distinct source with a clear status; never assign it to the other
   disc by position. A missing audio backing file appears on its track; a
   readable data track remains browsable.
3. Use a left section browser headed by global search and filters for kind,
   source disc (Both/Disc 1/Disc 2), status (available, missing, ambiguous,
   invalid or still indexing) and core assets versus extras. Its
   **Game assets** sections use the taxonomy above; **Source discs** contains a
   root for each mounted disc with physical files and audio tracks. The default
   source filter is Both.
4. Select a group to show a sortable list with name, kind, source availability,
   parent and index status. Select an item to show its metadata and relationships.
   Search covers names, project numbers, internal names and source paths across
   the catalog, including collapsed branches; it is not limited to visible rows.
5. Open a project to follow scene, object, dialogue, video and project links.
   Show every source candidate with its disc and a **Reveal in source disc**
   action. Keep missing and ambiguous targets visible instead of choosing a
   winner. The source view preserves the current location of each disc.
6. Select a physical file to see path, size, checked type when known and source
   disc. Expand supported indexed children under the owning file; each detail
   identifies its parent, internal key and verified extent/size. An unknown file
   remains selectable. Selecting anything never starts a media preview.
7. Inspect audio tracks separately from ISO files, with number, mode, backing
   filename and parsed index positions. Close or replace a mount independently;
   invalidate only its selections and index entries. A failed replacement leaves
   the previous valid mount usable.

The left section browser, result list and details area can use the existing
Dear ImGui integration without docking or a multi-window editor layout. SDL3's
native file selection is preferred where available; editable paths are a
development fallback. Cancelling selection changes nothing. An unknown or
ambiguous marker may still allow raw source inspection with that status shown,
but it must not be assigned a game-disc identity by guessing.

Index ISO paths first, then invoke the ported retail functions in staged work so
the browser stays usable. Preserve their required state and call order; serialize
access when original globals or mutable cursors require it. Show indexing
progress and whether search results are still partial. Once indexing completes,
search includes every supported logical child without requiring the user to
expand its container. One bad container reports its own error without hiding
its raw file or stopping other work. Do not hash every movie or copy an image
into memory merely to build the catalog.

## Shared disc access

Put CUE parsing, raw-sector access, ISO traversal, mount ownership, the ported
retail functions and the read-only catalog adapter in **ODShared**. ODViewer
owns browsing state and the UI. ODRuntime consumes the same ported functions;
neither application owns a substitute for a retail parser. ODViewer may own
supplemental read-only decoders as described below. There is no new
dependency-free library requirement and no need to reorganize recovered game
functions for this work.

The original engine accessed mounted media/files through its OS services. Reading
cue/bin containers directly is new portability support; these routines have no
retail equivalent. Replace original file/drive calls at their boundary so the
ported loaders read the selected source through the portable access layer. New
search, grouping and provenance bookkeeping may consume the ported loaders'
results. Supplemental viewer decoders may inspect loaded data where the retail
code has no function for the desired detail; they may not repeat behavior
already supplied by a ported retail function.

The shared functionality must support, without prescribing a class hierarchy:

- Opening and closing a disc source, with a stable identity for its mount lifetime.
- Reporting its track inventory and supported data-track access.
- Enumerating a directory and looking up a file by an explicit disc and path.
- Reporting an entry's kind, name, byte size and source identity.
- Reading a bounded slice of a file without loading its entire payload.
- Enumerating a supported container's logical children with the physical
  `(disc, path)` and internal key attached; resolving project references without
  erasing unresolved or conflicting targets.
- Enumerating indexed entries across mounts for global search, with type,
  provenance, parent, index status and completion state so partial results are
  distinguishable from a completed search.
- Returning a useful error when input is invalid or a read cannot be completed.

Use 64-bit host-file offsets and checked size arithmetic. Preserve on-disc field
widths while decoding; do not cast raw records to pointer-bearing host structs.
Reads and mount replacement must not leave dangling UI selections or open-file
resources tied to an invalidated mount.

### Retail-function porting contract

The [initial retail-function port map](retail-functions.md) lists candidate
entry points, addresses, known dependencies and paths deferred from 002. It is
a planning map; `opendreams/port-map.tsv` records functions only once ported.

For every game-format or asset-lookup behavior that has a retail counterpart:

1. Identify the retail function and its required callees, data tables and state
   in Ghidra. Record the checked name, address, evidence and C++ location in
   `opendreams/port-map.tsv`; retain recognizable function boundaries and call
   order. Port missing dependencies needed to execute the selected path.
2. Make ODViewer invoke that ODShared ported path. The catalog adapter may choose
   a source, request a load, read the resulting fields, snapshot them for search
   and attach provenance. It must not reimplement the same BF, DDAT, DSN, DAN,
   DRD, FSB, sprite, video or other behavior in viewer code.
3. Make the same entry point available to ODRuntime. When original global loader
   state prevents two simultaneous calls, serialize catalog requests and switch
   the source at the shared file-access boundary. Keep each result's source and
   lifetime explicit without silently changing the retail lookup semantics.
4. Compare the port with the retail decompilation and the Python decoder or
   corpus where available. Python is a development oracle, never a runtime
   dependency or a substitute for an existing retail-function port.

For additional information with no retail function, ODViewer may use a clearly
identified read-only decoder on bytes or structures obtained through ported
shared file/loader functions. It may expose internal entries, derived fields or
later previews without forcing ODRuntime to use that decoder. Document which
fields are **retail-derived** and which are **viewer-derived**, and validate new
interpretations against the corpus or Python reference. If neither a retail
path nor a viewer extension indexes an internal entry, keep its physical file
browseable with an **unindexed** status. `OBJET1.PAK` is a candidate for a
viewer-derived one-chunk index after shared VFS loading; its internal reader is
not falsely entered in the retail port map. New CUE/ISO, mounting, search,
filters and ImGui functions are also explicit portability and tool support.

### Recovered lookup contract for the asset catalog

The addresses and limits below are explained in [asset-access.md](../../asset-access.md).
They specify what 002 must be able to **locate and enumerate**, not a demand to
reproduce Watcom file descriptors, global mutable cursors, fatal dialogs,
resource arena allocation or gameplay side effects.

| Retail methods in `WINDREAM.EXE` | Fact to implement in 002 |
|---|---|
| `CD_FindDrive` (`0x427f11`), `CD_GetDiscNumber` (`0x4287fa`), `FILE_GetDataRoot` (`0x4285e9`), `CD_CopyFileList` (`0x428356`) | Marker files identify discs; install/cache roots and `ListL*.txt` are selective runtime paths, not the source inventory. Inspect both markers, reporting neither/both as unknown or ambiguous; enumerate the ISO instead of manifests. |
| `BF_Mount` (`0x43ad40`), `VFS_FindMember` (`0x43ab96`), `VFS_OpenMember` (`0x43af27`), `VFS_Read` (`0x4093f0`) | Parse UBIK's 267-byte named-member rows and expose bounded member reads. Show every member under its own `.BF`, including Disc 2's `TITRES.SPR`; keep both discs' versions rather than applying the game's global overwrite rule. |
| `DDAT_LoadRecord` (`0x449bf9`), `RLE_UnpackZeros` (`0x448e25`), `ENT_LoadModel` (`0x41da7d`) | Index each disc's 150 projects and their scene/object filenames; search mounted roots for all physical candidates, retaining their discs and missing/ambiguous results. Do not use the retail mutable previous-record fallback. |
| `RES_ReadFile` (`0x41c666`), `DSN_LoadHeader` (`0x4175bc`), `DAN_OpenArchive` (`0x40fff7`), `DAN_Load3DA` (`0x4105eb`) | Enumerate DSN header map names and tag kinds, DAN material/clip directories and type kinds. Label `.3DA`/`.3DM` names as contextual logical resources, not physical ISO files. Resource decompression and relocation are later work. |
| `DRD_Open` (`0x41072c`), `DRD_LoadEntry` (`0x410928`), `FSB_Load` (`0x426143`) | Enumerate DRD's offset-indexed entries and FSB's size-indexed clips. Show tag/extent metadata and project dialogue links; leave voice, caption presentation and sound playback for later specs. |

Keep source entries keyed by disc, physical ISO path and indexed internal key;
a display name alone may repeat within one container. `VFS_Open`'s
OS-file-first fallback and later-archive overwrite rule describe the game's
runtime namespace; ODViewer's source catalog must not collapse these entries.

### Asset metadata exposed by ported functions

- **UBIK `.BF`:** Port `BF_Mount`, `VFS_FindMember` and the needed VFS reads.
  Expose their validated named-member table in stored order, retaining bounds
  checks at the portable file boundary.
- **`DREAMS.DAT`:** Port `DDAT_Load`, `DDAT_LoadRecord` and `RLE_UnpackZeros` with
  their required setup. Request each `ProjectN` through that path; expose the
  resulting `OBJET`, `LINK` and `LINKADVENT` fields for cross-source navigation.
  Preserve the original mutable previous-record fallback in the ported function;
  the catalog must validate a requested name before calling it so a missing
  record is not displayed as another project. Retain both disc copies.
- **DSN/DAN:** Port `DSN_LoadHeader`, `DAN_OpenArchive` and the retail readers
  needed for the object/material/clip directories and tag kinds. A logical
  `.3DA` inside DAN is not a standalone disc file. Report physical extents and
  decoded sizes only where the ported path establishes them; do not invent
  lengths for derived resources such as DSN-created `.3DM` textures.
- **DRD/FSB:** Port `DRD_Open`, `DRD_LoadEntry` and `FSB_Load` with their required
  offset/size handling. DRD's absolute `u32` offsets begin at `0x15`; entry
  0's header is at `0x2dd`, while its first RIFF payload is at `0x2eb`.
  Expose voice/caption/portrait tag presence and clip extents
  from the ported results; playback is later work.
- **Sprite sets and fonts:** Port `SPR_LoadSet`, `SPR_LoadIconBanks`,
  `SPR_GetDescriptor`, `TEXT_LoadFont` and their required tables before exposing
  slots, names or glyph descriptors. Aliased pointer slots and distinct payload
  records have different counts. Other `.SPR` families may gain supplemental
  viewer indexing after their bytes are read through the shared file path if
  no corresponding retail reader is found.
- **Models and textures:** Use the ported `ENT_LoadModel`, `RES_ReadFile` and
  related material paths for format-specific fields when available. Standalone
  `.3DC`/`.3DM` and `OBJET1.PAK` always remain findable as physical files. The
  verified one-chunk PAK layout may be exposed by a viewer-derived index after
  shared VFS loading if no retail reader covers it.
- **Video:** Port `VID_Open`, `VID_Close` and the necessary header/open path to
  classify HNM4, HNS6/HNM6 and UBB2/UBS2. Expose verified dimensions/frame
  counts from their state; frame decoding and playback are later work.
- **Text and manifests:** Use the relevant ported text/file paths where traced.
  `TEXT_LoadLanguageIni` is skipped by the examined English executable, so
  French `DREAMS.INI` labels must not be presented as English runtime text.
  Supplemental labels or `LISTL` references from shared-loaded files must be
  marked viewer-derived when no active retail path supplies them. Save examples
  and replay data remain physical entries unless indexed by a retail path or a
  documented viewer extension.

After ISO enumeration, invoke these shared paths for each source in staged,
state-safe work. A completed catalog/search includes every supported logical
child exposed by the ported functions or documented viewer extensions whether
or not its file was selected. Show **unindexed** when neither path covers a
format rather than claiming a complete internal inventory. Failure in one load
leaves its raw ISO file selectable with a format-specific error. An unknown
format remains a normal physical file; its extension alone does not establish
its type.

### CUE and raw-track handling

For the initial split-track layout, parse `FILE`, `TRACK`, `INDEX 00` and
`INDEX 01`, quoted filenames, whitespace and line endings. Validate track order,
supported file/track modes, index relationships and offsets against the backing
file. Handle non-layout metadata deliberately; unsupported layout/timing
directives must not be silently treated as comments.

Index timestamps are positions within the referenced file. Preserve that
relationship in the parsed representation. Distinguish a stored pregap before
`INDEX 01` from program content; do not concatenate files and accidentally apply
their indexes as offsets in one synthetic file.

A [Windows libcdio spike](../../reviews/002-libcdio-windows-spike.md) confirmed
that its unmodified BIN/CUE driver opens these multi-BIN CUEs but assigns
repeated track starts and fails to expose the ISO filesystem. A one-track CUE
for either data BIN works. Any library used here must pass the original CUE
layout and sector-byte tests, not just report a successful open.

For `MODE1/2352`, map each raw 2352-byte sector to its 2048-byte payload beginning
at byte 16, as documented by the existing Python reader. ISO logical reads must
work across sector boundaries and honour the parsed data-track start. Detect
truncated/out-of-range reads instead of silently dropping a partial sector.

Audio is metadata only in 002. Missing or unreadable audio backing files must
be visible in source/track status; an otherwise readable data track may still
be browsed. This does not claim that the image set is complete enough to play
the game.

### ISO 9660 navigation

Read the primary volume information, root directory, directory records and file
extents through the shared logical-sector reader. Support the structures actually
used by the target corpus, including directories spanning multiple sectors and
directory-record padding. Validate record lengths and extent bounds, and handle
parent/self entries without recursive traversal loops.

Use case-insensitive lookup for the game's DOS-style paths, while retaining the
source spelling for display and diagnostics. Normalize path separators. Handle
ISO file-version suffixes consistently for lookup while retaining the original
identifier in metadata when useful. Report ambiguous normalized names instead of
silently discarding one.

Enumerate directory metadata incrementally to build the complete source catalog;
read file byte ranges on demand. ISO traversal alone does not decode media,
stream all music, hash every movie or copy the disc into memory. Subsequent
ported loader calls may read, allocate or decode what their original call paths
require. Keep the browser usable while reporting mount/index work and failures;
this requirement does not prescribe a threading framework.

## Two discs and conflicting filenames

Expose **Disc 1** and **Disc 2** as separate roots in 002. A shared lookup includes
the disc identity; there is no implicit last-mounted-wins rule. Original files,
including development leftovers, stay available in their source-disc view.

The [merge research](../../disc-layout.md#merging-both-discs) explains why:
`DATA/HNM/INTRO.HNM` has different content on the discs, and the icon banks also
differ. `DREAMS.DAT` differs in six project records; the
[format findings](../../file-formats.md) do not establish one copy as universally
authoritative. Equal paths or equal sizes do not prove equal contents.

A unified runtime namespace and its per-path precedence policy can be added
when the game loaders need it. That work must use the research and preserve
provenance. It is not required to complete a browser that faithfully shows each
disc's actual directory tree.

Language directories are browsed as they exist on the images. Navigation does
not select the game's language, interpret French placeholders as runtime text,
or invoke the original language loader.

## Configuration, dependencies and portability

Reuse the framework/build stack from 001. Implement the required CUE, raw-track
and ISO support in ODShared as planned by the north star; no runtime dependency
on Python, 7-Zip, OS-mounted ISO volumes or the existing extract/bake pipeline.

Existing `DREAMS_DISC1` and `DREAMS_DISC2` settings describe **extracted
directories** for the toolkit. They must not silently become CUE paths. The UI
can supply image paths directly; exact command-line/configuration names and
optional persistence remain small implementation decisions. Local paths and
recent-file history belong in user/ignored configuration, never tracked source.

Use the same mount and navigation code on Windows, macOS and Linux, including
host paths with spaces and non-ASCII characters. Continue compiling both
applications for Emscripten. Browser access to user-selected disc images, packs
or HTTP content is deferred; a browser build may retain the foundation screen
with a clear indication that disc-image opening is currently desktop-only.

## Implementation plan

| Step | Work | Observable result |
|---|---|---|
| 1. Model and parse supported images | Establish the CUE/track representation, supported-layout checks and synthetic cases. | Correct file-relative indexes and clear errors, including the observed nonuniform audio index. |
| 2. Read data-track sectors | Add seekable bounded reads and raw-to-logical sector mapping. | Known byte ranges agree with an independent extraction/reference. |
| 3. Traverse ISO directories | Decode the required volume/directory metadata and expose directory entries and file ranges. | The original disc trees can be listed without extraction or payload decoding. |
| 4. Port retail asset paths | Refine the [initial function map](retail-functions.md) through Ghidra, then port its required BF/VFS, DDAT/RLE, DSN/DAN, DRD/FSB, sprite/font and video open/index paths plus their dependencies into ODShared; record implemented functions in `port-map.tsv`. | A shared function exercised from ODViewer produces the same structures and lookup results that ODRuntime can consume; no retail behavior is reimplemented in viewer code. |
| 5. Build and link the catalog | Invoke those shared functions for each mounted source, snapshot their exposed fields, add documented viewer-only details where retail has no function, resolve references and group source variants. | Supported logical children are searchable without manually opening parents; projects navigate to their assets, and every field identifies its retail or viewer provenance. |
| 6. Connect mounting and UI | Add the two-CUE setup, marker-based disc assignment, mount/index status, type-first sections, global search/filters, source trees, details and independent remount/unmount. | ODViewer starts in a useful both-disc asset view and can reveal every result in its source location. |
| 7. Validate the corpus and platform builds | Compare physical listings, logical indexes, classification and bytes; check search/filter and error/recovery paths, native operation and browser compilation. | A recorded navigation result covers both discs and every supported asset group, with failures and partial indexing reported explicitly. |

Do not make asset previews or restoration of gameplay code part of these steps.
ODRuntime remains runnable throughout and reuses ODShared without needing to
start the new browser UI.

## Validation

### Data-free checks

Use small self-authored fixtures or byte arrays, respecting the repository's
game-data exclusions. Cover CUE filenames/line endings, file-relative indexes,
missing files, invalid ordering/ranges, unsupported layouts, sector-boundary
reads, truncated sectors, directory padding, multi-sector directories, bounded
file reads, normalized lookup and invalid record/extent handling. Add focused
fixtures for BF table/member bounds and duplicate names, DDAT offsets and RLE
records, DSN/DAN name directories and tag bounds, DRD offset-table bounds and
entry extents, and FSB size-table bounds. Include sprite/font descriptor bounds
and aliases, BF-contained sprite sets and video-header classification through
their ported paths. A bad container must
leave its raw source file browsable. Check neither/both CD markers and two
selected images claiming the same disc identity, and test
that logical entries from one disc or mount never resolve through another by
accident.

Check mount replacement and unmount invalidate the correct entries. Exercise
paired mounting in either selection order, one-disc operation, global search
before/during/after indexing, collapsed branches, disc/kind/status filters,
source reveal and selection invalidation with synthetic content. Do not mirror
the implementation with tests that only restate its own calculations.

### Local corpus comparison

With the owner's images available:

- Compare track inventories with the CUE files and the Python track listing.
  Verify indexes independently because the Python parser does not expose them.
- Compare raw data-track payload ranges with the existing Python sector
  conversion, including reads spanning sector boundaries.
- Compare full per-disc normalized path/kind/size listings with independently
  extracted reference trees or a trusted ISO listing tool. Investigate expected
  naming/version differences rather than masking them.
- Compare file bytes or streaming hashes against independent extraction. A
  dedicated full-corpus run can cover all files without making the interactive
  browser read every payload at mount time.
- Check representative paths such as `DREAMS.DAT`, `DATA/3DC`, `DATA/HNM`,
  `DATA/ICONE` and `DATA/LANG`, empty directories, and the two distinct
  `INTRO.HNM` entries. Listing an entry is not a claim to understand its format.
- Compare BF members on both discs with the Python BF parser (5 and 6 for the
  inspected `ICONES.BF` files), including Disc 2's `TITRES.SPR`; check that
  selecting one disc never shadows the other's archive members.
- Compare each disc's 150 DDAT projects and source references with the Python
  project decoder. Preserve the six differing record copies, and report
  unresolved links rather than choosing a disc-wide precedence rule.
- Compare DSN/DAN names and tag/type inventories, DRD's 178 entry boundaries
  and FSB's 24 clip extents with the independent Python decoders. Check DRD
  entry 0 at `0x2dd` and its first RIFF at `0x2eb`.
- Compare sprite/font descriptor counts and menu names with the Python image
  decoder, and video kind/header classifications with the Python video reader.
  Check HNM4 animated textures
  are not placed among cutscenes and UBB files are not classified as BF members.
- Verify the catalog reaches a completed state and lists all supported internal
  entries before any container is manually expanded. Compare representative
  type, source and status filters against the independent source inventory.
- Audit each game-format index in ODViewer: retail-derived behavior names the
  ODShared function it calls, with address and evidence in
  `opendreams/port-map.tsv`; viewer-derived detail names the shared function
  that loaded its source data and the additional interpretation it performs.
  Exercise ported functions with an ODRuntime-compatible caller or shared
  harness and compare results. Reject viewer code that duplicates an existing
  retail-function port; accept supplemental inspection where none exists.

The existing recorded file counts and track counts are useful regression
references for this corpus, not hard-coded disc-identification rules. Keep
original images, extracted references and generated comparison output outside
tracked source. Missing licensed data produces an explicit skipped corpus check;
a requested corpus-validation run cannot report success without doing it.

## Acceptance criteria

- ODViewer launches without images and provides a two-CUE **Open game discs**
  setup; both original image sets mount together and receive marker-based Disc
  1/2 identities regardless of selection order. One valid disc remains usable.
- The required CUE indexes are parsed, including disc 2 track 5's `00:02:01`;
  unsupported layouts are identified explicitly.
- The left browser lists the named game-asset groups, has global search and
  kind/disc/status/extras filters, and exposes separate complete source trees.
  Completed search includes supported logical children of collapsed containers;
  partial indexing is visibly identified.
- Directory/file navigation and metadata work directly from each data track.
- Supported BF, DDAT, DSN/DAN, DRD, FSB, sprite/font and video metadata indexes
  are browseable under their owning physical files and through the asset groups.
  Retail-derived fields come from documented ODShared ports that ODRuntime can
  call. Viewer-derived fields may extend loaded assets where retail has no
  counterpart; a PAK child is allowed after shared VFS loading. Uncovered
  formats remain visible as unindexed physical files. Project references show
  every matching mounted-source entry with its disc identity, and results can
  reveal their source location.
- Logical resource names remain distinct from physical ISO filenames, and
  invalid indexes or missing references remain visible without breaking raw
  disc navigation.
- Audio tracks are listed separately with metadata and no playback controls.
- The browser preserves source identity and exposes conflicting paths on both
  discs without silently merging them. Logical-name and source-copy counts are
  distinct; equal names or sizes are not treated as proof of equal content.
- Names, sizes and file-read results agree with independent corpus references.
- Invalid input, missing files and remount/unmount operations leave the
  application in a clear, usable state with resources released correctly.
- ODRuntime still runs its foundation screen. Native builds remain portable and
  both applications still compile for the browser.
- No media previews, playback, editing, extraction or gameplay are needed to
  meet this spec. Required ported loaders may still read or decode payloads as
  part of their original behavior. Viewer-only decoding may add information
  missing from those functions but must not replace their behavior.

## Open details

- Exact image-path CLI/configuration names and whether to persist recent paths.
- Small control and list-presentation choices within the required left section
  browser, global search, filters, source view and details flow.
- Any additional CUE/ISO layout needed by newly examined legitimate releases;
  extend support only with evidence and fixtures.
- Whether to add a read-only source-catalog listing utility for automated
  comparisons or expose the same checks through the existing CTest setup.

These details can be settled during implementation. Asset preview features and
runtime disc-conflict resolution remain outside the navigation-only milestone.
