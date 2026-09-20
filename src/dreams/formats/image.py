"""Image assets: ``UBIK`` bundles (``.BF``), sprites (``.SPR``) and ``.ALP`` maps.

Both containers are now decoded. **[verified]**

``ICONES.BF`` — UBIK bundle
    char[4]  "UBIK"
    u32      2                    container version
    u32      table offset         == filesize - 267*count
    u32      count
    ---- 0x10 ----                payload, entries back to back
    ---- table ----               count x 267-byte records:
        char[259] filename        NUL-padded
        u32       absolute offset
        u32       length

The payload chain is exact: each ``offset + length`` equals the next entry's
offset, and the last ends precisely at the table. Members are ordinary ``.SPR``
and ``.ALP`` files, so ``.BF`` carries no image format of its own.

``.SPR`` has no magic and comes in two unrelated flavours.

*Indexed bundles* (``DATA/OBJET``) — 8-bit paletted, NOT raw RGB555:
    0x000  u8[4][256]   VGA palette, RGBX, 6-bit channels (<= 0x3F)
    0x400  u32[256]     pointer table, offsets relative to 0x400
    record u32 width, u32 height, u32 ?, u32 ?, then width*height indices
           record size = round4(16 + width*height)

*Font files* (``DATA/FONT`` — ``HI320``/``HI480``/``HI640``):
    0x00   u16[16]      RGB555 palette
    tail   8 x 28-byte glyph descriptors (offset @+0, width @+8, height @+0x0C)
    end    u32 data_end, u32 0x100

The glyph *pixel* encoding is not pinned down, so fonts are reported but not
rendered. Guessing would produce confidently wrong images.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

UBIK_MAGIC = b"UBIK"
RECORD_SIZE = 267
NAME_SIZE = 259

# Pointer-table slots that are not payload records. Their meaning is unverified;
# they are consistent across all five OBJET sprite files.
SENTINELS = {0, 0x100, 0x5000}


@dataclass
class Entry:
    name: str
    offset: int
    size: int


@dataclass
class Bundle:
    path: Path
    version: int
    table_offset: int
    entry_count: int
    entries: list[Entry] = field(default_factory=list)

    @property
    def chain_is_exact(self) -> bool:
        """True if every payload abuts the next and the last ends at the table."""
        pos = 16
        for e in self.entries:
            if e.offset != pos:
                return False
            pos += e.size
        return pos == self.table_offset


def read_bundle(path: str | Path) -> Bundle:
    p = Path(path)
    data = p.read_bytes()
    if not data.startswith(UBIK_MAGIC):
        raise ValueError(f"{p.name}: not a UBIK bundle")

    version, table_offset, count = struct.unpack_from("<III", data, 4)
    bundle = Bundle(p, version, table_offset, count)

    for i in range(count):
        base = table_offset + i * RECORD_SIZE
        if base + RECORD_SIZE > len(data):
            break
        name = data[base : base + NAME_SIZE].split(b"\x00")[0].decode("latin-1")
        offset, size = struct.unpack_from("<II", data, base + NAME_SIZE)
        bundle.entries.append(Entry(name, offset, size))
    return bundle


def extract_bundle(bundle: Bundle, out_dir: str | Path) -> list[Path]:
    """Write every member of a UBIK bundle as a standalone file."""
    data = bundle.path.read_bytes()
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    written = []
    for e in bundle.entries:
        blob = data[e.offset : e.offset + e.size]
        if len(blob) != e.size:
            continue
        target = out / e.name
        target.write_bytes(blob)
        written.append(target)
    return written


# ----------------------------------------------------------------- sprites ---


@dataclass
class Sprite:
    index: int
    offset: int
    width: int
    height: int
    unknown_a: int
    unknown_b: int

    @property
    def record_size(self) -> int:
        raw = 16 + self.width * self.height
        return raw + (-raw % 4)  # round up to 4


@dataclass
class SpriteSheet:
    path: Path
    palette: list[tuple[int, int, int]]
    sprites: list[Sprite] = field(default_factory=list)


def looks_like_vga_palette(data: bytes, entries: int = 256) -> bool:
    """True if the buffer opens with 4-byte RGBX records whose channels are 6-bit."""
    if len(data) < entries * 4:
        return False
    return all(
        data[i * 4] <= 0x3F and data[i * 4 + 1] <= 0x3F and data[i * 4 + 2] <= 0x3F
        for i in range(entries)
    )


def read_spritesheet(path: str | Path) -> SpriteSheet:
    """Parse an indexed ``.SPR`` bundle. Raises if it is not the indexed flavour."""
    from dreams.png import vga6_to_rgb

    p = Path(path)
    data = p.read_bytes()
    if not looks_like_vga_palette(data):
        raise ValueError(f"{p.name}: no 6-bit VGA palette - not an indexed .SPR")

    palette = [vga6_to_rgb(data[i * 4 : i * 4 + 4]) for i in range(256)]
    sheet = SpriteSheet(p, palette)

    pointers = struct.unpack_from("<256I", data, 0x400)
    seen: set[int] = set()
    for i, rel in enumerate(pointers):
        if rel in SENTINELS:
            continue
        off = 0x400 + rel
        if off in seen or off + 16 > len(data):
            continue
        width, height, ua, ub = struct.unpack_from("<IIII", data, off)
        if not (0 < width <= 4096 and 0 < height <= 4096):
            continue
        if off + 16 + width * height > len(data):
            continue
        seen.add(off)
        sheet.sprites.append(Sprite(i, off, width, height, ua, ub))
    return sheet


def sprite_rgba(sheet: SpriteSheet, sprite: Sprite, transparent_index: int = 0) -> bytes:
    """Expand one sprite to tightly packed RGBA bytes.

    ``transparent_index`` is **[unverified]**: index 0 is the usual convention
    for 8-bit sprite transparency and these sheets do keep index 0 dark, but the
    engine has not been checked. Pass ``-1`` to make every pixel opaque.
    """
    data = sheet.path.read_bytes()
    start = sprite.offset + 16
    pixels = data[start : start + sprite.width * sprite.height]

    out = bytearray(len(pixels) * 4)
    for i, idx in enumerate(pixels):
        r, g, b = sheet.palette[idx]
        out[i * 4] = r
        out[i * 4 + 1] = g
        out[i * 4 + 2] = b
        out[i * 4 + 3] = 0 if idx == transparent_index else 255
    return bytes(out)
