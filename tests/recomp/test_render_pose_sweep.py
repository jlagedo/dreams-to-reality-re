"""Camera-pose sweep of the direct renderer against retail's face selection
(recomp/windream/verify/render_pose_sweep.py).

Run with: uv run --with unicorn --with pillow pytest tests/recomp/test_render_pose_sweep.py

The geometry helpers are tested on synthetic values. The sweep itself needs a
memory image from render_parity.py capture (game-derived, under
DREAMS_OUT/recomp/render-parity/cases) and the scene adapter DLL from
direct_render_validate.py; it is skipped without them.
"""

import math
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
VERIFY = ROOT / "recomp" / "windream" / "verify"
SCRIPT = VERIFY / "render_pose_sweep.py"

pytest.importorskip("unicorn")
sys.path.insert(0, str(VERIFY))
sys.path.insert(0, str(ROOT / "recomp"))
import recomp_env  # noqa: E402
import render_pose_sweep as sweep  # noqa: E402


def test_composed_camera_floors_the_translation():
    rotation = [32767, 0, 0, 0, 32767, 0, 0, 0, 32767]
    # R^T eye >> 15 rounds toward minus infinity, unlike a truncating divide:
    # -49150.5 -> -49151 (negated 49151), +49150.5 -> 49150 (negated -49150).
    composed = sweep.composed_camera([-49152, 49152, 0], rotation)
    assert composed[:3] == [49151, -49150, 0]
    rotation = [1, 2, 3, 4, 5, 6, 7, 8, 9]
    assert sweep.composed_camera([0, 0, 0], rotation)[3:] == [1, 4, 7, 2, 5, 8, 3, 6, 9]


def test_pose_rotations_are_orthonormal_and_keep_handedness():
    # Screen-down column along +Y, as in the captured cameras.
    captured = [32767, 0, 0, 0, 32767, 0, 0, 0, 32767]
    camera = sweep.Camera([0, 0, 0], captured)
    assert camera.down == [0.0, 1.0, 0.0]
    for yaw in (0.0, 1.0, 2.5):
        for pitch in (-0.2, 0.0, 0.6):
            r = camera.rotation_for(yaw, pitch)
            m = [[r[i * 3 + j] / 32767 for j in range(3)] for i in range(3)]
            for a in range(3):
                for b in range(3):
                    dot = sum(m[k][a] * m[k][b] for k in range(3))
                    assert abs(dot - (a == b)) < 1e-3
            assert sweep.det3(m) > 0
            forward_down = m[1][2]  # forward column, world-down component
            assert abs(forward_down - math.sin(pitch)) < 1e-3


def test_near_clip_and_viewport_clip():
    tri = [(0.0, 0.0, 50.0), (10.0, 0.0, 200.0), (0.0, 10.0, 200.0)]
    clipped = sweep.clip_near(tri, 100.0)
    assert len(clipped) == 4 and all(p[2] >= 100.0 - 1e-9 for p in clipped)
    assert sweep.clip_near(tri, 300.0) == []
    square = [(-10.0, -10.0), (10.0, -10.0), (10.0, 10.0), (-10.0, 10.0)]
    inside = sweep.clip_rect(square, (0, 0, 640, 480))
    assert abs(abs(sweep.shoelace(inside)) / 2 - 100.0) < 1e-9


def test_screen_winding_sign():
    # od_triangle_visible (direct_math.cpp) keeps a culled face when its NDC
    # area is positive; NDC y is up and screen y down, so a front face has a
    # negative shoelace in screen coordinates (counter-clockwise as seen).
    seen_clockwise = [(0.0, 0.0), (10.0, 0.0), (0.0, 10.0)]  # y down
    ndc = [(x, -y) for x, y in seen_clockwise]
    assert sweep.shoelace(ndc) < 0  # back facing for the GPU
    assert sweep.shoelace(seen_clockwise) > 0  # what the sweep calls back_gpu


def test_glide_draw_types():
    assert not sweep.glide_no_draw(2) and not sweep.glide_no_draw(0x16)
    assert sweep.glide_no_draw(0x19) and sweep.glide_no_draw(-8) and sweep.glide_no_draw(-2)


def test_sweep_matches_retail():
    cases = recomp_env.out_dir("render-parity") / "cases"
    images = sorted(cases.glob("*-0.wdmi"))[:2]
    dll = recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll"
    if not images or not dll.is_file():
        pytest.skip("needs render_parity.py capture and direct_render_validate.py")
    pytest.importorskip("PIL")
    result = subprocess.run(
        [sys.executable, str(SCRIPT), *map(str, images), "--poses", "16", "--images-for", "0"],
        capture_output=True,
        text=True,
        cwd=ROOT,
    )
    assert result.returncode == 0, result.stdout[-3000:] + result.stderr[-3000:]
