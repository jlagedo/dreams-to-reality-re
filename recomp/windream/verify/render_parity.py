# ruff: noqa: E501
"""Render parity: Windows and browser replay of the same recorded 3D input.

Nobody plays: the inputs are taken from the Windows build at frozen frames and
replayed through the production scene adapter and renderer by one program,
built for Windows (D3D11) and for the browser (WebGL2).

    capture   start the Windows build in each project (a bank with that project
              in slot 0), and at a few paused frames write through the control
              channel: <case>.wds, the scene inputs the renderer was about to
              draw, <case>.wdmi, the guest's committed memory, and <case>.png,
              the frame the game showed. Scene and screenshot share a present;
              memory is dumped afterward, while still paused.
    build     the replay program, native and Emscripten
    run       replay every case natively and in Chrome, then compare:
                data    a memory image goes through the adapter's capture on
                        both hosts; the two snapshots must be the same bytes
                        (64-bit host against wasm32)
                pixels  the 640x480 frames, per case and per input kind
    all       the three in order (capture only when there are no cases yet)

    uv run --with playwright --with pillow python recomp/windream/verify/render_parity.py all
    uv run python recomp/windream/verify/render_parity.py capture --projects 0,62 --shots 3
    uv run --with playwright --with pillow python recomp/windream/verify/render_parity.py run --swiftshader

Everything is under DREAMS_OUT/recomp/render-parity (game-derived: never
committed): cases/, build-native/, build-web/, native/, web/, report.json and
diff images in report/. Exit status 1 when a case fails.

A pixel fails when a channel differs by more than --tolerance (default 2: the
two GPUs' rounding); a case fails when more than --budget of its pixels do
(default 0.2%), when its data differs, or when either side cannot draw it.

This checks backend agreement, not retail fidelity or gameplay equivalence.
Scene replay includes live host palettes and fog; plain-memory replay does not.
The scene inputs precede submission/callback effects, whereas memory is read
after presentation. Those two input kinds are not equivalent scene snapshots.
"""

from __future__ import annotations

import argparse
import base64
import functools
import hashlib
import http.server
import json
import shutil
import struct
import subprocess
import sys
import threading
import time
from datetime import UTC, datetime
from pathlib import Path
from uuid import uuid4

REPO = Path(__file__).resolve().parents[3]
WINDREAM = REPO / "recomp" / "windream"
SOURCE = WINDREAM / "verify" / "native" / "render_parity"
sys.path.insert(0, str(REPO / "recomp"))
sys.path.insert(0, str(WINDREAM))
sys.path.insert(0, str(WINDREAM / "debug"))
import recomp_env  # noqa: E402

OUT = recomp_env.out_dir("render-parity")
CASES = OUT / "cases"
# The projects of the browser demo (recomp/web/demo/make_demo.py, profile wip).
DEMO_PROJECTS = [0, 62, 39, 30, 108, 46, 31, 99, 48, 76]
WIDTH, HEIGHT = 640, 480


# ---- capture (Windows build, control channel) ----


def capture_frame(
    ctl, scene: Path, memory: Path, screenshot: Path, *, attempts: int = 30, steps: int = 6
) -> dict | None:
    """Leave the game paused after a scene, its present, and a memory dump.

    A paused screenshot request advances exactly one frame and saves that
    present. Arm the scene request only after pausing, then use the screenshot
    as the step: a separate step/screenshot sequence would advance another frame.
    Dialogue may suppress 3D; discard any capture made during a retry's free run.
    """
    for _ in range(attempts):
        ctl.pause()
        scene.unlink(missing_ok=True)
        ctl.call("scene_capture", path=str(scene))
        for _ in range(steps):
            screenshot.unlink(missing_ok=True)
            shot = ctl.screenshot(screenshot)
            if not scene.is_file():
                continue
            dump = ctl.call("memory_dump", path=str(memory), timeout=120)
            status = ctl.status()
            if (
                dump["scene_pending"]
                or not status["paused"]
                or dump["frame"] != shot["frame"]
                or status["frame"] != shot["frame"]
            ):
                raise RuntimeError("scene capture did not finish at one paused presentation")
            with scene.open("rb") as source:
                header = source.read(20)
            with memory.open("rb") as source:
                memory_header = source.read(8)
            if (
                len(header) != 20
                or not header.startswith(b"WDS")
                or len(memory_header) != 8
                or memory_header[:4] != b"WDM2"
            ):
                raise RuntimeError("invalid scene or memory capture header")
            root = struct.unpack_from("<I", header, 16)[0]
            if root != struct.unpack_from("<I", memory_header, 4)[0]:
                raise RuntimeError("scene and memory capture have different render roots")
            return {
                "frame": shot["frame"],
                "root": root,
                "memory_bytes": dump["bytes"],
                "screenshot": {key: shot[key] for key in ("width", "height", "format")},
                "stages": {
                    "scene": "inputs retained for this draw, before submission",
                    "screenshot": "the same presentation",
                    "memory": "after presentation, paused; no host/GPU resources",
                },
            }
        ctl.resume()
        ctl.wait(ms=1000)
    return None


