# 002 asset-loader dispatch findings

Scope: `WINDREAM.EXE` methods that resolve game assets above the OS/VFS read
layer: project records, DSN/DAN resources, dialogue records, and the UI icon
archive. Disc mounting, path selection, and VFS handle internals are covered by
the other 002 reviews.

## Method and independent cross-check

I decompiled `WINDREAM.EXE` from the existing Ghidra project with
`analyzeHeadless -noanalysis -readOnly -postScript Decompile.java` for the
addresses below. My initial repository reconnaissance had already surfaced
some docs and registry summaries before decompilation, so I cannot claim a
strictly blind review. For the subsequent code pass I stripped every C block
comment from the captured output, including `[NAME]` and `[DOCS_SYNC]` plates,
and based the behavioral findings on the code statements and call graph. The
project and registry were not modified. Since these functions already have
registry entries with prior two-source evidence, this pass is not presented as
new blind evidence for their names.

The current registry already contains entries for the named functions below
in [`re/names/WINDREAM.EXE.tsv`](../../re/names/WINDREAM.EXE.tsv). No manual
Ghidra rename is needed for this batch.

## Recovered lookup and read paths

### Project bank → scene/object assets

`GAME_Init` (`0x4156bf`) calls `DDAT_Load` (`0x448f5f`). The latter opens
`dreams.dat`, reads the self-indexed bank into the game buffer, and calls
`RLE_UnpackZeros` (`0x448e25`). `DDAT_LoadRecord` (`0x449bf9`) scans 150 names,
decompresses the chosen record into a shared `0x2200`-byte scratch record at
`0x65b344`, and returns its pointer; a missing name can fall back to the prior
record while the game is running. A browser should use owned record copies and
direct indexed lookup instead of reproducing that mutable fallback.

Within each decoded project, `OBJET0` carries the scene filename at record
offset `+0x60c` (`OBJET0 + 0x0c`); `OBJET1`–`OBJET15` carry model or prop
filenames in the same field. `SCENE_LoadLevel` (`0x41f9db`) passes object
records through `ENT_InstantiateFromObjet` (`0x41deb8`), which forwards the
asset name to `ENT_LoadModel` (`0x41da7d`). The project bank is therefore a
usable catalog root, not just an opaque game-data file. `LINK` entries connect
projects; for dialogue, `LINKADVENT +0x1c` supplies the one-based DRD entry ID
for opcode `0x40`, while `LINKADVENT +0x2c` can name a cutscene file.

