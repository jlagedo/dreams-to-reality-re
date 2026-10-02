"""Collision regression over every project: start a new game in each one
(bank_patch slot 0), walk and jump for a while with every collision check on,
and tabulate.

Per project, headless, the direct (GPU) renderer unless --renderer software,
WD_PHYS_INVARIANT=1:
- new game through the menus (game_nav.boot_into) into the record's scene;
- the player's collider: the floor-flagged collider nearest the record's spawn;
- then for --seconds: UP held, a jump (CTRL) about once a second, RETURN when
  he stops (a dialogue line), a turn when he is still stuck;
- every 400 ms: the game's log (`[inv]`, re-add, crash), falling through a
  triangle (from the real vertices), ground under him, how deep his sphere
  sits in geometry, and the camera: mode, eye distance to him and whether the
  eye left the level's geometry;
- every 15 s, paused: the retail rules of collision_check.py;
- stops early when the in-game menu opens (he fell off the level), the
  project changes (he walked into a link) or the process ends.

Results: DREAMS_OUT/recomp/collision-regression/slot-NNN.json per project
(kept across runs; --redo to replace), summary.json and summary.md for the
whole sweep. The failures here are the collision ones (invariant, re-add,
crash, fell through, deep penetration, retail rule); boot problems, menus and
links are outcomes of the run, listed apart.

usage: python recomp/windream/debug/collision_regress.py [--slots 0-149] [--workers 3]
                                                          [--seconds 40] [--redo]
"""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import struct
import sys
import time
import traceback
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import bank_patch  # noqa: E402
import collision_check as cc  # noqa: E402
import collision_walk as cw  # noqa: E402
import game_nav as nav  # noqa: E402
import recomp_env  # noqa: E402

wdctl = nav.wdctl
OUT = recomp_env.out_dir("collision-regression")
SAMPLE_MS = 400
CHECK_EVERY_S = 15.0
STUCK_SAMPLES = 4
LOOK_AHEAD = 500  # units ahead of him that must have geometry under them
HUGE = 15000  # a triangle this long on an axis is a sky dome, not level geometry
CAM_MODE = 0x52C874
CAM_STATES = {
    0: 0x52C61C,
    2: 0x52C6A4,
    3: 0x52C6E8,
    4: 0x52C72C,
    5: 0x52C770,
    6: 0x52C7B4,
    7: 0x52C7F8,
}
CAM_FAR = 2500  # follow mode: eye distance 528 behind the actor, lead 1024
# Deep penetration has no retail reference, so the sweep reports it apart.
COLLISION = tuple(f for f in cw.COLLISION if f != "deep penetration")
INV_LINE = re.compile(
    r"\[inv\]\s+tri ([0-9A-F]{8}) \[(-?\d+), (-?\d+)\] vs \[(-?\d+), (-?\d+)\]: (.*)"
)
READD_LINE = re.compile(
    r"\[phys\] re-add #\d+ col ([0-9A-F]{8}) axis (\d) tri ([0-9A-F]{8}) "
    r"bits (\d+) -> (\w+) ret ([0-9A-F]{8})"
)


def parse_slots(text: str, count: int) -> list[int]:
    out = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            out += range(int(a), int(b) + 1)
        elif part.strip():
            out.append(int(part))
    return [s for s in out if 0 <= s < count]


