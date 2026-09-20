"""Redbook CD audio — the game's music.

The soundtrack is the one asset class that exists only in the disc *images* and
never in an extracted filesystem. Both discs are mixed-mode: track 1 is the
ISO9660 data track, every track after it is redbook audio. **[verified]**

    Disc 1   12 tracks   11 audio   26:58
    Disc 2   14 tracks   13 audio   22:28

Redump-style dumps store one ``.bin`` per track, and an audio track's ``.bin``
is already raw CD-DA: 44100 Hz, 16-bit signed little-endian, stereo. There is no
decoding to do — only a container to put it in.

Three tracks are duplicated across the two discs, so 24 physical tracks hold 21
unique pieces of music, 46 minutes in total. **[verified]** by SHA-256.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from pathlib import Path

RATE = 44100
CHANNELS = 2
BITS = 16
BYTE_RATE = RATE * CHANNELS * BITS // 8  # 176400

TRACK_RE = re.compile(r"\(Track (\d+)\)", re.IGNORECASE)


@dataclass
class Track:
    disc: int
    number: int
    path: Path
    size: int
    is_audio: bool
    sha256: str = ""

    @property
    def seconds(self) -> float:
        return self.size / BYTE_RATE if self.is_audio else 0.0

    @property
    def duration(self) -> str:
        s = self.seconds
        return f"{int(s // 60)}:{s % 60:04.1f}"


def find_tracks(image_dir: str | Path, disc: int) -> list[Track]:
    """List the per-track ``.bin`` files of a Redump-style dump directory."""
    d = Path(image_dir)
    tracks: list[Track] = []
    for p in sorted(d.glob("*.bin")):
        m = TRACK_RE.search(p.name)
        if not m:
            continue
        n = int(m.group(1))
        tracks.append(Track(disc, n, p, p.stat().st_size, is_audio=n > 1))
    return tracks


def hash_track(track: Track) -> str:
    """SHA-256 of a track, cached on the record. Used to find cross-disc dupes."""
    if not track.sha256:
        h = hashlib.sha256()
        with track.path.open("rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 22), b""):
                h.update(chunk)
        track.sha256 = h.hexdigest()
    return track.sha256


def dedupe(tracks: list[Track]) -> tuple[list[Track], dict[str, list[Track]]]:
    """Split audio tracks into (unique, duplicate-groups-by-hash)."""
    groups: dict[str, list[Track]] = {}
    for t in tracks:
        if t.is_audio:
            groups.setdefault(hash_track(t), []).append(t)
    unique = [g[0] for g in groups.values()]
    dupes = {k: v for k, v in groups.items() if len(v) > 1}
    return unique, dupes
