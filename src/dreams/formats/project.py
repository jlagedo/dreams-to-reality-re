"""`DREAMS.DAT` — the project bank, which is the game's **level graph**.

150 projects, each the engine's unit of progression. The file indexes itself::

    0x000   u32[151]   record offsets, relative to 0x400
    0x25c   420 x 00   padding
    0x400   150 records, each **zero-run compressed**

The codec is two lines, and it is the engine's own - `FUN_00448e25` in
``WINDREAM.EXE``: a nonzero byte is copied, and ``00 N`` expands to ``N`` zero
bytes. Every record decodes to exactly **0x2200 bytes**, a fixed struct::

    +0x0000  header      0x200   starts "ProjectN"
    +0x0200  Link[8]     0x80    destination + a two-corner volume
    +0x0600  Objet[16]   0xc0    an asset and where it stands
    +0x1200  Box[12]     0x100   typed point geometry
    +0x1e00  LinkAdvent[16] 0x40

An active slot starts with its family name; an all-zero slot is unused.

**Why this was misread for so long.** Scanned in its compressed form the file
looks like a tagged key/value stream with variable-width integers: ``c4 09 00
02`` is just 2500 with its two zero bytes run-length encoded. The count byte
after a name's terminator was read as a "type byte", and the count byte before
the *next* name was read as part of that name - which is where the key names
``FLINK`` and ``DLINK`` came from. ``F`` is 0x46: a run of seventy zeros.

A ``LINK`` is the level transition itself: a destination project and an
axis-aligned volume. Project 0's ``LINK1`` spans (-500, 1000, -19000) to
(5500, 5000, -12500), names ``Project62``, and contains the floating island
``F84.DAN`` at (2500, 4000, -15000). Fly into the box, load *Ile du Hamam*.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

INDEX = 151  #: u32 offsets, the last one marking the end of the file
RECORDS = 0x400  #: offsets are relative to here
RECORD_SIZE = 0x2200  #: every record, once decompressed

LINKS, LINK_AT, LINK_SIZE = 8, 0x200, 0x80
OBJETS, OBJET_AT, OBJET_SIZE = 16, 0x600, 0xC0
BOXES, BOX_AT, BOX_SIZE = 12, 0x1200, 0x100
ADVENTS, ADVENT_AT, ADVENT_SIZE = 16, 0x1E00, 0x40

Vec = tuple[int, int, int]


def decompress(record: bytes) -> bytes:
    """Expand one zero-run compressed record: ``00 N`` becomes ``N`` zeros."""
    out = bytearray()
    i, n = 0, len(record)
    while i < n:
        b = record[i]
        i += 1
        if b:
            out.append(b)
        elif i < n:
            out += bytes(record[i])
            i += 1
        else:  # a trailing escape with no count; none occur on the discs
            raise ValueError("truncated zero-run escape")
    return bytes(out)


def _cstr(cell: bytes) -> str:
    return cell.split(b"\x00", 1)[0].decode("latin-1")


@dataclass
class Link:
    name: str
    destination: str  #: "Project<n>", or "EMPTY" / "" in 5 of 244
    lo: Vec
    hi: Vec

    @property
    def project(self) -> int | None:
        tail = self.destination[7:]
        return int(tail) if self.destination.startswith("Project") and tail.isdigit() else None

    def contains(self, p: Vec) -> bool:
        return all(self.lo[i] <= p[i] <= self.hi[i] for i in range(3))


@dataclass
class Objet:
    name: str
    asset: str
    position: Vec


@dataclass
class Box:
    name: str
    kind: int  #: 0..9; what each means is not established
    points: list[Vec]


@dataclass
class Project:
    index: int
    name: str
    links: list[Link] = field(default_factory=list)
    objets: list[Objet] = field(default_factory=list)
    boxes: list[Box] = field(default_factory=list)
    advents: list[str] = field(default_factory=list)

    @property
    def scene(self) -> str:
        """``OBJET0`` is always the scene - in 150 of 150 projects."""
        return self.objets[0].asset if self.objets else ""


def _slots(rec: bytes, at: int, size: int, count: int, family: bytes):
    for i in range(count):
        cell = rec[at + size * i : at + size * (i + 1)]
        # Unused slots can hold stale fragments ("INK0", "BJET3"); only a slot
        # that starts with its family name is live.
        if cell.startswith(family):
            yield cell


def parse(index: int, rec: bytes) -> Project:
    """One decompressed 0x2200-byte record."""
    if len(rec) != RECORD_SIZE:
        raise ValueError(f"record {index}: {len(rec):#x} bytes, expected {RECORD_SIZE:#x}")
    pj = Project(index, _cstr(rec[:0x20]))
    for c in _slots(rec, LINK_AT, LINK_SIZE, LINKS, b"LINK"):
        v = struct.unpack_from("<6i", c, 0x24)
        pj.links.append(Link(_cstr(c[:12]), _cstr(c[12:24]), v[:3], v[3:]))
    for c in _slots(rec, OBJET_AT, OBJET_SIZE, OBJETS, b"OBJET"):
        pj.objets.append(
            Objet(_cstr(c[:12]), _cstr(c[12:44]), struct.unpack_from("<3i", c, 0x40))
        )
    for c in _slots(rec, BOX_AT, BOX_SIZE, BOXES, b"BOX"):
        n = max(0, min(struct.unpack_from("<i", c, 0xE4)[0], 16))
        pts = [struct.unpack_from("<3i", c, 0x24 + 12 * k) for k in range(n)]
        pj.boxes.append(Box(_cstr(c[:12]), struct.unpack_from("<i", c, 0xF0)[0], pts))
    for c in _slots(rec, ADVENT_AT, ADVENT_SIZE, ADVENTS, b"LINKADVENT"):
        pj.advents.append(_cstr(c[:12]))
    return pj


def read(path: str | Path) -> list[Project]:
    """Every project in ``DREAMS.DAT``."""
    data = Path(path).read_bytes()
    offsets = struct.unpack_from(f"<{INDEX}I", data, 0)
    if RECORDS + offsets[-1] != len(data):
        raise ValueError("DREAMS.DAT: index does not end at the file size")
    return [
        parse(i, decompress(data[RECORDS + offsets[i] : RECORDS + offsets[i + 1]]))
        for i in range(INDEX - 1)
    ]


def reachable(projects: list[Project], start: int = 0) -> set[int]:
    """Projects reachable from ``start`` by following ``LINK`` destinations."""
    seen, todo = {start}, [start]
    while todo:
        for link in projects[todo.pop()].links:
            d = link.project
            if d is not None and d not in seen and d < len(projects):
                seen.add(d)
                todo.append(d)
    return seen
