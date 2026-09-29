"""Verify the small, literal-byte-identical demo-to-retail naming evidence set.

Checks embedded symbol ownership, complete body hashes, original LE file bytes,
the retail target (both Windows twins when applicable), source uniqueness, and
the name registry. DOS evidence also pins the target executable SHA-256.
This does not accept normalized similarity as byte identity or modify Ghidra.

    uv run python tools/check_wip_names.py --demo <July-1997-demo-directory>
"""

from __future__ import annotations

import argparse
import csv
import hashlib
from pathlib import Path

from match_identical import PE
from watcom_debug import read_debug, read_le_bytes

from dreams import paths

ROOT = Path(__file__).resolve().parents[1]


def verify(demo: Path, evidence: Path = ROOT / "re/reviews/wip-byte-matches.tsv") -> int:
    with evidence.open(encoding="utf-8", newline="") as file:
        rows = list(csv.DictReader(file, delimiter="\t"))
    registries = {}
    for program in {r.get("target", "WINDREAM.EXE") for r in rows}:
        with (ROOT / "re/names" / f"{program}.tsv").open(encoding="utf-8", newline="") as file:
            registries[program] = {r["address"]: r for r in csv.DictReader(file, delimiter="\t")}
    binaries = {}
    failures = 0
    cache = {}
    for row in rows:
        try:
            source = row["source"]
            if source not in cache:
                data = (demo / source).read_bytes()
                cache[source] = data, read_debug(data)[1]
            data, symbols = cache[source]
            if hashlib.sha256(data).hexdigest() != row["source_sha256"]:
                raise ValueError("source executable SHA-256 differs")
            size = int(row["size"])
            address = int(row["source_address"], 16)
            symbol = [s for s in symbols if s["address"] == row["source_address"]]
            if not any(
                s["name"] == row["name"] and s["module"] == row["module"] and s["kind"] & 4
                for s in symbol
            ):
                raise ValueError("embedded code symbol or module differs")
            code_symbols = {int(s["address"], 16) for s in symbols if s["kind"] & 4}
            if any(address < entry < address + size for entry in code_symbols):
                raise ValueError("claimed body crosses another code symbol")
            body = read_le_bytes(data, address, size)
            if hashlib.sha256(body).hexdigest() != row["body_sha256"]:
                raise ValueError("source body SHA-256 differs")
            hits = []
            for entry in code_symbols:
                try:
                    if read_le_bytes(data, entry, size) == body:
                        hits.append(entry)
                except ValueError:
                    continue
            if hits != [address]:
                raise ValueError("source body is not unique among code symbols")
            target = int(row["address"], 16)
            program = row.get("target", "WINDREAM.EXE")
            targets = ("WINDREAM.EXE", "GDIDREAM.EXE") if program == "WINDREAM.EXE" else (program,)
            for name in targets:
                if name not in binaries:
                    binary = paths.disc(1) / name
                    binaries[name] = (
                        binary.read_bytes()
                        if name in ("DREAMS.EXE", "DREAMSFX.EXE")
                        else PE(binary)
                    )
                image = binaries[name]
                target_digest = row.get("target_sha256")
                raw_target = image if isinstance(image, bytes) else image.b
                if target_digest and hashlib.sha256(raw_target).hexdigest() != target_digest:
                    raise ValueError(f"{name} executable SHA-256 differs")
                actual = (
                    read_le_bytes(image, target, size)
                    if isinstance(image, bytes)
                    else image.read(target, size)
                )
                if actual != body:
                    raise ValueError(f"{name} body bytes differ")
                if not isinstance(image, bytes) and any(
                    target <= r < target + size for r in image.relocs
                ):
                    raise ValueError(f"{name} body requires relocation")
            if registries[program].get(row["address"], {}).get("name") != row["name"]:
                raise ValueError("name registry differs")
            print(f"PASS {row['address']} {row['name']} ({size} bytes, {', '.join(targets)})")
        except (ValueError, OSError) as error:
            failures += 1
            print(f"FAIL {row['name']}: {error}")
    return failures


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, default=ROOT / "re/reviews/wip-byte-matches.tsv")
    args = parser.parse_args()
    raise SystemExit(1 if verify(args.demo, args.evidence) else 0)


if __name__ == "__main__":
    main()