class Project:
    def __init__(self, slot: int, seconds: float, renderer: str = "direct"):
        self.slot, self.seconds, self.renderer = slot, seconds, renderer
        bank = bank_patch.Bank.from_disc(1)
        self.bank = bank
        record = bank.parsed(slot)
        self.scene = record.scene
        self.spawn = record.spawn_position
        self.result = {
            "slot": slot,
            "name": record.name,
            "scene": self.scene,
            "level": bank.level(slot),
            "spawn": self.spawn,
            "outcome": None,
            "failures": [],
            "events": [],
            "checks": [],
            "camera": {"modes": {}, "max_eye_player": 0, "far": 0, "outside": 0, "first_far": None},
            "stats": {
                "samples": 0,
                "standing": 0,
                "airborne": 0,
                "no_ground": 0,
                "contacts": 0,
                "max_depth": None,
                "max_gap": 0,
                "distance": 0,
                "jumps": 0,
                "returns": 0,
                "turns": 0,
                "edge_turns": 0,
                "min_y": None,
                "max_y": None,
            },
        }
        self.t0 = time.monotonic()
        self.game = self.ctl = None
        self.geometry = {}
        self.bounds = None
        self.player = None
        self.radius = 0
        self.prev = None
        self.menu_tick = None
        self.project_name = None
        self.triangles = None
        self.last_check = -1e9
        self.inspected = set()

    # -- reporting --

    def event(self, kind: str, **data) -> None:
        data.update(kind=kind, t=round(time.monotonic() - self.t0, 1))
        self.result["events"].append(data)

    def fail(self, kind: str, **data) -> None:
        if kind not in self.result["failures"]:
            self.result["failures"].append(kind)
        self.event("FAIL " + kind, **data)

    # -- start --

    def start(self) -> bool:
        tag = f"collreg-{self.slot:03d}"
        run_dir = recomp_env.out_dir("windream") / f"run-{tag}"
        shutil.rmtree(run_dir, ignore_errors=True)
        if self.slot != 0:
            self.bank.copy(self.slot, 0)
        self.bank.write(run_dir / "sandbox")
        self.game = wdctl.start_game(
            tag=tag,
            headless=True,
            args=["--renderer", self.renderer, "--scale", "1"],  # 640x480: three GL games fit
            extra_env={"WD_PHYS_INVARIANT": "1"},
        )
        self.ctl = self.game.ctl
        self.result["renderer"] = self.renderer
        try:
            nav.boot_into(self.ctl, self.scene)
        except (nav.NavError, wdctl.CtlError) as error:
            self.result["outcome"] = f"boot failed: {error}"
            return False
        self.ctl.wait(ms=2500)
        project = self.ctl.current_project()
        self.project_name = project["name"] if project else None
        self.menu_tick = self.ctl.read32(nav.GAME_MENU_TICK)
        self.geometry = cc.read_geometry(self.ctl)
        level = {
            t: v
            for t, v in self.geometry.items()
            if all(max(p[i] for p in v) - min(p[i] for p in v) < HUGE for i in range(3))
        }
        self.result["triangles"] = {"all": len(self.geometry), "level": len(level)}
        if level:
            self.bounds = (
                [min(min(p[i] for p in v) for v in level.values()) for i in range(3)],
                [max(max(p[i] for p in v) for v in level.values()) for i in range(3)],
            )
        self.geometry = level or self.geometry
        self.find_player()
        self.full_check()
        return True

    def colliders(self) -> list[dict]:
        n = self.ctl.read32(cc.WORLD + 0x40C)
        pointers = struct.unpack(f"<{n}I", self.ctl.read(cc.WORLD + 0x10, 4 * n)) if n else ()
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

    def find_player(self) -> None:
        """The swept, floor-flagged collider nearest the record's spawn: the
        record also has an inert collider standing exactly at the spawn."""
        cols = self.colliders()
        swept = [c for c in cols if c["swept"] and c["flags"] & cc.FLAG_FLOORS]
        floors = swept or [c for c in cols if c["flags"] & cc.FLAG_FLOORS] or cols
        if not floors:
            self.player = None
            return
        best = min(floors, key=lambda c: math.dist(c["centre"], self.spawn))
        self.player, self.radius = best["addr"], best["radius"]
        self.result["player"] = {k: v for k, v in best.items()}
        self.result["player"]["spawn_distance"] = round(math.dist(best["centre"], self.spawn))

    # -- one sample --

    def position(self):
        return struct.unpack("<3i", self.ctl.read(self.player, 12))

    def camera(self) -> dict:
        mode = self.ctl.read8(CAM_MODE)
        state = CAM_STATES.get(mode, CAM_STATES[0])
        eye = struct.unpack("<3i", self.ctl.read(state, 12))
        return {"mode": mode, "eye": eye}

    def sample(self) -> tuple | None:
        """Returns the position, or None when the run must stop."""
        stats, cam = self.result["stats"], self.result["camera"]
        if self.game.process.poll() is not None:
            text = self.game.stderr_text
            if "=== recomp: CRASH" in text:
                at = text.index("=== recomp: CRASH")
                self.fail("crash", log=text[at : at + 800])
            self.result["outcome"] = f"process ended ({self.game.process.returncode})"
            return None
        text = self.game.stderr_text
        for marker, kind in (("[inv]", "invariant"), ("re-add", "re-add")):
            if marker in text and kind not in self.result["failures"]:
                at = text.index(marker)
                self.fail(kind, log=text[at : at + 600])
        tick = self.ctl.read32(nav.GAME_MENU_TICK)
        if tick != self.menu_tick:
            self.result["outcome"] = "menu opened"
            return None
        project = self.ctl.current_project()
        if project and self.project_name and project["name"] != self.project_name:
            self.result["outcome"] = f"level changed to {project['name']}"
            return None
        p = self.position()
        stats["samples"] += 1
        stats["min_y"] = p[1] if stats["min_y"] is None else min(stats["min_y"], p[1])
        stats["max_y"] = p[1] if stats["max_y"] is None else max(stats["max_y"], p[1])
        prev = self.prev
        if prev is not None:
            stats["distance"] += round(math.dist((p[0], p[2]), (prev[0], prev[2])))
            for tri, (a, b, c) in self.geometry.items():
                y0 = cw.plane_y(a, b, c, prev[0], prev[2])
                if y0 is None or prev[1] > y0 - 1:
                    continue
                y1 = cw.plane_y(a, b, c, p[0], p[2])
                if y1 is not None and p[1] > y1 + 10:
                    self.fail("fell through", before=prev, after=p, triangle=f"{tri:08X}")
        under = [y for y, _ in cw.surfaces_under(self.geometry, p[0], p[2]) if y >= p[1] - 5]
        if under:
            gap = min(under) - p[1]
            stats["max_gap"] = max(stats["max_gap"], round(gap))
            if gap <= self.radius + cw.STAND_GAP:
                stats["standing"] += 1
            elif gap > self.radius + 3 * cw.STAND_GAP:
                stats["airborne"] += 1
        else:
            stats["no_ground"] += 1
        distance, tri = cw.nearest_surface(self.geometry, p, self.radius)
        if tri is not None:
            depth = round(self.radius - distance, 1)
            stats["contacts"] += 1
            stats["max_depth"] = (
                depth if stats["max_depth"] is None else max(stats["max_depth"], depth)
            )
            if distance < cw.DEEP * self.radius and tri not in self.inspected:
                self.inspected.add(tri)
                self.fail(
                    "deep penetration",
                    position=p,
                    triangle=f"{tri:08X}",
                    distance=round(distance, 1),
                )
        c = self.camera()
        cam["modes"][str(c["mode"])] = cam["modes"].get(str(c["mode"]), 0) + 1
        d = math.dist(c["eye"], p)
        cam["max_eye_player"] = max(cam["max_eye_player"], round(d))
        if c["mode"] == 0 and d > CAM_FAR:
            cam["far"] += 1
            if cam["first_far"] is None:
                cam["first_far"] = {
                    "eye": c["eye"],
                    "player": p,
                    "t": round(time.monotonic() - self.t0, 1),
                }
                shot = self.game.run_dir / "camera-far.bmp"
                self.ctl.screenshot(shot)
        if self.bounds and any(
            c["eye"][i] < self.bounds[0][i] - 1000 or c["eye"][i] > self.bounds[1][i] + 1000
            for i in range(3)
        ):
            cam["outside"] += 1
        self.prev = p
        return p

    def full_check(self) -> None:
        self.ctl.pause()
        try:
            world = cc.snapshot(self.ctl, self.triangles)
            self.triangles = world.triangles
            result = cc.check(world)
        finally:
            self.ctl.resume()
            self.menu_tick = self.ctl.read32(nav.GAME_MENU_TICK)
        self.last_check = time.monotonic()
        self.result["checks"].append(
            {k: result[k] for k in ("records", "nodes", "ties", "unswept")}
            | {"stale": result["stale"]["count"]}
            | {"violations": {k: v["count"] for k, v in result["violations"].items()}}
        )
        if result["violations"]:
            self.fail("retail rule", violations=result["violations"])

    # -- driving --

    def drive(self) -> None:
        ctl, stats = self.ctl, self.result["stats"]
        if self.player is None:
            self.result["outcome"] = "no collider"
            return
        end = time.monotonic() + self.seconds
        still, loops, returns_in_a_row = 0, 0, 0
        ctl.key_down("UP")
        try:
            while time.monotonic() < end:
                loops += 1
                ctl.wait(ms=SAMPLE_MS)
                p = self.sample()
                if p is None:
                    return
                if loops % 2 == 0:
                    ctl.tap("CTRL")
                    stats["jumps"] += 1
                if self.edge_ahead(p):
                    ctl.key_up("UP")
                    ctl.key_down("LEFT")
                    ctl.wait(ms=800)
                    ctl.key_up("LEFT")
                    ctl.key_down("UP")
                    stats["edge_turns"] += 1
                    self.prev = p
                    continue
                recent = self.recent_move(p)
                if recent < 40:
                    still += 1
                else:
                    still, returns_in_a_row = 0, 0
                if still >= STUCK_SAMPLES:
                    still = 0
                    if returns_in_a_row < 2:
                        ctl.tap("RETURN")
                        stats["returns"] += 1
                        returns_in_a_row += 1
                    else:
                        ctl.key_down("LEFT")
                        ctl.wait(ms=700)
                        ctl.key_up("LEFT")
                        stats["turns"] += 1
                        returns_in_a_row = 0
                if time.monotonic() - self.last_check >= CHECK_EVERY_S:
                    before = time.monotonic()
                    self.full_check()
                    end += time.monotonic() - before  # the pause is not driving time
            self.result["outcome"] = "ran"
        finally:
            for key in ("UP", "LEFT"):
                ctl.key_up(key)

    def edge_ahead(self, p) -> bool:
        """No level triangle under the point LOOK_AHEAD units along his movement:
        the edge of the level (or of this level's geometry) is coming."""
        history = self.__dict__.get("_history", [])
        if len(history) < 2:
            return False
        q = history[-2]
        h = (p[0] - q[0], p[2] - q[2])
        length = math.hypot(*h)
        if length < 30:
            return False
        ahead = (p[0] + LOOK_AHEAD * h[0] / length, p[2] + LOOK_AHEAD * h[1] / length)
        return not cw.surfaces_under(self.geometry, *ahead)

    def recent_move(self, p) -> float:
        history = self.__dict__.setdefault("_history", [])
        history.append(p)
        del history[:-STUCK_SAMPLES]
        return math.dist((p[0], p[2]), (history[0][0], history[0][2])) if len(history) > 1 else 1e9

    def finish(self) -> dict:
        try:
            if self.ctl and self.game.process.poll() is None:
                try:
                    self.full_check()
                    self.ctl.screenshot(self.game.run_dir / "end.bmp")
                except (wdctl.CtlError, OSError) as error:  # the channel is gone
                    self.event("final check failed", error=str(error))
        finally:
            if self.game:
                text = self.game.stderr_text
                self.result["stderr_tail"] = text[-1500:]
                self.result["hook"] = hook_details(text)
                if "=== recomp: CRASH" in text and "crash" not in self.result["failures"]:
                    at = text.index("=== recomp: CRASH")
                    self.fail("crash", log=text[at : at + 800])
                try:
                    self.game.close()
                except OSError:
                    self.game.process.kill()
        self.result["seconds"] = round(time.monotonic() - self.t0, 1)
        return self.result