Corpus check: the existing project reader parsed 150 projects from each disc.
Both `OBJET0` records for `Project0` name `H18ANGKR.DSN`; both parsed 711 live
object records in total. See [`project.py`](../../src/dreams/formats/project.py)
and [`file-formats.md`](../file-formats.md#dreamsdat--the-project-bank).

### Logical model name → DSN/DAN resources

`ENT_LoadModel` receives an object’s asset filename, constructs the same stem
with `.DSN` and `.DAN`, and probes under the data root using the literal format
`%sdata/3dc/%s` (`0x4c4664`). If a matching `.DSN` exists it opens it with
`DSN_LoadHeader` (`0x4175bc`); otherwise it clears DSN mode. It then opens a
matching `.DAN` with `DAN_OpenArchive` (`0x40fff7`) when present, requests the
object’s `.3DC` through `RES_Load`, optionally requests `.3DI`, and reads the
DAN animation chunks. If the object flags request clips, it calls
`ANIM_LoadEntitySet` (`0x404d98`).

`RES_Load` (`0x456e24`) is the resource-handle layer: it reserves arena space,
calls `RES_ReadFile` (`0x41c666`), relocates the returned resource, and returns
a resource handle or `-1`. `RES_ReadFile` first dispatches by the active
container and requested extension:

| Active source and logical suffix | Reader | What it resolves |
|---|---|---|
| DSN, `.3DC` | `DSN_LoadMaterialsAndFaces` (`0x4177f5`) | Tag 1 renderable scene/model graph |
| DSN, `.3DI` | `DSN_LoadVertexPool` (`0x4178fe`) | Tag 2 collision vertex pool |
| DSN, `.3DM` | `DSN_Create3DM` (`0x417a07`) | Finds the named map in the DSN header and creates a texture buffer; `DSN_LoadTextures` (`0x417afd`) fills its palette and tiles progressively |
| DAN, `.3DC` | `DAN_Read3DC` (`0x41020f`) | Decompresses the DAN type-1 model chunk; then `DAN_ReadTextureChunks` (`0x410318`) reads type-2 texture chunks |
| DAN, `.3DM` | `DAN_Load3DM` (`0x41053e`) | Uppercases and looks up an 11-byte texture/material name, then LZ-decompresses its data |
| DAN, `.3DA` | `DAN_Load3DA` (`0x4105eb`) | Matches the first eight characters in the 13-byte clip-name directory, then LZ-decompresses that clip |
| No matching container route | raw file read | Opens the supplied path; `.3DC` is prefixed using `FILE_GetDataRoot`; the reader consumes and discards the first eight header bytes before placing the body in the destination |

`DAN_OpenArchive` reads the DANF header, then two counted name directories:
11-byte names used by type-2 texture lookup and 13-byte `.3DA` clip names.
`DAN_ReadAnimChunks` (`0x410435`) reads type-3 chunks into the animation
buffer. `ANIM_LoadEntitySet` uses those clip names when a DAN is open; without
one, it searches `data\3dc\<stem>an???.3da`. It derives the runtime slot from
the two digits at clip-name offsets 6–7, loads the logical `.3DA` through
`RES_Load`, and fills missing slots from the nearest earlier clip. The shipped
corpus contains **no physical `.3DA` files**, so the DAN directory is the
important browsing source for animation names.

The return conventions differ by layer: DAN open and chunk-directory methods
return success/failure; DAN name lookups return the decompressed byte count or
zero; `RES_Load` returns a handle or `-1`. The raw `RES_ReadFile` path returns
the original file length on success, `-1` when open fails, and zero when the
file is too large for the supplied capacity.

The blind reading agrees with [`dsn-loader.md`](../dsn-loader.md#the-body-unpackers-and-call-hierarchy-verified),
[`models.md`](../models.md), and the existing checked names. The practical
asset-browser distinction is that a `.DAN` is a physical file containing
virtual `.3DC`, `.3DM`, and `.3DA` resources, while a scene `.DSN` supplies
named texture resources without standalone `.3DM` files.

### Dialogue bank → entry, captions, voice, portrait

`GAME_Init` passes `data\3dc\dialog.drd` (string at `0x4c3dc1`) to
`DRD_Open` (`0x41072c`), which opens it from the install root. It reads the
header, verifies `DRDF`, retains the entry-offset table, and allocates one
reusable entry buffer sized by the maximum-entry-size header field. Its return
value is boolean success. `DRD_LoadEntry` (`0x410928`) seeks to a zero-based
entry, reads its 9-byte entry header and the remainder into that buffer, then
parses tag 2 as WAVE bytes, tag 3 as timed text records, and an optional tag 4
as portrait data. Caption timings are converted by `15/100`. Loading the
already-current entry returns success without rereading. `DRD_SelectEntry`
(`0x410cd6`) is the boolean wrapper. `MENJ_Dispatcher` (`0x435896`) selects the
entry; the voice/caption path uses the same entry for sound, text, and portrait.

The project linkage is useful in a browser: for opcode `0x40`,
`LINKADVENT +0x1c` is a **one-based** dialogue ID; runtime event `0x40` queues
ID minus one. This is supported by [`sprites-ui-dialog.md`](../sprites-ui-dialog.md#dialogdrd-entry-event-to-voice-and-captions)
and [`file-formats.md`](../file-formats.md#linkadvent-fields-0x40-bytes-verified).

Corpus check: disc 1 has one 24,592,952-byte `DIALOG.DRD`; disc 2 has none.
[`dialog.py`](../../src/dreams/formats/dialog.py) parsed 178 entries, 589
nonempty timed lines, WAVE data in all 178 entries, and portraits in 169. The
first entry begins at `0x2dd`, and the final entry ends exactly at EOF.

**Correction for the research docs:** [`assets.md`](../assets.md#dialogdrd--voice-bank)
and [`file-formats.md`](../file-formats.md#drd--dialog-drdf) label `0x2eb` as
the first entry. Direct bytes and `dialog.py` show the first entry header at
`0x2dd`; `0x2eb` is the first WAVE/RIFF payload, 14 bytes later (9-byte entry
header plus 5-byte tag-2 header). Keep the record start and WAVE start distinct
in the spec and any decoder.

### UI archive → named sprite banks and named icon slots

`UI_InitIcons` (`0x4341eb`) passes `data\icone\icones.bf` (string at
`0x4c5243`) to `BF_Mount` (`0x43ad40`), then calls `SPR_LoadIconBanks`
(`0x426c46`) and `ICON_FindByName` (`0x427217`). `BF_Mount` verifies `UBIK`,
reads the table offset/count and named-file entries, then registers the members
with the VFS. `SPR_LoadIconBanks` opens five names from the executable table at
`0x49dacc`: `MAGIE.ALP`, `ANIM.ALP`, `PYRAM.ALP`, `TOUCHES.SPR`, and
`INTERF.ALP`. It reads each palette and descriptor table, then the pixel body
for populated descriptors. `ICON_FindByName` maps the executable’s 72 icon
labels to a `(bank, slot)` pair; it does not find a file on disc.

For a browser, enumerate every UBIK member by its stored name and preserve its
owning disc. Keep the retail-loaded five banks distinct from archive contents:
disc 2 adds `TITRES.SPR`, which is present in the archive but absent from the
executable’s five-name load list. The archive reader already validates the
UBIK member chain. See [`sprites-ui-dialog.md`](../sprites-ui-dialog.md#ui-asset-path)
and [`image.py`](../../src/dreams/formats/image.py).

Corpus check: disc 1 `ICONES.BF` has five members and is 372,358 bytes; disc 2
has six members and is 440,029 bytes. The parsed payload chains are exact on
both. Members on disc 1 are `MAGIE.ALP`, `ANIM.ALP`, `PYRAM.ALP`,
`TOUCHES.SPR`, `INTERF.ALP`; disc 2 adds `TITRES.SPR`.

## Spec 002 implications

The current draft explicitly treats `.BF` and `.DRD` as opaque files and
excludes payload decoding. That meets a raw ISO navigator; it does not meet the
requested asset browser. Add a semantic catalog layer while retaining the raw
per-disc directory view:

- Parse `DREAMS.DAT` per disc and group the 150 `ProjectN` records. Show the
  scene from `OBJET0`, related `.DAN`/`.3DC` object assets, project links, and
  dialogue/video references from `LINKADVENT`.
- Resolve each referenced logical model by its stem and container context.
  List DSN tags and its named maps; list DAN type-2 and type-3 names with their
  physical parent `.DAN`. Do not invent filesystem `.3DA` entries when those
  names are embedded in DAN.
- Expand UBIK `.BF` tables into named children. If icon-label lookup is wanted,
  expose the separate executable label-to-bank/slot mapping. Preserve
  disc-specific versions and the D2-only `TITRES.SPR`.
- Expand DRD into zero-based entry children and expose caption/voice/portrait
  relationships. Follow the project’s one-based `LINKADVENT` ID when jumping
  from a project record to a dialogue entry.
- Keep identities qualified by disc, physical path, and internal resource
  name. The loader’s logical names are contextual to the currently open DSN or
  DAN; flattening them into one global filename list can conflate unrelated
  resources.
- Keep reads bounded/on demand. No Ghidra loader routine requires a browser to
  load all disc payloads at mount time. Previews can be specified separately;
  locating/decoding these structures does not by itself define playback or
  rendering behavior.

Corpus reference for acceptance checks: across the two extracted discs,
`DATA/3DC` contains 95 unique DSN names, 191 DAN files, 32 `.3DC` files, eight
`.3DM` files, one DRD (disc 1), and no `.3DA` files. `DREAMS.DAT` contains 150
projects on each disc. The UBIK archive has five/six children on disc 1/2.

## Validation record

- Representative command (same flags used for the other address batches):

  ```powershell
  . .\tools\dreams-env.ps1
  & (Join-Path (Get-DreamsSetting DREAMS_GHIDRA_ROOT) 'support\analyzeHeadless.bat') ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly -scriptPath ghidra_scripts -postScript Decompile.java 0041c666 00456e24 0041da7d 00404d98 0040fff7 0041020f 00410318 00410435 0041053e 004105eb 0041072c 00410928 00410cd6 00448f5f 00449bf9 004341eb 0043ad40 00426c46
  ```

- Read-only headless decompilation completed for `RES_ReadFile`, `RES_Load`,
  `ENT_LoadModel`, `ANIM_LoadEntitySet`, `DAN_OpenArchive`, the DAN readers,
  DRD open/read/select and callers, DDAT load/lookup, `ENT_InstantiateFromObjet`,
  `GAME_Init`, `UI_InitIcons`, `BF_Mount`, and `SPR_LoadIconBanks`.
- C block comments were stripped before the code-behavior pass; due to the
  preliminary reconnaissance, treat it as comment-stripped rather than a
  strictly blind review. Existing registry evidence remains the source for
  the two-source name checks.
- Corpus parsers and raw file metadata were read directly from both configured
  extracted discs. No tests were run and no game data or generated outputs were
  written to the repository.
- The DRD `0x2eb` entry-start label is the one disagreement found in this
  batch; see the correction above.
