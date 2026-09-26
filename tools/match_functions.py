"""Pair functions across two builds of the engine.

Input is two feature dumps written by ghidra_scripts/ExportFunctionFeatures.java
(out/ghidra/features/<program>.json). The builds are the same Watcom source
compiled for different targets, so identical code is rare but constants
(immediates, struct offsets), string literals, instruction shape and the call
graph survive.

Matching, in order:
  1. seeds: unique identical masked code (>= 8 instructions), unique identical
     string sets, and strings referenced by exactly one function on each side;
  2. propagation: unmatched callees and callers of matched pairs, paired when
     mutually best and above a threshold, plus call-slot alignment (an
     unmatched call between the same matched neighbours in both call
     sequences);
  3. a strict whole-program pass for what is left.

Every pair gets a score and a call-graph agreement ratio: the share of the
pair's already-matched callees that map onto each other.

    uv run python tools/match_functions.py DREAMSFX.EXE WINDREAM.EXE
    uv run python tools/match_functions.py DREAMSFX.EXE WINDREAM.EXE --renames

--renames also writes, for each program, name proposals (address, name,
comment) for its unnamed functions whose twin is named, in the file format
ghidra_scripts/Rename.java accepts as @file.
"""

from __future__ import annotations

import argparse
import difflib
import json
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FEATURES = ROOT / "out" / "ghidra" / "features"
OUT = ROOT / "out" / "ghidra" / "match"

SEED_MIN_INS = 8
PROPAGATE_MIN = 0.55
SLOT_MIN = 0.35
GLOBAL_MIN = 0.80
GLOBAL_MIN_INS = 20
MARGIN = 0.05
# Names that are not evidence of meaning and must not be transferred.
GENERIC_PREFIXES = ("FUN_", "caseD_", "switchD_", "LAB_", "thunk_")
# Platform layers: a twin in the other build is a different mechanism, so
# these names never transfer (docs/engine.md, Renderer backends).
PLATFORM_PREFIXES = (
    "GLIDE_",
    "gr",
    "gu",
    "_",
    "DDRAW_",
    "DSOUND_",
    "GDI_",
    "VID_",
    "KBD_",
    "TIMER_",
    "DPMI_",
    "AIL_",
    "JOY_",
    "INPUT_",
    "SYS_",
    "WinMain",
)
# A proposal needs a strong score or call-graph agreement.
RENAME_MIN_SCORE = 0.60
RENAME_MIN_AGREE = 2


@dataclass
class Func:
    entry: str
    name: str
    named: bool
    ins: int
    masked: tuple[str, ...]
    consts: Counter
    bigrams: Counter
    strings: frozenset[str]
    calls: list[str]
    icalls: int
    callers: set[str] = field(default_factory=set)


def load(program: str) -> dict[str, Func]:
    raw = json.loads((FEATURES / f"{program}.json").read_text(encoding="utf-8"))
    funcs: dict[str, Func] = {}
    for r in raw:
        mnem = r["mnem"]
        funcs[r["entry"]] = Func(
            entry=r["entry"],
            name=r["name"],
            named=r["named"] and not r["name"].startswith(GENERIC_PREFIXES),
            ins=r["ins"],
            masked=tuple(r["masked"]),
            consts=Counter(r["consts"]),
            bigrams=Counter(f"{a} {b}" for a, b in zip(mnem, mnem[1:], strict=False)),
            strings=frozenset(r["strings"]),
            calls=r["calls"],
            icalls=r["icalls"],
        )
    for f in funcs.values():
        for c in f.calls:
            if c in funcs:
                funcs[c].callers.add(f.entry)
    return funcs


def jaccard(a: Counter, b: Counter) -> float:
    union = sum((a | b).values())
    return sum((a & b).values()) / union if union else 1.0


def similarity(a: Func, b: Func) -> float:
    """0..1; weights calibrated on hand-matched pairs (see docs/re-setup.md)."""
    if a.ins == 0 or b.ins == 0:
        return 0.0
    ratio = min(a.ins, b.ins) / max(a.ins, b.ins)
    if ratio < 0.5:
        return 0.0
    calls = min(len(a.calls), len(b.calls)) + 1
    calls /= max(len(a.calls), len(b.calls)) + 1
    parts = [(0.35, jaccard(a.bigrams, b.bigrams)), (0.15, ratio), (0.10, calls)]
    if a.consts or b.consts:
        parts.append((0.40, jaccard(a.consts, b.consts)))
    if a.strings or b.strings:
        inter = len(a.strings & b.strings)
        parts.append((0.40, inter / len(a.strings | b.strings)))
    weight = sum(w for w, _ in parts)
    return sum(w * v for w, v in parts) / weight


