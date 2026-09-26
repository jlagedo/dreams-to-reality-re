"""A level's collision mesh: ``.DSN`` tag 2, loaded by the engine as ``.3DI``.

The renderer never reads this record. ``WINDREAM.EXE`` loads it as resource
type 5 (the ``5`` at ``+0x08`` is the resource type the exporter's heap
held) and relocates it with ``MDL_RelocCollision`` (``0x455fb4``); the
collision library tests spheres against its triangles (see
``docs/engine.md``, *Collision and physics*). It is a subset of the rendered
faces plus a few invisible proxies: skies, video surfaces and water are left
out.

::

    +0x14  u32   point count V
    +0x18  u32   points (a stale pointer; relocates to +0x30)
    +0x1c  u32   triangle count F
    +0x20  u32   triangles
    +0x24  u32   normal count (== F)
    +0x28  u32   normals
    +0x30  i32[3] x V          points, scene units, Y down
           96 bytes x F        triangles
           i32[3] x F          unit normals, Q15

    triangle (``COLL_Triangle`` in re/structs/windream.h):
    +0x00  u32 x3   point pointers
    +0x0c  u32      normal pointer
    +0x10  i32      plane distance, n . v0 >> 15
    +0x14  i32[3]x3 edge normals, Q15, pointing into the triangle
    +0x38  i32 x3   edge distances; a point is inside edge k when
                    e_k . p >> 15 - c_k >= 0
    +0x44  u32      pointer back to the mesh header
    +0x48  i32[3]   bounding-box minimum
    +0x54  i32[3]   bounding-box maximum (the broadphase sorts on these)

Across all 95 scenes (152,536 triangles) the normals are unit length in
99.5%, the box is exact in 99.2%, the plane distance is within 2 units in
97.6%, and all three vertices lie on or inside all three edge planes in
94.5%. The misses cluster in a few scenes; ``F31CIEL`` (the sky) is lowest.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

from dreams.formats import lz, scene

TRIANGLE = 96
POINT = 12
HEADER = 0x14
POINTS_AT = 0x30


@dataclass(frozen=True)
class Triangle:
    points: tuple[int, int, int]  #: indices into :attr:`Collision.points`
    normal: tuple[int, int, int]  #: Q15
    plane: int  #: n . v0 >> 15
    edges: tuple[tuple[int, int, int], ...]  #: three inward Q15 edge normals
    edge_planes: tuple[int, int, int]
    box_min: tuple[int, int, int]
    box_max: tuple[int, int, int]

    def is_floor(self) -> bool:
        """The engine skips triangles with a zero normal Y when finding floors."""
        return self.normal[1] != 0


@dataclass
class Collision:
    points: list[tuple[int, int, int]]
    triangles: list[Triangle]


def parse(tag2: bytes) -> Collision:
    """Decode a decompressed tag 2 record."""
    count, points_ref, tri_count, tris_ref = struct.unpack_from("<4I", tag2, HEADER)
    delta = POINTS_AT - points_ref  # every pointer shares one relocation delta
    points = [struct.unpack_from("<3i", tag2, POINTS_AT + POINT * i) for i in range(count)]
    triangles = []
    for f in range(tri_count):
        at = tris_ref + delta + TRIANGLE * f
        w = struct.unpack_from("<24i", tag2, at)
        idx = []
        for ref in w[0:3]:
            off = (ref & 0xFFFFFFFF) + delta - POINTS_AT
            if off % POINT or not 0 <= off // POINT < count:
                raise ValueError(f"triangle {f}: point pointer {ref:#x} is off the array")
            idx.append(off // POINT)
        normal = struct.unpack_from("<3i", tag2, (w[3] & 0xFFFFFFFF) + delta)
        triangles.append(
            Triangle(
                points=tuple(idx),
                normal=normal,
                plane=w[4],
                edges=(tuple(w[5:8]), tuple(w[8:11]), tuple(w[11:14])),
                edge_planes=tuple(w[14:17]),
                box_min=tuple(w[18:21]),
                box_max=tuple(w[21:24]),
            )
        )
    return Collision(points, triangles)


def read_collision(path: str | Path) -> Collision:
    """Decode the collision mesh of a ``.DSN`` scene."""
    p = Path(path)
    try:
        payload = next(r.payload for r in scene.read_records(p) if r.tag == scene.TAG_PACKED)
    except StopIteration as exc:
        raise ValueError(f"{p.name}: no tag 2") from exc
    return parse(lz.decompress(payload))


def floor_height(tri: Triangle, points, x: int, z: int) -> int | None:
    """Y of the triangle's plane under ``(x, z)``, or None when outside it.

    The engine's floor probe (``0x45e458``) tests the point against the three
    edges in the XZ plane and needs a non-zero normal Y.
    """
    a, b, c = (points[i] for i in tri.points)
    s = [
        (x - q[0]) * -(p[2] - q[2]) + (z - q[2]) * (p[0] - q[0])
        for p, q in ((a, b), (b, c), (c, a))
    ]
    if not tri.is_floor() or (min(s) < 0 < max(s)):
        return None
    nx, ny, nz = tri.normal
    # n . (x, y, z) >> 15 == plane  ->  y = (plane * 32768 - nx*x - nz*z) / ny
    return round((tri.plane * 32768 - nx * x - nz * z) / ny)
