"""Count face block types (primitive_type) over Guardian's .3dc files with the Dreams node reader.

uv run python re/guardian/tools/face_types.py [RESDIR]
"""

import collections
import sys
from pathlib import Path

from dreams.formats import node

REPO = Path(__file__).resolve().parents[3]
res = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "out" / "guardian" / "res" / "game"
faces = collections.Counter()
files = collections.Counter()
for p in sorted(res.rglob("*")):
    if p.suffix.lower() != ".3dc":
        continue
    try:
        m = node.read_3dc(p)
    except Exception:  # noqa: BLE001 - count what decodes
        continue
    seen = set()
    for f in m.faces:
        faces[f.primitive_type] += 1
        seen.add(f.primitive_type)
    for t in seen:
        files[t] += 1
for t in sorted(faces):
    print(f"type {t:4d}: {faces[t]:7d} faces in {files[t]:4d} files")