def fingerprint(path: Path) -> dict:
    with path.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    return {"bytes": path.stat().st_size, "sha256": digest}


def capture(projects: list[int], shots: int, settle_ms: int) -> int:
    import bank_patch
    import game_nav
    import wdctl

    CASES.mkdir(parents=True, exist_ok=True)
    made = 0
    for project in projects:
        bank = bank_patch.Bank.from_disc(1)
        scene = bank.parsed(project).scene
        if project:
            bank.copy(project, 0)
        tag = f"parity-{project}-{uuid4().hex[:8]}"
        run_dir = recomp_env.out_dir("windream") / f"run-{tag}"
        run_dir.mkdir(parents=True, exist_ok=False)
        bank.write(run_dir / "sandbox")
        print(f"project {project} ({scene})", flush=True)
        game = wdctl.start_game(tag=tag, headless=True, args=["--renderer", "direct"])
        try:
            ctl = game.ctl
            game_nav.boot_into(ctl, scene)
            ctl.wait(ms=settle_ms)
            for shot in range(shots):
                name = f"p{project:03d}-{shot}"
                wds, wdmi = CASES / f"{name}.wds", CASES / f"{name}.wdmi"
                for path in (wds, wdmi):
                    path.unlink(missing_ok=True)
                png = CASES / f"{name}.png"
                evidence = capture_frame(ctl, wds, wdmi, png)
                if evidence is None:
                    print(f"  {name}: no 3D frame after 30 capture attempts, skipped")
                    print("    " + " | ".join(game.stderr_text.splitlines()[-3:]))
                    ctl.resume()
                    break
                executable = Path(game.process.args[0]).resolve()
                evidence["host_executable"] = {"path": str(executable), **fingerprint(executable)}
                evidence["files"] = {path.name: fingerprint(path) for path in (wds, wdmi, png)}
                wds.with_suffix(".capture.json").write_text(json.dumps(evidence, indent=2) + "\n")
                ctl.resume()
                print(
                    f"  {name}: frame {evidence['frame']}, scene {wds.stat().st_size:,} bytes, "
                    f"memory {evidence['memory_bytes']:,} bytes"
                )
                made += 1
                # another view for the next shot: turn, then walk
                ctl.key("LEFT" if shot % 2 == 0 else "UP", ms=700)
                ctl.wait(ms=1500)
        except (wdctl.CtlError, game_nav.NavError) as error:
            print(f"  project {project}: {error}")
        finally:
            game.close(remove=True)
    expected = len(projects) * shots
    print(f"{made}/{expected} requested cases in {CASES}")
    return 0 if made == expected and made else 1


# ---- build ----


