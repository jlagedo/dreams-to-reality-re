# Toolchain

Which compiler built the game, how that was established, and the reference
material now held locally to exploit it.

`engine.md` establishes *that* all four executables are Watcom builds. This doc
pins the *version* and turns it into recovered symbol names.

## Verdict

The four game executables do **not** share one toolchain. **[verified]**

| Binary | Game code compiled by | Runtime library | Linker |
|---|---|---|---|
| `WINDREAM.EXE`, `GDIDREAM.EXE` | **Watcom 11.0** | **11.0** (not 11.0a) | 11.0 `wlink` |
| `DREAMS.EXE` | a **mix** of 10.6 and 11.0 object files | 10.6 | not checked |
| `DREAMSFX.EXE` | a **mix**, mostly 10.6 (built with `-d1+`) and some 11.0 | 10.6 | not checked |

An earlier revision of this page said all four were Watcom 10.6. It excluded
11.0 using the 11.0c banner (Sybase, 2000), but plain 11.0 (1997) still prints
the WATCOM International banner, and the library fingerprinting only compared
10.5 with 10.6. Steps 1 and 2 below are still correct as far as they go: the
game is not 10.5, and the DOS builds do link the 10.6 runtime. Steps 3–6 are
the evidence for the revised verdict.

Compiler flags for matching Windows code (step 6): most of Cryo's Windows code
was compiled **unoptimized with stack checking and full debug info**,
reproduced by `-5r -d2` (each function starts `push N; call __CHK`). `-d2`
implies `-od` and also turns off the jump folding and register reuse that
plain `-od` still does. 92 byte-exact unoptimized functions all match under
`-5r -d2`, and 56 of them only under it (step 6). The optimized modules are
reproduced by `-5r -otexan -s`; `-4r`/`-6r` and `-oaxt`/`-oneatx` give the
same bytes, while `-3r` and plain `-ox` do not.

`SETUP.EXE` is *not* part of this — it is MSVC-built, as its `.text`/`.data`
section names show.

## Step 1 — the runtime banner narrows it to 10.5–11.0

Every game binary carries this string verbatim:

```
WATCOM C/C++32 Run-Time system. (c) Copyright by WATCOM International Corp. 1988-1995.
```

The DOS builds additionally carry a `C/C++16 ... 1988-1994` banner at offset
`0x3ef` — that is the unchanged DOS/4GW LE stub, not a second compiler.

Reference banners pulled from original distributions: **[verified]**

| Release | `CLIB3R.LIB` banner |
|---|---|
| Watcom 10.5 | `WATCOM International Corp. 1988-1995` |
| Watcom 10.6 / 10.6a | `WATCOM International Corp. 1988-1995` |
| Watcom 11.0 | `WATCOM International Corp. 1988-1995` |
| Watcom 11.0c | `Sybase, Inc. 1988-2000` |

Sybase branding rules out 11.0c only. 10.5, 10.6 and 11.0 share a banner, so
the string alone cannot separate them.

The banner's origin is visible in source: `SRC\STARTUP\386\CSTRT386.ASM` on the
10.6 CD, lines 177–179 —

```asm
	db	"WATCOM C/C++32 Run-Time system. "
        db      "(c) Copyright by WATCOM International Corp. 1988-1995."
	db	" All rights reserved."
```

— which is the startup module that becomes each executable's entry point.

## Step 2 — code fingerprinting separates 10.5 from 10.6

Method: parse the OMF `LEDATA` payloads out of the 10.5 and 10.6 `CLIB3R.LIB`
files, build the set of 24-byte code windows unique to each version, then count
how many of each appear in the game binaries. Low-entropy windows (fewer than 10
distinct byte values) are discarded — repetitive data tables otherwise produce
matches in both directions.

| Binary | 10.5-only windows | 10.6-only windows |
|---|---|---|
| `WINDREAM.EXE` | 5 (longest run 26 B) | **174 (longest run 133 B)** |
| `GDIDREAM.EXE` | 5 (longest run 26 B) | **174 (longest run 133 B)** |
| `DREAMS.EXE` | 260 (longest run 46 B) | **1345 (longest run 178 B)** |
| `DREAMSFX.EXE` | 260 (longest run 46 B) | **1553 (longest run 178 B)** |

The long 10.6 runs resolve to named library modules — `prtf` (the printf core,
DOS builds), `bufld386` and `signlwnt` (Windows builds).

