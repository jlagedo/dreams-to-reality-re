# The `.DSN` loader in `WINDREAM.EXE`

Decompilation of `FUN_004175bc` and the stream helpers it uses. **[verified]** —
all addresses are from `WINDREAM.EXE`, image base `0x400000`.

## Headline

**The binary confirms the header layout we derived by hand from the files.** The
loader reads a `u32` at **offset 5** — the unaligned file-size field we found by
diffing six samples — straight out of the header pointer:

```c
_DAT_005df494 = *(undefined4 *)(iVar3 + 5);
```

Two independent methods agreeing is as strong as this gets without source.

## But `FUN_004175bc` is the header reader, not the unpacker

It validates the tag, extracts three header fields into globals, memcpy's two
blocks, sets a "loaded" flag and returns. There is no decompression loop in it.
The packed body is consumed elsewhere — see [Next](#next).

## The stream abstraction

Everything is read through a buffered stream with a **peek/commit** pair, not by
indexing a flat buffer. `FUN_00454feb()` returns the stream context.

**[verified]** against the Watcom 10.6 libraries in `E:\dev_game\watcom`
(`sigs/windream.csv`, produced by `python -m dreams.watcom`). Four entries in the
original version of this table were guesses and three of them were **wrong** —
they are stock C runtime, not Cryo stream code:

| Function | Role | Source |
|---|---|---|
| `FUN_00454feb` | **`__CHK`** — Watcom stack-overflow probe | library match |
| `FUN_004154db` | **peek(stream, n)** — ensure n bytes buffered | Cryo code |
| `FUN_004155a2` | **commit(stream)** — advance past the last peek | Cryo code |
| `FUN_0045c278` | **`memcpy_`** | library match |
| `FUN_0045fd67` | **`strncmp_`** | library match |
| `FUN_0045fd4e` | **`strlen_`** | library match |
| `FUN_00460d12` | **`memmove_`** | library match |
| `FUN_00460d5f` | **`printf_`** | library match |
| `FUN_00460d82` | still unidentified | Cryo code |

`FUN_00454feb` is the important correction. It is Watcom's stack probe, called
at entry with the frame size in EAX — **not** a "get stream context" accessor.
The `CryoStream` layout below was reconstructed on the assumption that it
returned a context pointer, so **treat that struct as unverified**: the field
offsets came from a decompilation made under the wrong calling convention.
`FUN_00460d5f` being `printf_` likewise kills the "refill from file" reading.

### Stream context layout

Recovered from the field offsets in `FUN_004154db` and `FUN_004155a2`:

```c
struct CryoStream {        // 0x2c+ bytes
/* 0x00 */ uint8_t *base;          // buffer base pointer
/* 0x04 */ uint32_t cursor;        // read offset within the buffer
/* 0x0c */ uint32_t remaining;     // bytes left in the stream
/* 0x10 */ uint32_t consumed;      // running total consumed
/* 0x14 */ uint32_t limit;         // end of valid buffered data
/* 0x18 */ uint32_t span;          // secondary extent, used in the refill test
/* 0x24 */ uint8_t  flags;         // commit() clears bit 1 (&= 0xFD)
/* 0x28 */ uint32_t peeked;        // size of the outstanding peek
};
```

`commit()` wraps the cursor when `cursor + peeked` passes `limit`, so this is a
**ring buffer** — the loader streams the file rather than loading it whole.
That matters: a 1.8 MB scene is never fully resident.

**[unverified]** — field names are inferred from use, not from symbols.

## `FUN_004175bc` reconstructed

**Re-decompiled under `__watcall`.** The peek lengths were invisible before —
they travel in EDX — and they are what pins the body offset:

```c
int load_dsn_header(void)
{
    __CHK(frame);                           // FUN_00454feb — stack probe
    /* ... open + path/string build ... */
    if (!open(g_file, path)) return 0;      // FUN_004152f2

    hdr = peek(g_file, 9);                  // magic + u8 + u32 size
    if (!hdr) return 0;
    if (strncmp(hdr, "DSNF", strlen("DSNF")) != 0)
        return 0;
    g_dsn_size = *(uint32_t *)(hdr + 5);    // <-- OFFSET 5, unaligned
    commit(g_file);

    p = peek(g_file, 5);                    // u8 flag + u32 headerSpan A
    if (!p || *p != 0) return 0;            // the u8 must be zero; A is SKIPPED
    commit(g_file);

    p16 = peek(g_file, 2);
    if (!p16) return 0;
    g_dsn_count = *(uint16_t *)p16;         // <-- our name_count, B
    commit(g_file);

    len = g_dsn_count * 0xb;                // 11-byte name records
    blk = peek(g_file, len);  if (!blk) return 0;
    memcpy(0x5df720, blk, len);
    commit(g_file);

    len = g_dsn_count * 0x14;               // 20-byte per-object records
    blk = peek(g_file, len);  if (!blk) return 0;
    memcpy(0x5df4a0, blk, len);
    commit(g_file);

    g_dsn_loaded = 1;
    return 1;
}
```

### This settles the body offset

The loader consumes `9 + 5 + 2 = 16` bytes of header, then `11B`, then `20B`
**with nothing in between**. So the packed body begins at **`16 + 31B`**, which
equals `9 + A` — the same relation `.DAN` uses. `file-formats.md` previously said
`24 + 31B` and postulated an 8-byte "scene-wide block" to explain the gap. There
is no such block; the 8 bytes belong to the body, and they are an `01` tag plus
a `u32`, exactly matching how `.DAN` bodies open. Corrected everywhere.

Note also that **`A` is never read.** The loader derives everything from `B`, so
`A = 31B + 7` is a redundant field the exporter wrote for seek-ahead.

### Globals it populates

| Address | Meaning |
|---|---|
| `_DAT_005df494` | `u32` from header offset 5 — the file size |
| `_DAT_005df498` | `u16` — the object/name count |
| `_DAT_005df49c` | set to 1 on success — "scene header loaded" |
| `0x5dfa84` | written by a string op before the read — likely the path buffer |

Cross-reference [file-formats.md](file-formats.md): we measured `name_count` as
26 / 25 / 29 on three scenes and confirmed the name table holds exactly that
many 11-byte records. The loader reading a `u16` into a global here matches.

## The blocker: wrong calling convention

**Every decompilation in this binary is currently lossy.** Ghidra assumes cdecl
or guesses `__fastcall`, but Watcom's default is **`__watcall`** — arguments in
`EAX, EDX, EBX, ECX`, returns in `EAX`. That is why the output is full of
`extraout_ECX`, `unaff_EBX` and `extraout_EDX`: those *are* the parameters, and
Ghidra cannot see them.

`FUN_0045c278` makes it obvious. It is plainly `memcpy`, and the register roles
read straight off:

```c
undefined4 * __fastcall FUN_0045c278(undefined4 param_1, undefined4 *param_2) {
    undefined4 *in_EAX;        // <- dest      (EAX)
    uint unaff_EBX;            // <- count     (EBX)
    ...                        //    src is param_2 (EDX)
}
```

So `memcpy(EAX=dst, EDX=src, EBX=len)`. Likewise `peek()` takes its length in
`EDX` — which is why `FUN_004154db()` appears to take no arguments while
comparing against `extraout_EDX` internally.

### Fixed — `tools/watcall-cspec.patch`

**[verified]** Ghidra 12.1.3 ships **no Watcom compiler spec** (zero matches for
"watcom" under `Ghidra/Processors/x86/data/`). One was added:

1. `tools/watcall-cspec.patch` adds a `__watcall` prototype model to
   `x86win.cspec` — inputs `EAX, EDX, EBX, ECX` then stack, output `EAX` /
   `EDX:EAX` / `ST0`, callee stack cleanup, the four argument registers marked
   `killedbycall` (which is why `EBX` differs from `__fastcall`).
2. `ghidra_scripts/ApplyWatcall.java` applies it program-wide, skipping the 31
   runtime helpers that have bespoke register contracts. Watcom decorates
   register-convention symbols with a **trailing underscore** (`memcpy_`,
   `strlen_`), so `sigs/windream.csv` classifies them for free: 88 take
   `__watcall`, 31 (`__CHK`, `__STK`, `IF@DSIN`, `__FDD`, …) do not.
3. It also resets each signature to `SourceType.DEFAULT` — while a signature
   inferred under the wrong convention stands, the decompiler will not promote
   `EBX`/`ECX` to parameters no matter what the prototype model says. This step
   is essential and easy to miss.

Applied to `WINDREAM.EXE`: **1251 functions converted, 26 skipped.**

Ground truth: `FUN_0045c278` is `memcpy_` per the library match, and under
`__watcall` it decompiles as a textbook `memcpy(param_1, param_2, param_3)`
returning `param_1` — a dword loop plus a byte tail. Two independent methods
agreeing.

Re-apply the cspec patch after any Ghidra upgrade, as with the GhidraMCP patch.

Pairs with `src/dreams/watcom.py`, which recovers Watcom 10.6 runtime symbol
names from the stock OMF libraries — see [toolchain.md](toolchain.md). Between a
correct cspec and named runtime functions, only Cryo's own code stays anonymous.

## Next

The unpacker is not in `FUN_004175bc`. Leads, in order:

1. **`FUN_0041da7d`** — the sole caller. It presumably calls the header reader
   and then walks the body.
2. **`FUN_004177f5`, `FUN_004178fe`, `FUN_00417afd`** — siblings in the same
   `0x417xxx` neighbourhood that also use `peek`/`commit`. Strong candidates for
   the per-record body readers.
3. **`FUN_0041754e`, `FUN_0043b0e2`, `FUN_004152f2`, `FUN_004153fe`** — the
   open/setup chain, worth naming to make everything else readable.

Whatever consumes bytes past the name table and writes decoded output is the
answer to the 157 MB question.
