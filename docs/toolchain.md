# Toolchain

Which compiler built the game, how that was established, and the reference
material now held locally to exploit it.

`engine.md` establishes *that* all four executables are Watcom builds. This doc
pins the *version* and turns it into recovered symbol names.

## Verdict

All four game executables were built with **Watcom C/C++ 10.6** (released
August 1996). **[verified]**

10.0, 10.5 and 11.0 are excluded. 10.6 and its maintenance release **10.6a
cannot be told apart** by any method here — they ship a byte-identical
`CLIB3R.LIB` (MD5 `3bb5e9971a8cd3dcfa1b96b542c1c51a`).

`SETUP.EXE` is *not* part of this — it is MSVC-built, as its `.text`/`.data`
section names show.

## Step 1 — the runtime banner narrows it to the 10.5/10.6 family

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
| Watcom 11.0c | `Sybase, Inc. 1988-2000` |

Sybase branding rules out 11.0 immediately. 10.5 and 10.6 share a banner, so the
string alone cannot separate them.

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

## Step 3 — supporting detail

- `WINDREAM.EXE` PE timestamp is **1997-10-29 14:25:41 UTC**, before Watcom 11.0
  shipped. **[verified]**
- PE linker version is **2.18**. That is Watcom's `wlink` — `PE_LNK_MAJOR 2`,
  `PE_LNK_MINOR 0x12`, hardcoded in
  [`bld/watcom/h/exepe.h`](https://github.com/open-watcom/open-watcom-v2/blob/master/bld/watcom/h/exepe.h).
  It confirms the linker but carries no version information. **[verified]**
- `DOS4GW.EXE` is DOS/4GW **1.97**, build stamp `May 19 1994`, with the
  `Rational Systems, Inc. 1990-1994` copyright. **[verified]**
- `WINDREAM.EXE` resources carry the `eGW4` marker of Watcom's resource
  compiler. **[verified]**
- The game binaries contain **no debug information** — no object names, no
  source paths beyond `X:\CRYO\DREAMS\`. **[verified]**

## Reference material on disk

Nothing below is in this repo. Paths on this machine, under
`paths.get("watcom")`:

```
E:\dev_game\watcom\                                    258 MB
├── sources\
│   ├── open_watcom_1.0.0-src.zip           38 MB   26,600 files, dated 2003-01-24
│   └── open_watcom_1.0.0-src\             146 MB   extracted
│       └── bld\   clib 12M · cg 6.1M · wl 2.6M · cc 2.4M · wlib, wpp, wdw, ...
└── 10.6-cd\                                74 MB   read off the retail 10.6 ISO
    ├── SRC\                               569 KB   genuine 10.6 source (69 files)
    ├── LIB386\                             70 MB   187 files, DOS/NT/OS2/WIN/NETWARE
    └── H\                                 2.5 MB   10.6 headers
```

### What is and is not available **[sourced]**

- **Compiler source exists only for 11.0c.** Sybase open-sourced the Watcom
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
the extraction here was done with HTTP range reads against the ISO9660
directory rather than downloading the full 664 MB image.

## Recovering runtime symbol names

Watcom's libraries are OMF, and OMF keeps `PUBDEF` records: real function names
bound to the exact bytes the linker copies into the image. Since the exact
library version is now known, the stock libraries act as a symbol source.

`src/dreams/watcom.py` parses the libraries, masks out every byte covered by a
`FIXUPP` record (call displacements and absolute addresses differ between the
`.LIB` and the linked `.EXE`), and scans the executables. A hit requires 32
matching bytes; whether the rest of the symbol's extent also matches is recorded
but not required.

```
PYTHONPATH=src python -m dreams.watcom
```

Results: **[verified]**

| Binary | Libraries | Signatures | Matches | Full extent |
|---|---|---|---|---|
| `WINDREAM.EXE` | `NT\CLIB3R`, `MATH387R`, `MATH3R` | 989 | 119 | 102 |
| `GDIDREAM.EXE` | same | 989 | 119 | 102 |
| `DREAMS.EXE` | `DOS\CLIB3R`, `MATH387R`, `MATH3R`, `DOS\GRAPH`, `DOS\EMU387` | 1242 | 287 | 285 |
| `DREAMSFX.EXE` | same | 1242 | 282 | 280 |

The DOS builds link far more of the runtime than the Windows ones, and they do
use Watcom's own graphics library: `GRAPH.LIB` alone accounts for 40 matches,
including `_getvideoconfig_`, `_clearscreen_` and `_settextposition_`.

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
them all, e.g. `fprintf_|fscanf_|sscanf_`.

### Applying the output

- **IDA** (Free works): File → Script file → `windream.idc`. Symbols are
  prefixed `wat_`. FLAIR's `pcf`/`sigmake` are not needed and are not bundled
  with IDA Free.
- **Ghidra**: Script Manager → `windream_ghidra.py`. It labels the address and
  renames the containing function when the match is at its entry point.

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
2. The DOS builds' matches are reported as file offsets only. Mapping them to
   linear addresses needs an LE-format section walker, which would also let the
   DOS builds get IDA/Ghidra scripts rather than just a CSV. **[unverified]**
3. 10.6 versus 10.6a is undecidable from the runtime library, which is
   byte-identical between them. Whether anything else in the two distributions
   differs in a way the game binaries would reveal has not been checked.
   **[unverified]**