def build_native() -> Path:
    env = recomp_env.build_env()
    cmake = recomp_env._cmake(env)
    build = OUT / "build-native"
    subprocess.run(
        [cmake, "-S", str(SOURCE), "-B", str(build), "-G", "Ninja", *recomp_env._compilers(cxx=True),
         f"-DCMAKE_PREFIX_PATH={recomp_env.ensure_sdl3()}", "-DCMAKE_BUILD_TYPE=Release"],
        env=env, check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    subprocess.run([cmake, "--build", str(build)], env=env, check=True, stdout=subprocess.DEVNULL)
    return build


def build_web() -> Path:
    import web_build

    tools = web_build.load_tools()
    env = web_build.web_env(tools)
    cmake = shutil.which("cmake", path=env["PATH"]) or "cmake"
    emscripten = Path(tools["emsdk"]) / "upstream" / "emscripten"
    toolchain = emscripten / "cmake" / "Modules" / "Platform" / "Emscripten.cmake"
    build = OUT / "build-web"
    subprocess.run(
        [cmake, "-S", str(SOURCE), "-B", str(build), "-G", "Ninja",
         f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}", "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_PREFIX_PATH={Path(tools['sdl3']).as_posix()}",
         f"-DSDL3_DIR={env['SDL3_DIR']}".replace("\\", "/"),
         f"-DOD_SHDC_EXECUTABLE={Path(tools['shdc']).as_posix()}",
         f"-DCMAKE_MAKE_PROGRAM={Path(tools['ninja']).as_posix()}"],
        env=env, check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    subprocess.run([cmake, "--build", str(build)], env=env, check=True, stdout=subprocess.DEVNULL)
    return build


# ---- run ----


def case_inputs() -> list[Path]:
    return sorted([*CASES.glob("*.wds"), *CASES.glob("*.wdmi")])


def out_name(path: Path) -> str:
    """p000-0.wds -> p000-0.scene, p000-0.wdmi -> p000-0.memory"""
    return f"{path.stem}.{'memory' if path.suffix == '.wdmi' else 'scene'}"


def run_native(inputs: list[Path], out: Path | None = None) -> dict[str, str]:
    exe = (
        OUT
        / "build-native"
        / ("wd_render_parity.exe" if sys.platform == "win32" else "wd_render_parity")
    )
    out = out or OUT / "native"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    errors = {}
    for path in inputs:
        p = subprocess.run(
            [str(exe), str(path), str(out / out_name(path))], capture_output=True, text=True
        )
        if p.returncode:
            lines = [ln for ln in p.stderr.splitlines() if ln.startswith("FAIL")]
            errors[out_name(path)] = lines[-1] if lines else f"exit {p.returncode}"
    return errors


class Handler(http.server.SimpleHTTPRequestHandler):
    """The build directory, and the cases under /cases/, with the isolation
    headers the pthreads build needs."""

    def translate_path(self, path):
        clean = path.split("?")[0]
        if clean.startswith("/cases/"):
            return str(CASES / clean[len("/cases/") :])
        return super().translate_path(path)

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *args):
        pass


class BrowserUnavailable(RuntimeError):
    """Playwright explicitly reports that the requested Chrome is not installed."""


def launch_browser(chromium, *, headless: bool, args: list[str]):
    try:
        return chromium.launch(headless=headless, args=args, channel="chrome")
    except Exception as error:
        if "Chromium distribution 'chrome' is not found" in str(error):
            raise BrowserUnavailable(str(error)) from error
        raise


def run_web(
    inputs: list[Path], swiftshader: bool, headed: bool, out: Path | None = None
) -> tuple[dict[str, str], str]:
    """The page fetches each input from /cases/<name>, served from CASES."""
    from playwright.sync_api import sync_playwright

    out = out or OUT / "web"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    handler = functools.partial(Handler, directory=str(OUT / "build-web"))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    chrome_args = ["--ignore-gpu-blocklist", "--enable-unsafe-swiftshader"]
    if swiftshader:
        chrome_args += ["--use-gl=angle", "--use-angle=swiftshader", "--disable-gpu-sandbox"]
    errors: dict[str, str] = {}
    gpu = "?"
    try:
        with sync_playwright() as pw:
            browser = launch_browser(pw.chromium, headless=not headed, args=chrome_args)
            page = browser.new_page(viewport={"width": 700, "height": 620})
            console: list[str] = []
            page.on("console", lambda m: console.append(m.text))
            page.on("pageerror", lambda e: console.append(f"pageerror {e}"))
            page.goto(f"{base}/wd_render_parity.html")
            try:
                page.wait_for_function("window.parityReady === true", timeout=120000)
            except Exception:  # noqa: BLE001
                tail = " | ".join(console[-4:])
                return {out_name(p): f"the page never became ready: {tail}" for p in inputs}, gpu
            gpu = page.evaluate(
                "() => { const g = document.createElement('canvas').getContext('webgl2');"
                " const d = g && g.getExtension('WEBGL_debug_renderer_info');"
                " return d ? g.getParameter(d.UNMASKED_RENDERER_WEBGL) : 'unknown'; }"
            )
            page.set_default_timeout(300000)
            for path in inputs:
                name = out_name(path)
                try:
                    result = page.evaluate(
                        "([url, name]) => window.parityCase(url, name)",
                        [f"{base}/cases/{path.name}", path.name],
                    )
                except Exception as error:  # noqa: BLE001 - a crashed page fails the case
                    errors[name] = f"page error: {str(error).splitlines()[0]}"
                    page.goto(
                        f"{base}/wd_render_parity.html"
                    )  # a trap ends the module: start again
                    page.wait_for_function("window.parityReady === true", timeout=120000)
                    continue
                if result["code"]:
                    errors[name] = result["error"] or f"exit {result['code']}"
                    continue
                for which, suffix in (("rgba", ".rgba"), ("wds", ".wds")):
                    size, data = result[which], bytearray()
                    for begin in range(0, size, 1 << 22):
                        text = page.evaluate(
                            "([w, b, e]) => window.paritySlice(w, b, e)",
                            [which, begin, min(size, begin + (1 << 22))],
                        )
                        data += base64.b64decode(text)
                    if size:
                        (out / (name + suffix)).write_bytes(bytes(data))
            browser.close()
    finally:
        server.shutdown()
    for path in inputs:  # a page that died leaves the rest undone
        name = out_name(path)
        if name not in errors and not (out / (name + ".rgba")).is_file():
            errors[name] = "no frame came back from the page"
    return errors, gpu


