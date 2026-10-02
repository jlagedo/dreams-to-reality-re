"""Walk and jump across the first map (Project0, Ile d'Angkor, H18ANGKR.DSN) to
the central face tower, checking collision the whole way.

Duncan starts at the spawn, about 3,000 units south of the tower; the way
north climbs four terraces (ground y -232, -445, -645, -845) to the tower's
base at z ~ -700, and the tower's top (y -3740) spans x [-931, 817],
z [-685, 952] in the collision triangles. The isle floats: past its edge
there is nothing until the sky dome (triangles beyond +-20,000), and falling
there opens the system menu. The test holds UP, steers toward the tower's
centre (x -57, z 133) from Duncan's own movement (LEFT turns him
counter-clockwise in world x/z, about 120 degrees a second, measured while he
stands), keeps off the edge, taps CTRL to jump about once a second, and once
at the tower keeps pushing and jumping into its face. Dialogue lines stop
his input until RETURN; the driver presses it whenever he stops moving.

Checked while it plays:
- the game's own log: no `[inv]` line (WD_PHYS_INVARIANT=1, the exact overlap
  invariant after every sweep), no `[phys] re-add` (a leaked node), no crash;
- every few seconds, with the game paused, the retail rules of
  collision_check.py (overlap trees, wall and floor lists, byte accounting,
  endpoint order); its `stale` overlap bits are recorded, not failed;
- every half second, against the triangles' real geometry: Duncan's centre
  never goes from above a surface to below it while over it (falling
  through), there is ground under him, and how deep his sphere sits in any
  triangle.

Penetration has no retail reference here: its depth is reported, and only a
centre closer than half the radius to a surface fails. Leaving the isle,
the menu opening and not reaching the tower are failures of the run (the
driver), listed apart from collision failures.

usage: python recomp/windream/debug/collision_walk.py [--tag TAG] [--seconds 180] [--push 40]
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import collision_check as cc  # noqa: E402
import game_nav as nav  # noqa: E402

wdctl = nav.wdctl
SCENE = "H18ANGKR.DSN"
SPAWN = (-319, -625, -3187)  # Project0's record +0xB4
HEAD = (-57, 133)  # x, z: centre of the tower's top in the collision triangles
ARRIVED = 1100  # horizontal distance from HEAD at which Duncan is at the tower's face
DOME = 20000  # a triangle reaching past this is the sky dome, not the isle
SAMPLE_MS = 500
JUMP_EVERY = 2  # loops of the drive: a jump every other one, about one a second
JUMP_SAMPLE_MS, JUMP_SAMPLES = 100, 7
CHECK_EVERY_S = 15.0
SHOT_EVERY_S = 8.0
LOOK_AHEAD = 450  # units ahead that must have ground under them
STAND_GAP = 40  # centre to support surface within r + this: standing
DEEP = 0.5  # a centre closer than DEEP * r to a triangle fails
COLLISION = (
    "invariant",
    "re-add",
    "crash",
    "exited",
    "fell through",
    "deep penetration",
    "retail rule",
)


# ---- geometry ----


def plane_y(a, b, c, x, z):
    """y of the triangle's plane at (x, z) when (x, z) is inside its xz projection."""
    d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
    if d == 0:
        return None  # vertical
    u = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
    v = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
    if min(u, v, 1 - u - v) < -1e-9:
        return None
    return u * a[1] + v * b[1] + (1 - u - v) * c[1]


def surfaces_under(geometry, x, z):
    out = []
    for tri, (a, b, c) in geometry.items():
        if min(a[0], b[0], c[0]) <= x <= max(a[0], b[0], c[0]) and min(
            a[2], b[2], c[2]
        ) <= z <= max(a[2], b[2], c[2]):
            y = plane_y(a, b, c, x, z)
            if y is not None:
                out.append((y, tri))
    return out


def sub(p, q):
    return (p[0] - q[0], p[1] - q[1], p[2] - q[2])


def dot(p, q):
    return p[0] * q[0] + p[1] * q[1] + p[2] * q[2]


def closest_on_triangle(p, a, b, c):
    """Ericson, Real-Time Collision Detection 5.1.5; a degenerate triangle (two
    vertices equal, or all three in a line) answers with its nearest vertex."""
    try:
        return _closest_on_triangle(p, a, b, c)
    except ZeroDivisionError:
        return min((a, b, c), key=lambda q: math.dist(p, q))


def _closest_on_triangle(p, a, b, c):
    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        return a
    bp = sub(p, b)
    d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0 and d4 <= d3:
        return b
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        t = d1 / (d1 - d3)
        return (a[0] + t * ab[0], a[1] + t * ab[1], a[2] + t * ab[2])
    cp = sub(p, c)
    d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0 and d5 <= d6:
        return c
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        t = d2 / (d2 - d6)
        return (a[0] + t * ac[0], a[1] + t * ac[1], a[2] + t * ac[2])
    va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
        t = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return (b[0] + t * (c[0] - b[0]), b[1] + t * (c[1] - b[1]), b[2] + t * (c[2] - b[2]))
    denom = 1 / (va + vb + vc)
    v, w = vb * denom, vc * denom
    return tuple(a[i] + ab[i] * v + ac[i] * w for i in range(3))


def nearest_surface(geometry, p, r):
    """(distance, tri) of the triangle closest to p among those within r."""
    best = (math.inf, None)
    for tri, (a, b, c) in geometry.items():
        if any(
            min(a[i], b[i], c[i]) > p[i] + r or max(a[i], b[i], c[i]) < p[i] - r for i in range(3)
        ):
            continue
        d = math.dist(p, closest_on_triangle(p, a, b, c))
        if d < best[0]:
            best = (d, tri)
    return best


def cross(p, q):
    return (p[1] * q[2] - p[2] * q[1], p[2] * q[0] - p[0] * q[2], p[0] * q[1] - p[1] * q[0])


def angle(v):
    return math.atan2(v[1], v[0])


def wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


# ---- the run ----


