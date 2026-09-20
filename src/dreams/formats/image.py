"""Image assets: ``UBIK`` bundles (``.BF``) and raw sprites (``.SPR``/``.ALP``).

``ICONES.BF`` is UNSOLVED and is the best next target.

    char[4]  "UBIK"
    u32      2              version
    u32      size-ish       0x5a94f - close to but not equal to the file size
    u32      5              entry count
    ---- 0x10 ----          pixel data

Measurements say it is raw imagery, not compressed: entropy 5.20, zlib 26%
(raw controls on these discs sit at 18-31%, compressed video at 69%). Row
autocorrelation gives a clean 128-byte stride and its 256-byte multiple - the
strongest periodicity of any file tested.

But rendering at that stride as rgb555, rgb565 and gray8 all yield structured
noise. The container declares 5 entries, so per-entry sub-headers carry the real
dimensions and pixel format. Parse them; do not keep guessing strides.

Four older generations ship alongside (``.OLI``, ``.BAK``, ``OLD/.BAK``,
``OLD/.OLD``), which makes this an unusually good format-diffing target.

``.SPR`` has no magic. Two flavours observed:
  * ``ALPHABET.SPR`` opens with a 6-bit VGA palette in RGBX records (<= 0x3F).
  * ``HI320.SPR`` opens with a descending RGB555 grey ramp.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

UBIK_MAGIC = b"UBIK"


@dataclass
class Bundle:
    path: Path
    version: int
    declared: int
    entry_count: int
    data_offset: int
    size: int


def read_bundle(path: str | Path) -> Bundle:
    p = Path(path)
    data = p.read_bytes()
    if not data.startswith(UBIK_MAGIC):
        raise ValueError(f"{p.name}: not a UBIK bundle")
    version, declared, count = struct.unpack_from("<III", data, 4)
    return Bundle(p, version, declared, count, 16, len(data))


def looks_like_vga_palette(data: bytes, entries: int = 256) -> bool:
    """True if the buffer opens with 4-byte RGBX records whose channels are 6-bit."""
    if len(data) < entries * 4:
        return False
    return all(
        data[i * 4] <= 0x3F and data[i * 4 + 1] <= 0x3F and data[i * 4 + 2] <= 0x3F
        for i in range(entries)
    )
