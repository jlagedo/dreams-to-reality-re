"""Run the recompiled game from DREAMS_OUT/recomp/windream/run (sandbox, logs,
snapshots and crash dumps land there).

  run.py                                   play (window, sound, real keyboard)
  run.py --seconds 60 --keys 2000:ESC,5000:RETURN --snap-ms 4000
                                           unattended test: scripted keys, a
                                           snapshot every 4 s, stopped after 60 s

Afterwards it prints the crash block from stderr.txt (host stack, registers,
guest stack, minidump path), or otherwise the lines worth a look.

--exe defaults to DREAMS_DISC1/GDIDREAM.EXE. --read-roots is where the guest's
C:\\CRYO\\DREAMS\\... resolves: by default the directory holding CRYO/DREAMS in
DREAMS_INSTALL_ROOT.

usage: uv run python recomp/windream/run.py [options]
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import recomp_env  # noqa: E402

from dreams import paths  # noqa: E402


def read_roots() -> str:
    install = paths.configured("install_root")
    if install is None:
        sys.exit("set DREAMS_INSTALL_ROOT (the retail CRYO/DREAMS tree) or pass --read-roots")
    parts = [p.upper() for p in install.parts[-2:]]
    return str(install.parent.parent if parts == ["CRYO", "DREAMS"] else install)


def main() -> int:
    ap = argparse.ArgumentParser(description="Run the recompiled game.")
    ap.add_argument("--exe", help="guest exe (default DREAMS_DISC1/GDIDREAM.EXE)")
    ap.add_argument("--read-roots", help="extra read roots, ';'-separated")
    ap.add_argument("--seconds", type=int, default=0, help="stop after N s (0 = play)")
    ap.add_argument("--keys", default="", help="scripted keys, ms:KEY,...")
    ap.add_argument("--snap-ms", type=int, default=0, help="snapshot interval")
    ap.add_argument("--fps", type=int, default=25, help="frame cap (0 = uncapped)")
    ap.add_argument(
        "--dump", choices=("full", "mini", "0"), default="full",
        help="crash minidump: full = all memory incl. the guest arena",
    )  # fmt: skip
    args = ap.parse_args()

    exe = args.exe or str(paths.disc(1) / "GDIDREAM.EXE")
    out = recomp_env.out_dir("windream")
    run = out / "run"
    run.mkdir(exist_ok=True)
    for snap in run.glob("snap_*.bmp"):
        snap.unlink()
    unattended = args.seconds > 0
    env = dict(
        os.environ,
        WD_READ_ROOTS=args.read_roots or read_roots(),
        WD_KEYS=args.keys,
        WD_SNAP_MS=str(args.snap_ms) if args.snap_ms else "",
        WD_FPS=str(args.fps),  # "0" = uncapped (an empty value would unset it)
        WD_DUMP=args.dump,
        WD_QUIET="1" if unattended else "",
        WD_FOCUS="1" if unattended else "",
    )
    binary = out / "build" / recomp_env.exe_name("windream_recomp")
    with open(run / "stderr.txt", "wb") as err, open(run / "stdout.txt", "wb") as so:
        p = subprocess.Popen([str(binary), exe, "--run"], cwd=run, env=env, stdout=so, stderr=err)
        try:
            p.wait(timeout=args.seconds or None)
            print(f"exited with code {p.returncode}")
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            print(f"still running after {args.seconds} s (stopped)")

    log = (run / "stderr.txt").read_text(encoding="utf-8", errors="replace").splitlines()
    at = next((i for i, line in enumerate(log) if "=== recomp: CRASH" in line), None)
    if at is not None:
        print("\n".join(log[at : at + 71]))
    else:
        worth = [line for line in log if re.search(r"MessageBox|unresolved|ExitProcess", line)]
        print("\n".join(worth[:10]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
