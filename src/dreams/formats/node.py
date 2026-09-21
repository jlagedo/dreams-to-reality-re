"""The engine's scene-graph node, as written into ``.DSN`` and ``.DAN`` tag 1.

Cryo's exporter ``fwrite``'d live C structs, so both formats carry the **same**
node, and solving one solved the other. A node is found by signature, never by
following an index::

    +0x24  u32       parent node address        0 at a root
    +0x28  u32       first child
    +0x2c  u32       next sibling
    +0x30  i32[3]    translation, RELATIVE to the parent
    +0x3c  i32[3][3] rotation, Q15 row-major (32768 = 1.0)
    +0x60  i32[3]    world translation   always 0 - never composed on disk
    +0x6c  i32[3][3] world rotation      a copy of +0x3c, for the same reason
    +0x90  u32       vertex count
    +0x94  u32       pointer to this node's own vertex array
    +0x9c  u32       == +0x94 + 40*count
    +0xc4  i32       bounding-sphere radius
    +0xc8  i32[3]    sphere centre, equal to the centroid of the vertices
    +0xd4  u32       40, the vertex record stride, stored literally
    +0xf0  ----      the vertex array begins at +0xf0 (position at +4 of each
                     40-byte record, so coordinates read at +0xf4 + 40*i)

The header is the 240 bytes **before** the vertex array, so a node's own
address is ``base - 240`` and the runtime node the engine passes around starts
0x14 further in, at ``base - 220``. That last number is what the child and
sibling pointers hold, which is why searching for ``base`` finds nothing.

Verified: ``+0xd4 == 40`` and the ``+0x9c`` identity hold for **2,248** nodes
across 95 scenes and **1,886** nodes across 159 models; the sphere centre
equals the centroid of the vertices read at ``+0xf4`` in every one.

The world transform composes parent-first, with a single arithmetic shift::

    R_world = (R_parent @ R_child) >> 15
    T_world = ((R_parent @ T_child) >> 15) + T_parent

Both the order and the ``sar 0xf`` rounding come from ``FUN_0045b86c`` and
``FUN_0047e498`` in ``WINDREAM.EXE``. Python's ``>>`` on ints floors, which is
what ``sar`` does, so the shift transcribes directly.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

STRIDE = 40  #: vertex record size, also stored at +0xd4
ARRAY = 0xF0  #: vertex array start, relative to the node header
POSITION = 4  #: position offset inside a vertex record
HEADER = 240  #: header bytes preceding the vertex array
NODE_BIAS = 220  #: child/sibling pointers hold ``base - 220``

PARENT, CHILD, SIBLING = 0x24, 0x28, 0x2C
TRANSLATION, ROTATION = 0x30, 0x3C
COUNT, BASE, END = 0x90, 0x94, 0x9C
RADIUS, CENTRE, STRIDE_AT = 0xC4, 0xC8, 0xD4

FACE_RECORD = 68
FACE_COUNT_AT = 16  #: within a face block
FACE_STRIDE_AT = 32  #: holds 68
FACE_DATA = 44
VERTEX_WORDS = (1, 4, 7)
UV_WORDS = (12, 13, 14)
UV_OFFSET = -5  #: a UV reference points 5 bytes into its 8-byte record

#: A UV is stored as ``pixel * 255`` in 16.16, i.e. divide by this for pixels.
UV_SCALE = 65536 * 255


@dataclass
class Node:
    """One scene-graph node with geometry."""

    offset: int  #: byte offset of the header within the decompressed record
    base: int  #: address of its vertex array, the value at +0x94
    count: int
    translation: tuple[int, int, int]
    rotation: tuple[tuple[int, int, int], ...]
    radius: int
    centre: tuple[int, int, int]
    parent: int  #: parent node address, 0 at a root

    @property
    def address(self) -> int:
        """This node's own address, as child/sibling pointers express it."""
        return self.base - NODE_BIAS


def find_nodes(buf: bytes) -> list[Node]:
    """Every geometry node in a decompressed tag-1 record.

    Located by signature rather than by a directory: ``u32 40`` at ``+0xd4``
    plus the ``+0x9c == +0x94 + 40*count`` identity. Two independent
    conditions make false positives negligible, and the bounding sphere at
    ``+0xc4`` agrees with the decoded vertices in every node found.
    """
    out: list[Node] = []
    n = len(buf)
    for off in range(0, max(n - 0x100, 0), 4):
        if struct.unpack_from("<I", buf, off + STRIDE_AT)[0] != STRIDE:
            continue
        count, base = struct.unpack_from("<II", buf, off + COUNT)
        if count == 0 or count > 100_000:
            continue
        if struct.unpack_from("<I", buf, off + END)[0] != base + STRIDE * count:
            continue
        if off + ARRAY + STRIDE * count > n:
            continue
        out.append(
            Node(
                offset=off,
                base=base,
                count=count,
                translation=struct.unpack_from("<3i", buf, off + TRANSLATION),
                rotation=tuple(
                    struct.unpack_from("<3i", buf, off + ROTATION + 12 * r) for r in range(3)
                ),
                radius=struct.unpack_from("<i", buf, off + RADIUS)[0],
                centre=struct.unpack_from("<3i", buf, off + CENTRE),
                parent=struct.unpack_from("<I", buf, off + PARENT)[0],
            )
        )
    return out


