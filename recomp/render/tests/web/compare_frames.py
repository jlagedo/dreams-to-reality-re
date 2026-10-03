"""Compare two frame exports written by od_web_frame (native vs browser).

    uv run python recomp/render/tests/web/compare_frames.py A.rgba B.rgba [--png diff.png]

Prints the number of differing pixels, the largest channel difference and a
histogram. Exit status 1 if a channel differs by more than --tolerance
(default 2: the native and the browser GPU round colours independently), for
more than --outliers pixels (default 16: triangle edges may land on a
different pixel between rasterizers).
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def load(path: str):
    data = Path(path).read_bytes()
    w, h = struct.unpack_from("<II", data)
    return w, h, data[8:]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--tolerance", type=int, default=2)
    ap.add_argument("--outliers", type=int, default=16)
    ap.add_argument("--png", default="")
    args = ap.parse_args()
    wa, ha, a = load(args.a)
    wb, hb, b = load(args.b)
    if (wa, ha) != (wb, hb):
        print(f"size differs: {wa}x{ha} vs {wb}x{hb}")
        return 1
    hist: dict[int, int] = {}
    differing = 0
    worst = (0, 0, 0)
    for i in range(0, len(a), 4):
        d = max(abs(a[i + k] - b[i + k]) for k in range(4))
        hist[d] = hist.get(d, 0) + 1
        if d:
            differing += 1
            if d > worst[0]:
                worst = (d, (i // 4) % wa, (i // 4) // wa)
    print(
        f"{wa}x{ha}: {differing} of {wa * ha} pixels differ; max channel difference {worst[0]} "
        f"at x={worst[1]} y={worst[2]}"
    )
    print("histogram of max channel difference:", dict(sorted(hist.items())))
    if args.png:
        from PIL import Image

        out = bytearray()
        for i in range(0, len(a), 4):
            d = max(abs(a[i + k] - b[i + k]) for k in range(4))
            v = min(255, d * 32)
            out += bytes((v, v, v, 255))
        Image.frombytes("RGBA", (wa, ha), bytes(out)).save(args.png)
    outliers = sum(n for d, n in hist.items() if d > args.tolerance)
    print(f"{outliers} pixels beyond tolerance {args.tolerance}")
    return 1 if outliers > args.outliers else 0


if __name__ == "__main__":
    sys.exit(main())
