# July 1997 demo: the surviving editor and original engine types

Follow-up (2026-10-03): [cryo-editor.md](cryo-editor.md) describes the whole
editor (layout, keys, record lifecycle, wiring, July→October changes,
workflow). Two corrections to this page: the extracted trees show no field
for the LINKADVENT leaves because the extraction script omitted that working
record (`_CurrentSceneLinkAdventureS`), not because the binary lacks them;
and the Windows "Page Up" toggle is the DOS character `!` (0x21) kept as a
virtual-key code.

Date: 2026-09-29. Sources: the four executables in
`E:\dev_game\DREAMS_ISOS\DREAMS_WIP_PCJ\DREAMS`, imported into the separate
Ghidra project `ghidra/wip/dreams-july-1997.gpr`. This is static binary analysis;
the game and editor UI have not been run during this investigation.

## Ghidra project

The import copies are byte-identical to the source files and have a `WIP_`
prefix so feature/symbol exports cannot overwrite the retail exports.

| Program | Exported non-thunk functions | Direct original names registered |
|---|---:|---:|
| WIP_DREAMS.EXE | 2,543 | 2,413 |
| WIP_DREAMSFX.EXE | 2,869 | 2,734 |
| WIP_DREAMWIN.EXE | 1,651 | — |
| WIP_DRWIN.EXE | 1,651 | — |

The DOS names come from embedded Watcom code-symbol records, at their own
object-relative addresses in the identical imported executable. They are
direct original-build imports, not cross-build guesses. Ambiguous names and
multiple aliases at one entry were left out of the primary-name registry.
The complete raw symbols remain under `out/research/wip_pcj`.

`SeedWatcomDebug.java` authenticated the imported executable SHA-256 before
creating previously missing entries, without splitting any existing function.
It created 1,198 entries in the software build and 1,284 in Glide; 24/39
overlapping entries and 2/12 failures remain for boundary review. Some exported
functions are analysis-created fragments, so function totals are not source
function counts. All added names went through `re/names/WIP_*.tsv`,
`check_names.py`, and `Rename.java`; both DOS programs have exported snapshots
in `re/symbols/wip_*.tsv`.

Both Windows imports received the BSS extension, a literal-byte-verified
`__CHK` entry, and the existing Watcom prologue/switch recovery pass. That pass
raised each Windows program from 1,301 to 1,774 total Ghidra functions
(including thunks). Their names and parameter recovery still need further
work; the original-symbol DOS programs are currently the better starting point.

## A much more complete editor survives

All four binaries contain the same **351-node editor menu tree**, with
**259 leaves** and five root branches:

- Project: create/load/save/delete projects; object, link, adventure-event and
  box records; player start position, movement parameters, camera and lighting.
- Scene Particle: generation volume, speeds, lifespan, forces, attraction,
  mana gain, quantities and display options.
- Option: camera constants and animated-material options.
- Debug.
- Exit To DOS.

The retail Windows tree described in [spec 005](../specs/005-debug-tools/spec.md)
has only the root and Exit To DOS. The demo preserves the data for the full
menu, not just editor strings or unused record-page routines. Nodes are 64
bytes: 24-byte label, five children, a value pointer, editing parameters and
flags. The copied labels, links and value bindings were traversed from the
actual root with no dangling reads.

| Item | DOS software | DOS Glide | Both Windows demos |
|---|---|---|---|
| Menu root | 000c5478 | 000d9138 | 00495d28 |
| Editor mode flag | 000c5430 | 000d90f0 | 00495ce0 |
| Editor draw | 00022e18 | 00022ee4 | 00443380 |

In DOS debug symbols the names are `_editorObjetMain`, `_editor`, and
`WorksEdit_`. Editor functions belong to `C:\DREAMS\src\WORKS.C`. Examples
include `WorksCreatScene_`, `WorksEditCompObjet_`, `WorksGetEditor_`,
`sceneKeyboard_`, `GetAll3dcInDirectory_`, `LoadDiskScene_`, and
`SaveDiskScene_`. Original spellings, including `Creat` and `Seach`, are kept.

The complete extracted trees, including field bindings for the DOS working
records, are `out/research/wip_pcj/DREAMS.EXE.editor-tree.md` and the equivalent
files for the other three builds. `editor-trees.json` includes raw flags,
value pointers and auxiliary editing words.

## EDITOR.DAT is the startup bank, not merely a leftover

`LoadDiskScene_` at DOS `0001ef30` (Windows counterpart `0043ee4a`) opens
`editor.dat` and reads the 1,305,600-byte uncompressed project bank. It is
called by startup and presentation initialization. It first fills the mesh,
movie, texture-animation and symbol filename lists, selects project zero,
copies its 0x2200-byte record to the working record, and clears the dirty flag.

`SaveDiskScene_` at DOS `0001efd4` (Windows `0043eef1`) checks that dirty flag,
writes `editor.dat`, invokes `WOR_SceneRLECompress_`, and writes `dreams.dat`.
It is called by shutdown and keyboard handling. Thus the compressed
`DREAMS.DAT` is also an **export product of the surviving editor**.

This refines the initial package inspection: the two banks are different
snapshots, and the compressed bank's graph should not be taken as the demo's
startup graph. EDITOR.DAT has 138 populated projects, versus 134 in
DREAMS.DAT. The uncompressed Project 0 spawn is (-85,-750,-2813), versus
(-85,-1334,-2813) in the compressed snapshot. These are file-record values;
no new model-transform assumptions are made here.

## Input survives, but the editor draw is disconnected

The demo Windows event producer at `0042029a`:

- polls `GetAsyncKeyState`, posts new virtual-key presses as message `0x33`;
- calls `GetCursorPos` and `ScreenToClient`, clamps the cursor to 640 by 480,
  computes deltas, and posts mouse movement;
- reads left/right mouse press/release edges and posts button messages.

This is the mouse producer missing from the examined European retail build.
The code explicitly puts the virtual-key index in the keyboard event, so the
following Windows shortcut interpretation is grounded in the producer:

- **Page Up (`0x21`)** saves `data\game.dat`, then toggles the editor flag if
  the current width is 640. The condition does not test the height.
- **F10 (`0x79`)** calls the editor-bank save routine.
- The menu labels retain record shortcuts such as Project Create Shift+A,
  Project Load Shift+Q, Project Save Shift+W, and Object Create Shift+Z.
  The complete behavior of every labelled shortcut is not yet validated.

However, **WorksEdit_ has no static call/jump or function-pointer reference in
the four demo executables**. The DOS frame handler `MCM1_Dispatcher_`
(`00011094`) calls drawing, HUD, debug information and `DreamsKey_`, but not
WorksEdit_. The Windows editor draw is also a complete retained body with no
caller/pointer. A Page Up toggle therefore does not prove that the UI opens:
the missing frame-loop draw link still needs restoration and a runtime test.
No executable or running process was patched in this pass.

The early editor is nevertheless much closer to usable than retail:

| Part | July demo | European retail Windows |
|---|---|---|
| Menu tree | 351 nodes / five branches | Root plus Exit To DOS |
| Editor toggle setter | Present in hotkey handler | Removed |
| Windows mouse producer | Present | Removed |
| Directory-list initialization | Called by LoadDiskScene_ | Calls removed |
| Mesh filename stride | 13 bytes | Broken one-byte stride |
| Editor draw call | Not found | Not found |

`GetAll3dcInDirectory_` at DOS `0001e818` scans `*.3dc`, `*.dan`, and `*.dsn`.
The decompilation and instruction arithmetic both show 13-byte list entries.
This provides direct reference behavior for the broken retail file picker.

## Original engine layouts recovered

`re/tools/watcom_types.py` decodes the type record streams, including the
variable-width type indices, explicit structure sizes and member offsets.
TYPE_EOF is respected: following source cue/file data is not mistaken for
type records. It finds 89 distinct named (name, size) layouts across 14
modules, including VESA and Glide library definitions as well as game engine
structures. This is not a claim of 89 wholly new engine types.

Nine core layouts were reconstructed in `re/structs/wip-engine.h` and imported
into **WIP_DREAMSFX.EXE only**, under distinct WIP-prefixed names. External
pointer targets remain opaque where the subset does not define their type.
`CheckTypeLayouts.java` verified **all nine sizes and 75 member offsets and
lengths** against `re/reviews/wip-type-layouts.json` in the saved Ghidra program.

