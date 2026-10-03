# ruff: noqa: E501
"""Drive the web shell in headless Chromium (Playwright) and check what it does.

    uv run --with playwright python recomp/web/browser_check.py            # real build if present, else the mock
    uv run --with playwright python recomp/web/browser_check.py --mock     # mock engine and fake pack
    uv run --with playwright python recomp/web/browser_check.py --real --run-seconds 20

Needs a Chromium: `uv run --with playwright python -m playwright install chromium`
(or the installed Chrome, which is used when Playwright has none).

Mock mode (recomp/web/mock/): a tiny real Emscripten module stands in for the
engine and a fake pack for the demo; both are built under out/recomp/web/mock/
(the engine needs emcc: `. ./recomp/web-env.ps1`). Real mode uses
out/recomp/windream/build-web (WASM stream) and out/recomp/web/demo (DEMO
stream). Both are packaged with package.py into a dist folder, served by
serve.py, and loaded in the browser:

  - cross-origin isolation and SharedArrayBuffer
  - progress UI, download of the chunks, sha256 check, files written to /dreams
  - reload: the pack comes from the cache, no chunk is requested again
  - saves: persisted in IndexedDB across reloads (mock: a counter file)
  - keys do not scroll or trigger browser defaults, mute toggle
  - a damaged chunk is reported, a page without COOP/COEP says why it cannot run
  - the pack on another origin (CORS), as with R2
Screenshots go to the --out folder (default out/recomp/web/check).
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
WEB_OUT = REPO / "out" / "recomp" / "web"
MOCK = WEB_OUT / "mock"
REAL_ENGINE = REPO / "out" / "recomp" / "windream" / "build-web"
REAL_DEMO = WEB_OUT / "demo"


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


os.environ.setdefault("WD_WEB_QUIET", "1")
serve = _load("dreams_web_serve", HERE / "serve.py")


class Report:
    def __init__(self) -> None:
        self.rows: list[tuple[bool, str, str]] = []

    def check(self, ok: bool, name: str, detail: str = "") -> bool:
        self.rows.append((bool(ok), name, detail))
        print(
            f"[{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail else ""), flush=True
        )
        return bool(ok)

    @property
    def ok(self) -> bool:
        return all(r[0] for r in self.rows)


def build_mock() -> None:
    if (MOCK / "engine" / "dreams.js").is_file() and (MOCK / "demo" / "manifest.json").is_file():
        return
    mod = _load("dreams_web_build_mock", HERE / "mock" / "build_mock.py")
    mod.build_engine(MOCK / "engine")
    mod.build_pack(MOCK / "demo")


def package(engine: Path, demo: Path, out: Path) -> None:
    subprocess.run(
        [
            sys.executable,
            str(HERE / "package.py"),
            "--engine",
            str(engine),
            "--demo",
            str(demo),
            "--out",
            str(out),
            "--clean",
        ],
        check=True,
    )


class Server:
    def __init__(self, root: Path, **kw) -> None:
        self.httpd = serve.make_server(root, 0, **kw)
        self.port = self.httpd.server_address[1]
        self.url = f"http://127.0.0.1:{self.port}/"
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def close(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()


def launch(pw, headed: bool = False):
    args = [
        "--enable-unsafe-swiftshader",
        "--ignore-gpu-blocklist",
        "--use-gl=angle",
        "--use-angle=swiftshader",
        "--autoplay-policy=no-user-gesture-required",
    ]
    try:
        return pw.chromium.launch(headless=not headed, args=args)
    except Exception as e:  # noqa: BLE001 - no Playwright browser: try the installed Chrome
        print(
            f"Playwright chromium not available ({str(e).splitlines()[0]}), trying Chrome",
            flush=True,
        )
        return pw.chromium.launch(headless=not headed, args=args, channel="chrome")


def wait_phase(page, phases, timeout_s: float = 30):
    page.wait_for_function(
        "(p) => window.dreamsPage && p.includes(window.dreamsPage.phase)",
        arg=list(phases),
        timeout=timeout_s * 1000,
    )
    return page.evaluate("window.dreamsPage.phase")


def pack_hashes(page, files) -> dict:
    return page.evaluate(
        """async (files) => {
          const M = window.dreamsPage.Module, out = {};
          const hex = (b) => Array.from(new Uint8Array(b), x => x.toString(16).padStart(2, '0')).join('');
          for (const f of files) {
            try {
              const d = M.FS.readFile('/dreams/' + f.path);
              out[f.path] = d.length === f.size ? hex(await crypto.subtle.digest('SHA-256', d)) : 'size ' + d.length;
            } catch (e) { out[f.path] = 'ERR ' + e.message; }
          }
          return out;
        }""",
        files,
    )


def run_checks(
    browser,
    base: str,
    dist: Path,
    out: Path,
    rep: Report,
    mock: bool,
    run_seconds: float,
    keys: str = "",
) -> None:
    manifest = json.loads((dist / "demo" / "manifest.json").read_text(encoding="utf-8"))
    files = manifest["files"]
    chunk_names = {c["url"] for f in files for c in f["chunks"]}
    out.mkdir(parents=True, exist_ok=True)
    ctx = browser.new_context(viewport={"width": 1000, "height": 700})
    page = ctx.new_page()
    console: list[str] = []
    page.on("console", lambda m: console.append(f"[{m.type}] {m.text}"))
    page.on("pageerror", lambda e: console.append(f"[pageerror] {e}"))
    chunk_requests: list[str] = []
    page.on(
        "request",
        lambda r: (
            chunk_requests.append(r.url)
            if r.url.split("?")[0].rsplit("/", 1)[-1] in chunk_names
            else None
        ),
    )

    # Slow the chunks down a little so the progress UI can be seen.
    def slow(route):
        time.sleep(0.25)
        route.continue_()

    if mock:
        ctx.route(
            "**/demo/**",
            lambda r: (
                slow(r)
                if r.request.url.split("?")[0].rsplit("/", 1)[-1] in chunk_names
                else r.continue_()
            ),
        )

    # 1. first load: isolation, progress, download
    page.goto(base + "?debug")
    rep.check(
        page.evaluate("window.crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined'"),
        "cross-origin isolated, SharedArrayBuffer present",
    )
    if mock:
        try:
            page.wait_for_function(
                "window.dreamsPage.bytesDone > 0 && window.dreamsPage.phase === 'downloading'",
                timeout=15000,
            )
            page.screenshot(path=str(out / "1-downloading.png"))
            rep.check(
                page.locator("#bar").get_attribute("aria-valuenow") is not None,
                "progress bar shown during the download",
                page.inner_text("#bytes"),
            )
        except Exception:  # noqa: BLE001
            rep.check(False, "progress observed during the download")
    phase = wait_phase(page, ["ready", "error"], 120)
    page.screenshot(path=str(out / "2-ready.png"))
    st = page.evaluate("window.dreamsPage")
    rep.check(
        phase == "ready", "download finished and verified", f"phase={phase} {st.get('error') or ''}"
    )
    rep.check(
        st["filesDone"] == len(files)
        and st["bytesDone"] == st["bytesTotal"] == sum(f["size"] for f in files),
        "all files and bytes accounted for",
        f"{st['filesDone']}/{len(files)} files, {st['bytesDone']}/{st['bytesTotal']} bytes",
    )
    rep.check(not st["cacheHit"], "first load downloads (no cache hit)")
    rep.check(page.locator("#play").is_enabled(), "play button enabled")
    if phase != "ready":
        print("\n".join(console[-20:]))
        ctx.close()
        return

    # 2. click to start
    page.click("#play")
    phase = wait_phase(page, ["running", "error"], 120 if not mock else 30)
    rep.check(
        phase == "running",
        "game started after the click",
        f"phase={phase} {page.evaluate('window.dreamsPage.error') or ''}",
    )
    if phase != "running":
        print("\n".join(console[-30:]))
        page.screenshot(path=str(out / "3-start-failed.png"))
        ctx.close()
        return
    page.wait_for_timeout(1500)
    page.screenshot(path=str(out / "3-running.png"))
    rep.check(
        page.evaluate("document.getElementById('overlay').classList.contains('hidden')"),
        "overlay hidden while running",
    )

    # 3. files in /dreams
    try:
        hashes = pack_hashes(page, files)
        bad = {
            p: h
            for p, h in hashes.items()
            if h != next(f["sha256"] for f in files if f["path"] == p)
        }
        saves_only = all(p.startswith("DATA/GAME/") for p in bad)
        rep.check(
            not bad or (not mock and saves_only),
            "pack files in /dreams match the manifest (read back from the engine FS)",
            str(bad) if bad else "",
        )
    except Exception as e:  # noqa: BLE001
        rep.check(False, "engine FS readable from the page", str(e))
    rep.check(
        page.evaluate("window.dreamsPage.mounted.length") == len(files),
        "every pack file written into /dreams in preRun (saves seeded after the IDBFS load)",
    )
    if mock:
        lines = page.evaluate("window.dreamsPage.Module.mockLines")
        rep.check(
            "WD_INSTALL_ROOT=/dreams" in lines, "WD_INSTALL_ROOT reaches the engine environment"
        )
        rep.check(
            "pthread result 42" in lines, "a pthread ran (SharedArrayBuffer works in the engine)"
        )
        rep.check(
            "save starts 1" in lines,
            "engine wrote its save file",
            str([line for line in lines if "save" in line]),
        )
        f0 = page.evaluate("window.dreamsPage.Module.mockFrame")
        page.wait_for_timeout(500)
        rep.check(
            page.evaluate("window.dreamsPage.Module.mockFrame") > f0, "canvas keeps being drawn"
        )

    # 4. keys and mute
    page.evaluate(
        "window.__kp = []; addEventListener('keydown', e => window.__kp.push([e.key, e.defaultPrevented]))"
    )
    for k in ["ArrowDown", "ArrowUp", "Space", "Alt", "Control", "Escape", "F10", "Tab"]:
        page.keyboard.press(k)
    kp = page.evaluate("window.__kp")
    rep.check(
        kp and all(p for _, p in kp),
        "game keys are prevented (no page scrolling or browser menus)",
        str([k for k, p in kp if not p]),
    )
    rep.check(page.evaluate("document.scrollingElement.scrollTop") == 0, "page did not scroll")
    page.hover("#hud")
    page.click("#mute")
    rep.check(
        page.evaluate("window.dreamsPage.audio.muted")
        and page.evaluate("localStorage.getItem('dreams.mute')") == "1",
        "mute toggle",
    )
    page.click("#mute")
    rep.check(not page.evaluate("window.dreamsPage.audio.muted"), "unmute toggle")
    if keys:
        for item in keys.split(","):
            t, k = item.split(":", 1)
            page.wait_for_timeout(int(float(t) * 1000))
            page.keyboard.press(k)

    # 5. saves persist
    page.evaluate("dispatchEvent(new Event('pagehide'))")
    try:
        page.wait_for_function("window.dreamsPage.saveSyncs >= 1", timeout=10000)
        rep.check(
            True,
            "saves synced to IndexedDB (IDBFS)",
            f"persist={page.evaluate('window.dreamsPage.persistSaves')}",
        )
    except Exception:  # noqa: BLE001
        rep.check(
            False,
            "saves synced to IndexedDB (IDBFS)",
            "engine lacks IDBFS/addRunDependency exports?",
        )
    if run_seconds > 0:
        page.wait_for_timeout(int(run_seconds * 1000))
        page.screenshot(path=str(out / "3b-running-later.png"))

    # 6. reload: cache hit
    chunk_requests.clear()
    t0 = time.time()
    page.reload()
    phase = wait_phase(page, ["ready", "error"], 60)
    st = page.evaluate("window.dreamsPage")
    page.screenshot(path=str(out / "4-reload-cached.png"))
    rep.check(
        phase == "ready" and st["cacheHit"],
        "reload: pack comes from the cache",
        f"{time.time() - t0:.1f}s",
    )
    rep.check(
        not chunk_requests, "reload: no chunk requested from the server", str(chunk_requests[:3])
    )
    page.click("#play")
    phase = wait_phase(page, ["running", "error"], 120 if not mock else 30)
    rep.check(phase == "running", "reload: game starts again")
    if mock:
        page.wait_for_timeout(500)
        lines = page.evaluate("window.dreamsPage.Module.mockLines")
        rep.check(
            "save starts 2" in lines,
            "reload: the save from the first run is back",
            str([x for x in lines if "save" in x]),
        )
        rep.check(
            page.evaluate(
                "window.dreamsPage.Module.FS.analyzePath('/dreams/DATA/GAME/GAME1.DAT').exists"
            ),
            "pack's seed save present",
        )
    errs = [c for c in console if c.startswith(("[error]", "[pageerror]"))]
    rep.check(not errs, "no console errors", "; ".join(errs[:3]))
    ctx.close()


def run_failure_checks(
    browser, base: str, plain_base: str, dist: Path, out: Path, rep: Report
) -> None:
    manifest = json.loads((dist / "demo" / "manifest.json").read_text(encoding="utf-8"))
    first_chunk = manifest["files"][0]["chunks"][0]["url"]

    # A damaged chunk
    ctx = browser.new_context()
    page = ctx.new_page()

    def corrupt(route):
        resp = route.fetch()
        body = bytearray(resp.body())
        body[len(body) // 2] ^= 0xFF
        route.fulfill(response=resp, body=bytes(body))

    ctx.route(f"**/{first_chunk}*", corrupt)
    page.goto(base)
    phase = wait_phase(page, ["ready", "error"], 60)
    err = page.evaluate("window.dreamsPage.error") or {}
    page.screenshot(path=str(out / "5-bad-checksum.png"))
    rep.check(
        phase == "error" and "Checksum mismatch" in err.get("detail", ""),
        "damaged chunk is rejected with a checksum message",
        err.get("detail", "")[:60],
    )
    ctx.close()

    # No COOP/COEP
    ctx = browser.new_context()
    page = ctx.new_page()
    page.goto(plain_base)
    phase = wait_phase(page, ["error"], 20)
    txt = page.inner_text("#error")
    page.screenshot(path=str(out / "6-not-isolated.png"))
    rep.check(
        "cross-origin isolated" in txt and "Cross-Origin-Opener-Policy" in txt,
        "missing COOP/COEP gives a clear message",
    )
    ctx.close()

    # Pack on another origin (R2-like), manifest by ?demo=
    other = Server(dist / "demo", cors=True)
    try:
        ctx = browser.new_context()
        page = ctx.new_page()
        page.goto(base + "?demo=" + other.url + "manifest.json&autostart")
        phase = wait_phase(page, ["running", "error", "ready"], 60)
        err = page.evaluate("window.dreamsPage.error")
        rep.check(
            phase in ("running", "ready"),
            "pack from another origin (CORS) loads under COEP",
            f"{phase} {err or ''}",
        )
        page.wait_for_timeout(500)
        page.screenshot(path=str(out / "7-cross-origin-pack.png"))
        ctx.close()
    finally:
        other.close()


VIEWPORTS = [(1920, 1080), (1366, 768), (1024, 768), (390, 844)]


def run_design_checks(
    browser, base: str, dist: Path, out: Path, rep: Report, mock: bool, viewports=VIEWPORTS
) -> None:
    """The page's presentation (site/): layout at four viewports, the stage, the controls
    dialog, the key shield, the veil, pixel-perfect scaling. Screenshots: d-<W>x<H>-*.png."""
    manifest = json.loads((dist / "demo" / "manifest.json").read_text(encoding="utf-8"))
    chunk_names = {c["url"] for f in manifest["files"] for c in f["chunks"]}
    out.mkdir(parents=True, exist_ok=True)
    for w, h in viewports:
        tag = f"{w}x{h}"
        ctx = browser.new_context(viewport={"width": w, "height": h})
        page = ctx.new_page()
        console: list[str] = []
        page.on("console", lambda m, c=console: c.append(f"[{m.type}] {m.text}"))
        page.on("pageerror", lambda e, c=console: c.append(f"[pageerror] {e}"))
        if mock:

            def slow(route):
                time.sleep(0.4)
                route.continue_()

            ctx.route(
                "**/demo/**",
                lambda r: (
                    slow(r)
                    if r.request.url.split("?")[0].rsplit("/", 1)[-1] in chunk_names
                    else r.continue_()
                ),
            )
        page.goto(base)
        try:
            page.wait_for_function(
                "window.dreamsPage.phase === 'downloading' && window.dreamsPage.bytesDone > 0",
                timeout=15000,
            )
            page.screenshot(path=str(out / f"d-{tag}-1-loading.png"))
        except Exception:  # noqa: BLE001 - a fast cache hit skips this state
            pass
        wait_phase(page, ["ready", "error"], 60)
        page.wait_for_timeout(400)
        page.screenshot(path=str(out / f"d-{tag}-2-ready.png"))
        # The tribute opens with the manual; assess the playable section after
        # navigating to it, just as the page's Play links do.
        page.locator("#player").evaluate(
            "el => el.scrollIntoView({block: 'start', behavior: 'instant'})"
        )
        geo = page.evaluate(
            """() => { const r = document.getElementById('stage').getBoundingClientRect();
              const b = document.getElementById('hud').getBoundingClientRect();
              return {w: r.width, h: r.height, top: r.top, bottom: b.bottom,
                      scrollW: document.documentElement.scrollWidth, innerW: innerWidth, innerH: innerHeight}; }"""
        )
        rep.check(
            abs(geo["w"] / geo["h"] - 4 / 3) < 0.01,
            f"{tag}: the stage keeps the 4:3 shape",
            f"{geo['w']:.0f}x{geo['h']:.0f}",
        )
        rep.check(geo["scrollW"] <= geo["innerW"], f"{tag}: no horizontal overflow")
        if w >= 1024:
            rep.check(
                geo["top"] >= 0 and geo["bottom"] <= geo["innerH"] + 1,
                f"{tag}: stage and toolbar fit in the viewport",
                f"bottom {geo['bottom']:.0f} of {geo['innerH']}",
            )
        phone = page.evaluate("getComputedStyle(document.querySelector('.notice.phone')).display")
        rep.check((phone != "none") == (w <= 760), f"{tag}: phone notice only on narrow screens")

        page.click("#play")
        wait_phase(page, ["running", "error"], 60)
        page.wait_for_timeout(1200)
        page.screenshot(path=str(out / f"d-{tag}-3-running.png"))
        rep.check(
            page.evaluate("document.activeElement.id") == "canvas",
            f"{tag}: the canvas has the focus",
        )

        # the ? key opens the controls overlay and keeps keys from the game
        page.keyboard.press("?")
        page.wait_for_timeout(200)
        rep.check(
            page.evaluate("document.getElementById('controls-dialog').open"),
            f"{tag}: ? opens the controls",
        )
        page.screenshot(path=str(out / f"d-{tag}-4-controls.png"))
        rep.check(
            not page.evaluate("window.dreamsPage.captureKeys()"),
            f"{tag}: keys are the page's while the controls are open",
        )
        page.keyboard.press("Escape")
        page.wait_for_timeout(200)
        rep.check(
            not page.evaluate("document.getElementById('controls-dialog').open")
            and page.evaluate("document.activeElement.id") == "canvas",
            f"{tag}: Esc closes the controls and returns the keys to the game",
        )

        # keyboard way out of the game, and back
        page.keyboard.press("Shift+Tab")
        rep.check(
            page.evaluate("document.activeElement.id") == "esc",
            f"{tag}: Shift+Tab leaves the game for the toolbar",
        )
        page.keyboard.press("Escape")
        rep.check(
            page.evaluate("document.activeElement.id") == "canvas",
            f"{tag}: Esc on the toolbar returns to the game",
        )

        # losing focus shows the veil; clicking it takes control again
        page.evaluate("document.activeElement.blur()")
        try:  # the page reacts on its next task; a busy real engine can delay it
            page.wait_for_function("!document.getElementById('veil').hidden", timeout=5000)
        except Exception:  # noqa: BLE001 - the check below reports it
            pass
        rep.check(
            not page.evaluate("document.getElementById('veil').hidden"),
            f"{tag}: 'click to take control' shows when the game has no focus",
        )
        page.screenshot(path=str(out / f"d-{tag}-5-veil.png"))
        page.click("#veil")
        page.wait_for_timeout(200)
        rep.check(
            page.evaluate("document.getElementById('veil').hidden")
            and page.evaluate("document.activeElement.id") == "canvas",
            f"{tag}: clicking the veil gives the game the keys",
        )

        # pixel-perfect: sharp pixels, whole multiples of 640 device pixels when it fits well
        page.click("#scale")
        sz = page.evaluate(
            "(() => { const r = document.getElementById('stage').getBoundingClientRect(); return [r.width * devicePixelRatio, document.body.classList.contains('pixel'), document.body.dataset.pixelSize]; })()"
        )
        rep.check(
            sz[1] and (sz[2] == "fit" or abs(sz[0] / 640 - round(sz[0] / 640)) < 0.001),
            f"{tag}: pixel-perfect is a whole multiple of 640 px, or the fitted size",
            f"{sz[0]:.1f}px {sz[2]}",
        )
        page.screenshot(path=str(out / f"d-{tag}-6-pixel.png"))
        page.click("#scale")
        if w in (1366, 390):
            page.evaluate("window.scrollTo(0, 0)")
            page.screenshot(path=str(out / f"d-{tag}-7-full.png"), full_page=True)
        # the toolbar's Menu button sends Esc to the game
        page.evaluate(
            "window.__esc = []; addEventListener('keydown', e => window.__esc.push(e.key)); addEventListener('keyup', e => window.__esc.push('up:' + e.key));"
        )
        page.click("#esc")
        page.wait_for_timeout(400)
        got = page.evaluate("window.__esc")
        rep.check(
            "Escape" in got and "up:Escape" in got,
            f"{tag}: the Menu button taps Esc into the game",
            str(got),
        )
        if w == 1366:
            # fullscreen: the stage fills the screen, the toolbar is still reachable
            page.click("#fs")
            page.wait_for_timeout(500)
            fs = page.evaluate(
                "(() => { const r = document.getElementById('stage').getBoundingClientRect(); return [!!document.fullscreenElement, r.width, r.height, innerWidth, innerHeight]; })()"
            )
            rep.check(
                fs[0]
                and fs[1] <= fs[3] + 1
                and fs[2] <= fs[4] + 1
                and max(fs[1] / fs[3], fs[2] / fs[4]) > 0.99,
                f"{tag}: fullscreen fills the screen at 4:3",
                str(fs),
            )
            page.screenshot(path=str(out / f"d-{tag}-8-fullscreen.png"))
            page.click("#fs")
            page.wait_for_timeout(300)
        errs = [c for c in console if c.startswith(("[error]", "[pageerror]"))]
        rep.check(not errs, f"{tag}: no console errors", "; ".join(errs[:2]))
        ctx.close()

    # error panel with plain-language help (a damaged chunk)
    first_chunk = manifest["files"][0]["chunks"][0]["url"]
    ctx = browser.new_context(viewport={"width": 1366, "height": 768})
    page = ctx.new_page()

    def corrupt(route):
        resp = route.fetch()
        body = bytearray(resp.body())
        body[len(body) // 2] ^= 0xFF
        route.fulfill(response=resp, body=bytes(body))

    ctx.route(f"**/{first_chunk}*", corrupt)
    page.goto(base)
    wait_phase(page, ["error"], 60)
    page.wait_for_timeout(300)
    rep.check(
        "What you can try" in page.inner_text("#error-help"),
        "error help gives plain-language fixes",
    )
    page.screenshot(path=str(out / "d-error-checksum.png"))
    ctx.close()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--mock", action="store_true", help="mock engine and fake pack")
    g.add_argument(
        "--real", action="store_true", help="out/recomp/windream/build-web and out/recomp/web/demo"
    )
    ap.add_argument("--out", type=Path, default=WEB_OUT / "check")
    ap.add_argument(
        "--run-seconds",
        type=float,
        default=0,
        help="keep the game running this long before the reload check (real build)",
    )
    ap.add_argument("--keys", default="", help="after the key checks, e.g. 1:Enter,2:ArrowDown")
    ap.add_argument("--headed", action="store_true")
    ap.add_argument("--skip-failure-checks", action="store_true")
    ap.add_argument(
        "--design", action="store_true", help="also run the page-design checks on a real build"
    )
    ap.add_argument(
        "--design-only", action="store_true", help="only the page-design checks (fast iteration)"
    )
    args = ap.parse_args()

    real_ready = (REAL_ENGINE / "dreams.js").is_file() and (REAL_DEMO / "manifest.json").is_file()
    mock = args.mock or (not args.real and not real_ready)
    if args.real and not real_ready:
        print(f"real build missing: need {REAL_ENGINE}/dreams.js and {REAL_DEMO}/manifest.json")
        return 2
    dist = WEB_OUT / ("mock" if mock else "check-real") / "dist"
    if mock:
        build_mock()
        package(MOCK / "engine", MOCK / "demo", dist)
    else:
        package(REAL_ENGINE, REAL_DEMO, dist)
    print(f"mode: {'mock' if mock else 'real'}; dist {dist}", flush=True)

    from playwright.sync_api import sync_playwright

    rep = Report()
    srv = Server(dist)
    plain = Server(dist, plain=True)
    try:
        with sync_playwright() as pw:
            browser = launch(pw, args.headed)
            if not args.design_only:
                run_checks(browser, srv.url, dist, args.out, rep, mock, args.run_seconds, args.keys)
                if not args.skip_failure_checks:
                    run_failure_checks(browser, srv.url, plain.url, dist, args.out, rep)
            if mock or args.design or args.design_only:
                run_design_checks(browser, srv.url, dist, args.out, rep, mock)
            browser.close()
    finally:
        srv.close()
        plain.close()
    print(
        f"\n{sum(r[0] for r in rep.rows)}/{len(rep.rows)} checks passed; screenshots in {args.out}"
    )
    return 0 if rep.ok else 1


if __name__ == "__main__":
    sys.exit(main())