# ---- compare ----


def read_rgba(path: Path):
    raw = path.read_bytes()
    width, height = struct.unpack_from("<2I", raw)
    assert (width, height) == (WIDTH, HEIGHT) and len(raw) == 8 + width * height * 4, path
    return raw[8:]


def pixel_stats(a: bytes, b: bytes, tolerance: int) -> dict:
    """Channel differences of two RGBA8 frames (alpha ignored)."""
    differing = beyond = worst = 0
    for i in range(0, len(a), 4):
        if a[i : i + 3] == b[i : i + 3]:
            continue
        d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
        differing += 1
        beyond += d > tolerance
        worst = max(worst, d)
    return {"differing": differing, "beyond": beyond, "worst": worst}


def blank(frame: bytes) -> bool:
    return len({frame[i : i + 3] for i in range(0, len(frame), 4 * 97)}) < 4


def write_images(name: str, native: bytes, web: bytes, tolerance: int) -> None:
    try:
        from PIL import Image
    except ImportError:
        return
    report = OUT / "report"
    report.mkdir(exist_ok=True)
    a = Image.frombytes("RGBA", (WIDTH, HEIGHT), native).convert("RGB")
    b = Image.frombytes("RGBA", (WIDTH, HEIGHT), web).convert("RGB")
    diff = Image.new("RGB", (WIDTH, HEIGHT))
    pa, pb, pd = a.load(), b.load(), diff.load()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            d = max(abs(u - v) for u, v in zip(pa[x, y], pb[x, y], strict=True))
            pd[x, y] = (255, 0, 0) if d > tolerance else (60, 60, 0) if d else (0, 0, 0)
    sheet = Image.new("RGB", (WIDTH * 3, HEIGHT))
    for i, image in enumerate((a, b, diff)):
        sheet.paste(image, (WIDTH * i, 0))
    sheet.save(report / f"{name}.png")  # Windows | browser | difference


def compare(inputs, native_errors, web_errors, tolerance: int, budget: float) -> list[dict]:
    rows = []
    for path in inputs:
        name = out_name(path)
        row = {"case": name, "ok": False}
        rows.append(row)
        if name in native_errors or name in web_errors:
            row["error"] = "; ".join(
                f"{side}: {errors[name]}"
                for side, errors in (("windows", native_errors), ("browser", web_errors))
                if name in errors
            )
            continue
        native, web = OUT / "native" / name, OUT / "web" / name
        if path.suffix == ".wdmi":
            a = (native.parent / (name + ".wds")).read_bytes()
            b = (web.parent / (name + ".wds")).read_bytes()
            row["data_bytes"] = len(a)
            row["data_same"] = a == b
            row["data_sha256"] = hashlib.sha256(a).hexdigest()[:16]
        frame_a = read_rgba(native.parent / (name + ".rgba"))
        frame_b = read_rgba(web.parent / (name + ".rgba"))
        row["native_rgba_sha256"] = hashlib.sha256(frame_a).hexdigest()
        row["web_rgba_sha256"] = hashlib.sha256(frame_b).hexdigest()
        row.update(pixel_stats(frame_a, frame_b, tolerance))
        row["blank"] = blank(frame_a) or blank(frame_b)
        row["ok"] = (
            row.get("data_same", True)
            and not row["blank"]
            and row["beyond"] <= budget * WIDTH * HEIGHT
        )
        if not row["ok"] or row["differing"]:
            write_images(name, frame_a, frame_b, tolerance)
    return rows


