# 000 — The recomp: using the Watcom toolchain and the recompiled game as research instruments

Status: **W1–W3 done 2026-09-28** (results under each item); W4–W7 not started
Date: 2026-09-28
Depends on: [toolchain.md](../../toolchain.md) (Watcom 11.0 verdict),
[re-setup.md](../../re-setup.md) (Ghidra passes). Numbered 000 because it
serves the reverse engineering that every OpenDreams spec (001 onward) ports
from, rather than an OpenDreams deliverable.

## Purpose and boundary

We run three experiments beside Ghidra, all under the gitignored
`out/recomp/`:

| Instrument | Directory | What it is |
|---|---|---|
| **Static recompilation** | `out/recomp/windream/` | `GDIDREAM.EXE` lifted instruction by instruction to C with pcrecomp's `lift32`, built with clang-cl against hand-written Win32 shims, and run. |
| **Differential tests** | `out/recomp/difftest/` | Small Watcom programs run natively and recompiled; the outputs are diffed to find lifter bugs. |
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
  - Build: `build.ps1` (clang-cl, Ninja).
  - Run: `run.ps1` (scripted keys, BMP snapshots).
- **Result:** the recompiled game plays `INTRO.HNM` and `GENERIC.HNM`, takes
  scripted Esc/Return and reaches level loading. An older run reached the
  in-game view.
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
  - `difftest/wat.ps1`: build with 11.0 and the game's two flag profiles.
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
    reaches level loading (`OMBRE.3DC`, `xh_.DAN`, `GR00.3DC`).

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

## File map

| Path | Content |
|---|---|
| `out/recomp/pcrecomp/` | Upstream toolbox clone (`tools/lift`, `tools/disasm`, `tools/ghidra/DumpBounds.java`, `runtime/recomp32`) |
| `out/recomp/nocturne/`, `out/recomp/pod-recomp/` | Reference Watcom recomp projects |
| `out/recomp/windream/` | `lift.py`, `bounds.csv`, `gen/`, `runtime/`, `build.ps1`, `run.ps1`, `run/` |
| `out/recomp/difftest/` | `difftest.ps1`, `wat.ps1`, `t_*.c`, `cov.txt`, `flagdiff.txt` |
| `out/recomp/matchdecomp/` | `match.py`, `flagsweep.py`, `cases.txt`, `src/`, `blind/`, the toolchain evidence scripts |
| `out/dev/research/pcrecomp/` | Notes on the pcrecomp pipeline and the hybrid approach |
