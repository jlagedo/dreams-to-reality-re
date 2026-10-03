"""Render parity of the browser build against the Windows build
(recomp/windream/verify/render_parity.py): captured scene inputs and guest
memory images, replayed by one program on D3D11 and in Chrome on WebGL2.

Run with: uv run --with playwright --with pillow pytest tests/recomp/test_render_parity.py

Skipped without the captured cases (the capture step needs the discs and the
Windows build), the browser tools, Playwright or an installed Chrome. Browser
launch/execution failures otherwise fail the test. The cases are
game-derived and stay under DREAMS_OUT/recomp/render-parity.
"""

import importlib.util
import json
import struct
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


class CaptureControl:
    """A game that draws on selected single-frame screenshot requests."""

    root = 0x18001000

    def __init__(self, draws):
        self.draws = iter(draws)
        self.frame = 0
        self.paused = False
        self.scene = None
        self.pending = False
        self.free_run_captures = 0

    def pause(self):
        self.paused = True

    def resume(self):
        self.paused = False

    def wait(self, **kwargs):
        assert not self.paused
        # Simulate a stale capture made while dialogue clears between retries.
        self.frame += 10
        self.draw_scene()
        self.free_run_captures += 1

    def draw_scene(self):
        if self.pending:
            self.scene.write_bytes(struct.pack("<5I", 0x43534457, self.frame, 0, 0, self.root))
            self.pending = False

    def screenshot(self, path):
        assert self.paused, "capture must pause before arming and stepping"
        self.frame += 1
        if next(self.draws):
            self.draw_scene()
        path.write_bytes(b"present " + str(self.frame).encode())
        return {"frame": self.frame, "width": 640, "height": 480, "format": "png"}

    def call(self, command, **kwargs):
        assert self.paused
        if command == "scene_capture":
            self.scene = Path(kwargs["path"])
            assert not self.scene.exists(), "discard a capture made during a free run"
            self.pending = True
            return {}
        assert command == "memory_dump"
        assert not self.pending
        Path(kwargs["path"]).write_bytes(b"WDM2" + struct.pack("<I", self.root))
        return {"frame": self.frame, "bytes": 8, "scene_pending": False}

    def status(self):
        return {"paused": self.paused, "frame": self.frame}


@pytest.mark.parametrize(
    "draws,steps,expected", [([True], 6, 1), ([False, True], 6, 2), ([False, False, True], 2, 13)]
)
def test_capture_keeps_scene_screenshot_and_memory_on_one_present(
    parity, tmp_path, draws, steps, expected
):
    ctl = CaptureControl(draws)
    scene, memory, png = (tmp_path / name for name in ("case.wds", "case.wdmi", "case.png"))
    result = parity.capture_frame(ctl, scene, memory, png, attempts=2, steps=steps)
    assert result["frame"] == expected
    assert struct.unpack_from("<I", scene.read_bytes(), 4)[0] == expected
    assert png.read_bytes() == b"present " + str(expected).encode()
    assert ctl.paused and ctl.frame == expected
    assert ctl.free_run_captures == (expected == 13)


def test_browser_execution_errors_are_not_unavailability(parity):
    class Chromium:
        def launch(self, **kwargs):
            raise RuntimeError("browser process crashed")

    with pytest.raises(RuntimeError, match="browser process crashed") as error:
        parity.launch_browser(Chromium(), headless=True, args=[])
    assert not isinstance(error.value, parity.BrowserUnavailable)


@pytest.mark.parametrize("fault", ["frame", "root"])
def test_capture_rejects_inconsistent_memory(parity, tmp_path, fault):
    class ChangedCapture(CaptureControl):
        def call(self, command, **kwargs):
            result = super().call(command, **kwargs)
            if command == "memory_dump":
                if fault == "frame":
                    result["frame"] += 1
                else:
                    Path(kwargs["path"]).write_bytes(b"WDM2" + struct.pack("<I", self.root + 4))
            return result

    scene, memory, png = (tmp_path / name for name in ("case.wds", "case.wdmi", "case.png"))
    with pytest.raises(RuntimeError, match="paused presentation|different render roots"):
        parity.capture_frame(ChangedCapture([True]), scene, memory, png)


def test_missing_chrome_is_explicit_unavailability(parity):
    class Chromium:
        def launch(self, **kwargs):
            raise RuntimeError("Chromium distribution 'chrome' is not found at /chrome")

    with pytest.raises(parity.BrowserUnavailable):
        parity.launch_browser(Chromium(), headless=True, args=[])


def test_report_fingerprints_inputs_and_records_thresholds(parity, tmp_path, monkeypatch):
    monkeypatch.setattr(parity, "OUT", tmp_path)
    case = tmp_path / "case.wds"
    case.write_bytes(b"captured scene")
    sidecar = case.with_suffix(".capture.json")
    sidecar.write_text(json.dumps({"files": {case.name: parity.fingerprint(case)}}))
    parity.write_report(
        [case], [{"case": "case.scene", "ok": True}], "test GPU", tolerance=2, budget=0.002
    )
    report = json.loads((tmp_path / "report.json").read_text())
    assert report["inputs"][str(case)] == parity.fingerprint(case)
    assert (report["tolerance"], report["budget"]) == (2, 0.002)
    assert report["sources_at_comparison"]["recomp/windream/host/render/render_hooks.c"]
    assert report["capture_record_validation"][sidecar.name]["files_match"]
    case.write_bytes(b"another scene")
    assert report["inputs"][str(case)] != parity.fingerprint(case)
    parity.write_report([case], [], "test GPU", tolerance=2, budget=0.002)
    report = json.loads((tmp_path / "report.json").read_text())
    assert report["capture_record_validation"][sidecar.name] == {
        "files_match": False,
        "mismatched_files": [case.name],
    }


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
    except parity.BrowserUnavailable as error:
        pytest.skip(f"Chrome is not installed: {str(error).splitlines()[0]}")
    rows = parity.compare(inputs, native_errors, web_errors, tolerance=2, budget=0.002)
    parity.write_report(inputs, rows, gpu, tolerance=2, budget=0.002)
    failed = [row for row in rows if not row["ok"]]
    assert not failed, f"browser GPU {gpu}: " + "; ".join(
        f"{row['case']}: {row.get('error') or row}" for row in failed[:5]
    )
