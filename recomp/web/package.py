# ruff: noqa: E501
"""Assemble the browser build into one folder to upload to Cloudflare Pages.

    uv run python recomp/web/package.py                  # engine + demo pack + page -> out/recomp/web/dist
    uv run python recomp/web/package.py --demo-url https://demo.example.com/dreams/
    uv run python recomp/web/package.py --engine DIR --demo DIR --out DIR

Inputs (all under out/, none of it is committed):
  engine  out/recomp/windream/build-web/dreams.{js,wasm,...}   WASM stream
  demo    out/recomp/web/demo/ (manifest.json + chunks)        DEMO stream
  page    recomp/web/index.html, loader.js, site/ (css, js)     this folder

Output (out/recomp/web/dist): index.html, loader.js, site/, config.js, dreams.*,
demo/, _headers. Deploy with `wrangler pages deploy out/recomp/web/dist`.

--shots DIR adds game screenshots (PNG or JPEG, game-derived: keep them under
out/, never commit them) to the "About the game" section: they are copied to
site/shots/ (shrunk to JPEG when Pillow is installed) and listed in config.js.
Without it the page has no game images, only original CSS and SVG art.

--poster FILE puts one frame of the game on the start screen (also game-derived:
a file under out/, never committed); it is copied to site/poster.jpg. Without it
the start screen shows a plain dusk sky.

--manual DIR adds the tribute's artwork and source PDF, prepared with
manual_assets.py. Defaults to out/recomp/web/manual. These are game-derived
assets and must remain outside the repository's tracked sources.

--demo-url leaves the pack out of dist and points the page at that base URL
(an R2 bucket, a CDN). The page fetches it with CORS, so the host must send
Access-Control-Allow-Origin; see recomp/README.md, "Browser build". The pack
to upload is then the --demo folder itself.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
OUT = REPO / "out" / "recomp" / "web"

PAGES_MAX_FILE = 25 * 1024 * 1024  # Cloudflare Pages: largest single asset
PAGES_MAX_FILES = 20000
PAGE_FILES = ("index.html", "loader.js")
SITE_DIR = HERE / "site"  # css, js: the page's presentation, copied as site/


def headers_text(demo_local: bool) -> str:
    """The Cloudflare Pages `_headers` file.

    Pages adds the headers of every matching rule, so Cache-Control is only set
    in specific rules, and `! Cache-Control` detaches the inherited value where
    a more specific rule needs a different one.
    """
    lines = [
        "# Written by recomp/web/package.py. Cloudflare Pages `_headers`.",
        "# Cross-origin isolation (SharedArrayBuffer, pthreads) needs both of the first two.",
        "/*",
        "  Cross-Origin-Opener-Policy: same-origin",
        "  Cross-Origin-Embedder-Policy: require-corp",
        "  Cross-Origin-Resource-Policy: same-origin",
        "  X-Content-Type-Options: nosniff",
        "  Referrer-Policy: no-referrer",
        "",
        "# The page, its loader and its config are small and change with a deploy.",
        "/",
        "  Cache-Control: no-cache",
        "/index.html",
        "  Cache-Control: no-cache",
        "/loader.js",
        "  Cache-Control: no-cache",
        "/config.js",
        "  Cache-Control: no-cache",
        "/site/*",
        "  Cache-Control: no-cache",
        "",
        "# The engine keeps its name between builds: always revalidate (ETag), never serve stale.",
        "/dreams.js",
        "  Cache-Control: public, max-age=0, must-revalidate",
        "/dreams.*.js",
        "  Cache-Control: public, max-age=0, must-revalidate",
        "/dreams.wasm",
        "  Content-Type: application/wasm",
        "  Cache-Control: public, max-age=0, must-revalidate",
        "",
    ]
    if demo_local:
        lines += [
            "# Pack chunks are fetched as <chunk>?v=<manifest hash>: a new manifest means new URLs,",
            "# so they can be cached forever. The manifest itself is always revalidated.",
            "/demo/*",
            "  Cache-Control: public, max-age=31536000, immutable",
            "/demo/manifest.json",
            "  ! Cache-Control",
            "  Cache-Control: no-cache",
            "",
        ]
    return "\n".join(lines)


def copy_engine(engine: Path, out: Path) -> list[Path]:
    if not (engine / "dreams.js").is_file() or not (engine / "dreams.wasm").is_file():
        sys.exit(
            f"{engine} has no dreams.js/dreams.wasm: build the browser engine first (WASM stream), or pass --engine"
        )
    copied = []
    for p in sorted(engine.iterdir()):
        if (
            p.is_file()
            and p.name.startswith("dreams")
            and not p.name.endswith((".map", ".d.ts", ".pdb"))
        ):
            shutil.copy2(p, out / p.name)
            copied.append(out / p.name)
    return copied


def copy_shots(src: Path, out: Path, limit: int = 8) -> list[dict]:
    """Copy up to `limit` screenshots to out/site/shots/ and describe them for config.js."""
    files = sorted(p for p in src.iterdir() if p.suffix.lower() in (".png", ".jpg", ".jpeg"))
    if not files:
        print(f"WARNING: no screenshots in {src}")
        return []
    step = max(1, len(files) // limit)
    dst = out / "site" / "shots"
    shutil.rmtree(dst, ignore_errors=True)
    dst.mkdir(parents=True)
    try:
        from PIL import Image
    except ImportError:
        Image = None  # noqa: N806 - copy files as they are
    shots = []
    for i, p in enumerate(files[::step][:limit]):
        name = f"shot{i + 1}.jpg" if Image else f"shot{i + 1}{p.suffix.lower()}"
        if Image:
            im = Image.open(p).convert("RGB")
            im.thumbnail((960, 720))
            im.save(dst / name, quality=82, optimize=True)
        else:
            shutil.copy2(p, dst / name)
        shots.append({"src": f"site/shots/{name}", "alt": "Screenshot of the demo", "caption": ""})
    print(f"screenshots: {len(shots)} from {src} (game-derived, not for the repository)")
    return shots


def copy_poster(src: Path, out: Path) -> str:
    """Copy the start screen's picture to out/site/ (a 1280-wide JPEG with Pillow)."""
    try:
        from PIL import Image
    except ImportError:
        name = f"poster{src.suffix.lower()}"
        shutil.copy2(src, out / "site" / name)
    else:
        name = "poster.jpg"
        im = Image.open(src).convert("RGB")
        im.thumbnail((1280, 960))
        im.save(out / "site" / name, quality=85, optimize=True)
    print(f"start screen picture: {src} (game-derived, not for the repository)")
    return f"site/{name}"


