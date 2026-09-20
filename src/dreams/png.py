"""Minimal PNG writer.

Deliberately dependency-free. The toolkit otherwise needs only typer and rich,
and pulling in Pillow to write a handful of 8-bit images is not a good trade.
PNG is simple enough: a signature, three chunks, and zlib-deflated scanlines
each prefixed with a filter byte.

Only what this project needs: 8-bit RGB and RGBA, no interlacing, filter type 0.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def write(path: str | Path, width: int, height: int, pixels: bytes, alpha: bool = False) -> Path:
    """Write an 8-bit PNG. ``pixels`` is tightly packed RGB or RGBA, top row first."""
    channels = 4 if alpha else 3
    expected = width * height * channels
    if len(pixels) != expected:
        raise ValueError(f"expected {expected} bytes for {width}x{height}, got {len(pixels)}")

    stride = width * channels
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        raw += pixels[y * stride : (y + 1) * stride]

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6 if alpha else 2, 0, 0, 0)
    blob = (
        SIGNATURE
        + _chunk(b"IHDR", ihdr)
        + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + _chunk(b"IEND", b"")
    )

    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(blob)
    return p


def rgb555_to_rgb(words: bytes, count: int) -> bytes:
    """Expand little-endian RGB555 halfwords to 8-bit RGB triples.

    The engine's colour format throughout: 5 bits per channel in the low 15
    bits, top bit unused. Channels are scaled by replicating the high bits
    (``v << 3 | v >> 2``) so 0x1F maps to 0xFF rather than 0xF8.
    """
    out = bytearray(count * 3)
    for i in range(count):
        v = words[i * 2] | (words[i * 2 + 1] << 8)
        r, g, b = (v >> 10) & 0x1F, (v >> 5) & 0x1F, v & 0x1F
        out[i * 3] = (r << 3) | (r >> 2)
        out[i * 3 + 1] = (g << 3) | (g >> 2)
        out[i * 3 + 2] = (b << 3) | (b >> 2)
    return bytes(out)


def vga6_to_rgb(entry: bytes) -> tuple[int, int, int]:
    """Expand one 6-bit VGA DAC palette entry (0..63 per channel) to 8-bit."""
    r, g, b = entry[0] & 0x3F, entry[1] & 0x3F, entry[2] & 0x3F
    return (r << 2) | (r >> 4), (g << 2) | (g >> 4), (b << 2) | (b >> 4)
