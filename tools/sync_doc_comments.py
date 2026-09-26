"""Copy what the docs say about each address into Ghidra plate comments.

Every docs/*.md sentence, list item or table row that mentions an address in
the program (FUN_00xxxxxx, 0x4xxxxx, 004xxxxx) or a registered function name
(re/names/<program>.tsv) becomes part of a [DOCS_SYNC] block on that address:

  [DOCS_SYNC]
  [docs/<file> > <section>] <sentence>

  [docs/<file> > <section>] <sentence>

ghidra_scripts/ApplyDocComments.java applies the file: it removes every
existing [DOCS_SYNC] block first, so comments follow the docs as they change.

  uv run python tools/sync_doc_comments.py WINDREAM.EXE

Writes out/ghidra/match/docsync-<program>.tsv (address, text with \\n escapes).
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
FEATURES = ROOT / "out" / "ghidra" / "features"
REGISTRY = ROOT / "re" / "names"
OUT = ROOT / "out" / "ghidra" / "match"

MAX_PER_ADDRESS = 8
MAX_CHARS = 600


def units(text: str):
    """(section, unit) pairs: table rows, list items and sentences of paragraphs."""
    section = ""
    in_code = False
    for block in re.split(r"\n\s*\n", text):
        lines = block.strip("\n").split("\n")
        for line in lines:
            if line.startswith("```"):
                in_code = not in_code
            if line.startswith("#") and not in_code:
                section = line.lstrip("#").strip()
        if in_code or block.lstrip().startswith("```"):
            continue
        if all(ln.lstrip().startswith("|") for ln in lines if ln.strip()):
            for ln in lines:
                if not re.match(r"^\s*\|[\s:|-]+\|\s*$", ln):
                    yield section, ln.strip()
            continue
        if all(re.match(r"^\s*([-*]|\d+\.)\s", ln) or ln.startswith("  ") for ln in lines):
            item = ""
            for ln in lines:
                if re.match(r"^\s*([-*]|\d+\.)\s", ln) and item:
                    yield section, item.strip()
                    item = ""
                item += " " + ln.strip()
            if item:
                yield section, item.strip()
            continue
        para = " ".join(ln.strip() for ln in lines if not ln.startswith("#"))
        for sentence in re.split(r"(?<=[.!?])\s+(?=[A-Z`*])", para):
            if sentence.strip():
                yield section, sentence


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("program")
    args = ap.parse_args()

    feats = json.loads((FEATURES / f"{args.program}.json").read_text(encoding="utf-8"))
    entries = {int(f["entry"], 16) for f in feats}
    lo = min(entries)
    hi = max(int(f["entry"], 16) + f["size"] for f in feats)
    names: dict[str, int] = {}
    reg = REGISTRY / f"{args.program}.tsv"
    if reg.exists():
        with reg.open(encoding="utf-8", newline="") as fh:
            for r in csv.DictReader(fh, delimiter="\t"):
                if not r["address"].startswith("#"):
                    names[r["name"]] = int(r["address"], 16)
    addr_re = re.compile(r"(?<![0-9A-Za-z_])(?:FUN_|LAB_|0x)?(00[0-9a-fA-F]{6}|4[0-9a-fA-F]{5})\b")
    alternatives = "|".join(map(re.escape, sorted(names, key=len, reverse=True)))
    name_re = (
        re.compile(r"(?<![0-9A-Za-z_])(" + alternatives + r")(?![0-9A-Za-z_])") if names else None
    )

    found: dict[int, list[str]] = {}
    for doc in sorted(DOCS.glob("*.md")):
        text = doc.read_text(encoding="utf-8")
        for section, unit in units(text):
            hits = set()
            for m in addr_re.finditer(unit):
                a = int(m.group(1), 16)
                if lo <= a < hi:
                    hits.add(a)
            if name_re:
                hits.update(names[m.group(1)] for m in name_re.finditer(unit))
            clean = " ".join(unit.split())
            if len(clean) > MAX_CHARS:
                clean = clean[: MAX_CHARS - 1] + "…"
            for a in hits:
                entry = f"[docs/{doc.name} > {section}] {clean}"
                lst = found.setdefault(a, [])
                if entry not in lst and len(lst) < MAX_PER_ADDRESS:
                    lst.append(entry)

    OUT.mkdir(parents=True, exist_ok=True)
    out = OUT / f"docsync-{args.program}.tsv"
    with out.open("w", encoding="utf-8", newline="\n") as fh:
        for a in sorted(found):
            body = "[DOCS_SYNC]\n" + "\n\n".join(found[a])
            fh.write(f"{a:08x}\t{body.replace(chr(10), chr(92) + 'n')}\n")
    print(f"{args.program}: {len(found)} addresses from docs -> {out}")


if __name__ == "__main__":
    main()
