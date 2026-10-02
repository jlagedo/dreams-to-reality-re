"""Check the live collision world of a running game through the control channel.

Every rule here is read off the retail bytes of GDIDREAM.EXE (same addresses
in WINDREAM.EXE), not off the recomp:

- PHYS_AddCandidate (0x45D10C) and PHYS_RemoveCandidate (0x45D25C) keep, per
  collider, a binary tree of 24-byte overlap records at +0x2C: triangle +0,
  axis bits +4, right child +8 (larger triangle), left child +0xC, wall node
  +0x10, floor node +0x14. A record whose bits drop to 0 is freed.
- A wall node exists exactly when the collider has flag 4 and the bits are 7;
  a floor node exactly when it has flag 8 and bits & 5 == 5. Nodes are 12
  bytes (record +0, next +4, prev +8) on the lists at +0x30 and +0x34.
- The allocators 0x45CCC0 / 0x45CCE0 add 0x18 / 0xC to the byte counters at
  0x4AA9BC / 0x4AA9C0 and the frees subtract them, so the counters equal the
  live records and nodes of every collider.
- After a sweep a triangle has an axis bit exactly when its [min, max] on that
  axis strictly overlaps the collider's [c - r, c + r] (docs/specs/000, checked
  on two full retail dumps; exact ties may go either way). That holds right
  after the sweep: the host checks it there (WD_PHYS_INVARIANT=1, `[inv]`
  lines). A snapshot comes later, after collision response, an inactive
  entity or an open menu may have moved a centre without a sweep, so here a
  mismatch is reported as `stale` (count and distance from the interval's
  edge), not as a violation.
- The collision world (0x66E01C): triangle count +0, three endpoint arrays
  +4/+8/+0xC (8 bytes each: triangle, is-max byte), colliders +0x10, collider
  count +0x40C. Triangle bounds: min +0x48, max +0x54. Collider: centre +0
  (y grows downward), radius +0xC, flags +0x10, per-axis entry lo/hi +0x14.

Take a snapshot with the game paused (Ctl.pause) so it is consistent.

usage: python collision_check.py --run-dir out/recomp/windream/run-<tag> [--json]
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wdctl  # noqa: E402

WORLD = 0x66E01C
RECORD_BYTES, NODE_BYTES = 0x4AA9BC, 0x4AA9C0
FLAG_WALLS, FLAG_FLOORS = 4, 8
MAX_ITEMS = 200_000
CHUNK = 0x10000


def read_big(ctl, addr: int, size: int) -> bytes:
    return b"".join(ctl.read(addr + at, min(CHUNK, size - at)) for at in range(0, size, CHUNK))


def read_geometry(ctl) -> dict[int, tuple[tuple[int, int, int], ...]]:
    """Every world triangle's three vertices: a triangle's +0, +4 and +8 point
    at 12-byte vertices (int32 x, y, z, world space). Checked on the first map:
    the vertices reproduce the +0x48 / +0x54 bounds of all 1,959 triangles."""
    n, a0 = struct.unpack("<2I", ctl.read(WORLD, 8))
    raw = read_big(ctl, a0, 2 * n * 8)
    triangles = sorted({struct.unpack_from("<I", raw, i * 8)[0] for i in range(2 * n)})
    pointers = {
        tri: struct.unpack("<3I", data) for tri, data in _read_many(ctl, triangles, 12).items()
    }
    vertices = _read_many(ctl, sorted({p for ps in pointers.values() for p in ps}), 12)
    return {
        tri: tuple(struct.unpack("<3i", vertices[p]) for p in ps) for tri, ps in pointers.items()
    }


def _read_many(ctl, addresses: list[int], size: int) -> dict[int, bytes]:
    """`size` bytes at each address: one read of the span they lie in when
    that is small (the level's triangles and vertices are allocated together),
    one read per address otherwise."""
    if not addresses:
        return {}
    lo, hi = addresses[0], addresses[-1] + size
    if hi - lo <= 16 * CHUNK:
        span = read_big(ctl, lo, hi - lo)
        return {a: span[a - lo : a - lo + size] for a in addresses}
    return {a: ctl.read(a, size) for a in addresses}


@dataclass
class Collider:
    index: int
    addr: int
    centre: tuple[int, int, int]
    radius: int
    flags: int
    entries: tuple[int, ...] = ()  # per-axis lo, hi positions in the endpoint arrays
    records: dict[int, tuple] = field(default_factory=dict)  # tri -> (addr, bits, wall, floor)
    walls: list[tuple[int, int]] = field(default_factory=list)  # (node, record)
    floors: list[tuple[int, int]] = field(default_factory=list)


@dataclass
class World:
    triangles: dict[int, tuple[tuple[int, ...], tuple[int, ...]]]  # tri -> (min xyz, max xyz)
    endpoints: list[list[tuple[int, int]]]  # per axis: (tri, is_max)
    colliders: list[Collider]
    record_bytes: int
    node_bytes: int
    problems: list[str] = field(default_factory=list)

    def bounds(self):
        lo = [min(t[0][a] for t in self.triangles.values()) for a in range(3)]
        hi = [max(t[1][a] for t in self.triangles.values()) for a in range(3)]
        return lo, hi


def _list(ctl, head: int, problems: list[str], what: str) -> list[tuple[int, int]]:
    nodes, seen, prev, node = [], set(), 0, head
    while node:
        if node in seen or len(nodes) > MAX_ITEMS:
            problems.append(f"{what}: cycle at node {node:08X}")
            break
        seen.add(node)
        record, nxt, back = struct.unpack("<3I", ctl.read(node, 12))
        if back != prev:
            problems.append(f"{what}: node {node:08X} prev {back:08X}, expected {prev:08X}")
        nodes.append((node, record))
        prev, node = node, nxt
    return nodes


def snapshot(ctl, triangles=None) -> World:
    """Read the whole collision world. Pass the previous snapshot's triangles to
    skip re-reading them (they do not change within a level)."""
    problems: list[str] = []
    n, a0, a1, a2 = struct.unpack("<4I", ctl.read(WORLD, 16))
    endpoints = []
    for arr in (a0, a1, a2):
        raw = read_big(ctl, arr, 2 * n * 8)
        endpoints.append([struct.unpack_from("<IB", raw, i * 8) for i in range(2 * n)])
    if triangles is None or set(triangles) != {t for t, _ in endpoints[0]}:
        triangles = {}
        for tri in {t for t, _ in endpoints[0]}:
            v = struct.unpack("<6i", ctl.read(tri + 0x48, 24))
            triangles[tri] = (v[:3], v[3:])
    colliders = []
    count = ctl.read32(WORLD + 0x40C)
    pointers = struct.unpack(f"<{count}I", ctl.read(WORLD + 0x10, 4 * count)) if count else ()
    for index, addr in enumerate(pointers):
        if not addr:
            continue
        raw = ctl.read(addr, 0x38)
        cx, cy, cz, radius = struct.unpack_from("<3iI", raw, 0)
        root, wall_head, floor_head = struct.unpack_from("<3I", raw, 0x2C)
        col = Collider(
            index, addr, (cx, cy, cz), radius, raw[0x10], struct.unpack_from("<6I", raw, 0x14)
        )
        name = f"collider {index} ({addr:08X})"
        stack, seen = [(root, None, None)], set()
        while stack:
            rec, lo, hi = stack.pop()
            if not rec:
                continue
            if rec in seen or len(seen) > MAX_ITEMS:
                problems.append(f"{name}: tree revisits record {rec:08X}")
                continue
            seen.add(rec)
            tri, bits, right, left, wall, floor = struct.unpack("<IB3x4I", ctl.read(rec, 24))
            if (lo is not None and tri <= lo) or (hi is not None and tri >= hi):
                problems.append(f"{name}: record {rec:08X} key {tri:08X} out of tree order")
            if tri in col.records:
                problems.append(f"{name}: triangle {tri:08X} in two records")
            col.records[tri] = (rec, bits, wall, floor)
            stack += [(right, tri, hi), (left, lo, tri)]
        col.walls = _list(ctl, wall_head, problems, f"{name} wall list")
        col.floors = _list(ctl, floor_head, problems, f"{name} floor list")
        colliders.append(col)
    record_bytes, node_bytes = ctl.read32(RECORD_BYTES), ctl.read32(NODE_BYTES)
    return World(triangles, endpoints, colliders, record_bytes, node_bytes, problems)


def check(world: World) -> dict:
    """Every retail rule above, against one snapshot. Returns counts and the
    first examples of each violation (an empty `violations` is a pass)."""
    v: dict[str, list[str]] = {}

    def bad(kind: str, text: str) -> None:
        v.setdefault(kind, [])
        v[kind].append(text)

    for p in world.problems:
        bad("structure", p)
    tris = world.triangles
    for axis, arr in enumerate(world.endpoints):
        values = [tris[t][1 if m else 0][axis] for t, m in arr]
        out = sum(1 for i in range(1, len(values)) if values[i] < values[i - 1])
        if out:
            bad("endpoints", f"axis {axis}: {out} endpoints out of order")
        mins = sorted(t for t, m in arr if not m)
        maxs = sorted(t for t, m in arr if m)
        if mins != sorted(tris) or maxs != sorted(tris):
            bad("endpoints", f"axis {axis}: a triangle is not listed once as min and once as max")
    ties = 0
    stale: list[tuple[int, str]] = []
    unswept = []
    for col in world.colliders:
        name = f"collider {col.index}"
        if not col.records and not any(col.entries):
            unswept.append(col.index)  # never swept: no interval to check
            continue
        for tri, (rec, bits, wall, floor) in col.records.items():
            if tri not in tris:
                bad("record", f"{name}: record {rec:08X} names {tri:08X}, not a world triangle")
                continue
            if not 1 <= bits <= 7:
                bad("record", f"{name}: record {rec:08X} has axis bits {bits:#x}")
            want_wall = bool(col.flags & FLAG_WALLS) and bits & 7 == 7
            want_floor = bool(col.flags & FLAG_FLOORS) and bits & 5 == 5
            if bool(wall) != want_wall:
                bad("wall-node", f"{name}: tri {tri:08X} bits {bits} wall node {wall:08X}")
            if bool(floor) != want_floor:
                bad("floor-node", f"{name}: tri {tri:08X} bits {bits} floor node {floor:08X}")
        for kind, nodes, slot in (("wall", col.walls, 2), ("floor", col.floors, 3)):
            by_record = {r[0]: r for r in col.records.values()}
            linked = [n for n, r in nodes]
            if len(set(linked)) != len(linked):
                bad(f"{kind}-list", f"{name}: a node is linked twice")
            for node, record in nodes:
                owner = by_record.get(record)
                if owner is None:
                    bad(
                        f"{kind}-list",
                        f"{name}: node {node:08X} points at {record:08X}, no live record",
                    )
                elif owner[slot] != node:
                    bad(f"{kind}-list", f"{name}: node {node:08X} is not its record's {kind} node")
            expected = {r[slot] for r in col.records.values() if r[slot]}
            missing = expected - set(linked)
            if missing:
                bad(f"{kind}-list", f"{name}: {len(missing)} {kind} nodes not on the list")
        for axis in range(3):
            lo, hi = col.centre[axis] - col.radius, col.centre[axis] + col.radius
            for tri, (mn, mx) in tris.items():
                if mn[axis] == hi or mx[axis] == lo:
                    ties += 1
                    continue
                want = mn[axis] < hi and mx[axis] > lo
                rec = col.records.get(tri)
                has = bool(rec and rec[1] & (1 << axis))
                if want != has:
                    edge = min(abs(mn[axis] - hi), abs(mx[axis] - lo))
                    stale.append(
                        (
                            edge,
                            f"{name} axis {axis}: tri {tri:08X} [{mn[axis]}, {mx[axis]}] vs "
                            f"[{lo}, {hi}]: {'bit missing' if want else 'bit set, no overlap'}",
                        )
                    )
    records = sum(len(c.records) for c in world.colliders)
    nodes = sum(len(c.walls) + len(c.floors) for c in world.colliders)
    if world.record_bytes != 0x18 * records:
        bad("accounting", f"record bytes {world.record_bytes} for {records} live records")
    if world.node_bytes != 0xC * nodes:
        bad("accounting", f"node bytes {world.node_bytes} for {nodes} live nodes")
    return {
        "triangles": len(tris),
        "colliders": len(world.colliders),
        "records": records,
        "nodes": nodes,
        "ties": ties,
        "stale": {
            "count": len(stale),
            "max_edge_distance": max((e for e, _ in stale), default=0),
            "colliders": sorted({int(t.split()[1]) for _, t in stale}),
            "first": [t for _, t in sorted(stale, reverse=True)[:5]],
        },
        "unswept": unswept,
        "violations": {k: {"count": len(x), "first": x[:5]} for k, x in v.items()},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--run-dir", type=Path, required=True)
    ap.add_argument("--json", action="store_true")
    options = ap.parse_args()
    with wdctl.Ctl.connect(run_dir=options.run_dir) as ctl:
        ctl.pause()
        try:
            result = check(snapshot(ctl))
        finally:
            ctl.resume()
    if options.json:
        print(json.dumps(result, indent=2))
    else:
        print(
            f"{result['triangles']} triangles, {result['colliders']} colliders, "
            f"{result['records']} records, {result['nodes']} nodes, {result['ties']} ties, "
            f"unswept colliders {result['unswept']}; stale overlap bits "
            f"{result['stale']['count']} (up to {result['stale']['max_edge_distance']} off)"
        )
        for kind, item in result["violations"].items():
            print(f"FAIL {kind}: {item['count']}")
            for line in item["first"]:
                print(f"  {line}")
        if not result["violations"]:
            print("PASS")
    return 1 if result["violations"] else 0


if __name__ == "__main__":
    sys.exit(main())
