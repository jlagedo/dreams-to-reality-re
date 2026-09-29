"""Check every collider's candidate lists: python colliders.py <dump> <arena-hex>"""

import struct
import sys
from collections import Counter

from mdmp import Dump

d = Dump(sys.argv[1], int(sys.argv[2], 16))
W = 0x66E01C


def tag(p):
    return d.u32(p - 4)


ntri, ncol = d.u32(W), d.u32(W + 0x40C)
print(f"triangles {ntri}, colliders {ncol}")
for i in range(ncol or 0):
    col = d.u32(W + 0x10 + 4 * i)
    if not col:
        continue
    flags = d.read(col + 0x10, 1)[0]
    seen, stack, full = set(), [d.u32(col + 0x2C)], 0
    while stack:
        r = stack.pop()
        if not r or r in seen or len(seen) > 100000:
            continue
        seen.add(r)
        w = d.dwords(r, 6)
        if w is None:
            break
        full += (w[1] & 7) == 7
        stack += [w[2], w[3]]
    for lst in (0x30, 0x34):
        nodes, n = [], d.u32(col + lst)
        while n and len(nodes) < 100000:
            w = d.dwords(n, 3)
            if w is None:
                break
            nodes.append((n, w[0]))
            n = w[1]
        slot = 0x10 if lst == 0x30 else 0x14
        orph = sum(1 for n, r in nodes if d.u32(r + slot) != n)
        dup = sum(c - 1 for c in Counter(r for _, r in nodes).values() if c > 1)
        bad = sum(1 for _, r in nodes if tag(r) != 0x21)
        if lst == 0x30:
            c = ",".join(str(struct.unpack("<i", d.read(col + 4 * k, 4))[0]) for k in range(3))
            head = (
                f"collider {col:08X} flags {flags:#x} c=({c}) r={d.u32(col + 0xC)} "
                f"tree {len(seen)} (all-3 {full})"
            )
            print(head)
        print(
            f"    {'wall ' if lst == 0x30 else 'floor'} list {len(nodes)} nodes, orphans {orph}, "
            f"duplicate links {dup}, not a live record {bad}"
        )
