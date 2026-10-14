"""Turn-by-turn play over the control channel (recomp/windream/debug/game_play.py).

The ground probe and the angle conventions are checked on made-up triangles.
One headless disc-mode game then boots into project 0 (Ile d'Angkor) and
checks the observation against the project's own record (the spawn, LINK0 to
Project134) and the actions against what Duncan does: turning lands on the
asked facing, walking toward the face tower climbs the terraces (y grows
downward) and arrives, and flying onto the tower's ledge and walking into
LINK0's box takes him to Project134. Skipped when the development build, its WD_DEVTOOLS
option or the two disc images are missing.
"""

import importlib.util
import math
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "recomp" / "windream" / "debug"))
import game_nav as nav  # noqa: E402
import game_play as play  # noqa: E402

wdctl = nav.wdctl
TAG = "game-play-test"
SPAWN = (-319, -625, -3187)  # Project0's record +0xB4 (collision_walk.py)
TOWER = (-57, 133)  # x, z: the face tower's centre (collision_walk.py)
SOUTH = (-1200, -1300)  # x, z: open air south-west of the tower, over its terraces
LEDGE = (-1200, 450)  # x, z: on the ledge north of LINK0's box, floor y about -1520


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# ---- without a game ----


def square(x0, x1, z0, z1, y):
    return [((x0, y, z0), (x1, y, z0), (x1, y, z1)), ((x0, y, z0), (x1, y, z1), (x0, y, z1))]


def test_probe_tells_floor_step_ledge_drop_and_nothing():
    triangles = [
        *square(-1000, 1000, -1000, 1000, 0),  # where he stands
        *square(1000, 2000, -1000, 1000, -200),  # a step up
        *square(2000, 3000, -1000, 1000, -3000),  # a tower top
        *square(-3000, -1000, -1000, 1000, 2500),  # far below
        ((0, 0, 1000), (0, -500, 1000), (500, 0, 1000)),  # a wall: not a floor
    ]
    floors = play.Floors(dict(enumerate(triangles)))
    assert len(floors.floors) == 8 and len(floors.walls) == 1
    assert floors.probe(0, 0, 0) == {"what": "floor", "drop": 0}
    assert floors.probe(1500, 0, 0) == {"what": "up", "drop": -200}
    assert floors.probe(2500, 0, 0) == {"what": "high ledge", "drop": -3000}
    assert floors.probe(-2000, 0, 0) == {"what": "big drop", "drop": 2500}
    assert floors.probe(0, 5000, 0) == {"what": "nothing", "drop": None}


def test_angles():
    assert play.facing_of(0) == 0
    assert play.facing_of(1024) == 270  # LEFT lowers the heading and raises the facing
    assert play.facing_of(3072) == 90
    assert play.facing_of(4096 + 3072) == 90
    duncan = {"position": [0, 0, 0], "facing": 90.0}
    assert play._relative(duncan, (0, 100)) == {"distance": 100, "bearing": 0.0}
    assert play._relative(duncan, (-100, 0))["bearing"] == 90.0  # to his left
    assert play._relative(duncan, (100, 0))["bearing"] == -90.0


# ---- in project 0 ----


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


@pytest.fixture(scope="module")
def ctl():
    requirements()
    game = wdctl.start_game(tag=TAG, headless=True)
    try:
        ctl = game.ctl
        nav.boot_into(ctl, "H18ANGKR.DSN")
        # The opening camera and dialogue hold the level for a while
        # (AGENTS.md): wait for the dialogue, then for the level to play.
        ctl.wait_until_mem(play.GAME_HANDLER, "==", play.GAME_TICK_FRAME, timeout_ms=60000)
        try:
            ctl.wait_until_mem(play.GAME_HANDLER, "==", play.CTRL_DISPATCHER, timeout_ms=20000)
        except wdctl.CtlError:
            pass
        for _ in range(40):
            if play.mode(ctl) == "playing" and ctl.read8(play.CAMERA_MODE) == 0:
                break
            ctl.tap("ESC")
            ctl.wait(ms=500)
        yield ctl
    finally:
        game.close()
        shutil.rmtree(game.run_dir, ignore_errors=True)


def test_observation_matches_the_project_record(ctl):
    state = play.observe(ctl)
    assert state["mode"] == "playing" and state["project"] == "Project0"
    assert state["scene"] == "H18ANGKR.DSN" and state["camera"] == "follow"
    player = state["player"]
    assert player["movement"] == "ground" and player["vitality"] > 0
    x, _, z = player["position"]
    assert math.hypot(x - SPAWN[0], z - SPAWN[2]) < 400, player  # still near the spawn
    to = {e["to"]: e for e in state["exits"]}
    assert "Project134" in to and not to["Project134"]["inside"]
    assert all(a["kind"] != "enemy" for a in state["actors"])  # the isle has none
    assert [p["distance"] for p in state["ground_ahead"]] == [150, 300, 600, 1000]
    assert state["ground_ahead"][0]["what"] in ("floor", "up", "down")


def test_turn_lands_on_the_facing(ctl):
    for facing in (0, 120, 250):
        done = play.turn(ctl, facing=facing)
        assert abs(done["error"]) <= 6, str(done)
    assert ctl.status()["paused"]


def until_done(ctl, action, *args, **kwargs):
    """Run an action again after a dialogue line stops it (ESC ends the line)."""
    for _ in range(6):
        done = action(ctl, *args, **kwargs)
        if done.get("reason") != "dialogue or menu":
            return done
        for _ in range(20):
            if play.mode(ctl) == "playing":
                break
            ctl.tap("ESC")
            ctl.step(15)
    return done


def test_walk_toward_the_tower_climbs_and_arrives(ctl):
    start = play.read_player(ctl)
    done = until_done(ctl, play.walk_to, *TOWER, radius=1300, max_frames=900)
    assert done["reason"] == "arrived", str(done)
    assert done["to"][1] < start["position"][1] - 150, str(done)  # higher up: y grows downward
    assert ctl.status()["paused"]
    after = play.observe(ctl)
    assert after["player"]["vitality"] == start["vitality"]


def test_fly_onto_the_ledge_and_take_the_exit(ctl):
    """The route an agent found (2026-10-13): LINK0's box (Project134) sits on a
    ledge of the tower's west side, about 700 units above the base ring, out of
    a jump's reach. Fly above the ledge, drop onto it, walk into the box."""
    exit_ = next(e for e in play.observe(ctl)["exits"] if e["to"] == "Project134")
    centre = exit_["centre"]
    # The straight line from the tower's south face goes through it, and the ledge is
    # narrow across (x) and long along z: come in from the south, along it.
    around = until_done(ctl, play.fly_to, SOUTH[0], -1950, SOUTH[1], radius=250, land=False)
    assert around["reason"] == "arrived" and around["movement"] == "flying", str(around)
    assert around["took_off"]
    flown = until_done(ctl, play.fly_to, LEDGE[0], -1950, LEDGE[1], radius=120)
    assert flown["reason"] == "arrived and landed", str(flown)
    assert flown["movement"] == "ground"
    lo, hi = exit_["box_min"], exit_["box_max"]
    assert lo[1] <= flown["to"][1] <= hi[1], str(flown)  # on the ledge: the box's height
    walked = until_done(ctl, play.walk_to, centre[0], centre[2], radius=25, max_frames=300)
    assert walked["reason"] == "level changed to Project134", str(walked)
    ctl.resume()
    ctl.wait_until_mem(play.GAME_HANDLER, "==", play.GAME_TICK_FRAME, timeout_ms=60000)
    assert play.observe(ctl)["project"] == "Project134"
