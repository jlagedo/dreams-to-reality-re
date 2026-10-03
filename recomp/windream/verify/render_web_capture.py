"""Capture the browser build's own frames in a project and check them against retail and Windows.

uv run --with unicorn --with pillow --with playwright \\
    python recomp/windream/verify/render_web_capture.py all --project 46
uv run --with playwright --with pillow \\
    python recomp/windream/verify/render_web_capture.py capture --project 46 --shots 3
uv run --with unicorn --with pillow --with playwright \\
    python recomp/windream/verify/render_web_capture.py analyze --project 46

capture: the demo pack with its DREAMS.DAT replaced by a bank whose slot 0 is
the project (bank_patch.Bank.copy), so New Game starts there; package.py and
serve.py as for a deploy; the page with ?autostart in Chrome on the GPU. After
the level's autosave the page asks the engine for frames (host/web/web_glue.c,
WD_WEB_CAPTURE): per shot the scene inputs (.wds), the frame as presented
(.rgba, .png) and the committed guest memory (.wdmi), in render_parity's order.
No key is pressed: the frames are the spawn view.

analyze, per shot:
- retail on the browser's memory: render_pose_sweep.py with the captured pose
  and generated ones (faces retail draws against the direct renderer's);
- the level against the Windows build's memory image of the same project
  (render_parity.py capture): the adapter's snapshot of both, node by node
  (vertices, faces, transforms), so state the browser computed differently
  shows by node;
- the live frame against its own scene inputs replayed by render_parity's
  harness (Windows D3D11 and WebGL2): a live-only difference (host palettes,
  fog, timing) shows as live pixels the replay does not have.

Everything goes to DREAMS_OUT/recomp/web-capture/ (game-derived).
"""

from __future__ import annotations

import argparse
import base64
import ctypes
import hashlib
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "recomp"))
sys.path.insert(0, str(REPO / "recomp" / "windream" / "debug"))
import recomp_env  # noqa: E402

OUT = recomp_env.out_dir("web-capture")
ENGINE = recomp_env.out_dir("windream", "build-web")
DEMO = recomp_env.out_dir("web", "demo")
PARITY_CASES = recomp_env.out_dir("render-parity") / "cases"


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


# ---- capture ----


def demo_bank():
    from bank_patch import Bank

    manifest = json.loads((DEMO / "manifest.json").read_text())
    entry = next(e for e in manifest["files"] if e["path"].upper() == "DREAMS.DAT")
    return Bank(b"".join((DEMO / c["url"]).read_bytes() for c in entry["chunks"]))


def bank_for(project: int, spawn=None) -> bytes:
    """The demo's bank with slot 0 = the project (New Game starts there),
    optionally with another spawn position."""
    bank = demo_bank()
    bank.copy(project, 0)
    if spawn is not None:
        bank.set_spawn(0, tuple(int(v) for v in spawn))
    return bank.data()


def set_pack_bank(pack: Path, data: bytes, name: str) -> None:
    """Point a pack's manifest at another DREAMS.DAT (a new chunk, so the
    page's cache, keyed by the manifest, cannot serve the old one)."""
    manifest = json.loads((pack / "manifest.json").read_text())
    entry = next(e for e in manifest["files"] if e["path"].upper() == "DREAMS.DAT")
    digest = hashlib.sha256(data).hexdigest()
    url = f"{digest[:10]}-DREAMS.DAT.000"
    (pack / url).write_bytes(data)
    entry.update(size=len(data), sha256=digest, chunks=[{"url": url, "size": len(data)}])
    manifest["name"] = name
    (pack / "manifest.json").write_text(json.dumps(manifest, indent=1))


def derived_pack(project: int) -> Path:
    """The demo pack, hard-linked, with DREAMS.DAT slot 0 = the project."""
    pack = OUT / f"pack-p{project:03d}"
    shutil.rmtree(pack, ignore_errors=True)
    pack.mkdir(parents=True)
    for path in DEMO.iterdir():
        try:
            os.link(path, pack / path.name)
        except OSError:
            shutil.copy2(path, pack / path.name)
    (pack / "manifest.json").unlink()
    shutil.copy2(DEMO / "manifest.json", pack / "manifest.json")
    set_pack_bank(pack, bank_for(project), f"demo-p{project:03d}")
    return pack


