"""Trace the camera every frame in any project while Duncan walks and jumps.

The generic counterpart of camera_watch.py (first map only): starts the
project through bank_patch slot 0 (collision_regress.Project), then pauses the
game and steps it frame by frame with UP held, a jump every 25 frames, RETURN
when a dialogue line stops him and a turn when he is stuck, reading the camera
mode, the eye, camera node 0 and Duncan each frame. A snap is an eye or node-0
move above JUMP units in one frame within a mode, a follow-mode eye farther
than FAR from Duncan, or an eye outside the level's geometry; each keeps the
frames before it and a screenshot. The whole trace goes to
<run dir>/camera-trace.json.

usage: python recomp/windream/debug/camera_trace.py --slot 69 [--frames 2500] [--renderer direct]
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import camera_watch as cwatch  # noqa: E402
import collision_regress as cr  # noqa: E402

JUMP, FAR, RING = 400, 2500, 20
LEVEL_RECORD = 0x661E04  # CAM_TickFollow: [[0x661e04] + 0x144] != 0 skips CAM_EnableCollision
EYE_COLLISION = 0x52C84C  # set each frame by CAM_EnableCollision


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--slot", type=int, required=True)
    ap.add_argument("--frames", type=int, default=2500)
    ap.add_argument("--renderer", default="direct")
    ap.add_argument(
        "--no-eye-collision",
        action="store_true",
        help="set the level record's +0x144, which makes CAM_TickFollow skip CAM_EnableCollision",
    )
    options = ap.parse_args()
    project = cr.Project(options.slot, 0, options.renderer)
    report = {
        "slot": options.slot,
        "snaps": [],
        "trace": [],
        "stats": {"max_eye_step": 0, "max_node_step": 0, "max_eye_player": 0, "modes": {}},
    }
    try:
        if not project.start():
            print(f"[trace] {project.result['outcome']}")
            return 2
        ctl = project.ctl
        if options.no_eye_collision:
            level = ctl.read32(LEVEL_RECORD)
            ctl.write32(level + 0x144, 1)
            print(f"[trace] eye collision off: [{level:08X}+0x144] = 1", flush=True)
        lo = [min(min(p[i] for p in v) for v in project.geometry.values()) for i in range(3)]
        hi = [max(max(p[i] for p in v) for v in project.geometry.values()) for i in range(3)]
        ring, prev, history, held = [], None, [], None
        ctl.pause()
        ctl.key_down("UP")
        try:
            for frame in range(options.frames):
                if frame % 25 == 0:
                    ctl.key_down("CTRL")
                elif frame % 25 == 3:
                    ctl.key_up("CTRL")
                ctl.step(1)
                mode = ctl.read8(cwatch.MODE)
                at = cwatch.MODE_STATE.get(mode, cwatch.STATES)
                eye = struct.unpack("<3i", ctl.read(at, 12))
                group = ctl.read32(ctl.read32(cwatch.NODES))
                node0 = struct.unpack("<3i", ctl.read(ctl.read32(group + 0x18) + 0x1C, 12))
                player = project.position()
                target = struct.unpack("<3i", ctl.read(at + 0xC, 12))
                actor = ctl.read32(cwatch.STATES + 0x1C)
                actor_mode = ctl.read32(actor + 0x34) if actor else -1
                action = ctl.read32(actor + 0x15C) if actor else -1
                collide = ctl.read32(EYE_COLLISION)
                now = {
                    "frame": frame,
                    "mode": mode,
                    "eye": eye,
                    "node0": node0,
                    "player": player,
                    "target": target,
                    "actor_mode": actor_mode,
                    "action": action,
                    "eye_collision": collide,
                }
                report["trace"].append(
                    [frame, mode, *eye, *node0, *player, *target, actor_mode, action, collide]
                )
                stats = report["stats"]
                d = math.dist(eye, player)
                stats["max_eye_player"] = max(stats["max_eye_player"], round(d))
                stats["modes"][str(mode)] = stats["modes"].get(str(mode), 0) + 1
                reasons = []
                if prev and prev["mode"] == mode:
                    step = math.dist(prev["eye"], eye)
                    stats["max_eye_step"] = max(stats["max_eye_step"], round(step))
                    if step > JUMP:
                        reasons.append(f"eye moved {round(step)} in one frame")
                    moved = math.dist(prev["node0"], node0)
                    stats["max_node_step"] = max(stats["max_node_step"], round(moved))
                    if moved > JUMP:
                        reasons.append(f"camera node 0 moved {round(moved)} in one frame")
                if mode == 0 and d > FAR:
                    reasons.append(f"eye {round(d)} from Duncan")
                if any(eye[i] < lo[i] - 1000 or eye[i] > hi[i] + 1000 for i in range(3)):
                    reasons.append("eye outside the level's geometry")
                if reasons and (
                    not report["snaps"] or frame - report["snaps"][-1]["now"]["frame"] > 5
                ):
                    shot = project.game.run_dir / f"snap-{len(report['snaps']):02d}.bmp"
                    ctl.screenshot(shot)
                    report["snaps"].append(
                        {"reasons": reasons, "now": now, "before": list(ring), "shot": shot.name}
                    )
                    print(
                        f"[trace] SNAP frame {frame}: {'; '.join(reasons)} {json.dumps(now)}",
                        flush=True,
                    )
                ring = (ring + [now])[-RING:]
                prev = now
                history.append(player)
                if frame % 8 == 0 and len(history) > 16:
                    if held:
                        ctl.key_up(held)
                        held = None
                    moved = math.dist((player[0], player[2]), (history[-16][0], history[-16][2]))
                    if moved < 30:
                        if frame % 32 == 0:
                            ctl.tap("RETURN")
                        else:
                            held = "LEFT"
                            ctl.key_down(held)
                if frame % 250 == 0:
                    print(f"[trace] frame {frame} {json.dumps(now)}", flush=True)
                if project.game.process.poll() is not None:
                    print("[trace] the game ended", flush=True)
                    break
        finally:
            for key in ("UP", "CTRL", "LEFT"):
                ctl.key_up(key)
            ctl.resume()
    finally:
        out = project.game.run_dir / "camera-trace.json" if project.game else None
        project.finish()
        if out:
            out.write_text(json.dumps(report, indent=1))
            print(f"[trace] {out}")
    print(f"[trace] stats {json.dumps(report['stats'])}; {len(report['snaps'])} snaps")
    return 1 if report["snaps"] else 0


if __name__ == "__main__":
    sys.exit(main())
