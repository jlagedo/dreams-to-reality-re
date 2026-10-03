"""Run the Dreams .3DC/.3DM readers over Guardian's extracted files and report what decodes.

uv run python re/guardian/tools/probe_3d.py [RESDIR]
"""

from __future__ import annotations

import collections
import sys
from pathlib import Path

from dreams.formats import node

REPO = Path(__file__).resolve().parents[3]
res = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "out" / "guardian" / "res" / "game"


def head(p: Path, n: int = 8) -> bytes:
    with p.open("rb") as f:
        return f.read(n)


for ext in (".3dc", ".3dm", ".3da", ".3di"):
    files = sorted(p for p in res.rglob("*") if p.suffix.lower() == ext)
    magics = collections.Counter(head(p, 4) for p in files)
    sizes = collections.Counter(p.stat().st_size for p in files)
    print(
        f"{ext}: {len(files)} files; leading bytes {magics.most_common(4)};"
        f" commonest sizes {sizes.most_common(3)}"
    )

c3 = sorted(p for p in res.rglob("*") if p.suffix.lower() == ".3dc")
ok = nodes = faces = 0
fail = collections.Counter()
for p in c3:
    try:
        m = node.read_3dc(p)
    except Exception as e:  # noqa: BLE001 - tally, not stop
        fail[type(e).__name__ + ": " + str(e)[:60]] += 1
        continue
    if m.nodes:
        ok += 1
        nodes += len(m.nodes)
        faces += len(m.faces)
    else:
        fail["no nodes found"] += 1
print(
    f".3dc via read_3dc: {ok}/{len(c3)} with nodes, {nodes} nodes, {faces} faces;"
    f" failures {fail.most_common(5)}"
)

m3 = sorted(p for p in res.rglob("*") if p.suffix.lower() == ".3dm")
good = 0
bad = collections.Counter()
for p in m3:
    size = p.stat().st_size - node.TEX_3DM_SKIP
    if size == node.TEX_TOTAL:
        good += 1
    else:
        bad[size] += 1
print(
    f".3dm: {good}/{len(m3)} have exactly the Dreams bank size {node.TEX_TOTAL};"
    f" other sizes {bad.most_common(5)}"
)
