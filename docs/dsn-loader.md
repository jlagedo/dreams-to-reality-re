# The `.DSN` loader in `WINDREAM.EXE`

> **Function names verified (2026-09-26).** Every `WINDREAM.EXE` function this page names is in [`re/names/WINDREAM.EXE.tsv`](../re/names/WINDREAM.EXE.tsv) with two independent sources (these docs and a blind review of the decompilation) and facts checked against the binary by `tools/check_names.py`.

Decompilation of `DSN_LoadHeader` (`0x4175bc`) and the stream helpers it uses. **[verified]** —
all addresses are from `WINDREAM.EXE`, image base `0x400000`.

## Headline

**The binary confirms the header layout we derived by hand from the files.** The
loader reads a `u32` at **offset 5** — the unaligned file-size field we found by
diffing six samples — straight out of the header pointer:

```c
_DAT_005df494 = *(undefined4 *)(iVar3 + 5);
```

Two independent methods agreeing is as strong as this gets without source.

## But `DSN_LoadHeader` (`0x4175bc`) is the header reader, not the unpacker

It opens the file (`STRM_Open` (`0x4152f2`)), validates the tag, extracts three
header fields into globals, memcpy's two blocks, sets a "loaded" flag and returns. There is no decompression loop in it.
The packed body is consumed elsewhere — see [The body unpackers](#the-body-unpackers-and-call-hierarchy-verified).

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
| `STRM_Peek` (`0x4154db`) | **peek(stream, n)** — ensure n bytes buffered | Cryo code |
| `STRM_Commit` (`0x4155a2`) | **commit(stream)** — advance past the last peek | Cryo code |
| `FUN_0045c278` | **`memcpy_`** | library match |
| `FUN_0045fd67` | **`strncmp_`** | library match |
| `FUN_0045fd4e` | **`strlen_`** | library match |
| `FUN_00460d12` | **`memmove_`** | library match |
| `FUN_00460d5f` | **`printf_`** | library match |
| `exit_` (`0x460d82`) | **`exit_`** — `STRM_Peek` calls it when even the slack area is too small | runtime, identified by behaviour |

`FUN_00454feb` is the important correction. It is Watcom's stack probe, called
at entry with the frame size in EAX — **not** a "get stream context" accessor.
The `CryoStream` layout below was reconstructed on the assumption that it
returned a context pointer, so **treat that struct as unverified**: the field
offsets came from a decompilation made under the wrong calling convention.
`FUN_00460d5f` being `printf_` likewise kills the "refill from file" reading.

### Stream context layout

Recovered from the field offsets in `STRM_Peek` (`0x4154db`) and `STRM_Commit` (`0x4155a2`):

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

The stream is built once, by `STRM_Create` (`0x415201`): `GAME_Init` (`0x4156bf`)
calls it as `0x5df47c = STRM_Create(0x57800, 0x57800, 0x8000)`. The 0x2c-byte
descriptor it allocates matches the struct `STRM_Open` uses (`+0x14` size,
`+0x10` set equal to `+0x14`, cursors `+0x4`/`+0x8`/`+0xc` zeroed, `+0x20`
alignment, `+0x24` open flag). `STRM_Open` closes the previous file, opens the
new one with `_uopen_`, resets the positions and allocates twice the buffer size
from the MEM stack at `0x5df484`.

## `DSN_LoadHeader` (`0x4175bc`) reconstructed

**Re-decompiled under `__watcall`.** The peek lengths were invisible before —
they travel in EDX — and they are what pins the body offset:

```c
int DSN_LoadHeader(void)
{
    __CHK(frame);                           // FUN_00454feb — stack probe
    /* ... open + path/string build ... */
    if (!open(g_file, path)) return 0;      // STRM_Open

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
`EDX` — which is why `STRM_Peek` appears to take no arguments while
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

Re-apply the cspec patch after any Ghidra upgrade — it patches a file inside the
Ghidra install, which an upgrade replaces.

Pairs with `src/dreams/watcom.py`, which recovers Watcom 10.6 runtime symbol
names from the stock OMF libraries — see [toolchain.md](toolchain.md). Between a
correct cspec and named runtime functions, only Cryo's own code stays anonymous.

## The Body Unpackers and Call Hierarchy **[verified]**

The body unpacking and asset consumption pipeline has been fully decompiled and identified:

1. **`DSN_LoadMaterialsAndFaces` (`0x4177f5`)**:
   Verifies tag `\x01`, reads the packed length $L$, seeks past chunk padding, calls `LZ_Unpack` (`0x49afd1`), Cryo's LZ77 decompressor (hand-written asm, stack arguments), to unpack the LZ stream into the arena directory, materials table, scene-graph nodes, and face records.
2. **`DSN_LoadVertexPool` (`0x4178fe`)**:
   Verifies tag `\x02`, reads length $L$, and unpacks the LZ stream via `LZ_Unpack` into the shared vertex pool at `+0x30`.
3. **`DSN_LoadTextures` (`0x417afd`)**:
   Called progressively (32 iterations) by `SCENE_LoadLevel` (`0x41f9db`) and the per-frame `GAME_Tick` (`0x4240ba`), so the load is spread over frames:
   - On iteration 0: verifies tag `\x03` (palette), reading $B \times 1024$ bytes into per-object texture palettes at `offset + 0x3C00`.
   - On iterations $0 \dots 31$: reads pairs of tag `\x04` records (64 progressive planes total), feeding them to `DSN_BlitTileToPage` (`0x417d78`) to interleave into the $256 \times 256$ RGB surfaces.
4. **`RES_ReadFile` (`0x41c666`)**:
   The file reader behind `RES_Load` (`0x456e24`). Routes by extension: `DSN_LoadMaterialsAndFaces` (`0x4177f5`) (`.3DC`), `DSN_LoadVertexPool` (`0x4178fe`) (`.3DI`), `DSN_Create3DM` (`0x417a07`) (`.3DM`) and `DAN_Load3DA` (`0x4105eb`) (`.3DA`); anything else is read as a raw body. In DSN mode `DSN_Create3DM` builds the `.3DM` texture in memory rather than reading a file: it copies the 20-byte header template from the DSN table, zeroes a 0x8000 palette block and a 0x10000 page, and `DSN_LoadTextures` fills it later.
5. **`ENT_LoadModel` (`0x41da7d`)**:
   Loads one actor's model (DSN, DAN, `.3DC`, `.3DI`, anim set) for `ENT_InstantiateFromObjet`; it is not a scene-wide loader. Caller of `DSN_LoadHeader` (`0x4175bc`). Dispatches model geometry loading, then invokes `ANIM_LoadEntitySet` (`0x404d98`) / `ANIM_RequestState` (`0x405118`) / `ANIM_ApplyPendingState` (`0x4058d5`) for entity initialization if flag bit 1 is set.
6. **`ENT_InstantiateFromObjet` (`0x41deb8`)**:
   Converts an `OBJET` record from `DREAMS.DAT` into a live in-game entity struct.
7. **`SCENE_LoadLevel` (`0x41f9db`)**:
   The level loader, run when the pending-load flag is set: `CD_PrepareLevel` (`0x427d64`) and the loading screen, DSN textures via `DSN_LoadTextures`, ambient and directional lights, fog, clear color, then a loop through `OBJET1` .. `OBJET15` to spawn active entities. Spawning is one part of the level load.
8. **`DBG_DrawObjectInfo` (`0x416606`)**:
   The developer debug HUD overlay that renders entity fields with exact internal labels (`Object Pos`, `Object Speed`, `Object PHY Speed`, `Object Flags`, `Object Angle`, `Object 3D Col`, `Object Anim 0`, `Object Anim 1`, `Nombre d'objet`).
