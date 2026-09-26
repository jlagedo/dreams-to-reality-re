"""Find functions shipped byte-for-byte in two PE images.

Used to prove that code from CryoLib (CRYO.DLL, MSVC) sits unchanged in the
game (WINDREAM.EXE, Watcom): hand-written assembly survives any compiler, so an
identical body means the same object code was linked into both.

A pair (A function, B function) is accepted only if, over the whole body:

  * the lengths are equal (Ghidra boundaries on both sides);
  * every byte is equal, except
      - 4-byte absolute addresses that the base-relocation table of *each*
        image lists at the same offset (relocation sets must be identical), and
      - rel32 operands of E8/E9/0F 8x in both images;
  * every such address maps consistently: one A address never maps to two B
    addresses (checked across all accepted pairs), and a direct call from an
    accepted pair lands on a function entry in both images.

Pairs that fail only the global consistency check are reported, not accepted.
Near misses (same leading bytes, different body) are listed separately; they
are candidates for a modified copy, never for a name transfer.

Needs the feature dumps from ExportFunctionFeatures.java for both programs.

  uv run python tools/match_identical.py CRYO.DLL WINDREAM.EXE [--names re/symbols/cryo.dll.tsv]

Writes out/ghidra/match/<A>--<B>.identical.tsv.
"""

from __future__ import annotations

import argparse
import difflib
import json
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FEATURES = ROOT / "out" / "ghidra" / "features"
OUT = ROOT / "out" / "ghidra" / "match"
sys.path.insert(0, str(ROOT / "src"))


class PE:
    def __init__(self, path: Path):
        self.b = path.read_bytes()
        b = self.b
        e = struct.unpack_from("<I", b, 0x3C)[0]
        nsec = struct.unpack_from("<H", b, e + 6)[0]
        opt = e + 24
        self.base = struct.unpack_from("<I", b, opt + 28)[0]
        self.secs = []
        for i in range(nsec):
            s = opt + struct.unpack_from("<H", b, e + 20)[0] + 40 * i
            vs, va, rs, ra = struct.unpack_from("<IIII", b, s + 8)
            self.secs.append((va, max(vs, rs), ra, rs))
        rel_rva, rel_sz = struct.unpack_from("<II", b, opt + 96 + 5 * 8)
        self.relocs: set[int] = set()
        o = self.rva2off(rel_rva)
        end = o + rel_sz
        while o is not None and o < end:
            page, size = struct.unpack_from("<II", b, o)
            if size < 8:
                break
            for k in range(8, size, 2):
                ent = struct.unpack_from("<H", b, o + k)[0]
                if ent >> 12 == 3:  # IMAGE_REL_BASED_HIGHLOW
                    self.relocs.add(self.base + page + (ent & 0xFFF))
            o += size

    def rva2off(self, rva: int) -> int | None:
        for va, vsz, ra, rs in self.secs:
            if va <= rva < va + vsz and rva - va < rs:
                return rva - va + ra
        return None

    def read(self, va: int, n: int) -> bytes | None:
        o = self.rva2off(va - self.base)
        if o is None:
            return None
        return self.b[o : o + n]

    def u32(self, va: int) -> int:
        return struct.unpack_from("<I", self.read(va, 4))[0]

    def jmp_target(self, va: int) -> int | None:
        """Target of a `jmp rel32` at va (MSVC incremental-link thunks)."""
        b = self.read(va, 5)
        if b and b[0] == 0xE9:
            return (va + 5 + struct.unpack_from("<i", b, 1)[0]) & 0xFFFFFFFF
        return None

    def exports(self) -> dict[int, str]:
        b = self.b
        e = struct.unpack_from("<I", b, 0x3C)[0]
        rva = struct.unpack_from("<I", b, e + 24 + 96)[0]
        if not rva:
            return {}
        o = self.rva2off(rva)
        _, nn, af, np_, no = struct.unpack_from("<IIIII", b, o + 20)
        out = {}
        for i in range(nn):
            name_rva = struct.unpack_from("<I", b, self.rva2off(np_) + 4 * i)[0]
            name = b[self.rva2off(name_rva) :].split(b"\0")[0].decode()
            ordinal = struct.unpack_from("<H", b, self.rva2off(no) + 2 * i)[0]
            out[self.base + struct.unpack_from("<I", b, self.rva2off(af) + 4 * ordinal)[0]] = name
        return out


def rel32_operands(code: bytes) -> set[int]:
    """Offsets of plausible rel32 operands (after E8, E9, 0F 80..8F)."""
    out = set()
    for i, c in enumerate(code):
        if c in (0xE8, 0xE9) and i + 5 <= len(code):
            out.add(i + 1)
        elif c == 0x0F and i + 6 <= len(code) and 0x80 <= code[i + 1] <= 0x8F:
            out.add(i + 2)
    return out


