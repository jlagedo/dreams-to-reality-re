"""Inventory original Windows rendering content and its project/caller routes.

Reads configured extracted discs without modifying them. Generated asset hashes,
block/node evidence and route candidates go to DREAMS_OUT/recomp/render-content-inventory.
Static occurrence establishes source coverage, never that gameplay exercised a mode.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from collections import Counter, defaultdict
from dataclasses import asdict
from pathlib import Path

from dreams import paths
from dreams.formats import lz, node, project, rig, scene

DIRECT_MODES = {-7, -6, -5, -4, -3, 1, 2, 3, 9, 0x16, 0x17, 0x18}
FLAT_MODES = {-2, 1, 4, 0x11, 0x1B}
WINDOWS_MODES = set(range(-15, -1)) | {
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    0x11,
    0x12,
    0x14,
    0x15,
    0x16,
    0x17,
    0x18,
    0x19,
    0x1A,
    0x1B,
    0x1C,
    0x1D,
    0x1E,
}


def word(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"word outside source record: {offset:#x}/{len(data):#x}")
    return struct.unpack_from("<I", data, offset)[0]


def signed(data: bytes, offset: int) -> int:
    value = word(data, offset)
    return value if value < 0x80000000 else value - 0x100000000


def cell(data: bytes, offset: int, length: int = 16) -> str:
    if offset < 0 or offset + length > len(data):
        raise ValueError("string outside source record")
    return data[offset : offset + length].split(b"\0", 1)[0].decode("latin-1")


def graph_inventory(data: bytes) -> dict:
    """Walk the loader's actual directory and block chains, not a face signature scan."""
    nodes = rig.read_rig(data)
    delta = node.address_delta(node.find_nodes(data))
    blocks, node_rows = [], []
    totals: Counter[int] = Counter()
    vertex_ranges = [(word(data, n.offset + 0x94), word(data, n.offset + 0x90)) for n in nodes]
    truncated_tails = 0
    for entry in nodes:
        offset = entry.offset
        flags, lights = word(data, offset + 0x20), word(data, offset + 0xD8)
        node_rows.append(
            {
                "slot": entry.slot,
                "name": entry.name,
                "offset": offset,
                "parent": entry.parent,
                "flags": flags,
                "light_count": lights,
                "environment_flag": bool(flags & 0x800),
                "visibility_note": "source flags only; runtime setup can hide nodes",
                "vertices": word(data, offset + 0x90),
            }
        )
        block = word(data, offset + 0xB8)
        visited = set()
        while block:
            if block in visited or len(visited) >= 10000:
                raise ValueError("cyclic/oversized source block chain")
            visited.add(block)
            at = block + delta
            mode, count = signed(data, at + 4), word(data, at + 0x1C)
            start, stride = word(data, at + 0x20) + delta, word(data, at + 0x2C)
            if count > 20000 or (count and stride != (56 if mode in FLAT_MODES else 68)):
                raise ValueError(f"invalid source face count/stride: {mode}/{count}/{stride}")
            dynamic_normals = cross_node = 0
            for i in range(count):
                face_at = start + i * stride
                required = 0x34 if stride == 56 else 0x40
                if face_at < 0 or face_at + required > len(data):
                    raise ValueError("face consumed prefix outside source record")
                truncated_tails += face_at + stride > len(data)
                dynamic_normals += bool(word(data, face_at) & 8)
                owners = []
                for c in range(3):
                    ref = word(data, face_at + 8 + 12 * c)
                    owner = next(
                        (
                            index
                            for index, (base, size) in enumerate(vertex_ranges)
                            if base <= ref < base + size * 40 and (ref - base) % 40 == 0
                        ),
                        None,
                    )
                    if owner is None:
                        raise ValueError(f"unresolved corner pointer {ref:#x}")
                    owners.append(owner)
                cross_node += len(set(owners)) > 1
            blocks.append(
                {
                    "owner": entry.slot,
                    "source_offset": at,
                    "mode": mode,
                    "material": cell(data, at + 0xC),
                    "faces": count,
                    "stride": stride,
                    "dynamic_normal_faces": dynamic_normals,
                    "cross_node_faces": cross_node,
                }
            )
            totals[mode] += count
            block = word(data, at)
    materials_at = 0x18 + len(nodes) * 4
    material_count = word(data, materials_at)
    if material_count > 256:
        raise ValueError("oversized source material directory")
    materials = [
        {
            "name": cell(data, materials_at + 4 + i * 44),
            "file": cell(data, materials_at + 20 + i * 44),
            "colour": word(data, materials_at + 36 + i * 44),
        }
        for i in range(material_count)
    ]
    return {
        "nodes": node_rows,
        "blocks": blocks,
        "face_modes": dict(sorted(totals.items())),
        "materials": materials,
        "faces": sum(totals.values()),
        "source_faces_with_omitted_unused_tail": truncated_tails,
    }


