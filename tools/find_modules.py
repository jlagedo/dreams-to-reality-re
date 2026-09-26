"""Recover source-file (translation unit) blocks in a Watcom PE build.

The file names are gone, but Watcom's layout rules keep each .c file's pieces
together (checked against the Open Watcom 1.0 source, cc/c/cgen2.c, csym.c,
cinfo.c):

  * code: functions in source order; each file's code is one contiguous run;
  * CONST: string literals and constants, emitted per file in first-use order;
  * _DATA: initialised data, per file, in definition order;
  * _BSS: uninitialised data, globals included (no COMDEFs), per file, in
    name-hash order;
  * every segment holds the files in link order, then the library modules.

So two functions are in the same file when evidence ties them:

  C      a literal/constant they share, or one out of order between them
         (constants are never shared across files: 0 false joins);
  shared a data or bss item used only by functions within --window of the cut
         and on both sides of it;
  calls  a function called only from <= 2 functions within --window of it.

A cut with no evidence is a candidate boundary, not a proven one. Proven
boundaries come from a second build (--cross): the same file keeps its
function order in every build, so where confident twins appear in reversed
order the builds must have linked different files between them. Weak matches
are ignored; one reversed pair of solid matches is proof.

Calibration on WINDREAM.EXE against 17 proven boundaries: C + shared joins
none of them; adding calls joins 1.

  uv run python tools/find_modules.py WINDREAM.EXE --cross DREAMSFX.EXE

Writes out/ghidra/modules/<program>.tsv (one row per block).
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FEATURES = ROOT / "out" / "ghidra" / "features"
MATCH = ROOT / "out" / "ghidra" / "match"
OUT = ROOT / "out" / "ghidra" / "modules"
sys.path.insert(0, str(ROOT / "src"))
sys.path.insert(0, str(ROOT / "tools"))

from match_identical import PE  # noqa: E402


def library_start(program: str, entries: list[int]) -> int:
    """First Watcom runtime function; the game's own files all precede it."""
    from dreams import paths

    sigs = Path(paths.get("watcom")) / "sigs" / f"{Path(program).stem.lower()}.csv"
    with sigs.open(encoding="utf-8") as fh:
        lib = [int(r["virtual_address"], 16) for r in csv.DictReader(fh)]
    return min(a for a in lib if a in set(entries))


def data_regions(pe: PE, refs: list[set[int]], lib_refs: set[int], code_end: int):
    """Game parts of each data segment: runs of game-only items between library runs."""
    game = {x for r in refs for x in r if x >= code_end}
    library = {x for x in lib_refs - game if x >= code_end}
    tagged = sorted([(x, "G") for x in game] + [(x, "L") for x in library])
    runs: list[list] = []
    for x, t in tagged:
        if runs and runs[-1][0] == t:
            runs[-1][2] = x
            runs[-1][3] += 1
        else:
            runs.append([t, x, x, 1])
    # A few library references inside game data (shared tables) do not split it.
    merged: list[list] = []
    for run in runs:
        if merged and run[0] == "L" and run[3] < 3:
            continue
        if merged and merged[-1][0] == run[0]:
            merged[-1][2] = run[2]
            merged[-1][3] += run[3]
        else:
            merged.append(run)
    # Import thunks are shared by every caller of an API, not per file.
    e = struct.unpack_from("<I", pe.b, 0x3C)[0]
    imp = pe.base + struct.unpack_from("<I", pe.b, e + 24 + 96 + 8)[0]
    idata = next(
        (
            (pe.base + va, pe.base + va + vs)
            for va, vs, _, _ in pe.secs
            if pe.base + va <= imp < pe.base + va + vs
        ),
        (0, 0),
    )
    return [
        (a, z + 1)
        for t, a, z, n in merged
        if t == "G" and n >= 20 and not (idata[0] <= a < idata[1])
    ]


