# ruff: noqa: E501
"""The browser build's web shell (recomp/web/): headers, packaging, and the page in Chromium.

Run with: uv run --with playwright pytest tests/recomp/test_web_page.py

The header and packaging tests need nothing. The browser test drives the
shell in headless Chromium against a mock engine and a fake demo pack
(recomp/web/mock/, built under out/recomp/web/mock/ with emcc); it is skipped
when Playwright or a browser is missing, or the mock engine is not built and
emcc is not available. The same checks run against the real build with
`recomp/web/browser_check.py --real`.
"""

import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "recomp" / "web"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


@pytest.fixture(scope="module")
def serve():
    return load(WEB / "serve.py", "dreams_web_serve_t")


@pytest.fixture(scope="module")
def package():
    return load(WEB / "package.py", "dreams_web_package_t")


def test_headers_file_has_isolation_and_caching(package, serve):
    text = package.headers_text(demo_local=True)
    rules = serve.parse_headers_file(text)
    h = dict(serve.headers_for(rules, "/index.html"))
    assert h["Cross-Origin-Opener-Policy"] == "same-origin"
    assert h["Cross-Origin-Embedder-Policy"] == "require-corp"
    assert h["Cache-Control"] == "no-cache"
    assert dict(serve.headers_for(rules, "/dreams.wasm"))["Content-Type"] == "application/wasm"
    chunk = dict(serve.headers_for(rules, "/demo/DREAMS.DAT.000"))
    assert "immutable" in chunk["Cache-Control"]
    # The manifest detaches the inherited immutable value.
    manifest = dict(serve.headers_for(rules, "/demo/manifest.json"))
    assert manifest["Cache-Control"] == "no-cache"
    assert "Cache-Control" not in dict(
        serve.headers_for(serve.parse_headers_file(package.headers_text(False)), "/demo/x")
    )


def test_package_assembles_dist(tmp_path):
    engine = tmp_path / "engine"
    demo = tmp_path / "demo"
    engine.mkdir()
    demo.mkdir()
    (engine / "dreams.js").write_text("// stub")
    (engine / "dreams.wasm").write_bytes(b"\0asm")
    (demo / "manifest.json").write_text(
        json.dumps({"version": 1, "name": "t", "total": 0, "files": []})
    )
    out = tmp_path / "dist"
    cmd = [
        sys.executable,
        str(WEB / "package.py"),
        "--engine",
        str(engine),
        "--demo",
        str(demo),
        "--out",
        str(out),
    ]
    subprocess.run(cmd, check=True, capture_output=True)
    for name in (
        "index.html",
        "loader.js",
        "site/style.css",
        "site/ui.js",
        "config.js",
        "_headers",
        "dreams.js",
        "dreams.wasm",
        "demo/manifest.json",
    ):
        assert (out / name).is_file(), name
    assert 'DREAMS_DEMO_BASE = "demo/"' in (out / "config.js").read_text()
    # No game-derived image unless --shots was given.
    assert not (out / "site" / "shots").exists()
    assert "DREAMS_SHOTS" not in (out / "config.js").read_text()
    assert "/site/*" in (out / "_headers").read_text()
    # --demo-url leaves the pack out and points the page at it.
    out2 = tmp_path / "dist2"
    subprocess.run(
        [*cmd[:-1], str(out2), "--demo-url", "https://pack.example.com/d"],
        check=True,
        capture_output=True,
    )
    assert not (out2 / "demo").exists()
    assert 'DREAMS_DEMO_BASE = "https://pack.example.com/d/"' in (out2 / "config.js").read_text()
    assert "/demo/*" not in (out2 / "_headers").read_text()


def test_serve_range_and_headers(tmp_path, serve):
    import threading
    import urllib.request

    (tmp_path / "a.bin").write_bytes(bytes(range(256)))
    (tmp_path / "_headers").write_text("/*\n  X-Test: 1\n/a.bin\n  X-Test: 2\n")
    httpd = serve.make_server(tmp_path, 0)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        url = f"http://127.0.0.1:{httpd.server_address[1]}/a.bin"
        r = urllib.request.urlopen(urllib.request.Request(url, headers={"Range": "bytes=10-19"}))
        assert r.status == 206 and r.read() == bytes(range(10, 20))
        assert r.headers["Content-Range"] == "bytes 10-19/256"
        assert r.headers["X-Test"] == "1, 2"
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_page_in_browser():
    pytest.importorskip("playwright.sync_api")
    from playwright.sync_api import sync_playwright

    check = load(WEB / "browser_check.py", "dreams_web_browser_check_t")
    if not (check.MOCK / "engine" / "dreams.js").is_file() and not shutil.which("emcc"):
        pytest.skip(
            "mock engine not built and emcc not on PATH (. ./recomp/web-env.ps1, then recomp/web/mock/build_mock.py)"
        )
    try:
        check.build_mock()
    except (SystemExit, subprocess.CalledProcessError) as e:
        pytest.skip(f"mock engine cannot be built: {e}")
    dist = check.WEB_OUT / "mock" / "dist"
    check.package(check.MOCK / "engine", check.MOCK / "demo", dist)
    out = check.WEB_OUT / "check-test"
    rep = check.Report()
    srv = check.Server(dist)
    plain = check.Server(dist, plain=True)
    try:
        with sync_playwright() as pw:
            try:
                browser = check.launch(pw)
            except Exception as e:  # noqa: BLE001
                pytest.skip(f"no browser: {str(e).splitlines()[0]}")
            try:
                check.run_checks(browser, srv.url, dist, out, rep, mock=True, run_seconds=0)
                check.run_failure_checks(browser, srv.url, plain.url, dist, out, rep)
                check.run_design_checks(browser, srv.url, dist, out, rep, mock=True)
            finally:
                browser.close()
    finally:
        srv.close()
        plain.close()
    failed = [f"{n}: {d}" for ok, n, d in rep.rows if not ok]
    assert not failed, "\n".join(failed)
