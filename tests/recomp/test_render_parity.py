"""Render parity of the browser build against the Windows build
(recomp/windream/verify/render_parity.py): captured scene inputs and guest
memory images, replayed by one program on D3D11 and in Chrome on WebGL2.

Run with: uv run --with playwright --with pillow pytest tests/recomp/test_render_parity.py

Skipped without the captured cases (the capture step needs the discs and the
Windows build), the browser tools, Playwright or Chrome. The cases are
game-derived and stay under DREAMS_OUT/recomp/render-parity.
"""

import importlib.util
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "recomp" / "windream" / "verify" / "render_parity.py"


@pytest.fixture(scope="module")
def parity():
    spec = importlib.util.spec_from_file_location("wd_render_parity_t", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def test_pixel_stats_and_blank(parity):
    a = bytes([10, 20, 30, 255] * 8)
    b = bytearray(a)
    b[0] += 1  # within the tolerance
    b[5] += 9  # beyond it
    stats = parity.pixel_stats(a, bytes(b), tolerance=2)
    assert stats == {"differing": 2, "beyond": 1, "worst": 9}
    assert parity.blank(bytes([0, 0, 0, 255] * parity.WIDTH * parity.HEIGHT))


def test_browser_frames_match_windows(parity):
    pytest.importorskip("playwright.sync_api")
    inputs = parity.case_inputs()
    if not inputs:
        pytest.skip("no captured cases: render_parity.py capture (needs the discs)")
    import web_build

    if not web_build.tools_config().is_file():
        pytest.skip("no browser tools (out/recomp/web-tools/tools.json)")
    parity.build_native()
    parity.build_web()
    native_errors = parity.run_native(inputs)
    try:
        web_errors, gpu = parity.run_web(inputs, swiftshader=False, headed=False)
    except Exception as error:  # noqa: BLE001
        pytest.skip(f"no browser: {str(error).splitlines()[0]}")
    rows = parity.compare(inputs, native_errors, web_errors, tolerance=2, budget=0.002)
    failed = [row for row in rows if not row["ok"]]
    assert not failed, f"browser GPU {gpu}: " + "; ".join(
        f"{row['case']}: {row.get('error') or row}" for row in failed[:5]
    )
