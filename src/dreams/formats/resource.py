"""``DREAMS.INI`` - the game's text resource file. SOLVED (it is plain text).

Sections are ``[OBJECT]``, ``[PROJECT]``, ``[DIALOG]`` and ``[SYSTEM]``; records
within a section are separated by ``[NEW]``. Lines starting with ``#`` are
comments, per the file's own documented syntax header.

Encoding is CP1252 (the file is French and full of accented characters).

Content notes live in docs/game-content.md: 30 inventory items, 150 levels, and
a lot of placeholder description text that appears to have shipped.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

ENCODING = "cp1252"


@dataclass
class Resources:
    path: Path
    sections: dict[str, list[list[str]]] = field(default_factory=dict)

    @property
    def items(self) -> list[tuple[str, str]]:
        """(name, description) for each inventory object."""
        out = []
        for rec in self.sections.get("OBJECT", []):
            if rec:
                out.append((rec[0], " / ".join(rec[1:])))
        return out

    @property
    def levels(self) -> list[tuple[str, str]]:
        """(project id, descriptive name) for each level."""
        out = []
        for rec in self.sections.get("PROJECT", []):
            if len(rec) >= 2:
                out.append((rec[0], rec[1]))
            elif rec:
                out.append((rec[0], ""))
        return out


def read(path: str | Path) -> Resources:
    p = Path(path)
    text = p.read_text(encoding=ENCODING, errors="replace")

    res = Resources(p)
    section: str | None = None
    record: list[str] = []

    def flush() -> None:
        nonlocal record
        if section and record:
            res.sections.setdefault(section, []).append(record)
        record = []

    for raw in text.splitlines():
        line = raw.rstrip()
        if line.startswith("#"):
            continue
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            tag = stripped[1:-1]
            if tag == "NEW":
                flush()
            else:
                flush()
                section = tag
                res.sections.setdefault(section, [])
            continue
        if stripped:
            record.append(stripped)
    flush()
    return res
