"""Compare animation decode hypotheses against independent asset evidence.

Run: uv run python -m dreams.animation_harness --out out/animation-harness

Translation fingerprints and engine conventions are stronger evidence than
humanoid plausibility. Diagnostic limits below are deliberately loose research
thresholds, not clinical limits or proof of correctness. No pose is clamped.
Rotations are checked at stored keys (no spline approximation in local tests).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from dataclasses import asdict, dataclass
from pathlib import Path

from dreams import paths
from dreams.formats import animation, lz, scene
from dreams.formats.rig import RigNode, read_rig

IDENTITY = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b, strict=True))


def unit(v):
    length = math.sqrt(dot(v, v))
    if length < 1e-12:
        raise ValueError("Cannot measure angle of a zero vector")
    return tuple(x / length for x in v)


def transpose(m):
    return tuple(zip(*m, strict=True))


def multiply(a, b):
    return tuple(tuple(dot(row, col) for col in transpose(b)) for row in a)


def apply(m, v):
    return tuple(dot(row, v) for row in m)


def rotation_matrix(q, convention="xyzw"):
    x, y, z, w = q
    if convention == "conjugate":
        x, y, z = -x, -y, -z
    elif convention == "wxyz":
        w, x, y, z = q
    elif convention != "xyzw":
        raise ValueError(convention)
    return (
        (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
        (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
        (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)),
    )


def hinge_angles(rotation, rest_direction, bend_direction):
    """Signed bend and departure from the rest/bend plane, in degrees.

    This is a directional test; it cannot diagnose twist around the limb axis.
    Rest axes come from the asset. Positive elbow bend is toward source +X;
    positive knee bend is toward -X. Those anatomical signs remain hypotheses.
    """
    axis = unit(rest_direction)
    bend = unit(
        tuple(b - dot(bend_direction, axis) * a for a, b in zip(axis, bend_direction, strict=True))
    )
    normal = (
        axis[1] * bend[2] - axis[2] * bend[1],
        axis[2] * bend[0] - axis[0] * bend[2],
        axis[0] * bend[1] - axis[1] * bend[0],
    )
    direction = unit(apply(rotation, axis))
    flex = math.degrees(math.atan2(dot(direction, bend), dot(direction, axis)))
    off_plane = math.degrees(math.asin(min(1.0, abs(dot(direction, normal)))))
    return flex, off_plane


@dataclass(frozen=True)
class JointLimit:
    name: str
    child: str
    bend: tuple[int, int, int]
    minimum: float
    maximum: float
    plane_maximum: float


LIMITS = (
    JointLimit("avbras-d", "main-d", (1, 0, 0), -15, 165, 35),
    JointLimit("avbras-g", "main-g", (1, 0, 0), -15, 165, 35),
    JointLimit("mollet-d", "pied-d", (-1, 0, 0), -15, 170, 35),
    JointLimit("mollet-g", "pied-g", (-1, 0, 0), -15, 170, 35),
)


def read_translations(buf: bytes):
    """Separate translation keys in each track, without normalizing values.

    +0x1c is the translation count; +0x24 is its array pointer. The difference
    between rotation and translation pointers gives the rotation array size.
    Stored pointers are relative to payload +20. Runtime evaluators at
    00459808 and 0045a03c use rotation/translation strides 20/16 and 60/48.
    """
    count = struct.unpack_from("<I", buf, 20)[0]
    result = []
    for slot in range(count):
        off = struct.unpack_from("<I", buf, 24 + slot * 4)[0]
        duration, rotations, translations, rp, tp = struct.unpack_from("<5I", buf, off + 20)
        if not rotations or (tp - rp) % rotations:
            raise ValueError(f"Slot {slot}: cannot establish rotation stride")
        stride = (tp - rp) // rotations
        if stride not in (20, 60) or rp + 20 != off + 40:
            raise ValueError(f"Slot {slot}: inconsistent rotation pointer/stride")
        pos_stride = {20: 16, 60: 48}[stride]
        start = tp + 20
        if start < 0 or start + translations * pos_stride > len(buf):
            raise ValueError(f"Slot {slot}: translation array exceeds payload")
        keys = [struct.unpack_from("<4i", buf, start + k * pos_stride) for k in range(translations)]
        if any(a[0] > b[0] for a, b in zip(keys, keys[1:], strict=False)):
            raise ValueError(f"Slot {slot}: nonmonotonic translation times")
        if any(k[0] < 0 or k[0] > duration for k in keys):
            raise ValueError(f"Slot {slot}: translation time outside duration")
        result.append(keys)
    return result


def target_slots(rig: list[RigNode], mapping: str):
    if mapping == "directory":
        return list(range(len(rig)))
    physical = sorted(rig, key=lambda nd: nd.offset)
    if mapping == "scan":
        return [nd.slot for nd in physical if nd.mesh_index is not None]
    if mapping == "alphabetical":
        return [nd.slot for nd in sorted(rig, key=lambda nd: nd.name)]
    raise ValueError(mapping)


def translation_fingerprint(rig, translations, targets):
    errors, mismatches = [], []
    for source, slot in enumerate(targets):
        if source >= len(translations) or not translations[source]:
            continue
        nd = rig[slot]
        # Root motion is legitimate, so only compare child local translations.
        if nd.parent == -1:
            continue
        value = translations[source][0][1:]
        error = math.dist(value, nd.translation)
        errors.append(error)
        if error > 1:
            mismatches.append({"track": source, "bone": nd.name, "error": round(error, 3)})
    return {
        "tested": len(errors),
        "within_one_unit": sum(e <= 1 for e in errors),
        "mean_error": round(sum(errors) / len(errors), 4) if errors else None,
        "worst": sorted(mismatches, key=lambda item: -item["error"])[:8],
    }


def evaluate_hypothesis(rig, clips, mapping, convention, space="local"):
    targets = target_slots(rig, mapping)
    by_name = {nd.name: nd for nd in rig}
    inverse = {slot: source for source, slot in enumerate(targets)}
    summaries, violations = [], []
    for limit in LIMITS:
        if limit.name not in by_name or limit.child not in by_name:
            continue
        nd, child = by_name[limit.name], by_name[limit.child]
        if child.parent != nd.slot or nd.slot not in inverse:
            raise ValueError(f"Unexpected hierarchy for {limit.name}")
        bends, planes = [], []
        count = 0
        for clip in clips:
            track = clip.tracks[inverse[nd.slot]]
            for key in track.keyframes:
                rotation = rotation_matrix(key.rotation, convention)
                if space == "world" and nd.parent != -1:
                    # This hypothesis needs the parent's sample at the same time.
                    # Between its keys, use the existing SLERP approximation.
                    parent_source = inverse.get(nd.parent)
                    if parent_source is not None:
                        parent_q = clip.sample_pose(key.time)[parent_source]
                        rotation = multiply(
                            transpose(rotation_matrix(parent_q, convention)), rotation
                        )
                bind = tuple(tuple(v / 32768 for v in row) for row in nd.rotation)
                relative = multiply(transpose(bind), rotation)
                bend, plane = hinge_angles(relative, child.translation, limit.bend)
                bends.append(bend)
                planes.append(plane)
                excess = max(limit.minimum - bend, 0, bend - limit.maximum)
                excess = max(excess, plane - limit.plane_maximum)
                if excess > 0:
                    count += 1
                    violations.append(
                        {
                            "clip": clip.name,
                            "frame": key.time,
                            "bone": nd.name,
                            "source_track": track.node_index,
                            "bend": round(bend, 2),
                            "off_plane": round(plane, 2),
                            "excess": round(excess, 2),
                        }
                    )
        summaries.append(
            {
                "bone": nd.name,
                "keys": len(bends),
                "violations": count,
                "bend_min": round(min(bends), 2),
                "bend_max": round(max(bends), 2),
                "off_plane_max": round(max(planes), 2),
            }
        )
    total = sum(s["keys"] for s in summaries)
    return {
        "id": f"{mapping}/{convention}/{space}",
        "keys": total,
        "violations": len(violations),
        "violation_percent": round(100 * len(violations) / total, 2) if total else None,
        "joints": summaries,
        "worst": sorted(violations, key=lambda item: -item["excess"])[:12],
    }


def investigate(path: Path):
    records = scene.read_records(path, "dan")
    raw_model = lz.decompress(next(r.payload for r in records if r.tag == 1))
    rig = read_rig(raw_model)
    clips = animation.read_dan_animations(path)
    payloads = [lz.decompress(r.payload) for r in records if r.tag == 3]
    if any(c.track_count != len(rig) for c in clips):
        raise ValueError("Animation count differs from explicit model directory")
    translations = [read_translations(b) for b in payloads]
    fingerprints = {}
    for mapping in ("directory", "scan", "alphabetical"):
        targets = target_slots(rig, mapping)
        rows = [translation_fingerprint(rig, tr, targets) for tr in translations]
        tested = sum(r["tested"] for r in rows)
        matches = sum(r["within_one_unit"] for r in rows)
        fingerprints[mapping] = {
            "tested": tested,
            "within_one_unit": matches,
            "match_percent": round(matches / tested * 100, 2) if tested else None,
            "mean_error": round(
                sum(r["mean_error"] * r["tested"] for r in rows if r["tested"]) / tested, 4
            )
            if tested
            else None,
            "first_clip": rows[0] if rows else None,
        }
    hypotheses = [
        evaluate_hypothesis(rig, clips, mapping, convention)
        for mapping in ("directory", "scan", "alphabetical")
        for convention in ("xyzw", "conjugate", "wxyz")
    ]
    hypotheses.append(evaluate_hypothesis(rig, clips, "directory", "xyzw", "world"))
    norms = [
        math.sqrt(sum(v * v for v in k.raw_quat)) / 32768
        for c in clips
        for t in c.tracks
        for k in t.keyframes
    ]
    return {
        "model": path.name,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "clip_count": len(clips),
        "node_count": len(rig),
        "geometry_node_count": sum(n.mesh_index is not None for n in rig),
        "raw_quaternion_norm": {"min": min(norms), "max": max(norms), "keys": len(norms)},
        "limits": [asdict(limit) for limit in LIMITS],
        "nodes": [asdict(nd) for nd in rig],
        "translation_fingerprints": fingerprints,
        "hypotheses": hypotheses,
        "limitations": [
            "Humanoid limits are research envelopes, not a proof or a pose correction.",
            "Hinge tests omit axial twist, shoulders, hips, spine, fingers and hair.",
            "Local hypotheses use exact stored keys; world hypothesis interpolates parent keys.",
            "Duncan's identity bind rotations make pre/post multiplication indistinguishable.",
            "A global reflection applied consistently cannot be resolved by joint limits.",
            "Spline timing, translation playback and original-game matching need separate tests.",
        ],
    }


def markdown_report(report):
    lines = [
        f"# Animation hypotheses: {report['model']}",
        "",
        f"{report['clip_count']} clips; {report['node_count']} named nodes. "
        "No pose is clamped or corrected by these tests.",
        "",
        "## Independent translation fingerprint",
        "",
        "First translation key versus named child node rest position; roots excluded.",
        "",
        "| Mapping | Matches within 1 source unit | Mean error |",
        "|---|---:|---:|",
    ]
    for name, row in report["translation_fingerprints"].items():
        lines.append(
            f"| {name} | {row['within_one_unit']}/{row['tested']} "
            f"({row['match_percent']}%) | {row['mean_error']} |"
        )
    lines += [
        "",
        "## Humanoid diagnostic envelopes",
        "",
        "Exact stored rotation keys, four elbow/knee joints. Counts depend on each "
        "track's key density; use the fingerprint and engine evidence to select a mapping.",
        "",
        "| Hypothesis | Keys outside envelope | Percent |",
        "|---|---:|---:|",
    ]
    for row in report["hypotheses"]:
        lines.append(
            f"| {row['id']} | {row['violations']}/{row['keys']} | {row['violation_percent']}% |"
        )
    lines += [
        "",
        "## Directory mapping",
        "",
        "| Track slot | Geometry index | Original name | Parent slot |",
        "|---:|---:|---|---:|",
    ]
    for nd in report["nodes"]:
        lines.append(f"| {nd['slot']} | {nd['mesh_index']} | {nd['name']} | {nd['parent']} |")
    lines += ["", "## Limits of the test", ""]
    lines += [f"- {line}" for line in report["limitations"]]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", nargs="?", type=Path)
    parser.add_argument("--out", type=Path, default=paths.get("out") / "animation-harness")
    args = parser.parse_args()
    model = args.model or paths.disc(1) / "DATA/3DC/XH_.DAN"
    report = investigate(model)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (args.out / "report.md").write_text(markdown_report(report), encoding="utf-8")
    print(markdown_report(report).split("## Directory mapping")[0])
    print(f"Reports: {args.out.resolve()}")


if __name__ == "__main__":
    main()
