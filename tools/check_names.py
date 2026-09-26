"""Check the evidence behind every function name in re/names/<program>.tsv.

A name is only as good as the facts under it. Each registry row lists the
facts that make the name true, as machine-checkable assertions against the
function-feature dump (ghidra_scripts/ExportFunctionFeatures.java):

  str:<text>      references a string containing <text>
  imp:<Name>      calls the imported function <Name> (e.g. ReadFile)
  call:<target>   calls <target>: a hex address or a name in the registry
  caller:<target> is called by <target>
  const:<hex>     uses the scalar <hex> (any size)
  ref:<hex>       reads or writes the data address <hex>
  size:<n>        body is <n> bytes

A row that fails any fact is reported and left out of the rename file. Rows
also name their sources (docs sections, blind review, byte match); the rule
in AGENTS.md is two independent sources plus passing facts.

  uv run python tools/check_names.py WINDREAM.EXE
  uv run python tools/check_names.py WINDREAM.EXE --renames --twin GDIDREAM.EXE

--renames writes out/ghidra/match/names-<program>.tsv for Rename.java, with a
plate comment carrying the kind, note, sources and facts. --twin also writes
the same names for a build whose bytes are identical at the same addresses
(GDIDREAM.EXE); rows whose bytes differ there are skipped and listed.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FEATURES = ROOT / "out" / "ghidra" / "features"
REGISTRY = ROOT / "re" / "names"
MATCH = ROOT / "out" / "ghidra" / "match"
sys.path.insert(0, str(ROOT / "src"))
sys.path.insert(0, str(ROOT / "tools"))

KINDS = {
    "recovered": "Name recovered",
    "descriptive": "Descriptive name",
    "runtime": "Watcom runtime, identified by behaviour",
    "cryolib": "CryoLib code",
}


def load_registry(program: str, path: Path | None = None) -> list[dict]:
    path = path or REGISTRY / f"{program}.tsv"
    with path.open(encoding="utf-8", newline="") as fh:
        rows = [r for r in csv.DictReader(fh, delimiter="\t") if not r["address"].startswith("#")]
    for r in rows:
        r["address"] = r["address"].lower().zfill(8)
        if r["kind"] not in KINDS:
            raise SystemExit(f"{r['address']} {r['name']}: unknown kind {r['kind']!r}")
    return rows


def evaluate(feats: list[dict], rows: list[dict]) -> list[tuple[dict, list[str]]]:
    """Each registry row with the facts it fails (empty when it passes)."""
    fs = {f["entry"]: f for f in feats}
    callers: dict[str, set[str]] = {}
    for e, f in fs.items():
        for c in f["calls"]:
            callers.setdefault(c, set()).add(e)
    # A name can sit on several addresses in the dump (runtime duplicates such
    # as fprintf_); a registry name owns exactly one.
    names: dict[str, set[str]] = {}
    for e, f in fs.items():
        names.setdefault(f["name"], set()).add(e)
    names.update({r["name"]: {r["address"]} for r in rows})
    seen: dict[str, str] = {}
    for r in rows:
        if r["name"] in seen:
            raise SystemExit(f"{r['name']} is registered twice ({seen[r['name']]}, {r['address']})")
        seen[r["name"]] = r["address"]

    def target(t: str) -> set[str]:
        if t in names:
            return names[t]
        try:
            return {f"{int(t, 16):08x}"}
        except ValueError:
            return set()

    def check(r: dict) -> list[str]:
        f = fs.get(r["address"])
        if f is None:
            return ["no function at this address"]
        bad = []
        for fact in filter(None, (x.strip() for x in r["facts"].split(";"))):
            kind, _, val = fact.partition(":")
            if kind == "str":
                ok = any(val in s for s in f["strings"])
            elif kind == "imp":
                ok = val in f.get("imports", [])
            elif kind in ("call", "caller"):
                pool = f["calls"] if kind == "call" else callers.get(r["address"], ())
                ok = any(t in pool for t in target(val))
            elif kind == "const":
                v = int(val, 16)
                ok = v in f["consts"] or v in f.get("bigconsts", [])
            elif kind == "ref":
                ok = f"{int(val, 16):08x}" in f.get("refs", [])
            elif kind == "size":
                ok = f["size"] == int(val)
            else:
                ok = False
                fact = f"unknown fact kind: {fact}"
            if not ok:
                bad.append(fact)
        if not r["facts"].strip():
            bad.append("no facts")
        return bad

    return [(r, check(r)) for r in rows]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("program")
    ap.add_argument("--renames", action="store_true", help="write the Rename.java input file")
    ap.add_argument("--twin", help="identical build to copy the names to (GDIDREAM.EXE)")
    ap.add_argument("--registry", type=Path, help="registry file (default re/names/<program>.tsv)")
    args = ap.parse_args()

    feats = json.loads((FEATURES / f"{args.program}.json").read_text(encoding="utf-8"))
    fs = {f["entry"]: f for f in feats}
    rows = load_registry(args.program, args.registry)
    results = evaluate(feats, rows)
    passed = [(r, bad) for r, bad in results if not bad]
    failed = [(r, bad) for r, bad in results if bad]
    for r, bad in failed:
        print(f"FAIL {r['address']} {r['name']}: {'; '.join(bad)}")
    print(f"{args.program}: {len(passed)} names pass, {len(failed)} fail")

    if not args.renames:
        sys.exit(1 if failed else 0)

    def comment(r: dict) -> str:
        text = f"[NAME] {KINDS[r['kind']]}."
        if r["note"]:
            text += f" {r['note']}"
        text += f" Sources: {r['sources']}. Facts (tools/check_names.py): {r['facts']}."
        return text

    MATCH.mkdir(parents=True, exist_ok=True)
    good = [r for r, _ in passed]
    out = MATCH / f"names-{args.program}.tsv"
    with out.open("w", encoding="utf-8", newline="\n") as fh:
        for r in good:
            fh.write(f"{r['address']}\t{r['name']}\t{comment(r)}\n")
    print(f"-> {out}")

    if args.twin:
        from match_identical import PE

        from dreams import paths

        disc = Path(paths.get("disc1"))
        a, b = PE(disc / args.program), PE(disc / args.twin)
        same, differ = [], []
        for r in good:
            addr, size = int(r["address"], 16), fs[r["address"]]["size"]
            (same if a.read(addr, size) == b.read(addr, size) else differ).append(r)
        for r in differ:
            print(f"twin differs, skipped: {r['address']} {r['name']}")
        out = MATCH / f"names-{args.twin}.tsv"
        with out.open("w", encoding="utf-8", newline="\n") as fh:
            for r in same:
                note = f" Same bytes as {args.program} at this address."
                fh.write(f"{r['address']}\t{r['name']}\t{comment(r)}{note}\n")
        print(f"-> {out} ({len(same)} names)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
