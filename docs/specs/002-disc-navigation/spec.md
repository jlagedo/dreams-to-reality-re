# 002 — Disc-image loading and navigation

Status: **Draft — planning only**  
Date: 2026-09-26  
Depends on: [001 — project initialization](../001-project-init/spec.md)

## Goal

Open the two original Dreams to Reality disc images in **ODViewer** and browse
their contents using the SDL3, sokol and Dear ImGui foundation from spec 001.
The user supplies cue/bin files; no extraction, installation or Python bake
step is needed to navigate them.

**This is a navigation-only asset browser.** It displays directories, filenames,
sizes, source-disc information and track metadata. It does not decode, render
or play the assets inside those files.

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
| Navigate directories and select files; inspect basic metadata | Decoding DSN, DAN, DRD, BF, sprites, textures or other game payloads |
| List audio tracks as disc metadata | Music, sound, dialogue or video playback |
| Bounded byte reads for shared access and verification | Model/map/image previews, animation and thumbnails |
| Remount/unmount and useful error messages | Editing, exporting or writing game files |
| Keep native builds and Emscripten compilation working | Browser image upload, HTTP packs, caching and delivery |

Original game archives such as `.BF` and `.DRD` appear as files. Expanding their
internal entries is format-decoder work for a later spec. File extensions can
label entries, but do not establish that their contents have been decoded or
validated. Installer programs and other executable files are listed, never run.

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

## User flow

1. Launch ODViewer. Without configured images, show an empty browser with
   **Open disc image** controls and a short explanation of the required `.cue`
   input. Do not require data just to launch.
2. Select the CUE for either disc. Mount it, identify the disc from its contents,
   and show its root directory and track list. The user can open the second
   disc independently; opening both is the normal completed setup.
3. Select a disc and navigate its directory tree or directory listing. Show the
   current path and an up/root navigation action. Keep each disc's location
   separate so switching discs does not lose the user's place.
4. Select a file to see its path, byte size, extension/type hint and source
   disc. Double-clicking a directory enters it; selecting a file does not
   invoke a decoder or open a media preview.
5. Inspect the track list for track number, mode, backing filename and parsed
   index positions. No playback controls appear in this spec.
6. Close or replace a mounted image. Release its files and invalidate selections
   belonging to it. A failed replacement leaves the previous valid mount usable.

Use the existing Dear ImGui integration. A simple disc selector, directory/file
view and details area are enough; multi-window docking and an editor layout are
not requirements. Display an extension as an extension, not a claim that the
application supports rendering that format.

The proposed input UI uses SDL3's native file selection where available, with
an editable path as a development fallback. Cancelling selection changes nothing.
Exact control arrangement is an implementation choice within this flow.

Opening one valid disc while another is missing is useful for inspection and
must remain possible. Show mount failures beside the affected source rather than
turning them into a fatal application error. Browser mode is not the game's
future requirement to have both discs available to play.

## Shared disc access

Put CUE parsing, raw-sector access, ISO traversal and mount ownership in
**ODShared**, initially alongside the new disc-support code planned in 001.
ODViewer owns browsing state and the UI. There is no new dependency-free library
requirement and no need to reorganize recovered game functions for this work.

The original engine accessed mounted media/files through its OS services. Reading
cue/bin containers directly is new portability support. Keep that distinction
clear; these new routines are not recovered Cryo functions. Existing game loaders
will use this access later, with their original relationships preserved.

The shared functionality must support, without prescribing a class hierarchy:

- Opening and closing a disc source, with a stable identity for its mount lifetime.
- Reporting its track inventory and supported data-track access.
- Enumerating a directory and looking up a file by an explicit disc and path.
- Reporting an entry's kind, name, byte size and source identity.
- Reading a bounded slice of a file without loading its entire payload.
- Returning a useful error when input is invalid or a read cannot be completed.

Use 64-bit host-file offsets and checked size arithmetic. Preserve on-disc field
widths while decoding; do not cast raw records to pointer-bearing host structs.
Reads and mount replacement must not leave dangling UI selections or open-file
resources tied to an invalidated mount.

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

Enumerate directories and read file ranges on demand. Mounting does not decode
assets, stream all music, hash every movie or copy the disc into memory. Keep the
browser usable while reporting mount/index work and failures; this requirement
does not prescribe a threading framework.

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
| 4. Connect mount lifecycle and UI | Add opening, disc identity/status, per-disc navigation, metadata selection and unmount/remount. | ODViewer browses both sources using the 001 ImGui integration. |
| 5. Validate the corpus and platform builds | Compare listings and bytes; check error/recovery paths, native operation and browser compilation. | A recorded navigation result for both discs, with unsupported cases and missing data reported explicitly. |

Do not make asset previews or restoration of gameplay code part of these steps.
ODRuntime remains runnable throughout and reuses ODShared without needing to
start the new browser UI.

## Validation

### Data-free checks

Use small self-authored fixtures or byte arrays, respecting the repository's
game-data exclusions. Cover CUE filenames/line endings, file-relative indexes,
missing files, invalid ordering/ranges, unsupported layouts, sector-boundary
reads, truncated sectors, directory padding, multi-sector directories, bounded
file reads, normalized lookup and invalid record/extent handling.

Check mount replacement and unmount invalidate the correct entries. Exercise
UI selection/navigation with synthetic content where helpful; do not mirror the
implementation with tests that only restate its own calculations.

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

The existing recorded file counts and track counts are useful regression
references for this corpus, not hard-coded disc-identification rules. Keep
original images, extracted references and generated comparison output outside
tracked source. Missing licensed data produces an explicit skipped corpus check;
a requested corpus-validation run cannot report success without doing it.

## Acceptance criteria

- ODViewer launches without images and provides usable image-opening controls.
- Both original CUE/BIN image sets can be mounted together and independently.
- The required CUE indexes are parsed, including disc 2 track 5's `00:02:01`;
  unsupported layouts are identified explicitly.
- Directory/file navigation and metadata work directly from each data track.
- Audio tracks are listed separately with metadata and no playback controls.
- The browser preserves source identity and exposes conflicting paths on both
  discs without silently merging them.
- Names, sizes and file-read results agree with independent corpus references.
- Invalid input, missing files and remount/unmount operations leave the
  application in a clear, usable state with resources released correctly.
- ODRuntime still runs its foundation screen. Native builds remain portable and
  both applications still compile for the browser.
- No asset previews, game-payload decoders, playback, editing, extraction or
  gameplay are needed to meet this spec.

## Open details

- Exact image-path CLI/configuration names and whether to persist recent paths.
- Small UI choices such as tree versus list navigation and optional filtering.
- Any additional CUE/ISO layout needed by newly examined legitimate releases;
  extend support only with evidence and fixtures.
- Whether to add a read-only directory-listing utility for automated comparisons
  or expose the same checks through the existing CTest setup.

These details can be settled during implementation. Asset preview features and
runtime disc-conflict resolution remain outside the navigation-only milestone.
