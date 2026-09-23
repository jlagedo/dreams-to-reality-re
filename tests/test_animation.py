from __future__ import annotations

import math

import pytest

from dreams import paths
from dreams.formats.animation import (
    read_dan_animations,
    slerp,
)

DISC1 = paths.disc(1)
pytestmark = pytest.mark.skipif(
    not DISC1.exists() or not (DISC1 / "DATA" / "3DC" / "CH0.DAN").exists(),
    reason="Disc 1 character models not present",
)


def test_slerp_identity():
    q1 = (0.0, 0.0, 0.0, 1.0)
    q2 = (0.0, 1.0, 0.0, 0.0)

    # At t=0, should equal q1
    res0 = slerp(q1, q2, 0.0)
    assert pytest.approx(res0[0]) == 0.0
    assert pytest.approx(res0[1]) == 0.0
    assert pytest.approx(res0[2]) == 0.0
    assert pytest.approx(res0[3]) == 1.0

    # At t=1, should equal q2
    res1 = slerp(q1, q2, 1.0)
    assert pytest.approx(res1[1]) == 1.0

    # At t=0.5, midpoint rotation 45 deg around Y
    res_mid = slerp(q1, q2, 0.5)
    expected_val = math.sin(math.pi / 4)
    assert pytest.approx(res_mid[1], abs=1e-3) == expected_val
    assert pytest.approx(res_mid[3], abs=1e-3) == expected_val


def test_read_ch0_animations():
    ch0_path = DISC1 / "DATA" / "3DC" / "CH0.DAN"
    clips = read_dan_animations(ch0_path)
    assert len(clips) == 2

    c0 = clips[0]
    assert c0.name == "CH0AN000.3DA"
    assert c0.duration_frames == 75
    assert c0.track_count == 17
    assert len(c0.tracks) == 17

    # Validate that tracks have valid keyframes with normalized quaternions
    has_keys = False
    for trk in c0.tracks:
        if trk.num_keys > 0:
            has_keys = True
            for k in trk.keyframes:
                x, y, z, w = k.rotation
                norm = math.sqrt(x * x + y * y + z * z + w * w)
                assert pytest.approx(norm, abs=1e-3) == 1.0
    assert has_keys


def test_sample_pose():
    ch0_path = DISC1 / "DATA" / "3DC" / "CH0.DAN"
    clips = read_dan_animations(ch0_path)
    c0 = clips[0]

    pose_0 = c0.sample_pose(0.0)
    assert len(pose_0) == 17

    pose_mid = c0.sample_pose(37.5)
    assert len(pose_mid) == 17

    # Quaternions in sampled pose should have norm == 1.0
    for _node_idx, quat in pose_mid.items():
        x, y, z, w = quat
        norm = math.sqrt(x * x + y * y + z * z + w * w)
        assert pytest.approx(norm, abs=1e-3) == 1.0


def test_clip_to_dict():
    ch0_path = DISC1 / "DATA" / "3DC" / "CH0.DAN"
    clips = read_dan_animations(ch0_path)
    d = clips[0].to_dict()
    assert d["name"] == "CH0AN000.3DA"
    assert d["duration"] == 75
    assert d["trackCount"] == 17
    assert len(d["tracks"]) == 17
    assert "keyframes" in d["tracks"][0]