The residual 10.5 column is noise, and it was checked rather than assumed: those
hits are short runs interleaved with 10.6 hits at library chunk boundaries, and
the two longest apparent 10.5 runs in `DREAMSFX.EXE` (199 B and 135 B at
`0xda529` / `0xda469`) turned out to be a repetitive `01 00 00 00` data table,
not code.

Matching 10.6 exactly is positive evidence and does not depend on the 10.5
comparison, so 10.0 is excluded too: its library would differ from 10.6 as well,
and the game binaries would then match neither.

This step did not test 11.0. Much of 11.0's library is byte-identical to
10.6's, so "10.6-only versus 10.5" windows do not exclude 11.0; step 3 does
that comparison.

## Step 3 — the runtime library: 11.0 on Windows, 10.6 on DOS

The same method as step 2, with full-extent signature matches: count library
functions whose bytes exist in only one version's `CLIB3R`, `MATH387R` and
`MATH3R` (DOS builds also `GRAPH` and `EMU387`). **[verified]**

| Binary | 10.6-only functions found | 11.0-only functions found |
|---|---|---|
| `WINDREAM.EXE`, `GDIDREAM.EXE` | 2 of 524 | **179 of 745** |
| `DREAMS.EXE` | **140 of 488** | 2 of 837 |
| `DREAMSFX.EXE` | **137 of 488** | 1 of 837 |

11.0 against 11.0a on `WINDREAM.EXE`: **127** of 535 functions unique to 11.0
are present, **0** of 552 unique to 11.0a. The 11.0-only matches include the
thread-data and semaphore code, `_EFG_Format_` and a `scanf` integer parser
with a 64-bit (`long long`) path that calls `__U8M`; 10.6's `scanf` has no
64-bit path.

## Step 4 — the linker: 11.0's section layout

