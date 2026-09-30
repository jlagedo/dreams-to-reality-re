"""Inventory and run bounded Windows rendering checks; never infer visual acceptance.

uv run python recomp/windream/debug/render_acceptance.py --inventory
uv run python recomp/windream/debug/render_acceptance.py --run first-scene-native
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from uuid import uuid4

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

from dreams import paths  # noqa: E402

SPEC = paths.REPO_ROOT / "docs/specs/006-recomp-glide-renderer"
FAILURES = re.compile(
    r"=== recomp: CRASH|\[direct\] FATAL:|unclassified CPU|unresolved import", re.I
)


def content_inventory(level_map: str, roots: list[Path]) -> dict:
    projects = {}
    for scene, entries in re.findall(r"^\| `([^`]+\.DSN)` \| (.+) \|$", level_map, re.M):
        for number in re.findall(r"\bP(\d+)\b", entries):
            projects[number] = scene
    assets = []
    for index, root in enumerate(roots, 1):
        if not root.is_dir():
            raise FileNotFoundError(root)
        for file in sorted(root.rglob("*")):
            if file.is_file() and file.suffix.upper() in {
                ".DSN",
                ".DAN",
                ".HNM",
                ".DRD",
                ".SPR",
                ".FSB",
                ".3DC",
                ".BF",
            }:
                assets.append(
                    {
                        "disc": index,
                        "path": file.relative_to(root).as_posix(),
                        "bytes": file.stat().st_size,
                    }
                )
    return {
        "source": "docs/level-map.md; configured original discs",
        "projects": projects,
        "project_count": len(projects),
        "scene_count": len(set(projects.values())),
        "assets": assets,
        "coverage": "inventory only; no gameplay route exercised",
    }


def analyze(log: str, route: dict, returncode: int) -> dict:
    failures = [line for line in log.splitlines() if FAILURES.search(line)]
    checkpoints = {pattern: bool(re.search(pattern, log)) for pattern in route["checkpoints"]}
    routine = [int(value) for value in re.findall(r"routine_readbacks=(\d+)", log)]
    resources = [int(value) for value in re.findall(r"\bresources=(\d+)", log)]
    exports = [line for line in log.splitlines() if "explicit readback reason=" in line]
    if any(routine):
        failures.append("nonzero routine readbacks")
    status = (
        "failed"
        if failures or returncode
        else (
            "checkpoints-reached"
            if checkpoints and all(checkpoints.values()) and routine
            else "not-exercised"
        )
    )
    return {
        "status": status,
        "acceptance": "pending",
        "evidence_kind": route["evidence_kind"],
        "checkpoints": checkpoints,
        "failures": failures,
        "returncode": returncode,
        "routine_readback_samples": routine,
        "explicit_exports": exports,
        "resources": {
            "samples": resources,
            "min": min(resources, default=None),
            "max": max(resources, default=None),
            "last": resources[-1] if resources else None,
        },
        "remaining": route["remaining"],
        "limitations": [
            "Timeout is not route completion.",
            "Reported zero routine reads requires strict audit and classified host accesses.",
            "Resource samples alone do not prove bounded lifetime or ten reloads.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument(
        "--run", action="append", default=[], help="route ID; repeat to run serially"
    )
    parser.add_argument("--manifest", type=Path, default=SPEC / "acceptance-routes.json")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    selected = []
    for identifier in args.run:
        matches = [route for route in manifest["routes"] if route["id"] == identifier]
        if not matches or not matches[0].get("args"):
            parser.error(f"unknown or not yet automated route: {identifier}")
        selected.append(matches[0])
    if not args.inventory and not selected:
        print(json.dumps(manifest, indent=2))
        return 0
    session = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid4().hex[:8]
    output = recomp_env.out_dir("acceptance", session)
    report = {
        "session": session,
        "manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
        "routes": [],
        "acceptance": "pending",
    }
    if args.inventory:
        inventory = content_inventory(
            (paths.REPO_ROOT / "docs/level-map.md").read_text(encoding="utf-8"),
            [paths.disc(1), paths.disc(2)],
        )
        (output / "inventory.json").write_text(
            json.dumps(inventory, indent=2) + "\n", encoding="utf-8"
        )
    # Serial execution: the shared GPU is also the pacing/appearance reference.
    for route in selected:
        tag = f"acceptance-{session}-{route['id']}"
        command = [
            sys.executable,
            str(paths.REPO_ROOT / "recomp/windream/run.py"),
            "--renderer",
            "direct",
            "--render-audit",
            "--tag",
            tag,
            *route["args"],
        ]
        result = subprocess.run(command, capture_output=True, text=True, errors="replace")
        run = recomp_env.out_dir("windream", "run-" + tag)
        log_file = run / "stderr.txt"
        log = log_file.read_text(encoding="utf-8", errors="replace") if log_file.exists() else ""
        entry = {
            "id": route["id"],
            "command": command,
            "artifacts": str(run),
            "launcher_stdout": result.stdout,
            "launcher_stderr": result.stderr,
            **analyze(log, route, result.returncode),
        }
        report["routes"].append(entry)
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{route['id']}: {entry['status']}; acceptance pending")
    print(output)
    return 1 if any(row["status"] != "checkpoints-reached" for row in report["routes"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
