# 002 — initial retail-function port map

Status: **planning map, not an implementation record**. The file-root getters,
VFS/BF, DDAT/RLE, STRM/DSN header and DAN directory paths have been adapted
in `opendreams/`; the other asset paths below remain to be ported. Record
completed functions in
`port-map.tsv` under the policy in
[`PORT_MAP.md`](../../../opendreams/PORT_MAP.md) only when code is implemented.

This map starts from the checked
[`WINDREAM.EXE` name registry](../../../re/names/WINDREAM.EXE.tsv),
[asset-access research](../../asset-access.md), and the focused
[file-path](../../reviews/002-disc-file-paths.md),
[VFS](../../reviews/002-vfs-archives.md), and
[asset-dispatch](../../reviews/002-asset-loader-dispatch.md) reviews. Addresses
are `WINDREAM.EXE` virtual addresses; `GDIDREAM.EXE` is its checked twin. The
table identifies entry points and known dependencies, not the complete call
closure. Before porting each group, inspect its Ghidra body, callees, globals,
allocation and cleanup paths. Use the registry's current checked names; older
review prose contains earlier VFS names.

## Boundary for 002

The viewer selects a disc and calls the **same ODShared retail-function port**
that ODRuntime will use to open or load game data. The new CUE/BIN, sector and
ISO layer supplies files to those functions in place of OS-mounted CD paths.
The catalog snapshots results with `(disc, physical path, internal key)` and
may add read-only viewer-derived details after shared loading where no retail
function provides them. It must not duplicate a retail reader. The retail
function's address and implementation go in `port-map.tsv`; a viewer-derived
decoder has no invented retail address and records which shared call supplied
its input.

### Required shared entry points

| Order | Retail functions to port | Use in 002 and known dependency |
|---|---|---|
| 1. File access | `FILE_GetDataRoot` `0x004285e9`, `FILE_GetInstallRoot` `0x004285be`; `VFS_Open` `0x00409304`, `VFS_Read` `0x004093f0`, `VFS_Seek` `0x00409634`, `VFS_Close` `0x0040985e` | The asset loaders' path and byte access. Adapt their OS file boundary to the selected ISO source while preserving the retail functions' game-facing behavior. `DRD_Open` uses the install-root getter. The catalog, unlike the game, keeps both physical sources separately. |
| 2. BF archive | `BF_Mount` `0x0043ad40`, `VFS_AddArchiveEntries` `0x0043ac08`, `VFS_FindMember` `0x0043ab96`, `VFS_OpenMember` `0x0043af27`, `VFS_FreeArchives` `0x0043aefc` | Mount UBIK and expose its member rows through the ported VFS. Retail registration replaces same-name members across mounts, so index one archive/source at a time and snapshot its rows before switching. Keep row index as the browser identity; it distinguishes repeated names. The five icon banks and Disc 2's sixth `TITRES.SPR` remain source entries even though the retail icon loader requests only five. |
| 3. Projects | `DDAT_Load` `0x00448f5f`, `DDAT_LoadRecord` `0x00449bf9`, `RLE_UnpackZeros` `0x00448e25`; `DDAT_InitEmptyRecords` `0x00448c6d` if the load path needs it | Obtain each `ProjectN` 0x2200-byte record through the retail DDAT path. Preserve the original previous-record fallback inside the port; validate names in the catalog before calling it and copy each result before loading another disc/record. Scene, object, link and dialogue-reference listings are read-only views of that result. |
| 4. DSN header | `DSN_InitState` `0x004174e7`, `DSN_ResetState` `0x0041754e`, `DSN_LoadHeader` `0x004175bc`; stream path `STRM_Open` `0x004152f2`, `STRM_Fill` `0x004153fe`, `STRM_Peek` `0x004154db`, `STRM_Commit` `0x004155a2`, `STRM_Close` `0x004153bf` | Open scenes and obtain their fixed map/object name directory from the retail path. The shared stream uses mutable state; close or reset it between sources. A read-only tag inventory beyond fields exposed by these calls may be viewer-derived from bytes supplied by the shared file/stream path. |
| 5. DAN directory | `DAN_OpenArchive` `0x0040fff7`, `DAN_ReadAnimChunks` `0x00410435`, `DAN_GetAnimCount` `0x0041069d`, `DAN_GetAnimName` `0x004106c9`, `DAN_CloseArchive` `0x0040ffb2` | Open each DAN through the retail path; expose its 11-byte material directory and 13-byte clip directory with parent/source identity. The count getter reads state filled by `DAN_ReadAnimChunks`, so verify that call order in Ghidra and preserve its reads. Do not present a `.3DA` label as an ISO file. Close the current archive before selecting another. |
| 6. Dialogue | `DRD_Open` `0x0041072c`, `DRD_LoadEntry` `0x00410928`, `DRD_GetLineCount` `0x00410bc7`, `DRD_GetPortrait` `0x00410b8c`, `DRD_Close` `0x004108c7` | Index the bank and load each numbered entry through retail functions; expose WAVE, caption and portrait presence and project links. `DRD_LoadEntry` reuses one buffer, so snapshot an entry before loading the next. The bank exists on Disc 1 in the inspected corpus. Caption presentation and voice playback are later work. |
| 7. Effects | `FSB_Load` `0x00426143`, `FSB_GetSample` `0x0042639d`, `FSB_Free` `0x00426340` | Load the FSB bank and expose its numbered clip table from retail state. `FSB_Load` reads the contiguous WAVE blob; preserve that behavior rather than creating a second size-table reader. Playing samples is later work. |
| 8. UI sprites | `SPR_LoadIconBanks` `0x00426c46`, `SPR_FreeIconBanks` `0x00426f6f`, `ICON_FindByName` `0x00427217`; `SPR_LoadSet` `0x00425254`, `SPR_GetDescriptor` `0x00425593`, `SPR_FreeSet` `0x00425630`; `TEXT_LoadFont` `0x00425c61`, `TEXT_FreeFont` `0x00425d77` | Inspect the five retail-loaded BF icon banks, executable name-to-slot mapping, standalone `SOUR.ALP` and HI font descriptors using the shared port. `SPR_LoadIconBanks` has a fixed five-name list; BF enumeration still shows Disc 2's extra member. Other sprite families or additional slot detail may use a documented viewer decoder after shared VFS loading if no retail routine provides it. |
| 9. Video kind | `VID_Open` `0x004085dc`, `VID_Close` `0x0040887b` | Use retail magic dispatch to distinguish HNM4 animated textures, UBB2/UBS2 movies and HNM6/HNS6 cutscenes. Open/close must release stream and sound state. Header fields not surfaced by the port may be viewer-derived from the bytes it opened. No frame decode or playback is required for 002. |