def address_delta(nodes: list[Node]) -> int:
    """Add this to an address to get a byte offset in the same record.

    Every pointer in these files is a **stale address** from the exporter's
    heap. The whole record shifts by one delta, so any node recovers it:
    its vertex array lives at ``offset + 0xf0`` and is addressed as ``base``.
    """
    return min(nd.offset + ARRAY - nd.base for nd in nodes)


def _mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(3)) >> 15 for c in range(3)] for r in range(3)]


def _apply(m, v):
    return [sum(m[r][k] * v[k] for k in range(3)) >> 15 for r in range(3)]


def world_transforms(nodes: list[Node]) -> dict[int, tuple[list, list]]:
    """Compose every node's world transform by walking parent links.

    Parent pointers are used rather than first-child/next-sibling: both are
    present, but the forward links are sparse in these files while the parent
    link resolves for every non-root node. Across 159 models this yields at
    most **2 roots per model** - two because most ``.DAN`` files carry two
    copies of the model.
    """
    by_address = {nd.address: nd for nd in nodes}
    world: dict[int, tuple[list, list]] = {}

    def solve(nd: Node, depth: int = 0) -> tuple[list, list]:
        if nd.base in world:
            return world[nd.base]
        local = ([list(r) for r in nd.rotation], list(nd.translation))
        parent = by_address.get(nd.parent)
        if parent is None or parent.base == nd.base or depth > 64:
            world[nd.base] = local
        else:
            pr, pt = solve(parent, depth + 1)
            world[nd.base] = (
                _mul(pr, local[0]),
                [a + b for a, b in zip(_apply(pr, local[1]), pt, strict=True)],
            )
        return world[nd.base]

    for nd in nodes:
        solve(nd)
    return world


def world_vertices(buf: bytes, nodes: list[Node]) -> dict[int, list[tuple[int, int, int]]]:
    """Each node's vertices, transformed into world space, keyed by ``base``."""
    world = world_transforms(nodes)
    out: dict[int, list[tuple[int, int, int]]] = {}
    for nd in nodes:
        rot, tr = world[nd.base]
        pts = []
        for i in range(nd.count):
            at = nd.offset + ARRAY + POSITION + STRIDE * i
            v = struct.unpack_from("<3i", buf, at)
            pts.append(tuple(a + b for a, b in zip(_apply(rot, v), tr, strict=True)))
        out[nd.base] = pts
    return out


@dataclass
class Face:
    corners: tuple[tuple[int, int, int], ...]
    uvs: tuple[tuple[float, float], ...]
    bridge: bool = False  #: spans more than one node


@dataclass
class Model:
    path: Path
    nodes: list[Node]
    faces: list[Face] = field(default_factory=list)
    #: Nodes that carry no faces. 129 of 131 across all models are exactly
    #: 8-vertex boxes - collision proxies - and 2 are single points.
    empty: list[Node] = field(default_factory=list)

    @property
    def bridge_count(self) -> int:
        return sum(1 for f in self.faces if f.bridge)


def read_faces(buf: bytes, nodes: list[Node]) -> tuple[list[Face], set[int]]:
    """Every face in a tag-1 record, resolved against ``nodes``.

    Two rules here are easy to get wrong and both cost visible geometry:

    **A face may span two nodes.** Its three references are resolved
    *independently*, each in its owning node's world space. These bridging
    triangles are the skin over a joint and always join a parent-child pair;
    requiring one owner for all three corners drops them and leaves the model
    in disconnected pieces. ``.DSN`` levels are the opposite case - all
    71,689 faces there sit in a single node.

    **The last block may overrun.** In 86 of 159 models the final face block
    declares four bytes more than the record holds. Only the unused tail is
    missing, so bounds are checked per record, over the 32 bytes a record
    needs for its three vertex references.
    """
    if not nodes:
        return [], set()
    delta = address_delta(nodes)
    verts = world_vertices(buf, nodes)
    spans = sorted((nd.base, nd.base + STRIDE * nd.count) for nd in nodes)

    def owner(ref: int) -> int | None:
        for lo, hi in spans:
            if lo <= ref < hi:
                return lo
        return None

    faces: list[Face] = []
    used: set[int] = set()
    seen: set[int] = set()
    for off in range(0, max(len(buf) - 48, 0), 4):
        if off + FACE_STRIDE_AT + 4 > len(buf):
            break
        if struct.unpack_from("<I", buf, off + FACE_STRIDE_AT)[0] != FACE_RECORD:
            continue
        n = struct.unpack_from("<I", buf, off + FACE_COUNT_AT)[0]
        if not 0 < n < 20_000:
            continue
        if off + FACE_DATA + FACE_RECORD * (n - 1) + 32 > len(buf):
            continue
        for f in range(n):
            at = off + FACE_DATA + FACE_RECORD * f
            if at + 32 > len(buf) or at in seen:
                continue
            row = struct.unpack_from("<15I", buf, at) if at + 60 <= len(buf) else None
            refs = (
                row[1 : 8 : 3] if row else struct.unpack_from("<8I", buf, at)[1:8:3]
            )
            corners, owners, ok = [], set(), True
            for ref in refs:
                b = owner(ref)
                if b is None:
                    ok = False
                    break
                slot = (ref - b) // STRIDE
                if not 0 <= slot < len(verts[b]):
                    ok = False
                    break
                owners.add(b)
                corners.append(verts[b][slot])
            if not ok:
                continue
            uvs = []
            for k in range(3):
                a = (row[UV_WORDS[k]] + UV_OFFSET + delta) if row else -1
                if 0 <= a and a + 8 <= len(buf):
                    u, v = struct.unpack_from("<2i", buf, a)
                    uvs.append((max(u, 0) / UV_SCALE, max(v, 0) / UV_SCALE))
                else:
                    uvs.append((0.0, 0.0))
            seen.add(at)
            used |= owners
            faces.append(Face(tuple(corners), tuple(uvs), bridge=len(owners) > 1))
    return faces, used