def package(pack: Path) -> Path:
    dist = OUT / "dist"
    subprocess.run(
        [sys.executable, str(REPO / "recomp/web/package.py"), "--engine", str(ENGINE),
         "--demo", str(pack), "--out", str(dist), "--clean"],
        check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    return dist


READ_JS = """(path) => {
  const M = window.dreamsPage.Module;
  try { window.__wdBuf = M.FS.readFile(path); } catch (e) { window.__wdBuf = null; return -1; }
  return window.__wdBuf.length;
}"""
SLICE_JS = """([b, e]) => {
  const bytes = window.__wdBuf.subarray(b, e); let text = '';
  for (let i = 0; i < bytes.length; i += 0x8000)
    text += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(text);
}"""


def read_file(page, path: str) -> bytes | None:
    size = page.evaluate(READ_JS, path)
    if size < 0:
        return None
    data = bytearray()
    for begin in range(0, size, 1 << 22):
        data += base64.b64decode(page.evaluate(SLICE_JS, [begin, min(size, begin + (1 << 22))]))
    return bytes(data)


def rgba_png(raw: bytes, path: Path) -> tuple[int, int]:
    from PIL import Image

    w, h = struct.unpack_from("<2I", raw)
    Image.frombytes("RGBA", (w, h), raw[8 : 8 + w * h * 4]).convert("RGB").save(path)
    return w, h


class BrowserRun:
    """The packaged page served as for a deploy and one Chrome; each session is
    a fresh context (no cache, no saves) that starts the game and takes shots."""

    def __init__(self, project: int, swiftshader: bool, headed: bool):
        from playwright.sync_api import sync_playwright

        self.rp = load("wd_render_parity_c", HERE / "render_parity.py")
        os.environ.setdefault("WD_WEB_QUIET", "1")
        serve = load("dreams_web_serve_c", REPO / "recomp/web/serve.py")
        self.project = project
        self.dist = package(derived_pack(project))
        self.httpd = serve.make_server(self.dist, 0)
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.httpd.server_address[1]}/?autostart"
        args = ["--ignore-gpu-blocklist", "--enable-unsafe-swiftshader",
                "--autoplay-policy=no-user-gesture-required"]  # fmt: skip
        if swiftshader:
            args += ["--use-gl=angle", "--use-angle=swiftshader", "--disable-gpu-sandbox"]
        self.pw = sync_playwright().start()
        self.browser = self.rp.launch_browser(self.pw.chromium, headless=not headed, args=args)
        self.wasm_sha256 = hashlib.sha256((ENGINE / "dreams.wasm").read_bytes()).hexdigest()

    def close(self):
        self.browser.close()
        self.pw.stop()
        self.httpd.shutdown()

    def set_bank(self, data: bytes, name: str):
        set_pack_bank(self.dist / "demo", data, name)

    def session(self, prefix: str, shots: int, settle_ms: int, cases: Path) -> list[str]:
        """Start the game, wait for the level's autosave, take shots. The names
        made, <prefix>-<shot>.{wds,rgba,wdmi,png,capture.json}."""
        context = self.browser.new_context(viewport={"width": 1280, "height": 960})
        page = context.new_page()
        console: list[str] = []
        t0 = time.time()
        page.on("console", lambda m: console.append(f"{time.time() - t0:7.2f} {m.text}"))
        page.on("pageerror", lambda e: console.append(f"{time.time() - t0:7.2f} pageerror {e}"))
        made = []
        try:
            page.goto(self.url)
            deadline = time.time() + 180
            while time.time() < deadline and not any("[save] open W" in c for c in console):
                if any("status fatal" in c or "pageerror" in c for c in console):
                    break
                page.wait_for_timeout(250)
            if not any("[save] open W" in c for c in console):
                print(f"{prefix}: the level never autosaved; console tail:")
                print("\n".join(console[-15:]))
                return made
            gpu = page.evaluate(
                "() => { const g = document.createElement('canvas').getContext('webgl2');"
                " const d = g && g.getExtension('WEBGL_debug_renderer_info');"
                " return d ? g.getParameter(d.UNMASKED_RENDERER_WEBGL) : 'unknown'; }"
            )
            for shot in range(shots):
                page.wait_for_timeout(settle_ms)
                if not page.evaluate("() => window.dreamsPage.Module._wd_web_capture()"):
                    print(f"{prefix} shot {shot}: a capture is already running")
                    continue
                state, deadline = 1, time.time() + 60
                while state in (1, 2) and time.time() < deadline:
                    page.wait_for_timeout(100)
                    state = page.evaluate("() => window.dreamsPage.Module._wd_web_capture_state()")
                if state != 3:
                    print(f"{prefix} shot {shot}: capture state {state} (no 3D frame?)")
                    continue
                name = f"{prefix}-{shot}"
                files = {}
                for suffix in ("wds", "rgba", "wdmi"):
                    data = read_file(page, f"/capture/frame.{suffix}")
                    if data is None:
                        raise RuntimeError(f"/capture/frame.{suffix} is missing")
                    (cases / f"{name}.{suffix}").write_bytes(data)
                    files[f"{name}.{suffix}"] = hashlib.sha256(data).hexdigest()
                size = rgba_png((cases / f"{name}.rgba").read_bytes(), cases / f"{name}.png")
                record = {
                    "project": self.project,
                    "shot": shot,
                    "seconds_after_load": round(time.time() - t0, 1),
                    "frame_size": size,
                    "gpu": gpu,
                    "engine_wasm_sha256": self.wasm_sha256,
                    "files": files,
                }
                (cases / f"{name}.capture.json").write_text(json.dumps(record, indent=2) + "\n")
                print(f"{name}: {size[0]}x{size[1]} on {gpu}", flush=True)
                made.append(name)
        finally:
            (OUT / "logs").mkdir(exist_ok=True)
            (OUT / "logs" / f"{prefix}.log").write_text("\n".join(console) + "\n")
            context.close()
        return made


