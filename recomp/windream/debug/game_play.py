"""Playing the running game turn by turn over the control channel: what an
agent sees (game state, an overhead map) and what it does (hold keys for some
frames, turn to a heading, walk to a point).

Every action pauses the game and leaves it paused, so nothing happens between
two calls however long the caller thinks; time passes only in the frames an
action steps. Helpers for a `wdctl.Ctl`:

    import game_play as play                    # recomp/windream/debug on sys.path
    state = play.observe(ctl)                   # Duncan, actors, exits, ground ahead
    play.turn(ctl, toward=(-57, 133))           # face the tower
    play.walk_to(ctl, -57, 133)                 # walk until there, blocked or interrupted
    play.hold(ctl, ["UP", "CTRL"], 12)          # a running jump

Angles. The world's horizontal plane is (x, z); y grows downward. A facing
or a bearing is in degrees, measured as atan2(dz, dx): 0 is +x, 90 is +z. The
actor's heading (+0x5c, 4096 per turn) gives facing = (4096 - heading) / 4096
* 360, checked by walking in project 0 (heading 3046 walked toward 91 degrees,
2349 toward 155). LEFT lowers the heading, so it raises the facing: a positive
bearing (target minus facing) means turn LEFT. The overhead map draws +x to the
right and +z up, so LEFT turns counter-clockwise there too.

Facts of the retail game used here (WINDREAM.EXE and GDIDREAM.EXE, same
addresses; docs/research/ai-animation-runtime.md, file-formats.md,
engine.md, and live reads in project 0, 2026-10-13):

- The player actor is the second of the 32 actors at 0x4fb7a8 (0x2d0 bytes
  each): 0x4fba78 (host/sdl/dev_tools.c PLAYER).
- Actor: position doubles x, y, z at +0; movement mode +0x34 (1 ground, 2
  swimming, 3 flying); vitality +0x38, magic +0x3c, oxygen +0x40 (floats,
  100-point scales); heading +0x5c; pitch +0x6c (4096 per turn, negative
  nose up); model name +0x8c (char[12]); flags +0xa8..+0xaf; collision
  radius +0x10c; current action +0x15c; airborne +0x278 bit 0; inventory
  pointer +0x30.
- Ticked when +0xa9 & 4 (ENT_TickAll); a walker when +0xa9 & 2; removed when
  +0xab & 4. AI_ScanNearbyActors (0x4131a4) counts a walker as an enemy of
  another when bit +0xa9 & 0x20 differs between them (an ally when it is the
  same); it sorts the player's enemies seen within sphere +0x118 into +0x1dc
  (count +0x204) and his allies into +0x1f0 (count +0x208).
- Combat stance +0xac & 0x20: ENT_TickPlayerControl (0x423767) sets it when
  an enemy is within 800 units (1,500 or 2,500 with a weapon drawn).
- The game runs GAME_TickFrame (0x416d45) as its handler (0x626f74) while a
  level plays; CTRL_Dispatcher (0x40e75c) while a dialogue or the game menu
  holds the frame (ESC or CTRL skips a dialogue line).
- The project record ([0x661e04], 0x2200 bytes) holds eight LINK exits at
  +0x200 (0x80 each): destination +0xc, flags +0x18 (1 in use, 2 no living
  enemy, 4 hit-link object, 8 invert the item test, 0x10 the level's triggers
  done, 0x20 CTRL pressed, 0x40 fire outside the box, 0x80 any actor), box
  min +0x24 and max +0x30 (all zero: no box), required object +0x3c. The
  triggers-done flag is the dword at 0x6155e4.
- Each Δt-1.0 frame a walk moves Duncan about 28 units; a held turn about 3
  degrees (measured, project 0). The yaw rate halves every frame after the
  key is released, so a turn coasts about 3 degrees.

Development only, like the channel itself (recomp/windream/devtools).
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import collision_check as cc  # noqa: E402
import wdctl  # noqa: E402

# ---- guest addresses ----

ACTORS, ACTOR_SIZE, ACTOR_COUNT = 0x4FB7A8, 0x2D0, 32
PLAYER = 0x4FBA78
GAME_HANDLER = 0x626F74
GAME_TICK_FRAME = 0x416D45
CTRL_DISPATCHER = 0x40E75C
CAMERA_MODE = 0x52C874
TRIGGER_DONE = 0x6155E4
PROJECT_POINTER = wdctl.PROJECT_POINTER
LINKS_AT, LINK_SIZE, LINK_COUNT = 0x200, 0x80, 8

MOVEMENT = {1: "ground", 2: "swimming", 3: "flying"}
CAMERA = {0: "follow", 2: "fixed", 3: "track", 4: "pair", 5: "ride", 6: "overhead", 7: "free"}
LINK_FLAGS = (
    (2, "no living enemy"),
    (4, "touch a hit-link object"),
    (0x10, "level triggers done"),
    (0x20, "press CTRL"),
    (0x40, "fires outside the box"),
    (0x80, "any actor"),
)

# ---- tuning of the actions ----

TURN_COAST = 3.0  # degrees a released turn still goes
TURN_TOLERANCE = 4.0
WALK_REAIM = 12.0  # stop and turn when the target is this far off the facing
WALK_CHECK = 4  # frames between checks while walking
STUCK_FRAMES = 24  # no progress in this many frames: blocked
STUCK_PROGRESS = 40  # units of progress that count
EDGE_AHEAD = 300  # units ahead that must have ground
DROP = 600  # a surface this far below the feet is not ground to walk onto
STEP_UP = 120  # a floor this far above Duncan's centre is the one he stands on
RISE = 1500  # a floor up to this far above the feet is a step or a ledge, not an edge
FLOOR_NY = 0.5  # |normal y| at least this: a floor, else a wall
GRID = 512
FLY_MAX_PITCH = 60.0  # degrees fly_to pitches at most (climbing first, then cruising)
LAND_LEAD = 400  # units he still drifts forward after ALT is let go (320 to 460 measured)
MAP_BAND = 4000  # the overhead map draws triangles within this height of Duncan


def _wrap(degrees: float) -> float:
    return (degrees + 180.0) % 360.0 - 180.0


def facing_of(heading: int) -> float:
    return ((4096 - heading % 4096) % 4096) * 360.0 / 4096.0


def _cstr(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("latin-1")


# ---- reading the game ----


def _actor(raw: bytes, slot: int) -> dict:
    flags = raw[0xA8:0xB0]
    heading = struct.unpack_from("<i", raw, 0x5C)[0]
    x, y, z = struct.unpack_from("<3d", raw, 0)
    vitality, magic, oxygen = struct.unpack_from("<3f", raw, 0x38)
    mode = struct.unpack_from("<i", raw, 0x34)[0]
    return {
        "slot": slot,
        "address": ACTORS + slot * ACTOR_SIZE,
        "model": _cstr(raw[0x8C:0x98]),
        "position": [round(x), round(y), round(z)],
        "heading": heading,
        "facing": round(facing_of(heading), 1),
        "pitch": round(pitch_of(struct.unpack_from("<i", raw, 0x6C)[0]), 1),
        "movement": MOVEMENT.get(mode, mode),
        "action": struct.unpack_from("<i", raw, 0x15C)[0],
        "vitality": round(vitality, 1),
        "magic": round(magic, 1),
        "oxygen": round(oxygen, 1),
        "radius": struct.unpack_from("<i", raw, 0x10C)[0],
        "airborne": bool(raw[0x278] & 1),
        "ticked": bool(flags[1] & 4),
        "walker": bool(flags[1] & 2),
        "removed": bool(flags[3] & 4),
        "faction": 1 if flags[1] & 0x20 else 0,
        "combat_stance": bool(flags[4] & 0x20),
    }


def read_actors(ctl) -> list[dict]:
    """The 32 actor slots, decoded (also the empty ones)."""
    raw = b"".join(
        ctl.read(ACTORS + i * ACTOR_SIZE * 8, ACTOR_SIZE * 8) for i in range(ACTOR_COUNT // 8)
    )
    return [_actor(raw[i * ACTOR_SIZE : (i + 1) * ACTOR_SIZE], i) for i in range(ACTOR_COUNT)]


def read_player(ctl) -> dict:
    return _actor(ctl.read(PLAYER, ACTOR_SIZE), (PLAYER - ACTORS) // ACTOR_SIZE)


def inventory(ctl) -> list[dict]:
    """The player's objects with a count (inventory block at actor +0x30:
    names +0x4, 16 bytes each, counts +0x314; game-content.md)."""
    block = ctl.read32(PLAYER + 0x30)
    if not block:
        return []
    raw = ctl.read(block, 0x3B4)
    out = []
    for i in range(32):
        count = struct.unpack_from("<i", raw, 0x314 + 4 * i)[0]
        name = _cstr(raw[4 + 16 * i : 20 + 16 * i])
        if count > 0 and name:
            out.append({"name": name, "count": count})
    return out


def _relative(origin: dict, point) -> dict:
    """Distance and bearing (degrees, positive: turn LEFT) from an actor to an (x, z) point."""
    dx, dz = point[0] - origin["position"][0], point[1] - origin["position"][2]
    return {
        "distance": round(math.hypot(dx, dz)),
        "bearing": round(_wrap(math.degrees(math.atan2(dz, dx)) - origin["facing"]), 1),
    }


def read_exits(ctl, player: dict | None = None) -> list[dict]:
    """The project's live LINK exits, with what each needs and where its box is."""
    pointer = ctl.read32(PROJECT_POINTER)
    if not pointer:
        return []
    raw = ctl.read(pointer + LINKS_AT, LINK_SIZE * LINK_COUNT)
    triggers = ctl.read32(TRIGGER_DONE)
    out = []
    for i in range(LINK_COUNT):
        link = raw[i * LINK_SIZE : (i + 1) * LINK_SIZE]
        flags = link[0x18]
        if not flags & 1:
            continue
        lo = struct.unpack_from("<3i", link, 0x24)
        hi = struct.unpack_from("<3i", link, 0x30)
        item = _cstr(link[0x3C:0x4C])
        needs = [text for bit, text in LINK_FLAGS if flags & bit]
        if item:
            needs.append(f"{'not ' if flags & 8 else ''}holding {item}")
        exit_ = {
            "link": i,
            "to": _cstr(link[0xC:0x18]),
            "needs": needs,
            "triggers_done": bool(triggers),
        }
        if any(lo) or any(hi):
            centre = [(a + b) // 2 for a, b in zip(lo, hi, strict=True)]
            exit_.update(box_min=list(lo), box_max=list(hi), centre=centre)
            if player:
                p = player["position"]
                exit_.update(_relative(player, (centre[0], centre[2])))
                exit_["inside"] = all(lo[k] <= p[k] <= hi[k] for k in range(3))
        else:
            exit_["box"] = None  # fires on its conditions alone
        out.append(exit_)
    return out


# ---- the level's floors ----


class Floors:
    """The collision world's triangles, split into floors and walls, in a
    grid for queries at a point. Read once per level (Floors.of)."""

    _cache: dict = {}

    def __init__(self, geometry: dict):
        self.floors: list[tuple] = []
        self.walls: list[tuple] = []
        self.grid: dict[tuple[int, int], list[int]] = {}
        for a, b, c in geometry.values():
            u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
            if not length:
                continue
            if abs(n[1]) / length >= FLOOR_NY:
                index = len(self.floors)
                self.floors.append((a, b, c))
                for gx in range(min(a[0], b[0], c[0]) // GRID, max(a[0], b[0], c[0]) // GRID + 1):
                    for gz in range(
                        min(a[2], b[2], c[2]) // GRID, max(a[2], b[2], c[2]) // GRID + 1
                    ):
                        self.grid.setdefault((gx, gz), []).append(index)
            else:
                self.walls.append((a, b, c))

    @classmethod
    def of(cls, ctl) -> Floors:
        """The current level's, read when the collision world changed."""
        key = struct.unpack("<2I", ctl.read(cc.WORLD, 8))
        if key not in cls._cache:
            cls._cache.clear()
            cls._cache[key] = cls(cc.read_geometry(ctl))
        return cls._cache[key]

    def heights(self, x: float, z: float) -> list[float]:
        """y of every floor over (x, z)."""
        out = []
        for index in self.grid.get((int(x) // GRID, int(z) // GRID), ()):
            a, b, c = self.floors[index]
            d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if not d:
                continue
            u = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
            v = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
            if min(u, v, 1 - u - v) >= -1e-9:
                out.append(u * a[1] + v * b[1] + (1 - u - v) * c[1])
        return out

    def ground(self, x: float, z: float, feet: float) -> float | None:
        """The floor at (x, z) nearest the feet' height, among those at most
        RISE above and DROP below them (y grows downward); None: a drop or
        nothing at all."""
        ys = [y for y in self.heights(x, z) if feet - RISE <= y <= feet + DROP]
        return min(ys, key=lambda y: abs(y - feet)) if ys else None

    def probe(self, x: float, z: float, feet: float) -> dict:
        """What is at (x, z) for someone standing at `feet`: a floor within
        reach ("floor", "up", "down", with its height relative to the feet,
        negative up), else "high ledge" (a floor more than RISE above: a wall
        to walk into, whatever lies below it), "big drop" (only floors more
        than DROP below; in project 0 that is the sky dome under the isle) or
        "nothing" (no floor at all: the edge of the world)."""
        g = self.ground(x, z, feet)
        if g is not None:
            drop = round(g - feet)
            return {
                "what": "floor" if abs(drop) <= 60 else "up" if drop < 0 else "down",
                "drop": drop,
            }
        ys = self.heights(x, z)
        above = [y for y in ys if y < feet - RISE]
        if above:
            return {"what": "high ledge", "drop": round(max(above) - feet)}
        if ys:
            return {"what": "big drop", "drop": round(min(ys) - feet)}
        return {"what": "nothing", "drop": None}


def _feet(floors: Floors, player: dict) -> float:
    """Where Duncan stands: the floor just under his centre, else centre + radius."""
    x, y, z = player["position"]
    under = [h for h in floors.heights(x, z) if h >= y - STEP_UP]
    return min(under) if under else y + player["radius"]


def ground_ahead(ctl, player: dict, distances=(150, 300, 600, 1000)) -> list[dict]:
    """Along the facing, Floors.probe at each distance: what is there and its
    height relative to the feet (positive lower; y grows downward)."""
    floors = Floors.of(ctl)
    feet = _feet(floors, player)
    x, _, z = player["position"]
    angle = math.radians(player["facing"])
    return [
        {"distance": d, **floors.probe(x + d * math.cos(angle), z + d * math.sin(angle), feet)}
        for d in distances
    ]


# ---- the whole observation ----


def mode(ctl) -> str:
    handler = ctl.read32(GAME_HANDLER)
    if handler == GAME_TICK_FRAME:
        return "playing"
    if handler == CTRL_DISPATCHER:
        return "dialogue or menu"
    return f"other (handler {handler:#x})"


def level(ctl) -> str | None:
    """The project the game is in ("Project0"), None before there is one."""
    pointer = ctl.read32(PROJECT_POINTER)
    return _cstr(ctl.read(pointer, 16)) if pointer else None


def observe(ctl, radius: int = 6000) -> dict:
    """Everything an agent needs to choose the next action, as plain data."""
    project = ctl.current_project()
    actors = read_actors(ctl)
    player = actors[(PLAYER - ACTORS) // ACTOR_SIZE]
    others = []
    for actor in actors:
        if actor["slot"] == player["slot"] or not actor["ticked"] or actor["removed"]:
            continue
        p = actor["position"]
        if not any(p):
            continue
        where = _relative(player, (p[0], p[2]))
        if where["distance"] > radius:
            continue
        if actor["walker"]:
            kind = "enemy" if actor["faction"] != player["faction"] else "ally"
            if actor["vitality"] <= 0:
                kind = "dead " + kind
        else:
            kind = "object"
        others.append(
            {
                "slot": actor["slot"],
                "kind": kind,
                "model": actor["model"],
                "position": p,
                **where,
                "height": p[1] - player["position"][1],
                "vitality": actor["vitality"],
                "action": actor["action"],
                "movement": actor["movement"],
            }
        )
    others.sort(key=lambda a: a["distance"])
    state = {
        "mode": mode(ctl),
        "paused": ctl.status().get("paused"),
        "project": project["name"] if project else None,
        "scene": project["objet0_asset"] if project else None,
        "camera": CAMERA.get(ctl.read8(CAMERA_MODE), ctl.read8(CAMERA_MODE)),
        "player": {
            k: player[k]
            for k in (
                "position",
                "facing",
                "pitch",
                "movement",
                "action",
                "vitality",
                "magic",
                "oxygen",
                "airborne",
                "combat_stance",
            )
        },
        "inventory": inventory(ctl),
        "actors": others,
        "exits": read_exits(ctl, player),
    }
    try:
        state["ground_ahead"] = ground_ahead(ctl, player)
    except wdctl.CtlError as error:  # no level loaded yet
        state["ground_ahead"] = str(error)
    return state


# ---- acting, one paused stretch at a time ----


def hold(ctl, keys: list[str], frames: int) -> dict:
    """Hold keys together for exactly `frames` frames, release them; the game
    stays paused. The player's state before and after."""
    ctl.pause()
    before = read_player(ctl)
    for key in keys:
        ctl.key_down(key)
    try:
        ctl.step(frames)
    finally:
        for key in keys:
            ctl.key_up(key)
    after = read_player(ctl)
    return {"frames": frames, "before": _brief(before), "after": _brief(after)}


def _brief(player: dict) -> dict:
    return {k: player[k] for k in ("position", "facing", "action", "vitality", "magic")}


def turn(
    ctl,
    facing: float | None = None,
    toward: tuple[float, float] | None = None,
    tolerance: float = TURN_TOLERANCE,
    max_frames: int = 200,
) -> dict:
    """Turn on the spot to a facing (degrees) or toward an (x, z) point,
    holding LEFT or RIGHT one frame at a time; paused after. Stops early
    ("interrupted") when a dialogue or menu takes over."""
    ctl.pause()
    player = read_player(ctl)
    if toward is not None:
        x, _, z = player["position"]
        facing = math.degrees(math.atan2(toward[1] - z, toward[0] - x))
    if facing is None:
        raise ValueError("turn needs facing or toward")
    held = None
    frames = 0
    interrupted = None
    try:
        while frames < max_frames:
            if mode(ctl) != "playing":
                interrupted = "dialogue or menu"
                break
            error = _wrap(facing - player["facing"])
            want = None
            if abs(error) > max(tolerance, TURN_COAST):
                want = "LEFT" if error > 0 else "RIGHT"
            if want != held:
                if held:
                    ctl.key_up(held)
                if want:
                    ctl.key_down(want)
                held = want
            if want is None:
                break
            ctl.step(1)
            frames += 1
            player = read_player(ctl)
    finally:
        if held:
            ctl.key_up(held)
    for _ in range(4):  # let the yaw rate die out
        ctl.step(1)
        frames += 1
    player = read_player(ctl)
    answer = {
        "facing": player["facing"],
        "target": round(facing % 360, 1),
        "error": round(_wrap(facing - player["facing"]), 1),
        "frames": frames,
    }
    if interrupted:
        answer["interrupted"] = interrupted
    return answer


def walk_to(
    ctl,
    x: float,
    z: float,
    radius: float = 150,
    max_frames: int = 600,
    stop_for_enemies: bool = True,
    allow_drops: bool = False,
    jump_when_blocked: bool = True,
) -> dict:
    """Walk to (x, z) in straight lines: turn on the spot when the target is
    more than WALK_REAIM degrees off, else hold UP. Stops, paused, on the
    first of: arrived within `radius`; a dialogue or menu took over; vitality
    dropped; combat stance (an enemy near, unless stop_for_enemies is off); no
    floor EDGE_AHEAD units ahead (unless allow_drops); no progress in
    STUCK_FRAMES frames (after one jump when jump_when_blocked); the level
    changing (an exit taken); max_frames."""
    ctl.pause()
    start = read_player(ctl)
    floors = Floors.of(ctl)
    project = level(ctl)
    player = start
    frames = 0
    jumped = False
    best = math.hypot(x - start["position"][0], z - start["position"][2])
    best_at = 0
    reason = "out of frames"
    walking = False

    def stop_walking():
        nonlocal walking
        if walking:
            ctl.key_up("UP")
            walking = False

    try:
        while frames < max_frames:
            px, _, pz = player["position"]
            distance = math.hypot(x - px, z - pz)
            if distance <= radius:
                reason = "arrived"
                break
            if level(ctl) != project:
                reason = f"level changed to {level(ctl)}"
                break
            if mode(ctl) != "playing":
                reason = "dialogue or menu"
                break
            if player["vitality"] < start["vitality"]:
                reason = "took damage"
                break
            if stop_for_enemies and player["combat_stance"] and not start["combat_stance"]:
                reason = "enemy near (combat stance)"
                break
            if distance < best - STUCK_PROGRESS:
                best, best_at = distance, frames
            elif frames - best_at >= STUCK_FRAMES:
                if jump_when_blocked and not jumped:
                    jumped = True
                    best_at = frames
                    ctl.key_down("UP")
                    ctl.key_down("CTRL")
                    ctl.step(3)
                    ctl.key_up("CTRL")
                    ctl.step(20)
                    walking = True
                    frames += 23
                    player = read_player(ctl)
                    continue
                reason = "blocked"
                break
            error = _wrap(math.degrees(math.atan2(z - pz, x - px)) - player["facing"])
            if abs(error) > WALK_REAIM:
                stop_walking()
                done = turn(ctl, toward=(x, z), max_frames=max_frames - frames)
                frames += done["frames"]
                best_at = frames  # turning is not being stuck
                player = read_player(ctl)
                continue
            if not allow_drops:
                angle = math.radians(player["facing"])
                ahead = min(EDGE_AHEAD, distance)
                feet = _feet(floors, player)
                there = floors.probe(
                    px + ahead * math.cos(angle), pz + ahead * math.sin(angle), feet
                )
                if there["what"] in ("big drop", "nothing"):
                    reason = f"edge ahead ({there['what']})"
                    break
            if not walking:
                ctl.key_down("UP")
                walking = True
            ctl.step(WALK_CHECK)
            frames += WALK_CHECK
            player = read_player(ctl)
    finally:
        stop_walking()
        for key in ("CTRL", "LEFT", "RIGHT"):
            ctl.key_up(key)
    ctl.step(1)
    end = read_player(ctl)
    return {
        "reason": reason,
        "frames": frames,
        "from": start["position"],
        "to": end["position"],
        "remaining": round(math.hypot(x - end["position"][0], z - end["position"][2])),
        "facing": end["facing"],
        "vitality": end["vitality"],
    }


def pitch_of(raw_pitch: int) -> float:
    """The actor's pitch +0x6c in degrees, -180 to 180; negative is nose up
    (climbing: y decreases)."""
    return _wrap(raw_pitch % 4096 * 360.0 / 4096.0)


def _read_pitch(ctl) -> float:
    return pitch_of(ctl.read32(PLAYER + 0x6C))


def take_off(ctl) -> dict:
    """From the ground: UP and ALT held, CTRL pressed (ENT_TickPlayerGround,
    flight-capable actor +0xab & 2, magic over 4). ALT stays held: releasing
    it ends the flight. Paused after, ALT down."""
    ctl.pause()
    ctl.key_down("UP")
    ctl.key_down("ALT")
    ctl.step(2)
    ctl.key_down("CTRL")
    ctl.step(4)
    ctl.key_up("CTRL")
    ctl.key_up("UP")
    ctl.step(1)
    player = read_player(ctl)
    return {"flying": player["movement"] == "flying", **_brief(player)}


def fly_to(
    ctl,
    x: float,
    y: float,
    z: float,
    radius: float = 200,
    land: bool = True,
    max_frames: int = 900,
    max_pitch: float = FLY_MAX_PITCH,
) -> dict:
    """Fly to (x, y, z), taking off first when on the ground (turned toward the
    point first). Flies in a straight line: route around what is in the way
    with several calls (land=False on all but the last).

    ENT_TickPlayerFlying (0x422cd7): ALT held keeps him flying (its release
    requests 0xe, the end of the flight); LEFT and RIGHT turn as on the
    ground; DOWN raises the nose and UP lowers it (pitch +0x6c, held when the
    key is let go); he moves along his pitch and facing by himself, about 25
    units a frame; magic drains while he flies and the flight ends at 0 or on
    a hard impact. Checked in project 0: DOWN for 25 frames took the pitch to
    -72 degrees and he climbed about 25 units a frame.

    Steers each frame toward the point and holds the pitch toward the height
    (y grows downward) within max_pitch. Stops, paused, on arriving within
    `radius` horizontally (with land, LAND_LEAD units earlier, then releases
    ALT: the flight ends and he drifts on as he falls; measured in project 0,
    he lands about 300 units further along his course, so approach a narrow
    ledge along its length), on the flight ending (impact, no magic), the level changing, a
    dialogue or menu, damage, or max_frames."""
    ctl.pause()
    start = read_player(ctl)
    project = level(ctl)
    took_off = start["movement"] != "flying"
    if took_off:
        turn(ctl, toward=(x, z))  # he takes off the way he faces
        if not take_off(ctl)["flying"]:
            ctl.key_up("ALT")
            return {"reason": "could not take off", **_brief(read_player(ctl))}
    else:
        ctl.key_down("ALT")
    player = read_player(ctl)
    frames = 0
    reason = "out of frames"
    held: set[str] = set()

    def press(keys):
        for key in held - keys:
            ctl.key_up(key)
        for key in keys - held:
            ctl.key_down(key)
        held.clear()
        held.update(keys)

    try:
        while frames < max_frames:
            px, py, pz = player["position"]
            distance = math.hypot(x - px, z - pz)
            if distance <= radius + (LAND_LEAD if land else 0):
                reason = "arrived"
                break
            if level(ctl) != project:
                reason = f"level changed to {level(ctl)}"
                break
            if player["movement"] != "flying":
                reason = "the flight ended (impact or no magic)"
                break
            if mode(ctl) != "playing":
                reason = "dialogue or menu"
                break
            if player["vitality"] < start["vitality"]:
                reason = "took damage"
                break
            keys = set()
            error = _wrap(math.degrees(math.atan2(z - pz, x - px)) - player["facing"])
            if abs(error) > 3:
                keys.add("LEFT" if error > 0 else "RIGHT")
            # too low (py > y): nose up, a negative pitch
            want = max(-max_pitch, min(max_pitch, -(py - y) / 8))
            pitch = _read_pitch(ctl)
            if pitch > want + 4:
                keys.add("DOWN")
            elif pitch < want - 4:
                keys.add("UP")
            press(keys)
            ctl.step(1)
            frames += 1
            player = read_player(ctl)
    finally:
        press(set())
    if reason == "arrived" and land:
        ctl.key_up("ALT")
        ctl.step(1)
        frames += 1
        for _ in range(60):  # fall until on the ground
            ctl.step(5)
            frames += 5
            player = read_player(ctl)
            if player["movement"] != "flying" and not player["airborne"] and player["action"] == 0:
                break
        reason = "arrived and landed"
    elif reason != "arrived":
        ctl.key_up("ALT")
    end = read_player(ctl)
    return {
        "reason": reason,
        "frames": frames,
        "took_off": took_off,
        "from": start["position"],
        "to": end["position"],
        "remaining": round(math.hypot(x - end["position"][0], z - end["position"][2])),
        "height_error": end["position"][1] - round(y),
        "movement": end["movement"],
        "magic": end["magic"],
        "vitality": end["vitality"],
    }


# ---- the overhead map ----


def overhead_map(ctl, path: str | Path, span: int = 6000, size: int = 768) -> dict:
    """A top-down picture of the level around Duncan, `span` units across:
    floors shaded by height (lighter is higher), walls dark red, Duncan a
    yellow arrow, enemies red, allies green, objects blue, exit boxes cyan
    outlines; +x right, +z up, a grid line every 1,000 units. Needs Pillow."""
    from PIL import Image, ImageDraw

    floors = Floors.of(ctl)
    state = observe(ctl, radius=span)
    px, py, pz = state["player"]["position"]
    scale = size / span

    def at(x, z):
        return ((x - px) * scale + size / 2, size / 2 - (z - pz) * scale)

    image = Image.new("RGB", (size, size), (16, 16, 24))
    draw = ImageDraw.Draw(image)
    half = span / 2
    for k in range(int((px - half) // 1000), int((px + half) // 1000) + 2):
        draw.line([at(k * 1000, pz - half), at(k * 1000, pz + half)], fill=(40, 40, 56))
    for k in range(int((pz - half) // 1000), int((pz + half) // 1000) + 2):
        draw.line([at(px - half, k * 1000), at(px + half, k * 1000)], fill=(40, 40, 56))

    def near(tri):
        """Around Duncan, and within MAP_BAND of his height: leaves out the sky dome."""
        return any(
            abs(v[0] - px) < half * 1.5 and abs(v[2] - pz) < half * 1.5 for v in tri
        ) and all(abs(v[1] - py) < MAP_BAND for v in tri)

    shown = [t for t in floors.floors if near(t)]
    shown.sort(key=lambda t: -(t[0][1] + t[1][1] + t[2][1]))  # lowest first, highest on top
    for tri in shown:
        rel = (sum(v[1] for v in tri) / 3 - py) / 1500  # negative: above Duncan
        shade = max(40, min(230, int(140 - 90 * rel)))
        draw.polygon([at(v[0], v[2]) for v in tri], fill=(shade // 2, shade, shade // 2))
    for tri in floors.walls:
        if near(tri):
            draw.polygon([at(v[0], v[2]) for v in tri], outline=(150, 40, 40))
    for exit_ in state["exits"]:
        if "box_min" in exit_:
            lo, hi = exit_["box_min"], exit_["box_max"]
            a, b = at(lo[0], hi[2]), at(hi[0], lo[2])
            draw.rectangle([a, b], outline=(0, 230, 230), width=2)
            draw.text((a[0] + 2, a[1] + 2), f"L{exit_['link']}", fill=(0, 230, 230))
    colours = {"enemy": (255, 60, 60), "ally": (60, 255, 60), "object": (90, 140, 255)}
    for actor in state["actors"]:
        x, _, z = actor["position"]
        c = at(x, z)
        colour = colours.get(actor["kind"], (128, 128, 128))
        draw.ellipse([c[0] - 5, c[1] - 5, c[0] + 5, c[1] + 5], fill=colour)
        draw.text((c[0] + 7, c[1] - 6), f"{actor['slot']} {actor['model']}", fill=colour)
    angle = math.radians(state["player"]["facing"])
    c = at(px, pz)
    tip = (c[0] + 16 * math.cos(angle), c[1] - 16 * math.sin(angle))
    left = (c[0] + 7 * math.cos(angle + 2.5), c[1] - 7 * math.sin(angle + 2.5))
    right = (c[0] + 7 * math.cos(angle - 2.5), c[1] - 7 * math.sin(angle - 2.5))
    draw.polygon([tip, left, right], fill=(255, 230, 0))
    draw.text((6, 6), f"{state['project']} at ({px}, {py}, {pz}) span {span}", fill=(230, 230, 230))
    image.save(path)
    return {
        "path": str(path),
        "centre": [px, pz],
        "span": span,
        "pixels_per_1000": round(scale * 1000, 1),
    }