def inspect_asset(path: Path, asset_id: str) -> dict:
    data = path.read_bytes()
    kind = path.suffix[1:].lower()
    header = scene.read_dsn(path) if kind == "dsn" else scene.read_dan(path)
    records = scene.read_records(path, kind)
    if (
        not header.size_ok
        or not header.span_ok
        or sum(r.size for r in records) != len(data) - header.body_offset
    ):
        raise ValueError("source header/record chain does not account for the whole file")
    models = [graph_inventory(lz.decompress(r.payload)) for r in records if r.tag == 1]
    if not models:
        raise ValueError("source asset has no tag-1 geometry")
    banks = []
    if kind == "dan":
        for record in records:
            if record.tag == 2:
                bank = lz.decompress(record.payload)
                banks.append(
                    {
                        "record_offset": record.offset,
                        "bytes": len(bank),
                        "palette_rows": 32 if len(bank) == node.TEX_TOTAL else None,
                        "page_dimensions": [256, 256] if len(bank) == node.TEX_TOTAL else None,
                        "sha256": hashlib.sha256(bank).hexdigest(),
                    }
                )
    return {
        "id": asset_id,
        "name": path.name.upper(),
        "kind": kind,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "source_names": header.names,
        "record_tags": dict(sorted(Counter(r.tag for r in records).items())),
        "models": models,
        "texture_banks": banks,
        "dsn_palette_records": sum(r.tag == 3 for r in records) if kind == "dsn" else 0,
        "dsn_tile_records": sum(r.tag == 4 for r in records) if kind == "dsn" else 0,
        "animation_frames": len(header.frame_refs) if kind == "dan" else 0,
        "project_references": [],
    }


