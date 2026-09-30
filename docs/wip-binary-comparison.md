# July 1997 demo: binary comparison and Ghidra improvements

Further Windows transfer: [148 new names and 79 original-name annotations](wip-windows-transfers.md) now pass the stricter loaded-body and address checks.

Follow-up: [editor and type-table exploration](wip-editor-discovery.md) imports all
four builds, decodes the retained types and applies further checked DOS names.

Compared on 2026-09-29 with the configured European retail binaries. Source:
`E:\dev_game\DREAMS_ISOS\DREAMS_WIP_PCJ\DREAMS`. The Windows linker dates are
July 10, 1997; the European reference is October 29, 1997. The demo's DOS
executables have embedded Watcom debug tables. No demo executable was run.

## Windows code continuity

Searching the earlier `DREAMWIN.EXE` code section for complete retail
`WINDREAM.EXE` function bodies found **1,151 matches out of 1,944 functions**
in the current Ghidra feature export. Those bodies account for 341,704 bytes;
188 match literally, and the remainder match after normalizing relocation and
direct branch/call operands. **[verified for this comparison method]**

This is a lower bound on shared code, not a claim that every other function
changed behavior. The comparison excludes functions under eight instructions,
noncontiguous or incompletely decoded bodies, ambiguous matches, and changes
in instruction encoding. It preserves scalar constants, structure offsets,
registers and instruction bytes. Branches inside the matched body must retain
their relative destinations. There are zero conflicts among outgoing
call/branch targets that have matched counterparts. The source function starts
are inferred from matching code, not an authoritative Windows debug table.
Data-address identity and every unmatched call target have not been proven;
normalized matches alone do not establish identical runtime behavior.

The companion Windows demo executables differ in just two byte positions:
one timestamp byte and one code immediate. The retail Windows twins have
their separately documented backend-selection difference. This pass compares
the main Windows demo directly and checks both retail Windows twins for the
symbol transfers below.

## Original names recoverable from DOS debug symbols

The two DOS demo builds were independently compared with each retail build.
Results below are the union by retail address, not a sum of duplicate hits.
Counts of unnamed functions describe the state **before** the six additions
below. The table includes runtime/library functions in its first column.

| Retail target | Matched complete bodies | Game/engine bodies | Previously unnamed game/engine bodies |
|---|---:|---:|---:|
| DREAMS.EXE | 634 | 268 | 268 |
| DREAMSFX.EXE | 451 | 164 | 146 |
| WINDREAM.EXE | 228 | 169 | 112 |
| GDIDREAM.EXE | 228 | 169 | 112 |

These matches require the same instruction bytes and operand-mask positions,
apart from known LE/PE fixups and direct control-flow operands, with matching
internal branch destinations. Bodies must match in size and instruction count.
The source extent is bounded by the next debug code symbol, with trailing
alignment NOPs removed. Matching is unique in both directions among the
eligible functions. Known matched call targets do not conflict.

The independent DOS demo builds support 511 of the retail DOS matches, 422
of the retail Glide matches and 55 of the Windows matches. They assign no
conflicting original names to those shared candidates.

The source symbols use LE segment/object-relative offsets; the comparison
uses object preferred bases, matching the Ghidra loader. The custom LE reader's
relocated bytes at retail DREAMSFX `00020058` were checked against 128 bytes
read from Ghidra, including code and data relocations. Unhandled selector/far
fixups are excluded from candidate bodies.

### What the names add

Initial normalized body matches (names shown before the [Windows follow-up](wip-windows-transfers.md)):

| Retail Windows address | Existing name | Demo's original spelling | Source module |
|---|---|---|---|
| 0045d420 | PHYS_SweepAxis | Update_CSphere_ | 3DC_COL2.C |
| 0045a03c | ANIM_EvalTrackSpline | Anim_Rot3_ | 3DC_ANIM.C |
| 00456038 | MDL_LoadMaterials | Update_Map_ | 3DC_MEM.C |
| 0047b3d0 | REND_TransformLights | Compute_Face_Illum_ | 3DC_HIER.C |
| 004619bc | unnamed | ClosestEdgeEdge_ | 3DC_COLL.C |
| 00462d78 | unnamed | EdgeFace_ | 3DC_COLL.C |
| 00457c90 | unnamed | Get_World_Matrix2_ | 3DC_MEM.C |
| 00457cd0 | unnamed | Get_World_Pos2_ | 3DC_MEM.C |
| 0046c8ec | unnamed | BT_Linear_ | 3DC_SL.C |
| 0046fe20 | unnamed | BT_Perspective_ | 3DC_SL.C |

