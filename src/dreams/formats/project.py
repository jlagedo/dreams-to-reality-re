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
    entity_type: int = 1  #: 1=prop/landmark, 19=gnome/NPC, 3=creature, 531/1043=special
    heading: int = 0  #: 12-bit angle 0..4095 where 4096 == 360 deg (2*pi)
    flags: int = 1  #: 16-bit flag +0x34 (bit 0=active, bit 1=character, bit 8=dormant)
    behavior_type: int = 0  #: +0x64 AI (1=static, 3=hostile, 5=gnome patrol, 6=flying)
    speed: float = 16.0  #: +0x68 velocity / speed scale (default 16.0f)
    route_index: int = 0  #: +0x6C waypoint route index into BOX
    health: int = 0  #: +0x70 health points / dialogue ID
    radius: int = 0  #: +0x3C bounding / collision radius

    @property
    def is_active_on_start(self) -> bool:
        return bool((self.flags & 1) and not (self.flags & 0x100))

    @property
    def is_character(self) -> bool:
        return bool(self.flags & 2)

    @property
    def yaw_radians(self) -> float:
        import math

        return (self.heading / 4096.0) * (2.0 * math.pi)


@dataclass
class Box:
    name: str
    kind: int  #: 0=ground patrol, 1=aerial flight waypoints
    points: list[Vec]


@dataclass
class LinkAdvent:
    name: str
    target_object: int = -1  #: +0x14 index into OBJET array
    condition_stage: int = 0  #: +0x1C adventure / quest progression stage
    event_opcode: int = 0  #: +0x20 event type / trigger opcode
    action_param: int = 0  #: +0x24 action parameter
    cutscene_video: str = ""  #: +0x2C cutscene video (.HNM / .UBB)


@dataclass
class Project:
    index: int
    name: str
    spawn_position: Vec = (0, 0, 0)
    spawn_heading: int = 0
    anim_video: str = ""
    anim_material: str = ""
    anim_video2: str = ""
    anim_material2: str = ""
    ambient_rgb: Vec = (128, 128, 128)
    dir_light1: Vec = (0, 0, 0)
    dir_light2: Vec = (0, 0, 0)
    fog: tuple[int, int, int, int] = (0, 0, 0, 0)
    sky_rgb: tuple[int, int, int, int] = (0, 0, 0, 0)
    lighting_mode: int = 0  #: 0=Day, 1=Night
    camera_fov: int = 64  #: Field of view in degrees
    cd_track: int = 0  #: Redbook audio track number
    links: list[Link] = field(default_factory=list)
    objets: list[Objet] = field(default_factory=list)
    boxes: list[Box] = field(default_factory=list)
    advents: list[LinkAdvent] = field(default_factory=list)

    @property
    def spawn_yaw_radians(self) -> float:
        import math

        return (self.spawn_heading / 4096.0) * (2.0 * math.pi)

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
    hdr = rec[:0x200]
    pj = Project(
        index,
        _cstr(hdr[:0x20]),
        spawn_position=struct.unpack_from("<3i", hdr, 0xB4),
        spawn_heading=struct.unpack_from("<i", hdr, 0x10C)[0],
        anim_video=_cstr(hdr[0x3C:0x5C]),
        anim_material=_cstr(hdr[0x6C:0x8C]),
        anim_video2=_cstr(hdr[0x5C:0x7C]),
        anim_material2=_cstr(hdr[0x8C:0xAC]),
        ambient_rgb=struct.unpack_from("<3i", hdr, 0x30),
        dir_light1=struct.unpack_from("<3i", hdr, 0x18),
        dir_light2=struct.unpack_from("<3i", hdr, 0x24),
        fog=struct.unpack_from("<4i", hdr, 0xE0),
        sky_rgb=struct.unpack_from("<4i", hdr, 0xF0),
        lighting_mode=struct.unpack_from("<i", hdr, 0x138)[0],
        camera_fov=struct.unpack_from("<i", hdr, 0xA4)[0],
        cd_track=struct.unpack_from("<i", hdr, 0x1F8)[0],
    )
    for c in _slots(rec, LINK_AT, LINK_SIZE, LINKS, b"LINK"):
        v = struct.unpack_from("<6i", c, 0x24)
        pj.links.append(Link(_cstr(c[:12]), _cstr(c[12:24]), v[:3], v[3:]))
    for c in _slots(rec, OBJET_AT, OBJET_SIZE, OBJETS, b"OBJET"):
        spd_raw = struct.unpack_from("<i", c, 0x68)[0]
        pj.objets.append(
            Objet(
                _cstr(c[:12]),
                _cstr(c[12:44]),
                struct.unpack_from("<3i", c, 0x40),
                entity_type=struct.unpack_from("<i", c, 0x34)[0],
                heading=struct.unpack_from("<i", c, 0x5C)[0],
                flags=struct.unpack_from("<H", c, 0x34)[0],
                behavior_type=struct.unpack_from("<i", c, 0x64)[0],
                speed=float(spd_raw) if spd_raw != 0 else 16.0,
                route_index=struct.unpack_from("<i", c, 0x6C)[0],
                health=struct.unpack_from("<i", c, 0x70)[0],
                radius=struct.unpack_from("<i", c, 0x3C)[0],
            )
        )
    for c in _slots(rec, BOX_AT, BOX_SIZE, BOXES, b"BOX"):
        n = max(0, min(struct.unpack_from("<i", c, 0xE4)[0], 16))
        pts = [struct.unpack_from("<3i", c, 0x24 + 12 * k) for k in range(n)]
        pj.boxes.append(Box(_cstr(c[:12]), struct.unpack_from("<i", c, 0xF0)[0], pts))
    for c in _slots(rec, ADVENT_AT, ADVENT_SIZE, ADVENTS, b"LINKADVENT"):
        pj.advents.append(
            LinkAdvent(
                name=_cstr(c[:12]),
                target_object=struct.unpack_from("<i", c, 0x14)[0],
                condition_stage=struct.unpack_from("<i", c, 0x1C)[0],
                event_opcode=struct.unpack_from("<i", c, 0x20)[0],
                action_param=struct.unpack_from("<i", c, 0x24)[0],
                cutscene_video=_cstr(c[0x2C:0x3C]),
            )
        )
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
