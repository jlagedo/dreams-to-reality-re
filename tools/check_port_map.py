"""Validate the retail-function port map and report behavior coverage.

Run from the repository root: ``uv run python tools/check_port_map.py``.
The map is the durable source for both C++ locations and Ghidra PORT tags.
"""

from __future__ import annotations

import csv
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PORT_MAP = ROOT / "opendreams" / "port-map.tsv"
REGISTRY_ROOT = ROOT / "re" / "names"
FIELDS = [
    "program",
    "address",
    "checked_name",
    "source_block",
    "cpp_file",
    "cpp_symbol",
    "status",
    "evidence",
    "adaptation",
    "coverage",
    "remaining_work",
    "reviewed",
]
STATUSES = {"ported", "adapted", "replaced", "omitted"}
COVERAGE = {"complete", "partial", "unverified", "none"}
REVIEWED = {"yes", "no"}


def checked_names(program: str) -> dict[str, str]:
    names: dict[str, str] = {}
    with (REGISTRY_ROOT / f"{program}.tsv").open(encoding="utf-8", newline="") as source:
        for row in csv.DictReader(
            (line for line in source if not line.startswith("#")), delimiter="\t"
        ):
            names[row["address"].lower()] = row["name"]
    return names


def validate() -> int:
    registries = {
        program: checked_names(program)
        for program in ("WINDREAM.EXE", "DREAMSFX.EXE")
    }
    registries["GDIDREAM.EXE"] = registries["WINDREAM.EXE"]
    errors: list[str] = []
    counts: dict[str, Counter[str]] = defaultdict(Counter)
    review_counts: Counter[str] = Counter()
    twins: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    seen: set[tuple[str, str]] = set()
    with PORT_MAP.open(encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source, delimiter="\t")
        if reader.fieldnames != FIELDS:
            print(f"port-map.tsv: expected columns {FIELDS}", file=sys.stderr)
            return 1
        for line, row in enumerate(reader, 2):
            prefix = f"port-map.tsv:{line} {row['program']} {row['checked_name']}:"
            if None in row or any(value is None for value in row.values()):
                errors.append(f"{prefix} wrong number of columns")
                continue
            address = row["address"].lower()
            identity = (row["program"], address)
            if identity in seen:
                errors.append(f"{prefix} duplicate program/address")
            seen.add(identity)
            if row["program"] not in registries:
                errors.append(f"{prefix} unexpected program")
            if registries.get(row["program"], {}).get(address) != row["checked_name"]:
                errors.append(f"{prefix} name/address differs from the checked registry")
            if row["status"] not in STATUSES:
                errors.append(f"{prefix} invalid status {row['status']!r}")
            if row["coverage"] not in COVERAGE:
                errors.append(f"{prefix} invalid coverage {row['coverage']!r}")
            if row["reviewed"] not in REVIEWED:
                errors.append(f"{prefix} reviewed must be yes or no")
            if (row["status"] == "omitted") != (row["coverage"] == "none"):
                errors.append(f"{prefix} omitted status requires coverage=none")
            remaining = row["remaining_work"].strip()
            if (row["coverage"] == "complete" and remaining != "-") or (
                row["coverage"] != "complete" and (not remaining or remaining == "-")
            ):
                errors.append(f"{prefix} remaining_work disagrees with coverage")
            if not row["evidence"].strip():
                errors.append(f"{prefix} evidence is empty")
            if row["status"] in {"adapted", "replaced"} and not row["adaptation"].strip():
                errors.append(f"{prefix} adaptation is empty")
            block = row["source_block"].split(":", 1)
            if len(block) != 2 or block[0] not in {"proven", "candidate", "unknown"}:
                errors.append(f"{prefix} source_block lacks provenance status")
            elif not (ROOT / block[1]).is_file():
                errors.append(f"{prefix} source_block file is missing: {block[1]}")
            if row["status"] == "omitted":
                if row["cpp_file"] or row["cpp_symbol"]:
                    errors.append(f"{prefix} omitted row has a C++ location")
            else:
                cpp_file = (ROOT / row["cpp_file"]).resolve()
                if not cpp_file.is_relative_to(ROOT) or not cpp_file.is_file():
                    errors.append(f"{prefix} C++ file is missing or outside the repo")
                else:
                    symbol = row["cpp_symbol"].split("::")[-1]
                    if not re.search(
                        rf"\b{re.escape(symbol)}\s*\(", cpp_file.read_text(encoding="utf-8")
                    ):
                        errors.append(f"{prefix} C++ symbol is missing from {row['cpp_file']}")
            counts[row["program"]][row["coverage"]] += 1
            if row["reviewed"] == "yes":
                review_counts[row["program"]] += 1
            if row["program"] in {"WINDREAM.EXE", "GDIDREAM.EXE"}:
                twins[(address, row["checked_name"])].append(row)
    for (address, name), rows in twins.items():
        if {row["program"] for row in rows} != {"WINDREAM.EXE", "GDIDREAM.EXE"}:
            errors.append(f"{address} {name}: Windows twin mapping is missing")
        elif (
            len(
                {
                    (
                        row["cpp_symbol"],
                        row["status"],
                        row["coverage"],
                        row["remaining_work"],
                        row["reviewed"],
                    )
                    for row in rows
                }
            )
            != 1
        ):
            errors.append(f"{address} {name}: Windows twin mapping or review differs")
    for program in sorted(counts):
        coverage = counts[program]
        print(
            f"{program}: {sum(coverage.values())} mapped; "
            f"{coverage['complete']} complete, {coverage['partial']} partial, "
            f"{coverage['unverified']} unverified, {coverage['none']} none; "
            f"{review_counts[program]} reviewed"
        )
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(validate())