The PE linker version field is **2.18** in all four PE outputs tested: 10.6,
10.6a and 11.0 `wlink` all write it (`PE_LNK_MAJOR 2`, `PE_LNK_MINOR 0x12`,
hardcoded in
[`bld/watcom/h/exepe.h`](https://github.com/open-watcom/open-watcom-v2/blob/master/bld/watcom/h/exepe.h)),
so it identifies Watcom but not the version. The section layout and header
flags do. Linking one test program with each linker against each version's
libraries shows that the linker alone decides them: **[verified]**

| Linker | Section order | `Characteristics` |
|---|---|---|
| 10.6, 10.6a | `BEGTEXT, DGROUP, .bss, .idata, .reloc` | `0x8182` |
| 11.0 | `AUTO, .idata, DGROUP, .bss, .reloc` | `0x182` |
| `WINDREAM.EXE` | `AUTO, .idata, DGROUP, .bss, .reloc, .rsrc` | `0x182` |

## Step 5 — code-generation idioms that do not depend on the source

Constructs compiled with both compilers at every tested flag set, where the
difference comes from the compiler version and not from how the C is written:
**[verified]**

- **Switch jump tables.** 10.6 dispatches with `jmp cs:[reg*4+table]`
  (`2E FF 24 ..`) under all 375 flat-model flag sets tried; 11.0 never emits
  the `CS` prefix in the flat model (420 flag sets), only with `-ms`.
- **Division by a constant.** 10.6 loads the divisor into EBX, 11.0 into ECX.
  This is the same register-preference change that breaks 10.6 matches in
  general (11.0 tries ECX before EBX). It is soft evidence: 11.0 uses EBX when
  ECX is busy.
- **Unoptimized frames without locals.** 10.6 emits `sub esp,0`; 11.0 reserves
  a spare 4 bytes (`sub esp,4`), stack-checks 4 more bytes and restores with
  `mov esp,ebp`.

Counts outside the matched runtime library:

| Binary | Switch `cs:` (10.6) | Switch plain (11.0) | Divisor EBX / ECX | Unoptimized frames: `sub esp,0` / `sub esp,4` / total |
|---|---|---|---|---|
| `WINDREAM.EXE` | 1 | **112** | 9 / **31** | **0** / 147 / 898 |
| `DREAMS.EXE` | 49 | 111 | 26 / 10 | 1 / 0 / 24 |
| `DREAMSFX.EXE` | **67** | 38 | 27 / 8 | 0 / 0 / 37 |

Under 10.6, every unoptimized function without locals would produce
`sub esp,0`; `WINDREAM.EXE` has 898 unoptimized frames and none of them. The
counts also show that the Windows game code is mostly unoptimized, while the
DOS builds are almost entirely optimized.

**The DOS builds mix object files.** Ordered by address, the 10.6 and 11.0
markers form long runs rather than interleaving: `DREAMS.EXE` has 21 version
changes across 196 markers where random order would give about 92, and
`DREAMSFX.EXE` 13 across 140 against about 61. The linker lays out whole
object files contiguously, so this is what object files compiled by different
compilers look like. All four executables were written to the disc on
1997-10-29 between 12:07 and 12:25, so the older-compiler objects were relinked
on the same day, not left over from an earlier build.

## Step 6 — matching decompilation

Functions written back in C and compiled with each compiler, then compared
byte for byte with retail; relocated bytes are wildcards. **[verified]**

- **Pilot, seven small `MATH_` functions** (`0x45b154`–`0x45bb84`): all seven
  are byte-exact under 11.0 with the flags above; 10.6 matches only
  `MATH_SetIdentityMat3`, which both compilers produce the same way. The same C
  compiled with 11.0 is found byte-exact in `DREAMS.EXE` (at `0x913a4`–`0x91dd4`,
  inside an 11.0 run of step 5); compiled with 10.6 `-5r -otexan -d1+ -s`, six of
  the seven are found in `DREAMSFX.EXE` (`0x86d24`–`0x8776c`, inside a 10.6 run).
- **Blind test, 24 random functions.** Drawn from `WINDREAM.EXE` with a fixed
  seed, excluding the runtime library and the pilot. Four agents wrote C from
  the Ghidra decompilation and retail disassembly. They saw the compilers only
  as "A" and "B"; every attempt was scored under both, with the same flags, at
  most 12 attempts per function.

  | Measure | 11.0 | 10.6 | Two-sided sign test |
  |---|---|---|---|
  | Exact from the first-draft C, only under one compiler | **12** | 1 | p = 0.0034 |
  | Exact on any attempt, only under one compiler | 5 | 1 | p = 0.22 |

  The second row does not separate the compilers because in unoptimized code
  10.6 also matches once the C gains an unused local variable, which pads its
  frame to 11.0's size. The writers found this blind and every such 10.6 match
  needed one. The single 10.6-only first-draft match, `0x46068d`, is not Cryo
  code: its bytes are in 11.0's own `CLIB3R.LIB` (the heap code beside `free`
  at `0x460589`). The table leaves out three sampled functions next to the
  runtime's `scanf` code that may be unrecognised library code: `0x4896ec`
  (10.6-exact) and two hand-written assembly routines, `0x48a64a` and
  `0x48d7e8`. Besides those two, `0x40464f` and `VID_Lock` (`0x445bf2`)
  matched under neither compiler with the `-5r -od` the test used. Retail
  has a `je +2; jmp` branch there. Rescored under 11.0 `-5r -d2` on
  2026-09-28, five attempts at each are exact; `-d2` keeps the conditional
  jump over a `jmp` that `-od` folds.
- **Port-map sweep, 126 functions** (2026-09-28;
  [spec 000](specs/000-the-recomp/spec.md), W3). Every retail function in
  `opendreams/port-map.tsv` of at most 500 bytes, runtime library excluded,
  was written as C from the Ghidra decompilation and compiled under 11.0
  (at most 10 compiles each). This is not blind: the writers knew the
  compiler.

  | Outcome | Functions |
  |---|---:|
  | Byte-exact, unoptimized (`-5r -d2`) | 92 |
  | Byte-exact, optimized (`-5r -otexan -s`) | 15 |
  | Hand-written assembly: `pushad`, results in `EDI`, `stc`/`adc` bit refills | 9 |
  | Optimized, only register allocation or instruction order differs | 9 |
  | Unoptimized, off by two stack slots | 1 |

  Two of the hand-written routines match only as a naked `_asm` body.

  The same 107 exact sources compiled with 10.6 (same flags) give 15 exact.

The scripts for steps 3–6 are in `out/recomp/matchdecomp/` (local, not
committed): `libversion.py`, `linkver.ps1`, `fpscan.py`, `fpruns.py`,
`framescan.py`, `match.py` and `blind/`.

## Step 7 — supporting detail

- `WINDREAM.EXE` PE timestamp is **1997-10-29 14:25:41 UTC**. Watcom 11.0 is
  a 1997 release, and the 11.0a CD's files are dated 1997-08-29, so both
  predate the build. **[verified]**
- `DOS4GW.EXE` is DOS/4GW **1.97**, build stamp `May 19 1994`, with the
  `Rational Systems, Inc. 1990-1994` copyright. **[verified]**
- `WINDREAM.EXE` resources carry the `eGW4` marker of Watcom's resource
  compiler. **[verified]**
- The configured English game binaries have zero PE/LE debug-directory fields,
  no object names and no source paths beyond `X:\CRYO\DREAMS\`.
  Dutch and Spanish Windows builds retain OMF object records with original
  renderer names and source paths; see
  [localized-build-symbols.md](localized-build-symbols.md). **[verified]**

## Reference material on disk

Nothing below is in this repo. Paths on this machine, under
`paths.get("watcom")`:

```
E:\dev_game\watcom\                                    258 MB
├── sources\
│   ├── open_watcom_1.0.0-src.zip           38 MB   26,600 files, dated 2003-01-24
│   └── open_watcom_1.0.0-src\             146 MB   extracted
│       └── bld\   clib 12M · cg 6.1M · wl 2.6M · cc 2.4M · wlib, wpp, wdw, ...
├── 10.6-cd\                                74 MB   read off the retail 10.6 ISO
│   ├── SRC\                               569 KB   genuine 10.6 source (69 files)
│   ├── LIB386\                             70 MB   187 files, DOS/NT/OS2/WIN/NETWARE
│   └── H\                                 2.5 MB   10.6 headers
├── wc106\watcom10.6\                               installed 10.6 GA (binaries dated 1996-02-29)
├── wc106a\10.6a\                                   10.6a linkers only (dated 1997-01-10)
└── wc110\
    ├── 11.0\                                       11.0: BINNT, BINW, H, LIB386
    └── 11.0a\                                      11.0a: BINNT, BINW, H, LIB386 (dated 1997-08-29)
```

`wc106` and `wc110` hold working compilers (`BINNT\WCC386.EXE`), used for
matching decompilation.

### What is and is not available **[sourced]**

- **Compiler source exists only for 11.0c**, a later update of the compiler
  that built the Windows game code. Sybase open-sourced the Watcom
  codebase in 2003 under the Sybase Open Watcom Public License; that release,
  Open Watcom 1.0, *is* the 11.0c tree. Mirrors:
  [openwatcom.org/ftp/source/](https://openwatcom.org/ftp/source/),
  [open-watcom-v2](https://github.com/open-watcom/open-watcom-v2) (maintained),
  [open-watcom-v1](https://github.com/open-watcom/open-watcom-v1) (frozen 1.9).
- **10.6's own tree was never released.** Treat 1.0.0 as a near relative, not as
  ground truth: `cstrt386.asm` alone grew 13 KB → 20 KB between the 10.6 CD copy
  and the 2003 tree, with 883 diff lines. For anything the CD ships, the
  `10.6-cd\SRC\` copy is authoritative.
- **No PDBs exist and none can.** PDB is Microsoft's format; Watcom never
  emitted one. Watcom wrote debug info *inside* the executable in its own
  format, or optionally CodeView or DWARF — the 10.6 CD ships `DWARF.DLL` and
  `CODEVIEW.DLL` in `BINNT\` as debugger readers. No symbol package for
  Watcom's own tools was ever published, and the retail tool binaries are
  stripped.

The 10.6 CD is on the Internet Archive as
[watcom-c-cpp-compilers-collection](https://archive.org/details/watcom-c-cpp-compilers-collection);
`10.6-cd` was extracted with HTTP range reads against the ISO9660 directory.
The whole collection (4.8 GB, every release from 6.5 to 11.0c) has since been
downloaded; `wc106a` and `wc110` come from its `watcom-10.6a`, `watcom-11.0`
and `watcom-11.0a` items.

## Recovering runtime symbol names

Watcom's libraries are OMF, and OMF keeps `PUBDEF` records: real function names
bound to the exact bytes the linker copies into the image. Given the library
version, the stock libraries act as a symbol source. Each binary is matched
against the runtime it links (step 3): 11.0's `wc110\11.0\LIB386` for the
Windows builds, 10.6's `10.6-cd\LIB386` for DOS (`TARGETS` in `watcom.py`).

`src/dreams/watcom.py` parses the libraries, masks out every byte covered by a
`FIXUPP` record (call displacements and absolute addresses differ between the
`.LIB` and the linked `.EXE`), and scans the executables. A hit requires 32
matching bytes; whether the rest of the symbol's extent also matches is recorded
but not required.

```
PYTHONPATH=src python -m dreams.watcom
```

Results: **[verified]**

| Binary | Runtime | Libraries | Signatures | Matches | Full extent |
|---|---|---|---|---|---|
| `WINDREAM.EXE` | 11.0 | `NT\CLIB3R`, `MATH387R`, `MATH3R` | 1219 | 271 | 267 |
| `GDIDREAM.EXE` | 11.0 | same | 1219 | 271 | 267 |
| `DREAMS.EXE` | 10.6 | `DOS\CLIB3R`, `MATH387R`, `MATH3R`, `DOS\GRAPH`, `DOS\EMU387` | 1242 | 287 | 285 |
| `DREAMSFX.EXE` | 10.6 | same | 1242 | 282 | 280 |

The DOS builds use Watcom's own graphics library: `GRAPH.LIB` alone accounts
for 40 matches, including `_getvideoconfig_`, `_clearscreen_` and
`_settextposition_`.

The Windows pass used the 10.6 libraries until 2026-09-28 and found 119
matches (102 full extent). Moving to 11.0 **[verified]**:

- 114 of the 119 addresses match again; 157 are new. Twelve of the new ones are
  runtime routines the name registry had identified by blind review alone
  because 10.6 missed them (`read_`, `lseek_`, `memset_`, `atoi_`, `exit_`,
  `_dos_findfirst_`, …); the 11.0 signature gives the same name at the same
  address for every one. No new match lands on a registry name.
- Four 10.6 hits were core-only labels one to three bytes past the real entry
  (`__ioalloc_`, `__NewExceptionHandler_`, `verify_pentium_fdiv_bug_`,
  `__SigInit_`); 11.0 matches those functions over their full extent at the
  true entry.
- `__threadid_` (`0x4783eb`) is lost. Its bytes still agree with 11.0's
  `mainwnt` module, but 11.0 puts another public (`__sig_null_rtn_`) 17 bytes
  in, leaving 9 fixed bytes, under `MIN_FIXED`.
- Ten names change to 11.0's spelling: `_uopen_` → `open_`, `_itoa_` →
  `itoa_`, `_uultoa_` → `ultoa_`, `_strdup_` → `strdup_`, `_ustrlen_` →
  `wcslen_`, `__DLLstart_` → `WinMainCRTStartup` (`0x465538`, the PE entry).

Output lands in `E:\dev_game\watcom\sigs\` — a CSV per binary, plus an IDC
script and a Ghidra script for the two PE targets (LE files have no simple
file-offset-to-address mapping, so the DOS builds get the CSV only).

Checks that the matcher is honest, each of which caught a real defect:

- `_cstart_` is reported at `0x8f2b8` in `DREAMS.EXE`, and the Watcom runtime
  banner string sits at `0x8f2b9` — found separately, by string scan, before the
  matcher existed.
- Signatures are restricted to segments of class `CODE`. Without that filter the
  zero-filled data tables `__IsKTable` and `___MBCSIsTable` match every run of
  nulls in the image: 99,353 false positives each.
- A fixed-length prefix check is not sufficient. `W?$ct:streambuf$n()_` opens
  with 32 bytes that are *all* relocated, so a 32-byte prefix test accepts every
  offset in the file — 149,164 hits. The core prefix therefore extends until it
  carries at least `MIN_FIXED` non-relocated bytes.

Names carry Watcom's register-calling-convention trailing underscore
(`strcpy_`, `fopen_`). Where several public symbols share one body the CSV lists
them all, e.g. `fprintf_|fscanf_|sprintf_|sscanf_`. 11.0 builds wide and
multibyte twins from the ANSI code (`strcat_|_mbscat_`, `open_|_wopen_`); those
are listed after the ANSI name, which is the one the tools apply. The last CSV
column, `runtime`, records the release matched.

### Applying the output

- **IDA** (Free works): File → Script file → `windream.idc`. Symbols are
  prefixed `wat_`. FLAIR's `pcf`/`sigmake` are not needed and are not bundled
  with IDA Free.
- **Ghidra**: `ghidra_scripts/ApplyWatcomSigs.java <sigs>\<program>.csv`
  (headless or Script Manager) names the functions in all four binaries,
  including the LE ones, then `ApplyWatcomHeaders.java` applies the Watcom
  header prototypes (11.0's `H` for Windows, 10.6's for DOS); see
  `re-setup.md`. Re-running replaces the earlier release's names and
  comments. The generated `windream_ghidra.py`
  needs PyGhidra, which this machine's Python 3.14 cannot run.

## Layout rules, from the compiler source

Read from Open Watcom 1.0 (the 11.0c tree), then checked against
`WINDREAM.EXE`. Together these rules make source-file boundaries recoverable
(`tools/find_modules.py`, [re-setup.md](re-setup.md)). The file *names* are
not in the binaries.

| Rule | Source | Seen in the binary |
|---|---|---|
| A whole `.c` file is parsed before any code is generated; functions are then emitted in source order (`GenModuleCode`) | `cc/c/cgen2.c` `DoCompile` | order is kept across builds within a file |
| String literals are emitted the first time the code generator meets them, 4-byte aligned when optimising for time, into that file's `CONST` | `cgen2.c` `Emit1String`, `EmitLiteral` | constants are in code order even inside a file |
| Uninitialised variables, globals included, go to that file's `_BSS`; there are no common symbols | `cc/c/cinfo.c` `AssignSeg` | `.bss` is contiguous per file |
| File-scope variables are emitted by walking `GlobalSym`, which is chained by **name-hash bucket** | `cc/c/csym.c`, `cgen2.c` `EmitSyms` | `.bss` order inside a file looks scrambled |
| Function alignment: 1 byte with `-os` or the default, 4 with `-3` for speed, 16 with `-4`/`-5` | `cg/intel/c/i86enc2.c` `DepthAlign(PROC_ALIGN)` | game functions are unaligned (about 25% are 4-aligned, i.e. chance), so alignment reveals no per-file options; the stack-check prologue does (optimized versus `-od`, step 5) |
| The linker keeps each segment's contributions in link order: every game file, then the libraries | not read; observed only | each data region is the game part, then the library part |

## Was the C++ compiler used?

Probably not, but this is not settled. **[unverified]**

`PLIB3R.LIB`, which holds the C++ iostream and C++ runtime-support modules, is
excluded from the default run because its hits do not survive scrutiny:

- Seven of the nine are the same two `streambuf` constructors matched at four
  different addresses, all core-only, never over the full extent. A constructor
  does not appear four times in one linked image.
- The remaining two, `__wcpp_2_fatal_runtime_error__` and
  `__wcpp_2_undefined_member_function__`, do match over their full extent, but
  they are only 29 and 18 bytes long — at the floor of what this method treats
  as evidence.
- No Watcom C++ runtime error string appears anywhere in any of the four
  binaries.

So the working assumption is a C codebase, consistent with the C-style symbol
residue in `engine.md`. Confirming it needs a different technique.

## Open questions

1. Nothing here identifies the middleware builds (Miles, UniVBE, Glide). Those
   libraries are not on the Watcom CD and would need their own references
   before the same trick could name their functions.
2. ~~The DOS builds' matches are reported as file offsets only.~~ Resolved for
   `DREAMSFX.EXE`: `ApplyWatcomSigs.java` maps them through the LE object and
   page tables and verifies the bytes (279 named, 2 labelled). Ghidra's own
   `Memory.locateAddressesForFileOffset` resolves into the loader's raw
   `.image` copy instead and must not be used. `DREAMS.EXE` is not imported
   yet.
3. ~~10.6 versus 10.6a is undecidable.~~ Mostly moot: the Windows builds are
   11.0 (steps 3–6). 10.6a differs from 10.6 GA only in its 1997-01-10 linkers,
   which write the same PE layout as 10.6 GA (step 4). The DOS builds' LE
   linker version has not been checked. **[unverified]**
4. ~~The Windows runtime-name pass still uses the 10.6 libraries.~~ Resolved
   2026-09-28: 11.0's libraries and headers, applied to both Windows programs
   (above).
5. ~~Some retail debug-built code has a `je +2; jmp` branch and reloads
   locals the way only `volatile` reproduces.~~ Resolved 2026-09-28: 11.0
   `-5r -d2` produces both (`VID_Lock` `0x445bf2`, `CD_ResumeAudio`
   `0x40464f` and 56 port-map functions match only with it). `-d1+` and `-d3`
   give the same code; `-d1` does not. **[verified]**
6. Which object files in `DREAMS.EXE` and `DREAMSFX.EXE` are 10.6 and which
   11.0 is known only as address runs (step 5), not mapped to source files
   (`tools/find_modules.py`).
