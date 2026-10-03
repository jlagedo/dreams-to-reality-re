"""List or extract The Devil Inside .gtz archives (Datas/*.gtz).

Layout, read from the retail archives (the loader is not traced yet):

    u32 version (1)
    directory, recursively:
      u32 file_count
      u32 subdirectory_count
      u8  name_length, name (no terminator)
      file_count files:
        u8  kind              2 in every file seen: zlib-compressed
        u8  name_length, name, NUL
        u32 time              Unix time (files are dated 1999-2000)
        u32 compressed_size
        u32 size
        u32 unknown
        compressed_size bytes of zlib data
      subdirectory_count directories

The matching .str files repeat the directory part.

  uv run python re/devil/tools/gtz.py list Level1.gtz
  uv run python re/devil/tools/gtz.py stats Persos.gtz
  uv run python re/devil/tools/gtz.py extract Persos.gtz [OUTDIR]
"""

from __future__ import annotations

import argparse
import collections
import datetime
import struct
import sys
import zlib
from pathlib import Path
from typing import NamedTuple

REPO = Path(__file__).resolve().parents[3]


class Entry(NamedTuple):
    path: str
    kind: int
    time: int
    csize: int
    size: int
    unknown: int
    offset: int


def walk(buf: bytes) -> list[Entry]:
    (version,) = struct.unpack_from("<I", buf, 0)
    if version != 1:
        raise ValueError(f"version {version}")
    out: list[Entry] = []

    def directory(p: int, parent: str) -> int:
        nfiles, ndirs = struct.unpack_from("<II", buf, p)
        n = buf[p + 8]
        name = buf[p + 9 : p + 9 + n].decode("latin-1")
        p += 9 + n
        here = f"{parent}/{name}" if parent else name
        for _ in range(nfiles):
            kind, n = buf[p], buf[p + 1]
            fname = buf[p + 2 : p + 2 + n].decode("latin-1")
            p += 2 + n + 1
            t, csize, size, unk = struct.unpack_from("<4I", buf, p)
            p += 16
            out.append(Entry(f"{here}/{fname}", kind, t, csize, size, unk, p))
            p += csize
        for _ in range(ndirs):
            p = directory(p, here)
        return p

    end = directory(4, "")
    if end != len(buf):
        raise ValueError(f"tree ends at {end}, file is {len(buf)} bytes")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("command", choices=["list", "stats", "extract"])
    ap.add_argument("archive", type=Path)
    ap.add_argument("outdir", type=Path, nargs="?")
    args = ap.parse_args()
    buf = args.archive.read_bytes()
    entries = walk(buf)
    if args.command == "list":
        for e in entries:
            day = datetime.datetime.fromtimestamp(e.time, datetime.UTC).date()
            print(f"{e.size:10d} {e.csize:10d} k{e.kind} {day} {e.unknown:#010x}  {e.path}")
    elif args.command == "stats":
        n, b = collections.Counter(), collections.Counter()
        for e in entries:
            k = Path(e.path).suffix.lower() or "(none)"
            n[k] += 1
            b[k] += e.size
        kinds = collections.Counter(e.kind for e in entries)
        print(f"{len(entries)} files, {sum(b.values()):,} bytes; kinds {dict(kinds)}")
        for k, c in n.most_common():
            print(f"  {k:8} {c:6d} files {b[k]:14,d} bytes")
    else:
        out = args.outdir or REPO / "out" / "devil" / "gtz" / args.archive.stem
        for e in entries:
            data = zlib.decompress(buf[e.offset : e.offset + e.csize])
            if len(data) != e.size:
                raise ValueError(f"{e.path}: {len(data)} bytes, header says {e.size}")
            dst = out / e.path
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
        print(f"{len(entries)} files -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
