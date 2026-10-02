"""Load a renderer check page in headless Chrome (WebGL2 on SwiftShader) and report.

    uv run --with playwright python recomp/render/tests/web/run_browser.py od_gpu_tests
    uv run --with playwright python recomp/render/tests/web/run_browser.py od_web_frame
    uv run --with playwright python recomp/render/tests/web/run_browser.py od_web_frame_mt

The page is one of the executables built by CMakeLists.txt in this directory
(default build directory out/recomp/render-web/build). It is served with the
COOP/COEP headers that the pthreads build needs. The run ends when the page
reports a result (window.odResult), then prints the console, saves a canvas
screenshot and, for od_web_frame*, the exported frame (frame.rgba, as the
program wrote it into the Emscripten file system) next to it.

Exit status: 0 if the program reported success, 1 otherwise.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *args):
        pass


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("program", help="od_gpu_tests, od_web_frame or od_web_frame_mt")
    ap.add_argument("--build", default=str(REPO / "out" / "recomp" / "render-web" / "build"))
    ap.add_argument("--out", default=str(REPO / "out" / "recomp" / "render-web" / "check"))
    ap.add_argument("--seconds", type=float, default=240)
    ap.add_argument(
        "--wait", type=float, default=1.5, help="seconds of frames before the screenshot"
    )
    ap.add_argument(
        "--lose-context",
        action="store_true",
        help="after the first frame, lose the WebGL context and expect a reported failure",
    )
    ap.add_argument("--chrome", default="", help="Chrome/Edge executable (default: channel chrome)")
    ap.add_argument("--gpu", action="store_true", help="use the real GPU instead of SwiftShader")
    ap.add_argument("--headed", action="store_true")
    args = ap.parse_args()

    from playwright.sync_api import sync_playwright

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    handler = functools.partial(Handler, directory=args.build)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()

    chrome_args = ["--ignore-gpu-blocklist", "--enable-unsafe-swiftshader"]
    if not args.gpu:
        chrome_args += ["--use-gl=angle", "--use-angle=swiftshader", "--disable-gpu-sandbox"]
    status = 1
    with sync_playwright() as pw:
        kw = {"executable_path": args.chrome} if args.chrome else {"channel": "chrome"}
        browser = pw.chromium.launch(headless=not args.headed, args=chrome_args, **kw)
        page = browser.new_page(viewport={"width": 700, "height": 900})
        lines: list[str] = []

        def on_console(m):
            lines.append(f"[{m.type}] {m.text}")
            print(lines[-1], flush=True)

        page.on("console", on_console)
        page.on("pageerror", lambda e: print(f"[pageerror] {e}", flush=True))
        page.on("crash", lambda: print("[page crashed]", flush=True))
        page.goto(f"http://127.0.0.1:{port}/{args.program}.html")
        t0 = time.time()
        result = None
        while time.time() - t0 < args.seconds:
            result = page.evaluate("window.odResult")
            if result:
                break
            page.wait_for_timeout(250)
        print(f"result after {time.time() - t0:.1f}s: {result}")
        if result:
            status = 0 if result.get("code") == 0 else 1
        if args.lose_context:
            page.evaluate("window.odResult = null")
            page.evaluate(
                "document.getElementById('canvas').getContext('webgl2').getExtension('WEBGL_lose_context').loseContext()"
            )
            for _ in range(80):
                page.wait_for_timeout(250)
                after = page.evaluate("window.odResult")
                if after:
                    break
            print(f"after context loss: {after}")
            status = 0 if after and after.get("code") == 1 else 1
        page.wait_for_timeout(int(args.wait * 1000))  # more presented frames
        shot = out / f"{args.program}.png"
        try:
            page.locator("#canvas").screenshot(path=str(shot), timeout=3000)
        except Exception:  # a hidden window hides its canvas
            page.screenshot(path=str(shot))
        print(f"screenshot: {shot}")
        if args.program.startswith("od_web_frame") and status == 0:
            data = page.evaluate(
                "() => { try { return Array.from(Module.FS.readFile('/frame.rgba')); }"
                " catch (e) { return String(e); } }"
            )
            if isinstance(data, str):
                print(f"frame export not readable: {data}")
            else:
                (out / f"{args.program}.rgba").write_bytes(bytes(data))
                print(f"frame export: {out / (args.program + '.rgba')} ({len(data)} bytes)")
        info = page.evaluate(
            "() => { const c = document.createElement('canvas'); const g = c.getContext('webgl2');"
            " if (!g) return 'no webgl2'; const d = g.getExtension('WEBGL_debug_renderer_info');"
            " return d ? g.getParameter(d.UNMASKED_RENDERER_WEBGL) : g.getParameter(g.RENDERER); }"
        )
        print(f"WebGL2 renderer: {info}")
        browser.close()
    server.shutdown()
    return status


if __name__ == "__main__":
    sys.exit(main())
