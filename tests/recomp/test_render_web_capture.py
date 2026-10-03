"""The browser frame capture (recomp/windream/verify/render_web_capture.py,
host/web/web_glue.c WD_WEB_CAPTURE).

Run with: uv run pytest tests/recomp/test_render_web_capture.py

The derived pack needs the demo pack (game-derived, out/recomp/web/demo) and
is skipped without it. The capture itself needs the browser build, Playwright
and Chrome; run the script.
"""

import hashlib
import importlib.util
import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "recomp" / "windream" / "verify" / "render_web_capture.py"
GLUE = ROOT / "recomp" / "windream" / "host" / "web" / "web_glue.c"
CMAKE = ROOT / "recomp" / "windream" / "CMakeLists.txt"


@pytest.fixture(scope="module")
def capture():
    spec = importlib.util.spec_from_file_location("wd_render_web_capture_t", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def test_capture_is_a_build_option_and_exported():
    cmake = CMAKE.read_text()
    assert 'option(WD_WEB_CAPTURE "' in cmake
    assert "target_compile_definitions(windream_recomp PRIVATE WD_WEB_CAPTURE)" in cmake
    glue = GLUE.read_text()
    assert "EMSCRIPTEN_KEEPALIVE int wd_web_capture(void)" in glue
    assert "EMSCRIPTEN_KEEPALIVE int wd_web_capture_state(void)" in glue
    # The page polls these states; the script waits on 1/2 and accepts 3.
    script = SCRIPT.read_text()
    assert "_wd_web_capture_state()" in script and "state != 3" in script


def test_derived_pack_starts_the_project(capture, monkeypatch, tmp_path):
    if not (capture.DEMO / "manifest.json").is_file():
        pytest.skip("needs the demo pack (recomp/web/demo)")
    monkeypatch.setattr(capture, "OUT", tmp_path)
    pack = capture.derived_pack(46)
    manifest = json.loads((pack / "manifest.json").read_text())
    entry = next(e for e in manifest["files"] if e["path"].upper() == "DREAMS.DAT")
    data = b"".join((pack / c["url"]).read_bytes() for c in entry["chunks"])
    assert hashlib.sha256(data).hexdigest() == entry["sha256"] and len(data) == entry["size"]
    sys.path.insert(0, str(ROOT / "recomp" / "windream" / "debug"))
    from bank_patch import Bank

    bank = Bank(data)
    original = Bank(
        b"".join(
            (capture.DEMO / c["url"]).read_bytes()
            for e in json.loads((capture.DEMO / "manifest.json").read_text())["files"]
            if e["path"].upper() == "DREAMS.DAT"
            for c in e["chunks"]
        )
    )
    assert bank.parsed(0).name == original.parsed(46).name
    others = [e for e in manifest["files"] if e is not entry]
    assert all((pack / c["url"]).is_file() for e in others for c in e["chunks"])
