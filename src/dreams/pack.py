"""Pack a deployable static site: the web app build plus a subset of the data root.

The third pipeline stage (``docs/pipeline.md``). Pack never transforms a
content file: it selects files from ``baked/``, copies them unchanged under
``site/data/``, and rebuilds ``index.json`` with the same :func:`write_index`
bake uses, so the subset lists exactly what it holds.

    releases/<name>/
      manifest.json   what went in: commit, projects, sizes
      site/           upload this folder as-is
        index.html  assets/   the Vite build
        data/                 the subset of baked/
        _headers              Cloudflare Pages cache rules

The selection follows the game's own dependency data: each project's
``needs``, the boot assets and resident set from ``index.json``, and the small
shared folders every screen uses.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
from datetime import UTC, datetime
from pathlib import Path

from dreams.bake import write_index
from dreams.paths import REPO_ROOT

#: Cloudflare Pages and Workers static assets, free plan: per file, per deployment.
MAX_FILE_BYTES = 25 * 1024 * 1024
MAX_FILES = 20_000

#: Always shipped: small, and used by every screen.
SHARED = ("ui/", "audio/sfx/", "text/")

#: Vite's hashed assets never change under a name; data may, so it revalidates
#: after ten minutes (Pages answers with ETags, so that is a 304, not a download).
HEADERS = """\
/assets/*
  Cache-Control: public, max-age=31536000, immutable
/data/*
  Cache-Control: public, max-age=600
"""


def _expand(root: Path, entries: list[str]) -> tuple[list[str], list[str]]:
    files, missing = set(), []
    for entry in dict.fromkeys(entries):
        path = root / entry
        if entry.endswith("/"):
            if path.is_dir():
                files.update(p.relative_to(root).as_posix() for p in path.rglob("*") if p.is_file())
            else:
                missing.append(entry)
        elif path.is_file():
            files.add(entry)
        else:
            missing.append(entry)
    return sorted(files), missing


def select(baked_root: Path, projects: list[int] | None) -> tuple[list[str], list[str]]:
    """Files to ship, relative to the data root, and any listed entries not on disk.

    ``projects=None`` ships the whole data root.
    """
    if projects is None:
        skip = {"index.json", "manifest.json"}
        return sorted(
            p.relative_to(baked_root).as_posix()
            for p in baked_root.rglob("*")
            if p.is_file() and p.relative_to(baked_root).as_posix() not in skip
        ), []

    index = json.loads((baked_root / "index.json").read_text(encoding="utf-8"))
    entries = list(SHARED)
    boot = index.get("boot", {})
    entries += [boot[k] for k in ("intro", "warp", "elder", "menuMusic") if boot.get(k)]
    resident = index.get("resident", {})
    entries += [f"scenes/{s}/" for s in resident.get("scenes", [])]
    entries += [f"models/{m}/" for m in resident.get("models", [])]
    for n in projects:
        path = baked_root / "projects" / f"{n}.json"
        if not path.is_file():
            raise ValueError(f"project {n} is not baked: {path}")
        entries += json.loads(path.read_text(encoding="utf-8"))["needs"]
    return _expand(baked_root, entries)


def _git() -> dict:
    def out(*args: str) -> str:
        r = subprocess.run(["git", *args], cwd=REPO_ROOT, capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else ""

    return {"commit": out("rev-parse", "HEAD"), "dirty": bool(out("status", "--porcelain"))}


def build_web(web_dir: Path) -> None:
    npm = shutil.which("npm")
    if not npm:
        raise RuntimeError("npm not found on PATH; build the app yourself and pass --no-build")
    subprocess.run([npm, "--prefix", str(web_dir), "run", "build"], check=True)


def run(
    baked_root: Path,
    releases_root: Path,
    name: str,
    projects: list[int] | None = None,
    web_dir: Path = REPO_ROOT / "web",
    build: bool = True,
) -> dict:
    """Write ``releases_root/name`` and return its manifest."""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
        raise ValueError(f"release name must be a plain folder name: {name!r}")
    if not (baked_root / "index.json").is_file():
        raise RuntimeError(f"no index.json in {baked_root}; run: dreams bake")

    if build:
        build_web(web_dir)
    dist = web_dir / "dist"
    if not (dist / "index.html").is_file():
        raise RuntimeError(f"no app build at {dist}; run: npm --prefix web run build")

    files, missing = select(baked_root, projects)

    release = releases_root / name
    site = release / "site"
    if site.exists():
        shutil.rmtree(site)  # pack's own output, rebuilt whole every time
    shutil.copytree(dist, site)
    data = site / "data"
    for rel in files:
        dest = data / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(baked_root / rel, dest)
    index = write_index(data)
    (site / "_headers").write_text(HEADERS, encoding="utf-8", newline="\n")

    everything = [p for p in site.rglob("*") if p.is_file()]
    largest = max(everything, key=lambda p: p.stat().st_size)
    problems = [
        f"{p.relative_to(site).as_posix()} is {p.stat().st_size / 1048576:.1f} MiB (limit 25)"
        for p in everything
        if p.stat().st_size > MAX_FILE_BYTES
    ]
    if len(everything) > MAX_FILES:
        problems.append(f"{len(everything)} files (limit {MAX_FILES})")

    manifest = {
        "name": name,
        "created": datetime.now(UTC).isoformat(timespec="seconds"),
        "git": _git(),
        "baked_root": str(baked_root),
        "projects": [p["index"] for p in index["projects"]],
        "files": len(everything),
        "bytes": sum(p.stat().st_size for p in everything),
        "largest": {"path": largest.relative_to(site).as_posix(), "bytes": largest.stat().st_size},
        "missing": missing,
        "problems": problems,
        "site": str(site),
    }
    (release / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest
