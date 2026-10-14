"""Reach a level's exit by flying, with no one in the loop: plan on the
collision mesh, then fly the plan frame by frame (game_play.py's reads and
keys, the game paused between frames, so a run is deterministic).

    import autopilot                            # recomp/windream/debug on sys.path
    result = autopilot.reach_exit(ctl, "Project134")
    result["reason"]                            # "level changed to Project134", or why not

The plan:
1. Goal: the point inside the exit's box nearest Duncan among those with
   room for him (his centre must stay more than his radius from every
   collision triangle), else the roomiest.
2. Approach: a straight line from an alignment point in open air (room
   to finish a turn), APPROACH long to the box and MIN_INSIDE into it (the
   exit fires on the first frame inside), with room all along; of the roomy
   points and headings, the one nearest the direction from Duncan.
3. Route: A* over a grid of free cells (CELL units; free when the cell's
   centre is more than radius + ROUTE_MARGIN from every triangle) from Duncan
   to the approach point, straightened where the straight line stays in free
   cells; the approach line is appended.
4. Flight: take off facing the route, then each frame steer toward the point
   LOOKAHEAD ahead on the route (pure pursuit) and pitch toward its height.
   Near the alignment point, the final turn onto the approach line
   is planned from Duncan's position and facing as the shortest clear
   turn-straight-turn (Dubins) path with turns of TURN_R, and flown with a
   shorter look-ahead.
   An exit fires when Duncan is inside its box (SCENE_CheckExits), flying or
   not: the run ends when the level changes. A dialogue that holds the frame
   is skipped with ESC.

Flight, measured in project 0 (2026-10-13, out/research/agent-play lab runs):
- Movement comes from the flight clip's root motion, not the velocity vector
  (+0x238 stays 0): about 25-28 units a frame along facing and pitch; the
  flight path angle equals the pitch (-15.7 degrees climbs 7 units per 25).
- Take-off: about 40 frames of the take-off clip (action 12), slow and level;
  the pitch keys do nothing until the cruise clip (action 8) runs.
- DOWN raises the nose and UP lowers it, 2 to 3 degrees a frame, and a few
  degrees of coast; LEFT and RIGHT turn 3.07 degrees a frame (a circle about
  485 units in radius at cruise speed).
- Magic drains about 0.15 a frame while flying.
- Letting ALT go plays a 24-frame glide (action 14) at an almost constant
  height, its speed moving 1 a frame toward about 14, then he drops nearly
  straight down; ALT + DOWN + CTRL starts a slow descent (action 13: 6 units a
  frame forward, 2 to 3 down).

Development only, like the channel itself (recomp/windream/devtools).
"""

from __future__ import annotations

import heapq
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import collision_check as cc  # noqa: E402
import collision_walk as cw  # noqa: E402
import game_play as play  # noqa: E402

DOME = 20000  # a triangle reaching past this is the sky dome
RADIUS = 135  # Duncan's collision sphere (actor +0x10c; plan() reads it)
LINE_MARGIN = 55  # room beyond RADIUS on the approach outside the box (steering, pushes)
ROUTE_MARGIN = 150  # clearance above RADIUS for the route's cells (corners get cut)
CELL = 150
MIN_INSIDE = 50  # units of the approach line inside the exit's box (2 frames)
INSIDE_MARGIN = 15  # room beyond RADIUS on the line inside the box
ALIGN = 600  # the alignment point, this far before the approach
ALIGN_ROOM = 500  # clearance round the alignment point (a turn's radius is 485)
STAGE_NEAR = 350  # the final approach starts this close to the alignment point
TURN_R = 560  # turn radius planned for the final turn (485 measured)
DUBINS_STEP = 30
FINAL_LOOKAHEAD = 160
PURSUIT_WINDOW = 800  # units of route ahead searched for the nearest point
APPROACH = 900
LOOKAHEAD = 350
MAX_CLIMB, MAX_DIVE = 60.0, 45.0  # pitch limits, degrees
TAKEOFF_CLEAR = 350  # free run needed ahead at take-off
MAGIC_PER_FRAME = 0.16
SPEED = 26.0


# ---- the level's triangles ----


