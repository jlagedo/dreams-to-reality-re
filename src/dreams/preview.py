"""Flat-shaded orthographic preview of a scene mesh, as a PNG.

Exists because the cheap numeric checks do not catch a wrong vertex mapping.
Degenerate-triangle rate, edge sanity and bounding-box size all pass on scenes
that render as debris - ``L14_PETI`` scores 0.8% slivers and a perfect edge
score while being a twisted ribbon. Looking at it settles the question in one
step, and does not need Blender.

Three axis-aligned views side by side, z-buffered, shaded by face normal.
"""

from __future__ import annotations

import math
from pathlib import Path

from dreams import png

VIEWS = ((0, 2, 1), (0, 1, 2), (2, 1, 0))  # (across, up, depth)
BACKGROUND = 0x18


def render(mesh, target: str | Path, size: int = 260) -> Path:
    """Write a three-view preview of ``mesh`` to ``target``."""
    lo, hi = mesh.bounds()
    centre = [(lo[i] + hi[i]) / 2 for i in range(3)]
    scale = max(hi[i] - lo[i] for i in range(3)) or 1

    width = size * len(VIEWS)
    img = bytearray(bytes([BACKGROUND]) * (width * size * 3))

    for slot, (ax, ay, az) in enumerate(VIEWS):
        depth = [1e30] * (size * size)
        for obj in mesh.objects:
            for face in obj.faces:
                p = [mesh.vertices[i] for i in face]
                u = [p[1][k] - p[0][k] for k in range(3)]
                v = [p[2][k] - p[0][k] for k in range(3)]
                n = (
                    u[1] * v[2] - u[2] * v[1],
                    u[2] * v[0] - u[0] * v[2],
                    u[0] * v[1] - u[1] * v[0],
                )
                length = math.sqrt(sum(x * x for x in n)) or 1
                shade = int(60 + 170 * abs(n[az] / length))

                pts = [
                    (
                        (q[ax] - centre[ax]) / scale * size * 0.8 + size / 2,
                        -(q[ay] - centre[ay]) / scale * size * 0.8 + size / 2,
                        q[az],
                    )
                    for q in p
                ]
                a, b, c = pts
                det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                if abs(det) < 1e-9:
                    continue
                x0 = max(int(min(q[0] for q in pts)), 0)
                x1 = min(int(max(q[0] for q in pts)) + 1, size)
                y0 = max(int(min(q[1] for q in pts)), 0)
                y1 = min(int(max(q[1] for q in pts)) + 1, size)
                for y in range(y0, y1):
                    for x in range(x0, x1):
                        w0 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / det
                        w1 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / det
                        w2 = 1 - w0 - w1
                        if w0 < -0.001 or w1 < -0.001 or w2 < -0.001:
                            continue
                        z = w0 * a[2] + w1 * b[2] + w2 * c[2]
                        k = y * size + x
                        if z < depth[k]:
                            depth[k] = z
                            at = (y * width + slot * size + x) * 3
                            img[at : at + 3] = bytes((shade, shade, shade))

    return png.write(target, width, size, bytes(img))
