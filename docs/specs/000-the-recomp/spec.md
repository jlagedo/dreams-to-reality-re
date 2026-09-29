# 000 — The recomp: using the Watcom toolchain and the recompiled game as research instruments

Status: **W1–W3 done 2026-09-28** (results under each item); the recomp's
collision bug found and fixed the same day (lifter defects section); W4–W7
not started
Date: 2026-09-28
Depends on: [toolchain.md](../../toolchain.md) (Watcom 11.0 verdict),
[re-setup.md](../../re-setup.md) (Ghidra passes). Numbered 000 because it
serves the reverse engineering that every OpenDreams spec (001 onward) ports
from, rather than an OpenDreams deliverable.

## Purpose and boundary

We run three experiments beside Ghidra. The recompilation and difftest
sources are in `recomp/`; their outputs and the matching decompilation are
under the gitignored `out/recomp/`:

| Instrument | Directory | What it is |
|---|---|---|
| **Static recompilation** | `recomp/windream/` | `GDIDREAM.EXE` lifted instruction by instruction to C with pcrecomp's `lift32`, built with clang-cl against hand-written Win32 shims, and run. |
| **Differential tests** | `recomp/difftest/` | Small Watcom programs run natively and recompiled; the outputs are diffed to find lifter bugs. |
| **Matching decompilation** | `out/recomp/matchdecomp/` | C written for one retail function, compiled with a real Watcom compiler, and compared byte for byte with `WINDREAM.EXE`. |

This spec records what those instruments are and what they have shown so far,
and plans how to turn them into better **decompilation**: function boundaries,
prototypes, names and verified C, all fed back into Ghidra and the name
registry. A shipping recompiled game is not a goal. The recompiled build is a
measuring device, the same way the Ghidra project is.

Rules, from `AGENTS.md`, that apply throughout:

- Lifted and matched C is game-derived. It stays under `out/` and is never
  committed. What gets committed is tooling that reads the game at run time,
  registry facts, Ghidra scripts and documentation.
- A recompiled run, a trace or a byte match is **evidence**, not owner review.
  It never sets `reviewed` in `opendreams/port-map.tsv`.
- Names still need two independent sources and go through
  `re/names/<program>.tsv` and `tools/check_names.py`. Nothing here renames
  functions in Ghidra directly.

## Why the toolchain matters

`WINDREAM.EXE` and `GDIDREAM.EXE` were compiled, linked and given their runtime
by **Watcom 11.0**, not 10.6 ([toolchain.md](../../toolchain.md)). **[verified]**
Most game code is unoptimized, stack-checked and built with debug info
(`-5r -d2`: every function starts `push N; call __CHK`); the optimized
modules match `-5r -otexan -s`. With the right compiler and flags, ordinary C
comes out byte-identical to retail. That was true for the 7-function `MATH_`
pilot, for 20 of the 24 blind-sampled functions and for 107 of 126 port-map
functions (W3). **[verified]**

That makes a stronger claim possible than "the decompilation looks right": a
C function that compiles to the retail bytes has, by construction, the same
argument count and registers, the same widths and signedness (`movsx` versus
`movzx`), the same struct offsets and the same control flow. The experiments
were built before 11.0 was pinned down; W1 moved them to it.

## Current state

### Static recompilation (`windream/`) **[verified 2026-09-28]**

