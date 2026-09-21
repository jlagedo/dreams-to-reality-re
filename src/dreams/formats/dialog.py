"""`DIALOG.DRD` — the game's **voice bank**, with the script alongside it.

24.6 MB, and it was catalogued as a "dialog bundle". It is overwhelmingly
audio: **72.6% is 178 RIFF/WAVE clips**, 27.3% is auxiliary binary, and only
**0.084%** is the 589 lines of script. **[verified]**

::

    0x00  char[4]  "DRDF"
    0x04  u32      file size, equal to the physical length
    0x08  u32      entry count, 178
    0x0c  u32      two further words, meaning unknown
    0x14  u32[N]   entry table

A table word is **not** a plain offset. Its low byte is a bank/flag and the
upper three bytes are the offset within that bank::

    offset = (word & 0xff) << 24 | word >> 8

The low byte is 0 for entries 0-122 and 1 for 123-177, so treating the word as
a plain address breaks at the 122/123 boundary. **[unverified]** as to what the
bank means; what is certain is that this reading walks all 178 entries exactly
to EOF, with ``P[i] + entry_size == P[i+1]`` throughout.

Each entry header is 14 bytes, then sub-blocks tagged ``u8 tag, u32 size``::

    P+0   u8   1
    P+1   u32  entry size, through to the next header
    P+5   u32  text-line count
    P+9   u8   2
    P+10  u32  stored WAVE size, consistently 13 more than the RIFF's own
    P+14       RIFF/WAVE, mono 11,025 Hz 8-bit
    ...        tag 3 = text lines, tag 4 = auxiliary binary

The dialogue is plain 7-bit ASCII and **English**, while ``DATA/LANG`` is
French-labelled — this dump is a mixed build.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

MAGIC = b"DRDF"
TABLE = 0x14
HEADER = 14


@dataclass
class Entry:
    index: int
    offset: int
    size: int
    lines: list[str] = field(default_factory=list)
    timings: list[int] = field(default_factory=list)
    wave: bytes = b""


def read(path: str | Path) -> list[Entry]:
    """Every dialogue entry, with its WAVE payload and text."""
    raw = Path(path).read_bytes()
    if raw[:4] != MAGIC:
        raise ValueError(f"{Path(path).name}: not DRDF")
    count = struct.unpack_from("<I", raw, 8)[0]
    words = struct.unpack_from(f"<{count}I", raw, TABLE)
    out: list[Entry] = []
    for i, w in enumerate(words):
        at = ((w & 0xFF) << 24) | (w >> 8)
        if at + HEADER > len(raw):
            continue
        size = struct.unpack_from("<I", raw, at + 1)[0]
        entry = Entry(i, at, size)
        wave_size = struct.unpack_from("<I", raw, at + 10)[0]
        pos = at + HEADER
        if raw[pos : pos + 4] == b"RIFF":
            riff = struct.unpack_from("<I", raw, pos + 4)[0] + 8
            entry.wave = raw[pos : pos + riff]
            pos += riff
        else:  # pragma: no cover - every sampled entry has one
            pos += max(wave_size - 13, 0)
        end = min(at + size, len(raw))
        while pos + 5 <= end:
            tag = raw[pos]
            blk = struct.unpack_from("<I", raw, pos + 1)[0]
            if blk <= 0 or pos + 5 + blk > end:
                break
            body = raw[pos + 5 : pos + 5 + blk]
            if tag == 3:
                # Each line is: u32 timing, u8 length (counting the NUL),
                # then the text. The first line of an entry is always t=0.
                q = 0
                while q + 5 <= len(body):
                    timing = struct.unpack_from("<I", body, q)[0]
                    length = body[q + 4]
                    q += 5
                    if length == 0 or q + length > len(body):
                        break
                    text = body[q : q + length].split(b"\x00")[0]
                    q += length
                    if text.strip():
                        entry.lines.append(text.decode("latin-1"))
                        entry.timings.append(timing)
            pos += 5 + blk
        out.append(entry)
    return out


def script(entries: list[Entry]) -> str:
    """The whole script as plain text, one block per entry."""
    parts = [
        "# DIALOG.DRD - recovered script",
        f"# {len(entries)} entries, {sum(len(e.lines) for e in entries)} lines, 7-bit ASCII",
        "# t= is the line's timing field, in the game's own units.",
        "",
    ]
    for e in entries:
        parts.append(f"[entry {e.index:03d}]")
        parts += [
            f"  t={t:<6d} {line}"
            for t, line in zip(e.timings, e.lines, strict=False)
        ]
        parts.append("")
    return "\n".join(parts)
