# Using linker layout and Watcom behavior to recover more Windows names

Date: 2026-09-29. Applied to the European WINDREAM.EXE and GDIDREAM.EXE.

This pass adds **88 original primary names and four original-name annotations**.
The Windows registry now contains **956 checked names**. In the current Ghidra
non-thunk feature exports, **1,211 of 1,944 functions are meaningfully named
(62.3%)**, covering **473,059 / 617,400 function-body bytes (76.6%)**. These
counts include libraries and describe discovered functions, not port completeness.

| Evidence path | Newly named Windows functions |
|---|---:|
| Exact bodies plus module/order, operand and caller constraints | 65 |
| Same C model reproduced with two Watcom compilation profiles | 12 |
| Candidate-hidden retail review, source comparison, and Windows code bridge | 11 |

All names and annotations were saved to both Windows Ghidra programs and
exported to `re/symbols`. No game executable, retail prototype, C++ behavior,
port coverage or owner-review flag was changed.

## The demo contains actual linker contribution records

The Watcom debug address-info section contains runs of `(size, module index)`
inside each LE object. These supply exact source object-code/data ranges,
including entries with no public function symbol. `read_contributions()` in
`tools/watcom_debug.py` now decodes them.

The software demo has **665 contributions: 332 executable and 333 data**.
The Glide demo has 701 contributions, 340 executable. Every software-demo
code symbol agrees with its recorded module owner. These are linker records,
not guessed intervals between function names.

All 233 previously verified engine name correspondences preserve function
order within their original modules: zero inversions across 14 modules.
The new matcher uses verified neighbors in the same contribution to constrain
short or repetitive bodies, alongside already mapped globals and callers.
It requires actual code equality after explained relocations; position alone
never assigns a name. Modules with contradictory anchor order do not provide
ordering evidence.

`re/reviews/wip-module-layout.tsv` records source contribution extents and the
retail matched-anchor spans. The latter are explicitly **not** claimed to be
the complete retail module boundaries.

## Concrete compiler effects that blocked the earlier pass

### Preincremented array addressing

In `cpu_reset_all_`, Watcom emits an index starting at zero, adds 16 before
the first access, and uses displacements one record before the actual array.
The source displacement `003c7938` happens to be `_vertices_processed` in
the preceding 3DC_HIER.C contribution. But the effective first address is
`003c7948`, a member of `_tbank` starting at `003c7944` in 3DC_PROF.C.

Treating the bare displacement as a global identity incorrectly linked this
profiler access to the renderer counter. `tools/watcom_patterns.py` recognizes
only the proven leaf-loop idiom: zero initialization, one fixed increment,
no intervening calls/branches or additional index writes, and a matching
counted back edge. Address comparison then uses the first effective element.
This resolved both `cpu_reset_all_` and the renderer's `Build_Generic_`.

### Shared returns and shared code

Watcom places `Link_Header_`, an empty routine, on a RET byte also used by
`Dec_Map_counter_`. That original symbol inside the enclosing body does not
mean unrelated code has been swallowed. The exact RET, instruction boundary
and common module ownership are checked before accepting the enclosing name.

Other functions jump into corresponding instructions of an already verified
body. Those shared tails are checked through the mapped owner, rather than
requiring Ghidra to invent a separate function at every destination. A direct
call into an unproved interior entry remains rejected.

Together these checks resolved the earlier Update_class_pointer_,
Env_Mapping_Obj_, Miror_Obj_, Dec_Map_counter_, Build_Generic_ and
cpu_reset_all_ holds. Existing descriptive names on three of those functions
were retained with their original spellings added to the notes.

### A wrong Ghidra instruction boundary

The demo's FaceInside_ stopped after 63 bytes because Ghidra decoded a NOP
at `00054638`, one byte inside the real MOV beginning at `00054637`.
The correct MOV/LEA pair occupies 13 bytes. All 34 checked 3DC_COLL.C anchors
share one address delta, and the entire 612-byte source slice matches retail.

The reviewed repair in `re/boundaries/WIP_DREAMS.EXE.tsv` realigns those
instructions and rebuilds the body: **63 → 612 bytes**. The original
FaceInside_ name now transfers to retail `00463c08`. The new `code` boundary
action refuses to erase a function entry in the repaired range.

## Compiler reproduction bridges optimized and debug code

The same abstract C memory-copy operation was compiled with Watcom 11.0 under
both profiles:

```c
typedef struct { unsigned char bytes[0x2200]; } Block;
extern Block current;
void f(Block *p) { memcpy(&current, p, sizeof(current)); }
```

- `-5r -otexan -s` reproduces the named DOS demo helper's **35 bytes**.
- `-5r -d2` reproduces the Windows helper's **54 bytes**: stack checking,
  debug-style parameter handling and a call to memcpy rather than the optimized
  inline copy.

Both are complete compiled-body matches, not scores. OMF fixups identify
which operands are extern globals or calls; the checker verifies the actual
`__CHK`/`memcpy_` destinations and consistent source-to-target global roles.
The same method covers 13 project/object/link/adventure/box helpers: twelve
new names and an original-name note for DDAT_CopyRecord. Original return types
are not uniquely established merely by reproducing these bodies, so prototypes
were left alone. Reproduction with 11.0 also does not, by itself, establish
which compiler version built the demo.

`tools/check_wip_compiler.py` regenerates the models and verifies
`re/reviews/wip-compiler-transfers.json`. C/OBJ outputs remain under
`out/recomp/wip-compiler-check`.

## Recompiled gameplay functions reviewed without candidate-name priming

A proposal had to agree along two paths: DOS demo → retail Windows, and DOS
demo → Windows demo → unchanged retail Windows body. Candidate scores were
used to build the queue, not to authorize a rename.

For eleven functions, the assistant reviewed comment-stripped retail C before
revealing the proposed original names. Existing retail callees and literal
strings remained visible. The observations were saved first, then the original
demo counterparts were examined. This was the same assistant's candidate-hidden
review, not a third-party review or owner sign-off.

Confirmed names include `clonePLayer_`, `WEA_ChockObjet2_`,
`SYM_InitSymboleInObjet_`, `SYM_PrintTextReceive_`, `CopyRandParticle_`,
`PAR_InitParticlesCreate_`, `PutScreenInfo_` and `Remap256to16bits_`.
The evidence and pre-reveal observations are in
`re/reviews/wip-layout-blind-review.json`; `tools/check_wip_review.py` rechecks
the original symbols, executable hashes and Windows-demo/retail code bridge.

This also demonstrates why structure transfer needs separate work. The DOS
clone routine advances actors by `0x2c4`, while both the Windows demo and retail
use `0x2d0`. The knockback routine's source mass-related double is at `+0x24c`,
versus Windows `+0x250`. Those are observed build/platform layout differences;
compiler packing is a possible explanation, not established here. Function
identity can survive such differences without making the layouts interchangeable.

## Other recovered names and remaining limits

The exact-code pass now includes Cryo's bare `3D_*.asm` module names, which the
earlier path-based game/engine filter had treated as library context. It names
large rasterizers including `_Display_EX_Alpha`, `_Display_EX_T50`,
`_Display_EX_mip`, `_Display_EX_16bit` and their line/background helpers.
It also recovers `decompresser_adpcm_mono_`, small memory/animation helpers and
additional library routines.

The layout checker still holds HNM_JPEG_8X8_WARP and __full_io_exit_ because
their interior call destinations are not covered by an accepted owner-body
proof. Broader module/call-graph proposals include recompiled or changed game
functions, but are not names to bulk-apply. Windows-specific/new retail code
may have no original name in this DOS demo at all.

The approach closes demonstrable gaps and supplies a module-based work queue;
it does not establish that every remaining anonymous function is recoverable
from the demo. The current remaining count is **733 discovered functions**.

## Validation

```powershell
uv run --with capstone python tools/check_wip_layout.py --demo <demo> --check re/reviews/wip-layout-transfers.json
uv run python tools/check_wip_compiler.py --demo <demo> --check re/reviews/wip-compiler-transfers.json
uv run --with capstone python tools/check_wip_review.py --demo <demo> --check re/reviews/wip-layout-blind-review.json
uv run python tools/check_names.py WINDREAM.EXE --renames --twin GDIDREAM.EXE
uv run --with capstone pytest tests/test_wip_layout.py tests/test_wip_windows.py tests/test_watcom_debug.py tests/test_watcom_types.py tests/test_check_names.py
```

The Windows registry passes 956 names; 955 have identical GDI-twin bodies,
with the existing VID_Init selector difference excluded. The focused suite has
52 passing tests. The port-map checker passes and owner-review state is unchanged.
