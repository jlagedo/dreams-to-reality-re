"""Run Ghidra's analyzeHeadless on the local project.

Resolves the Ghidra installation from DREAMS_GHIDRA_ROOT (or
GHIDRA_INSTALL_DIR), defaults the project to the repository's ghidra/dreams and
the script path to re/ghidra_scripts/, and passes everything else through:

    uv run python re/tools/ghidra_headless.py -process WINDREAM.EXE -noanalysis \\
        -readOnly -postScript Decompile.java 004175bc

A first argument that does not start with '-' is a project directory, followed
by the project name, as with analyzeHeadless itself. On Windows the
analyzeHeadless.bat launcher splits script arguments on '=', so scripts take
`address:name` rather than `address=name`.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from dreams import paths

REPO = paths.REPO_ROOT
PROJECT_DIR = REPO / "ghidra"
PROJECT_NAME = "dreams"
SCRIPTS = REPO / "re" / "re/ghidra_scripts"


def headless() -> Path:
    name = "analyzeHeadless.bat" if sys.platform == "win32" else "analyzeHeadless"
    path = paths.get("ghidra") / "support" / name
    if not path.is_file():
        raise SystemExit(f"analyzeHeadless not found at {path}; check DREAMS_GHIDRA_ROOT")
    return path


def command(args: list[str]) -> list[str]:
    """analyzeHeadless command line: project defaulted, -scriptPath added."""
    args = list(args)
    if not args or args[0].startswith("-"):
        args = [str(PROJECT_DIR), PROJECT_NAME, *args]
    if "-scriptPath" not in args:
        args += ["-scriptPath", str(SCRIPTS)]
    return [str(headless()), *args]


def run(args: list[str], *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    """Run analyzeHeadless; DREAMS_REPO tells the scripts where re/ lives."""
    env = dict(os.environ, DREAMS_REPO=str(REPO))
    return subprocess.run(
        command(args), env=env, capture_output=capture, text=True, errors="replace"
    )


def main() -> int:
    return run(sys.argv[1:]).returncode


if __name__ == "__main__":
    sys.exit(main())
