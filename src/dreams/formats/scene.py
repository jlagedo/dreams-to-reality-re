"""Scenes (``.DSN``) and animations (``.DAN``). Both PARTIAL.

The two formats share a 9-byte preamble with the file size stored **unaligned at
offset 5** - verified exact on six samples:

    char[4]  magic          "DSNF" / "DANF"
    u8       flag           0x00 in every sample
    u32      file size      little-endian, at offset 5

``.DSN`` then continues:

    u8       0x00
    u32      count_a        813 / 782 / 906 across samples - meaning unknown
    u16      name_count     26 / 25 / 29 - confirmed exact
    ---- 0x10 ----
    char[11][name_count]    null-padded object names, DOS FCB 8+3 style
    ----                    16.16 fixed-point values, then a packed body

Object names decode as French room construction: ``M`` + compass letter for
walls (ME/MN/MO/MS = Mur Est/Nord/Ouest/Sud), ``SOL`` for floor, ``P`` for
plafond. See docs/assets.md.

The ``.DSN`` body is PACKED - entropy 6.4, zlib 72%, flat across every 64 KB
bucket. Level geometry and textures are in there and are the project's top
unsolved target.

``.DAN`` continues with an 11-byte object name, a frame count, and a run of
null-terminated ``.3DA`` source filenames. Those ``.3DA`` files do not exist on
either disc - the frames are embedded and the names are authoring labels. The
sparse numbering (000, 002, 004, 016, 050) means keyframes.
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field
from pathlib import Path

NAME_RECORD = 11


@dataclass
class Scene:
    path: Path
    magic: str
    declared_size: int
    actual_size: int
    count_a: int
    name_count: int
    names: list[str]
    body_offset: int
    coords: list[float] = field(default_factory=list)

    @property
    def size_ok(self) -> bool:
        return self.declared_size == self.actual_size


@dataclass
class Animation:
    path: Path
    declared_size: int
    actual_size: int
    name: str
    frame_refs: list[str]
    materials: list[str]
    body_offset: int

    @property
    def size_ok(self) -> bool:
        return self.declared_size == self.actual_size


def _preamble(data: bytes, expect: bytes) -> tuple[str, int]:
    magic = data[:4]
    if magic != expect:
        raise ValueError(f"expected {expect!r}, found {magic!r}")
    return magic.decode("latin-1"), struct.unpack_from("<I", data, 5)[0]


def read_dsn(path: str | Path) -> Scene:
    p = Path(path)
    data = p.read_bytes()
    magic, declared = _preamble(data, b"DSNF")

    count_a = struct.unpack_from("<I", data, 10)[0]
    name_count = struct.unpack_from("<H", data, 14)[0]

    names: list[str] = []
    off = 16
    while off + NAME_RECORD <= len(data):
        rec = data[off : off + NAME_RECORD]
        text = rec.split(b"\0")[0]
        if not text or not all(32 <= c < 127 for c in text):
            break
        names.append(text.decode("latin-1"))
        off += NAME_RECORD

    coords = [struct.unpack_from("<i", data, off + i * 4)[0] / 65536.0 for i in range(min(8, 8))]
    return Scene(p, magic, declared, len(data), count_a, name_count, names, off, coords)


def read_dan(path: str | Path) -> Animation:
    p = Path(path)
    data = p.read_bytes()
    _magic, declared = _preamble(data, b"DANF")

    name = data[16:27].split(b"\0")[0].decode("latin-1")

    refs = [
        m.group().decode("latin-1") for m in re.finditer(rb"[A-Za-z0-9_]{1,12}\.3D[A-Za-z]", data)
    ]
    body = data.find(refs[-1].encode()) + len(refs[-1]) + 1 if refs else 27

    materials = [
        s.decode("latin-1")
        for s in re.findall(rb"[A-Za-z][A-Za-z0-9_]{3,15}", data[:512])
        if s.decode("latin-1") not in {name, "DANF"} and not s.endswith(b"3DA")
    ]
    return Animation(p, declared, len(data), name, refs, materials[:8], body)


def classify_name(name: str) -> str:
    """Interpret a ``.DSN`` object name. See docs/assets.md - UNVERIFIED."""
    body = name.split("_", 1)[-1]
    table = {
        "ME": "wall east (mur est)",
        "MN": "wall north (mur nord)",
        "MO": "wall west (mur ouest)",
        "MS": "wall south (mur sud)",
        "SOL": "floor (sol)",
        "P": "ceiling (plafond)?",
        "COL": "column (colonne)",
        "BAS": "base",
        "CENT": "centre",
        "CH": "?",
    }
    for prefix, meaning in sorted(table.items(), key=lambda kv: -len(kv[0])):
        if body.startswith(prefix):
            return meaning
    return "?"
