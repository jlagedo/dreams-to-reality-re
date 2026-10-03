"""Record the files the native game opens, from boot to a level and a little play.

Development tool behind make_demo.py's file list: it starts the native recomp
through the control channel, drives the menus with game_nav, plays a little
(walk, jump, punch, the in-game menu), optionally fires the level exit, and
writes every open event (guest path, resolved host path, ok) to
out/recomp/web/demo/opens-<tag>.json.

    # the retail files, a new game starting in project 62, then follow its exit
    uv run python recomp/web/demo/record_opens.py --project 62 --follow 1

    # the cut: boot the demo root (make_demo.py first), same session
    uv run python recomp/web/demo/record_opens.py --cut --tag demo-cut

Retail mode links disc 1 into out/recomp/web/rec-root (the EXE's directory:
the CD-root paths) and out/recomp/web/rec-native/CRYO/DREAMS (the install-root
paths), with DREAMS.DAT replaced by a bank whose slot 0 is the chosen project.
`--cut` runs demo-root and demo-native as they are.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import demo_common as dc  # noqa: E402  (this directory is on sys.path when run as a script)
import game_nav as nav  # noqa: E402

LEVEL_ID = (1).to_bytes(4, "little")
KEYS = ["UP", "UP", "LEFT", "UP", "RIGHT", "CTRL", "UP", "ALT"]


def rec_trees(project_slot: int | None) -> tuple[Path, Path]:
    """(exe directory, WD_READ_ROOTS) for a recording of the retail files."""
    disc = dc.disc1()
    dc.link_tree(disc, dc.REC_ROOT)
    inner = dc.REC_NATIVE / "CRYO" / "DREAMS"
    dc.link_tree(disc, inner)
    (inner / "DATA" / "LEVEL.ID").write_bytes(LEVEL_ID)
    if project_slot:
        bank = dc.load_bank()
        bank.copy(project_slot, 0)
        for name in ("DREAMS.DAT", "dreams.dat"):
            target = dc.REC_ROOT / name
            if target.exists():
                target.unlink()
        (dc.REC_ROOT / "dreams.dat").write_bytes(bank.data())
    return dc.REC_ROOT, dc.REC_NATIVE


def drain(ctl, since: int, events: list) -> int:
    while True:
        batch = ctl.log(since=since, max=500)["events"]
        if not batch:
            return since
        events.extend(batch)
        since = batch[-1]["seq"]


def play(ctl, seconds: float, events: list, seq: int, menus: bool = True) -> int:
    """A little play: walk and fight, then the in-game menu's tabs."""
    keys = KEYS
    end = time.time() + seconds
    step = 0
    while time.time() < end:
        ctl.tap(keys[step % len(keys)], ms=300)
        ctl.wait(ms=300)
        step += 1
        seq = drain(ctl, seq, events)
    if menus:
        try:
            nav.open_system_page(ctl)
            seq = drain(ctl, seq, events)
            ctl.tap("ESC")
            ctl.wait(ms=400)
            ctl.tap("ESC")
            ctl.wait(ms=400)
        except nav.NavError as error:
            print("menu walk:", error)
        seq = drain(ctl, seq, events)
    return seq


def record(tag, exe_dir, read_root, scene, play_s, follow, renderer, fire_exit=True):
    args = ["--renderer", renderer, "--exe", str(exe_dir / "GDIDREAM.EXE")]
    env = {"WD_READ_ROOTS": str(read_root), "WD_FILES_LOG": "5000"}
    events: list = []
    boots: list = []
    with nav.wdctl.start_game(tag=tag, discs=False, headless=True, args=args, extra_env=env) as g:
        ctl = g.ctl
        seq = 0
        t0 = time.time()
        nav.boot_into(ctl, scene)
        boots.append({"project": ctl.current_project(), "ms": int((time.time() - t0) * 1000)})
        seq = drain(ctl, seq, events)
        seq = play(ctl, play_s, events, seq)
        shot = dc.REC_OUT / f"{tag}-0.png"
        shot.parent.mkdir(parents=True, exist_ok=True)
        try:
            ctl.screenshot(shot)
        except Exception as error:  # noqa: BLE001
            print("screenshot failed:", error)
        for n in range(follow):
            before = ctl.status()["seq"]
            if fire_exit:
                nav.complete_level_triggers(ctl)
            ctl.wait(ms=6000)
            seq = drain(ctl, seq, events)
            boots.append({"project": ctl.current_project(), "after_exit": n + 1})
            seq = play(ctl, play_s, events, seq, menus=False)
            del before
        stderr = g.stderr_text
        project = ctl.current_project()
    return events, boots, stderr, project


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--tag", default=None)
    ap.add_argument("--project", type=int, default=0, help="retail record to start the new game in")
    ap.add_argument("--cut", action="store_true", help="run the demo root (make_demo.py output)")
    ap.add_argument("--scene", default=None, help="the scene name boot_into waits for")
    ap.add_argument("--play-s", type=float, default=8)
    ap.add_argument("--follow", type=int, default=0, help="fire the level exit this many times")
    ap.add_argument("--renderer", default="direct")
    ap.add_argument("--no-keys", action="store_true", help="stand still: no movement keys")
    a = ap.parse_args()
    if a.no_keys:
        KEYS[:] = ["SHIFT"]
    tag = a.tag or (f"demo-rec{a.project}" if not a.cut else "demo-cut")
    if a.cut:
        exe_dir, read_root = dc.cut_variant(a.project)
    else:
        exe_dir, read_root = rec_trees(a.project)
    scene = a.scene
    if scene is None:
        bank = dc.load_bank()
        scene = bank.parsed(a.project).scene
    events, boots, stderr, project = record(
        tag, exe_dir, read_root, scene, a.play_s, a.follow, a.renderer
    )
    opens = [e for e in events if e.get("kind") == "open"]
    target = dc.REC_OUT / f"opens-{tag}.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps({"events": events, "boots": boots}, indent=1))
    failed = sorted({e["path"] for e in opens if not e["ok"]})
    print(f"{len(opens)} opens ({len(failed)} failed names), project {project and project['name']}")
    print("boots:", boots)
    fatal = [line for line in stderr.splitlines() if "FATAL" in line or "crash" in line.lower()]
    print("fatal/crash lines:", fatal[:5])
    print("->", target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