class Matcher:
    def __init__(self, a: dict[str, Func], b: dict[str, Func]):
        self.a, self.b = a, b
        self.ab: dict[str, str] = {}
        self.ba: dict[str, str] = {}
        self.how: dict[str, tuple[str, float]] = {}

    def pair(self, x: str, y: str, method: str, score: float) -> bool:
        if x in self.ab or y in self.ba:
            return False
        self.ab[x], self.ba[y] = y, x
        self.how[x] = (method, score)
        return True

    def seed(self) -> None:
        def unique(funcs: dict[str, Func], key) -> dict:
            groups = defaultdict(list)
            for f in funcs.values():
                k = key(f)
                if k is not None:
                    groups[k].append(f.entry)
            return {k: v[0] for k, v in groups.items() if len(v) == 1}

        code_a = unique(self.a, lambda f: f.masked if f.ins >= SEED_MIN_INS else None)
        code_b = unique(self.b, lambda f: f.masked if f.ins >= SEED_MIN_INS else None)
        for k, x in code_a.items():
            if k in code_b:
                self.pair(x, code_b[k], "code", 1.0)

        set_a = unique(self.a, lambda f: f.strings or None)
        set_b = unique(self.b, lambda f: f.strings or None)
        for k, x in set_a.items():
            if k in set_b:
                self.pair(x, set_b[k], "strings", 1.0)

        # A string owned by exactly one function on each side, backed by
        # overall similarity so a moved error message cannot mislead.
        own_a, own_b = defaultdict(list), defaultdict(list)
        for f in self.a.values():
            for s in f.strings:
                own_a[s].append(f.entry)
        for f in self.b.values():
            for s in f.strings:
                own_b[s].append(f.entry)
        for s, xs in own_a.items():
            ys = own_b.get(s, [])
            if len(xs) == 1 and len(ys) == 1 and len(s) >= 6:
                score = similarity(self.a[xs[0]], self.b[ys[0]])
                if score >= PROPAGATE_MIN:
                    self.pair(xs[0], ys[0], "string", score)

    def _best(self, xs, ys, minimum: float) -> list[tuple[str, str, float]]:
        """Mutual-best pairs between two candidate sets."""
        xs = [x for x in xs if x not in self.ab]
        ys = [y for y in ys if y not in self.ba]
        if not xs or not ys:
            return []
        scores = {(x, y): similarity(self.a[x], self.b[y]) for x in xs for y in ys}
        out = []
        for x in xs:
            ranked = sorted(((scores[x, y], y) for y in ys), reverse=True)
            s, y = ranked[0]
            if s < minimum or (len(ranked) > 1 and s - ranked[1][0] < MARGIN):
                continue
            back = sorted(((scores[x2, y], x2) for x2 in xs), reverse=True)
            if back[0][1] != x or (len(back) > 1 and back[0][0] - back[1][0] < MARGIN):
                continue
            out.append((x, y, s))
        return out

    def slots(self, x: str, y: str) -> list[tuple[str, str, float]]:
        """Unmatched callees in the same call slot of a matched pair.

        Call sequences are aligned on already-matched callees; an equal-length
        run of unmatched calls between two aligned anchors pairs positionally.
        """
        seq_a = [self.ab.get(c, "?" + c) for c in self.a[x].calls]
        seq_b = [c if c in self.ba else "?" + c for c in self.b[y].calls]
        out = []
        sm = difflib.SequenceMatcher(None, seq_a, seq_b, autojunk=False)
        for op, i1, i2, j1, j2 in sm.get_opcodes():
            if op != "replace" or i2 - i1 != j2 - j1:
                continue
            for ta, tb in zip(seq_a[i1:i2], seq_b[j1:j2], strict=True):
                if ta.startswith("?") and tb.startswith("?"):
                    x2, y2 = ta[1:], tb[1:]
                    if x2 in self.a and y2 in self.b:
                        s = similarity(self.a[x2], self.b[y2])
                        if s >= SLOT_MIN:
                            out.append((x2, y2, s))
        return out

    def propagate(self) -> None:
        changed = True
        while changed:
            changed = False
            for x, y in list(self.ab.items()):
                fa, fb = self.a[x], self.b[y]
                callees_a = [c for c in dict.fromkeys(fa.calls) if c in self.a]
                callees_b = [c for c in dict.fromkeys(fb.calls) if c in self.b]
                for x2, y2, s in self._best(callees_a, callees_b, PROPAGATE_MIN):
                    changed |= self.pair(x2, y2, "callee", s)
                for x2, y2, s in self._best(fa.callers, fb.callers, PROPAGATE_MIN):
                    changed |= self.pair(x2, y2, "caller", s)
                for x2, y2, s in self.slots(x, y):
                    changed |= self.pair(x2, y2, "slot", s)

    def global_pass(self) -> None:
        xs = [x for x, f in self.a.items() if x not in self.ab and f.ins >= GLOBAL_MIN_INS]
        ys = [y for y, f in self.b.items() if y not in self.ba and f.ins >= GLOBAL_MIN_INS]
        for x, y, s in self._best(xs, ys, GLOBAL_MIN):
            self.pair(x, y, "global", s)

    def agreement(self, x: str) -> tuple[int, int]:
        """Matched callees of x whose twins are callees of x's twin."""
        y = self.ab[x]
        callees_b = set(self.b[y].calls)
        known = [c for c in set(self.a[x].calls) if c in self.ab]
        return sum(self.ab[c] in callees_b for c in known), len(known)

    def confident(self, x: str) -> bool:
        """Strong enough to carry a name across builds."""
        method, score = self.how[x]
        ok, n = self.agreement(x)
        if n and ok / n < 0.75:
            return False
        return score >= RENAME_MIN_SCORE or ok >= RENAME_MIN_AGREE


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("a", help="first program, e.g. DREAMSFX.EXE")
    ap.add_argument("b", help="second program, e.g. WINDREAM.EXE")
    ap.add_argument("--renames", action="store_true", help="write name-transfer files")
    args = ap.parse_args()

    a, b = load(args.a), load(args.b)
    m = Matcher(a, b)
    m.seed()
    seeded = len(m.ab)
    m.propagate()
    m.global_pass()
    m.propagate()

    OUT.mkdir(parents=True, exist_ok=True)
    table = OUT / f"{args.a}--{args.b}.tsv"
    rows = ["a_entry\ta_name\tb_entry\tb_name\tmethod\tscore\tagree"]
    methods = Counter()
    for x, y in sorted(m.ab.items()):
        method, score = m.how[x]
        methods[method] += 1
        ok, n = m.agreement(x)
        rows.append(f"{x}\t{a[x].name}\t{y}\t{b[y].name}\t{method}\t{score:.2f}\t{ok}/{n}")
    table.write_text("\n".join(rows) + "\n", encoding="utf-8")

    disagree = sum(1 for x in m.ab if (lambda r: r[1] and r[0] / r[1] < 0.5)(m.agreement(x)))
    print(f"{args.a}: {len(a)} functions, {args.b}: {len(b)}")
    print(f"matched {len(m.ab)} ({seeded} seeds); by method: {dict(methods)}")
    print(f"pairs whose matched callees mostly disagree: {disagree}")
    print(f"wrote {table.relative_to(ROOT)}")

    if args.renames:
        for src, dst, fwd, prog_src, target in (
            (a, b, m.ab, args.a, args.b),
            (b, a, m.ba, args.b, args.a),
        ):
            lines, conflicts = [], []
            for x, y in sorted(fwd.items()):
                ax = x if fwd is m.ab else y
                name = src[x].name
                if not src[x].named or name.startswith(PLATFORM_PREFIXES):
                    continue
                if dst[y].named:
                    if dst[y].name != name:
                        conflicts.append(f"{target} {y} {dst[y].name} vs {prog_src} {x} {name}")
                    continue
                if not m.confident(ax):
                    continue
                method, score = m.how[ax]
                lines.append(
                    f"{y}\t{name}\tName matched from {prog_src} {x} "
                    f"({method}, {score:.2f}; tools/match_functions.py)."
                )
            path = OUT / f"renames-{target}.tsv"
            path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
            print(f"wrote {len(lines)} name proposals to {path.relative_to(ROOT)}")
            for c in conflicts:
                print(f"  name conflict: {c}")


if __name__ == "__main__":
    main()
