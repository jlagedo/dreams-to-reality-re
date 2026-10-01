"""Build the Ghidra project for Dreams to Reality from the disc images.

The Ghidra project is a build artefact: binary, unmergeable, and it embeds
copies of the game executables, so it never goes into git. This script
regenerates it from the local discs, runs auto-analysis, and with
--import-symbols re-applies the names stored in re/symbols/.
--import-structs parses the C layouts under re/structs/ into the program's Data
Type Manager. Run it once to set up, and again for a clean slate.

By default it imports the Windows build (PE32) plus CryoLib and the installer.
The DOS builds are LE/DOS4GW and need the LE loader extension (docs/research/re-setup.md,
"LE loader for the DOS builds"); import one with --binaries. Only the listed
binaries are imported; other programs in the project stay.

usage: uv run python re/tools/ghidra_import.py [--import-symbols] [--import-structs]
           [--no-analyze] [--binaries EXE ...] [--project DIR] [--project-name NAME]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import ghidra_headless

from dreams import paths

NOISE = re.compile(r"INFO|WARN\s+Unable to|^\s*$")


def default_binaries() -> list[Path]:
    disc1, disc2 = paths.disc(1), paths.disc(2)
    return [
        disc1 / "WINDREAM.EXE",  # PE32 Watcom - the primary target
        disc1 / "GDIDREAM.EXE",  # same build, GDI backend
        disc1 / "SETUP.EXE",  # MSVC installer, reads SETUP.INI
        disc2 / "DEMOS2" / "CRYO.DLL",  # CryoLib debug build, 165 exports
    ]


def main() -> int:
    ap = argparse.ArgumentParser(description="Build the Ghidra project from the discs.")
    ap.add_argument("--binaries", nargs="+", type=Path, help="executables to (re)import")
    ap.add_argument("--project", type=Path, default=ghidra_headless.PROJECT_DIR)
    ap.add_argument("--project-name", default=ghidra_headless.PROJECT_NAME)
    ap.add_argument("--no-analyze", action="store_true", help="import without auto-analysis")
    ap.add_argument("--import-symbols", action="store_true", help="re/symbols")
    ap.add_argument("--import-structs", action="store_true", help="re/structs C layouts")
    args = ap.parse_args()

    binaries = args.binaries or default_binaries()
    missing = [b for b in binaries if not b.is_file()]
    if missing:
        print("skipping missing binaries:\n  " + "\n  ".join(map(str, missing)))
        binaries = [b for b in binaries if b.is_file()]
    if not binaries:
        raise SystemExit("no binaries to import; check DREAMS_DISC1 and DREAMS_DISC2")
    args.project.mkdir(parents=True, exist_ok=True)
    project = args.project.resolve()

    print(f"Ghidra   : {paths.get('ghidra')}")
    print(f"Project  : {project / args.project_name}")
    print(f"Scripts  : {ghidra_headless.SCRIPTS}")
    print(f"Importing: {len(binaries)} binaries\n")
    for binary in binaries:
        print(f"--- {binary.name} ---")
        cmd = [str(project), args.project_name, "-import", str(binary), "-overwrite",
               # Watcom writes VirtualSize 0; map the whole .bss before analysis.
               "-preScript", "FixWatcomBss.java"]  # fmt: skip
        if args.no_analyze:
            cmd.append("-noanalysis")
        if args.import_symbols:
            cmd += ["-postScript", "ImportSymbols.java"]
        if args.import_structs:
            cmd += ["-postScript", "ImportStructs.java"]
        p = ghidra_headless.run(cmd, capture=True)
        lines = (p.stdout + p.stderr).splitlines()
        keep = [ln for ln in lines if not NOISE.search(ln) or re.search("ERROR|Exception", ln)]
        print("\n".join(keep[-8:]))

    print(f"\nDone. Open {project / args.project_name}.gpr in Ghidra.")
    print("Next: Script Manager > Dreams > FindFormatParsers.java, rename what you identify,")
    print("then re/tools/re_checkpoint.py to persist the names to re/symbols/.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