def enrich_dynamic_evidence(report: dict, output: Path) -> None:
    evidence = []
    pattern = re.compile(r"/\* ([\w_]+) @ ([0-9a-fA-F]{8})\s+body (\d+) bytes \*/")
    for name in (
        "dynamic-setters-ghidra.txt",
        "dynamic-mode-callers-ghidra.txt",
        "dynamic-mode-roots-ghidra.txt",
        "material-mode-dispatch-ghidra.txt",
    ):
        path = output.parent / name
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        markers = list(pattern.finditer(text))
        seen = set()
        for i, marker in enumerate(markers):
            function, address, size = marker.groups()
            if address in seen:
                continue
            seen.add(address)
            body = text[
                marker.end() : markers[i + 1].start() if i + 1 < len(markers) else len(text)
            ]
            callers = re.search(r"// callers: (.*?)\s*\(GhidraScript\)", body)
            evidence.append(
                {
                    "address": address,
                    "function": function,
                    "body_bytes": int(size),
                    "direct_callers": callers.group(1).strip() if callers else "unknown",
                    "source": str(path),
                    "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "limit": "Direct calls only; indirect/control entry needs route evidence.",
                }
            )
    report["dynamic_setter_evidence"] = evidence
    report["dynamic_route_candidates"] = [
        {
            "name": "natural effect-light lifetime",
            "chain": ["0042d824", "0042ee5e", "0042bcb9", "0042bb85", "00477e14"],
            "writers": ["00477cb0", "00477da0", "00477d78"],
            "trigger": (
                "Actor +0xab bit 8; player range uses project +0xcc or 2000. "
                "Attack spawn/tick also adds/removes."
            ),
            "passing_check": (
                "Reach naturally, move/remove while multiple slots exist, "
                "verify bound-node shading and stale-slot safety."
            ),
        },
        {
            "name": "class mutations",
            "writers": ["004575d4", "00457688", "004577cc"],
            "trigger": (
                "Shadow, clone and attack callers reach 0x4577cc. "
                "0x41ce67 class dispatcher selects -7/9, no direct callers found."
            ),
            "passing_check": (
                "Exercise real shadows, clones and attack objects; log mutated mode "
                "and source stride; static mode inventory is not exhaustive."
            ),
        },
        {
            "name": "environment/mirror toggles",
            "writers": ["004574b4", "004574d0"],
            "trigger": (
                "Write node flags 0x800/0x1000; no direct callers found in saved Windows analysis."
            ),
            "passing_check": (
                "Resolve supported control/indirect callers before implementing "
                "more natural routes; retain controlled existing tests."
            ),
        },
    ]
    banks = {}
    for row in report["projects"]:
        bank = row["bank"]
        if bank not in banks:
            disc, relative = bank.split("/", 1)
            path = Path(report["sources"][int(disc[4:]) - 1]) / relative
            banks[bank] = project.records(path)
        raw = banks[bank][row["index"]]
        row["effect_light_flags"] = word(raw, 0xC8)
        row["player_light_outer_radius"] = signed(raw, 0xCC)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--disc1", type=Path, default=paths.configured("disc1"))
    parser.add_argument("--disc2", type=Path, default=paths.configured("disc2"))
    parser.add_argument(
        "--refresh-callers",
        action="store_true",
        help="enrich an existing inventory with saved Ghidra caller evidence",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=paths.get("out") / "recomp" / "render-content-inventory" / "inventory.json",
    )
    args = parser.parse_args()
    if args.refresh_callers:
        report = json.loads(args.output.read_text(encoding="utf-8"))
        enrich_dynamic_evidence(report, args.output)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Refreshed {len(report['dynamic_setter_evidence'])} caller entries: {args.output}")
        return 0
    roots = [args.disc1, args.disc2]
    if any(root is None or not root.is_dir() for root in roots):
        parser.error("both original extracted disc roots must exist")
    report = {
        "schema": 1,
        "scope": "static original content and caller inventory, not live coverage",
        "sources": [str(root.resolve()) for root in roots],
        "assets": [],
        "movies": [],
        "projects": [],
        "failures": [],
        "caller_routes": [],
        "modes": [],
    }
    by_name: dict[str, list[dict]] = defaultdict(list)
    dat_files = []
    for disc, root in enumerate(roots, 1):
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(root).as_posix()
            asset_id = f"disc{disc}/{relative}"
            if path.suffix.lower() in {".dsn", ".dan"}:
                try:
                    asset = inspect_asset(path, asset_id)
                except (ValueError, IndexError, struct.error) as exc:
                    report["failures"].append({"id": asset_id, "error": str(exc)})
                    continue
                report["assets"].append(asset)
                by_name[path.name.upper()].append(asset)
            elif path.name.upper() == "DREAMS.DAT":
                dat_files.append((asset_id, path))
            elif path.suffix.lower() in {".hnm", ".ubb"}:
                with path.open("rb") as stream:
                    header = stream.read(64)
                report["movies"].append(
                    {
                        "id": asset_id,
                        "name": path.name.upper(),
                        "bytes": path.stat().st_size,
                        "header_hex": header.hex(),
                    }
                )
        print(f"disc {disc}: {len(report['assets'])} geometry files inspected", flush=True)
    for bank_id, path in dat_files:
        raw_records = project.records(path)
        for index, raw in enumerate(raw_records):
            parsed = project.parse(index, raw)
            row = {
                "bank": bank_id,
                "index": index,
                "name": parsed.name,
                "spawn": parsed.spawn_position,
                "links": [asdict(link) for link in parsed.links],
                "objects": [],
                "events": [asdict(event) for event in parsed.advents],
                "project_movie": cell(raw, 0x3C),
                "animated_material": cell(raw, 0x4C),
                "animated_video": cell(raw, 0x5C),
                "scroll_material": cell(raw, 0x6C),
                "blend_material": cell(raw, 0x7C),
                "player_model_override": cell(raw, 0x8C, 32),
                "palette_lighting_mode": parsed.lighting_mode,
                "fog_rgb_density": struct.unpack_from("<4i", raw, 0x1C0),
                "water_height": signed(raw, 0xD4),
            }
            for obj in parsed.objets:
                candidates = by_name.get(obj.asset.upper(), [])
                entry = asdict(obj)
                entry["asset_candidates"] = [asset["id"] for asset in candidates]
                row["objects"].append(entry)
                for asset in candidates:
                    asset["project_references"].append(
                        {"bank": bank_id, "project": index, "slot": obj.name, "flags": obj.flags}
                    )
            report["projects"].append(row)
    observed_modes = {
        mode
        for asset in report["assets"]
        for model in asset["models"]
        for mode in model["face_modes"]
    }
    for mode in sorted(set(range(-15, 31)) | observed_modes):
        occurrences = []
        for asset in report["assets"]:
            for model in asset["models"]:
                for block in model["blocks"]:
                    if block["mode"] == mode and block["faces"]:
                        occurrences.append({"asset": asset["id"], **block})
        report["modes"].append(
            {
                "mode": mode,
                "direct_support": "implemented"
                if mode in DIRECT_MODES
                else "shadow-only"
                if mode == 0x1B
                else "unsupported",
                "windows_dispatch": mode in WINDOWS_MODES,
                "windows_behavior": "visible dispatch"
                if mode in WINDOWS_MODES
                else "default no-draw branch",
                "status": "required route not yet exercised",
                "static_faces": sum(block["faces"] for block in occurrences),
                "blocks": occurrences,
                "passing_check": (
                    "Matched live source/caller route with strict audit and useful visible output; "
                    "static absence is not a waiver."
                ),
            }
        )
    report["caller_routes"] = [
        {
            "route": name,
            "caller": address,
            "source": source,
            "status": "required route not yet exercised",
            "passing_check": check,
        }
        for name, address, source, check in [
            (
                "Walker proxy-node visibility",
                "0040bedb",
                "re/names/WINDREAM.EXE.tsv",
                "PHYS_AttachActorCollider hides ZZZZZ/BASSIN01 on walkers; "
                "stored type1 proxy boxes are not proof of visible gameplay geometry.",
            ),
            (
                "Windows face-mode dispatch",
                "00473014/004731b8",
                "out/recomp/windows-modes-006.txt",
                "Exercise each required visible dispatch or establish its intentional Windows "
                "no-draw/unreachable branch.",
            ),
            (
                "Lit faces and callback metadata",
                "0047b7e0",
                "docs/specs/006-recomp-glide-renderer/debug-renderer-contracts.md",
                "Naturally bind/unbind supported lights and verify pose/metadata/callback "
                "ordering plus visible shading.",
            ),
            (
                "Environment UV versions",
                "0047e094",
                "docs/specs/006-recomp-glide-renderer/implementation.md",
                "Exercise natural environment assets, shared opaque/deferred UV updates "
                "and offscreen interleave.",
            ),
            (
                "Mirror setup",
                "0047e824",
                "out/recomp/windows-mode-callees-006.txt",
                "Identify actual supported writer/caller route; no callers found is not proof "
                "against indirect entry.",
            ),
            (
                "Animated material pixels",
                "0042dae2",
                "opendreams/shared/port/level_materials.cpp",
                "Project +0x4c/+0x5c material/video changes remain visible "
                "with zero routine readbacks.",
            ),
            (
                "Page scroll/blend",
                "0042dea3/0042dfa0",
                "opendreams/shared/port/palette_lighting.cpp",
                "Exercise project +0x6c/+0x7c source versions over successive frames.",
            ),
            (
                "Collector and collision lines",
                "00478800/00465c80",
                "docs/specs/005-debug-tools/spec.md",
                "Keypad3 plus Backspace freezes scene and accumulates readable GPU lines; "
                "arrays bounded, no unsafe accesses.",
            ),
        ]
    ]
    totals = Counter()
    for asset in report["assets"]:
        for model in asset["models"]:
            totals.update(model["face_modes"])
    for movie in report["movies"]:
        references = []
        for row in report["projects"]:
            for field in ("project_movie", "animated_video"):
                if row[field].upper() == movie["name"]:
                    references.append(
                        {"bank": row["bank"], "project": row["index"], "consumer": field}
                    )
            for event in row["events"]:
                if event["cutscene_video"].upper() == movie["name"]:
                    references.append(
                        {
                            "bank": row["bank"],
                            "project": row["index"],
                            "consumer": event["name"],
                            "event_opcode": event["event_opcode"],
                            "condition_stage": event["condition_stage"],
                        }
                    )
        movie["project_references"] = references
    report["summary"] = {
        "physical_geometry_files": len(report["assets"]),
        "by_kind": dict(Counter(asset["kind"] for asset in report["assets"])),
        "unique_geometry_hashes": len({asset["sha256"] for asset in report["assets"]}),
        "project_banks": len(dat_files),
        "project_records": len(report["projects"]),
        "movie_files": len(report["movies"]),
        "physical_face_modes": dict(sorted(totals.items())),
        "failures": len(report["failures"]),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    enrich_dynamic_evidence(report, args.output)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))
    print(f"Report: {args.output}")
    return int(bool(report["failures"]))


if __name__ == "__main__":
    raise SystemExit(main())
