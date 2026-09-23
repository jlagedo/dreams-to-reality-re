from __future__ import annotations

import math

import pytest

from dreams import paths
from dreams.formats.animation import Keyframe, read_dan_animations, sample_rotation, spline_ease


def key(rotation, **kwargs):
    return Keyframe(0, rotation, (0, 0, 0, 32768), **kwargs)


def test_authored_controls_change_the_curve_without_changing_endpoints():
    identity = (0, 0, 0, 1)
    quarter_turn = (0, 0, math.sqrt(0.5), math.sqrt(0.5))
    a = key(identity, out_control=identity)
    b = key(quarter_turn, in_control=identity)
    assert sample_rotation(a, b, 0) == pytest.approx(identity)
    assert sample_rotation(a, b, 1) == pytest.approx(quarter_turn)
    # At half-time, the primary arc is 45 degrees, the control arc 0;
    # the second blend has weight 0.5, giving 22.5 degrees, not SLERP's 45.
    assert sample_rotation(a, b, 0.5) == pytest.approx(
        (0, 0, math.sin(math.pi / 16), math.cos(math.pi / 16))
    )


def test_missing_controls_use_continuous_slerp():
    a = key((0, 0, 0, 1))
    b = key((0, 0, math.sqrt(0.5), math.sqrt(0.5)))
    assert sample_rotation(a, b, 0.5)[2] == pytest.approx(math.sin(math.pi / 8))


def test_easing_field_order_and_piecewise_continuity():
    assert spline_ease(0.1, 0.2, 0.4) == pytest.approx(0.01 / (1.4 * 0.4))
    assert spline_ease(0.5, 0.2, 0.4) == pytest.approx(0.6 / 1.4)
    assert spline_ease(0.9, 0.2, 0.4) == pytest.approx(1 - 0.01 / (1.4 * 0.2))
    for point in (0.4, 0.8):
        assert abs(spline_ease(point - 1e-6, 0.2, 0.4) - spline_ease(point + 1e-6, 0.2, 0.4)) < 3e-6
    assert spline_ease(0.3, 0, 0) == 0.3
    assert spline_ease(0.5, 1, 1) == pytest.approx(0.5)


def test_duncan_run_loop_excludes_initialization_pose_and_exports_controls():
    disc = paths.configured("disc1")
    if disc is None or not (disc / "DATA/3DC/XH_.DAN").exists():
        pytest.skip("Duncan unavailable")
    clips = read_dan_animations(disc / "DATA/3DC/XH_.DAN")
    run = next(c for c in clips if c.name == "XH_AN055.3DA")
    first, last, rest = run.sample_pose(1), run.sample_pose(run.duration_frames), run.sample_pose(0)

    def difference(a, b):
        return 2 * math.acos(min(1, abs(sum(x * y for x, y in zip(a, b, strict=True)))))

    assert max(difference(first[i], last[i]) for i in first) < 1e-6
    assert max(difference(rest[i], last[i]) for i in rest) > math.radians(80)
    exported = clips[0].to_dict()["tracks"][0]["keyframes"][0]
    assert exported["ease"] == [0, 0]
    assert exported["outControl"] is not None
    assert math.hypot(*exported["outControl"]) == pytest.approx(1)
