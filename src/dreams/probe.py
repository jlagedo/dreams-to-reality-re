"""Analysis helpers for unknown data: identification, entropy, stride detection.

These are the measurements that told us ``.DSN`` bodies are packed (zlib 72%,
comparable to compressed video) while ``.BF`` icons are raw (zlib 26%).
"""

from __future__ import annotations

import math
import re
import zlib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

# Four-character tags observed on the discs, mapped to a human label.
MAGIC = {
    b"F3DC": ("3dc", "Cryo geometry chunk (.3DC/.3DM)"),
    b"DANF": ("dan", "Cryo animation (.DAN)"),
    b"DSNF": ("dsn", "Cryo scene/level (.DSN)"),
    b"DRDF": ("drd", "Cryo dialogue bank (.DRD)"),
    b"PAK0": ("pak", "Cryo model archive (.PAK)"),
    b"UBIK": ("bf", "UBIK image bundle (.BF)"),
    b"UBB2": ("ubb", "UBIK presentation bundle"),
    b"UBS2": ("ubb", "UBIK presentation bundle (S variant)"),
    b"HNM4": ("hnm4", "Cryo HNM4 video"),
    b"HNM6": ("hnm6", "Cryo HNM6 video"),
    b"HNS6": ("hnm6", "Cryo HNM6 video (S variant)"),
    b"RIFF": ("riff", "RIFF container (WAVE)"),
    b"AIL3": ("dig", "Miles Sound System driver (.DIG)"),
    b"MZ": ("exe", "DOS/PE/LE executable"),
}

FSB_MAGIC = b"DREAMS FSB  "


@dataclass
class Identity:
    path: Path
    size: int
    kind: str
    label: str
    magic: str


def identify(path: str | Path) -> Identity:
    """Identify a file by magic bytes, falling back to the extension."""
    p = Path(path)
    head = p.open("rb").read(16)
    size = p.stat().st_size

    if head.startswith(FSB_MAGIC):
        return Identity(p, size, "fsb", "Dreams sound-effect bank", "DREAMS FSB")

    for magic, (kind, label) in MAGIC.items():
        if head.startswith(magic):
            return Identity(p, size, kind, label, magic.decode("latin-1"))

    ext = p.suffix.lower().lstrip(".")
    guesses = {
        "spr": ("spr", "sprite / palette data (raw)"),
        "alp": ("alp", "alpha map (raw)"),
        "tga": ("tga", "Truevision Targa"),
        "id": ("id", "disc/install marker"),
        "ini": ("ini", "text resource"),
        "txt": ("txt", "text"),
        "dat": ("dat", "unknown binary table"),
    }
    kind, label = guesses.get(ext, ("unknown", "unidentified"))
    return Identity(p, size, kind, label, head[:4].hex(" "))


# --------------------------------------------------------------------------


@dataclass
class Stats:
    size: int
    entropy: float
    zlib_ratio: int
    zero_pct: int
    ascii_pct: int

    @property
    def verdict(self) -> str:
        """Rule of thumb calibrated against known controls on these discs.

        Raw controls (.TGA, .3DC, .SPR) sit at 18-31%. INTRO.HNM, known
        compressed video, sits at 69%.
        """
        if self.zlib_ratio >= 85:
            return "packed/compressed"
        if self.zlib_ratio >= 65:
            return "dense (likely packed)"
        if self.zlib_ratio >= 45:
            return "mixed"
        return "raw"


def stats(data: bytes) -> Stats:
    n = len(data)
    if n == 0:
        return Stats(0, 0.0, 0, 0, 0)
    counts = Counter(data)
    entropy = -sum((c / n) * math.log2(c / n) for c in counts.values())
    ratio = len(zlib.compress(data, 6)) * 100 // n
    zeros = counts[0] * 100 // n
    printable = sum(c for b, c in counts.items() if 32 <= b < 127) * 100 // n
    return Stats(n, entropy, ratio, zeros, printable)


def region_map(data: bytes, bucket: int = 65536) -> list[tuple[int, int, int]]:
    """Per-bucket (index, zero%, ascii%). Flat output means homogeneous data."""
    rows = []
    for i in range(0, len(data), bucket):
        ch = data[i : i + bucket]
        z = ch.count(0) * 100 // len(ch)
        a = sum(1 for b in ch if 32 <= b < 127) * 100 // len(ch)
        rows.append((i // bucket, z, a))
    return rows


def best_strides(
    data: bytes, lo: int = 16, hi: int = 1400, samples: int = 4000, top: int = 8
) -> list[tuple[float, int]]:
    """Guess image row width by mean absolute difference between rows.

    A low score at stride S means byte i and byte i+S correlate, i.e. S is a
    plausible row pitch. Returns ``(score, stride_in_bytes)`` best first.
    """
    n = min(len(data), 400_000)
    out = []
    for s in range(lo, hi):
        step = max(1, (n - s) // samples)
        total = count = 0
        for i in range(0, n - s, step):
            total += abs(data[i] - data[i + s])
            count += 1
        if count:
            out.append((total / count, s))
    out.sort()
    return out[:top]


def find_tags(data: bytes) -> Counter:
    """Count embedded four-character tags anywhere in the buffer."""
    hits: Counter = Counter()
    for magic in list(MAGIC) + [FSB_MAGIC, b"LZWCRYO", b"WAVE"]:
        n = data.count(magic)
        if n:
            hits[magic.decode("latin-1")] = n
    return hits


def strings(data: bytes, min_len: int = 5) -> list[str]:
    pattern = rb"[ -~]{%d,}" % min_len
    return [m.group().decode("latin-1") for m in re.finditer(pattern, data)]
