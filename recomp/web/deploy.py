"""Prepare cf's assets-only Build Output and publish the packaged browser site.

Package the release first, then run:
    uv run python recomp/web/deploy.py

Requires cf 1.0.0-beta.12 or a compatible CLI. All build output and game data
remain under out/. The custom domain deploy also provisions its DNS and TLS.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / "out" / "recomp" / "web"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dist", type=Path, default=OUT / "dist")
    ap.add_argument("--name", default="dreams-to-reality")
    ap.add_argument("--domain", default="dreams.lagedo.dev")
    ap.add_argument("--account-id", default=os.environ.get("CLOUDFLARE_ACCOUNT_ID"))
    ap.add_argument("--prepare-only", action="store_true")
    args = ap.parse_args()
    dist = args.dist.resolve()
    for name in ("index.html", "dreams.js", "dreams.wasm", "_headers", "demo/manifest.json"):
        if not (dist / name).is_file():
            ap.error(f"Missing {dist / name}; package the release engine and demo first")

    root = (OUT / "cloudflare").resolve()
    output = root / ".cloudflare" / "output" / "v0"
    worker = output / "workers" / "default"
    assets = worker / "assets"
    if not assets.resolve().is_relative_to(root):
        ap.error("Asset output must remain inside the generated Cloudflare project")
    if assets.exists():
        shutil.rmtree(assets)
    shutil.copytree(dist, assets)
    settings = {"buildContext": {"isPreview": False}}
    if args.account_id:
        settings["accountId"] = args.account_id
    config = {
        "name": args.name,
        "compatibilityDate": "2026-10-03",
        "assets": {"notFoundHandling": "none"},
        "domains": [args.domain],
        "workersDev": True,
        "observability": {
            "enabled": True,
            "logs": {"enabled": True},
            "traces": {"enabled": True},
        },
    }
    (output / "config.json").write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")
    (worker / "worker.config.json").write_text(
        json.dumps(config, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Prepared {args.name} at {root} for https://{args.domain}", flush=True)
    if args.prepare_only:
        return 0
    cli = shutil.which("cf")
    if not cli:
        ap.error("cf is not installed or not on PATH")
    return subprocess.run([cli, "deploy", "--prebuilt"], cwd=root, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