| Original type | Bytes | Examples of recovered information |
|---|---:|---|
| C3D_VERTEX | 40 | v_flags, v_local, v_global, v_screen, v_unz |
| C3D_BOX | 280 | v0, Xe, Ye, transformed vertices, minv/maxv |
| _objet | 220 | Named parent/child/sibling links, local/global transform fields, geometry pointers, lights and virtual vertices |
| _light | 148 | Local/global position, matrices and directions; near/far and intensity |
| _CSPHERE | 56 | Centre, radius, axis bounds, overlap/collision linked-list heads |
| C3D_ANIM_ROT / C3D_ANIM_POS | 20 / 16 | Original animation-key members |
| C3D_ANIM_ROT2 / C3D_ANIM_POS2 | 60 / 48 | Extended animation-key members |

An especially useful review target is `_objet`: the debug layout calls
`+0x84` **o_nbr_uvtext** and `+0x88` **o_ptr_uvtext**. Our retail MDL_Node
header currently calls those `unknown_84` and `vertices_end`. It also omits
the demo's virtual-vertex fields at `+0xd4/+0xd8`. These are concrete leads,
not automatically applied retail corrections: retail field accesses and data
must decide whether a layout changed. Similarly, the original name `o_gpos`
does not establish that every runtime use is in world space rather than
camera space. No coordinate-space or pointer-sentinel behavior was changed.

## Further retail transfers applied

An additional **35 literal-byte-identical names** were verified and saved:
25 in retail DREAMS.EXE and 10 in DREAMSFX.EXE. They include
`WOR_RLEDecompress_`, `ANI_CopyAnimObjet_`, `SYM_TestSymbole_`,
`PHY_SetPhy23D_`, the sword/bow/gun active-object tests, and mathematical and
collision helpers. Sources and body hashes are in
`re/reviews/wip-dos-byte-matches.tsv`. The matched bodies have no LE fixups;
both original file bytes and loaded comparison bytes agree.

Only those checked rows were applied. The retail registry now passes 25
names for DREAMS.EXE and 298 for DREAMSFX.EXE. Original names and provenance
are saved in their Ghidra programs and exported symbol files. No retail
prototypes, port coverage or owner-reviewed flags were changed.

## Reproduction and next experiments

Core commands, after copying each source to an ignored WIP_-prefixed import
file and importing it with FixWatcomBss.java:

```powershell
uv run python re/tools/watcom_debug.py <demo>\DREAMS.EXE --out out/research/wip_pcj/DREAMS.EXE.symbols.tsv
uv run python re/tools/ghidra_headless.py ghidra/wip dreams-july-1997 -process WIP_DREAMS.EXE -noanalysis -postScript SeedWatcomDebug.java out/research/wip_pcj/DREAMS.EXE.symbols.tsv <source-sha256> -postScript ExportFunctionFeatures.java
uv run python re/tools/register_watcom_debug.py <demo>\DREAMS.EXE out/research/wip_pcj/import/WIP_DREAMS.EXE
uv run python re/tools/check_names.py WIP_DREAMS.EXE --renames
uv run python re/tools/ghidra_headless.py ghidra/wip dreams-july-1997 -process WIP_DREAMS.EXE -noanalysis -postScript Rename.java @out/ghidra/match/names-WIP_DREAMS.EXE.tsv -postScript ExportSymbols.java
uv run python re/tools/watcom_types.py <demo>\DREAMSFX.EXE --out out/research/wip_pcj/types.json
uv run python re/tools/ghidra_headless.py ghidra/wip dreams-july-1997 -process WIP_DREAMSFX.EXE -noanalysis -postScript ApplyTypes.java re/structs/wip-engine.h -postScript CheckTypeLayouts.java re/reviews/wip-type-layouts.json
uv run python re/tools/check_wip_names.py --demo <demo> --evidence re/reviews/wip-dos-byte-matches.tsv
```

For Glide use its own SHA-256 and symbol export. The Windows imports use the
checked __CHK registry row followed by CreateWatcomFunctions.java apply.
Import logs, decompilations, menu trees and decoded type records are saved in
`out/research/wip_pcj`; Ghidra databases and executable copies remain ignored.

The most focused next experiment is to restore the Windows editor draw in a
disposable working copy, validate the startup file paths and mouse interaction,
then exercise a project/asset picker and a save/export round trip. A separate
analysis can transfer engine layouts after checking the retail readers and
writers. Neither a working editor UI nor a complete retail type migration is
claimed by this investigation.