These are the **initial entry-point set**, not a claim that every listed helper
must be independently callable from the UI. ODViewer can call a higher ported
entry point which reaches its retail callees. The port should retain original
state transitions and error behavior where practical, while the disc/file
boundary makes user-supplied image errors recoverable. The exact dependency
closure, especially memory allocation, `STRM_*` refill and video sound setup,
needs a function-by-function Ghidra pass before implementation.

### Retail paths to inspect, then include only if their output is needed in 002

| Retail path | Why conditional |
|---|---|
| `DAN_ReadTextureChunks` `0x00410318`, `DAN_Read3DC` `0x0041020f`, `DAN_Load3DM` `0x0041053e`, `DAN_Load3DA` `0x004105eb`, `LZ_Unpack` `0x0049afd1` | The opener and required animation-chunk read supply the clip directory. These further routines read/decompress model and texture payloads. Port them in 002 if a promised tag or size detail genuinely depends on their state; otherwise they belong with later model/animation previews. Do not write a second version of their decompression in ODViewer. |
| `DSN_LoadMaterialsAndFaces` `0x004177f5`, `DSN_LoadVertexPool` `0x004178fe`, `DSN_Create3DM` `0x00417a07`, `DSN_LoadTextures` `0x00417afd`, `LZ_Unpack` `0x0049afd1` | These are retail DSN geometry, collision and texture payload paths. The scene name directory needs only `DSN_LoadHeader`; a viewer-derived tag-header inventory can avoid claiming decoded geometry. Port the deeper functions when 002 uses their results or in the later preview milestone. |
| `ENT_LoadModel` `0x0041da7d`, `RES_Load` `0x00456e24`, `RES_ReadFile` `0x0041c666`, `RES_Relocate` `0x00456368` | Retail model/resource dispatch and relocation. These are the right shared paths for actual model loading, but a project reference or a DAN/DSN directory does not require full model instantiation. Port when the viewer requests resource-level fields or previews, rather than copying their behavior into catalog code. |

### Evidence and policy, not required ports for 002

| Retail function/path | 002 decision |
|---|---|
| `CD_FindDrive` `0x00427f11`, `CD_GetDiscNumber` `0x004287fa`, `CD_InitPaths` `0x00427d0e`, `CD_PrepareLevel` `0x00427d64`, `CD_CopyFileList` `0x00428356` | These implement OS drive probing, the game's one-disc-at-a-time rule and install-cache copying. They establish marker and manifest semantics; the browser's two-CUE identity check and full ISO inventory are new support code. Port this retail policy for ODRuntime when its disc/game startup path needs it, not to make the viewer work. |
| `CD_GetTrackCount` `0x00404786`, `CD_PlayTrack` `0x004044fa` | MCI audio control does not enumerate a CUE image's backing files or indexes. 002 lists audio tracks through the new CUE reader; shared playback ports come later. |
| `UI_InitIcons` `0x004341eb` | Retail boot/UI orchestration calls BF and sprite loaders. The browser can call those shared lower-level functions without starting the original menu initialization. Restore this caller when the runtime UI is ported. |
| `TEXT_LoadLanguageIni` `0x00433cfa` | The examined English executable jumps over the parser body. Shipped French labels are source text, not proof of runtime English labels. The viewer may show attributed text read through shared file access; that does not make the dead retail path active in ODRuntime. |
| `SCENE_LoadLevel` `0x0041f9db`, `ANIM_LoadEntitySet` `0x00404d98`, `DRD_PlayVoice` `0x00410d19`, `VID_DecodeFrame` `0x00408816`, `GAME_Init` `0x004156bf` | These advance full scene initialization, animation, playback or game startup. Their relationships explain the assets but are not required to list them in 002. Later preview/runtime work ports them with the same shared-loader rule. |
| `OBJET1.PAK`, `DATA/OBJET` sprite sheets, text manifests | A format/file, not a known retail function. First obtain its bytes through the ported shared VFS path; a documented read-only viewer index may expose extra entries without inventing a retail mapping. Keep provenance and uncertainty visible. |

## First implementation gates

1. Verify the exact callees and mutable state of each required entry point in
   read-only Ghidra, especially cleanup and source switching. Update this map
   when an apparent helper turns out to be essential or has a different scope.
2. Port VFS and its portable file boundary first, then prove the same call can
   read a physical file and an indexed BF member from the selected image.
3. Bring up one source through DDAT, DSN/DAN, DRD/FSB, sprite and video open
   paths in that order; compare each result against the Python oracle and the
   measured corpus. Repeat on the other disc and across mount replacement.
4. Record actual ported functions in `opendreams/port-map.tsv` and audit that
   ODViewer calls them. Label supplementary viewer fields separately; never
   assign them a fabricated retail address.
