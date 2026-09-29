"""Find where the engine's builds swap code: the developers' backend cut.

Cryo compiled one engine three ways: DREAMS.EXE (DOS, software rasterizer on
VESA), DREAMSFX.EXE (DOS, Glide) and WINDREAM.EXE (Windows, software
rasterizer on DirectDraw/GDI). Code that differs between two builds is a
backend; the shared code that reaches it marks the cut.

Input is two feature dumps and their match table (match_functions.py), plus
optionally a third build matched against both. Three views:

  slots     Watcom links each file's functions in source order and the files
            in link order, so matched functions form a chain in the same
            order in both builds (the longest increasing run of pairs). The
            unmatched functions between two chain neighbours fill the same
            link slot: code on both sides is a swapped implementation, code
            on one side only is an addition. Matched pairs off the chain were
            linked in a different order ("moved").
  modified  matched pairs whose masked code differs, with the unmatched
            callees each side calls: shared callers edited for the backend.
  edges     calls and function-pointer references from matched code into
            unmatched code: the entry points of each backend.

    uv run python tools/find_cut.py DREAMS.EXE DREAMSFX.EXE --third WINDREAM.EXE

Writes out/ghidra/cut/<a>--<b>.{slots,modified,edges}.tsv. Unmatched means
the matcher found no twin, not that none exists; read the slots before
calling a function build-specific.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FEATURES = ROOT / "out" / "ghidra" / "features"
MATCH = ROOT / "out" / "ghidra" / "match"
OUT = ROOT / "out" / "ghidra" / "cut"


def load(program: str) -> dict[str, dict]:
    raw = json.loads((FEATURES / f"{program}.json").read_text(encoding="utf-8"))
    return {r["entry"]: r for r in raw}


def pairs(a: str, b: str) -> dict[str, tuple[str, str]]:
    """a entry -> (b entry, method), whichever order the table was written in."""
    for x, y, flip in ((a, b, False), (b, a, True)):
        path = MATCH / f"{x}--{y}.tsv"
        if path.is_file():
            with path.open(encoding="utf-8") as fh:
                rows = list(csv.DictReader(fh, delimiter="\t"))
            if flip:
                return {r["b_entry"]: (r["a_entry"], r["method"]) for r in rows}
            return {r["a_entry"]: (r["b_entry"], r["method"]) for r in rows}
    raise SystemExit(f"no match table for {a} and {b}; run tools/match_functions.py first")


def chain(ab: dict[str, tuple[str, str]]) -> set[str]:
    """a entries of the longest run of pairs in the same order in both builds."""
    ps = sorted((int(x, 16), int(y, 16), x) for x, (y, _) in ab.items())
    tails: list[int] = []
    tail_at: list[int] = []
    prev = [-1] * len(ps)
    for i, (_, y, _) in enumerate(ps):
        k = bisect.bisect_left(tails, y)
        if k == len(tails):
            tails.append(y)
            tail_at.append(i)
        else:
            tails[k] = y
            tail_at[k] = i
        prev[i] = tail_at[k - 1] if k else -1
    out, i = set(), tail_at[-1] if tail_at else -1
    while i >= 0:
        out.add(ps[i][2])
        i = prev[i]
    return out


def label(fs: dict[str, dict], e: str) -> str:
    n = fs[e]["name"]
    return e if n.startswith("FUN_") else f"{e}:{n}"


def fnrefs(f: dict, fs: dict[str, dict]) -> list[str]:
    """Function entries a function uses as data (hook and callback stores)."""
    return [r.zfill(8) for r in f["refs"] if r.zfill(8) in fs]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("a", help="first build, e.g. DREAMS.EXE")
    ap.add_argument("b", help="second build, e.g. DREAMSFX.EXE")
    ap.add_argument("--third", help="a build matched against both (WINDREAM.EXE)")
    args = ap.parse_args()

    A, B = load(args.a), load(args.b)
    ab = pairs(args.a, args.b)
    ba = {y: (x, m) for x, (y, m) in ab.items()}
    on_chain = chain(ab)
    C, ac, bc = {}, {}, {}
    if args.third:
        C = load(args.third)
        ac, bc = pairs(args.a, args.third), pairs(args.b, args.third)

    def twin_c(side: str, e: str) -> str:
        m = (ac if side == "a" else bc).get(e)
        return label(C, m[0]) if m else ""

    OUT.mkdir(parents=True, exist_ok=True)
    stem = f"{args.a}--{args.b}"

    def out(kind: str) -> Path:
        return OUT / f"{stem}.{kind}.tsv"

    # Slots between consecutive chain pairs.
    ordA = sorted(A, key=lambda e: int(e, 16))
    ordB = sorted(B, key=lambda e: int(e, 16))
    posB = {e: i for i, e in enumerate(ordB)}
    anchors = [(i, posB[ab[e][0]]) for i, e in enumerate(ordA) if e in on_chain]
    anchors = [(-1, -1), *anchors, (len(ordA), len(ordB))]
    slot_rows, kinds = [], defaultdict(int)
    head = ["slot", "kind", "after_a", "after_b", "a_only", "a_bytes", "b_only", "b_bytes"]
    head += ["moved_a", "moved_b"] + ([f"{args.third}_twins"] if C else [])
    for k, ((ia, ib), (ja, jb)) in enumerate(zip(anchors, anchors[1:], strict=False)):
        a_in = ordA[ia + 1 : ja]
        b_in = ordB[ib + 1 : jb]
        a_only = [e for e in a_in if e not in ab]
        b_only = [e for e in b_in if e not in ba]
        if not a_only and not b_only:
            continue
        kind = "swap" if a_only and b_only else ("a-only" if a_only else "b-only")
        kinds[kind] += 1
        twins = sorted({t for e in a_only if (t := twin_c("a", e))}
                       | {t for e in b_only if (t := twin_c("b", e))})  # fmt: skip
        row = [
            k,
            kind,
            label(A, ordA[ia]) if ia >= 0 else "start",
            label(B, ordB[ib]) if ib >= 0 else "start",
            " ".join(label(A, e) for e in a_only),
            sum(A[e]["size"] for e in a_only),
            " ".join(label(B, e) for e in b_only),
            sum(B[e]["size"] for e in b_only),
            " ".join(label(A, e) for e in a_in if e in ab),
            " ".join(label(B, e) for e in b_in if e in ba),
        ]
        if C:
            row.append(" ".join(twins))
        slot_rows.append(row)
    with out("slots").open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("\t".join(head) + "\n")
        for r in slot_rows:
            fh.write("\t".join(map(str, r)) + "\n")

    # Modified shared functions and the backend code each side reaches.
    mod_rows = []
    for x, (y, method) in sorted(ab.items()):
        fa, fb = A[x], B[y]
        if fa["masked"] == fb["masked"]:
            continue
        ua = [c for c in fa["calls"] + fnrefs(fa, A) if c in A and c not in ab]
        ub = [c for c in fb["calls"] + fnrefs(fb, B) if c in B and c not in ba]
        mod_rows.append([
            label(A, x), label(B, y), method, fa["ins"], fb["ins"],
            " ".join(dict.fromkeys(label(A, c) for c in ua)),
            " ".join(dict.fromkeys(label(B, c) for c in ub)),
            twin_c("a", x) or twin_c("b", y) if C else "",
        ])  # fmt: skip
    with out("modified").open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("a\tb\tmethod\ta_ins\tb_ins\ta_unmatched_callees\tb_unmatched_callees")
        fh.write(f"\t{args.third or 'third'}\n")
        for r in mod_rows:
            fh.write("\t".join(map(str, r)) + "\n")

    # Edges from matched code into unmatched code, grouped by target.
    edge_rows = []
    for side, fs, own in (("a", A, ab), ("b", B, ba)):
        into: dict[str, list[str]] = defaultdict(list)
        for e, f in fs.items():
            if e not in own:
                continue
            for c in f["calls"]:
                if c in fs and c not in own:
                    into[c].append(label(fs, e))
            for c in fnrefs(f, fs):
                if c not in own:
                    into[c].append(label(fs, e) + "(ptr)")
        for tgt, srcs in sorted(into.items()):
            edge_rows.append([side, label(fs, tgt), len(srcs), " ".join(dict.fromkeys(srcs))])
    with out("edges").open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("side\ttarget\tcallers\tfrom\n")
        for r in edge_rows:
            fh.write("\t".join(map(str, r)) + "\n")

    print(f"{args.a}: {len(A)} functions, {args.b}: {len(B)}, matched {len(ab)}")
    print(f"link-order chain: {len(on_chain)} pairs; {len(ab) - len(on_chain)} moved")
    print(f"slots: {dict(kinds)}; modified shared functions: {len(mod_rows)}")
    print(f"edges into unmatched code: {len(edge_rows)}")
    print(f"-> {(OUT / stem).relative_to(ROOT)}.{{slots,modified,edges}}.tsv")


if __name__ == "__main__":
    main()
