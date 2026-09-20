"""Watcom OMF library parsing, used to recover runtime symbol names.

All four game executables link the runtime of Watcom C/C++ 10.6 (see
``docs/toolchain.md`` for how that version was pinned down). Watcom's libraries
are OMF, and OMF keeps ``PUBDEF`` records — real function names bound to the
exact code bytes the linker copies into the image. The stock 10.6 libraries are
therefore a symbol source: match library code against ``DREAMS.EXE`` and every
hit is a named C runtime routine that nobody needs to reverse engineer, leaving
only Cryo's own code unlabelled.

Relocated fields differ between the library and the linked image — a ``call``
displacement is a placeholder in the ``.LIB`` and a real delta in the ``.EXE``.
Signatures therefore carry a mask derived from the ``FIXUPP`` records:
1 = the byte must match, 0 = wildcard.

Reference libraries live outside the repo, under ``paths.get("watcom")``.
"""

from __future__ import annotations

import struct
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

# OMF record types. The odd variant of a pair is the 32-bit form, which widens
# offsets and displacements from 2 bytes to 4.
THEADR, LHEADR = 0x80, 0x82
MODEND = (0x8A, 0x8B)
PUBDEF = (0x90, 0x91)
LNAMES = 0x96
SEGDEF = (0x98, 0x99)
FIXUPP = (0x9C, 0x9D)
LEDATA = (0xA0, 0xA1)
LIB_HEADER, LIB_END = 0xF0, 0xF1

# Bytes patched by each FIXUPP location type; anything else we treat as 0.
LOC_SIZE = {0: 1, 1: 2, 2: 2, 3: 4, 4: 1, 5: 2, 9: 4, 11: 6, 13: 4}

# A signature needs enough substance to be worth trusting.
MIN_LENGTH = 16
MIN_FIXED = 10
MAX_LENGTH = 192  # how far past the symbol a pattern is allowed to run
CORE = 32  # prefix that must match for a hit, as in FLIRT
ANCHOR = 4  # consecutive fixed bytes used as the hash key when scanning


def _index(buf: bytes, i: int) -> tuple[int, int]:
    """Read an OMF variable-length index. Returns (value, next position)."""
    v = buf[i]
    if v & 0x80:
        return ((v & 0x7F) << 8) | buf[i + 1], i + 2
    return v, i + 1


@dataclass
class Segment:
    length: int
    class_name: str = ""
    data: bytearray = field(default_factory=bytearray)
    # Per byte: known = written by a LEDATA record, fixed = not relocated.
    known: bytearray = field(default_factory=bytearray)
    fixed: bytearray = field(default_factory=bytearray)

    def __post_init__(self) -> None:
        if not self.data:
            self.data = bytearray(self.length)
            self.known = bytearray(self.length)
            self.fixed = bytearray(b"\x01" * self.length)


@dataclass
class Module:
    name: str
    lnames: list[str] = field(default_factory=list)
    segments: dict[int, Segment] = field(default_factory=dict)
    publics: list[tuple[str, int, int]] = field(default_factory=list)  # name, seg, offset


@dataclass
class Signature:
    name: str
    module: str
    data: bytes
    mask: bytes
    anchor: int  # offset within data of the ANCHOR-byte hash key
    core: int  # prefix length a hit must agree on

    @property
    def exact(self) -> bool:
        return all(self.mask)

    @property
    def strength(self) -> int:
        return sum(self.mask)


