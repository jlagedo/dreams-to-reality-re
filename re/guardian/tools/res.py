"""List or extract The Guardian of Darkness resource archives (game.res, audio.res, ...).

Layout (read from the four retail archives; the loader is not yet traced):

    header, 16 bytes
      i32  -(16 + 40 * count)   negated size of header and table
      u32  count
      u32  0
      u16  0x30, u16 varies    unknown
    count entries, 40 bytes each
      char path[32]             ".\\ms1\\mission.mpl", NUL padded
      u32  size
      u32  offset               from the end of the table

  uv run python re/guardian/tools/res.py list game.res [--grep PATTERN]
  uv run python re/guardian/tools/res.py stats game.res
  uv run python re/guardian/tools/res.py extract game.res [OUTDIR] [--grep PATTERN]
"""

from __future__ import annotations

import argparse
import collections
import re
import struct
import sys
from pathlib import Path
from typing import NamedTuple

REPO = Path(__file__).resolve().parents[3]


class Entry(NamedTuple):
    path: str
    size: int
    offset: int  # absolute file offset


def read_table(path: Path) -> list[Entry]:
    with path.open("rb") as f:
        neg, count, zero, _u1, _u2 = struct.unpack("<iIIHH", f.read(16))
        table = 16 + 40 * count
        if -neg != table:
            raise ValueError(f"{path}: header says table ends at {-neg}, count gives {table}")
        raw = f.read(40 * count)
    size = path.stat().st_size
    out = []
    for i in range(count):
        name, n, off = struct.unpack_from("<32sII", raw, 40 * i)
        e = Entry(name.split(b"\0", 1)[0].decode("latin-1"), n, table + off)
        if e.offset + e.size > size:
            raise ValueError(f"{path}: entry {e.path} runs past the end of the file")
        out.append(e)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("command", choices=["list", "stats", "extract"])
    ap.add_argument("archive", type=Path)
    ap.add_argument("outdir", type=Path, nargs="?")
    ap.add_argument("--grep", help="regular expression on the stored path")
    args = ap.parse_args()

    entries = read_table(args.archive)
    if args.grep:
        pat = re.compile(args.grep, re.I)
        entries = [e for e in entries if pat.search(e.path)]

    if args.command == "list":
        for e in entries:
            print(f"{e.offset:10d} {e.size:10d}  {e.path}")
    elif args.command == "stats":
        ext = collections.Counter()
        total = collections.Counter()
        for e in entries:
            k = Path(e.path.replace("\\", "/")).suffix.lower() or "(none)"
            ext[k] += 1
            total[k] += e.size
        print(f"{len(entries)} entries, {sum(total.values()):,} bytes")
        for k, n in ext.most_common():
            print(f"  {k:8} {n:6d} files {total[k]:14,d} bytes")
    else:
        out = args.outdir or REPO / "out" / "guardian" / "res" / args.archive.stem
        with args.archive.open("rb") as f:
            for e in entries:
                rel = e.path.replace("\\", "/").lstrip("./")
                dst = out / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                f.seek(e.offset)
                dst.write_bytes(f.read(e.size))
        print(f"{len(entries)} files -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