def strong_match(row: dict) -> bool:
    agree, total = (row["agree"].split("/") + ["0"])[:2]
    if float(row["score"]) < 0.6:
        return False
    if row["method"] in ("strings", "code", "string"):
        return True
    return int(total) > 0 and int(agree) / int(total) >= 0.75


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("program", help="PE program with a feature dump (e.g. WINDREAM.EXE)")
    ap.add_argument("--cross", help="second build paired by match_functions.py (DREAMSFX.EXE)")
    ap.add_argument("--window", type=int, default=10, help="functions either side of a cut")
    ap.add_argument("--const-window", type=int, default=20)
    ap.add_argument("--no-calls", action="store_true", help="drop the call-graph evidence")
    args = ap.parse_args()

    from dreams import paths

    pe = PE(Path(paths.get("disc1")) / args.program)
    fs = json.loads((FEATURES / f"{args.program}.json").read_text(encoding="utf-8"))
    fs.sort(key=lambda f: int(f["entry"], 16))
    entries = [int(f["entry"], 16) for f in fs]
    lib = library_start(args.program, entries)
    game = [f for f in fs if int(f["entry"], 16) < lib]
    ent = [int(f["entry"], 16) for f in game]
    n = len(game)
    rel = sorted(pe.relocs)

    def refs(f: dict) -> set[int]:
        e = int(f["entry"], 16)
        i = bisect.bisect_left(rel, e)
        out = set()
        while i < len(rel) and rel[i] < e + f["size"]:
            out.add(pe.u32(rel[i]))
            i += 1
        return out

    code_end = pe.base + pe.secs[0][0] + pe.secs[0][1]
    R = [refs(f) for f in game]
    lib_refs = {x for f in fs if int(f["entry"], 16) >= lib for x in refs(f)}
    regions = data_regions(pe, R, lib_refs, code_end)

    def region(x: int) -> int | None:
        for k, (lo, hi) in enumerate(regions):
            if lo <= x < hi:
                return k
        return None

    # The constants region is the one whose items no two functions far apart share.
    users: dict[int, set[int]] = defaultdict(set)
    for i, r in enumerate(R):
        for x in r:
            if region(x) is not None:
                users[x].add(i)
    spread = {
        k: sum(max(u) - min(u) > args.window for x, u in users.items() if region(x) == k)
        / max(1, sum(1 for x in users if region(x) == k))
        for k in range(len(regions))
    }
    const = min(spread, key=spread.get)

    joins: dict[str, list[bool]] = {}
    W, CW = args.window, args.const_window

    def local(x: int) -> bool:
        return max(users[x]) - min(users[x]) <= W

    # C: any constant used left of the cut at or above one used right of it.
    j = [False] * (n - 1)
    for c in range(n - 1):
        left = [x for i in range(max(0, c - CW + 1), c + 1) for x in R[i] if region(x) == const]
        right = [x for i in range(c + 1, min(n, c + 1 + CW)) for x in R[i] if region(x) == const]
        j[c] = bool(left and right and max(left) >= min(right))
    joins["C"] = j

    # shared: a data/bss item confined to the window, used on both sides.
    j = [False] * (n - 1)
    for x, u in users.items():
        if region(x) != const and max(u) - min(u) <= W:
            for c in range(min(u), max(u)):
                j[c] = True
    joins["shared"] = j

    if not args.no_calls:
        idx = {e: i for i, e in enumerate(ent)}
        callers: dict[int, set[int]] = defaultdict(set)
        for i, f in enumerate(game):
            for c in f["calls"]:
                k = idx.get(int(c, 16)) if ":" not in c else None
                if k is not None and k != i:
                    callers[k].add(i)
        j = [False] * (n - 1)
        for g, cs in callers.items():
            if len(cs) <= 2 and max(abs(x - g) for x in cs) <= W:
                for c in range(min(min(cs), g), max(max(cs), g)):
                    j[c] = True
        joins["calls"] = j

    # A proven boundary lies at one of the cuts between the two twins. Use the
    # cut with the least join evidence (the first, on a tie).
    hard: dict[int, str] = {}
    if args.cross:
        path = MATCH / f"{args.cross}--{args.program}.tsv"
        with path.open(encoding="utf-8") as fh:
            rows = [r for r in csv.DictReader(fh, delimiter="\t") if strong_match(r)]
        index = {e: i for i, e in enumerate(ent)}
        pairs = sorted(
            (index[int(r["b_entry"], 16)], int(r["a_entry"], 16))
            for r in rows
            if int(r["b_entry"], 16) in index
        )
        for (ia, xa), (ib, xb) in zip(pairs, pairs[1:], strict=False):
            if xb < xa:
                zone = range(ia, ib)
                c = min(zone, key=lambda c: sum(joins[k][c] for k in joins))
                overruled = [k for k in joins if joins[k][c]]
                hard[c] = f"{ent[ia]:x}/{ent[ib]:x} reversed in {args.cross}" + (
                    f"; overrules {'+'.join(overruled)}" if overruled else ""
                )
    for k in joins:
        for c in hard:
            joins[k][c] = False

    cut = [not any(joins[k][c] for k in joins) for c in range(n - 1)]
    blocks, start = [], 0
    for c in range(n - 1):
        if cut[c]:
            blocks.append((start, c))
            start = c + 1
    blocks.append((start, n - 1))

    OUT.mkdir(parents=True, exist_ok=True)
    out = OUT / f"{args.program}.tsv"
    names = ["C", "D", "B"]
    with out.open("w", encoding="utf-8", newline="\n") as fh:
        head = ["block", "first", "last", "functions", "named", "evidence", "boundary_after"]
        head += [f"region{k}_{names[k] if k < 3 else k}" for k in range(len(regions))]
        fh.write("\t".join(head) + "\n")
        for k, (a, z) in enumerate(blocks):
            ev = sorted({name for name in joins for c in range(a, z) if joins[name][c]})
            span = range(a, z + 1)
            named = [game[i]["name"] for i in span if not game[i]["name"].startswith("FUN_")]
            spans = []
            for r in range(len(regions)):
                xs = [x for i in span for x in R[i] if region(x) == r and local(x)]
                spans.append(f"{min(xs):x}-{max(xs):x}" if xs else "")
            after = f"proven: {hard[z]}" if z in hard else ("end" if z == n - 1 else "candidate")
            cols = [k, f"{ent[a]:08x}", f"{ent[z]:08x}", z - a + 1, " ".join(named), "+".join(ev)]
            cols += [after, *spans]
            fh.write("\t".join(map(str, cols)) + "\n")
    sizes = [z - a + 1 for a, z in blocks]
    print(f"{args.program}: {n} game functions before the library at {lib:#x}")
    spans = ", ".join(f"{lo:#x}-{hi:#x}" for lo, hi in regions)
    print(f"data regions: {spans} (constants: region {const})")
    singles = sum(s == 1 for s in sizes)
    print(f"{len(blocks)} blocks ({singles} single functions), {len(hard)} proven boundaries")
    print(f"-> {out}")


if __name__ == "__main__":
    main()
