"""Measure how evenly Duncan and the camera move, frame by frame, in real time.

Boots a new game into the first level, holds UP, and has the host record every
presented frame (the control channel's `trace`: no pausing, so the timing is
the game's own): the frame delta Δt (0x5e5388), the 200 Hz tick counter
(0x6309e0), the follow camera's eye and target (0x52c61c) and Duncan's model
root node position ([actor +0x74] +0x1c). Prints the spread of each, and
writes the per-frame table to <run dir>/motion-trace.tsv.

What it reports:
  ticks/frame and Δt   whole 5 ms ticks the game counted for each frame
  player speed         units per real second between presents; its spread is
                       how unevenly Duncan moves on the screen's clock
  eye-player distance  horizontal distance from the eye to Duncan; its
                       frame-to-frame change is the chase camera's wobble

The game runs at the recomp's default, the fixed step (--retail: the original
timing). With --smooth (WD_INTERPOLATE, also headless) it reads the display
frames from WD_INTERP_TRACE: their rate, how evenly they fall on the
display's refreshes, how far the game step each shows is from even motion,
and how evenly the shown camera eye moves. --visible (muted, focus kept)
gives real vsync; a headless window is not paced by the display.

usage: python recomp/windream/debug/motion_trace.py [--frames 300] [--turn LEFT|""]
           [--smooth] [--visible] [--args "--scale 1"] [--env WD_SMOOTH_CAMERA=60 ...]
"""

from __future__ import annotations

import argparse
import math
import statistics
import struct
import sys
from itertools import pairwise
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_nav as nav  # noqa: E402

wdctl = nav.wdctl
SCENE = "H18ANGKR.DSN"
DELTA = 0x5E5388  # double: Δt in 30 Hz frames (engine.md, "The fixed step")
COUNTER = 0x6309E0  # 200 Hz tick counter (SYS_UpdateTimer)
FOLLOW = 0x52C61C  # follow camera: +0 eye, +0xc target (int32 xyz), +0x1c actor
MODE = 0x52C874  # camera mode, 0 = follow
NODES = 0x661EE0  # group table: node n of group g at [[NODES] + 4g] + 0x18 + 4n (0x455358)
PAUSE_NS = 50_000_000  # display frames further apart: interpolation paused


def player_node(ctl) -> int:
    """Duncan's model root: actor +0x74 is a node handle (group << 16 | index)."""
    handle = ctl.read32(ctl.read32(FOLLOW + 0x1C) + 0x74)
    group = ctl.read32(ctl.read32(NODES) + 4 * (handle >> 16))
    return ctl.read32(group + 0x18 + 4 * (handle & 0xFFFF))


def spread(values: list[float]) -> str:
    if len(values) < 2:
        return "n/a"
    mean = statistics.fmean(values)
    sd = statistics.pstdev(values)
    cv = sd / mean * 100 if mean else float("nan")
    low, high = min(values), max(values)
    return f"mean {mean:.3f}  sd {sd:.3f}  cv {cv:.1f}%  min {low:.3f}  max {high:.3f}"


def flat_distance(row: dict) -> float:
    """Horizontal distance from the follow camera's eye to Duncan."""
    return math.hypot(row["player"][0] - row["eye"][0], row["player"][2] - row["eye"][2])


