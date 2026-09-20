"""HNM video headers. Header parsing only - no decoder here.

Two generations ship on these discs:

  HNM4  20 files, 256x256   in-game animated textures (water, fire, bellows).
                            FFmpeg already decodes these.
  HNS6  73 files, 640x304   full-motion cutscenes, 16bpp
  HNM6   2 files            same, with the signature the spec documents

The 64-byte HNM6 header is documented in docs/hnm6-spec.md. A working decoder
for this codec ships on disc 2 inside CryoLib - see docs/cryolib.md.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

SIXTH_GEN = (b"HNM6", b"HNS6")


@dataclass
class Video:
    path: Path
    magic: str
    width: int
    height: int
    bpp: int
    frames: int
    speed: int
    audio_flags: int
    version: str
    copyright: str
    size: int

    @property
    def stereo(self) -> bool:
        return bool(self.audio_flags & 0x80)

    @property
    def audio_rate(self) -> int:
        return ((self.audio_flags >> 5) & 3) * 11025

    @property
    def fps(self) -> int:
        # The spec says speed may be zero; assume 15 fps in that case.
        return self.speed or 15

    @property
    def seconds(self) -> float:
        return self.frames / self.fps if self.frames else 0.0


FIFTH_GEN = (b"UBB2", b"UBS2")

#: NihAV / na_game_tool input-format plugin per magic. The ``S`` spellings are
#: structurally identical to the ``M``/``B`` ones and need a one-line patch to
#: the tool's tag gate before it will accept them. See docs/hnm-video.md.
PLUGIN = {
    "HNM4": "hnm4",
    "UBB2": "hnm5",
    "UBS2": "hnm5",
    "HNM6": "hnm6",
    "HNS6": "hnm6",
}


def read_header(path: str | Path) -> Video:
    p = Path(path)
    data = p.read_bytes()[:64]
    magic = data[:4]
    size = p.stat().st_size

    if magic == b"HNM4":
        # Shorter, different layout - do not apply the generation-6 parse.
        w, h = struct.unpack_from("<HH", data, 8)
        frames = struct.unpack_from("<I", data, 16)[0]
        copyright_ = data[48:64].rstrip(b"\0").decode("latin-1")
        # HNM4 runs at 24 fps; the rate is not stored anywhere in the header.
        return Video(p, "HNM4", w, h, 8, frames, 24, 0, "", copyright_, size)

    if magic in FIFTH_GEN:
        w, h = struct.unpack_from("<HH", data, 8)
        frames = struct.unpack_from("<I", data, 16)[0]
        note = data[32:48].rstrip(b"\0").decode("latin-1", "replace")
        copyright_ = data[48:64].rstrip(b"\0").decode("latin-1")
        return Video(p, magic.decode("latin-1"), w, h, 8, frames, 0, 0, note, copyright_, size)

    if magic not in SIXTH_GEN:
        raise ValueError(f"{p.name}: not an HNM file ({magic!r})")

    audio_flags = data[6]
    bpp = data[7]
    w, h = struct.unpack_from("<HH", data, 8)
    frames, frames_hi = struct.unpack_from("<HH", data, 16)
    version = data[20:24].rstrip(b"\0").decode("latin-1")
    speed, _maxbuffer = struct.unpack_from("<HH", data, 24)
    copyright_ = data[48:64].rstrip(b"\0").decode("latin-1")
    return Video(
        p,
        magic.decode("latin-1"),
        w,
        h,
        bpp,
        frames + (frames_hi << 16),
        speed,
        audio_flags,
        version,
        copyright_,
        size,
    )
