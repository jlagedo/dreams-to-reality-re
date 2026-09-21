"""Scene geometry from ``.DSN`` record tags 1 and 2.

Tag 2 is the vertex pool; tag 1 holds the material table and, per object, a run
of 68-byte face records that reference it. Both are LZ-packed - see
:mod:`dreams.formats.lz`.

::

    tag 2   +0x14  u32        vertex count
            +0x30  i32[3] x N  x, y, z in signed integer scene units

    tag 1   +0x18  u32        1200
            +0x1c  u32        material count (objects + 1 for DEFAULT)
            +0x20  44 bytes x count
                     char[16] MATERIAL name
                     char[16] texture name, the same word lowercased
                     u32 x3   DEFAULT carries 0x3DEF3DEF; textured
                              entries carry 1.0f at +36

            object block, located by its name:
              name-8   u32       3
              name+0   char[16]  object name
              name+16  u32       face-record count
              name+32  u32       68, the record size
              name+44  68-byte face records

    face record, 17 little-endian words:
              +4,+16,+28   vertex references   (== 1 mod 20)
              +8,+20,+32   parallel references (== 13 mod 16)
              +12,+24,+36  references          (== 77 mod 100), role open
              +48,+52,+56  UV references       (== 5 mod 8)

A reference is a **stale pointer**, not an index. Sorting the scene's distinct
vertex references and taking their rank gives the tag 2 vertex index - the
exporter never dereferences them. Reading a UV reference at ``p - 1`` yields two
16.16 values that are pixel coordinates in a 256x256 atlas.

Verified on ``E01GROTT``: 193 vertices, 338 faces, 1,014 corners, every index in
range, floors exactly planar, and the compass walls landing on opposite sides of
the room. **[unverified]** beyond the scenes reported by ``dreams mesh``.
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from functools import reduce
from math import gcd
from pathlib import Path

from dreams.formats import lz, scene

FACE_RECORD = 68
VERTEX_STRIDE = 12
MATERIAL_RECORD = 44
ATLAS = 256  # UVs are pixel coordinates in a 256x256 atlas

#: Word offsets within a face record.
VERTEX_REFS = (1, 4, 7)
UV_REFS = (12, 13, 14)
#: A UV reference points 5 bytes past the start of its 8-byte record.
UV_OFFSET = -5


@dataclass
class Material:
    name: str
    texture: str
    colour: int  # RGB555 pair for DEFAULT, else 0


@dataclass
class Object:
    name: str
    material: str
    faces: list[tuple[int, int, int]] = field(default_factory=list)
    uvs: list[tuple[float, float]] = field(default_factory=list)  # 3 per face


@dataclass
class Mesh:
    path: Path
    vertices: list[tuple[int, int, int]]
    materials: list[Material]
    objects: list[Object]
    #: Diagnostics for :attr:`mapping_is_clean`.
    ref_count: int = 0
    ref_stride: int = 0
    ref_slots: int = 0
    ref_base: int = 0
    declared: int = 0

    @property
    def edge_sanity(self) -> float:
        """Fraction of triangle edges that are short relative to their object.

        The direct test of whether the vertex mapping is right. A scrambled
        mapping makes triangles join distant points, so edges stretch across the
        whole object and the score collapses; correct geometry keeps every edge
        well inside its own bounding box.

        **Necessary but not sufficient, so it is NOT the export gate.** 70 of 95
        scenes score above 0.98, but a mapping can be wrong and still local:
        ``E02ARAI0`` scores a perfect 1.0 with **0 of 5** floors planar. Across
        scenes with at least three ``SOL`` objects, the contiguity-clean scenes
        have 8/14 floors planar against 3/176 for the rest - so the others are
        genuinely wrong, not merely unproven. Use :attr:`mapping_is_clean`.
        """
        bad = total = 0
        for obj in self.objects:
            used = {i for f in obj.faces for i in f}
            if len(used) < 3:
                continue
            pts = [self.vertices[i] for i in used]
            lo = tuple(min(q[a] for q in pts) for a in range(3))
            hi = tuple(max(q[a] for q in pts) for a in range(3))
            diag = math.dist(lo, hi)
            if diag == 0:
                continue
            for f in obj.faces:
                for a, b in ((0, 1), (1, 2), (2, 0)):
                    total += 1
                    if math.dist(self.vertices[f[a]], self.vertices[f[b]]) > diag * 0.95:
                        bad += 1
        return 1 - bad / total if total else 0.0

    @property
    def looks_sane(self) -> bool:
        """Edge sanity above 0.98. A weak screen, not proof - see above."""
        return self.edge_sanity > 0.98

    @property
    def mapping_is_clean(self) -> bool:
        """True when the reference-to-vertex mapping is provably right.

        References are stale pointers into 40-byte slots. Three conditions
        together make rank the index: the references form **one contiguous run
        of stride 40**, it **starts at 221**, and its length **matches the u32
        at tag 2 + 0x14**. That holds in **4 of 95 scenes** - E01GROTT,
        L03_REQI, L16_BOMB and O01EAU01.

        All three conditions are needed. Dropping the count match admits four
        more scenes whose references are still one clean run - E19_GARD,
        F31CIEL, L14_PETI, L15_EAU - and every one of them renders as debris:
        ``L14_PETI`` is a twisted ribbon from all three axes and ``E19_GARD`` a
        spray of spikes, against a closed room for E01GROTT and a submarine
        hull for L03_REQI. Degenerate-triangle rate does not catch it (all four
        sit under 6%), and neither does :attr:`edge_sanity`. Rendering does.

        When the run is shorter than the pool, some pool entries go unreferenced
        and rank silently slips past them.

        The other 87 split across several runs with unrelated bases - E10_PIEC
        has nine, starting 221, 22,281, 24,225 and on. Ordering those runs by
        pointer value or by first use both leave floor planarity at 8/39, no
        better than doing nothing, so run order is not the missing piece.
        """
        return (
            self.ref_stride == 40
            and self.ref_slots == self.ref_count
            and self.ref_base == 221
            and self.declared == self.ref_count
        )

    @property
    def face_count(self) -> int:
        return sum(len(o.faces) for o in self.objects)

    def bounds(self) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
        lo = tuple(min(v[i] for v in self.vertices) for i in range(3))
        hi = tuple(max(v[i] for v in self.vertices) for i in range(3))
        return lo, hi


def _cstr(b: bytes) -> str:
    return b.split(b"\0")[0].decode("latin-1")


def _blocks(tag1: bytes, names: list[str]) -> dict[str, int]:
    """Locate each object block by its name, confirmed by the 68 at +32."""
    found: dict[str, int] = {}
    for name in names:
        if not name:
            continue
        needle = name.encode()
        start = 0
        while True:
            at = tag1.find(needle, start)
            if at < 0:
                break
            start = at + 1
            if at + 48 > len(tag1):
                continue
            if struct.unpack_from("<I", tag1, at + 32)[0] == FACE_RECORD:
                found[name] = at
                break
    return found


def read_mesh(path: str | Path) -> Mesh:
    """Decode one scene's geometry. Raises ``ValueError`` if the tags are absent."""
    p = Path(path)
    sc = scene.read_dsn(p)
    recs = scene.read_records(p)

    try:
        tag1 = lz.decompress(next(r.payload for r in recs if r.tag == scene.TAG_GEOMETRY))
        tag2 = lz.decompress(next(r.payload for r in recs if r.tag == scene.TAG_PACKED))
    except StopIteration as exc:
        raise ValueError(f"{p.name}: missing tag 1 or tag 2") from exc

    n_mat = struct.unpack_from("<I", tag1, 0x1C)[0]
    materials = []
    for i in range(n_mat):
        off = 0x20 + MATERIAL_RECORD * i
        if off + MATERIAL_RECORD > len(tag1):
            break
        materials.append(
            Material(
                _cstr(tag1[off : off + 16]),
                _cstr(tag1[off + 16 : off + 32]),
                struct.unpack_from("<I", tag1, off + 32)[0],
            )
        )

    blocks = _blocks(tag1, sc.names)

    # Pass one: collect every vertex reference so rank can stand in for index.
    raw: dict[str, list[tuple[int, ...]]] = {}
    refs: set[int] = set()
    for name, at in blocks.items():
        n_faces = struct.unpack_from("<I", tag1, at + 16)[0]
        base = at + 44
        if n_faces <= 0 or base + FACE_RECORD * n_faces > len(tag1):
            continue
        rows = [
            struct.unpack_from("<17I", tag1, base + FACE_RECORD * f) for f in range(n_faces)
        ]
        raw[name] = rows
        refs.update(r[w] for r in rows for w in VERTEX_REFS)

    # Resolve references through tag 1's own arena directory rather than by
    # rank. An arena's local slot is (ref - base) // 40, and arenas are laid
    # into the pool in directory order. Rank only coincides with the slot when
    # every slot is referenced, which is why it missed E98ARAI1.
    arenas = read_arenas(tag1, blocks)
    rank: dict[int, int] = {}
    if arenas:
        cumulative, run = {}, 0
        for abase, acount in arenas:
            cumulative[abase] = run
            run += acount
        bases = sorted(cumulative)
        for v in refs:
            owner = None
            for b in bases:
                if b <= v:
                    owner = b
                else:
                    break
            if owner is not None:
                rank[v] = cumulative[owner] + (v - owner) // 40
    # The arena mapping indexes the pool directly, so size the vertex array by
    # the largest index it produces rather than by the number of references.
    # Fall back to rank if the arenas are missing or point past the pool.
    capacity = max((len(tag2) - 0x30) // VERTEX_STRIDE, 0)
    if not rank or max(rank.values(), default=0) >= capacity:
        rank = {v: i for i, v in enumerate(sorted(refs))}
    top = max(rank.values(), default=-1) + 1
    if top > capacity:
        raise ValueError(
            f"{p.name}: needs {top} vertices, tag 2 holds at most {capacity}"
        )
    vertices = [
        struct.unpack_from("<3i", tag2, 0x30 + VERTEX_STRIDE * i) for i in range(top)
    ]

    objects = []
    for name in sc.names:
        if name not in raw:
            continue
        mat = next((m.name for m in materials if m.name == name), materials[0].name)
        obj = Object(name, mat)
        for row in raw[name]:
            obj.faces.append(tuple(rank[row[w]] for w in VERTEX_REFS))
            for w in UV_REFS:
                # UV references are congruent to 5 mod 8, so the 8-byte record
                # starts at p - 5. Reading at p - 1 - the rule that works for
                # the other reference classes - lands mid-record and renders as
                # diagonal streaking. Verified by rasterising a wall both ways.
                at = row[w] + UV_OFFSET
                if 0 <= at and at + 8 <= len(tag1):
                    u, v = struct.unpack_from("<2i", tag1, at)
                    # 0xFFFFFFFF stands in for zero at a texture edge.
                    obj.uvs.append((max(u, 0) / 65536 / ATLAS, max(v, 0) / 65536 / ATLAS))
                else:
                    obj.uvs.append((0.0, 0.0))
        objects.append(obj)

    sorted_refs = sorted(rank)
    stride = 0
    if len(sorted_refs) > 1:
        stride = reduce(gcd, [b - a for a, b in zip(sorted_refs, sorted_refs[1:], strict=False)])
    slots = (sorted_refs[-1] - sorted_refs[0]) // stride + 1 if stride else len(sorted_refs)
    return Mesh(
        p, vertices, materials, objects,
        ref_count=len(rank), ref_stride=stride, ref_slots=slots,
        ref_base=sorted_refs[0] if sorted_refs else 0,
        declared=struct.unpack_from("<I", tag2, 0x14)[0],
    )


# ------------------------------------------------- tag 2's own triangle mesh --

TRI_RECORD = 96
TAG2_STRIDE = 12


def read_tri_mesh(path: str | Path) -> Mesh:
    """Build a scene mesh from **tag 2 alone**, ignoring tag 1's references.

    Tag 2 is not just a bag of points: it carries its own triangle array, each
    record holding three vertex references, a normal reference, edge half-space
    normals and plane constants, and an axis-aligned bounding box. That shape
    says collision or spatial acceleration rather than rendering - but the point
    pool is the scene's geometry either way, and the triangles index it directly.

    ::

        +0x14  u32        V, point count
        +0x18  u32        29, the base vertex references are measured from
        +0x1c  u32        F, triangle count
        +0x20  u32        29 + 12*V, the triangle array (relative)
        +0x30  i32[3] x V points
        +0x30 + 12*V      F records of 96 bytes:
                 +0x00 u32 x3   vertex refs, each 29 + 12*index

    The decisive advantage over :func:`read_mesh`: these references resolve
    arithmetically, so **all 95 scenes decode** rather than the 4 whose tag 1
    references happen to form one clean run. ``E10_PIEC`` renders as a clean
    rectangular chamber here and as debris through tag 1.

    The cost is that tag 2 has no object names, materials or UVs. Use
    :func:`read_mesh` when :attr:`Mesh.mapping_is_clean`, this otherwise.
    """
    p = Path(path)
    recs = scene.read_records(p)
    try:
        tag2 = lz.decompress(next(r.payload for r in recs if r.tag == scene.TAG_PACKED))
    except StopIteration as exc:
        raise ValueError(f"{p.name}: no tag 2") from exc

    count, base, faces_n = (struct.unpack_from("<I", tag2, o)[0] for o in (0x14, 0x18, 0x1C))
    start = 0x30 + TAG2_STRIDE * count
    if faces_n == 0 or start + TRI_RECORD * faces_n > len(tag2):
        raise ValueError(f"{p.name}: triangle array overruns tag 2")

    vertices = [
        struct.unpack_from("<3i", tag2, 0x30 + TAG2_STRIDE * i) for i in range(count)
    ]
    obj = Object(p.stem.lower(), "default")
    for f in range(faces_n):
        refs = struct.unpack_from("<3I", tag2, start + TRI_RECORD * f)
        idx = []
        for r in refs:
            if (r - base) % TAG2_STRIDE:
                raise ValueError(f"{p.name}: reference {r} is not on a vertex")
            i = (r - base) // TAG2_STRIDE
            if not 0 <= i < count:
                raise ValueError(f"{p.name}: vertex index {i} out of range")
            idx.append(i)
        obj.faces.append(tuple(idx))
        obj.uvs.extend([(0.0, 0.0)] * 3)

    return Mesh(p, vertices, [Material(p.stem.lower(), "", 0)], [obj], ref_count=count)


# ------------------------------------------------------- the arena directory --

ARENA_COUNT_AT = 0x90
ARENA_BASE_AT = 0x94
ARENA_END_AT = 0x9C


def read_arenas(tag1: bytes, blocks: dict[str, int]) -> list[tuple[int, int]]:
    """The scene's vertex arenas as ``(base, count)`` pairs, from tag 1's own directory.

    Vertex references are stale pointers, but tag 1 records where each source
    array began. The directory is reached through a bias derived from any object
    block, since the block stores its own address alongside its own offset::

        D       = name_offset - u32(name_offset + 20)
        bias    = D - 200
        n       = u32(tag1 + 0x14)                arena count
        O[j]    = u32(tag1 + 0x18 + 4*j)          descriptor offsets
        base[j] = O[j] - bias                     base in reference space

        at tag1 + O[j]:
            +0x90  u32  count
            +0x94  u32  base[j]                   confirms the bias
            +0x9c  u32  base[j] + 40*count        confirms the extent

    Both identities hold in **95/95** scenes, and every vertex reference falls
    inside some arena in 95/95. This supersedes guessing arena boundaries from
    gaps in the reference values.

    It classifies a reference but does not resolve it. ``ref`` belongs to the
    arena with the greatest ``base <= ref``, at local slot ``(ref - base) // 40``
    - and for 90 scenes that slot is **not** the tag 2 index. Arena counts sum
    to the pool size in only 15 scenes, and even there, searching a single index
    offset per arena matches 26-57% of faces. The remaining step is a
    shared-vertex merge that is not affine.
    """
    if not blocks:
        return []
    name_off = min(blocks.values())
    if name_off + 24 > len(tag1):
        return []
    bias = (name_off - struct.unpack_from("<I", tag1, name_off + 20)[0]) - 200
    n = struct.unpack_from("<I", tag1, 0x14)[0]
    if n == 0 or 0x18 + 4 * n > len(tag1):
        return []

    out = []
    for j in range(n):
        off = struct.unpack_from("<I", tag1, 0x18 + 4 * j)[0]
        if off + ARENA_END_AT + 4 > len(tag1):
            return []
        count = struct.unpack_from("<I", tag1, off + ARENA_COUNT_AT)[0]
        base = struct.unpack_from("<I", tag1, off + ARENA_BASE_AT)[0]
        end = struct.unpack_from("<I", tag1, off + ARENA_END_AT)[0]
        if base != off - bias or end != base + 40 * count:
            return []
        out.append((base, count))
    return out


def verify_against_tag2(path: str | Path) -> tuple[int, int]:
    """Score a tag 1 decode against tag 2's triangles. Returns ``(matched, total)``.

    Tag 2's triangle array is known-correct geometry, so every face that
    :func:`read_mesh` produces should appear in it. A full match is proof the
    reference mapping is right - far stronger than planarity or edge heuristics,
    and it needs no judgement.
    """
    m = read_mesh(path)
    t2 = lz.decompress(
        next(r.payload for r in scene.read_records(path) if r.tag == scene.TAG_PACKED)
    )
    count, base, faces_n = (struct.unpack_from("<I", t2, o)[0] for o in (0x14, 0x18, 0x1C))
    start = 0x30 + TAG2_STRIDE * count
    if start + TRI_RECORD * faces_n > len(t2):
        return 0, 0
    truth = {
        frozenset(
            (x - base) // TAG2_STRIDE
            for x in struct.unpack_from("<3I", t2, start + TRI_RECORD * f)
        )
        for f in range(faces_n)
    }
    total = matched = 0
    for obj in m.objects:
        for f in obj.faces:
            total += 1
            if frozenset(f) in truth:
                matched += 1
    return matched, total


# ------------------------------------------------- the scene-graph decode --


def read_node_mesh(path: str | Path) -> Mesh:
    """Decode a ``.DSN`` through the scene-graph node, as models are decoded.

    ``.DSN`` tag 1 and ``.DAN`` tag 1 hold the same struct, so the decoder in
    :mod:`dreams.formats.node` reads both. What it adds over tag 2 is what tag
    2 throws away: **per-object names and UVs**. Tag 2 is one nameless merged
    triangle array, which is why a level exported from it draws as a blank
    hull - on ``H18ANGKR`` you see a smooth sphere instead of a grass plateau
    with a temple and roots on it.

    Geometry agrees with tag 2: the face count is identical in 57 of 95 scenes
    and the bounds too. See :func:`verify_nodes` for why the older exact-match
    score said otherwise.
    """
    from dreams.formats import node as _node

    p = Path(path)
    tag1 = lz.decompress(next(r.payload for r in scene.read_records(p) if r.tag == 1))
    nodes = _node.find_nodes(tag1)
    faces, _ = _node.read_faces(tag1, nodes)

    index: dict[tuple[int, int, int], int] = {}
    vertices: list[tuple[int, int, int]] = []
    objects: dict[str, Object] = {}
    for f in faces:
        obj = objects.get(f.group)
        if obj is None:
            obj = objects[f.group] = Object(f.group, f.group)
        tri = []
        for corner in f.corners:
            at = index.get(corner)
            if at is None:
                at = index[corner] = len(vertices)
                vertices.append(corner)
            tri.append(at)
        obj.faces.append(tuple(tri))
        obj.uvs += list(f.uvs)
    return Mesh(
        path=p,
        vertices=vertices,
        materials=[Material(n, n, 0) for n in objects],
        objects=list(objects.values()),
    )


#: World positions are composed with an integer ``>> 15`` at every level, so a
#: node deep in the tree can land a unit or two from where the engine put it.
NODE_TOLERANCE = 2


def verify_nodes(path: str | Path, tol: int = NODE_TOLERANCE) -> float:
    """Fraction of node-composed positions that tag 2 also has, within ``tol``.

    **The tolerance is the whole point.** Scored on exact integer equality this
    reaches 99% in only 8 of 95 scenes, median 11.4%, and that number is what
    made level geometry look unsolved. At ``tol=2`` - one part in 50,000 of a
    scene that spans ~100,000 units - it is **58 of 95 at 99%, median 100%**.

    The reading is not "loosen until it passes": widening to 8 or to 32 moves
    nothing. A sharp step at 2 followed by a flat line is the signature of a
    fixed rounding offset, which is exactly what composing Q15 transforms with
    a flooring shift produces against an engine that rounds differently.
    """
    m = read_node_mesh(path)
    truth = read_tri_mesh(path).vertices
    if not m.vertices:
        return 0.0
    cell = max(tol, 1)
    grid: dict[tuple[int, int, int], list[tuple[int, int, int]]] = {}
    for v in truth:
        grid.setdefault(tuple(c // cell for c in v), []).append(tuple(v))
    hit = 0
    for c in set(m.vertices):
        base = tuple(x // cell for x in c)
        near = (
            v
            for dx in (-1, 0, 1)
            for dy in (-1, 0, 1)
            for dz in (-1, 0, 1)
            for v in grid.get((base[0] + dx, base[1] + dy, base[2] + dz), ())
        )
        hit += any(max(abs(v[i] - c[i]) for i in range(3)) <= tol for v in near)
    return hit / len(set(m.vertices))


def read_scene(path: str | Path) -> tuple[Mesh, str]:
    """Decode a ``.DSN`` by the best available route. Returns ``(mesh, source)``.

    One chooser, used by both the exporter and the CLI - they each had their
    own copy, and they drifted.

    Order is by what survives the decode:

    ``nodes``
        The scene-graph node, taken when :func:`verify_nodes` agrees with tag
        2 to within a unit or two. The only route that keeps per-object names
        and UVs, so the only one that can produce a **textured** level.
    ``tag1``
        The reference-mapped decode, taken when every face it makes is also a
        tag 2 triangle. Exact, and rare.
    ``tag2``
        Correct geometry as one nameless merged triangle array. Always works,
        and a level exported from it draws as a blank hull.
    """
    p = Path(path)
    try:
        if verify_nodes(p) >= 0.99:
            return read_node_mesh(p), "nodes"
    except (ValueError, struct.error, StopIteration, KeyError):
        pass
    m = read_mesh(p)
    hit, total = verify_against_tag2(p)
    if total and hit == total:
        return m, "tag1"
    return read_tri_mesh(p), "tag2"
