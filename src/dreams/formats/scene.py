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
          u32[5][name_count]            20-byte record per object
          ----                          packed body at 16 + 31*name_count

``count_a`` is not a second count. It is a **header span** - a precomputed
offset so the loader can skip the tables and jump straight to the payload:
``body_offset == 9 + count_a``, the same rule ``.DAN`` uses.

The body offset comes from the loader itself, not from arithmetic:
``FUN_004175bc`` in ``WINDREAM.EXE`` consumes 9 + 5 + 2 bytes of header, then
``name_count * 0xb`` for the name table and ``name_count * 0x14`` for the
records - 16 + 31*name_count, with no gap between the two tables. An earlier
reading put the body 8 bytes later and invented an 8-byte "scene block" to
explain the difference. See docs/dsn-loader.md.

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
        """``A = 31*name_count + 7`` — holds in all 98 scenes on the discs.

        Equivalently ``body_offset == 9 + A``, the same relation ``.DAN`` uses.
        """
        return (self.count_a == 31 * self.name_count + 7
                and self.body_offset == 9 + self.count_a)

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
    ``9 + A == 16 + 31*name_count`` - confirmed against the loader in
    ``WINDREAM.EXE``, which reads the two tables back to back.
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

    body_offset = 16 + 31 * name_count
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


# --------------------------------------------------------------- body records

#: Record tags seen in ``.DSN`` bodies. ``.DAN`` uses 1, 2 and 3 only.
TAG_GEOMETRY = 1  # variable size, carries object and material names
TAG_PACKED = 2  # variable size, no readable names
TAG_PALETTE = 3  # exactly 5 + 1024*N: one 256-entry palette per object
TAG_TILES = 4  # exactly 5 + 1024*N: one 32x32 tile per object, 64 records

TILE_W = TILE_H = 32
TILE_BYTES = TILE_W * TILE_H  # 1024, and also 256 palette entries x 4 bytes
TILES_PER_OBJECT = 64
PALETTE_ENTRIES = 256


@dataclass
class Record:
    tag: int
    offset: int  # from the start of the body
    size: int  # including the 5-byte header
    payload: bytes

    @property
    def per_object(self) -> int:
        return len(self.payload)


def read_records(path: str | Path, kind: str = "dsn") -> list[Record]:
    """Walk a ``.DSN`` or ``.DAN`` body as a chain of ``u8 tag, u32 size`` records.

    ``size`` includes the 5-byte header. The chain reaches EOF exactly in 95/95
    distinct scenes and 129/129 distinct animations, so a short walk means the
    body offset is wrong rather than the file being damaged.

    The grammar came from ``FUN_00417afd`` in ``WINDREAM.EXE``, which reads the
    tag, takes the ``u32`` length, then hands each object ``0x400`` bytes of the
    payload. See docs/dsn-loader.md.
    """
    p = Path(path)
    header = read_dsn(p) if kind == "dsn" else read_dan(p)
    body = p.read_bytes()[header.body_offset :]

    records: list[Record] = []
    pos = 0
    while pos + 5 <= len(body):
        tag = body[pos]
        size = struct.unpack_from("<I", body, pos + 1)[0]
        if size < 5 or pos + size > len(body):
            break
        records.append(Record(tag, pos, size, body[pos + 5 : pos + size]))
        pos += size
    return records


@dataclass
class ObjectTexture:
    """One object's 64-tile texture bank plus the palette it indexes."""

    name: str
    index: int
    palette: list[int]  # 256 RGB565 words
    tiles: list[bytes]  # 64 x 1024 bytes of 8-bit indices

    def tile_rgb(self, n: int) -> bytes:
        """Tile ``n`` as 32x32 packed RGB, ready for :func:`dreams.png.write`."""
        pal = [rgb565_to_rgb(c) for c in self.palette]
        return b"".join(bytes(pal[b]) for b in self.tiles[n])

    def sheet_rgb(self, cols: int = 8) -> bytes:
        """All 64 tiles laid out as a ``cols``-wide grid, packed RGB."""
        pal = [rgb565_to_rgb(c) for c in self.palette]
        rows_of_tiles = (len(self.tiles) + cols - 1) // cols
        out = []
        for gy in range(rows_of_tiles):
            for y in range(TILE_H):
                line = []
                for gx in range(cols):
                    i = gy * cols + gx
                    tile = self.tiles[i] if i < len(self.tiles) else bytes(TILE_BYTES)
                    row = tile[y * TILE_W : (y + 1) * TILE_W]
                    line.append(b"".join(bytes(pal[b]) for b in row))
                out.append(b"".join(line))
        return b"".join(out)


def rgb565_to_rgb(word: int) -> tuple[int, int, int]:
    """Expand an RGB565 word to full-range 8-bit RGB.

    **RGB565, not RGB555.** Decoding these palettes as 555 puts impossible cyan
    and magenta speckles through every texture; 565 renders clean rock, ice and
    lava that match the scenes' own French names. Verified visually across six
    scenes. Material colours in ``.3DC`` still read as 555 - see docs/assets.md.
    """
    return (
        ((word >> 11) & 0x1F) * 255 // 31,
        ((word >> 5) & 0x3F) * 255 // 63,
        (word & 0x1F) * 255 // 31,
    )


def read_textures(path: str | Path) -> list[ObjectTexture]:
    """Decode every object's texture bank from a ``.DSN`` scene.

    Layout, recovered from the loader and confirmed against all 95 scenes::

        tag 3   x1    1024 B per object   256 entries of (u16 zero, u16 rgb565)
        tag 4   x64   1024 B per object   one 32x32 8-bit indexed tile

    So each object owns a 256-colour palette and 64 distinct 32x32 tiles -
    65,536 bytes of image data. These are the level textures, and they are
    **uncompressed**: tags 3 and 4 are ~97% of the body by volume, which makes
    the old "157 MB of packed data" description of ``.DSN`` badly wrong.
    """
    sc = read_dsn(path)
    records = read_records(path)

    palettes = next((r.payload for r in records if r.tag == TAG_PALETTE), None)
    tiles = [r.payload for r in records if r.tag == TAG_TILES]
    if palettes is None or not tiles:
        return []

    out = []
    for i in range(sc.name_count):
        lo, hi = i * TILE_BYTES, (i + 1) * TILE_BYTES
        pal = [
            struct.unpack_from("<H", palettes, lo + k * 4 + 2)[0]
            for k in range(PALETTE_ENTRIES)
        ]
        out.append(ObjectTexture(sc.names[i], i, pal, [t[lo:hi] for t in tiles]))
    return out
