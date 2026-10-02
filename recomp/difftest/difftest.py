"""Differential test: a Watcom program (11.0 by default, the Windows game's
compiler) run natively vs. the same program recompiled.

Prints per line ok / DIFF (expected, then what the recompiled program
printed), then a summary and the time each phase took. Work directory (exe,
bounds, generated C, build, outputs): DREAMS_OUT/recomp/difftest/<name>-<tag>.
The test source is <name>.c here or, for the generated t_insn.c (gen_insn.py),
in DREAMS_OUT/recomp/difftest.

usage: uv run --with capstone --with pefile python recomp/difftest/difftest.py
           [name] [--flags "-5r -od"] [--tag od] [--cc wc110|wc106] [--timeout 60]
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import recomp_env  # noqa: E402
import wat  # noqa: E402

from dreams import paths  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description="Native vs recompiled Watcom test program.")
    ap.add_argument("name", nargs="?", default="t_core")
    ap.add_argument("--flags", default="-5r -od")
    ap.add_argument("--tag", default="od")
    ap.add_argument("--cc", choices=sorted(wat.COMPILERS), default="wc110")
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args()

    root = recomp_env.out_dir("difftest")
    work = root / f"{args.name}-{args.tag}"
    src = HERE / f"{args.name}.c"
    if not src.is_file():
        src = root / f"{args.name}.c"
    times: dict[str, str] = {}
    t0 = time.monotonic()

    def lap(phase: str) -> None:
        nonlocal t0
        times[phase] = f"{time.monotonic() - t0:.1f}s"
        t0 = time.monotonic()

    (work / "gen").mkdir(parents=True, exist_ok=True)
    shutil.copy(src, work)
    exe = wat.compile_and_link(work / src.name, work, args.flags, args.cc)
    with open(work / "expected.txt", "wb") as f:
        subprocess.run([str(exe)], stdout=f, check=False)
    lap("watcom+native")

    # Function bounds the same way as the game's: Ghidra auto-analysis +
    # pcrecomp DumpBounds.java. Analysis is most of the run, so it is cached,
    # keyed on what determines the code: source, flags and compiler (the exe's
    # own hash changes with the link timestamp on every build).
    key = hashlib.sha256(
        src.read_bytes() + f"\0{args.flags}\0{paths.get(wat.COMPILERS[args.cc])}".encode()
    ).hexdigest()
    bounds, stamp = work / "bounds.csv", work / "bounds.key"
    if bounds.is_file() and stamp.is_file() and stamp.read_text().strip() == key:
        lap("ghidra (cached)")
    else:
        proj = work / "ghidra"
        proj.mkdir(exist_ok=True)
        support = paths.get("ghidra") / "support"
        headless = support / (
            "analyzeHeadless.bat" if sys.platform == "win32" else "analyzeHeadless"
        )
        p = subprocess.run(
            [str(headless), str(proj), "difftest", "-import", str(exe), "-overwrite",
             "-scriptPath", str(recomp_env.pcrecomp() / "tools" / "ghidra"),
             "-postScript", "DumpBounds.java", str(bounds)],
            capture_output=True, text=True, errors="replace",
        )  # fmt: skip
        errors = [line for line in (p.stdout + p.stderr).splitlines() if "ERROR" in line]
        print("\n".join(errors[:5]))
        stamp.write_text(key)
        lap("ghidra")

    # Test programs have no Ghidra-complete function list: also follow code
    # pointers found in data.
    env = dict(os.environ, WD_SCAN_DATA_PTRS="1")
    p = subprocess.run(
        [sys.executable, str(recomp_env.LIFT / "lift.py"), str(exe), str(bounds),
         str(work / "gen")],
        env=env, capture_output=True, text=True, check=True,
    )  # fmt: skip
    print("\n".join(p.stdout.splitlines()[-2:]))
    subprocess.run(
        [sys.executable, str(recomp_env.LIFT / "gen_imports.py"), str(exe),
         str(work / "gen" / "imports_gen.c")],
        check=True,
    )  # fmt: skip
    lap("lift")

    recomp_env.configure_and_build(work / "build", work / "gen", quiet=True)
    lap("build")

    # Lifted run, with a timeout: a lifter bug can turn a test loop into a hang.
    binary = work / "build" / recomp_env.exe_name("windream_recomp")
    with open(work / "actual.txt", "wb") as so, open(work / "stderr.txt", "wb") as se:
        try:
            p = subprocess.run(
                [str(binary), str(exe), "--run"], cwd=work, stdout=so, stderr=se,
                # A test program has none of the game functions the direct
                # renderer (the Windows default) replaces.
                env=dict(os.environ, WD_RENDERER="software"),
                timeout=args.timeout,
            )  # fmt: skip
            print(f"exit {p.returncode}")
        except subprocess.TimeoutExpired:
            print(f"TIMEOUT after {args.timeout} s")
    lap("lifted run")

    def lines(name: str) -> list[str]:
        return (work / name).read_text(encoding="utf-8", errors="replace").splitlines()

    expected, actual = lines("expected.txt"), lines("actual.txt")
    ok = diff = 0
    for i in range(max(len(expected), len(actual))):
        e = expected[i] if i < len(expected) else ""
        a = actual[i] if i < len(actual) else ""
        if e == a:
            print(f"  ok   {e}")
            ok += 1
        else:
            print(f" DIFF  {e}\n  got  {a}")
            diff += 1
    print(f"summary: {ok} ok, {diff} DIFF")
    print("times: " + ", ".join(f"{k} {v}" for k, v in times.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
