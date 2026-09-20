"""Binary reading helpers and a zero-dependency PNG writer.

Every multi-byte field in Cryo's formats is little-endian. Several headers store
values at *unaligned* offsets (``DSNF`` and ``DANF`` put the file size at offset
5), so the reader is byte-oriented rather than struct-aligned.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Reader:
    """Little-endian cursor over a bytes buffer."""

    data: bytes
    pos: int = 0

    @classmethod
    def from_file(cls, path: str | Path, limit: int | None = None) -> Reader:
        raw = Path(path).read_bytes()
        return cls(raw[:limit] if limit else raw)

    def __len__(self) -> int:
        return len(self.data)

    def seek(self, pos: int) -> Reader:
        self.pos = pos
        return self

    def skip(self, n: int) -> Reader:
        self.pos += n
        return self

    def bytes(self, n: int) -> bytes:
        out = self.data[self.pos : self.pos + n]
        self.pos += n
        return out

    def u8(self) -> int:
        return self.bytes(1)[0]

    def u16(self) -> int:
        return struct.unpack("<H", self.bytes(2))[0]

    def u32(self) -> int:
        return struct.unpack("<I", self.bytes(4))[0]

    def s16(self) -> int:
        return struct.unpack("<h", self.bytes(2))[0]

    def s32(self) -> int:
        return struct.unpack("<i", self.bytes(4))[0]

    def fixed16_16(self) -> float:
        """Cryo stores world coordinates as 16.16 fixed point."""
        return self.s32() / 65536.0

    def tag(self) -> str:
        return self.bytes(4).decode("latin-1")

    def fixed_str(self, n: int) -> str:
        """Null-padded fixed-width name field (``.DSN`` uses 11-byte records)."""
        return self.bytes(n).split(b"\0")[0].decode("latin-1")

    def cstr(self, max_len: int = 256) -> str:
        end = self.data.find(b"\0", self.pos, self.pos + max_len)
        if end < 0:
            end = self.pos + max_len
        out = self.data[self.pos : end].decode("latin-1")
        self.pos = end + 1
        return out

    def peek(self, n: int, at: int | None = None) -> bytes:
        p = self.pos if at is None else at
        return self.data[p : p + n]


def hexdump(data: bytes, start: int = 0, length: int = 128, base: int = 0) -> str:
    """Classic 16-byte-per-row hex + ASCII dump."""
    lines = []
    chunk = data[start : start + length]
    for i in range(0, len(chunk), 16):
        row = chunk[i : i + 16]
        hexs = " ".join(f"{b:02x}" for b in row)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        lines.append(f"{base + start + i:08x}  {hexs:<47}  {text}")
    return "\n".join(lines)


# --------------------------------------------------------------------------
# PNG output. Written by hand so the toolkit has no image dependency - the
# whole point is decoding unknown pixel layouts, where Pillow adds nothing.
# --------------------------------------------------------------------------


def write_png(path: str | Path, width: int, height: int, rgb: bytes) -> Path:
    """Write 8-bit RGB pixel data as a PNG. ``rgb`` must be ``width*height*3``."""
    expected = width * height * 3
    if len(rgb) != expected:
        raise ValueError(f"expected {expected} bytes of RGB, got {len(rgb)}")

    def chunk(kind: bytes, payload: bytes) -> bytes:
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    scanlines = b"".join(b"\0" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height))
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines, 6))
        + chunk(b"IEND", b"")
    )
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(png)
    return p


# Pixel unpackers. The engine renders 16-bit hi-color; RGB555 is confirmed by
# the constant 0x3DEF (mid grey) in .3DC material blocks and by bpp=16 in every
# HNM6 header. 0x7C1F (magenta) appears to be the transparency key.
PIXEL_FORMATS = ("rgb555", "bgr555", "rgb565", "gray8", "pal8")


def unpack_pixels(data: bytes, count: int, fmt: str, palette: bytes | None = None) -> bytes:
    """Convert ``count`` pixels of ``fmt`` into packed 8-bit RGB triples."""
    out = bytearray()
    if fmt == "gray8":
        for i in range(count):
            v = data[i] if i < len(data) else 0
            out += bytes((v, v, v))
        return bytes(out)

    if fmt == "pal8":
        if not palette:
            raise ValueError("pal8 requires a palette")
        for i in range(count):
            idx = data[i] if i < len(data) else 0
            out += palette[idx * 3 : idx * 3 + 3].ljust(3, b"\0")
        return bytes(out)

    for i in range(count):
        o = i * 2
        v = (data[o] | (data[o + 1] << 8)) if o + 1 < len(data) else 0
        if fmt == "rgb555":
            r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
            out += bytes((r * 255 // 31, g * 255 // 31, b * 255 // 31))
        elif fmt == "bgr555":
            b, g, r = (v >> 10) & 31, (v >> 5) & 31, v & 31
            out += bytes((r * 255 // 31, g * 255 // 31, b * 255 // 31))
        elif fmt == "rgb565":
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            out += bytes((r * 255 // 31, g * 255 // 63, b * 255 // 31))
        else:
            raise ValueError(f"unknown pixel format {fmt!r}")
    return bytes(out)


def bytes_per_pixel(fmt: str) -> int:
    return 1 if fmt in ("gray8", "pal8") else 2


def vga6_palette(data: bytes, entries: int = 256) -> bytes:
    """Expand a 6-bit VGA palette in RGBX records to 8-bit RGB triples.

    ``ALPHABET.SPR`` opens with exactly this: 4-byte records whose channels never
    exceed 0x3F.
    """
    out = bytearray()
    for i in range(entries):
        o = i * 4
        r, g, b = data[o], data[o + 1], data[o + 2]
        out += bytes((r * 255 // 63, g * 255 // 63, b * 255 // 63))
    return bytes(out)
