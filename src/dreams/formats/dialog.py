"""`DIALOG.DRD` — the game's **voice bank**, with the script alongside it.

24.6 MB, and it was catalogued as a "dialog bundle". It is overwhelmingly
audio: **72.6% is 178 RIFF/WAVE clips**, 27.3% is tagged portrait sprites, and
only **0.084%** is the 589 timed text lines. **[verified]**

::

    0x00  char[4]  "DRDF"
    0x04  u32      file size, equal to the physical length
    0x08  u32      entry count, 178
    0x0c  u32      two further words, meaning unknown
    0x14  u32[N]   entry table

A table word is **not** a plain offset. The upper three bytes hold a 24-bit
offset field; the low byte has an unknown role::

offset24 = word >> 8
absolute_offset = (wrap_count << 24) | offset24

The low byte is 0 for entries 0-122 and 1 for 123-177. Treating the full word
as a plain offset breaks at the 122/123 boundary. The parser reconstructs the
absolute position by detecting a wrap in the 24-bit field and incrementing a
high-byte wrap counter; that reading walks all 178 entries exactly to EOF, with
``P[i] + entry_size == P[i+1]`` throughout. **[verified]**; the low byte's
semantic role is still unknown.

Each entry header is 14 bytes, then sub-blocks tagged ``u8 tag, u32 size``.
The sub-block size includes its own 5-byte header::

    P+0   u8   1
    P+1   u32  entry size, through to the next header
    P+5   u32  text-line count
    P+9   u8   2
    P+10  u32  stored WAVE size, consistently 13 more than the RIFF's own
    P+14       RIFF/WAVE, mono 11,025 Hz 8-bit
    ...        tag 3 = timed text, optional tag 4 = one indexed portrait sprite

The dialogue is plain 7-bit ASCII and **English**, while ``DATA/LANG`` is
French-labelled — this dump is a mixed build.

Tag 4's payload uses the same ``TABLE`` sprite layout as ``ICONES.BF``: a
512-byte RGB555 palette, a 2-byte-per-pixel image, the ``TABLE`` marker, 256
28-byte descriptor capacity, and a trailing count of 1. The first descriptor
is the portrait the game displays; the remaining reserved descriptors are
unused. 169 entries have this block and 9 do not. The images are 124×124 or
128×128 (with five nearby dimension variants), not lip-sync keys or camera
vectors.

At runtime, `FUN_0041072c` caches the entry-offset table and a reusable entry
buffer; `FUN_00410928` seeks and reads one requested entry. Event `0x40` routes
the selected entry to the voice/text presentation path. The game scales each
raw line timing by `15/100` before scheduling its caption. See
`docs/sprites-ui-dialog.md` for the retail call path.
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
    portrait: bytes = b""


def read(path: str | Path) -> list[Entry]:
    """Every dialogue entry, with its WAVE payload and text."""
    raw = Path(path).read_bytes()
    if raw[:4] != MAGIC:
        raise ValueError(f"{Path(path).name}: not DRDF")
    count = struct.unpack_from("<I", raw, 8)[0]
    words = struct.unpack_from(f"<{count}I", raw, TABLE)
    out: list[Entry] = []
    bank, prev = 0, -1
    for i, w in enumerate(words):
        low24 = w >> 8
        # Offsets rise monotonically, so a drop means the 24-bit field wrapped.
        # The low byte is NOT the bank: at entry 122 it is still 0 while the
        # true offset has already crossed into bank 1, which is why reading it
        # as the high byte loses exactly that one entry.
        if low24 < prev:
            bank += 1
        prev = low24
        at = (bank << 24) | low24
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
            # The tag-2 size includes its 5-byte header; the payload is the
            # WAVE itself.
            pos += max(wave_size - 5, 0)
        end = min(at + size, len(raw))
        while pos + 5 <= end:
            tag = raw[pos]
            block_size = struct.unpack_from("<I", raw, pos + 1)[0]
            # DRD block sizes include their 5-byte tag/size header. The tag-3
            # total therefore ends exactly where the optional tag-4 header
            # begins.
            if block_size < 5 or pos + block_size > end:
                break
            body = raw[pos + 5 : pos + block_size]
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
            elif tag == 4:
                entry.portrait = body
            pos += block_size
        out.append(entry)
    return out


def script(entries: list[Entry]) -> str:
    """The whole script as plain text, one block per entry."""
    parts = [
        "# DIALOG.DRD - recovered script",
        f"# {len(entries)} entries, {sum(len(e.lines) for e in entries)} timed lines, 7-bit ASCII",
        "# t= is the raw stored timing; the game multiplies it by 15/100 before display.",
        "",
    ]
    for e in entries:
        parts.append(f"[entry {e.index:03d}]")
        parts += [f"  t={t:<6d} {line}" for t, line in zip(e.timings, e.lines, strict=False)]
        parts.append("")
    return "\n".join(parts)
