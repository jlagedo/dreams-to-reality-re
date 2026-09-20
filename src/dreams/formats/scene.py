"""Scenes (``.DSN``) and animations (``.DAN``). **Headers solved, bodies packed.**

Both share a 9-byte preamble with the file size stored **unaligned at offset 5**,
then a u32 span field and a u16 count. Verified against every file on the discs -
98 scenes and 191 animations.

``.DSN``::

    0x00  char[4]  "DSNF"
    0x04  u8       0x00
    0x05  u32      file size            unaligned
    0x09  u8       0x00
    0x0A  u32      count_a              A = 31*name_count + 7   <- DERIVED
    0x0E  u16      name_count           1 .. 32
    0x10  char[11][name_count]          object names, DOS FCB 8+3 style
          u32[2]                        8-byte scene block
          u32[5][name_count]            20-byte record per object
          ----                          packed body at 24 + 31*name_count

``count_a`` is not a second count. It is a **header span** - a precomputed
offset so the loader can skip the tables and jump straight to the payload.

``.DAN``::

    0x0A  u32      span                 body_offset - 9
    0x0E  u16      name_count
    0x10  char[11][name_count]
          u16      frame_count
          char[13][frame_count]         ".3DA" labels, FIXED-WIDTH slots
          ----                          packed body

The ``.3DA`` files exist nowhere on either disc - frames are embedded and these
are retained authoring labels. Sparse numbering (000, 002, 004, 016, 050) means
keyframes selected from a longer authored sequence.

Object names decode as French room construction: ``M`` + compass letter for
walls (ME/MN/MO/MS = Mur Est/Nord/Ouest/Sud), ``SOL`` for floor, ``P`` for
plafond. See docs/assets.md.

Both bodies are PACKED and remain the project's top unsolved target: ``.DSN``
entropy 4.5-7.8 (median 6.9), ``.DAN`` 7.3-7.8.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
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

    @property
    def size_ok(self) -> bool:
        return self.declared_size == self.actual_size

    @property
    def span_ok(self) -> bool:
        """``A = 31*name_count + 7`` — holds in all 98 scenes on the discs."""
        return self.count_a == 31 * self.name_count + 7

    @property
    def body_size(self) -> int:
        return self.actual_size - self.body_offset


@dataclass
class Animation:
    path: Path
    declared_size: int
    actual_size: int
    names: list[str]
    frame_refs: list[str]
    body_offset: int
    span: int

    @property
    def size_ok(self) -> bool:
        return self.declared_size == self.actual_size

    @property
    def span_ok(self) -> bool:
        """``body_offset == 9 + A`` — holds in all 191 animations on the discs."""
        return self.body_offset == 9 + self.span

    @property
    def name(self) -> str:
        return self.names[0] if self.names else ""

    @property
    def body_size(self) -> int:
        return self.actual_size - self.body_offset


def _preamble(data: bytes, expect: bytes) -> tuple[str, int]:
    magic = data[:4]
    if magic != expect:
        raise ValueError(f"expected {expect!r}, found {magic!r}")
    return magic.decode("latin-1"), struct.unpack_from("<I", data, 5)[0]


def read_dsn(path: str | Path) -> Scene:
    """Parse a ``DSNF`` scene header.

    ``count_a`` is a derived header span, not a count: ``A = 31*name_count + 7``
    holds in all 98 files on the discs, putting the packed body at
    ``17 + A == 24 + 31*name_count``. Verified 98/98.
    """
    p = Path(path)
    data = p.read_bytes()
    magic, declared = _preamble(data, b"DSNF")

    count_a = struct.unpack_from("<I", data, 10)[0]
    name_count = struct.unpack_from("<H", data, 14)[0]

    names = []
    for i in range(name_count):
        off = 16 + i * NAME_RECORD
        names.append(data[off : off + NAME_RECORD].split(b"\0")[0].decode("latin-1"))

    body_offset = 24 + 31 * name_count
    return Scene(p, magic, declared, len(data), count_a, name_count, names, body_offset)


def read_dan(path: str | Path) -> Animation:
    """Parse a ``DANF`` animation header.

    Layout, verified against all 191 files::

        0x0A  u32 A          body_offset - 9
        0x0E  u16 N          object-name count
        0x10  char[11] x N   object names
              u16 F          frame-reference count
              char[13] x F   ".3DA" labels, FIXED-WIDTH slots
        body  == 9 + A

    The ``.3DA`` files themselves exist nowhere on either disc; the frames are
    embedded and these are retained authoring labels.
    """
    p = Path(path)
    data = p.read_bytes()
    _magic, declared = _preamble(data, b"DANF")

    span = struct.unpack_from("<I", data, 0x0A)[0]
    name_count = struct.unpack_from("<H", data, 0x0E)[0]

    names = []
    for i in range(name_count):
        off = 16 + i * NAME_RECORD
        names.append(data[off : off + NAME_RECORD].split(b"\0")[0].decode("latin-1"))

    off = 16 + NAME_RECORD * name_count
    frame_count = struct.unpack_from("<H", data, off)[0]
    off += 2

    refs = []
    for i in range(frame_count):
        rec = data[off + i * 13 : off + i * 13 + 13]
        refs.append(rec.split(b"\0")[0].decode("latin-1"))

    body = off + 13 * frame_count
    return Animation(p, declared, len(data), names, refs, body, span)


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