def compare(a: PE, fa: dict, b: PE, fb: dict):
    """Return (ok, reason, address pairs) for one candidate pair."""
    ea, eb, n = int(fa["entry"], 16), int(fb["entry"], 16), fa["size"]
    if fb["size"] != n:
        return False, "size", []
    ca, cb = a.read(ea, n), b.read(eb, n)
    if not ca or not cb or len(ca) != n or len(cb) != n:
        return False, "read", []
    ra = {v - ea for v in a.relocs if ea <= v < ea + n}
    rb = {v - eb for v in b.relocs if eb <= v < eb + n}
    if ra != rb:
        return False, "relocs", []
    pairs = []
    explained = set()
    for k in sorted(ra):
        pairs.append(
            ("abs", struct.unpack_from("<I", ca, k)[0], struct.unpack_from("<I", cb, k)[0])
        )
        explained.update(range(k, k + 4))
    rel = rel32_operands(ca) & rel32_operands(cb)
    for i in range(n):
        if ca[i] == cb[i] or i in explained:
            continue
        k = next(
            (k for k in rel if k <= i < k + 4 and not explained.intersection(range(k, k + 4))), None
        )
        if k is None:
            return False, f"byte +{i:#x}", []
        ta = (ea + k + 4 + struct.unpack_from("<i", ca, k)[0]) & 0xFFFFFFFF
        tb = (eb + k + 4 + struct.unpack_from("<i", cb, k)[0]) & 0xFFFFFFFF
        if not (ea <= ta < ea + n and ta - ea == tb - eb):
            pairs.append(("rel", ta, tb))
        explained.update(range(k, k + 4))
    return True, "identical", pairs


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("a", help="program with the names (e.g. CRYO.DLL)")
    ap.add_argument("b", help="program to check (e.g. WINDREAM.EXE)")
    ap.add_argument("--a-file", help="path to A's image (default: disc 2 DEMOS2\\CRYO.DLL)")
    ap.add_argument("--b-file", help="path to B's image (default: disc 1)")
    ap.add_argument("--min-size", type=int, default=16, help="smallest body considered (bytes)")
    ap.add_argument(
        "--insn", action="store_true", help="also compare near misses instruction by instruction"
    )
    args = ap.parse_args()

    from dreams import paths

    disc1, disc2 = paths.get("disc1"), paths.get("disc2")
    a_file = Path(args.a_file) if args.a_file else Path(disc2) / "DEMOS2" / args.a
    b_file = Path(args.b_file) if args.b_file else Path(disc1) / args.b
    a, b = PE(a_file), PE(b_file)
    fa = json.loads((FEATURES / f"{args.a}.json").read_text(encoding="utf-8"))
    fb = json.loads((FEATURES / f"{args.b}.json").read_text(encoding="utf-8"))
    entries_a = {int(f["entry"], 16): f for f in fa}
    entries_b = {int(f["entry"], 16): f for f in fb}

    # Name each A body: exports resolve through the incremental-link jmp thunks.
    names = {}
    for va, name in a.exports().items():
        names[a.jmp_target(va) or va] = name
    for f in fa:
        names.setdefault(int(f["entry"], 16), f["name"])

    def resolve(va: int) -> int:
        t = a.jmp_target(va)
        return t if t is not None and va not in entries_a else va

    # Candidates: B functions whose first 16 bytes equal A's, ignoring relocs.
    def key(pe: PE, entry: int, size: int) -> bytes | None:
        code = pe.read(entry, min(size, 16))
        if not code:
            return None
        code = bytearray(code)
        for k in range(len(code)):
            if any(entry + k - j in pe.relocs for j in range(4)):
                code[k] = 0
        for k in rel32_operands(bytes(code)):
            code[k : k + 4] = b"\0\0\0\0"
        return bytes(code)

    by_key = defaultdict(list)
    for f in fb:
        if f["size"] >= args.min_size:
            by_key[key(b, int(f["entry"], 16), f["size"])].append(f)

    accepted, near, ambiguous = [], [], []
    for f in fa:
        if f["size"] < args.min_size:
            continue
        cands = by_key.get(key(a, int(f["entry"], 16), f["size"]), [])
        hits = []
        for g in cands:
            ok, why, pairs = compare(a, f, b, g)
            if ok:
                hits.append((g, pairs))
            else:
                near.append((f, g, why))
        if len(hits) == 1:
            accepted.append((f, *hits[0]))
        elif hits:
            ambiguous.append((f, hits))

    # Short bodies can be identical in several places. Keep the one candidate
    # whose calls land on functions already paired, and repeat until stable.
    changed = True
    while changed and ambiguous:
        changed = False
        matched = {int(f["entry"], 16): int(g["entry"], 16) for f, g, _ in accepted}
        for item in list(ambiguous):
            f, hits = item
            fits = []
            for g, pairs in hits:
                known = [
                    (resolve(x), y)
                    for kind, x, y in pairs
                    if kind == "rel" and resolve(x) in matched
                ]
                if known and all(matched[x] == y for x, y in known):
                    fits.append((g, pairs))
            if len(fits) == 1:
                accepted.append((f, *fits[0]))
                ambiguous.remove(item)
                changed = True
    for f, hits in ambiguous:
        near.append((f, None, f"ambiguous: {len(hits)} identical bodies"))

    # Global consistency of every address the bodies carry.
    amap: dict[int, set[int]] = defaultdict(set)
    for _, _, pairs in accepted:
        for _, x, y in pairs:
            amap[x].add(y)
    matched = {int(f["entry"], 16): int(g["entry"], 16) for f, g, _ in accepted}
    rows = []
    for f, g, pairs in accepted:
        problems = []
        for kind, x, y in pairs:
            if len(amap[x]) > 1:
                problems.append(f"{x:#x}->{len(amap[x])} targets")
            if kind == "rel":
                xa = resolve(x)
                if xa in entries_a and y not in entries_b:
                    problems.append(f"call {x:#x} lands mid-function in B")
                if xa in matched and matched[xa] != y:
                    problems.append(f"call {x:#x} pairs elsewhere")
        rows.append(
            (f, g, "VERIFIED" if not problems else "INCONSISTENT", "; ".join(problems), len(pairs))
        )

    # Optional second tier: same instruction sequence, different encoding or
    # addresses. Needs capstone (uv run --with capstone ...).
    if args.insn:
        from capstone import CS_ARCH_X86, CS_MODE_32, Cs

        md = Cs(CS_ARCH_X86, CS_MODE_32)

        def insns(pe: PE, f: dict) -> list[str]:
            entry = int(f["entry"], 16)
            code = pe.read(entry, f["size"]) or b""
            return [
                re.sub(r"0x[0-9a-f]{5,}", "A", f"{i.mnemonic} {i.op_str}")
                for i in md.disasm(code, entry)
            ]

        done = {int(g["entry"], 16) for _, g, *_ in rows}
        seen = set()
        for f, g, why in near:
            if g is None or (f["entry"], g["entry"]) in seen or int(g["entry"], 16) in done:
                continue
            seen.add((f["entry"], g["entry"]))
            ia, ib = insns(a, f), insns(b, g)
            ops = difflib.SequenceMatcher(None, ia, ib, autojunk=False).get_opcodes()
            diff = sum(max(i2 - i1, j2 - j1) for t, i1, i2, j1, j2 in ops if t != "equal")
            if diff == 0:
                rows.append((f, g, "INSN-IDENTICAL", f"bytes differ: {why}", 0))
            elif diff <= max(len(ia), len(ib)) // 4:
                rows.append(
                    (f, g, "MODIFIED", f"{diff} of {len(ia)}/{len(ib)} instructions differ", 0)
                )

    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"{args.a}--{args.b}.identical.tsv"
    header = [
        "status",
        "a_entry",
        "b_entry",
        "size_a",
        "size_b",
        "a_name",
        "b_name",
        "addresses",
        "note",
        "a_callers",
    ]
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("\t".join(header) + "\n")
        for f, g, status, note, naddr in sorted(rows, key=lambda r: int(r[1]["entry"], 16)):
            ea = int(f["entry"], 16)
            an = names.get(ea, f["name"])
            callers = sorted(
                {
                    names.get(int(h["entry"], 16), h["name"])
                    for h in fa
                    for c in h["calls"]
                    if ":" not in c and resolve(int(c, 16)) == ea
                }
            )
            cols = [
                status,
                f["entry"],
                g["entry"],
                f["size"],
                g["size"],
                an,
                g["name"],
                naddr,
                note,
                ", ".join(callers),
            ]
            fh.write("\t".join(map(str, cols)) + "\n")
            print(
                f"{status:14s} {f['entry']} -> {g['entry']} {f['size']:5d}/{g['size']:<5d} "
                f"{an:24s} {note}  callers: {', '.join(callers[:3])}"
            )
    counts = defaultdict(int)
    for r in rows:
        counts[r[2]] += 1
    print(", ".join(f"{v} {k}" for k, v in sorted(counts.items())), "->", path)


if __name__ == "__main__":
    main()
