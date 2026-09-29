"""Register unambiguous original function names in an identical demo import.

This is a direct symbol import, not a cross-build name transfer. The copied
executable must be byte-identical to its debug-symbol source. Only existing
Ghidra function entries with a unique name and address are eligible. Existing
registry rows are preserved; aliases and ambiguous entries remain in the raw
debug export. Run check_names.py and Rename.java after this command.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path

from watcom_debug import read_debug

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("import_copy", type=Path)
    args = parser.parse_args()
    data = args.source.read_bytes()
    if data != args.import_copy.read_bytes():
        raise SystemExit("Import copy is not byte-identical to the symbol source")
    program = args.import_copy.name
    digest = hashlib.sha256(data).hexdigest()
    symbols = [s for s in read_debug(data)[1] if s["kind"] & 4]
    names = Counter(s["name"] for s in symbols)
    entries = Counter(s["address"] for s in symbols)
    features = json.loads((ROOT / "out/ghidra/features" / f"{program}.json").read_text())
    functions = {f["entry"]: f for f in features}
    registry = ROOT / "re/names" / f"{program}.tsv"
    fields = ["address", "name", "kind", "sources", "facts", "note"]
    rows = []
    if registry.exists():
        with registry.open(encoding="utf-8", newline="") as file:
            rows = [
                r for r in csv.DictReader(file, delimiter="\t") if not r["address"].startswith("#")
            ]
    registered = {r["address"] for r in rows}
    registered_names = {r["name"] for r in rows}
    added = 0
    for symbol in symbols:
        address, name = symbol["address"], symbol["name"]
        fn = functions.get(address)
        if not fn or not fn["ins"] or names[name] != 1 or entries[address] != 1:
            continue
        if address in registered or name in registered_names:
            continue
        rows.append(
            {
                "address": address,
                "name": name,
                "kind": "recovered",
                "sources": (
                    f"{args.source.name} embedded Watcom v3 code symbol; "
                    f"byte-identical import SHA256 {digest}; tools/watcom_debug.py; "
                    "tools/register_watcom_debug.py"
                ),
                "facts": f"size:{fn['size']}",
                "note": (
                    f"Original module: {symbol['module']}. "
                    f"Symbol object {symbol['segment']} offset {symbol['offset']}. "
                    "Direct original-build import; no retail equivalence or prototype inferred."
                ),
            }
        )
        registered.add(address)
        registered_names.add(name)
        added += 1
    with registry.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        writer.writerows(sorted(rows, key=lambda r: r["address"]))
    print(f"{program}: added {added} direct original names; {len(rows)} registry rows")


if __name__ == "__main__":
    main()
