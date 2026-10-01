import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "verify"))
from mdmp import Dump  # noqa: E402

d = Dump(sys.argv[1], int(sys.argv[2], 16))
W = 0x66E01C
n = d.u32(W)
print("triangles", n)
for axis in range(3):
    arr = d.u32(W + 4 + axis * 4)
    raw = d.read(arr, n * 2 * 8)
    vals = []
    for i in range(2 * n):
        tri, ismax = struct.unpack_from("<IB", raw, i * 8)
        v = struct.unpack("<i", d.read(tri + (0x54 if ismax else 0x48) + axis * 4, 4))[0]
        vals.append((v, ismax, tri))
    inv = [i for i in range(1, len(vals)) if vals[i][0] < vals[i - 1][0]]
    ties = sum(1 for i in range(1, len(vals)) if vals[i][0] == vals[i - 1][0])
    flat = sum(
        1
        for i in range(len(vals))
        if not vals[i][1]
        and d.u32(vals[i][2] + 0x48 + axis * 4) == d.u32(vals[i][2] + 0x54 + axis * 4)
    )
    print(
        f"axis {axis}: {len(vals)} endpoints, out of order {len(inv)}, "
        f"equal neighbours {ties}, flat triangles {flat}",
        [(i, vals[i - 1][0], vals[i][0]) for i in inv[:5]],
    )