def write_report(
    inputs: list[Path], rows: list[dict], gpu: str, *, tolerance: int, budget: float
) -> None:
    """Tie a comparison to its bytes, binaries and current source contents.

    Source fingerprints record the checkout at comparison time, not proof that
    an existing executable was compiled from it. Capture sidecars, when present,
    identify the separate game executable that produced the recorded inputs.
    """
    source_files = {
        Path(__file__),
        WINDREAM / "verify/native/render_parity.cpp",
        REPO / "recomp/recomp_env.py",
        WINDREAM / "web_build.py",
    }
    for directory in (SOURCE, WINDREAM / "host/render", REPO / "recomp/render"):
        source_files.update(
            path
            for path in directory.rglob("*")
            if path.is_file()
            and path.suffix in {".c", ".cpp", ".h", ".mm", ".glsl", ".cmake", ".txt", ".html"}
        )
    binaries = [
        OUT / "build-native" / recomp_env.exe_name("wd_render_parity"),
        OUT / "build-web/wd_render_parity.wasm",
        OUT / "build-web/wd_render_parity.js",
    ]
    captures = {}
    capture_validation = {}
    for path in inputs:
        sidecar = path.with_suffix(".capture.json")
        if sidecar.is_file():
            record = json.loads(sidecar.read_text())
            captures[sidecar.name] = record
            mismatched = [
                name
                for name, expected in record["files"].items()
                if not (sidecar.parent / name).is_file()
                or fingerprint(sidecar.parent / name) != expected
            ]
            capture_validation[sidecar.name] = {
                "files_match": not mismatched,
                "mismatched_files": mismatched,
            }
    report = {
        "created_utc": datetime.now(UTC).isoformat(),
        "gpu": gpu,
        "scope": "D3D11/WebGL2 backend agreement for each recorded input; "
        "not retail fidelity or gameplay equivalence",
        "tolerance": tolerance,
        "budget": budget,
        "size": [WIDTH, HEIGHT],
        "inputs": {str(path): fingerprint(path) for path in inputs},
        "capture_records": captures,
        "capture_record_validation": capture_validation,
        "binaries": {str(path): fingerprint(path) if path.is_file() else None for path in binaries},
        "sources_at_comparison": {
            path.relative_to(REPO).as_posix(): fingerprint(path) for path in sorted(source_files)
        },
        "cases": rows,
    }
    (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n")


def run(swiftshader: bool, headed: bool, tolerance: int, budget: float) -> int:
    inputs = case_inputs()
    if not inputs:
        print(f"no cases in {CASES}: run the capture step first")
        return 2
    t0 = time.time()
    native_errors = run_native(inputs)
    web_errors, gpu = run_web(inputs, swiftshader, headed)
    rows = compare(inputs, native_errors, web_errors, tolerance, budget)
    write_report(inputs, rows, gpu, tolerance=tolerance, budget=budget)
    print(f"browser GPU: {gpu}")
    for row in rows:
        if "error" in row:
            print(f"FAIL {row['case']}: {row['error']}")
            continue
        data = (
            ""
            if "data_same" not in row
            else " data same,"
            if row["data_same"]
            else " DATA DIFFERS,"
        )
        print(
            f"{'ok  ' if row['ok'] else 'FAIL'} {row['case']}:{data} {row['differing']} pixels differ, "
            f"{row['beyond']} by more than {tolerance}, worst {row['worst']}"
            f"{', BLANK FRAME' if row['blank'] else ''}"
        )
    failed = [r for r in rows if not r["ok"]]
    print(
        f"{len(rows) - len(failed)}/{len(rows)} cases pass in {time.time() - t0:.0f}s; "
        f"report {OUT / 'report.json'}, images {OUT / 'report'}"
    )
    return 1 if failed else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("step", choices=["capture", "build", "run", "all"])
    ap.add_argument("--projects", default=",".join(map(str, DEMO_PROJECTS)))
    ap.add_argument("--shots", type=int, default=3, help="frames captured per project")
    ap.add_argument("--settle-ms", type=int, default=3000)
    ap.add_argument("--swiftshader", action="store_true", help="software WebGL2 instead of the GPU")
    ap.add_argument("--headed", action="store_true")
    ap.add_argument("--tolerance", type=int, default=2)
    ap.add_argument("--budget", type=float, default=0.002)
    args = ap.parse_args()
    if args.step == "capture" or (args.step == "all" and not case_inputs()):
        rc = capture([int(p) for p in args.projects.split(",")], args.shots, args.settle_ms)
        if rc or args.step == "capture":
            return rc
    if args.step in ("build", "all"):
        build_native()
        build_web()
        if args.step == "build":
            return 0
    return run(args.swiftshader, args.headed, args.tolerance, args.budget)


if __name__ == "__main__":
    sys.exit(main())
