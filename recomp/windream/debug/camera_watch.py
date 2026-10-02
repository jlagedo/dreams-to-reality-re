"""Catch the camera snapping away while Duncan walks and jumps against walls
on the first map (Project0, H18ANGKR.DSN).

The camera (docs/research/engine.md, "Camera and projection"): CAM_CompCameraPos
(0x4099d2) runs once per frame; the mode is the byte at 0x52c874 and each mode
keeps its state in a block from 0x52c61c (follow, mode 0), 0x52c6a4 (2 fixed),
0x52c6e8 (3 track), 0x52c72c (4 pair), 0x52c770 (5 ride), 0x52c7b4 (6
overhead), 0x52c7f8 (7 free): eye int32 xyz +0, target +0xc. In follow mode
the eye eases a quarter of the gap per frame (y an eighth) toward a point 528
behind the actor and is then pushed out of geometry by CAM_CollideEye
(0x40d5e4) when the push-out is small enough. So within one mode the eye
moves smoothly; a cut belongs to a mode change.

The driver walks Duncan to the central tower (collision_walk.py's steering),
then pauses the game and steps it one frame at a time while it holds UP,
jumps about once a second and steers around the tower's base (walls, steps,
roots), reading the camera and Duncan every frame. A snap is
- the eye jumping more than JUMP units in one frame with the mode unchanged,
- the eye more than FAR units from Duncan in follow mode, or
- the eye outside the isle's collision geometry.
Each snap keeps the frames before it and a screenshot of the frame.

usage: python recomp/windream/debug/camera_watch.py [--frames 3000] [--tag TAG]
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import collision_walk as cw  # noqa: E402

nav, wdctl = cw.nav, cw.wdctl
STATES = 0x52C61C
MODE = 0x52C874
MODE_STATE = {
    0: 0x52C61C,
    2: 0x52C6A4,
    3: 0x52C6E8,
    4: 0x52C72C,
    5: 0x52C770,
    6: 0x52C7B4,
    7: 0x52C7F8,
}
BLOCK = MODE + 4 - STATES
JUMP = 400  # units the eye may move in one frame within a mode
JERK = 150  # a one-frame eye move above this is counted (not a failure)
NODES = 0x661EE0  # group table: node n of group g at [[NODES] + 4g] + 0x18 + 4n (0x455358)
NODE_DRIFT = 50  # follow mode: camera node 0 (what is drawn) vs the eye
FAR = 2500  # eye to Duncan, follow mode (eye distance 528, lead 1024)
RING = 20
ORBIT = 1150  # waypoints around the tower's base, this far from its centre


class Watch:
    def __init__(self, walk: cw.Walk, report: dict):
        self.walk, self.ctl, self.report = walk, walk.ctl, report
        lo = [min(min(v[i] for v in t) for t in walk.ground.values()) for i in range(3)]
        hi = [max(max(v[i] for v in t) for t in walk.ground.values()) for i in range(3)]
        self.bounds = (lo, hi)
        self.ring: list[dict] = []
        self.frame = 0
        self.prev = None

    def read(self) -> dict:
        block = self.ctl.read(STATES, BLOCK)
        mode = block[MODE - STATES]
        at = MODE_STATE.get(mode, STATES) - STATES
        eye = struct.unpack_from("<3i", block, at)
        target = struct.unpack_from("<3i", block, at + 0xC)
        player = self.walk.position()
        group = self.ctl.read32(self.ctl.read32(NODES))
        node0 = self.ctl.read32(group + 0x18)
        camera = struct.unpack("<3i", self.ctl.read(node0 + 0x1C, 12))
        self.report["trace"].append([self.frame, mode, *eye, *camera, *player])
        return {
            "frame": self.frame,
            "mode": mode,
            "eye": eye,
            "target": target,
            "node0": camera,
            "player": player,
        }

    def outside(self, p) -> bool:
        lo, hi = self.bounds
        return any(p[i] < lo[i] - 500 or p[i] > hi[i] + 500 for i in range(3))

    def check(self, now: dict) -> None:
        stats = self.report["stats"]
        d_player = math.dist(now["eye"], now["player"])
        stats["max_eye_player"] = max(stats["max_eye_player"], round(d_player))
        stats["modes"][str(now["mode"])] = stats["modes"].get(str(now["mode"]), 0) + 1
        reasons = []
        prev = self.prev
        if prev is not None and prev["mode"] == now["mode"]:
            step = math.dist(prev["eye"], now["eye"])
            stats["max_eye_step"] = max(stats["max_eye_step"], round(step))
            if step > JUMP:
                reasons.append(f"eye moved {round(step)} in one frame")
            elif step > JERK:
                stats["jerks"] += 1
                self.report["jerks"].append({"now": now, "before": self.ring[-3:]})
            moved = math.dist(prev["node0"], now["node0"])
            stats["max_node_step"] = max(stats["max_node_step"], round(moved))
            if moved > JUMP:
                reasons.append(f"camera node 0 moved {round(moved)} in one frame")
        if now["mode"] == 0 and d_player > FAR:
            reasons.append(f"eye {round(d_player)} from Duncan")
        if self.outside(now["eye"]):
            reasons.append("eye outside the isle")
        if self.outside(now["node0"]):
            reasons.append("camera node 0 outside the isle")
        drift = math.dist(now["eye"], now["node0"])
        if now["mode"] == 0 and drift > NODE_DRIFT:
            reasons.append(f"camera node 0 {round(drift)} from the eye")
        if reasons:
            shot = self.walk.game.run_dir / f"snap-{len(self.report['snaps']):02d}.bmp"
            self.ctl.screenshot(shot)
            self.report["snaps"].append(
                {"reasons": reasons, "now": now, "before": list(self.ring), "shot": shot.name}
            )
            print(
                f"[cam] SNAP frame {self.frame}: {'; '.join(reasons)} {json.dumps(now)}", flush=True
            )
        self.ring = (self.ring + [now])[-RING:]
        self.prev = now

    def run(self, frames: int) -> None:
        """Step frame by frame: UP held, a jump every 25 frames, steering every 6."""
        ctl, walk = self.ctl, self.walk
        ctl.pause()
        ctl.key_down("UP")
        waypoint = 0
        held = None
        history = []
        try:
            for self.frame in range(frames):
                if self.frame % 25 == 0:
                    ctl.key_down("CTRL")
                elif self.frame % 25 == 3:
                    ctl.key_up("CTRL")
                ctl.step(1)
                now = self.read()
                self.check(now)
                history.append(now["player"])
                if self.frame % 6 == 0 and len(history) > 12:
                    if held:
                        ctl.key_up(held)
                        held = None
                    p, q = history[-1], history[-12]
                    angle = waypoint * math.pi / 6
                    radius = ORBIT + (350 if waypoint % 2 else -250)  # across the steps
                    goal = (
                        cw.HEAD[0] + radius * math.cos(angle),
                        cw.HEAD[1] + radius * math.sin(angle),
                    )
                    if math.dist((p[0], p[2]), goal) < 300:
                        waypoint += 1
                    h = (p[0] - q[0], p[2] - q[2])
                    if math.hypot(*h) > 20:
                        error = cw.wrap(cw.angle((goal[0] - p[0], goal[1] - p[2])) - cw.angle(h))
                        if abs(error) > math.radians(15):
                            held = "LEFT" if (error > 0) == (walk.left > 0) else "RIGHT"
                            ctl.key_down(held)
                    elif self.frame % 30 == 0:
                        ctl.tap("RETURN")  # a dialogue line holds his input
                if self.frame % 250 == 0:
                    print(f"[cam] frame {self.frame} {json.dumps(now)}", flush=True)
                if game_over(walk):
                    break
        finally:
            for key in ("UP", "CTRL", "LEFT", "RIGHT"):
                ctl.key_up(key)
            ctl.resume()
        self.report["frames"] = self.frame + 1


def game_over(walk: cw.Walk) -> bool:
    return walk.game.process.poll() is not None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tag", default="camera-watch")
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--renderer", default="software")
    options = ap.parse_args()
    report = {
        "snaps": [],
        "stats": {
            "max_eye_step": 0,
            "max_node_step": 0,
            "max_eye_player": 0,
            "jerks": 0,
            "modes": {},
        },
        "jerks": [],
        "failures": [],
        "events": [],
        "checks": [],
        "shots": [],
        "jumps": [],
        "inspections": [],
        "trace": [],
        "stats_walk": {},
    }
    walk_stats = {
        "no_ground": 0,
        "standing": 0,
        "takeoffs": 0,
        "landings": 0,
        "contacts": 0,
        "max_gap": 0,
        "max_depth": -1e9,
        "min_y": 10**9,
        "max_y": -(10**9),
    }
    game = wdctl.start_game(
        tag=options.tag,
        headless=True,
        args=["--renderer", options.renderer],
        extra_env={"WD_PHYS_INVARIANT": "1"},
    )
    out = game.run_dir / "camera-watch.json"
    try:
        nav.boot_into(game.ctl, cw.SCENE)
        game.ctl.wait(ms=2000)
        walk_report = dict(report, stats=walk_stats)
        walk = cw.Walk(game, walk_report)
        cw.CHECK_EVERY_S = 1e9  # the per-frame camera trace is the point here
        cw.SHOT_EVERY_S = 1e9
        walk.find_player()
        walk.sample()
        walk.start_moving()
        walk.calibrate()
        walk.go(60, 0)  # to the tower, no push phase
        Watch(walk, report).run(options.frames)
    finally:
        report["stderr_tail"] = game.stderr_text[-2000:]
        game.close()
        out.write_text(json.dumps(report, indent=2))
    print(f"[cam] report {out}")
    print(f"[cam] {report.get('frames', 0)} frames, stats {json.dumps(report['stats'])}")
    print(f"[cam] {len(report['snaps'])} snaps")
    return 1 if report["snaps"] else 0


if __name__ == "__main__":
    sys.exit(main())