def _parse_module(buf: bytes, pos: int) -> tuple[Module, int]:
    """Parse one OMF module starting at ``pos``. Returns (module, end position)."""
    mod = Module(name="")
    seg_order = 0
    ledata: tuple[int, int] | None = None  # (segment index, offset of the record's data)

    while pos + 3 <= len(buf):
        rec = buf[pos]
        length = struct.unpack_from("<H", buf, pos + 1)[0]
        body = buf[pos + 3 : pos + 3 + length - 1]  # drop the trailing checksum
        wide = bool(rec & 1)
        pos += length + 3

        if rec in (THEADR, LHEADR):
            mod.name = body[1 : 1 + body[0]].decode("latin-1")
        elif rec == LNAMES:
            i = 0
            while i < len(body):
                n = body[i]
                mod.lnames.append(body[i + 1 : i + 1 + n].decode("latin-1"))
                i += n + 1
        elif rec in SEGDEF:
            seg_order += 1
            i = 0
            acbp = body[i]
            i += 1
            if acbp >> 5 == 0:  # absolute segment: frame number + offset follow
                i += 3
            size = struct.unpack_from("<I" if wide else "<H", body, i)[0]
            i += 4 if wide else 2
            if acbp & 0x02:  # "big" bit: the segment is exactly 64K / 4G
                size = 0x10000 if not wide else size
            _, i = _index(body, i)  # segment name
            cls, i = _index(body, i)  # class name -- "CODE", "DATA", "BSS", ...
            name = mod.lnames[cls - 1] if 0 < cls <= len(mod.lnames) else ""
            mod.segments[seg_order] = Segment(length=size, class_name=name)
        elif rec in LEDATA:
            i = 0
            seg, i = _index(body, i)
            off = struct.unpack_from("<I" if wide else "<H", body, i)[0]
            i += 4 if wide else 2
            payload = body[i:]
            target = mod.segments.get(seg)
            if target is not None and off + len(payload) <= target.length:
                target.data[off : off + len(payload)] = payload
                target.known[off : off + len(payload)] = b"\x01" * len(payload)
                ledata = (seg, off)
        elif rec in FIXUPP and ledata is not None:
            _apply_fixups(body, wide, mod.segments.get(ledata[0]), ledata[1])
        elif rec in PUBDEF:
            i = 0
            _, i = _index(body, i)  # base group
            seg, i = _index(body, i)
            if seg == 0:
                i += 2  # base frame
            step = 4 if wide else 2
            while i < len(body):
                nlen = body[i]
                i += 1
                if i + nlen + step > len(body):
                    break  # truncated trailing entry; GRAPH.LIB has these
                name = body[i : i + nlen].decode("latin-1")
                i += nlen
                off = struct.unpack_from("<I" if wide else "<H", body, i)[0]
                i += step
                _, i = _index(body, i)  # type index
                mod.publics.append((name, seg, off))
        elif rec in MODEND:
            break

    return mod, pos


def _apply_fixups(body: bytes, wide: bool, seg: Segment | None, base: int) -> None:
    """Mark every byte patched by this FIXUPP record as a wildcard."""
    i = 0
    while i < len(body):
        first = body[i]
        if first & 0x80:  # FIXUP subrecord
            loc = (first >> 2) & 0x0F
            rec_off = ((first & 0x03) << 8) | body[i + 1]
            i += 2
            fixdat = body[i]
            i += 1
            frame_method = (fixdat >> 4) & 0x07
            if not fixdat & 0x80:  # frame given explicitly, not by thread
                if frame_method <= 2:
                    _, i = _index(body, i)
                elif frame_method == 3:
                    i += 2
            if not fixdat & 0x08:  # target given explicitly
                _, i = _index(body, i)
            if not fixdat & 0x04:  # P=0: a target displacement follows
                i += 4 if wide else 2
            size = LOC_SIZE.get(loc, 0)
            if seg is not None and size:
                start = base + rec_off
                seg.fixed[start : start + size] = b"\x00" * size
        else:  # THREAD subrecord
            method = (first >> 2) & 0x07
            i += 1
            if method <= 2:
                _, i = _index(body, i)


def read_library(path: str | Path) -> list[Module]:
    """Parse an OMF ``.LIB`` from disk."""
    return read_library_bytes(Path(path).read_bytes(), label=str(path))