def capture(project: int, shots: int, settle_ms: int, swiftshader: bool, headed: bool) -> int:
    if not (ENGINE / "dreams.js").is_file() or not (DEMO / "manifest.json").is_file():
        print(f"needs the browser build ({ENGINE}) and the demo pack ({DEMO})")
        return 2
    cases = OUT / "cases"
    cases.mkdir(parents=True, exist_ok=True)
    for old in cases.glob(f"p{project:03d}-web-*"):
        old.unlink()
    run = BrowserRun(project, swiftshader, headed)
    try:
        made = run.session(f"p{project:03d}-web", shots, settle_ms, cases)
    finally:
        run.close()
    print(f"{len(made)}/{shots} shots in {cases}; console in {OUT / 'logs'}")
    return 0 if len(made) == shots else 1


# ---- analyze ----


def snapshot_of(image: Path, sweep_module, dll, scratch: Path):
    sweep = sweep_module.Sweep(image, dll, scratch.parent)
    return sweep, sweep.capture(scratch)


def level_diff(browser_snapshot, windows_snapshot) -> dict:
    """Node by node in the adapter's pre-order: transforms, source vertices and
    faces. Animated nodes differ with time; the level's own should not."""
    _, bn, bv, bf = browser_snapshot
    _, wn, wv, wf = windows_snapshot
    result = {
        "nodes": [len(bn), len(wn)],
        "vertices": [len(bv), len(wv)],
        "faces": [len(bf), len(wf)],
        "same_addresses": [n["address"] for n in bn] == [n["address"] for n in wn],
    }
    if len(bn) != len(wn):
        result["note"] = "different node trees: compared up to the shorter"

    def faces_by_owner(faces):
        owners: dict[int, list] = {}
        for f in faces:
            corners = tuple((c[0], c[1], round(c[2], 3), round(c[3], 3)) for c in f["corners"])
            owners.setdefault(f["owner"], []).append(
                (f["kind"], tuple(f["normal"]), f["distance"], corners)
            )
        return owners

    bfo, wfo = faces_by_owner(bf), faces_by_owner(wf)
    differing = []
    for i in range(min(len(bn), len(wn))):
        b, w = bn[i], wn[i]
        bverts = [xyz for _, xyz in bv[b["first"] : b["first"] + b["count"]]]
        wverts = [xyz for _, xyz in wv[w["first"] : w["first"] + w["count"]]]
        vertex_diffs = sum(1 for x, y in zip(bverts, wverts, strict=False) if x != y) + abs(
            len(bverts) - len(wverts)
        )
        bfaces, wfaces = bfo.get(i, []), wfo.get(i, [])
        face_diffs = sum(1 for x, y in zip(bfaces, wfaces, strict=False) if x != y) + abs(
            len(bfaces) - len(wfaces)
        )
        transform = b["local"] != w["local"]
        if vertex_diffs or face_diffs or transform or b["active"] != w["active"]:
            differing.append(
                {
                    "node": i,
                    "address": [hex(b["address"]), hex(w["address"])],
                    "faces": len(wfaces),
                    "vertices": len(wverts),
                    "vertex_diffs": vertex_diffs,
                    "face_diffs": face_diffs,
                    "transform_differs": transform,
                    "submitted": [b["active"], w["active"]],
                }
            )
    differing.sort(key=lambda d: -(d["face_diffs"] * 10 + d["vertex_diffs"]))
    result["differing_nodes"] = len(differing)
    result["differing_geometry_nodes"] = sum(1 for d in differing if d["face_diffs"])
    result["differences"] = differing[:40]
    return result