def hook_details(text: str) -> dict:
    """What phys_hook.c printed: the first invariant violation's triangles (flat
    on that axis or not) and the re-adds by caller, with their bits and nodes."""
    inv = [
        {
            "tri": m[1],
            "range": [int(m[2]), int(m[3])],
            "interval": [int(m[4]), int(m[5])],
            "flat": m[2] == m[3],
            "what": m[6],
        }
        for m in INV_LINE.finditer(text)
    ]
    readds: dict[str, dict] = {}
    for m in READD_LINE.finditer(text):
        key = f"ret {m[6]} axis {m[2]} bits {m[4]} -> {m[5]}"
        entry = readds.setdefault(key, {"count": 0, "triangles": set()})
        entry["count"] += 1
        entry["triangles"].add(m[3])
    for entry in readds.values():
        entry["triangles"] = sorted(entry["triangles"])[:8]
    sweep = re.search(r"\[inv\] first violation after sweep call #(\d+)", text)
    return {
        "invariant_sweep": int(sweep[1]) if sweep else None,
        "invariant": inv[:8],
        "readds": readds,
        "readd_total": sum(e["count"] for e in readds.values()),
    }


def run_project(slot: int, seconds: float, renderer: str = "direct") -> dict:
    project = Project(slot, seconds, renderer)
    try:
        if project.start():
            project.drive()
    except Exception as error:  # noqa: BLE001 - one project must not stop the sweep
        project.result["outcome"] = f"driver error: {error}"
        project.result["traceback"] = traceback.format_exc()[-1500:]
    result = project.finish()
    (OUT / f"slot-{slot:03d}.json").write_text(json.dumps(result, indent=1, default=list))
    collision = sorted(f for f in result["failures"] if f in COLLISION)
    print(
        f"[regress] slot {slot:3d} {result['name']:<11} {result['scene']:<13} "
        f"{result['outcome']}; {'FAIL ' + ', '.join(collision) if collision else 'collision ok'}; "
        f"camera far {result['camera']['far']} outside {result['camera']['outside']}",
        flush=True,
    )
    return result