def read_library_bytes(buf: bytes, label: str = "<bytes>") -> list[Module]:
    """Parse an OMF library. Modules are page-aligned; the dictionary ends them."""
    if not buf or buf[0] != LIB_HEADER:
        raise ValueError(f"{label}: not an OMF library")
    page = struct.unpack_from("<H", buf, 1)[0] + 3

    mods: list[Module] = []
    pos = page
    while pos + 3 <= len(buf) and buf[pos] != LIB_END:
        mod, end = _parse_module(buf, pos)
        mods.append(mod)
        nxt = -(-end // page) * page
        if nxt <= pos:
            break
        pos = nxt
    return mods


def signatures(modules: list[Module]) -> list[Signature]:
    """Turn public symbols into masked byte patterns.

    A symbol's extent runs to the next symbol in the same segment, so the
    patterns stop at a function boundary rather than running into the neighbour.
    """
    out: list[Signature] = []
    for mod in modules:
        by_seg: dict[int, list[tuple[str, int]]] = defaultdict(list)
        for name, seg, off in mod.publics:
            by_seg[seg].append((name, off))

        for seg_idx, syms in by_seg.items():
            seg = mod.segments.get(seg_idx)
            # Data symbols are useless here and actively harmful: zero-filled
            # tables such as __IsKTable match every run of nulls in the image.
            if seg is None or "CODE" not in seg.class_name.upper():
                continue
            syms.sort(key=lambda s: s[1])
            bounds = [o for _, o in syms] + [seg.length]
            for n, (name, off) in enumerate(syms):
                end = min(bounds[n + 1], off + MAX_LENGTH, seg.length)
                while end > off and not seg.known[end - 1]:
                    end -= 1
                if end - off < MIN_LENGTH or not seg.known[off]:
                    continue
                mask = bytes(
                    seg.known[off:end][i] & seg.fixed[off:end][i] for i in range(end - off)
                )
                if sum(mask) < MIN_FIXED:
                    continue
                anchor = _find_anchor(mask)
                if anchor is None:
                    continue
                out.append(
                    Signature(
                        name=name,
                        module=mod.name,
                        data=bytes(seg.data[off:end]),
                        mask=mask,
                        anchor=anchor,
                        core=_core_length(mask, anchor),
                    )
                )
    return out


def _find_anchor(mask: bytes) -> int | None:
    run = 0
    for i, m in enumerate(mask):
        run = run + 1 if m else 0
        if run == ANCHOR:
            return i - ANCHOR + 1
    return None


def _core_length(mask: bytes, anchor: int) -> int:
    """Shortest prefix that is both CORE bytes long and carries MIN_FIXED fixed bytes.

    Length alone is not enough. Some C++ constructors open with nothing but
    relocated stores -- ``W?$ct:streambuf$n()_`` has 32 leading wildcard bytes,
    so a fixed-length prefix check accepts every offset in the file and the
    scan degenerates into ~149k false hits.
    """
    seen = 0
    for i, m in enumerate(mask):
        seen += bool(m)
        if i + 1 >= CORE and seen >= MIN_FIXED and i + 1 >= anchor + ANCHOR:
            return i + 1
    return len(mask)


@dataclass
class Match:
    offset: int
    names: list[str]
    module: str
    length: int
    strength: int
    full: bool  # the whole extent matched, not just the CORE-byte prefix


def _agrees(sig: Signature, blob: bytes, pos: int, upto: int) -> bool:
    end = min(upto, len(sig.data))
    if pos < 0 or pos + end > len(blob):
        return False
    window = blob[pos : pos + end]
    if sig.exact:
        return window == sig.data[:end]
    return all(
        not m or a == b for a, m, b in zip(sig.data[:end], sig.mask[:end], window, strict=True)
    )


def match(sigs: list[Signature], blob: bytes) -> list[Match]:
    """Scan ``blob`` for every signature. Offsets are positions in ``blob``.

    A hit needs the signature's core prefix to agree. Whether the rest of the
    extent also agrees is reported but not required: the tail can legitimately
    diverge where the linker drops trailing padding or the next symbol in the
    segment was pulled from a different module.
    """
    index: dict[bytes, list[Signature]] = defaultdict(list)
    for sig in sigs:
        index[sig.data[sig.anchor : sig.anchor + ANCHOR]].append(sig)

    hits: dict[int, list[Signature]] = defaultdict(list)
    for i in range(len(blob) - ANCHOR + 1):
        bucket = index.get(blob[i : i + ANCHOR])
        if not bucket:
            continue
        for sig in bucket:
            pos = i - sig.anchor
            if _agrees(sig, blob, pos, sig.core):
                hits[pos].append(sig)

    out = []
    for off, found in sorted(hits.items()):
        best = max(found, key=lambda s: s.strength)
        out.append(
            Match(
                offset=off,
                names=sorted({s.name for s in found}),
                module=best.module,
                length=len(best.data),
                strength=best.strength,
                full=_agrees(best, blob, off, len(best.data)),
            )
        )
    return out


def pe_offset_to_va(path: str | Path):
    """Return a file-offset -> virtual-address mapper for a PE, or None."""
    buf = Path(path).read_bytes()
    if buf[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", buf, 0x3C)[0]
    if buf[pe : pe + 4] != b"PE\0\0":
        return None
    nsec = struct.unpack_from("<H", buf, pe + 6)[0]
    opt_size = struct.unpack_from("<H", buf, pe + 20)[0]
    base = struct.unpack_from("<I", buf, pe + 24 + 28)[0]
    sections = []
    for i in range(nsec):
        s = pe + 24 + opt_size + i * 40
        vsize, rva, rawsz, raw = struct.unpack_from("<IIII", buf, s + 8)
        sections.append((raw, rawsz or vsize, rva))

    def mapper(off: int) -> int | None:
        for raw, size, rva in sections:
            if raw <= off < raw + size:
                return base + rva + (off - raw)
        return None

    return mapper


def to_csv(matches: list[Match], mapper=None) -> str:
    rows = ["file_offset,virtual_address,name,module,length,fixed_bytes,extent"]
    for m in matches:
        va = mapper(m.offset) if mapper else None
        rows.append(
            f"0x{m.offset:x},{f'0x{va:x}' if va else ''},"
            f"{'|'.join(m.names)},{m.module},{m.length},{m.strength},"
            f"{'full' if m.full else 'core'}"
        )
    return "\n".join(rows) + "\n"


def to_ida_idc(matches: list[Match], mapper) -> str:
    """An IDC script that names every match. Works in IDA Free."""
    lines = [
        "// Watcom C/C++ 10.6 runtime symbols recovered from the stock libraries.",
        "// Generated by dreams.watcom -- see docs/toolchain.md.",
        "#include <idc.idc>",
        "static main(void) {",
    ]
    for m in matches:
        va = mapper(m.offset)
        if va is None:
            continue
        name = m.names[0].lstrip("_") or m.names[0]
        lines.append(f'    set_name(0x{va:X}, "wat_{name}", SN_NOCHECK|SN_FORCE);')
    lines += [f'    Message("dreams: applied {len(matches)} Watcom symbols\\n");', "}"]
    return "\n".join(lines) + "\n"


def to_ghidra_py(matches: list[Match], mapper) -> str:
    lines = [
        "# Watcom C/C++ 10.6 runtime symbols recovered from the stock libraries.",
        "# Generated by dreams.watcom -- run with Ghidra's Script Manager.",
        "# @category Dreams",
        "syms = [",
    ]
    for m in matches:
        va = mapper(m.offset)
        if va is None:
            continue
        lines.append(f'    (0x{va:X}, "wat_{m.names[0].lstrip("_") or m.names[0]}"),')
    lines += [
        "]",
        "fm = currentProgram.getFunctionManager()",
        "for addr, name in syms:",
        "    a = toAddr(addr)",
        "    createLabel(a, name, True)",
        "    f = fm.getFunctionContaining(a)",
        "    if f is not None and f.getEntryPoint() == a:",
        "        f.setName(name, ghidra.program.model.symbol.SourceType.IMPORTED)",
        'print("dreams: applied %d Watcom symbols" % len(syms))',
    ]
    return "\n".join(lines) + "\n"


# Which Watcom 10.6 libraries each executable draws on, relative to the CD's
# LIB386 tree. Both Windows builds are the same program (docs/engine.md).
#
# PLIB3R.LIB (the C++ runtime) is deliberately absent: its only hits are
# repeated core-only matches on heavily relocated iostream constructors, and no
# Watcom C++ error string appears in any binary. See docs/toolchain.md.
WIN_LIBS = ["NT/CLIB3R.LIB", "MATH387R.LIB", "MATH3R.LIB"]
DOS_LIBS = ["DOS/CLIB3R.LIB", "MATH387R.LIB", "MATH3R.LIB", "DOS/GRAPH.LIB", "DOS/EMU387.LIB"]

TARGETS = {
    "WINDREAM.EXE": WIN_LIBS,
    "GDIDREAM.EXE": WIN_LIBS,
    "DREAMS.EXE": DOS_LIBS,
    "DREAMSFX.EXE": DOS_LIBS,
}


def main(argv: list[str] | None = None) -> int:
    """Generate signature output for the four game executables."""
    import argparse

    from . import paths

    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", help="single executable to scan (default: all four)")
    ap.add_argument("--lib", action="append", help="OMF library to match, repeatable")
    ap.add_argument("--out", help="output directory (default: <watcom>/sigs)")
    args = ap.parse_args(argv)

    lib_root = paths.get("watcom") / "10.6-cd" / "LIB386"
    out = Path(args.out) if args.out else paths.get("watcom") / "sigs"
    out.mkdir(parents=True, exist_ok=True)

    if args.exe:
        jobs = [(Path(args.exe), [Path(p) for p in args.lib or []])]
    else:
        jobs = [(paths.disc(1) / n, [lib_root / r for r in rel]) for n, rel in TARGETS.items()]

    cache: dict[Path, list[Signature]] = {}
    for exe, libs in jobs:
        sigs = []
        for lib in libs:
            if lib not in cache:
                cache[lib] = signatures(read_library(lib))
            sigs += cache[lib]
        blob = exe.read_bytes()
        found = match(sigs, blob)
        mapper = pe_offset_to_va(exe)
        stem = exe.stem.lower()

        (out / f"{stem}.csv").write_text(to_csv(found, mapper), encoding="utf-8")
        if mapper:
            (out / f"{stem}.idc").write_text(to_ida_idc(found, mapper), encoding="utf-8")
            (out / f"{stem}_ghidra.py").write_text(to_ghidra_py(found, mapper), encoding="utf-8")

        full = sum(1 for m in found if m.full)
        print(
            f"{exe.name:<14} {len(libs)} libs, {len(sigs):4d} signatures -> "
            f"{len(found):3d} matches ({full} full extent, {len(found) - full} core only)"
        )
    print(f"written to {out}")
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
