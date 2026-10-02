"""Boot the demo root with the native recomp and check it plays.

    uv run python recomp/web/demo/verify_cut.py                  # project 0, then every project
    uv run python recomp/web/demo/verify_cut.py --projects 0,62  # some
    uv run python recomp/web/demo/verify_cut.py --transitions    # also walk every level exit

For each project: a new game starting there (slot 0 of the bank replaced by a
copy of it, as bank_patch.py does), through the boot menus with the keys the
browser build has to send (ESC past the missing intro, RETURN on "New game"),
a few seconds of play, then checks that the process did not crash and that no
file the game asked for and the pack lacks matters (see `tolerated`).
`--transitions` stands in each link's box and waits for the destination to load.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import demo_common as dc
import game_nav as nav
import record_opens as ro

# Names the retail game also fails to open in the full retail install.
ALWAYS_ABSENT = {"magie.alp", "anim.alp", "pyram.alp", "touches.spr", "interf.alp"}
# Cut on purpose: the intro and menu movies and the talking head (the game waits for ESC past
# a missing intro and goes on without the other two), and the levels' animated textures.
CUT_MOVIES = {"data\\hnm\\intro.hnm", "data\\hnm\\generic.hnm", "data\\hnm\\tete_e~1.hnm"}


def tolerated(path: str, root: Path) -> str | None:
    """Why a failed open is not a problem (None: it is one)."""
    low = path.replace("/", "\\").lower()
    base = low.rsplit("\\", 1)[-1]
    if base in ALWAYS_ABSENT:
        return "absent in the retail install too"
    if "data\\game\\" in low:
        return "saves: none yet"
    if low in CUT_MOVIES:
        return "movie cut from the pack on purpose"
    if low.startswith("data\\anim\\") or "cryo\\dreams\\data\\anim\\" in low:
        return "animated texture: optional, cut from the pack"
    stem, _, ext = base.rpartition(".")
    if ext in ("dsn", "dan"):
        if any(p.stem.lower() == stem for p in (root / "DATA" / "3DC").iterdir()):
            return "the loader tries .DSN, then .DAN, then the asset's own name"
    return None


def link_conditions(bank, slot: int, dest: int) -> tuple[int, str]:
    """(flags, object that must be held) of the link of `slot` to Project<dest>."""
    cell = dict(dc.project.slots(bytes(bank.records[slot]), "LINK"))[bank.link_slot(slot, dest)]
    return cell[0x18], dc.project._cstr(cell[0x3C:0x4C])


def run_project(
    slot: int,
    seconds: float,
    keys: bool,
    spawn_link: int | None = None,
    wait_for: str | None = None,
    root: Path = dc.DEMO_ROOT,
    tag: str | None = None,
    hold_ctrl: bool = False,
    triggers_done: bool = False,
):
    """-> dict(ok, project, events, problems, crash, reached)."""
    exe_dir, read_root = dc.cut_variant(slot, spawn_in_link=spawn_link, demo_root=root)
    scene = dc.load_bank().parsed(slot).scene
    args = ["--renderer", "direct", "--exe", str(exe_dir / "GDIDREAM.EXE")]
    env = {"WD_READ_ROOTS": str(read_root), "WD_FILES_LOG": "5000"}
    events: list = []
    reached = None
    tag = tag or f"demo-cut{slot}"
    with nav.wdctl.start_game(tag=tag, discs=False, headless=True, args=args, extra_env=env) as g:
        ctl = g.ctl
        seq = 0
        nav.boot_into(ctl, scene)
        seq = ro.drain(ctl, seq, events)
        if hold_ctrl:
            ctl.key_down("CTRL")
        if triggers_done:
            nav.complete_level_triggers(ctl)
        end = time.time() + seconds
        step = 0
        while time.time() < end:
            if keys:
                ctl.tap(ro.KEYS[step % len(ro.KEYS)], ms=300)
            ctl.wait(ms=300)
            step += 1
            seq = ro.drain(ctl, seq, events)
            if wait_for:
                cur = ctl.current_project()
                if cur and cur["name"] == wait_for:
                    reached = cur
                    break
        project = ctl.current_project()
        stderr = g.stderr_text
    crash = "=== recomp: CRASH" in stderr or "FATAL" in stderr
    problems = sorted(
        {
            e["path"]
            for e in events
            if e.get("kind") == "open"
            and not e["ok"]
            and not e.get("write")
            and tolerated(e["path"], root) is None
        }
    )
    return {
        "slot": slot,
        "project": project,
        "events": events,
        "problems": problems,
        "crash": crash,
        "reached": reached,
        "stderr": stderr,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--projects", help="comma list (default: the pack's projects)")
    ap.add_argument("--seconds", type=float, default=6)
    ap.add_argument("--keys", action="store_true", help="walk and fight while waiting")
    ap.add_argument("--transitions", action="store_true")
    a = ap.parse_args()
    import json  # noqa: PLC0415

    manifest = json.loads((dc.DEMO_OUT / "manifest.json").read_text())
    projects = [int(x) for x in a.projects.split(",")] if a.projects else manifest["projects"]
    bad = 0
    for slot in projects:
        r = run_project(slot, a.seconds, a.keys)
        name = r["project"] and r["project"]["name"]
        status = (
            "ok" if not r["crash"] and not r["problems"] and name == f"Project{slot}" else "FAIL"
        )
        bad += status != "ok"
        print(f"project {slot:3d}: {status}  {name}  crash={r['crash']}  missing={r['problems']}")
    if a.transitions:
        from bank_patch import Bank  # noqa: PLC0415

        bank = Bank((dc.DEMO_ROOT / "DREAMS.DAT").read_bytes())
        for slot in projects:
            for _, cell in dc.project.slots(bytes(bank.records[slot]), "LINK"):
                dest = dc.project._cstr(cell[12:24])
                if not dest.startswith("Project"):
                    continue
                d = int(dest[7:])
                if d == 0:
                    continue  # slot 0 of a variant is a copy of the start project
                flags, held = link_conditions(bank, slot, d)
                if held:
                    print(f"link {slot:3d} -> {d:3d}: skipped, the player must hold {held}")
                    continue
                r = run_project(
                    slot,
                    30,
                    False,
                    spawn_link=d,
                    wait_for=f"Project{d}",
                    tag=f"demo-cut{slot}-{d}",
                    hold_ctrl=bool(flags & 0x20),
                    triggers_done=bool(flags & 0x10),
                )
                ok = r["reached"] is not None and not r["crash"] and not r["problems"]
                bad += not ok
                print(
                    f"link {slot:3d} -> {d:3d}: {'ok' if ok else 'FAIL'} flags {flags:#x} "
                    f"reached={r['reached'] and r['reached']['name']} "
                    f"crash={r['crash']} missing={r['problems']}"
                )
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