def failed_result(bank: bank_patch.Bank, slot: int, outcome: str) -> dict:
    return {
        "slot": slot,
        "name": bank.parsed(slot).name,
        "scene": bank.parsed(slot).scene,
        "outcome": outcome,
        "failures": [],
        "checks": [],
        "stats": {"max_depth": None},
        "camera": {"far": 0, "outside": 0},
    }


def summarize(results: list[dict]) -> None:
    rows = [
        "| slot | project | scene | outcome | collision | hook | depth | cam far | cam out "
        "| stale |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    totals = {"projects": len(results), "ran": 0, "collision_fail": 0, "deep": 0, "camera_far": 0}
    for r in sorted(results, key=lambda r: r["slot"]):
        collision = sorted(f for f in r["failures"] if f in COLLISION)
        totals["ran"] += r["outcome"] in ("ran", "menu opened") or str(r["outcome"]).startswith(
            "level changed"
        )
        totals["collision_fail"] += bool(collision)
        totals["deep"] += "deep penetration" in r["failures"]
        totals["camera_far"] += r["camera"]["far"] > 0
        stale = max((c["stale"] for c in r["checks"]), default="-")
        hook = r.get("hook") or {}
        flat = sum(1 for i in hook.get("invariant", []) if i["flat"])
        hook_text = " ".join(
            x
            for x in (
                f"inv@{hook['invariant_sweep']} ({flat}/{len(hook['invariant'])} flat)"
                if hook.get("invariant_sweep")
                else "",
                f"re-add x{hook['readd_total']}" if hook.get("readd_total") else "",
            )
            if x
        )
        rows.append(
            f"| {r['slot']} | {r['name']} | {r['scene']} | {r['outcome']} | "
            f"{', '.join(collision) or 'ok'} | {hook_text or '-'} | {r['stats']['max_depth']} | "
            f"{r['camera']['far']} | {r['camera']['outside']} | {stale} |"
        )
    (OUT / "summary.json").write_text(
        json.dumps({"totals": totals, "projects": results}, indent=1, default=list)
    )
    (OUT / "summary.md").write_text(
        f"# Collision regression\n\n{json.dumps(totals)}\n\n" + "\n".join(rows) + "\n"
    )
    print(f"[regress] {json.dumps(totals)}\n[regress] {OUT / 'summary.md'}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--slots", default="0-149")
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--seconds", type=float, default=40)
    ap.add_argument("--redo", action="store_true", help="replace kept results")
    ap.add_argument(
        "--renderer",
        choices=("direct", "software"),
        default="direct",
        help="the renderer under test (direct: the GPU renderer, the product's)",
    )
    options = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    bank = bank_patch.Bank.from_disc(1)
    wanted = parse_slots(options.slots, len(bank.records))
    results, todo = [], []
    for slot in wanted:
        kept = OUT / f"slot-{slot:03d}.json"
        if kept.exists() and not options.redo:
            results.append(json.loads(kept.read_text()))
        elif not bank.parsed(slot).scene:
            results.append(failed_result(bank, slot, "no scene"))
        else:
            todo.append(slot)
    print(f"[regress] {len(todo)} projects to run, {len(results)} kept", flush=True)
    if options.workers > 1 and len(todo) > 1:
        with ProcessPoolExecutor(max_workers=options.workers) as pool:
            futures = {
                pool.submit(run_project, slot, options.seconds, options.renderer): slot
                for slot in todo
            }
            for future in as_completed(futures):
                slot = futures[future]
                try:
                    results.append(future.result())
                except Exception as error:  # noqa: BLE001 - one project must not end the sweep
                    print(f"[regress] slot {slot:3d} worker failed: {error}", flush=True)
                    results.append(failed_result(bank, slot, f"worker failed: {error}"))
    else:
        results += [run_project(slot, options.seconds, options.renderer) for slot in todo]
    summarize(results)
    return 1 if any(f in COLLISION for r in results for f in r["failures"]) else 0


if __name__ == "__main__":
    sys.exit(main())
