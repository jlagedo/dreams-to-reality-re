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
    declared: int = 0

    @property
    def mapping_is_clean(self) -> bool:
        """True when the reference-to-vertex mapping is provably right.

        References are stale pointers into 40-byte slots. When every distinct
        reference in a scene forms **one contiguous run** and the count agrees
        with the ``u32`` at tag 2 + 0x14, rank and index coincide and the decode
        is trustworthy. That holds in only **4 of 95 scenes** - E01GROTT,
        L03_REQI, L16_BOMB and O01EAU01.

        Elsewhere the references split across several arrays with unrelated
        bases (E10_PIEC has runs starting at 221, 22,281 and 24,225), so rank
        and index diverge and the geometry comes out scrambled. Those scenes
        need the per-array bases resolved before they can be exported.
        """
        return (
            self.ref_stride == 40
            and self.ref_slots == self.ref_count
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

    # The pool's length is not in the header: the u32 at tag 2 + 0x14 matches the
    # reference count in only 3 of 30 scenes sampled. Take the count from the
    # data instead - one vertex per distinct reference - and let the geometric
    # checks in `dreams mesh` say whether the result is sane.
    rank = {v: i for i, v in enumerate(sorted(refs))}
    need = 0x30 + VERTEX_STRIDE * len(rank)
    if need > len(tag2):
        raise ValueError(
            f"{p.name}: {len(rank)} vertices need {need} bytes, tag 2 has {len(tag2)}"
        )
    vertices = [
        struct.unpack_from("<3i", tag2, 0x30 + VERTEX_STRIDE * i) for i in range(len(rank))
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
                at = row[w] - 1
                if at + 8 <= len(tag1):
                    u, v = struct.unpack_from("<2i", tag1, at)
                    obj.uvs.append((u / 65536 / ATLAS, v / 65536 / ATLAS))
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
        declared=struct.unpack_from("<I", tag2, 0x14)[0],
    )
