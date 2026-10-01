# Further original-name transfers to retail Windows

Date: 2026-09-29. Applied to both WINDREAM.EXE and GDIDREAM.EXE.

Follow-up: [linker-layout and compiler-aware matching](wip-layout-compiler.md) adds
88 more names and resolves the earlier boundary/address holds listed below.

**148 previously unnamed functions now have their original Watcom names.**
Another **79 already named functions retain their primary names and gain the
original spelling, owning source module and checked provenance in their
registry notes and Ghidra plate comments.** These are name annotations, not
secondary Ghidra labels. Six previously transferred original names remain.

The new primary names cover 85,983 bytes and 23,941 instructions. The richer
source boundaries from the [demo project](wip-editor-discovery.md) found more
than the 106 unnamed candidates left by the initial comparison. This pass
does not import demo structures/prototypes or change any port/review status.

## Useful names now available

| Retail Windows address | Original name now applied | Source module |
|---|---|---|
| 00457898 | Get_Father_ | 3DC_MEM.C |
| 00457bf8 | Get_World_Matrix_ | 3DC_MEM.C |
| 00457c90 | Get_World_Matrix2_ | 3DC_MEM.C |
| 00457cd0 | Get_World_Pos2_ | 3DC_MEM.C |
| 0045a36c | Anim_Rot4_ | 3DC_ANIM.C |
| 0045a928 | Anim_Rot6_ | 3DC_ANIM.C |
| 004619bc | ClosestEdgeEdge_ | 3DC_COLL.C |
| 00462d78 | EdgeFace_ | 3DC_COLL.C |
| 00468ce8 | BT_Linear_G_ | 3DC_SL.C |
| 0046aafc | BT_Perspective_G_ | 3DC_SL.C |
| 00477de8 | Set_Light_Intensity_ | 3DC_LIGH.C |
| 0047e634 | Update_Obj_ | 3DC_HIER.C |
| 0047e824 | Update_Miror_ | 3DC_HIER.C |

Original spelling and decoration are preserved, including `Miror` and trailing
underscores. Existing names such as PHYS_SweepAxis, ANIM_EvalTrackSpline and
REND_TransformLights now document their original counterparts Update_CSphere_,
Anim_Rot3_ and Compute_Face_Illum_ respectively. These confirmations do not
automatically make an existing behavioral description wrong.

| Source module | Newly named functions |
|---|---:|
| 3DC_MEM.C | 30 |
| 3DC_COLL.C | 29 |
| 3DC_SL.C | 20 |
| 3DC_MATH.C | 13 |
| 3DC_COL2.C | 12 |
| spritea.asm | 10 |
| 3DC_PROF.C | 7 |
| 3DC_LIGH.C | 6 |
| 3DC_Z.C | 6 |
| 3DC_TEXT.C / 3DC_MAP.C | 4 each |
| 3DC_ANIM.C | 3 |
| 3DC_HIER.C / 3DC_DIV.C | 2 each |

## Evidence beyond similar-looking instructions

`re/tools/check_wip_windows.py` applies the relocated-object-code criterion used
by `match_identical.py` to the symbol-bearing DOS demo and the two retail PE
images. It uses the current Ghidra function bodies instead of estimating source
lengths from the next symbol and stripping only some alignment instructions.

Each accepted name has:

1. A unique original code symbol in the demo's checked Watcom table.
2. A complete contiguous Ghidra body on each side. `ExportBodyHashes.java`
   independently exports loaded-memory SHA-256 values and exact ranges; the
   Python PE/LE reader must reproduce those bytes. Executable hashes are pinned.
3. Equal instruction bytes except actual, paired relocation operands and
   decoded direct control-flow operands. Scalar constants, register choices,
   structure offsets, relocation operand roles and widths remain significant.
4. Identical internal branch destinations relative to the function start,
   landing on instruction boundaries. Every relocation within the body must
   be accounted for by a decoded address operand.
5. Mutual uniqueness across all eligible source and target function bodies.
6. A coherent external address map in both directions across the entire
   candidate set, including consistency with matched function entry addresses.
   External direct calls and jumps must land at Ghidra function entries.
7. No second original code symbol inside the claimed source body.
8. Identical retail Windows twin bodies and matching twin Ghidra ranges/hashes.

There are 331 unique body pairs in the context set, including library and
anonymous functions. **233 game/engine pairs pass all criteria:** 148 new
primary names, 79 original-name annotations, and six already named bodies.

This establishes function identity from shared compiled code. It does not
prove identical values in the referenced globals, identical callee behavior,
unchanged structures, or equivalent gameplay. No behavioral port was marked
complete on the strength of this evidence.

## Cases deliberately held

| Retail entry | Candidate original name | Unresolved evidence |
|---|---|---|
| 00455ed8 | Update_class_pointer_ | External jump reaches a non-entry shared tail |
| 004574b4 | Env_Mapping_Obj_ | External jump reaches a non-entry shared tail |
| 004574d0 | Miror_Obj_ | External jump reaches a non-entry shared tail |
| 00456244 | Dec_Map_counter_ | Current source body also contains original symbol Link_Header_ |
| 00478800 | Build_Generic_ | Source address 003c7938 maps to two retail globals |
| 00499634 | cpu_reset_all_ | Same conflicting source/global mapping |

The earlier FaceInside_ candidate also fails the full-body boundary criterion
and is absent from the new unique-body set. These cases require targeted
boundary/data analysis rather than weakening the acceptance checks. Existing
primary names and annotations for held entries were left unchanged.

## Recheck and restore

Persistent evidence is `re/reviews/wip-windows-transfers.json`. It pins source
and Windows binary hashes, source and target body hashes, names, modules,
addresses, sizes, instruction counts and the applied action. The algorithm
reconstructs and rechecks the full candidate/address context on each run.

First export `ExportBodyHashes.java` read-only from retail WINDREAM.EXE and
GDIDREAM.EXE in `ghidra/dreams`, and WIP_DREAMS.EXE in
`ghidra/wip/dreams-july-1997`. It writes `out/ghidra/bodies/<program>.tsv`.
The separate demo and retail function feature exports are also required.

```powershell
uv run --with capstone python re/tools/check_wip_windows.py --demo E:\dev_game\DREAMS_ISOS\DREAMS_WIP_PCJ\DREAMS --check re/reviews/wip-windows-transfers.json
uv run python re/tools/check_names.py WINDREAM.EXE --renames --twin GDIDREAM.EXE
```

The standard checked rename files restore both the primary names and notes.
This application filtered them to the 227 changed entries before running
Rename.java, then exported symbols, feature dumps and loaded-body hashes from
both saved programs. Recovered name annotations preserve other plate-comment
paragraphs, including the port map and owner-review state.

Validation: **868 Windows registry names pass**, zero failures; **867** pass
the GDI twin comparison, with only the existing VID_Init backend byte excluded.
The 43 focused tests include changed scalar constants, one-sided relocations,
internal branch changes, jumps into instruction interiors, address-map
conflicts, wrong callee entries and disagreement with Ghidra-loaded memory.
The port-map checker also passes. No game executable bytes were changed.
