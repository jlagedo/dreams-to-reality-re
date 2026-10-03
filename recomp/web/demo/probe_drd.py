"""Which DIALOG.DRD entries does a level request? Boots retail files (record_opens.rec_trees) in a
project and polls the DRD current-entry cell (0x5db3a4) while playing.

    uv run python recomp/web/demo/probe_drd.py --project 0 --seconds 40
"""

from __future__ import annotations

import argparse
import sys
import time

import demo_common as dc
import game_nav as nav
import record_opens as ro

DRD_CURRENT = 0x5DB3A4


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=int, default=0)
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--cut", action="store_true")
    a = ap.parse_args()
    exe_dir, read_root = (dc.DEMO_ROOT, dc.native_tree()) if a.cut else ro.rec_trees(a.project)
    scene = dc.load_bank().parsed(a.project).scene
    env = {"WD_READ_ROOTS": str(read_root), "WD_FILES_LOG": "5000"}
    args = ["--renderer", "direct", "--exe", str(exe_dir / "GDIDREAM.EXE")]
    seen = []
    with nav.wdctl.start_game(
        tag="demo-drd", discs=False, headless=True, args=args, extra_env=env
    ) as g:
        ctl = g.ctl
        nav.boot_into(ctl, scene)
        end = time.time() + a.seconds
        while time.time() < end:
            v = ctl.read32(DRD_CURRENT)
            if not seen or seen[-1] != v:
                seen.append(v)
                print(f"{time.time() - end + a.seconds:6.1f}s drd entry {v:#x}")
            ctl.wait(ms=200)
    return 0


if __name__ == "__main__":
    sys.exit(main())