# ------------------------------------------------------------- .DAN models --

#: ``.DAN`` tag 2 is a fixed-size texture bank in 191/191 models.
TEX_HEADER = 0x14
TEX_PALETTES = 0x8000  #: 32 palettes x 256 entries x 4 bytes
TEX_PAGE = 0x10000  #: one 256x256 indexed page
TEX_TOTAL = TEX_HEADER + TEX_PALETTES + TEX_PAGE  # == 98,324
TEX_SIZE = 256


def read_model(path: str | Path) -> Model:
    """Decode a ``.DAN`` character or prop model.

    ``.DAN`` was catalogued for years as "animation, payload meanings open".
    Tag 1 is in fact the **model**, built from the same node as ``.DSN`` tag 1,
    so the level decode solved it for free. 159/159 models on the discs decode
    to 1,886 nodes and 41,614 triangles.
    """
    from dreams.formats import lz, scene

    p = Path(path)
    records = scene.read_records(p, "dan")
    try:
        buf = lz.decompress(next(r.payload for r in records if r.tag == 1))
    except StopIteration as exc:  # pragma: no cover - every sampled file has one
        raise ValueError(f"{p.name}: no tag 1") from exc
    nodes = find_nodes(buf)
    faces, used = read_faces(buf, nodes)
    return Model(p, nodes, faces, [nd for nd in nodes if nd.base not in used])


def read_3dc(path: str | Path) -> Model:
    """Decode a ``.3DC`` prop or weapon.

    ``F3DC`` is **not** a tagged record chain and is **not** LZ-packed - it is a
    raw blob holding the same node as everything else, so it decodes by
    signature straight off the file bytes. 165 nodes across the 16 unique
    ``.3DC`` files on the discs.

    This closes a long-standing defect: ``BOULE`` reaches **0 boundary edges**
    (it was 18), as do ``EPEE`` and ``GUN``. ``CARRE`` comes out as 4 vertices
    and 2 triangles - *carre* is French for **square**, so a quad is correct and
    the old expectation of a box was the error. ``ARC`` still has 15 boundary
    edges and is unresolved.
    """
    p = Path(path)
    buf = p.read_bytes()
    nodes = find_nodes(buf)
    faces, used = read_faces(buf, nodes)
    return Model(p, nodes, faces, [nd for nd in nodes if nd.base not in used])


def texture_page(path: str | Path) -> tuple[list[tuple[int, int, int]], bytes] | None:
    """A model's ``(palette, page)`` from ``.DAN`` tag 2, or ``None``.

    The palette entry is a ``u16`` at ``+2`` of each 4-byte slot, **RGB565** -
    the same packing as level textures and the save thumbnail. Of 191 models
    there are 126 distinct pages, shared exactly where you would expect: 13
    ``MCHAPO`` files share one, 10 ``CAISSE`` files another.
    """
    from dreams.formats import lz, scene

    for rec in scene.read_records(Path(path), "dan"):
        if rec.tag != 2:
            continue
        try:
            buf = lz.decompress(rec.payload)
        except Exception:  # noqa: BLE001 - a short record is simply not the bank
            continue
        if len(buf) < TEX_TOTAL:
            continue
        palette = []
        for i in range(256):
            v = struct.unpack_from("<H", buf, TEX_HEADER + 4 * i + 2)[0]
            palette.append(
                (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
            )
        start = TEX_HEADER + TEX_PALETTES
        return palette, buf[start : start + TEX_PAGE]
    return None
