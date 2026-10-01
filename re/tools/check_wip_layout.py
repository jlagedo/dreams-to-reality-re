"""Validate additional symbol transfers using exact code plus linker layout.

Uses original Watcom contribution records to constrain ambiguous/short bodies,
previously verified address and caller relationships, instruction-proven biased
index loops, and matched shared tails. It never names by ordinal position alone.
Run with ``uv run --with capstone python re/tools/check_wip_layout.py --demo DIR``.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path

from check_wip_windows import bodies, fingerprint
from relocated_image import Image
from watcom_debug import read_contributions, read_debug

from dreams import paths

ROOT = Path(__file__).resolve().parents[2]


def profiles(image, ranges):
    result = {}
    for entry, row in ranges.items():
        size = int(row["size"])
        if row["ranges"] != f"{entry:08x}-{entry + size - 1:08x}":
            continue
        data = image.read(entry, size)
        if hashlib.sha256(data).hexdigest() != row["sha256"]:
            raise ValueError(f"Ghidra loaded bytes differ at {entry:08x}")
        decoded = fingerprint(image, entry, size, normalize_index_bias=True)
        if decoded:
            offsets, offset = [], 0
            for item in decoded[0]:
                offsets.append(offset)
                offset += len(item[0])
            result[entry] = {
                "size": size,
                "key": decoded[0],
                "edges": decoded[1],
                "offsets": offsets,
                "sha256": row["sha256"],
            }
    return result


def maps(rows):
    forward, reverse = defaultdict(set), defaultdict(set)
    for row in rows:
        for _, source, target in [("entry", row["source_entry"], row["entry"]), *row["edges"]]:
            forward[source].add(target)
            reverse[target].add(source)
    return forward, reverse


def validate_graph(rows, source_entries, target_entries, source, target):
    forward, reverse = maps(rows)
    for row in rows:
        row["problems"] = list(row["boundary_problems"])
        row["tails"] = []
        row["pending_tails"] = []
        for kind, a, b in row["edges"]:
            if len(forward[a]) > 1 or len(reverse[b]) > 1:
                row["problems"].append(f"conflicting address correspondence {a:08x}/{b:08x}")
            if kind in ("call", "branch") and (a not in source_entries or b not in target_entries):
                if kind == "branch" and source.read(a, 1) == target.read(b, 1) == b"\xc3":
                    row["tails"].append([a, b, "shared RET"])
                else:
                    row["pending_tails"].append((a, b))
    ready = {r["source_entry"] for r in rows if not r["problems"] and not r["pending_tails"]}
    changed = True
    while changed:
        changed = False
        for row in rows:
            if row["problems"] or row["source_entry"] in ready:
                continue
            unresolved = []
            for a, b in row["pending_tails"]:
                owner = next(
                    (
                        o
                        for o in rows
                        if o["source_entry"] in ready
                        and 0 <= a - o["source_entry"] < o["size"]
                        and a - o["source_entry"] == b - o["entry"]
                        and a - o["source_entry"] in o["offsets"]
                    ),
                    None,
                )
                if owner:
                    row["tails"].append(
                        [a, b, f"matched owner {owner['source_entry']:08x}/{owner['entry']:08x}"]
                    )
                else:
                    unresolved.append((a, b))
            row["pending_tails"] = unresolved
            if not unresolved:
                ready.add(row["source_entry"])
                changed = True
    for row in rows:
        row["problems"] += [f"unproved tail {a:08x}/{b:08x}" for a, b in row.pop("pending_tails")]


def audit(demo):
    source = Image(demo / "DREAMS.EXE")
    target = Image(paths.disc(1) / "WINDREAM.EXE")
    twin = Image(paths.disc(1) / "GDIDREAM.EXE")
    sb, tb, gb = (
        bodies("WIP_DREAMS.EXE", source),
        bodies("WINDREAM.EXE", target),
        bodies("GDIDREAM.EXE", twin),
    )
    src, dst = profiles(source, sb), profiles(target, tb)
    raw_symbols = [s for s in read_debug(source.raw)[1] if s["kind"] & 4]
    symbols = defaultdict(list)
    counts = Counter(s["name"] for s in raw_symbols)
    for s in raw_symbols:
        symbols[int(s["address"], 16)].append(s)
    contributions = [r for r in read_contributions(source.raw) if r["executable"]]

    def owner(entry):
        matches = [r for r in contributions if r["start"] <= entry < r["end"]]
        return matches[0]["module"] if len(matches) == 1 else ""

    owners = {e: owner(e) for e in src}
    by_src, by_dst = defaultdict(list), defaultdict(list)
    for e, p in src.items():
        by_src[p["key"]].append(e)
    for e, p in dst.items():
        by_dst[p["key"]].append(e)
    current = {
        int(f["entry"], 16): f
        for f in json.loads((ROOT / "out/ghidra/features/WINDREAM.EXE.json").read_text())
    }

    def pair(a, b, method, details):
        x, y = src[a], dst[b]
        if b not in gb or any(gb[b][k] != tb[b][k] for k in ("size", "ranges", "sha256")):
            return None
        if twin.read(b, y["size"]) != target.read(b, y["size"]):
            return None
        edges = []
        for (ka, sa), (kb, ta) in zip(x["edges"], y["edges"], strict=True):
            if ka != kb:
                raise ValueError("operand roles differ")
            edges.append((ka, sa, ta))
        syms = symbols.get(a, [])
        named = syms[0] if len(syms) == 1 and counts[syms[0]["name"]] == 1 else None
        errors, shared = [], []
        for address, extra in symbols.items():
            if a < address < a + x["size"]:
                offset = address - a
                if (
                    offset in x["offsets"]
                    and source.read(address, 1) == target.read(b + offset, 1) == b"\xc3"
                    and all(s["module"] == owners[a] for s in extra)
                ):
                    shared.append([address, [s["name"] for s in extra]])
                else:
                    errors.append(f"unresolved interior symbol {address:08x}")
        return {
            "source_entry": a,
            "entry": b,
            "name": named["name"] if named else "",
            "module": owners[a],
            "size": y["size"],
            "instructions": len(y["key"]),
            "offsets": x["offsets"],
            "edges": edges,
            "boundary_problems": errors,
            "shared_returns": shared,
            "source_body_sha256": x["sha256"],
            "target_body_sha256": y["sha256"],
            "current_name": current[b]["name"],
            "method": method,
            "details": details,
        }

    initial = []
    for key, aa in by_src.items():
        bb = by_dst.get(key, [])
        if len(key) >= 8 and len(aa) == len(bb) == 1:
            row = pair(aa[0], bb[0], "unique full body", {})
            if row:
                initial.append(row)
    validate_graph(initial, set(sb), set(tb), source, target)
    accepted = [r for r in initial if not r["problems"]]
    held = [r for r in initial if r["problems"]]
    for _iteration in range(20):
        forward, reverse = maps(accepted)
        taken_a, taken_b = {r["source_entry"] for r in accepted}, {r["entry"] for r in accepted}
        anchors, incoming = defaultdict(list), defaultdict(set)
        for r in accepted:
            anchors[r["module"]].append((r["source_entry"], r["entry"]))
            for k, a, b in r["edges"]:
                if k == "call":
                    incoming[a].add((b, r["entry"]))
        for values in anchors.values():
            values.sort()
        proposed = []
        for a, sa in src.items():
            if a in taken_a:
                continue
            names = symbols.get(a, [])
            if len(names) != 1 or counts[names[0]["name"]] != 1:
                continue
            points = anchors.get(owners[a], [])
            ordered = all(x[1] < y[1] for x, y in zip(points, points[1:], strict=False))
            i = bisect.bisect_left(points, (a, -1))
            bounds = [points[i - 1], points[i]] if ordered and 0 < i < len(points) else []
            hits = []
            for b in by_dst.get(sa["key"], []):
                if b in taken_b:
                    continue
                bracketed = bool(bounds and bounds[0][1] < b < bounds[1][1])
                if bounds and not bracketed:
                    continue
                known, bad = 0, False
                for (ka, x), (kb, y) in zip(sa["edges"], dst[b]["edges"], strict=True):
                    if ka != kb:
                        raise ValueError("operand mismatch")
                    if x in forward and forward[x] != {y}:
                        bad = True
                    if y in reverse and reverse[y] != {x}:
                        bad = True
                    if x in forward and forward[x] == {y}:
                        known += 1
                callers = sorted(f for destination, f in incoming[a] if destination == b)
                unique = len(by_src[sa["key"]]) == len(by_dst[sa["key"]]) == 1
                if not bad and (
                    (unique and (bracketed or known or callers))
                    or (bracketed and (known or callers))
                ):
                    hits.append(
                        (
                            b,
                            {
                                "brackets": bounds,
                                "known_operands": known,
                                "matched_callers": callers,
                                "globally_unique": unique,
                            },
                        )
                    )
            if len(hits) == 1:
                row = pair(a, hits[0][0], "layout and operand constrained", hits[0][1])
                if row:
                    proposed.append(row)
        destinations = Counter(r["entry"] for r in proposed)
        proposed = [r for r in proposed if destinations[r["entry"]] == 1]
        while proposed:
            validate_graph(accepted + proposed, set(sb), set(tb), source, target)
            good = [r for r in proposed if not r["problems"]]
            if len(good) == len(proposed):
                break
            held.extend(r for r in proposed if r["problems"])
            proposed = good
        validate_graph(accepted + proposed, set(sb), set(tb), source, target)
        if any(r["problems"] for r in accepted):
            raise ValueError("New pairs invalidate seed evidence")
        if not proposed:
            break
        accepted.extend(proposed)
    hashes = {
        "source_sha256": hashlib.sha256(source.raw).hexdigest(),
        "target_sha256": hashlib.sha256(target.raw).hexdigest(),
        "twin_sha256": hashlib.sha256(twin.raw).hexdigest(),
    }
    return {**hashes, "rows": accepted, "held": held, "contributions": contributions}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "out/research/wip_pcj/layout-audit.json")
    parser.add_argument("--check", type=Path)
    args = parser.parse_args()
    result = audit(args.demo)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    new = [r for r in result["rows"] if r["name"] and r["current_name"].startswith("FUN_")]
    print(f"{len(result['rows'])} accepted body identities; {len(new)} currently unnamed")
    if args.check:
        proof = json.loads(args.check.read_text())
        for key in ("source_sha256", "target_sha256", "twin_sha256"):
            if result[key] != proof[key]:
                raise ValueError(f"binary changed: {key}")
        by = {r["entry"]: r for r in result["rows"]}
        for row in proof["rows"]:
            actual = by.get(row["entry"])
            if actual is None:
                raise ValueError(f"identity no longer accepted: {row['entry']:08x}")
            for key in (
                "source_entry",
                "name",
                "module",
                "size",
                "source_body_sha256",
                "target_body_sha256",
            ):
                if actual[key] != row[key]:
                    raise ValueError(f"evidence differs: {row['entry']:08x} {key}")
        print("Layout manifest verified")
    for row in new:
        print(f"{row['entry']:08x} {row['name']} [{row['module']}]")


if __name__ == "__main__":
    main()
