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

| Function | Role |
|---|---|
| `FUN_00454feb` | get stream context pointer |
| `FUN_004154db` | **peek(n)** — ensure n bytes buffered, return pointer to them |
| `FUN_004155a2` | **commit()** — advance past the bytes last peeked |
| `FUN_0045c278` | **`memcpy`** — dword loop then byte tail, 35 call sites |
| `FUN_0045fd67` | **`memcmp`/`strcmp`** — returns 0 on match |
| `FUN_0045fd4e` | **`strlen`**-like |
| `FUN_00460d12` | buffer compaction (`memmove` to base) |
| `FUN_00460d5f` / `FUN_00460d82` | refill from file |

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

Cleaned up, with the stream calls named:

```c
int load_dsn_header(void)
{
    ctx = stream_ctx();
    FUN_0041754e();                 // ? setup
    FUN_0043b0e2();                 // ?
    FUN_0045505e(..., ctx);         // ? path/string build
    if (!FUN_004152f2())            // open — returns bool
        return 0;

    FUN_004153fe();                 // ? post-open init
    /* ... two more string ops, one writing to global 0x5dfa84 ... */

    hdr = peek(n);                  // FUN_004154db
    if (!hdr) return 0;

    if (memcmp(hdr, "DSNF", 4) != 0)        // DAT_004c421e
        return 0;

    g_dsn_size = *(uint32_t *)(hdr + 5);    // <-- OFFSET 5, unaligned
    commit();

    p = peek(n);
    if (!p || *p != 0) return 0;            // <-- the u8 flag must be zero
    commit();

    p16 = peek(n);
    if (!p16) return 0;
    g_dsn_count = *(uint16_t *)p16;         // <-- u16, our name_count
    commit();

    blk = peek(n);  if (!blk) return 0;
    memcpy(dst, blk, len);                  // FUN_0045c278
    commit();

    blk = peek(n);  if (!blk) return 0;
    memcpy(dst, blk, len);
    commit();

    g_dsn_loaded = 1;
    return 1;
}
```

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

**Fixing this is the highest-value next action.** Until it is fixed every
function signature is wrong and argument values are invisible. Options:

1. Add a `__watcall` prototype model to a custom compiler spec (`.cspec`) and
   re-import with it. Correct and permanent, but needs a Ghidra cspec written.
2. Override the convention per function in the GUI (Edit Function Signature →
   Calling Convention). Fast for a handful of functions, unscalable.
3. Manually set storage for the handful of runtime helpers (`memcpy`, `memcmp`,
   `strlen`, `peek`) so at least their call sites read correctly.

Option 3 unblocks reading immediately; option 1 is the real fix.

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
