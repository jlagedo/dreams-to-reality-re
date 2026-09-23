"""Independent binding fingerprints and deliberate bad-decode controls."""

from __future__ import annotations

import math
import struct

import pytest

from dreams import paths
from dreams.animation_harness import hinge_angles, investigate, read_translations, rotation_matrix
from dreams.formats import animation, lz, scene
from dreams.formats.rig import read_rig


def synthetic_model():
    """Physical order root/elbow/hand, explicit directory root/hand/elbow."""
    buf = bytearray(1280)
    offsets = (128, 512, 896)
    struct.pack_into("<4I", buf, 20, 3, offsets[0], offsets[2], offsets[1])
    for index, off in enumerate(offsets):
        name = (b"root", b"elbow", b"hand")[index]
        buf[off + 20 : off + 20 + len(name)] = name
        parent_address = offsets[index - 1] + 20 - 100 if index else 0
        struct.pack_into("<I", buf, off + 0x24, parent_address)
        struct.pack_into("<3i", buf, off + 0x30, 0, index * 10, 0)
        struct.pack_into("<9i", buf, off + 0x3C, 32768, 0, 0, 0, 32768, 0, 0, 0, 32768)
        base = off + 240 - 100
        struct.pack_into("<4I", buf, off + 0x90, 1, base, 0, base + 40)
        struct.pack_into("<I", buf, off + 0xD4, 40)
    return buf


def synthetic_track():
    buf = bytearray(160)
    struct.pack_into("<2I", buf, 20, 1, 64)
    # The duration is inside the track, not after the slot table.
    struct.pack_into("<5I", buf, 84, 80, 2, 1, 84, 124)
    struct.pack_into("<5i", buf, 104, 0, 0, 0, 0, 32768)
    struct.pack_into("<5i", buf, 124, 80, 0, 0, 0, 32768)
    struct.pack_into("<4i", buf, 144, 0, 4, -8, 12)
    return buf


def test_rig_uses_explicit_directory_not_physical_or_parent_order():
    rig = read_rig(synthetic_model())
    assert [n.name for n in rig] == ["root", "hand", "elbow"]
    assert [n.mesh_index for n in rig] == [0, 2, 1]
    assert [n.parent for n in rig] == [-1, 2, 0]


def test_rig_retains_nodes_without_geometry():
    buf = synthetic_model()
    # Keep the node in the directory but remove its vertex-array signature.
    struct.pack_into("<2I", buf, 512 + 0x90, 0, 0)
    rig = read_rig(buf)
    assert rig[2].name == "elbow"
    assert rig[2].mesh_index is None
    assert rig[1].parent == 2


def test_rig_rejects_cycle_and_bad_directory():
    buf = synthetic_model()
    struct.pack_into("<I", buf, 128 + 0x24, 896 + 20 - 100)
    with pytest.raises(ValueError, match="Cycle"):
        read_rig(buf)
    buf = synthetic_model()
    struct.pack_into("<I", buf, 24, len(buf))
    with pytest.raises(ValueError, match="offset"):
        read_rig(buf)


@pytest.mark.parametrize("side", [-1, 1])
def test_elbow_control_distinguishes_forward_from_backwards(side):
    half = math.sqrt(0.5)
    # Source +/-Z is the outstretched arm. Rotate each toward forward +X.
    q = (0, side * half, 0, half)
    flex, plane = hinge_angles(rotation_matrix(q), (0, 0, side), (1, 0, 0))
    assert flex == pytest.approx(90)
    assert plane == pytest.approx(0)
    bad, _ = hinge_angles(rotation_matrix(q, "conjugate"), (0, 0, side), (1, 0, 0))
    assert bad == pytest.approx(-90)


def test_knee_control_detects_sideways_bending():
    half = math.sqrt(0.5)
    flex, plane = hinge_angles(rotation_matrix((0, 0, half, half)), (0, 1, 0), (-1, 0, 0))
    assert flex == pytest.approx(90)
    assert plane == pytest.approx(0)
    _, bad_plane = hinge_angles(rotation_matrix((half, 0, 0, half)), (0, 1, 0), (-1, 0, 0))
    assert bad_plane == pytest.approx(90)


def test_duration_and_translation_count_are_not_fps_and_interpolation():
    buf = synthetic_track()
    clip = animation._parse_tag3_payload(buf, "synthetic")
    assert clip.duration_frames == 80
    assert clip.frame_rate == 30
    assert clip.to_dict()["frameRateSource"] == "windream-engine-base"
    assert clip.to_dict()["tracks"][0]["translationKeyCount"] == 1
    assert read_translations(buf) == [[(0, 4, -8, 12)]]
    with pytest.raises(ValueError, match="exceeds payload"):
        read_translations(buf[:-1])


DISC1 = paths.configured("disc1")
XH = DISC1 / "DATA/3DC/XH_.DAN" if DISC1 else None
needs_duncan = pytest.mark.skipif(XH is None or not XH.exists(), reason="Duncan asset unavailable")


@needs_duncan
def test_duncan_binding_names_and_export():
    clip = animation.read_dan_animations(XH)[0]
    assert clip.tracks[2].bone_name == "avbras-d"
    assert clip.tracks[2].mesh_node_index == 10
    assert clip.tracks[25].bone_name == "tete"
    assert clip.tracks[25].mesh_node_index == 4
    assert [t.bone_name for t in clip.tracks[17:21]] == ["nat01", "nat02", "nat03", "nat04"]
    exported = clip.to_dict()["tracks"][2]
    assert exported["nodeIndex"] == 2  # Original slot is preserved for analysis.
    assert exported["meshNodeIndex"] == 10
    assert exported["boneName"] == "avbras-d"


@needs_duncan
def test_duncan_hypotheses_against_independent_translation_fingerprints():
    report = investigate(XH)
    assert report["clip_count"] == 49
    fingerprints = report["translation_fingerprints"]
    assert fingerprints["directory"]["match_percent"] > 98
    assert fingerprints["scan"]["match_percent"] == 0
    by_id = {row["id"]: row for row in report["hypotheses"]}
    correct = by_id["directory/xyzw/local"]
    assert correct["violation_percent"] < 3
    assert by_id["scan/xyzw/local"]["violation_percent"] > 20
    assert by_id["directory/conjugate/local"]["violation_percent"] > 60
    assert all(row["violations"] == 0 for row in correct["joints"] if "mollet" in row["bone"])
    # Check the unnormalized source. Normalizing arbitrary bytes would hide corruption.
    assert report["raw_quaternion_norm"]["min"] > 0.9999
    assert report["raw_quaternion_norm"]["max"] < 1.0001


@pytest.mark.skipif(DISC1 is None, reason="Disc 1 unavailable")
def test_ch0_extra_slot_is_a_named_node_without_vertices():
    path = DISC1 / "DATA/3DC/CH0.DAN"
    if not path.exists():
        pytest.skip("CH0 asset unavailable")
    buf = lz.decompress(next(r.payload for r in scene.read_records(path, "dan") if r.tag == 1))
    rig = read_rig(buf)
    assert len(rig) == 18
    assert rig[17].name == "bassin01"
    assert rig[17].mesh_index is None