def check_sizes(out: Path, limit: int, label: str) -> int:
    files = [p for p in out.rglob("*") if p.is_file()]
    big = [p for p in files if p.stat().st_size > limit]
    for p in big:
        print(
            f"WARNING: {p.relative_to(out).as_posix()} is {p.stat().st_size / 2**20:.1f} MiB, over the {label} limit of {limit / 2**20:.0f} MiB"
        )
    if len(files) > PAGES_MAX_FILES:
        print(f"WARNING: {len(files)} files, Cloudflare Pages allows {PAGES_MAX_FILES}")
        return len(big) + 1
    return len(big)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument(
        "--engine", type=Path, default=REPO / "out" / "recomp" / "windream" / "build-web"
    )
    ap.add_argument(
        "--demo", type=Path, default=OUT / "demo", help="demo pack folder (manifest.json + chunks)"
    )
    ap.add_argument("--out", type=Path, default=OUT / "dist")
    ap.add_argument(
        "--demo-url",
        default="",
        help="leave the pack out of dist; the page fetches it from this base URL",
    )
    ap.add_argument(
        "--engine-url", default="", help="the engine files are on this base URL too (rarely wanted)"
    )
    ap.add_argument(
        "--no-demo", action="store_true", help="no pack at all (the page uses ?demo=<url>)"
    )
    ap.add_argument("--clean", action="store_true", help="empty the output folder first")
    ap.add_argument(
        "--shots",
        type=Path,
        default=None,
        help="folder of game screenshots to show in the page (game-derived; optional)",
    )
    ap.add_argument(
        "--poster",
        type=Path,
        default=None,
        help="a frame of the game for the start screen (game-derived; optional)",
    )
    ap.add_argument(
        "--manual",
        type=Path,
        default=OUT / "manual",
        help="tribute artwork prepared by manual_assets.py (game-derived)",
    )
    args = ap.parse_args()

    out = args.out.resolve()
    if args.clean and out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)

    for name in PAGE_FILES:
        shutil.copy2(HERE / name, out / name)
    shutil.rmtree(out / "site", ignore_errors=True)
    shutil.copytree(
        SITE_DIR, out / "site", ignore=shutil.ignore_patterns("shots", "poster.*", "*.md")
    )
    if args.manual.is_dir():
        shutil.copytree(args.manual.resolve(), out / "site" / "manual")
        print(f"tribute artwork: {args.manual} (game-derived, not for the repository)")
    else:
        print("WARNING: tribute artwork missing; run manual_assets.py MANUAL.pdf first")
    copy_engine(args.engine.resolve(), out)

    demo_local = False
    base = "demo/"
    if args.demo_url:
        base = args.demo_url.rstrip("/") + "/"
        print(f"demo pack left out of dist; the page will fetch {base}manifest.json")
        print(f"upload {args.demo.resolve()} to that location (CORS: see README, 'Browser build')")
        shutil.rmtree(out / "demo", ignore_errors=True)
    elif not args.no_demo:
        demo = args.demo.resolve()
        if not (demo / "manifest.json").is_file():
            sys.exit(
                f"{demo}/manifest.json is missing: build the demo pack first (DEMO stream), or pass --demo, --demo-url or --no-demo"
            )
        manifest = json.loads((demo / "manifest.json").read_text(encoding="utf-8"))
        shutil.rmtree(out / "demo", ignore_errors=True)
        shutil.copytree(demo, out / "demo")
        demo_local = True
        print(
            f"demo pack: {manifest.get('name')} v{manifest.get('version')}, {len(manifest['files'])} files, {manifest.get('total', 0) / 2**20:.1f} MiB"
        )

    cfg = [f"window.DREAMS_DEMO_BASE = {json.dumps(base)};"]
    if args.shots:
        shots = copy_shots(args.shots.resolve(), out)
        if shots:
            cfg.append(f"window.DREAMS_SHOTS = {json.dumps(shots)};")
    if args.poster:
        cfg.append(f"window.DREAMS_POSTER = {json.dumps(copy_poster(args.poster.resolve(), out))};")
    if args.engine_url:
        cfg.append(f"window.DREAMS_ENGINE_BASE = {json.dumps(args.engine_url.rstrip('/') + '/')};")
    (out / "config.js").write_text("\n".join(cfg) + "\n", encoding="utf-8")
    (out / "_headers").write_text(headers_text(demo_local), encoding="utf-8", newline="\n")

    n = sum(1 for p in out.rglob("*") if p.is_file())
    total = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"{out}: {n} files, {total / 2**20:.1f} MiB")
    over = check_sizes(out, PAGES_MAX_FILE, "Cloudflare Pages")
    if over:
        print(
            "Pages would reject this folder. Re-chunk the pack to 20 MiB at most, or keep it on R2 with --demo-url."
        )
    print("deploy: wrangler pages deploy " + str(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