def live_against_replay(name: str, cases: Path, out: Path, swiftshader: bool) -> dict:
    """The live frame against its scene inputs (.wds, with the host palettes and
    fog) replayed on D3D11 and WebGL2 at 640x480."""
    from PIL import Image, ImageChops, ImageStat

    rp = load("wd_render_parity_a", HERE / "render_parity.py")
    work = out / "replay"
    shutil.rmtree(work, ignore_errors=True)
    (work / "cases").mkdir(parents=True)
    shutil.copy2(cases / f"{name}.wds", work / "cases" / f"{name}.wds")
    rp.CASES = work / "cases"
    inputs = [work / "cases" / f"{name}.wds"]
    native_errors = rp.run_native(inputs, work / "native")
    web_errors, gpu = rp.run_web(inputs, swiftshader, False, work / "web")
    case = rp.out_name(inputs[0])
    if case in native_errors or case in web_errors:
        return {"error": native_errors.get(case) or web_errors.get(case)}
    # The harness draws the camera's projection over its whole 640x480; the
    # live frame shows it only in the camera's viewport (the letterbox and HUD
    # cover the rest). Compare that rectangle of both.
    from render_scene_smoke import read_snapshot

    camera = read_snapshot(cases / f"{name}.wds")[0]
    vx, vy, vw, vh = camera["viewport"]
    sw, sh = camera["screen"]
    rect = (vx * rp.WIDTH // sw, vy * rp.HEIGHT // sh,
            (vx + vw) * rp.WIDTH // sw, (vy + vh) * rp.HEIGHT // sh)  # fmt: skip
    frames = {}
    for side in ("native", "web"):
        raw = rp.read_rgba(work / side / f"{case}.rgba")
        frame = Image.frombytes("RGBA", (rp.WIDTH, rp.HEIGHT), raw).convert("RGB")
        frames[side] = frame.crop(rect).resize((rp.WIDTH, rp.HEIGHT))
    live = Image.open(cases / f"{name}.png").convert("RGB").resize((rp.WIDTH, rp.HEIGHT))
    live = live.crop(rect).resize((rp.WIDTH, rp.HEIGHT))
    sheet = Image.new("RGB", (rp.WIDTH * 3, rp.HEIGHT))
    for i, image in enumerate((live, frames["web"], frames["native"])):
        sheet.paste(image, (rp.WIDTH * i, 0))
    sheet.save(out / f"{name}-live-webgl-windows.png")  # live | replay WebGL2 | replay D3D11

    def dark_only_in(a, b):
        """Pixels near black in a but lit in b, as a fraction of the frame."""
        pa, pb = a.convert("L").load(), b.convert("L").load()
        count = sum(
            1 for y in range(rp.HEIGHT) for x in range(rp.WIDTH) if pa[x, y] < 8 and pb[x, y] > 40
        )
        return round(count / (rp.WIDTH * rp.HEIGHT), 4)

    def mean_diff(a, b):
        return round(sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3, 2)

    return {
        "gpu": gpu,
        "replay_webgl_vs_windows_mean_diff": mean_diff(frames["web"], frames["native"]),
        "live_vs_replay_mean_diff": mean_diff(live, frames["web"]),
        "dark_in_live_lit_in_replay": dark_only_in(live, frames["web"]),
        "dark_in_replay_lit_in_live": dark_only_in(frames["web"], live),
        "sheet": str(out / f"{name}-live-webgl-windows.png"),
    }


def analyze(project: int, windows: Path | None, poses: int, swiftshader: bool) -> int:
    sys.path.insert(0, str(HERE))
    import render_pose_sweep as sweep_module

    cases = OUT / "cases"
    images = sorted(cases.glob(f"p{project:03d}-web-*.wdmi"))
    if not images:
        print(f"no browser captures for project {project} in {cases}: run capture first")
        return 2
    windows = windows or PARITY_CASES / f"p{project:03d}-0.wdmi"
    dll = ctypes.CDLL(
        str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll")
    )
    report = {"project": project, "windows_image": str(windows), "shots": {}}
    sweep = subprocess.run(
        [sys.executable, str(HERE / "render_pose_sweep.py"), *map(str, images),
         "--poses", str(poses)],
        capture_output=True, text=True,
    )  # fmt: skip
    print(sweep.stdout.splitlines()[-1] if sweep.stdout else sweep.stderr[-2000:])
    windows_snapshot = None
    if windows.is_file():
        _, windows_snapshot = snapshot_of(windows, sweep_module, dll, OUT / "windows.wds")
    for image in images:
        name = image.stem
        out = OUT / "analysis" / name
        out.mkdir(parents=True, exist_ok=True)
        entry = report["shots"][name] = {}
        pose_report = json.loads(
            (recomp_env.out_dir("pose-sweep", name) / "report.json").read_text()
        )
        captured = pose_report["results"][0]
        entry["retail_vs_direct_captured_pose"] = {
            k: captured[k]
            for k in ("retail_faces", "direct_faces", "missing_pixels", "extra_pixels")
        }
        entry["retail_vs_direct_sweep"] = {
            k: pose_report[k]
            for k in ("poses", "failed_poses", "missing_pixels_by_stage", "poses_with_extra")
        }
        _, browser_snapshot = snapshot_of(image, sweep_module, dll, out / "browser.wds")
        if windows_snapshot:
            entry["level_vs_windows"] = level_diff(browser_snapshot, windows_snapshot)
        entry["live_vs_replay"] = live_against_replay(name, cases, out, swiftshader)
        print(f"{name}: " + json.dumps(entry)[:1500], flush=True)
    (OUT / f"report-p{project:03d}.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"report {OUT / f'report-p{project:03d}.json'}")
    return 0


# ---- spawns: the level from many places, browser and Windows ----


def spawn_points(project: int, count: int, windows: Path, sweep_module, dll):
    """Spawn positions over the level's floors: the horizontal faces of the
    Windows build's memory image that are big enough to stand on and have a
    roof, at the record's own height over its floor, farthest-first from the
    record's spawn."""
    import math

    from bank_patch import SPAWN_AT

    sweep, snapshot = snapshot_of(windows, sweep_module, dll, OUT / "windows-spawns.wds")
    camera = sweep_module.Camera(sweep.eye, sweep.rotation)
    down, axes = camera.down, camera.horizontal
    axis = next(a for a in range(3) if down[a])
    horizontals = sweep_module.horizontal_faces(snapshot, down)
    spawn = struct.unpack_from("<3i", demo_bank().records[project], SPAWN_AT)

    def along(p):
        return sweep_module.along(p, down)

    floors = [
        h for h in sweep_module.surfaces_at(spawn, horizontals, down, axes) if h > along(spawn)
    ]
    lift = along(spawn) - min(floors) if floors else -200.0  # negative: above the floor
    candidates = []
    for corners in horizontals:
        a, b = axes
        area = (
            abs(
                (corners[1][a] - corners[0][a]) * (corners[2][b] - corners[0][b])
                - (corners[2][a] - corners[0][a]) * (corners[1][b] - corners[0][b])
            )
            / 2
        )
        if area < 4 * lift * lift:
            continue
        centre = [sum(c[k] for c in corners) / 3 for k in range(3)]
        probe = [centre[k] - 10 * down[k] for k in range(3)]
        heights = sweep_module.surfaces_at(probe, horizontals, down, axes)
        below = [h for h in heights if h > along(probe)]
        above = [h for h in heights if h < along(probe)]
        if not below or abs(min(below) - along(centre)) > 1 or not above:
            continue
        if along(centre) - max(above) < 3 * abs(lift):
            continue  # too low to stand in
        point = list(centre)
        point[axis] = (along(centre) + lift) * down[axis]
        candidates.append(point)
    chosen = [list(spawn)]
    while len(chosen) < count and candidates:
        best = max(candidates, key=lambda p: min(math.dist(p, q) for q in chosen))
        chosen.append(best)
        candidates.remove(best)
    return [tuple(round(v) for v in p) for p in chosen], {
        "record_spawn": spawn,
        "height_over_floor": round(-lift),
        "candidates": len(candidates) + len(chosen) - 1,
    }


def windows_session(project: int, spawn, prefix: str, settle_ms: int, cases: Path):
    """The Windows build started in the project at the spawn; one paused capture."""
    from uuid import uuid4

    import bank_patch
    import game_nav
    import wdctl

    rp = load("wd_render_parity_w", HERE / "render_parity.py")
    bank = bank_patch.Bank.from_disc(1)
    scene = bank.parsed(project).scene
    bank.copy(project, 0)
    bank.set_spawn(0, spawn)
    tag = f"spawn-{project}-{uuid4().hex[:8]}"
    run_dir = recomp_env.out_dir("windream") / f"run-{tag}"
    run_dir.mkdir(parents=True, exist_ok=False)
    bank.write(run_dir / "sandbox")
    game = wdctl.start_game(tag=tag, headless=True, args=["--renderer", "direct"])
    name = f"{prefix}-0"
    try:
        game_nav.boot_into(game.ctl, scene)
        game.ctl.wait(ms=settle_ms)
        evidence = rp.capture_frame(
            game.ctl, cases / f"{name}.wds", cases / f"{name}.wdmi", cases / f"{name}.png"
        )
        if evidence is None:
            print(f"{name}: no 3D frame")
            return None
        (cases / f"{name}.capture.json").write_text(json.dumps(evidence, indent=2) + "\n")
        print(f"{name}: frame {evidence['frame']}", flush=True)
        return name
    except (wdctl.CtlError, game_nav.NavError) as error:
        print(f"{name}: {error}")
        return None
    finally:
        game.close(remove=True)


def frame_against(a_png: Path, b_png: Path, wds: Path, sheet: Path) -> dict:
    """Two frames inside the 3D viewport: pixels dark in a and lit in b."""
    from PIL import Image, ImageChops, ImageStat
    from render_scene_smoke import read_snapshot

    camera = read_snapshot(wds)[0]
    vx, vy, vw, vh = camera["viewport"]
    sw, sh = camera["screen"]
    images = []
    for path in (a_png, b_png):
        image = Image.open(path).convert("RGB").resize((sw, sh))
        images.append(image.crop((vx, vy, vx + vw, vy + vh)))
    a, b = images
    pa, pb = a.convert("L").load(), b.convert("L").load()
    dark = sum(1 for y in range(vh) for x in range(vw) if pa[x, y] < 8 and pb[x, y] > 40)
    out = Image.new("RGB", (vw * 2, vh))
    out.paste(a, (0, 0))
    out.paste(b, (vw, 0))
    out.save(sheet)
    return {
        "mean_diff": round(sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3, 2),
        "dark_here_lit_there": round(dark / (vw * vh), 4),
        "sheet": str(sheet),
    }


def submitted_nodes(snapshot) -> dict[int, int]:
    _camera, nodes, _vertices, faces = snapshot
    counts: dict[int, int] = {}
    for f in faces:
        counts[f["owner"]] = counts.get(f["owner"], 0) + 1
    return {n["address"]: counts.get(i, 0) for i, n in enumerate(nodes) if n["active"]}


def spawns(project, count, settle_ms, swiftshader, headed, windows: Path | None) -> int:
    sys.path.insert(0, str(HERE))
    import render_pose_sweep as sweep_module

    windows = windows or PARITY_CASES / f"p{project:03d}-0.wdmi"
    if not windows.is_file():
        print(f"needs a Windows memory image of project {project}: render_parity.py capture")
        return 2
    dll = ctypes.CDLL(
        str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll")
    )
    points, info = spawn_points(project, count, windows, sweep_module, dll)
    print(f"{len(points)} spawn points ({info})", flush=True)
    cases = OUT / "spawns" / f"p{project:03d}"
    shutil.rmtree(cases, ignore_errors=True)
    cases.mkdir(parents=True)
    run = BrowserRun(project, swiftshader, headed)
    pairs = []
    try:
        for i, spawn in enumerate(points):
            run.set_bank(bank_for(project, spawn), f"demo-p{project:03d}-s{i:02d}")
            browser = run.session(f"s{i:02d}-web", 1, settle_ms, cases)
            pairs.append([spawn, browser[0] if browser else None])
    finally:
        run.close()
    for i, pair in enumerate(pairs):
        pair.append(windows_session(project, pair[0], f"s{i:02d}-win", settle_ms, cases))
    results = []
    for i, (spawn, web, win) in enumerate(pairs):
        row = {"spawn": spawn, "browser": web, "windows": win}
        results.append(row)
        snaps = {}
        for side, name in (("browser", web), ("windows", win)):
            if not name:
                continue
            sweep = sweep_module.Sweep(cases / f"{name}.wdmi", dll, cases)
            pose = {"eye": list(sweep.eye), "rotation": list(sweep.rotation)}
            summary, *_ = sweep_module.run_pose(sweep, pose, cases / f"{name}-pose.wds")
            row[f"{side}_retail_vs_direct"] = {
                k: summary[k] for k in ("retail_faces", "direct_faces", "missing_pixels")
            }
            row[f"{side}_eye"] = list(sweep.eye)
            snaps[side] = sweep_module.read_snapshot(cases / f"{name}-pose.wds")
        if web:
            row["browser_live_vs_replay"] = live_against_replay(
                web, cases, OUT / "spawns" / "analysis" / web, swiftshader
            )
        if web and win:
            row["browser_vs_windows_frame"] = frame_against(
                cases / f"{web}.png", cases / f"{win}.png", cases / f"{web}.wds",
                cases / f"s{i:02d}-browser-windows.png",
            )  # fmt: skip
            b, w = submitted_nodes(snaps["browser"]), submitted_nodes(snaps["windows"])
            row["nodes_only_windows_draws"] = {hex(a): w[a] for a in w if a not in b}
            row["nodes_only_browser_draws"] = {hex(a): b[a] for a in b if a not in w}
            row["level_vs_windows"] = {
                k: v
                for k, v in level_diff(snaps["browser"], snaps["windows"]).items()
                if k != "differences"
            }
        flags = []
        frame = row.get("browser_vs_windows_frame", {})
        if frame.get("dark_here_lit_there", 0) > 0.02:
            flags.append("browser dark where Windows is lit")
        if row.get("browser_live_vs_replay", {}).get("dark_in_live_lit_in_replay", 0) > 0.02:
            flags.append("live dark where its replay is lit")
        if sum(row.get("browser_retail_vs_direct", {}).get("missing_pixels", {}).values()) >= 64:
            flags.append("direct misses faces retail draws")
        if row.get("level_vs_windows", {}).get("differing_geometry_nodes"):
            flags.append("level geometry differs from Windows")
        row["flags"] = flags
        print(f"s{i:02d} {spawn}: {flags or 'ok'} " + json.dumps(frame)[:200], flush=True)
    report = {"project": project, "points": info, "settle_ms": settle_ms, "spawns": results}
    (OUT / f"spawns-p{project:03d}.json").write_text(json.dumps(report, indent=2) + "\n")
    return same_camera(project)


def same_camera(project: int) -> int:
    """Each spawn's two memory images seen from one camera, the browser's: the
    builds' cameras turn at their own pace, so their frames differ by a few
    degrees. With the camera fixed, what retail draws and what the frame shows
    differ only where the game state does."""
    sys.path.insert(0, str(HERE))
    import render_pose_sweep as sweep_module
    from PIL import Image

    rp = load("wd_render_parity_s", HERE / "render_parity.py")
    path = OUT / f"spawns-p{project:03d}.json"
    report = json.loads(path.read_text())
    cases = OUT / "spawns" / f"p{project:03d}"
    work = OUT / "spawns" / "same-camera"
    shutil.rmtree(work, ignore_errors=True)
    (work / "cases").mkdir(parents=True)
    dll = ctypes.CDLL(
        str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll")
    )
    flagged = 0
    for i, row in enumerate(report["spawns"]):
        if not row.get("browser") or not row.get("windows"):
            continue
        posed, retail, direct = {}, {}, {}
        camera = None
        for side in ("browser", "windows"):
            sweep = sweep_module.Sweep(cases / f"{row[side]}.wdmi", dll, work)
            camera = camera or {"eye": list(sweep.eye), "rotation": list(sweep.rotation)}
            sweep.set_pose(camera["eye"], camera["rotation"])
            retail[side], _clipped, _nodes = sweep.retail()
            direct[side] = sweep.direct(sweep.capture(work / f"s{i:02d}-{side}.wds"))
            posed[side] = work / "cases" / f"s{i:02d}-{side}.wdmi"
            sweep.write_image(posed[side])
        # Actors (Duncan, creatures, moving parts) are caught at their own
        # animation phase in each build: a face of a node whose transform, or
        # an ancestor's, differs between the two is not a level difference.
        snaps = {
            side: sweep_module.read_snapshot(work / f"s{i:02d}-{side}.wds")
            for side in ("browser", "windows")
        }
        local = {s: {n["address"]: n["local"] for n in snaps[s][1]} for s in snaps}
        moved = {a for a in local["browser"] if local["browser"][a] != local["windows"].get(a)}
        animated = {}
        for side, (_c, nodes, _v, faces) in snaps.items():
            for f in faces:
                n = nodes[f["owner"]]
                while n["address"] not in moved and n["parent"] != -1:
                    n = nodes[n["parent"]]
                animated[(side, f["address"])] = n["address"] in moved
        only = {
            side: sorted(
                f for f in retail[side] - retail[other] if not animated.get((side, f), True)
            )
            for side, other in (("browser", "windows"), ("windows", "browser"))
        }
        actors = {
            side: len(retail[side] - retail[other]) - len(only[side])
            for side, other in (("browser", "windows"), ("windows", "browser"))
        }
        pixels = {
            side: round(sum(direct[side].get(f, ("", None, 0))[2] for f in faces))
            for side, faces in only.items()
        }
        errors = rp.run_native(list(posed.values()), work / "native")
        frame = {}
        if not errors:
            images = {
                side: Image.frombytes(
                    "RGBA",
                    (rp.WIDTH, rp.HEIGHT),
                    rp.read_rgba(work / "native" / f"{rp.out_name(p)}.rgba"),
                ).convert("RGB")
                for side, p in posed.items()
            }
            for side, image in images.items():
                image.save(work / f"s{i:02d}-{side}.png")
            frame = frame_against(
                work / f"s{i:02d}-browser.png", work / f"s{i:02d}-windows.png",
                work / f"s{i:02d}-browser.wds", work / f"s{i:02d}-same-camera.png",
            )  # fmt: skip
        row["same_camera"] = {
            "retail_faces": {side: len(faces) for side, faces in retail.items()},
            "level_faces_only_in": {side: len(faces) for side, faces in only.items()},
            "their_pixels": pixels,
            "actor_faces_only_in": actors,
            "level_faces_only_windows_draws": [hex(f) for f in only["windows"][:40]],
            "frame": frame or {"error": errors},
        }
        bad = (
            frame.get("dark_here_lit_there", 0) > 0.02
            or pixels["windows"] >= 64
            or pixels["browser"] >= 64
        )
        flagged += bad
        print(
            f"s{i:02d} same camera: retail {len(retail['browser'])}/{len(retail['windows'])} "
            f"faces; level faces only browser {len(only['browser'])} ({pixels['browser']} px), "
            f"only Windows {len(only['windows'])} ({pixels['windows']} px); actor faces "
            f"{actors['browser']}/{actors['windows']}; dark in browser only "
            f"{frame.get('dark_here_lit_there')}{'  <-- state differs' if bad else ''}",
            flush=True,
        )
    path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{flagged} spawns differ in game state at the same camera; {path}")
    return 1 if flagged else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("step", choices=["capture", "analyze", "all", "spawns", "same-camera"])
    ap.add_argument("--project", type=int, default=46)
    ap.add_argument("--shots", type=int, default=3)
    ap.add_argument("--count", type=int, default=16, help="spawn points (spawns)")
    ap.add_argument("--settle-ms", type=int, default=3000)
    ap.add_argument("--poses", type=int, default=150, help="pose-sweep poses per shot")
    ap.add_argument("--windows", type=Path, help="Windows memory image of the same project")
    ap.add_argument("--swiftshader", action="store_true", help="software WebGL2 instead of the GPU")
    ap.add_argument("--headed", action="store_true")
    args = ap.parse_args()
    if args.step == "same-camera":
        return same_camera(args.project)
    if args.step == "spawns":
        return spawns(
            args.project, args.count, args.settle_ms, args.swiftshader, args.headed, args.windows
        )
    if args.step in ("capture", "all"):
        rc = capture(args.project, args.shots, args.settle_ms, args.swiftshader, args.headed)
        if rc or args.step == "capture":
            return rc
    return analyze(args.project, args.windows, args.poses, args.swiftshader)


if __name__ == "__main__":
    sys.exit(main())
