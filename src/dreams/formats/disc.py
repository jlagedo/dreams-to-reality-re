"""Raw CD track conversion.

Redump-style dumps store MODE1/2352 sectors: a 16-byte sync+header, 2048 bytes
of payload, then 288 bytes of ECC. Strip to get a mountable 2048 B/sector ISO.

Audio tracks (2-12 on disc 1, 2-14 on disc 2) are redbook audio and hold the
game's music. They never appear as files - mount the ``.cue``, not the ``.iso``,
or the game loses its soundtrack and raises MCI errors.
"""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

RAW_SECTOR = 2352
USER_DATA = 2048
HEADER = 16


def iter_sectors(src: str | Path, chunk_sectors: int = 512) -> Iterator[bytes]:
    with Path(src).open("rb") as fh:
        while True:
            block = fh.read(RAW_SECTOR * chunk_sectors)
            if len(block) < RAW_SECTOR:
                return
            out = bytearray()
            for i in range(0, len(block) - RAW_SECTOR + 1, RAW_SECTOR):
                out += block[i + HEADER : i + HEADER + USER_DATA]
            yield bytes(out)


def to_iso(src: str | Path, dst: str | Path) -> int:
    """Convert a MODE1/2352 data track to a 2048 B/sector ISO. Returns bytes written."""
    target = Path(dst)
    target.parent.mkdir(parents=True, exist_ok=True)
    total = 0
    with target.open("wb") as out:
        for block in iter_sectors(src):
            out.write(block)
            total += len(block)
    return total


def parse_cue(path: str | Path) -> list[tuple[int, str, str]]:
    """Return (track number, mode, filename) for each track in a cue sheet."""
    tracks: list[tuple[int, str, str]] = []
    current_file = ""
    for line in Path(path).read_text(errors="replace").splitlines():
        s = line.strip()
        if s.upper().startswith("FILE "):
            parts = s.split('"')
            current_file = parts[1] if len(parts) > 1 else s.split()[1]
        elif s.upper().startswith("TRACK "):
            bits = s.split()
            tracks.append((int(bits[1]), bits[2], current_file))
    return tracks
