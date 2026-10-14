"""The autopilot (recomp/windream/debug/autopilot.py) takes exits by itself.

One headless disc-mode game boots into project 0 and the autopilot, with no
one in the loop, takes LINK0 to Project134: a box on a narrow ledge of the
face tower, about 700 units above the ground, under an overhang that pushes a
flyer coming from the south off the ledge. Then from Project134, whose exits
are tall columns at the ends of its walkways, it takes the exit back. Each leg
must succeed on its first attempt; the plan for project 0 must keep its
margins (room round the approach line, inside the box and round the
alignment point, and a real length of the line inside the box).
Skipped when the development build, its WD_DEVTOOLS option or the two disc
images are missing.
"""

import importlib.util
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "recomp" / "windream" / "debug"))
import autopilot  # noqa: E402
import game_nav as nav  # noqa: E402
import game_play as play  # noqa: E402

wdctl = nav.wdctl
TAG = "autopilot-test"


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def requirements():
    run = load("recomp/windream/run.py")
    recomp_env = run.recomp_env
    build = recomp_env.build_dir(recomp_env.out_dir("windream"))
    exe = build / recomp_env.exe_name("windream_recomp")
    if not exe.is_file():
        pytest.skip(f"{exe} is not built; run: uv run python recomp/windream/build.py")
    if "WD_DEVTOOLS:BOOL=ON" not in (build / "CMakeCache.txt").read_text(errors="replace"):
        pytest.skip(f"{build} was built without WD_DEVTOOLS")
    try:
        run.disc_sources(True, None, None)
    except Exception as error:
        pytest.skip(f"no disc images: {error}")


def settle(ctl):
    """Until the level plays with Duncan on his feet; the entry dialogue first."""
    ctl.resume()
    ctl.wait_until_mem(play.GAME_HANDLER, "==", play.GAME_TICK_FRAME, timeout_ms=90000)
    try:
        ctl.wait_until_mem(play.GAME_HANDLER, "==", play.CTRL_DISPATCHER, timeout_ms=15000)
    except wdctl.CtlError:
        pass
    for _ in range(120):
        if play.mode(ctl) == "playing" and play.read_player(ctl)["movement"] == "ground":
            break
        ctl.wait(ms=500)
    ctl.pause()


@pytest.fixture(scope="module")
def ctl():
    requirements()
    game = wdctl.start_game(tag=TAG, headless=True, args=["--renderer", "software"])
    try:
        nav.boot_into(game.ctl, "H18ANGKR.DSN")
        settle(game.ctl)
        yield game.ctl
    finally:
        game.close()
        shutil.rmtree(game.run_dir, ignore_errors=True)


def test_plan_in_project_0_keeps_room(ctl):
    p = autopilot.plan(ctl, "Project134")
    lo, hi = (
        autopilot.exit_box(ctl, "Project134")["box_min"],
        autopilot.exit_box(ctl, "Project134")["box_max"],
    )
    assert all(lo[k] <= p["goal"][k] <= hi[k] for k in range(3)), p["goal"]
    assert p["goal_clearance"] > autopilot.RADIUS + autopilot.INSIDE_MARGIN
    assert p["approach_clearance"] > autopilot.RADIUS + autopilot.LINE_MARGIN
    assert p["approach_clearance_inside"] > autopilot.RADIUS + autopilot.INSIDE_MARGIN
    assert p["approach_inside"] >= autopilot.MIN_INSIDE
    assert p["approach_room"] >= autopilot.ALIGN_ROOM
    assert p["route"] and p["route"][-2:] == [p["goal"], p["approach_end"]]


def test_takes_project_0_exit_by_itself(ctl):
    result = autopilot.reach_exit(ctl, "Project134")
    assert result["reason"] == "level changed to Project134", str(result)
    assert len(result["attempts"]) == 1, str(result)


def test_takes_project_134_exit_back(ctl):
    settle(ctl)
    assert play.level(ctl) == "Project134"
    result = autopilot.reach_exit(ctl, "Project0")
    assert result["reason"] == "level changed to Project0", str(result)
    assert len(result["attempts"]) == 1, str(result)
