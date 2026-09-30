"""Prove whole-body, relocation-equivalent DOS-demo -> Windows identities.

Run with ``uv run --with capstone python tools/check_wip_windows.py --demo DIR``.
Requires ExportBodyHashes.java for WIP_DREAMS.EXE, WINDREAM.EXE and GDIDREAM.EXE.
Preserves all non-address bytes, relocation roles and internal branch offsets;
requires mutually unique bodies, coherent external address mappings, and direct
control-flow destinations at known function entries. Does not claim that global
initial values, callees, layouts or runtime behavior are identical across builds.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter, defaultdict
from functools import lru_cache
from pathlib import Path

from relocated_image import Image
from watcom_debug import read_debug

from dreams import paths

ROOT = Path(__file__).resolve().parents[1]


@lru_cache(maxsize=1)
def disassembler():
    from capstone import CS_ARCH_X86, CS_MODE_32, Cs

    result = Cs(CS_ARCH_X86, CS_MODE_32)
    result.detail = True
    return result


def fingerprint(
    image: Image, entry: int, size: int, *, normalize_index_bias: bool = False
) -> tuple[tuple, list[tuple]] | None:
    from capstone import CS_GRP_CALL, CS_GRP_JUMP
    from capstone.x86 import X86_OP_IMM

    if any(entry <= address < entry + size for address in image.unsupported):
        return None
    code = image.read(entry, size)
    if len(code) != size:
        return None
    instructions = list(disassembler().disasm(code, entry))
    if sum(i.size for i in instructions) != size:
        return None
    starts = {i.address for i in instructions}
    biases = {}
    if normalize_index_bias:
        from watcom_patterns import preincremented_loop_biases

        biases = preincremented_loop_biases(instructions)
    patterns, edges, used_relocs = [], [], set()
    for ins in instructions:
        normal = bytearray(ins.bytes)
        masks, internal = [], []
        for role, offset, width in (
            ("immediate", ins.imm_offset, ins.imm_size),
            ("displacement", ins.disp_offset, ins.disp_size),
        ):
            if offset and width == 4 and ins.address + offset in image.relocs:
                target = int.from_bytes(normal[offset : offset + width], "little")
                target += biases.get(ins.address + offset, 0)
                edges.append(("abs", target))
                masks.append((role, offset, width))
                normal[offset : offset + width] = bytes(width)
                used_relocs.add(ins.address + offset)
        if (ins.group(CS_GRP_CALL) or ins.group(CS_GRP_JUMP)) and (
            ins.operands and ins.operands[0].type == X86_OP_IMM
        ):
            offset, width = ins.imm_offset, ins.imm_size
            target = ins.operands[0].imm & 0xFFFFFFFF
            role = "call" if ins.group(CS_GRP_CALL) else "branch"
            masks.append((role, offset, width))
            normal[offset : offset + width] = bytes(width)
            if entry <= target < entry + size:
                if target not in starts:
                    return None
                internal.append((role, target - entry))
            else:
                edges.append((role, target))
        patterns.append((bytes(normal), tuple(masks), tuple(internal)))
    expected_relocs = {r for r in image.relocs if entry <= r < entry + size}
    if used_relocs != expected_relocs:
        return None
    return tuple(patterns), edges


def bodies(program: str, image: Image) -> dict[int, dict]:
    file = ROOT / "out/ghidra/bodies" / f"{program}.tsv"
    lines = file.read_text(encoding="utf-8").splitlines()
    if lines[0] != "#sha256\t" + hashlib.sha256(image.raw).hexdigest():
        raise ValueError(f"{program}: Ghidra source SHA-256 does not match executable")
    return {int(r["address"], 16): r for r in csv.DictReader(lines[1:], delimiter="\t")}


def profiles(image: Image, ranges: dict[int, dict]) -> dict[int, dict]:
    result = {}
    for entry, row in ranges.items():
        size = int(row["size"])
        if row["ranges"] != f"{entry:08x}-{entry + size - 1:08x}":
            continue
        raw = image.read(entry, size)
        if hashlib.sha256(raw).hexdigest() != row["sha256"]:
            raise ValueError(f"Loaded image differs from Ghidra memory at {entry:08x}")
        decoded = fingerprint(image, entry, size)
        if decoded is not None and len(decoded[0]) >= 8:
            result[entry] = {"size": size, "key": decoded[0], "edges": decoded[1], **row}
    return result


def address_conflicts(rows: list[dict], source_entries: set, target_entries: set) -> None:
    forward, backward = defaultdict(set), defaultdict(set)
    for row in rows:
        pairs = [("entry", row["source_entry"], row["entry"]), *row["edges"]]
        for _, source, target in pairs:
            forward[source].add(target)
            backward[target].add(source)
    for row in rows:
        errors = list(row.get("boundary_problems", []))
        for kind, source, target in row["edges"]:
            if len(forward[source]) != 1:
                errors.append(f"one source address has multiple targets: {source:08x}")
            if len(backward[target]) != 1:
                errors.append(f"multiple source addresses share a target: {target:08x}")
            if kind in ("call", "branch") and (
                source not in source_entries or target not in target_entries
            ):
                errors.append(f"external {kind} is not between entries: {source:08x}/{target:08x}")
        row["problems"] = sorted(set(errors))


def audit(demo: Path) -> dict:
    source = Image(demo / "DREAMS.EXE")
    target = Image(paths.disc(1) / "WINDREAM.EXE")
    twin = Image(paths.disc(1) / "GDIDREAM.EXE")
    source_bodies = bodies("WIP_DREAMS.EXE", source)
    target_bodies = bodies("WINDREAM.EXE", target)
    twin_bodies = bodies("GDIDREAM.EXE", twin)
    src, dst = profiles(source, source_bodies), profiles(target, target_bodies)
    by_source, by_target = defaultdict(list), defaultdict(list)
    for entry, profile in src.items():
        by_source[profile["key"]].append(entry)
    for entry, profile in dst.items():
        by_target[profile["key"]].append(entry)
    symbol_rows = [r for r in read_debug(source.raw)[1] if r["kind"] & 4]
    symbols = defaultdict(list)
    name_counts = Counter(r["name"] for r in symbol_rows)
    for symbol in symbol_rows:
        symbols[int(symbol["address"], 16)].append(symbol)
    functions = json.loads((ROOT / "out/ghidra/features/WINDREAM.EXE.json").read_text())
    names = {int(f["entry"], 16): f["name"] for f in functions}
    rows = []
    for key, entries in by_source.items():
        targets = by_target.get(key, [])
        if len(entries) != 1 or len(targets) != 1:
            continue
        x, y = entries[0], targets[0]
        sa, sb = src[x], dst[y]
        size = int(sb["size"])
        twin_row = twin_bodies.get(y)
        if not twin_row or any(twin_row[k] != sb[k] for k in ("size", "ranges", "sha256")):
            continue
        if twin.read(y, size) != target.read(y, size):
            raise ValueError("Windows twin bytes differ")
        edges = []
        for (ka, a), (kb, b) in zip(sa["edges"], sb["edges"], strict=True):
            if ka != kb:
                raise ValueError("operand roles differ despite equal fingerprints")
            edges.append((ka, a, b))
        original = symbols.get(x, [])
        symbol = (
            original[0] if len(original) == 1 and name_counts[original[0]["name"]] == 1 else None
        )
        module = symbol["module"] if symbol else ""
        game = "DREAMS\\src" in module or "BEN11\\" in module or module.lower().startswith("src\\")
        rows.append(
            {
                "source_entry": x,
                "entry": y,
                "size": size,
                "instructions": len(key),
                "name": symbol["name"] if symbol else "",
                "module": module,
                "current_name": names[y],
                "game": game,
                "source_body_sha256": sa["sha256"],
                "target_body_sha256": sb["sha256"],
                "edges": edges,
                "boundary_problems": [
                    f"source body crosses debug code symbol: {address:08x}"
                    for address in symbols
                    if x < address < x + size
                ],
            }
        )
    address_conflicts(rows, set(source_bodies), set(target_bodies))
    return {
        "source_sha256": hashlib.sha256(source.raw).hexdigest(),
        "target_sha256": hashlib.sha256(target.raw).hexdigest(),
        "twin_sha256": hashlib.sha256(twin.raw).hexdigest(),
        "rows": rows,
    }


def check_manifest(result: dict, manifest: dict) -> None:
    for field in ("source_sha256", "target_sha256", "twin_sha256"):
        if result[field] != manifest[field]:
            raise ValueError(f"changed executable: {field}")
    current = {r["entry"]: r for r in result["rows"] if r["game"] and not r["problems"]}
    for row in manifest["rows"]:
        actual = current.get(row["entry"])
        if actual is None:
            raise ValueError(f"transfer is no longer accepted: {row['entry']:08x}")
        for key in (
            "source_entry",
            "size",
            "name",
            "module",
            "source_body_sha256",
            "target_body_sha256",
        ):
            if row[key] != actual[key]:
                raise ValueError(f"transfer evidence differs: {row['entry']:08x} {key}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument(
        "--out", type=Path, default=ROOT / "out/research/wip_pcj/windows-transfer-audit.json"
    )
    parser.add_argument("--check", type=Path)
    args = parser.parse_args()
    result = audit(args.demo)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    passed = [r for r in result["rows"] if r["game"] and not r["problems"]]
    unnamed = [r for r in passed if r["current_name"].startswith("FUN_")]
    held = [r for r in result["rows"] if r["game"] and r["problems"]]
    print(
        f"{len(result['rows'])} unique bodies; "
        f"{len(passed)} game/engine accepted; {len(unnamed)} unnamed"
    )
    for row in held:
        print(f"HELD {row['entry']:08x} {row['name']}: {'; '.join(row['problems'])}")
    if args.check:
        check_manifest(result, json.loads(args.check.read_text(encoding="utf-8")))
        print(f"Manifest verified: {args.check}")


if __name__ == "__main__":
    main()
