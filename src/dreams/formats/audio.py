"""Audio banks: ``FSB.DAT`` sound effects and ``DIALOG.DRD`` voice.

Both are SOLVED in the sense that matters - the clips inside are plain RIFF
WAVE, so extraction is lossless and needs no codec.

``FSB.DAT``  (DATA/SOUND/)      24 clips, PCM mono 11025 Hz 16-bit
    char[12]   "DREAMS FSB  "
    u32        count
    u32[count] block sizes (RIFF payload + 8)
    ---------- 0x70 for count=24
    RIFF WAVE  x count, contiguous

``DIALOG.DRD`` (DATA/3DC/)     178 clips, PCM mono 11025 Hz 8-bit
    char[4]    "DRDF"
    u32        total file size      (offset 4, aligned - unlike DSNF/DANF)
    u32        count
    ...        offset/size index    <- layout PARTIAL, not yet pinned down
    ---------- 0x2eb for DIALOG.DRD
    RIFF WAVE  x count

Because the DRD index is not fully decoded, extraction scans for validated RIFF
headers instead of trusting it. The declared count is used as a cross-check.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

FSB_MAGIC = b"DREAMS FSB  "
DRD_MAGIC = b"DRDF"


@dataclass
class Clip:
    index: int
    offset: int
    size: int
    channels: int
    rate: int
    bits: int

    @property
    def seconds(self) -> float:
        byte_rate = self.rate * self.channels * max(self.bits, 1) // 8
        return self.size / byte_rate if byte_rate else 0.0

    def describe(self) -> str:
        return f"{self.channels}ch {self.rate}Hz {self.bits}bit  {self.seconds:5.2f}s"


@dataclass
class Bank:
    path: Path
    kind: str
    declared_count: int
    clips: list[Clip]


def _wave_format(data: bytes, riff_at: int) -> tuple[int, int, int]:
    """Read (channels, rate, bits) from the ``fmt `` chunk of a RIFF at offset."""
    pos = riff_at + 12
    end = min(len(data), riff_at + 256)
    while pos + 8 <= end:
        cid = data[pos : pos + 4]
        csz = struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"fmt ":
            _tag, ch, rate, _bps, _align, bits = struct.unpack_from("<HHIIHH", data, pos + 8)
            return ch, rate, bits
        if csz <= 0 or csz > len(data):
            break
        pos += 8 + csz + (csz & 1)
    return 0, 0, 0


def _valid_riff(data: bytes, off: int) -> int | None:
    """Return RIFF payload size if a well-formed RIFF/WAVE starts at ``off``."""
    if data[off : off + 4] != b"RIFF" or data[off + 8 : off + 12] != b"WAVE":
        return None
    size = struct.unpack_from("<I", data, off + 4)[0]
    if size < 16 or off + 8 + size > len(data) + 64:
        return None
    return size


def read_fsb(path: str | Path) -> Bank:
    p = Path(path)
    data = p.read_bytes()
    if not data.startswith(FSB_MAGIC):
        raise ValueError(f"{p.name}: not a DREAMS FSB bank")

    count = struct.unpack_from("<I", data, 12)[0]
    sizes = list(struct.unpack_from(f"<{count}I", data, 16))
    offset = 16 + count * 4

    clips = []
    for i, size in enumerate(sizes):
        ch, rate, bits = _wave_format(data, offset)
        clips.append(Clip(i, offset, size, ch, rate, bits))
        offset += size
    return Bank(p, "fsb", count, clips)


def read_drd(path: str | Path) -> Bank:
    p = Path(path)
    data = p.read_bytes()
    if not data.startswith(DRD_MAGIC):
        raise ValueError(f"{p.name}: not a DRDF bank")

    declared = struct.unpack_from("<I", data, 8)[0]

    clips: list[Clip] = []
    pos = 0
    while True:
        pos = data.find(b"RIFF", pos)
        if pos < 0:
            break
        size = _valid_riff(data, pos)
        if size is None:
            pos += 4
            continue
        ch, rate, bits = _wave_format(data, pos)
        if rate == 0:  # false positive inside PCM data
            pos += 4
            continue
        clips.append(Clip(len(clips), pos, size + 8, ch, rate, bits))
        pos += 8 + size
    return Bank(p, "drd", declared, clips)


def read_bank(path: str | Path) -> Bank:
    """Dispatch on magic: FSB or DRD."""
    head = Path(path).open("rb").read(12)
    if head.startswith(FSB_MAGIC):
        return read_fsb(path)
    if head.startswith(DRD_MAGIC):
        return read_drd(path)
    raise ValueError(f"{Path(path).name}: not a recognised Dreams audio bank")


def extract(bank: Bank, out_dir: str | Path, prefix: str | None = None) -> list[Path]:
    """Write every clip as a standalone ``.wav``."""
    data = bank.path.read_bytes()
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    stem = prefix or bank.path.stem.lower()

    written = []
    for clip in bank.clips:
        blob = data[clip.offset : clip.offset + clip.size]
        if not blob.startswith(b"RIFF"):
            continue
        target = out / f"{stem}_{clip.index:03d}.wav"
        target.write_bytes(blob)
        written.append(target)
    return written