def analyse(rows: list[dict]) -> None:
    """Game frames: only pairs of consecutive ones in follow-camera mode count."""
    pairs = [(a, b) for a, b in pairwise(rows) if a["mode"] == 0 and b["mode"] == 0]
    print(f"follow-camera frame pairs {len(pairs)} of {len(rows) - 1}")
    follow = [a for a, _ in pairs] or rows
    ticks = [b["counter"] - a["counter"] for a, b in pairs]
    host = [(b["ns"] - a["ns"]) / 1e6 for a, b in pairs]
    moving = [
        (math.dist(a["player"], b["player"]), h)
        for (a, b), h in zip(pairs, host, strict=True)
        if math.dist(a["player"], b["player"]) > 0.5 and h > 0
    ]
    deltas = [r["delta"] for r in follow]
    print(
        f"frames            {len(follow)} over {(follow[-1]['ns'] - follow[0]['ns']) / 1e9:.2f} s"
    )
    print(f"ticks/frame       { {t: ticks.count(t) for t in sorted(set(ticks))} }")
    print(f"Δt values         { {round(d, 4): deltas.count(d) for d in sorted(set(deltas))} }")
    print(f"host ms/frame     {spread(host)}")
    print(f"player step       {spread([s for s, _ in moving])}")
    print(f"player speed u/s  {spread([s / h * 1000 for s, h in moving])}")
    print(f"eye-player dist   {spread([flat_distance(r) for r in rows if r['mode'] == 0])}")
    wobble = [abs(flat_distance(b) - flat_distance(a)) for a, b in pairs]
    print(f"  per-frame change {spread(wobble)}")


