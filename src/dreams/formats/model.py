"""Geometry (``.3DC`` / ``.3DM``) and the ``PAK0`` archive. Both PARTIAL.

``F3DC`` is shared by both ``.3DC`` and ``.3DM`` - they are one container, not
two formats.

    char[4]  "F3DC"
    u32      450            version; constant across every sample on the discs
    ...      sparse fixed-size records, 74% zero fill

Material name slots are readable in plain text (``DEFAULT``, ``GRILLE``,
``archer``, ``fleche``). ``ARC.3DC`` carries 0x3DEF twice at offset 0x4c - mid
grey in RGB555, a default material colour.

``PAK0`` is a thin wrapper:

    char[4]  "PAK0"
    u32      file size - 4
    u32      ?
    ---- 0x0C ----
    "F3DC" payload

Parsing PAK0 is the cheapest way into F3DC because it hands you chunk bounds.
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass
from pathlib import Path

F3DC_MAGIC = b"F3DC"
PAK_MAGIC = b"PAK0"


@dataclass
class Model:
    path: Path
    version: int
    size: int
    materials: list[str]
    fixed_values: list[float]
    zero_pct: int


@dataclass
class PakEntry:
    offset: int
    magic: str


@dataclass
class Pak:
    path: Path
    declared_size: int
    actual_size: int
    unknown: int
    entries: list[PakEntry]


def read_model(path: str | Path) -> Model:
    p = Path(path)
    data = p.read_bytes()
    if not data.startswith(F3DC_MAGIC):
        raise ValueError(f"{p.name}: not an F3DC chunk")

    version = struct.unpack_from("<I", data, 4)[0]
    names = [
        m.group().decode("latin-1")
        for m in re.finditer(rb"[A-Za-z][A-Za-z0-9_]{2,15}", data[:2048])
    ]
    materials = [n for n in dict.fromkeys(names) if n != "F3DC"][:16]
    fixed = [struct.unpack_from("<i", data, 8 + i * 4)[0] / 65536.0 for i in range(4)]
    zero_pct = data.count(0) * 100 // len(data)
    return Model(p, version, len(data), materials, fixed, zero_pct)


def read_pak(path: str | Path) -> Pak:
    p = Path(path)
    data = p.read_bytes()
    if not data.startswith(PAK_MAGIC):
        raise ValueError(f"{p.name}: not a PAK0 archive")

    declared = struct.unpack_from("<I", data, 4)[0]
    unknown = struct.unpack_from("<I", data, 8)[0]
    entries = [PakEntry(m.start(), "F3DC") for m in re.finditer(re.escape(F3DC_MAGIC), data)]
    return Pak(p, declared, len(data), unknown, entries)


def extract_pak(pak: Pak, out_dir: str | Path) -> list[Path]:
    """Split a PAK0 into its embedded F3DC chunks."""
    data = pak.path.read_bytes()
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    bounds = [e.offset for e in pak.entries] + [len(data)]
    written = []
    for i, start in enumerate(bounds[:-1]):
        target = out / f"{pak.path.stem.lower()}_{i:03d}.3dc"
        target.write_bytes(data[start : bounds[i + 1]])
        written.append(target)
    return written
