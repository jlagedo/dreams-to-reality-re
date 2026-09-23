"""Reproducible animation library and complete rig/vertex bindings for the viewer."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from dreams.formats import animation, lz, node, scene
from dreams.formats.rig import read_rig


def skin_payload(path: Path) -> dict:
    raw = lz.decompress(next(r.payload for r in scene.read_records(path, "dan") if r.tag == 1))
    rig = read_rig(raw)
    model = node.read_model(path)
    slots = {nd.mesh_index: nd.slot for nd in rig if nd.mesh_index is not None}
    primitives = {}
    for group in sorted({face.group for face in model.faces}):
        bindings = []
        for face in model.faces:
            if face.group == group:
                for index, local in zip(face.node_indices, face.local_coords, strict=True):
                    bindings.append([slots[index], *local])
        primitives[group or path.stem.lower().rstrip("_")] = bindings
    payload = {
        "schemaVersion": 2,
        "model": path.stem.lower(),
        "assetStem": path.stem.lower().rstrip("_"),
        "nodes": [
            {
                "index": nd.slot,
                "name": nd.name,
                "parent": nd.parent,
                "translation": nd.translation,
                "rotation": nd.rotation,
                "hasGeometry": nd.mesh_index is not None,
            }
            for nd in rig
        ],
        "primitives": primitives,
    }
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    payload["rigId"] = hashlib.sha256(canonical.encode()).hexdigest()
    return payload


def bind_clip(clip: animation.AnimationClip, skin: dict) -> dict:
    data = clip.to_dict()
    data["schemaVersion"] = 2
    data["model"] = skin["model"]
    data["rigId"] = skin["rigId"]
    data["interpolation"] = "slerp-approximation"
    expected = list(range(len(skin["nodes"])))
    actual = [track.node_index for track in clip.tracks]
    reason = None
    if actual != expected:
        reason = (
            f"Clip has {len(actual)} tracks; rig has {len(expected)} slots. Binding unresolved."
        )
    elif any(t.bone_name != nd["name"] for t, nd in zip(clip.tracks, skin["nodes"], strict=True)):
        reason = "Track names do not match the model directory. Binding unresolved."
    data["bindingStatus"] = "unresolved" if reason else "verified-directory"
    data["bindingError"] = reason
    return data


def export_library(
    destination: Path, sources: list[Path], models_destination: Path | None = None
) -> dict:
    """Export complete rigs plus all clips; preserve unresolved clips as inspectable data."""
    destination.mkdir(parents=True, exist_ok=True)
    manifest, catalog, issues = {}, [], []
    total = playable = 0
    for path in sources:
        skin = skin_payload(path)
        if models_destination is not None:
            from dreams.gltf import from_model

            # Skin primitives must have exactly the same groups and corner order
            # as the rendered geometry; old monolithic exports are incompatible.
            from_model(path, models_destination)
        stem = skin["model"]
        if stem in manifest:
            raise ValueError(f"Duplicate animation model ID: {stem}")
        clips = animation.read_dan_animations(path)
        folder = destination / stem
        folder.mkdir(exist_ok=True)
        entries = []
        for clip in clips:
            data = bind_clip(clip, skin)
            clip_id = clip.name.lower().removesuffix(".3da")
            (folder / f"{clip_id}.json").write_text(json.dumps(data), encoding="utf-8")
            valid = data["bindingStatus"] == "verified-directory"
            entries.append(
                {
                    "id": clip_id,
                    "name": clip.name,
                    "duration": clip.duration_frames,
                    "trackCount": clip.track_count,
                    "frameRate": clip.frame_rate,
                    "playable": valid,
                    "bindingError": data["bindingError"],
                }
            )
            total += 1
            playable += valid
            if not valid:
                issues.append({"model": stem, "clip": clip.name, "reason": data["bindingError"]})
        (destination / f"{skin['assetStem']}_skin.json").write_text(
            json.dumps(skin), encoding="utf-8"
        )
        manifest[stem] = entries
        eligible = [e for e in entries if e["playable"]]
        default = next((e for e in eligible if e["id"].endswith("an000")), None)
        default = default or (eligible[0] if eligible else None)
        catalog.append(
            {
                "model": stem,
                "assetStem": skin["assetStem"],
                "rigId": skin["rigId"],
                "nodeCount": len(skin["nodes"]),
                "clipCount": len(clips),
                "playableClipCount": len(eligible),
                "defaultClipId": default["id"] if default else None,
            }
        )
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (destination / "catalog.json").write_text(json.dumps(catalog, indent=2), encoding="utf-8")
    report = {"models": len(catalog), "clips": total, "playable": playable, "issues": issues}
    (destination / "export-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report
