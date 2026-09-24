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


def extract_sd_audio(path: str | Path) -> bytes | None:
    """Extract audio from HNS6/HNM6 SD chunks as raw 22050 Hz stereo 16-bit PCM WAV.

    The first SD chunk contains a 256-entry signed 16-bit delta table (512 bytes).
    Every payload byte thereafter is a DPCM delta code, interleaved L/R.
    The predictor is integrated with 16-bit unsigned addition mod 65536 and
    persists across chunk boundaries from zero.
    Returns WAV bytes if audio was found, or None if no SD chunks exist.
    """
    p = Path(path)
    b = p.read_bytes()
    if len(b) < 64:
        return None

    pos = 64
    payloads: list[bytes] = []
    while pos + 4 <= len(b):
        raw = struct.unpack_from("<I", b, pos)[0]
        if raw == 0:
            break
        end = pos + (raw & 0xFFFFFF)
        if end > len(b):
            break
        q = pos + 4
        while q < end:
            if q + 8 > end:
                break
            c = struct.unpack_from("<I", b, q)[0]
            if c < 8 or q + c > end:
                break
            if b[q + 4 : q + 6] == b"SD":
                payloads.append(b[q + 8 : q + c])
            q += (c + 3) & ~3
        pos = end

    if not payloads or len(payloads[0]) < 512:
        return None

    lut = struct.unpack("<256h", payloads[0][:512])

    state = [0, 0]
    out = bytearray()
    for pld in payloads:
        data = pld[512:] if pld is payloads[0] else pld
        for j, code in enumerate(data):
            ch = j & 1
            state[ch] = (state[ch] + lut[code]) & 0xFFFF
            s = state[ch] - 65536 if state[ch] >= 32768 else state[ch]
            out.extend(struct.pack("<h", s))

    if not out:
        return None

    # Construct standard 16-bit stereo 22050 Hz RIFF WAVE
    wav_hdr = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + len(out),
        b"WAVE",
        b"fmt ",
        16,
        1,  # PCM
        2,  # Stereo
        22050,  # Sample rate
        22050 * 4,  # Byte rate (22050 * 2ch * 2 bytes)
        4,  # Block align
        16,  # Bits per sample
        b"data",
        len(out),
    )
    return wav_hdr + out
