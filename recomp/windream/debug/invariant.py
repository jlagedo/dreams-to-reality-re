"""Per collider and axis: record bits vs brute-force overlap.

usage: python invariant.py <dump> <arena-hex>
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "verify"))
from mdmp import Dump  # noqa: E402

d = Dump(sys.argv[1], int(sys.argv[2], 16))


def s32(va):
    return struct.unpack("<i", d.read(va, 4))[0]


W = 0x66E01C
n = d.u32(W)
arr0 = d.u32(W + 4)
tris = sorted({d.u32(arr0 + 8 * i) for i in range(2 * n)})
bounds = {
    t: ([s32(t + 0x48 + 4 * a) for a in range(3)], [s32(t + 0x54 + 4 * a) for a in range(3)])
    for t in tris
}
for i in range(d.u32(W + 0x40C)):
    col = d.u32(W + 0x10 + 4 * i)
    c = [s32(col + 4 * a) for a in range(3)]
    r = s32(col + 0xC)
    bits, seen, stack = {}, set(), [d.u32(col + 0x2C)]
    while stack:
        x = stack.pop()
        if not x or x in seen:
            continue
        seen.add(x)
        w = d.dwords(x, 4)
        bits[w[0]] = w[1] & 0xFF
        stack += [w[2], w[3]]
    if not bits:
        continue
    out = []
    for a in range(3):
        L, H = c[a] - r, c[a] + r
        extra = missing = ties = 0
        for t, (mn, mx) in bounds.items():
            strict = mn[a] < H and mx[a] > L
            loose = mn[a] <= H and mx[a] >= L
            has = bool(bits.get(t, 0) & (1 << a))
            if strict != loose and has != strict:
                ties += 1
            elif has and not strict:
                extra += 1
            elif strict and not has:
                missing += 1
        out.append(f"axis{a}: extra {extra} missing {missing} ties {ties}")
    print(f"collider {col:08X} r={r:4} records {len(bits):5}  " + " | ".join(out))