class Mesh:
    """The collision triangles (sky dome left out) in a horizontal grid, for
    exact clearance queries."""

    _cache: dict = {}

    def __init__(self, geometry: dict):
        self.tris = [t for t in geometry.values() if all(abs(c) < DOME for v in t for c in v)]
        self.grid: dict[tuple[int, int], list[int]] = {}
        self.g = 400
        for i, t in enumerate(self.tris):
            for gx in range(min(v[0] for v in t) // self.g, max(v[0] for v in t) // self.g + 1):
                for gz in range(min(v[2] for v in t) // self.g, max(v[2] for v in t) // self.g + 1):
                    self.grid.setdefault((gx, gz), []).append(i)
        self.lo = [min(v[k] for t in self.tris for v in t) for k in range(3)]
        self.hi = [max(v[k] for t in self.tris for v in t) for k in range(3)]

    @classmethod
    def of(cls, ctl) -> Mesh:
        key = struct.unpack("<2I", ctl.read(cc.WORLD, 8))
        if key not in cls._cache:
            cls._cache.clear()
            cls._cache[key] = cls(cc.read_geometry(ctl))
        return cls._cache[key]

    def near(self, p, reach: float) -> set[int]:
        out: set[int] = set()
        for gx in range(int((p[0] - reach) // self.g), int((p[0] + reach) // self.g) + 1):
            for gz in range(int((p[2] - reach) // self.g), int((p[2] + reach) // self.g) + 1):
                out.update(self.grid.get((gx, gz), ()))
        return out

    def clearance(self, p, reach: float = 600) -> float:
        """Distance from p to the nearest triangle, at most `reach`."""
        best = reach
        for i in self.near(p, reach):
            t = self.tris[i]
            if min(v[1] for v in t) - best > p[1] or max(v[1] for v in t) + best < p[1]:
                continue
            best = min(best, math.dist(p, cw.closest_on_triangle(p, *t)))
        return best

    def line_clearance(self, a, b, every: float = 40) -> float:
        n = max(1, int(math.dist(a, b) / every))
        return min(self.clearance(_lerp(a, b, i / n)) for i in range(n + 1))


def _lerp(a, b, t):
    return tuple(a[k] + (b[k] - a[k]) * t for k in range(3))


# ---- choosing where to go ----


def exit_box(ctl, destination: str) -> dict:
    for e in play.read_exits(ctl):
        if e["to"] == destination and "box_min" in e:
            return e
    raise ValueError(f"no exit with a box leads to {destination}")


def goal_candidates(mesh: Mesh, lo, hi, here, count: int = 6) -> list[tuple[tuple, float]]:
    """Points inside the box (lo, hi) with clearance over RADIUS +
    INSIDE_MARGIN, the `count` nearest to `here`; else the single clearest point. A tall box
    (an exit that is a column of space) is only searched between the level's
    highest triangle and the box's bottom."""
    need = RADIUS + INSIDE_MARGIN
    top = max(lo[1], mesh.lo[1] - 600)
    if top > hi[1]:
        top = lo[1]
    size = [hi[0] - lo[0], hi[1] - top, hi[2] - lo[2]]
    steps = [max(15.0, s / 14) for s in size]
    roomy, clearest = [], (-1.0, None)
    x = lo[0]
    while x <= hi[0]:
        z = lo[2]
        while z <= hi[2]:
            y = top
            while y <= hi[1]:
                q = (x, y, z)
                c = mesh.clearance(q)
                if c > clearest[0]:
                    clearest = (c, q)
                if c >= need:
                    roomy.append((math.dist(q, here), q, c))
                y += steps[1]
            z += steps[2]
        x += steps[0]
    if roomy:
        roomy.sort()
        return [(q, c) for _, q, c in roomy[:count]]
    return [(clearest[1], clearest[0])]


def _through_box(goal, heading, lo, hi) -> tuple[float, float]:
    """Along the heading through the goal (inside the box): how far back the
    line entered the box and how far on it leaves it, horizontally."""
    a = math.radians(heading)
    d = (math.cos(a), math.sin(a))
    back, on = -1e9, 1e9
    for k, axis in ((0, 0), (1, 2)):
        if abs(d[k]) < 1e-9:
            continue
        t1 = (lo[axis] - goal[axis]) / d[k]
        t2 = (hi[axis] - goal[axis]) / d[k]
        back = max(back, min(t1, t2))
        on = min(on, max(t1, t2))
    return -back, on


def choose_approach(mesh: Mesh, candidates, lo, hi, here, directions: int = 24) -> dict:
    """A straight line at a candidate's height, through it: from an alignment
    point (ALIGN before the approach, with ALIGN_ROOM all round it to finish
    a turn in), APPROACH units to where it enters the box, then MIN_INSIDE
    into the box (the exit fires on the first frame inside, so that is all
    that has to be clear). Outside the box it keeps RADIUS + LINE_MARGIN of
    clearance, inside RADIUS + INSIDE_MARGIN, and the box must be at least
    MIN_INSIDE deep along it. Of those, the heading nearest the direction from
    `here`; else the roomiest. Its goal is MIN_INSIDE / 2 into the box."""
    need_out, need_in = RADIUS + LINE_MARGIN, RADIUS + INSIDE_MARGIN
    good, roomiest = None, None
    for goal, _ in candidates:
        direct = math.degrees(math.atan2(goal[2] - here[2], goal[0] - here[0]))
        for k in range(directions):
            heading = 360.0 * k / directions
            back, on = _through_box(goal, heading, lo, hi)
            a = math.radians(heading)
            d = (math.cos(a), 0.0, math.sin(a))

            def along(s, goal=goal, d=d):
                return (goal[0] + s * d[0], goal[1], goal[2] + s * d[2])

            entry = along(-back)
            mid, deep = along(-back + MIN_INSIDE / 2), along(-back + MIN_INSIDE)
            start = along(-back - APPROACH)
            align = along(-back - APPROACH - ALIGN)
            outside = mesh.line_clearance(align, entry)
            inside = mesh.line_clearance(entry, deep)
            room = mesh.clearance(align, reach=ALIGN_ROOM + 100)
            line = {
                "goal": mid,
                "align": align,
                "start": start,
                "end": deep,
                "heading": heading,
                "clearance": outside,
                "clearance_inside": inside,
                "inside": back + on,
                "room": room,
            }
            ok = (
                outside >= need_out
                and inside >= need_in
                and back + on >= MIN_INSIDE
                and room >= ALIGN_ROOM
            )
            rank = (min(outside / need_out, inside / need_in, room / ALIGN_ROOM), back + on)
            if roomiest is None or rank > roomiest[0]:
                roomiest = (rank, line)
            if ok:
                score = (abs(play._wrap(heading - direct)), -(back + on))
                if good is None or score < good[0]:
                    good = (score, line)
    return good[1] if good is not None else roomiest[1]


# ---- the route ----


class Cells:
    """Free cells of a box of space: a cell is free when its centre is more
    than RADIUS + ROUTE_MARGIN from every triangle."""

    def __init__(self, mesh: Mesh, lo, hi):
        self.mesh = mesh
        self.lo = [lo[k] - (lo[k] % CELL) for k in range(3)]
        self.n = [int((hi[k] - self.lo[k]) // CELL) + 2 for k in range(3)]
        need = RADIUS + ROUTE_MARGIN
        blocked = set()
        for t in mesh.tris:
            tlo = [min(v[k] for v in t) - need for k in range(3)]
            thi = [max(v[k] for v in t) + need for k in range(3)]
            rng = [
                range(max(0, self.index(tlo[k], k)), min(self.n[k], self.index(thi[k], k) + 1))
                for k in range(3)
            ]
            for i in rng[0]:
                for j in rng[1]:
                    for k in rng[2]:
                        if (i, j, k) in blocked:
                            continue
                        c = self.centre((i, j, k))
                        if math.dist(c, cw.closest_on_triangle(c, *t)) < need:
                            blocked.add((i, j, k))
        self.blocked = blocked

    def index(self, value, axis):
        return int((value - self.lo[axis]) // CELL)

    def cell(self, p):
        return tuple(self.index(p[k], k) for k in range(3))

    def centre(self, c):
        return tuple(self.lo[k] + (c[k] + 0.5) * CELL for k in range(3))

    def free(self, c):
        return all(0 <= c[k] < self.n[k] for k in range(3)) and c not in self.blocked

    def nearest_free(self, p):
        start = self.cell(p)
        for r in range(0, 6):
            ring = [
                (start[0] + i, start[1] + j, start[2] + k)
                for i in range(-r, r + 1)
                for j in range(-r, r + 1)
                for k in range(-r, r + 1)
                if max(abs(i), abs(j), abs(k)) == r
            ]
            ring = [c for c in ring if self.free(c)]
            if ring:
                return min(ring, key=lambda c: math.dist(self.centre(c), p))
        return None

    def visible(self, a, b) -> bool:
        n = max(1, int(math.dist(a, b) / (CELL / 3)))
        return all(self.free(self.cell(_lerp(a, b, i / n))) for i in range(n + 1))

    def route(self, a, b) -> list[tuple] | None:
        """A* from the free cell nearest a to the one nearest b, straightened."""
        s, g = self.nearest_free(a), self.nearest_free(b)
        if s is None or g is None:
            return None
        steps = [
            (i, j, k)
            for i in (-1, 0, 1)
            for j in (-1, 0, 1)
            for k in (-1, 0, 1)
            if (i, j, k) != (0, 0, 0)
        ]
        todo = [(0.0, s)]
        cost = {s: 0.0}
        came = {}
        while todo:
            _, c = heapq.heappop(todo)
            if c == g:
                break
            for d in steps:
                n = (c[0] + d[0], c[1] + d[1], c[2] + d[2])
                if not self.free(n):
                    continue
                new = cost[c] + math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2)
                if new < cost.get(n, 1e18):
                    cost[n] = new
                    came[n] = c
                    heapq.heappush(todo, (new + math.dist(n, g), n))
        if g not in cost:
            return None
        path = [g]
        while path[-1] != s:
            path.append(came[path[-1]])
        points = [tuple(a)] + [self.centre(c) for c in reversed(path)] + [tuple(b)]
        straight = [points[0]]
        i = 0
        while i < len(points) - 1:
            j = len(points) - 1
            while j > i + 1 and not self.visible(points[i], points[j]):
                j -= 1
            straight.append(points[j])
            i = j
        return straight


def plan(ctl, destination: str) -> dict:
    """The goal, the approach and the route from where Duncan is."""
    global RADIUS
    RADIUS = play.read_player(ctl)["radius"] or RADIUS
    mesh = Mesh.of(ctl)
    box = exit_box(ctl, destination)
    player = play.read_player(ctl)
    here = tuple(player["position"])
    lo, hi = box["box_min"], box["box_max"]
    line = choose_approach(mesh, goal_candidates(mesh, lo, hi, here), lo, hi, here)
    goal, start_line = line["goal"], line["align"]
    pts = [here, start_line, goal]
    lo = [min(p[k] for p in pts) - 2500 for k in range(3)]
    hi = [max(p[k] for p in pts) + 2500 for k in range(3)]
    lo[1] = max(lo[1], mesh.lo[1] - 900)  # no higher than the highest triangle + 900
    hi[1] = min(hi[1], max(p[1] for p in pts) + 600)
    cells = Cells(mesh, lo, hi)
    route = cells.route(here, start_line)
    tail = [[round(c) for c in goal], [round(c) for c in line["end"]]]
    return {
        "goal": tail[0],
        "goal_clearance": round(mesh.clearance(goal)),
        "approach_heading": line["heading"],
        "approach_clearance": round(line["clearance"]),
        "approach_clearance_inside": round(line["clearance_inside"]),
        "approach_inside": round(line["inside"]),
        "approach_start": [round(c) for c in line["start"]],
        "approach_align": [round(c) for c in start_line],
        "approach_room": round(line["room"]),
        "approach_end": tail[1],
        "route": None if route is None else [[round(c) for c in p] for p in route] + tail,
        "cells": cells,
        "mesh": mesh,
    }


# ---- the final turn ----


def _rel(turn: int, u):
    """Position on a circle relative to its centre while moving along u:
    turning LEFT (counter-clockwise, turn +1) the centre is on the left."""
    return (u[1], -u[0]) if turn > 0 else (-u[1], u[0])


def dubins(p0, heading0: float, p1, heading1: float, r: float) -> list[tuple] | None:
    """Turn-straight-turn paths (LSL, RSR, LSR, RSL) in the horizontal plane
    from (p0, heading0) to (p1, heading1) with turns of radius r, shortest
    first, each a list of (x, z) points every DUBINS_STEP units."""
    out = []
    a0, a1 = math.radians(heading0), math.radians(heading1)
    u0, u1 = (math.cos(a0), math.sin(a0)), (math.cos(a1), math.sin(a1))
    for s1 in (1, -1):
        for s2 in (1, -1):
            c1 = (p0[0] - r * _rel(s1, u0)[0], p0[1] - r * _rel(s1, u0)[1])
            c2 = (p1[0] - r * _rel(s2, u1)[0], p1[1] - r * _rel(s2, u1)[1])
            w = (c2[0] - c1[0], c2[1] - c1[1])
            d = math.hypot(*w)
            alpha = math.atan2(w[1], w[0])
            if s1 == s2:
                phi = alpha
            else:
                if d < 2 * r:
                    continue
                phi = alpha + s1 * math.asin(2 * r / d)
            u = (math.cos(phi), math.sin(phi))
            t1 = (c1[0] + r * _rel(s1, u)[0], c1[1] + r * _rel(s1, u)[1])
            t2 = (c2[0] + r * _rel(s2, u)[0], c2[1] + r * _rel(s2, u)[1])
            straight = math.hypot(t2[0] - t1[0], t2[1] - t1[1])
            if (t2[0] - t1[0]) * u[0] + (t2[1] - t1[1]) * u[1] < -1e-6:
                continue
            arc1 = (s1 * (phi - a0)) % (2 * math.pi)
            arc2 = (s2 * (a1 - phi)) % (2 * math.pi)
            points = []
            for c, s, a_from, arc in ((c1, s1, a0, arc1), (None, 0, 0, 0), (c2, s2, phi, arc2)):
                if c is None:
                    n = max(1, int(straight / DUBINS_STEP))
                    points += [
                        (t1[0] + (t2[0] - t1[0]) * i / n, t1[1] + (t2[1] - t1[1]) * i / n)
                        for i in range(n + 1)
                    ]
                    continue
                n = max(1, int(arc * r / DUBINS_STEP))
                for i in range(n + 1):
                    h = a_from + s * arc * i / n
                    rel = _rel(s, (math.cos(h), math.sin(h)))
                    points.append((c[0] + r * rel[0], c[1] + r * rel[1]))
            out.append((r * (arc1 + arc2) + straight, points))
    out.sort(key=lambda o: o[0])
    return [o[1] for o in out]


def final_approach(mesh: Mesh, player: dict, p: dict) -> tuple[list[tuple], str]:
    """From Duncan's position and facing: the shortest Dubins path onto the
    approach line's start, facing along it, that keeps RADIUS + LINE_MARGIN
    of clearance at the line's height, then the line into the box. Falls back
    to the bare line when no Dubins path is clear."""
    x, _, z = player["position"]
    y = p["goal"][1]
    start, deep = p["approach_start"], p["approach_end"]
    line = [tuple(start), tuple(p["goal"]), tuple(deep)]
    need = RADIUS + LINE_MARGIN
    for path in dubins(
        (x, z), player["facing"], (start[0], start[2]), p["approach_heading"], TURN_R
    ):
        pts = [(q[0], y, q[1]) for q in path]
        if all(mesh.clearance(q) >= need for q in pts[:: max(1, int(120 / DUBINS_STEP))]):
            return pts + line[1:], "dubins"
    return [(x, y, z)] + line, "line"


# ---- flying it ----


def _skip_dialogue(ctl):
    for _ in range(40):
        if play.mode(ctl) == "playing":
            return
        ctl.key_down("ESC")
        ctl.step(2)
        ctl.key_up("ESC")
        ctl.step(10)


class Keys:
    def __init__(self, ctl):
        self.ctl, self.held = ctl, set()

    def set(self, keys):
        for k in self.held - keys:
            self.ctl.key_up(k)
        for k in keys - self.held:
            self.ctl.key_down(k)
        self.held = set(keys)

    def release(self):
        self.set(set())


def _pursuit_target(route, p, segment, lookahead=LOOKAHEAD):
    """The point `lookahead` along the route past p's nearest point on it,
    searched on the current and next segments and on to PURSUIT_WINDOW units
    of route (so a loop in the route is not short-cut), never backwards. Also
    the segment it is on and, on the last segment, how far along it p is
    (beyond 1: past the end)."""
    best = (math.inf, segment, 0.0)
    ahead = 0.0
    i = segment
    while i < len(route) - 1 and (i <= segment + 1 or ahead <= PURSUIT_WINDOW):
        a, b = route[i], route[i + 1]
        ab = [b[k] - a[k] for k in range(3)]
        length2 = max(1e-9, sum(c * c for c in ab))
        raw = sum((p[k] - a[k]) * ab[k] for k in range(3)) / length2
        s = min(1.0, max(0.0, raw))
        d = math.dist(p, _lerp(a, b, s))
        if d < best[0]:
            best = (d, i, s if i < len(route) - 2 else raw)
        ahead += math.sqrt(length2)
        i += 1
    _, segment, t = best
    a, b = route[segment], route[segment + 1]
    left = lookahead + min(1.0, max(0.0, t)) * math.dist(a, b)
    i = segment
    while i < len(route) - 1:
        a, b = route[i], route[i + 1]
        seg = math.dist(a, b)
        if left <= seg or i == len(route) - 2:
            return _lerp(a, b, min(1.0, left / max(seg, 1e-9))), segment, t
        left -= seg
        i += 1
    return route[-1], segment, t


def fly_route(
    ctl,
    route,
    max_frames: int = 1500,
    log: list | None = None,
    on_frame=None,
    lookahead: float = LOOKAHEAD,
    stop_within: float | None = None,
) -> dict:
    """Fly along `route` (already airborne, ALT held) until the level
    changes, the flight ends, the route's end is passed, he is within
    `stop_within` of its end ("near the end"), or max_frames. ALT stays held
    when the flight goes on. on_frame(frames) runs after every frame."""
    keys = Keys(ctl)
    project = play.level(ctl)
    segment = 0
    reason = "out of frames"
    frames = 0
    try:
        while frames < max_frames:
            if play.mode(ctl) != "playing":
                keys.release()
                _skip_dialogue(ctl)
            player = play.read_player(ctl)
            if play.level(ctl) != project:
                reason = f"level changed to {play.level(ctl)}"
                break
            if player["movement"] != "flying":
                reason = "the flight ended (impact or no magic)"
                break
            p = player["position"]
            if stop_within is not None and math.dist(p, route[-1]) <= stop_within:
                reason = "near the end"
                break
            aim, segment, t = _pursuit_target(route, p, segment, lookahead)
            if segment == len(route) - 2 and t > 1.0 + 200 / max(1.0, math.dist(*route[-2:])):
                reason = "passed the end of the route"
                break
            want_heading = math.degrees(math.atan2(aim[2] - p[2], aim[0] - p[0]))
            error = play._wrap(want_heading - player["facing"])
            held = {"ALT"}
            if abs(error) > 2.5:
                held.add("LEFT" if error > 0 else "RIGHT")
            horizontal = math.hypot(aim[0] - p[0], aim[2] - p[2])
            want_pitch = math.degrees(math.atan2(aim[1] - p[1], max(horizontal, 1.0)))
            want_pitch = max(-MAX_CLIMB, min(MAX_DIVE, want_pitch))
            if player["pitch"] > want_pitch + 2:
                held.add("DOWN")
            elif player["pitch"] < want_pitch - 2:
                held.add("UP")
            keys.set(held)
            ctl.step(1)
            frames += 1
            if on_frame is not None:
                on_frame(frames)
            if log is not None:
                log.append([frames, *p, player["facing"], player["pitch"], sorted(held)])
    finally:
        keys.set({"ALT"} if reason in ("out of frames", "near the end") else set())
    return {"reason": reason, "frames": frames, "at": play.read_player(ctl)["position"]}


def take_off_toward(ctl, point, mesh: Mesh) -> dict:
    """Turn toward the first clear heading near the direction of `point` (else
    the roomiest), take off and fly out the take-off clip (pitch keys do
    nothing during it)."""
    player = play.read_player(ctl)
    p = player["position"]
    toward = math.degrees(math.atan2(point[2] - p[2], point[0] - p[0]))
    best, clearest = None, (-1.0, toward)
    for offset in (0, 20, -20, 40, -40, 60, -60, 90, -90, 120, -120, 150, -150, 180):
        a = math.radians(toward + offset)
        end = (p[0] + TAKEOFF_CLEAR * math.cos(a), p[1] - 40, p[2] + TAKEOFF_CLEAR * math.sin(a))
        c = mesh.line_clearance((p[0], p[1] - 40, p[2]), end)
        if c > RADIUS:
            best = toward + offset
            break
        if c > clearest[0]:
            clearest = (c, toward + offset)
    if best is None:  # nowhere quite clear: the roomiest way out
        best = clearest[1]
    play.turn(ctl, facing=best)
    play.take_off(ctl)
    keys = Keys(ctl)
    keys.set({"ALT"})
    for _ in range(80):
        if play.read_player(ctl)["action"] != 12:
            break
        ctl.step(1)
    player = play.read_player(ctl)
    return {"took_off": player["movement"] == "flying", "facing": best, "at": player["position"]}


def wait_for_magic(ctl, need: float, max_frames: int = 3000) -> float:
    """Stand still until magic reaches `need`; the magic reached."""
    frames = 0
    while frames < max_frames:
        magic = play.read_player(ctl)["magic"]
        if magic >= need:
            return magic
        ctl.step(30)
        frames += 30
        if play.mode(ctl) != "playing":
            _skip_dialogue(ctl)
    return play.read_player(ctl)["magic"]


def reach_exit(
    ctl, destination: str, attempts: int = 3, log: list | None = None, on_frame=None
) -> dict:
    """Plan and fly to the exit leading to `destination`, retrying from where
    Duncan ends up. The game stays paused after."""
    ctl.pause()
    _skip_dialogue(ctl)
    history = []
    project = play.level(ctl)
    for _ in range(attempts):
        player = play.read_player(ctl)
        if player["movement"] == "flying":
            ctl.key_up("ALT")  # land first, then plan from the ground
        for _ in range(120):
            player = play.read_player(ctl)
            if player["movement"] == "ground" and not player["airborne"]:
                break
            ctl.step(5)
        p = plan(ctl, destination)
        entry = {k: v for k, v in p.items() if k not in ("cells", "mesh")}
        if p["route"] is None:
            entry["reason"] = "no route"
            history.append(entry)
            break
        route = [tuple(q) for q in p["route"]]
        length = sum(math.dist(route[i], route[i + 1]) for i in range(len(route) - 1))
        need = min(100.0, 10 + MAGIC_PER_FRAME * (length / SPEED + 60))
        entry["magic_needed"] = round(need, 1)
        entry["magic"] = wait_for_magic(ctl, need)
        off = take_off_toward(ctl, route[1], p["mesh"])
        entry["take_off"] = off
        if not off["took_off"]:
            entry["reason"] = off.get("reason", "could not take off")
            history.append(entry)
            continue
        # re-plan from the air: the route's first leg starts where he is now
        here = tuple(play.read_player(ctl)["position"])
        route = p["cells"].route(here, tuple(p["approach_align"]))
        if route is None:
            entry["reason"] = "no route from the take-off"
            history.append(entry)
            ctl.key_up("ALT")
            continue
        entry["flown_route"] = [[round(c) for c in q] for q in route]
        staged = fly_route(ctl, route, log=log, on_frame=on_frame, stop_within=STAGE_NEAR)
        entry["staged"] = staged
        if staged["reason"] in ("near the end", "passed the end of the route"):
            final, kind = final_approach(p["mesh"], play.read_player(ctl), p)
            entry["final"] = kind
            entry["final_path"] = [[round(c) for c in q] for q in final]
            length = sum(math.dist(final[i], final[i + 1]) for i in range(len(final) - 1))
            flown = fly_route(
                ctl,
                final,
                max_frames=int(length / 20) + 90,
                log=log,
                on_frame=on_frame,
                lookahead=FINAL_LOOKAHEAD,
            )
        else:
            flown = staged
        entry.update(flown)
        history.append(entry)
        if play.level(ctl) != project:
            break
    ctl.key_up("ALT")
    final = history[-1] if history else {"reason": "nothing tried"}
    return {"reason": final.get("reason"), "attempts": history, "project": play.level(ctl)}
