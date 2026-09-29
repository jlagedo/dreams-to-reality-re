"""Compile and link a C file with Watcom as a Win32 console program.

wc110 (default) is the Windows game's compiler and runtime
(DREAMS_WATCOM_COMPILER); wc106 is DREAMS_WATCOM_COMPILER_106. Unset, both
fall back under DREAMS_WATCOM.

usage: uv run python recomp/difftest/wat.py test.c
           [--flags "-5r -od"] [--cc wc110|wc106] [--out dir]
       -> test.exe, test.map in --out (default: the current directory)
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

from dreams import paths

COMPILERS = {"wc110": "watcom_compiler", "wc106": "watcom_compiler_106"}


def compile_and_link(src: Path, out: Path, flags: str = "-5r -od", cc: str = "wc110") -> Path:
    w = paths.get(COMPILERS[cc])
    bindir = w / ("BINNT" if sys.platform == "win32" else "BINL")
    sep = os.pathsep
    # Child environment only: Watcom's BINNT has an RC.EXE that would shadow
    # the Windows SDK's for the clang-cl build that follows.
    env = dict(
        os.environ,
        WATCOM=str(w),
        PATH=f"{bindir}{sep}{w / 'BINW'}{sep}{os.environ.get('PATH', '')}",
        INCLUDE=f"{w / 'H'}{sep}{w / 'H' / 'NT'}",
        LIB=f"{w / 'LIB386'}{sep}{w / 'LIB386' / 'NT'}",
    )
    name = src.stem
    subprocess.run(
        [str(bindir / "WCC386"), "-zq", "-bt=nt", *flags.split(), str(src.resolve())],
        cwd=out, env=env, check=True,
    )  # fmt: skip
    subprocess.run(
        [str(bindir / "WLINK"), "option", "quiet", "system", "nt", "file", f"{name}.obj",
         "name", f"{name}.exe", "option", f"map={name}.map"],
        cwd=out, env=env, check=True,
    )  # fmt: skip
    return out / f"{name}.exe"


def main() -> None:
    ap = argparse.ArgumentParser(description="Compile and link a C file with Watcom (Win32).")
    ap.add_argument("src", type=Path)
    ap.add_argument("--flags", default="-5r -od")
    ap.add_argument("--cc", choices=sorted(COMPILERS), default="wc110")
    ap.add_argument("--out", type=Path, default=Path.cwd())
    args = ap.parse_args()
    print(compile_and_link(args.src, args.out, args.flags, args.cc))


if __name__ == "__main__":
    main()
