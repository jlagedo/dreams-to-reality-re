"""Persist Ghidra analysis into git. Run this often.

A Ghidra project is gitignored, unmergeable and embeds the game executables, so
it is not the record of what was learned; re/symbols/*.tsv is. This script
exports named functions and comments from each program in the project into
re/symbols/*.tsv, then optionally commits re/ and docs/.

Headless cannot open a project that the Ghidra GUI holds open (it is locked).
With the GUI closed, just run this. With it open: Ctrl+S, then Script Manager >
Dreams > ExportSymbols.java, then run this with --skip-export to commit.

usage: uv run python tools/re_checkpoint.py [-m MESSAGE] [--skip-export] [--no-commit]
           [--programs NAME ...]
"""

from __future__ import annotations

import argparse
import datetime
import re
import subprocess
import sys

import ghidra_headless

PROGRAMS = ["WINDREAM.EXE", "GDIDREAM.EXE", "SETUP.EXE", "CRYO.DLL", "DREAMSFX.EXE"]
# The whole point is that no game data ends up in git.
ASSET = re.compile(r"\.(rep|gpr|exe|dll|bin|iso|wav|hnm|dsn|dan|3dc|drd|tga|spr|bf|pak)\s*$", re.I)


def git(*args: str, capture: bool = True) -> str:
    p = subprocess.run(
        ["git", *args], cwd=ghidra_headless.REPO, capture_output=capture, text=True, check=True
    )
    return p.stdout if capture else ""


def export(programs: list[str]) -> bool:
    project = ghidra_headless.PROJECT_DIR
    if (project / f"{ghidra_headless.PROJECT_NAME}.lock").exists():
        print(
            "The Ghidra project is LOCKED - the GUI has it open. Headless cannot touch it:\n"
            "  1. In Ghidra: Ctrl+S to save the program\n"
            "  2. Window > Script Manager > Dreams > ExportSymbols.java\n"
            "  3. Re-run: uv run python tools/re_checkpoint.py --skip-export"
        )
        return False
    for prog in programs:
        print(f"exporting {prog}...", end="", flush=True)
        p = ghidra_headless.run(
            ["-process", prog, "-noanalysis", "-readOnly", "-postScript", "ExportSymbols.java"],
            capture=True,
        )
        out = (p.stdout or "") + (p.stderr or "")
        line = next((ln for ln in out.splitlines() if "named functions" in ln), None)
        if line:
            line = re.sub(r"^INFO\s+ExportSymbols\.java>\s*", "", line)
            print(" " + re.sub(r"\s*\(GhidraScript\)\s*$", "", line))
        elif re.search("not found|No program", out):
            print(" (not in project, skipped)")
        else:
            print(" no output - check the log")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description="Export Ghidra symbols to re/ and commit.")
    ap.add_argument("-m", "--message", help="commit message (default: timestamped)")
    ap.add_argument("--skip-export", action="store_true", help="GUI open: only commit re/")
    ap.add_argument("--no-commit", action="store_true", help="export and show the diff only")
    ap.add_argument("--programs", nargs="+", default=PROGRAMS)
    args = ap.parse_args()

    if not args.skip_export and not export(args.programs):
        return 1
    if not git("status", "--porcelain", "--", "re/", "docs/").strip():
        print("\nNothing changed since the last checkpoint.")
        return 0
    print("\n--- changes ---", flush=True)
    git("--no-pager", "diff", "--stat", "--", "re/", "docs/", capture=False)
    git("--no-pager", "diff", "--cached", "--stat", "--", "re/", "docs/", capture=False)
    suspect = [ln for ln in git("status", "--porcelain").splitlines() if ASSET.search(ln)]
    if suspect:
        print("Refusing to commit - these look like game assets:\n" + "\n".join(suspect))
        return 1
    if args.no_commit:
        print("\n--no-commit set; nothing committed.")
        return 0
    message = args.message or f"re: checkpoint {datetime.datetime.now():%Y-%m-%d %H:%M}"
    git("add", "re/", "docs/")
    git("commit", "-q", "-m", message)
    print(f"\ncommitted: {message}")
    git("--no-pager", "log", "--oneline", "-1", capture=False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