class Walk:
    def __init__(self, game, report):
        self.game, self.ctl, self.report = game, game.ctl, report
        geometry = cc.read_geometry(self.ctl)
        self.ground = {
            t: v for t, v in geometry.items() if all(abs(c) < DOME for p in v for c in p)
        }
        report["triangles"] = {"isle": len(self.ground), "dome": len(geometry) - len(self.ground)}
        self.triangles = None
        self.history: list[tuple[float, tuple[int, int, int]]] = []
        self.support = None  # (tri, y) Duncan last stood on
        self.player = None
        self.radius = 0
        self.samples = 0
        self.t0 = time.monotonic()
        self.last_check = self.last_shot = -1e9
        self.airborne = False
        self.menu_tick = None
        self.left, self.rate = 1, math.radians(120)
        self.inspected: set[int] = set()

    # -- reading --

    def colliders(self):
        n = self.ctl.read32(cc.WORLD + 0x40C)
        pointers = struct.unpack(f"<{n}I", self.ctl.read(cc.WORLD + 0x10, 4 * n))
        out = []
        for index, addr in enumerate(pointers):
            if addr:
                raw = self.ctl.read(addr, 0x2C)
                out.append(
                    {
                        "index": index,
                        "addr": addr,
                        "centre": struct.unpack_from("<3i", raw, 0),
                        "radius": struct.unpack_from("<I", raw, 0xC)[0],
                        "flags": raw[0x10],
                        "swept": any(struct.unpack_from("<6I", raw, 0x14)),
                    }
                )
        return out

    def position(self):
        return struct.unpack("<3i", self.ctl.read(self.player, 12))

    def event(self, kind, **data):
        data.update(kind=kind, t=round(time.monotonic() - self.t0, 1))
        self.report["events"].append(data)
        print(
            f"[walk] {kind} {json.dumps({k: v for k, v in data.items() if k != 'kind'})}",
            flush=True,
        )

    def fail(self, kind, **data):
        self.report["failures"].append(kind)
        self.event("FAIL " + kind, **data)

    def ground_at(self, x, z, below_y=None):
        ys = [
            y for y, _ in surfaces_under(self.ground, x, z) if below_y is None or y >= below_y - 5
        ]
        return min(ys) if ys else None

    # -- one sample --

    def sample(self):
        self.samples += 1
        now = time.monotonic()
        p = self.position()
        prev = self.history[-1][1] if self.history else None
        self.history.append((now, p))
        self.report["trace"].append([round(now - self.t0, 2), *p])
        stats = self.report["stats"]
        text = self.game.stderr_text
        for marker, kind in (
            ("[inv]", "invariant"),
            ("re-add", "re-add"),
            ("=== recomp: CRASH", "crash"),
        ):
            if marker in text and kind not in self.report["failures"]:
                at = text.index(marker)
                self.fail(kind, log=text[at : at + 600])
        if self.game.process.poll() is not None:
            self.fail("exited", code=self.game.process.returncode)
            raise SystemExit(1)
        tick = self.ctl.read32(nav.GAME_MENU_TICK)
        if self.menu_tick is not None and tick != self.menu_tick:
            self.fail("menu opened", position=p)
            raise SystemExit(1)
        self.menu_tick = tick
        # falling through: from above a surface to below it, over it both times
        if prev is not None:
            for tri, (a, b, c) in self.ground.items():
                y0 = plane_y(a, b, c, prev[0], prev[2])
                if y0 is None or prev[1] > y0 - 1:
                    continue
                y1 = plane_y(a, b, c, p[0], p[2])
                if y1 is not None and p[1] > y1 + 10:
                    self.fail(
                        "fell through",
                        before=prev,
                        after=p,
                        triangle=f"{tri:08X}",
                        surface_y=[round(y0), round(y1)],
                    )
        y = self.ground_at(p[0], p[2], p[1])
        if y is None:
            stats["no_ground"] += 1
            self.event("no isle ground under Duncan", position=p)
        else:
            gap = y - p[1]
            stats["max_gap"] = max(stats["max_gap"], round(gap))
            if gap <= self.radius + STAND_GAP:
                if self.airborne:
                    stats["landings"] += 1
                self.airborne = False
                stats["standing"] += 1
            elif gap > self.radius + 3 * STAND_GAP:
                if not self.airborne:
                    stats["takeoffs"] += 1
                self.airborne = True
        distance, tri = nearest_surface(self.ground, p, self.radius)
        if tri is not None:
            stats["max_depth"] = max(stats["max_depth"], round(self.radius - distance, 1))
            stats["contacts"] += 1
            if distance < DEEP * self.radius and tri not in self.inspected:
                self.inspected.add(tri)
                self.fail(
                    "deep penetration",
                    position=p,
                    triangle=f"{tri:08X}",
                    distance=round(distance, 1),
                )
                if len(self.inspected) <= 4:
                    self.inspect(tri, prev, p, distance)
        stats["min_y"] = min(stats["min_y"], p[1])
        stats["max_y"] = max(stats["max_y"], p[1])
        if now - self.last_check >= CHECK_EVERY_S:
            self.full_check()
        if now - self.last_shot >= SHOT_EVERY_S:
            self.last_shot = now
            shot = self.game.run_dir / f"walk-{len(self.report['shots']):02d}.bmp"
            self.ctl.screenshot(shot)
            self.report["shots"].append(
                {"file": shot.name, "position": p, "t": round(now - self.t0, 1)}
            )
        return p

    def record_of(self, tri):
        """Duncan's overlap record for tri (bits, wall node, floor node), or None."""
        rec = self.ctl.read32(self.player + 0x2C)
        for _ in range(4096):
            if not rec:
                return None
            key, bits, right, left, wall, floor = struct.unpack("<IB3x4I", self.ctl.read(rec, 24))
            if key == tri:
                return {"bits": bits, "wall": wall != 0, "floor": floor != 0}
            rec = right if tri > key else left
        return None

    def inspect(self, tri, prev, p, distance):
        """What a deep contact is: the triangle, the side Duncan's centre is on
        (and was on a sample earlier), whether his collider holds it, his speed."""
        a, b, c = self.ground[tri]
        n = cross(sub(b, a), sub(c, a))
        length = math.sqrt(dot(n, n)) or 1
        n = tuple(v / length for v in n)
        side = dot(sub(p, a), n)
        before = dot(sub(prev, a), n) if prev else None
        speed = None
        if len(self.history) >= 2:
            (t0, q0), (t1, q1) = self.history[-2], self.history[-1]
            speed = round(math.dist((q0[0], q0[2]), (q1[0], q1[2])) / max(t1 - t0, 1e-3))
        self.ctl.pause()
        try:
            record = self.record_of(tri)
        finally:
            self.ctl.resume()
            self.menu_tick = None
        shot = self.game.run_dir / f"deep-{len(self.inspected):02d}.bmp"
        self.ctl.screenshot(shot)
        item = {
            "triangle": f"{tri:08X}",
            "vertices": [a, b, c],
            "normal": [round(v, 3) for v in n],
            "kind": "floor" if abs(n[1]) > 0.7 else "wall" if abs(n[1]) < 0.3 else "slope",
            "position": p,
            "previous": prev,
            "distance": round(distance, 1),
            "plane_side_now": round(side, 1),
            "plane_side_before": round(before, 1) if before is not None else None,
            "crossed_plane": before is not None and (side > 0) != (before > 0),
            "speed_xz_per_s": speed,
            "duncan_record": record,
            "shot": shot.name,
        }
        self.report["inspections"].append(item)
        self.event("inspected", **{k: v for k, v in item.items() if k not in ("vertices",)})

    def self_test(self):
        """Positive controls: each detector must see a fault planted in Duncan's tree."""
        records = []
        self.ctl.pause()
        try:
            stack = [self.ctl.read32(self.player + 0x2C)]
            while stack and len(records) < 5000:
                rec = stack.pop()
                if rec:
                    key, bits, right, left, wall, floor = struct.unpack(
                        "<IB3x4I", self.ctl.read(rec, 24)
                    )
                    records.append((rec, key, bits, wall, floor))
                    stack += [right, left]
            walled = next(r for r in records if r[3])
            # 1. structural: a wall node the record no longer points at
            self.ctl.write(walled[0] + 0x10, struct.pack("<I", 0))
            world = cc.snapshot(self.ctl, self.triangles)
            self.triangles = world.triangles
            seen = cc.check(world)["violations"]
            self.ctl.write(walled[0] + 0x10, struct.pack("<I", walled[3]))
            restored = cc.check(cc.snapshot(self.ctl, self.triangles))["violations"]
            self.report["self_test"] = {"structural_seen": seen, "after_restore": restored}
            if not {"wall-node", "wall-list"} <= set(seen):
                self.fail("self-test: structural fault not seen", seen=seen)
            if restored:
                self.fail("self-test: violations after restoring", seen=restored)
            # 2. overlap invariant: an axis-1 bit cleared on a record with bits 0+1 (no nodes)
            target = next((r for r in records if r[2] == 3), None)
            if target is None:
                self.fail("self-test: no record with bits 3 to plant the overlap fault in")
                return
            self.ctl.write(target[0] + 4, bytes([1]))
        finally:
            self.ctl.resume()
            self.menu_tick = None
        self.event("planted", record=f"{target[0]:08X}", triangle=f"{target[1]:08X}")
        self.ctl.key_down("UP")
        for _ in range(20):
            self.ctl.tap("CTRL")  # jumps make the axis-1 sweeps run
            self.ctl.wait(ms=300)
            if "[inv]" in self.game.stderr_text:
                break
        self.ctl.key_up("UP")
        text = self.game.stderr_text
        if "[inv]" in text:
            at = text.index("[inv]")
            self.report["self_test"]["invariant_seen"] = text[at : at + 400]
            self.event("self-test: [inv] fired", log=text[at : at + 200])
        else:
            self.fail("self-test: planted overlap fault not seen by WD_PHYS_INVARIANT")

    def full_check(self):
        self.ctl.pause()
        try:
            world = cc.snapshot(self.ctl, self.triangles)
            self.triangles = world.triangles
            result = cc.check(world)
        finally:
            self.ctl.resume()
            self.menu_tick = None
        self.last_check = time.monotonic()
        self.report["checks"].append(
            {k: result[k] for k in ("records", "nodes", "ties", "unswept")}
            | {
                "stale": {
                    k: result["stale"][k] for k in ("count", "max_edge_distance", "colliders")
                }
            }
            | {"violations": {k: v["count"] for k, v in result["violations"].items()}}
        )
        if result["violations"]:
            self.fail("retail rule", violations=result["violations"])

    def wait(self, ms=SAMPLE_MS):
        self.ctl.wait(ms=ms)
        return self.sample()

    def jump(self):
        """Tap CTRL and follow the arc at 100 ms: take-off, peak and landing y."""
        start = self.history[-1][1]
        self.ctl.tap("CTRL")
        ys = []
        for i in range(JUMP_SAMPLES):
            ys.append(self.wait(JUMP_SAMPLE_MS)[1])
            if i == 2 and len(self.report["jumps"]) in (2, 10, 25, 40):  # a frame in mid-air
                shot = self.game.run_dir / f"jump-{len(self.report['jumps']):02d}.bmp"
                self.ctl.screenshot(shot)
                self.report["shots"].append({"file": shot.name, "position": self.history[-1][1]})
        peak = min(ys)
        self.report["jumps"].append(
            {
                "t": round(time.monotonic() - self.t0, 1),
                "from": start,
                "height": start[1] - peak,
                "landed_y": ys[-1],
            }
        )
        return self.history[-1][1]

    def heading(self, span=1.0):
        now, p = self.history[-1]
        for t, q in reversed(self.history):
            if now - t >= span:
                v = (p[0] - q[0], p[2] - q[2])
                return v if math.hypot(*v) > 40 else None
        return None

    # -- driving --

    def find_player(self):
        """The swept collider with floors (flag 8) nearest the spawn."""
        candidates = [c for c in self.colliders() if c["swept"] and c["flags"] & cc.FLAG_FLOORS]
        best = min(
            candidates,
            key=lambda c: math.dist((c["centre"][0], c["centre"][2]), (SPAWN[0], SPAWN[2])),
        )
        self.player, self.radius = best["addr"], best["radius"]
        self.report["player"] = best
        self.event(
            "player collider", index=best["index"], centre=best["centre"], radius=best["radius"]
        )

    def step_walk(self, ms):
        """Walk forward for ms; the xz movement, or None when he did not move."""
        a = self.position()
        self.ctl.key_down("UP")
        self.wait(ms)
        self.ctl.key_up("UP")
        b = self.wait(300)
        v = (b[0] - a[0], b[2] - a[2])
        return v if math.hypot(*v) > 60 else None

    def turn(self, key, ms):
        self.ctl.key_down(key)
        self.ctl.wait(ms=ms)
        self.ctl.key_up(key)

    def start_moving(self):
        """RETURN through the dialogue until UP moves Duncan."""
        for attempt in range(12):
            self.ctl.tap("RETURN")
            self.wait(400)
            if self.step_walk(600):
                self.event("moving", after_returns=attempt + 1)
                return
        self.fail("never moved")
        raise SystemExit(1)

    def calibrate(self):
        """Which way LEFT turns Duncan, measured standing (no wall to slide on)."""
        for _ in range(6):
            before = self.step_walk(500)
            if not before:
                self.ctl.tap("RETURN")
                continue
            self.turn("LEFT", 400)
            after = self.step_walk(500)
            self.turn("RIGHT", 400)
            if after:
                turn = wrap(angle(after) - angle(before))
                if abs(turn) > math.radians(10):
                    self.left = 1 if turn > 0 else -1
                    self.rate = max(abs(turn) / 0.4, math.radians(45))
                    self.event(
                        "calibrated",
                        left_turn_deg=round(math.degrees(turn)),
                        rate_deg_s=round(math.degrees(self.rate)),
                    )
                    return
            self.ctl.tap("RETURN")
        self.event(
            "calibration inconclusive; LEFT is counter-clockwise, 120 deg/s (measured earlier)"
        )

    def turn_towards(self, target, h, walking):
        p = self.history[-1][1]
        error = wrap(angle((target[0] - p[0], target[1] - p[2])) - angle(h))
        if abs(error) < math.radians(12):
            return error
        key = "LEFT" if (error > 0) == (self.left > 0) else "RIGHT"
        limit = 0.35 if walking else 1.5
        self.turn(key, max(int(min(abs(error) / self.rate, limit) * 1000), 60))
        return error

    def go(self, seconds, push):
        self.ctl.key_down("UP")
        end = time.monotonic() + seconds
        arrived_at = None
        still = 0
        loops = 0
        while time.monotonic() < end:
            loops += 1
            p = self.jump() if loops % JUMP_EVERY == 0 else self.wait(300)
            distance = math.hypot(p[0] - HEAD[0], p[2] - HEAD[1])
            if arrived_at is None and distance < ARRIVED:
                arrived_at = time.monotonic()
                self.report["arrived"] = {"t": round(arrived_at - self.t0, 1), "position": p}
                self.event("at the tower", position=p, distance=round(distance))
                end = arrived_at + push
            h = self.heading(0.5)
            if h is None:
                still += 1
                if still >= 3:  # stopped: a dialogue line holds his input until RETURN
                    self.ctl.tap("RETURN")
                    still = 0
                continue
            still = 0
            ahead = (
                p[0] + LOOK_AHEAD * h[0] / math.hypot(*h),
                p[2] + LOOK_AHEAD * h[1] / math.hypot(*h),
            )
            if self.ground_at(*ahead) is None and arrived_at is None:
                self.ctl.key_up("UP")
                self.event(
                    "edge ahead; turning on the spot", position=p, ahead=[round(v) for v in ahead]
                )
                self.turn_towards(HEAD, h, walking=False)
                self.ctl.key_down("UP")
                continue
            self.turn_towards(HEAD, h, walking=True)
        self.ctl.key_up("UP")
        for _ in range(6):  # settle
            self.wait()
        self.full_check()
        if arrived_at is None:
            self.fail("did not reach the tower", position=self.history[-1][1])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tag", default="collision-walk")
    ap.add_argument("--seconds", type=float, default=180, help="time limit to reach the tower")
    ap.add_argument(
        "--push", type=float, default=40, help="seconds of pushing and jumping at the tower"
    )
    ap.add_argument("--renderer", default="software")
    ap.add_argument(
        "--self-test",
        action="store_true",
        help="plant one structural and one overlap fault and require both detectors to fire",
    )
    options = ap.parse_args()
    report = {
        "scene": SCENE,
        "failures": [],
        "events": [],
        "checks": [],
        "shots": [],
        "jumps": [],
        "inspections": [],
        "trace": [],
        "stats": {
            "no_ground": 0,
            "standing": 0,
            "takeoffs": 0,
            "landings": 0,
            "contacts": 0,
            "max_gap": 0,
            "max_depth": -1e9,
            "min_y": 10**9,
            "max_y": -(10**9),
        },
    }
    game = wdctl.start_game(
        tag=options.tag,
        headless=True,
        args=["--renderer", options.renderer],
        extra_env={"WD_PHYS_INVARIANT": "1"},
    )
    out = game.run_dir / "collision-walk.json"
    try:
        nav.boot_into(game.ctl, SCENE)
        game.ctl.wait(ms=2000)
        walk = Walk(game, report)
        walk.find_player()
        walk.sample()
        walk.full_check()
        walk.start_moving()
        if options.self_test:
            walk.self_test()
        else:
            walk.calibrate()
            walk.go(options.seconds, options.push)
    except SystemExit:
        pass
    finally:
        report["samples"] = len(report["trace"])
        report["stderr_tail"] = game.stderr_text[-3000:]
        game.close()
        out.write_text(json.dumps(report, indent=2))
    collision = sorted({f for f in report["failures"] if f in COLLISION})
    run = sorted({f for f in report["failures"] if f not in COLLISION})
    print(f"[walk] report {out}")
    print(f"[walk] stats {json.dumps(report['stats'])}")
    heights = sorted(j["height"] for j in report["jumps"])
    if heights:
        print(
            f"[walk] {len(heights)} jumps, height min {heights[0]} median "
            f"{heights[len(heights) // 2]} max {heights[-1]}"
        )
    print(f"[walk] collision: {'FAIL ' + ', '.join(collision) if collision else 'PASS'}")
    print(f"[walk] run: {'FAIL ' + ', '.join(run) if run else 'completed'}")
    return 1 if report["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