The source-module attribution is particularly useful: the renderer names
previously recovered from localized retail object fragments are now explicitly
owned by `3DC_HIER.C`. Existing descriptive names remain useful behavioral
notes; original names can complement them. A discrepancy between the names
does not itself prove the existing behavioral analysis wrong.

## Six literal-byte transfers applied

The first application deliberately uses full bodies with **no differing bytes
and no address normalization**. The source spelling and owning module come
from the embedded symbol table. Each body is unique among demo code-symbol
entries and matches both Windows binaries. All six were unnamed in Ghidra.

| Windows address | Original name now in both Windows programs | Body bytes |
|---|---|---:|
| 00456e0c | Check_File_ | 24 |
| 00457884 | Get_Root_ | 19 |
| 0045baf8 | Vect_Morph_ | 79 |
| 0045bb98 | Mat_Cpy_ | 32 |
| 004616ac | ProjectionOnLine_ | 312 |
| 004618b0 | PlaneLineIntersection_ | 267 |

Their decompilations were inspected as an additional sanity check. This was
not a blind review and is not claimed as one; the independent evidence is
the literal byte match. No prototypes, function bodies, port coverage or owner
review flags were changed. The trailing underscores preserve Watcom's
original decorated spellings.

Evidence is in `re/reviews/wip-byte-matches.tsv`: source binary SHA-256,
source and target addresses, module, size, and body SHA-256. The registry is
`re/names/WINDREAM.EXE.tsv`. Both Windows programs have saved `[NAME]`
provenance comments, and `re/symbols/windream.exe.tsv` /
`re/symbols/gdidream.exe.tsv` were exported after applying the six checked rows.

Recheck the actual binaries and registry:

```powershell
uv run python tools/check_wip_names.py --demo E:\dev_game\DREAMS_ISOS\DREAMS_WIP_PCJ\DREAMS
uv run python tools/check_names.py WINDREAM.EXE --renames --twin GDIDREAM.EXE
```

All six literal-byte checks pass. The full Windows registry passes 720 names,
zero failures; 719 transfer to the GDI twin, with the expected `VID_Init`
backend byte difference excluded. Twenty-nine focused tests cover the new
Watcom parser and existing naming-fact checker. The new files pass Ruff.

## Types and further improvements

`tools/watcom_debug.py` validates the Watcom v3 trailer and extracts module
and global-symbol records directly, rather than relying on a strings scan:

```powershell
uv run python tools/watcom_debug.py E:\dev_game\DREAMS_ISOS\DREAMS_WIP_PCJ\DREAMS\DREAMSFX.EXE --out out/research/wip_pcj/glide-symbols.tsv
```

The DOS software demo contains 4,236 symbol records in 354 modules; the Glide
demo contains 4,650 in 361 modules. These include data, static symbols and
libraries, not just distinct game functions.

The software demo has local/type demand entries only for `svgautil.c`.
The Glide demo also has them for **13 engine modules**: `3DC_FX`, `3DC_COL2`,
`3DC_COLL`, `3DC_MEM`, `3DC_ANIM`, `3DC_MATH`, `3DC_TEXT`, `3DC_MAP`, `3DC_SL`,
`3DC_LIGH`, `3DC_HIER`, `3DC_LIST`, and `3DC_Z`. This is evidence of retained
type/local tables; their contents have **not** yet been decoded or imported.
It does not establish that all source types or function prototypes survived.

Useful next stages are to decode those type records, check layout differences
against retail field accesses, validate the broader name candidates' data and
call targets, and use named demo entry points to investigate missing retail
function boundaries. No retail boundary changes are justified by this pass
alone. The normalized match tables are research candidates, not bulk rename
inputs.

## Local comparison artifacts

The full initial build/content inventory is `out/research/wip_pcj/report.md`.
The comparison scripts and generated game-derived data stay under that same
ignored directory:

- `compare_code.py`: LE/PE fixup-aware whole-body comparison with DOS symbols.
- `compare_windows.py`: retail whole-body search in the demo Windows code.
- `comparison_rows.json` and `*.code.csv`: original-name transfer candidates.
- `windows-beta-retail.csv`: the 1,151 Windows-to-Windows body matches.
- `DREAMS.EXE.symbols.csv` / `DREAMSFX.EXE.symbols.csv`: initial symbol exports.

The broad scripts use Capstone (`uv run --with capstone python ...`). Only the
small literal-byte evidence set is wired into the persistent naming workflow.