def display(path: Path, start: int, end: int) -> None:
    """The display frames of WD_INTERP_TRACE between two host times: how
    evenly the refreshes they were drawn for are spaced, how far the game
    step each shows is from a straight line through them (in steps; 0 is
    perfectly even motion), and how evenly the shown camera eye moves."""
    frames = []
    for line in path.read_text().splitlines():
        cells = line.split()
        if len(cells) < 5:
            continue  # the last line of a process that did not close the file
        shown, when, step, in_present, stall = cells[:5]
        eye = tuple(float(c) for c in cells[5:8]) if len(cells) >= 8 else None
        if start <= int(shown) <= end:
            frames.append((int(shown), int(when), float(step), int(in_present), int(stall), eye))
    if len(frames) < 3:
        print("display frames    none in the traced window")
        return
    whens = [f[1] for f in frames]
    steps = [f[2] for f in frames]
    span = (frames[-1][0] - frames[0][0]) / 1e9
    # A straight line per stretch of display frames: interpolation pauses
    # (a dialogue, a menu) break the stretch.
    stretches, current = [], [0]
    for i in range(1, len(frames)):
        if whens[i] - whens[i - 1] > PAUSE_NS:
            stretches.append(current)
            current = []
        current.append(i)
    stretches.append(current)
    residual, slopes = [], []
    for stretch in (s for s in stretches if len(s) >= 10):
        ws, ss = [whens[i] for i in stretch], [steps[i] for i in stretch]
        mw, ms = statistics.fmean(ws), statistics.fmean(ss)
        pairs = list(zip(ws, ss, strict=True))
        slope = sum((w - mw) * (x - ms) for w, x in pairs) / sum((w - mw) ** 2 for w in ws)
        slopes.append((slope, len(stretch)))
        residual += [abs(x - (ms + slope * (w - mw))) for w, x in pairs]
    slope = sum(a * b for a, b in slopes) / sum(b for _, b in slopes) if slopes else 0
    in_present = sum(f[3] for f in frames)
    stalls = sum(f[4] for f in frames)
    print(f"stretches         {len(stretches)} (pauses over 50 ms split them)")
    print(
        f"display frames    {len(frames)} over {span:.2f} s = {len(frames) / span:.1f}/s; "
        f"in present {in_present}, stalls {stalls}"
    )
    print(f"refresh gaps ms   {spread([(b - a) / 1e6 for a, b in pairwise(whens)])}")
    print(f"steps per second  {slope * 1e9:.2f} (game steps shown per second of display)")
    print(f"step residual     {spread(residual)}")
    print(f"  max |residual|  {max(residual, default=0):.3f} steps")
    eyes = [(f[1], f[5]) for f in frames if f[5]]
    if len(eyes) > 3:
        # The displayed eye's velocity (units/s) between refreshes, and how much
        # it changes from one refresh to the next: what reads as camera shake.
        velocity = [
            tuple((b[i] - a[i]) / (tb - ta) * 1e9 for i in range(3))
            for (ta, a), (tb, b) in pairwise(eyes)
            if 0 < tb - ta <= PAUSE_NS
        ]
        print(f"eye speed u/s     {spread([math.hypot(*v) for v in velocity])}")
        print(f"eye speed change  {spread([math.dist(u, v) for u, v in pairwise(velocity)])}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--tag", default="motion")
    ap.add_argument("--visible", action="store_true", help="show the window (real vsync)")
    ap.add_argument("--env", action="append", default=[], help="NAME=VALUE for the game")
    ap.add_argument("--args", default="", help="extra run.py arguments, space-separated")
    ap.add_argument("--turn", default="LEFT", help="key held with UP while tracing")
    ap.add_argument(
        "--smooth", action="store_true",
        help="display interpolation (even headless), with its per-display-frame trace",
    )  # fmt: skip
    ap.add_argument(
        "--retail", action="store_true", help="the original timing (WD_FIXED_STEP=0)",
    )  # fmt: skip
    options = ap.parse_args()
    env = dict(e.split("=", 1) for e in options.env)
    if options.visible:
        env.setdefault("WD_FOCUS", "1")  # a window in the background must not pause the game
        env.setdefault("WD_MUTE", "1")
    if options.retail:
        env["WD_FIXED_STEP"] = "0"
    if options.smooth:
        env.update(WD_FIXED_STEP="1", WD_INTERPOLATE="1")
        sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
        import recomp_env

        run_dir = recomp_env.out_dir("windream") / f"run-{options.tag}"
        env["WD_INTERP_TRACE"] = str(run_dir / "interp.trace")
    with wdctl.start_game(
        tag=options.tag,
        headless=not options.visible,
        args=options.args.split() if options.args else [],
        extra_env=env,
    ) as game:
        ctl = game.ctl
        interp_trace = game.run_dir / "interp.trace"
        nav.boot_into(ctl, SCENE)
        ctl.wait(ms=2500)
        node = player_node(ctl)
        # Walk until the level's opening dialogue is over (RETURN ends a line)
        # and the follow camera has held for two seconds, then circle: UP and
        # LEFT keep Duncan in the open ground near the spawn, away from triggers.
        ctl.key_down("UP")
        calm = 0
        for _ in range(200):
            ctl.wait(ms=250)
            if ctl.read8(MODE) != 0:
                ctl.tap("RETURN")
                calm = 0
            elif (calm := calm + 1) >= 8:
                break
        if options.turn:
            ctl.key_down(options.turn)
        ctl.wait(ms=1000)
        path = game.run_dir / "motion-trace.raw"
        ranges = [(DELTA, 8), (COUNTER, 4), (FOLLOW, 24), (MODE, 1), (node + 0x1C, 12)]
        ctl.trace(path, options.frames, ranges)
        ctl.wait(frames=options.frames + 2)
        ctl.key_up("UP")
        if options.turn:
            ctl.key_up(options.turn)
        rows = []
        for line in path.read_text().splitlines():
            frame, ns, *cells = line.split()
            if "-" in cells:
                continue
            data = [bytes.fromhex(c) for c in cells]
            eye_target = struct.unpack("<6i", data[2])
            rows.append(
                {
                    "frame": int(frame),
                    "ns": int(ns),
                    "delta": struct.unpack("<d", data[0])[0],
                    "counter": struct.unpack("<I", data[1])[0],
                    "eye": eye_target[:3],
                    "target": eye_target[3:],
                    "mode": data[3][0],
                    "player": struct.unpack("<3i", data[4]),
                }
            )
        table = game.run_dir / "motion-trace.tsv"
        with table.open("w") as f:
            f.write("frame\tns\tdelta\tcounter\tmode\teye\ttarget\tplayer\n")
            for r in rows:
                f.write(
                    f"{r['frame']}\t{r['ns']}\t{r['delta']:.4f}\t{r['counter']}\t{r['mode']}\t"
                    f"{r['eye']}\t{r['target']}\t{r['player']}\n"
                )
        print(f"[motion] {table}")
        analyse(rows)
        ctl.quit()
        if options.smooth and interp_trace.exists():
            display(interp_trace, rows[0]["ns"], rows[-1]["ns"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