- **Pipeline:** `bounds.csv` (1,776 function ranges from Ghidra through
  pcrecomp's `DumpBounds.java`) → `lift.py`.
  - `lift.py` subclasses `lift32` with fixes for `bsr`/`bsf`, `aam`/`aad`,
    `fsincos`, x87 compare flags, mul/div flags, `sahf`, string compares and
    x87 pops.
  - It then keeps adding every call and jump target it finds to the function
    list until no new ones appear.
  - Output: 2,181 functions, 674,747 lines of C in `gen/`, and
    `lift-report.json`.
- **Runtime:** `runtime/` has about 3,700 lines of hand-written replacements
  for kernel, threads, files (write sandbox), user, GDI (emulated DIB and
  `StretchBlt`), WinMM (timer, joystick, emulated CD audio) and DirectSound.
  `gen_imports.py` generates the 109 import bridges.
  - Build: `build.py` (clang-cl, Ninja; one unoptimized build).
  - Run: `run.py` (scripted keys, BMP snapshots). Presents are capped at
    25 fps (`--fps`, `WD_FPS`; 0 = uncapped) and a crash writes a full-memory
    minidump (`--dump`, `WD_DUMP=mini|0`).
- **Result:** the recompiled game plays `INTRO.HNM` and `GENERIC.HNM`, takes
  scripted Esc/Return and reaches the first level in game. Since the
  collision fix below, Duncan lands on the ground at level start and a
  70-second scripted run finishes without a fault.
- **Unimplemented sites:** 61 instruction sites are unimplemented: `int`,
  `bound`, far `jmp`/`call`, `aas`, `ins`, `into`, `salc`, `cli`/`sti`,
  `pushf`/`popf`. **[inferred]** almost all are data decoded as code.
- **No 10.6 assumptions in the lifter.** The recomp works at instruction
  level, so it works whichever compiler built the game.

### Differential tests (`difftest/`) **[verified]**

- **Harness:** until W1, `wat.ps1` built every test with **Watcom 10.6** and
  `-5r -fp5 -ox`, flags the game does not use. It now defaults to 11.0 and
  `-5r -od` (see W1 for the 11.0 results).
- **Results with 10.6:**

  | Test | Result |
  |---|---|
  | `t_core` | 22 ok / 12 diff: x87 math (`sin`, `cos`, `atan`, `pow`, `exp`, `log`, `log10`, `tan`, `acos`), `setjmp`, one misc case |
  | `t_switch` | 5 / 0 |
  | `t_insn` (every instruction form the game uses, run from inline assembly) | 559 ok / 271 diff |

  All 271 `t_insn` diffs are in flags only (mostly AF/PF, some OF); no values
  differ.
- **Coverage:** `cov.txt` says the game uses 669 instruction forms; `t_core`
  covers 92.7% of instruction occurrences.

### Matching decompilation (`matchdecomp/`) **[verified]**

- **`match.py <c> <func_> <va> --cc <compiler> --flags "<flags>"`:**
  compiles with `WCC386`, takes the named public's bytes from the OMF object,
  and compares them with the retail extent from `windream/bounds.csv`,
  treating relocated bytes as wildcards. It prints `MATCH`, or a masked
  disassembly diff.
- **Results:** the pilot's 7 functions are byte-exact under 11.0. In the
  blind test, 18 of 24 are exact under 11.0 and 12 of those from the first
  draft. `0x40464f` and `VID_Lock` (`0x445bf2`) match under neither compiler
  (retail `je +2; jmp`).
- **Defaults:** since W1, `match.py` and `flagsweep.py` default to 11.0
  (`DREAMS_WATCOM_COMPILER`) and `-5r -od`, and read `WINDREAM.EXE` from
  `DREAMS_DISC1`.

### What pcrecomp offers for decompilation **[verified by survey]**

pcrecomp is compiler-agnostic by design. It lifts every function as
`void sub_X(void)` over global registers and left out its ABI and
parameter-count analyser (`docs/CONSOLIDATION.md:836-863`). It has no
signature matching, no `__watcall` recovery and no matching-decompilation
support. The parts that serve decompilation:

| Tool | Use for us |
|---|---|
| `tools/lift/recover.py` | Finds functions reached only by tail jumps or stored pointers, and alternate entries into another function's body. Its tests cover Watcom's shared epilogues reached by `je`/`jmp`. |
| `tools/disasm/score_recovery.py` | Scores one function list against another and sorts the errors: split or invented starts, short or long ends, embedded jump tables. |
| `tools/disasm/seed_from_log.py` | Turns the running build's `ICALL`/`ITAIL unresolved VA` lines into checked function entries, each with the call site that reached it. |
| `runtime/recomp32/recomp_trace.c` (already in `windream/runtime/`) | Debug switches: `--calltrace` logs every function entry, `--firsthit LO HI` the first entry to each function in a range, `--watch VA` registers. `--argtrace` reads stack arguments only, which is wrong for `__watcall`. |
| pod-recomp `POD_ARGS_AT` (`src/runtime/recomp_runtime.c:149-160`) | On entry to a chosen function, prints EAX/EDX/EBX/ECX and then the stack, showing pointers to printable text as strings. Watcom-aware, since POD is also a Watcom register-convention build. |
| nocturne and pod-recomp shared-tail notes (`nocturne/docs/PHASE7.md`, `pod-recomp/docs/PHASE3.md`) | IDA split Watcom shared tails into separate functions that end without a `ret`: 272 in Nocturne, 565 in POD. |

Neither reference project turned lifted code into readable C. Their names came
from assert strings (Nocturne) and IDA FLIRT (POD).

### Ghidra decompilation after W1–W3 **[verified 2026-09-28]**

`ghidra_scripts/DecompileAll.java` regenerated the whole decompilation of both
Windows programs into `out/decomp/` (`<program>.c`, `<program>.tsv`). The
same script ran on `out/ghidra-backup-20260928-pre-watcall110/` (the project
before the 11.0 move) for comparison (`out/decomp/compare.py`). WINDREAM.EXE
figures follow; GDIDREAM.EXE has the same bounds and near-identical numbers.

| WINDREAM.EXE | Before 11.0 | Now |
|---|---:|---:|
| Functions decompiled (externals excluded), failures | 1,798, 0 | 2,065, 0 |
| `__watcall` / unknown convention | 1,269 / 504 | 1,903 / 25 |
| `USER_DEFINED` signatures | 0 | 130 |
| Same 1,798 functions: `unaff_` variables (functions) | 433 (213) | 387 (194) |
| Same 1,798 functions: `extraout_` (functions) | 2,367 (657) | 2,370 (628) |
| Same 1,798 functions: `in_` (functions) | 238 (153) | 214 (132) |
| The 130 proven prototypes: `extraout_` (functions) | 86 (63) | 43 (23) |

- **The 267 new functions** from W2 (dead functions and missed entries)
  decompile without failures.
  - 109 are import thunks, which Ghidra reports as "Treating indirect jump as
    call". That accounts for the rise in that warning, from 57 to 164.
- **`extraout_` barely moved outside the proven set.** This is the W7 kill-set
  problem: every unproven `__watcall` still kills `EDX`, `EBX` and `ECX`.
- **Remaining warnings** worth a later look:
  - 28 "Could not find normalized switch variable" in `HNM5_DecodeFrame640`
    and `HNM6_DecodeCoefficients` (hand-written decoders).
  - 70 "Read-only address is written", at 41 addresses (`0x4029d8`–`0x402e48`)
    in five blitters (`FUN_004024b8` …). The blitters patch their own code.
    This is a retail trait, not an analysis error.
- **The recomp needs no re-lift.** The fresh `DumpBounds` export is
  byte-identical to the `windream/bounds.csv` behind the current `gen/`.

### Lifter defects found by play-testing **[verified 2026-09-28]**

**Symptom.** In the recomp Duncan fell through the map at level start
(retail lands him on the ground); after flying back up, collision worked.
Play then crashed within a minute in `PHYS_CollideSphereTriangle`
(`0x45E745`, `mov eax, [ebx+4]` with EBX a small value). Four crashes, at
30 and 25 fps, with manual and scripted input.

**Collision data.** Each collider keeps a binary tree of 24-byte overlap
records (collider `+0x2C`: triangle, axis bits, right, left, wall node,
floor node) and two lists of 12-byte nodes (walls `+0x30` for records with
all three axis bits, floors `+0x34` for bits 0 and 2).
`PHYS_SweepAxis` (`0x45D420`) moves the collider's interval through the
sorted endpoint arrays and calls `PHYS_AddCandidate` (`0x45D10C`) and
`PHYS_RemoveCandidate` (`0x45D25C`). The collision world is the global at
`0x66E01C` (triangle count, three endpoint arrays, the collider array at
`+0x10` and its count at `+0x40C`), the same address in both Windows
builds.

**How it was traced.**

1. **Full-memory dumps of the recomp.** The crashing "triangle" was a live
   overlap record or a freed one; the camera collider's wall list held 274
   nodes for 143 qualifying records, 131 of them orphans.
2. **Read-only hooks** (`lift.py` `HOOKS`, `runtime/phys_hook.c`): 27% of
   `PHYS_AddCandidate` calls set an axis bit that was already set, from all
   three add sites, on every moving collider. Retail never checks the bit,
   so each such call with all bits set leaks a node, which dangles once its
   record is freed.
3. **Retail as the reference.** Two full dumps of the running retail
   `GDIDREAM.EXE` (launched through `tools/fps_limit_launcher.py`, which
   changes only the call at `0x4170B7`; the rest of the code section is
   byte-identical to the EXE) after 40 seconds and after 8 minutes of play:
   on all 8 colliders, one node per qualifying record and no orphans. A
   triangle has an axis bit exactly when it strictly overlaps the
   collider's `[c−r, c+r]` on that axis (a handful of exact ties aside).
   So the fault was in the recomp.
4. **The same invariant inside the recomp** (`WD_PHYS_INVARIANT=1`, checked
   after every sweep) failed on the very first sweep after a level reset:
   230 adds, then 218 records missing.
5. **Replay on real x86 semantics.** The Unicorn emulator ran the original
   `PHYS_SweepAxis`, `PHYS_AddCandidate`, `PHYS_RemoveCandidate` and
   `malloc_` bytes on the state from the recomp's dump
   (`WD_PHYS_CAPTURE=1` forces the dump): all 230 records kept.
6. **The recomp's event log** showed the 230 adds followed by 648 removes of
   triangles never added. A correct remove finds nothing and returns.

**Cause: static flag state carried across labels.** pcrecomp's
`generate.py` tracks which instruction last set the flags and folds
conditions from it, but did not reset that state at block starts. At
`0x45D398`, reached by `cmp eax, ebp; jbe` from the tree search, the
instruction before it in address order is `test ecx, ecx; …; jmp`. After a
`test`, CF is 0, so the `jae` there was lifted as `if (1)`.
`PHYS_RemoveCandidate` then took every larger key in its search as a match
and deleted the wrong overlap records. The first sweep after a reset removes
hundreds of absent triangles, so colliders lost their floor triangles at
level start. The leaked, later dangling nodes caused the crashes.

**Fix** (`lift.py`, `WinDreamLifter`): the static flag state is dropped at
every block leader, so conditions there are decided at run time from the
lifted flag record (`recomp_cond_cf`). This changed 227 lifted lines across
the program. Afterwards a 70-second scripted run had no invariant
violation, no re-add and no crash, and Duncan lands on the ground (seen in
snapshots and in play). The upstream `generate.py` still has the defect.

**Also fixed: self-modifying blitters.** Five span blitters (`0x4024B8`,
`0x40254D`, `0x4027B8`, `0x40294D`, `0x4029AF`) write their texture
steps, pointer steps and loop limits into their own instructions through
a register (`lea ebx, [0x4029D8]`, then a store). The lift kept the
placeholders (`sub dl, 0x12`, `cmp ebx, 0x12345678`). `lift.py` now
reads those 41 operands (33 imm8, 8 imm32; Ghidra's "Read-only address is
written" warnings) from guest memory. pod-recomp hit the same pattern but
finds only absolute 32-bit stores. These blitters did not run in the
tested level, so this was not the collision bug.

**Checked and correct** along the way: the sweep's jump table, byte shifts
and bit clears, register preservation across `malloc_`/`free_`, the
endpoint sort, the reset after mesh add/remove, and every flag the lifter
computes wrongly (no game code reads one; `difftest/consumers.py`).

## Drift between the recomp and Ghidra **[verified 2026-09-28; resolved by W1 and W2]**

`bounds.csv` was exported at 14:52. The 11.0 Ghidra upgrade (`e368f1e`,
`4024c1b`) came after that.

- **Ghidra has since changed:**
  - It now has 1,787 functions (`out/ghidra/features/WINDREAM.EXE.json`).
  - 22 entries are new since the export and 11 are gone.
- **Where the lifter's 413 extra entries fall:**

  | Where | Count |
  |---|---:|
  | Now Ghidra function entries | 6 |
  | Inside the body of a Ghidra function | 220 |
  | Outside every Ghidra function | 187 |

  **[inferred]** The 220 inside bodies are cross-function jump targets:
  shared tails and alternate entries. The 187 outside are code Ghidra never
  made into functions (reached by fall-through or unrecognised pointers), or
  data. Both groups are boundary work for Ghidra.

## Work items

Ordered by value to the decompilation. Each is finished when its check passes
and the result is recorded in `docs/` (and in the registry where names are
involved).

### W1 — Retarget the instruments to 11.0

- **Changes:**
  - `match.py` and `flagsweep.py`: default to `--cc wc110` with `-5r -od`.
    Take the compiler from `DREAMS_WATCOM_COMPILER` in `.dreams.local.env`
    (already set to `wc110\11.0`) and the EXE from `DREAMS_DISC1`.
  - Add both variables to `dev/paths.example.env`.
  - `difftest/wat.py` (then `wat.ps1`): build with 11.0 and the game's two flag profiles.
  - Re-run `t_core` and `t_switch` against the 11.0 runtime. The math
    failures so far are in 10.6's library routines, not the ones the game
    links.
  - Re-export `bounds.csv` from the current Ghidra project and re-lift.
- **Check:**
  - The pilot and blind sets reproduce their recorded 11.0 results with no
    flags given.
  - The recomp still reaches level loading.
- **Done 2026-09-28.** **[verified]**
  - **Compiler setting:** `paths.py` resolves `watcom_compiler` and
    `watcom_compiler_106` (`dreams config` lists them), falling back to
    `DREAMS_WATCOM\wc110\11.0` and `DREAMS_WATCOM\wc106\watcom10.6`.
  - **`match.py`, `flagsweep.py`:** use these settings. `difftest/wat.ps1`
    takes `-Cc wc110|wc106` and restores `PATH` afterwards (Watcom's `RC.EXE`
    had broken the next clang-cl configure).
  - **Pilot and blind sets:** the 7 pilot functions are exact with only
    `--flags "-5r -otexan -s"`; the 18 blind 11.0 matches re-score exact with
    no compiler given (`blind/recheck.py`).
  - **Differential tests under 11.0:** `t_core` gives 21 ok / 13 diff at
    `-5r -od` and 22 / 12 at `-5r -otexan -s`; `t_switch` gives 5 / 0. The x87
    math failures (`sin` … `acos`) and `setjmp` are the same as under 10.6, so
    they are **lifter bugs, not 10.6 library differences**. W6 owns them.
  - **Recomp:** re-lifted from each new Ghidra export. The last lift has
    2,065 Ghidra ranges and 2,314 functions, and the recompiled game still
    loads the first level (`OMBRE.3DC`, `xh_.DAN`, `GR00.3DC`) and plays it
    in game with the HUD and the opening dialogue.

### W2 — Close the boundary gap

- **Changes:**
  - Diff Ghidra's function list against the lifter's 413 extra entries.
  - Run `recover.py` and `score_recovery.py` against a fresh `DumpBounds`
    export.
  - Classify each disagreement:
    - a shared tail or alternate entry: merge it, or leave it as a labelled
      chunk (`MergeFragments.java`);
    - a missed function: create it;
    - data.
  - Record the classification in `re/`, with a Ghidra script to apply it.
- **Check:**
  - No Ghidra function ends without a `ret`, `jmp` or call to a
    non-returning function unless it is recorded as a shared tail.
  - The 187 outside entries are each accounted for.
- **Done 2026-09-28.** **[verified]**
  - **Tools:** instead of pcrecomp's `recover.py` and `score_recovery.py`,
    whose inputs are a bounds CSV and raw bytes, the check runs inside
    Ghidra.
    - `ghidra_scripts/ReportBoundaries.java` reports every flow edge that
      leaves a body off an entry, code in no function, and undefined
      non-filler bytes.
    - `ghidra_scripts/ApplyBoundaries.java` applies the reviewed list in
      `re/boundaries/<program>.tsv`, followed by
      `CreateWatcomFunctions.java apply`.
    - Details and numbers are in
      [re-setup.md](../../re-setup.md#function-boundaries-reportboundariesjava-applyboundariesjava).
  - **Result:** both Windows builds go from 1,907 to 2,174 functions, and
    instructions in no function from 17,880 bytes to 1. Every one of the 51
    cross-function jumps lands on one of 40 recorded shared tails, so the
    first check passes.
  - **The lifter's extra entries:** after re-lifting, they number 252 against
    the old 413:

    | Where | Count |
    |---|---:|
    | Inside a Ghidra body (the lifter splits at intra-function jump targets; not a Ghidra defect) | 214 |
    | Recorded shared tails | 29 |
    | Outside every function | 9 |

    Of the 9, eight at `0x4823c4`–`0x482488` are artifacts of the lifter's
    linear decode: it falls out of step after 3 filler bytes at `0x4814e1`
    inside an HNM6 intra decoder, where Ghidra decodes correctly. The ninth,
    `0x49a217`, is a `pop edx; ret` fragment. The second check passes.
  - **Beyond the lifter's list:** the report found 249 **dead functions**
    (uncalled, unjumped, unpointed). They include 107 import thunks,
    `Update_Obj_` and `Update_Hierarchie_` (named in the Dutch and Spanish
    OMF records), and `Build_Obj_Miror_`.
  - **Left alone:** dead blocks inside functions, such as 1,206 bytes in
    `TEXT_LoadLanguageIni` that a prologue jumps over. They stay undefined
    and are not boundary errors.

### W3 — Matching decompilation as proof of prototypes

- **Changes:**
  - For each port-map function, compile our C (or a decompilation-derived C
    sketch) with `match.py` under 11.0.
  - A byte-exact match fixes the function's prototype: argument count,
    registers, widths and signedness, and the struct offsets it touches.
  - Feed those back to Ghidra as `USER_DEFINED` signatures through a
    committed script and a tracked prototype list. Only the prototype
    strings are tracked, not the C bodies.
- **Constraint:** a match proves what the code does, not what it is called.
  It is not a naming source under the two-source rule; `match_identical.py`
  byte identity between binaries remains one.
- **Check:** each committed prototype has a recorded exact match (address,
  flags, compiler). Decompiling those functions in Ghidra shows no
  `unaff_`/`extraout_` registers.
- **Done 2026-09-28.** **[verified]**
  - **The sweep:** every port-map function of at most 500 bytes, runtime
    library excluded (126), was written as C from its Ghidra decompilation
    by six parallel agents, at most 10 compiles each. They worked in
    `out/recomp/matchdecomp/proto/fn_<va>/` (`decomp.c`, `retail.asm`,
    `work/`, `match.c`, `result.txt`).
    - `proto/harvest.py` accepts nothing on the agents' word. It recompiles
      every `match.c`, requires an exact match, checks the prototype against
      the definition, and rejects matches written as inline assembly.
  - **Outcome:**

    | Outcome | Functions |
    |---|---:|
    | Byte-exact, unoptimized | 92 |
    | Byte-exact, optimized | 15 |
    | Hand-written assembly: `pushad`, register results, `stc`/`adc` bit refills | 9 |
    | Optimized, register allocation only | 9 |
    | Unoptimized, two stack slots swapped | 1 |

    Two of the assembly routines match only as a naked `_asm` body, which
    proves nothing about the prototype.
  - **Flags:** all 92 unoptimized matches are exact under `-5r -d2`, and 56
    only under it. `-d2` keeps the `je +2; jmp` branch and the
    `mov eax,[i]; inc [i]` increment that `-od` folds, so it is now
    `match.py`'s default ([toolchain.md](../../toolchain.md) step 6). It
    also turned the blind test's two misses exact.
  - **10.6 cross-check:** the same 107 sources under Watcom 10.6 give 15
    exact matches.
  - **Tracked result:**
    - `re/prototypes/<program>.tsv` holds 131 proven prototypes: the 107,
      the pilot, and the blind matches.
    - `ghidra_scripts/ApplyPrototypes.java` applied them to both Windows
      programs as `USER_DEFINED` `__watcall` (`__cdecl` for the variadic
      `TEXT_Print`), with a `[PROTO]` plate paragraph. 130 were applied; the
      float-returning `ANIM_ApplyEase` is skipped.
    - The C bodies stay under `out/`.
  - **Check result:**
    - The first part passes.
    - No `unaff_` register remains in the 131 decompilations, so every
      parameter is recovered.
    - 98 `extraout_` reads remain in 23 of them. These are registers read
      after a **call**. Retail prologues show that Watcom preserves every
      register except `EAX` and the ones carrying parameters:
      `CTRL_Shutdown` (0 parameters) saves `EBX ECX EDX`, `DAN_Load3DM` (2)
      saves `EBX ECX`, and `TEXT_LoadFont` (3) saves `ECX`.
    - `tools/watcall-cspec.patch` has one `__watcall` model that marks all
      four argument registers as killed, so a caller that keeps a value in
      `EDX` across a one-parameter call reads `extraout_EDX`. See W7.

### W4 — The running recomp as a naming and typing oracle

- **Changes:**
  - Build with `-DRECOMP_TRACE` and record what runs:
    - `--firsthit` over the game's code range during fixed scripted runs
      (intro, menu, New Game, first level);
    - the ordered first-entry list, kept under `out/`.
  - Port `POD_ARGS_AT` into `windream/runtime/recomp_trace.c`, so a named
    function prints its `__watcall` register arguments (strings shown as
    text).
  - Run `seed_from_log.py` on unresolved `ICALL`/`ITAIL` lines to find
    callback and function-pointer targets.
- **What it gives:**
  - The "first runs when X happens" and argument-value facts support a blind
    review as the second source for a name.
  - Register values check prototypes that W3 cannot match (the functions
    listed under W3's check that are optimized or hand-written).
- **Check:** at least one registry name gains a trace-derived fact that
  `check_names.py` or a recorded trace can re-check.

### W5 — Hybrid runs to test ports

- **Changes:** swap one lifted function for its OpenDreams C++ port (or a
  hand-written C version), keeping the recomp's register interface, and
  compare snapshots and traces with the all-lifted build. This is the
  selective lifted-versus-real swap from pcrecomp's `docs/HYBRID.md`.
- **What it gives:** a whole-game check of a port's behaviour beside the
  tests in `opendreams/`. It is evidence for the `coverage` column only,
  never for `reviewed`.
- **Check:** one ported function, for example a `MATH_` helper already
  byte-matched, runs swapped with identical snapshots.

### W6 — Lifter correctness for the game's own instructions

- **Changes:** fix or explain the 271 flag-only `t_insn` diffs. Most are AF
  and PF, which the game's code probably never reads.
- **Check:** a per-instruction-form list records whether the game reads the
  flag. Every form whose flags the game reads is exact.
- **Progress 2026-09-28:** `difftest/consumers.py` finds no game code that
  reads a flag `t_insn` shows wrong. Beyond single instructions, the
  collision bug exposed a lift-level flag defect (state carried across block
  starts), now fixed; see the lifter defects section. The approach that
  found it: an invariant checked against retail memory dumps, then a replay
  of one call in Unicorn.

### W7 — Per-arity `__watcall` models

- **Why:** found in W3. Watcom callees preserve every register that does not
  carry a parameter. Ghidra's single `__watcall` model kills `EDX`, `EBX` and
  `ECX` on every call, which produces the remaining `extraout_` reads.
- **Changes:**
  - Add `__watcall0` … `__watcall4` models to `tools/watcall-cspec.patch`,
    each killing only `EAX` and its own argument registers.
  - Have `ApplyPrototypes.java`, and `ApplyWatcall.java` where the parameter
    count is proven, choose the model by register-parameter count.
  - This changes the patch every Ghidra install applies.
- **Check:** the 23 W3 functions with `extraout_` reads decompile without
  them wherever the callee's prototype is proven.

## Not applicable

- **Source-file and assert mining** (`pcrecomp tools/pe/debug_symbols.py`,
  nocturne `mine_symbols.py`): the English `WINDREAM.EXE` contains no `.c`
  or `.h` path strings and no assert strings. Checked 2026-09-28.
  **[verified]**
  - The recovered error-string names are already in the registry.
  - The Dutch and Spanish builds carry a fragment of OMF object records
    ([localized-build-symbols.md](../../localized-build-symbols.md)).
- **pcrecomp's RTTI, vtable and mangler tools:** the game is C, not C++.
- **`lift32`'s call model and pcrecomp's Ghidra export scripts:** they carry
  no calling-convention or type information.
- **MSVC-oriented FLIRT and MAP donors:** our own OMF library matcher
  (`src/dreams/watcom.py`) already fills that role for Watcom 11.0.

## Open questions

1. ~~`0x40464f` and `VID_Lock` match under neither compiler (`je +2;
   jmp`).~~ Resolved in W3: both are exact under 11.0 `-5r -d2`. Plain `-od`
   folds the conditional jump over a `jmp`; `-d2` keeps it.
2. Is `GDIDREAM.EXE` a good enough stand-in for `WINDREAM.EXE` in the recomp?
   The two differ by one byte (`0x446036`). DirectDraw would need a new shim
   layer.
3. Should the scripted-run traces (W4) become a committed tool with a fixed
   script, like `tools/check_names.py`, so they can be re-checked on another
   machine with the discs?
4. The flag-state defect is in upstream pcrecomp (`tools/lift/generate.py`).
   Report it there? Not done: it is outward-facing.

## File map

| Path | Content |
|---|---|
| `out/recomp/pcrecomp/` | Upstream toolbox clone (`tools/lift`, `tools/disasm`, `tools/ghidra/DumpBounds.java`, `runtime/recomp32`) |
| `out/recomp/nocturne/`, `out/recomp/pod-recomp/` | Reference Watcom recomp projects |
| `recomp/windream/` | `lift.py` (`HOOKS`, `PROBES`, `PATCH_SITES`), `bounds.csv`, `runtime/` (`phys_hook.c`: collision hooks, `WD_PHYS_INVARIANT`, `WD_PHYS_CAPTURE`), `build.py`, `run.py`, `recomp_env.py` (one level up) |
| `out/recomp/windream/` | Outputs: `gen/` (lifted C), `build-*/`, `run/` (logs, sandbox, crash dumps) |
| `recomp/windream/debug/` | Dump analysis: `mdmp.py` (guest memory from a full dump; arena base 0 for a retail dump), `colliders.py` (candidate lists per collider), `invariant.py` (axis bits against brute-force overlap), `sortcheck.py`, `replay_sweep.py` and `replay_full.py` (one `PHYS_SweepAxis` call in Unicorn), `x86dis.py` |
| `tools/fps_limit_launcher.py` | Starts the retail Windows build with a frame limiter patched in memory; used for the retail dumps |
| `recomp/difftest/` | `difftest.py`, `wat.py`, `t_core.c`, `t_switch.c`, `gen_insn.py`, `coverage.py`, `flagdiff.py`, `consumers.py` |
| `out/recomp/difftest/` | Work directories (`<name>-<tag>/`), the generated `t_insn.c`, `cov.txt`, `flagdiff.txt` |
| `out/recomp/matchdecomp/` | `match.py`, `flagsweep.py`, `cases.txt`, `src/`, `blind/`, the toolchain evidence scripts |
| `out/dev/research/pcrecomp/` | Notes on the pcrecomp pipeline and the hybrid approach |
| `out/decomp/` | `DecompileAll.java` output for both Windows programs, `pre110/` baseline, `compare.py` |
