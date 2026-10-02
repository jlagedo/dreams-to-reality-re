# ruff: noqa: E501
"""Build the mock engine and a fake demo pack under out/recomp/web/mock/.

    uv run python recomp/web/mock/build_mock.py          # engine (needs emcc on PATH, see recomp/web-env.ps1) + pack

engine/dreams.{js,wasm,...}  from mock_dreams.c with the flags the real build is expected to use
demo/                        manifest.json and chunks: random files, one split in three chunks
Then `recomp/web/package.py --engine out/recomp/web/mock/engine --demo out/recomp/web/mock/demo
--out out/recomp/web/mock/dist` makes the folder the page test serves.
"""

from __future__ import annotations

import hashlib
import json
import random
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
OUT = REPO / "out" / "recomp" / "web" / "mock"


def build_engine(out: Path) -> None:
    emcc = shutil.which("emcc") or shutil.which("emcc.bat")
    if not emcc:
        sys.exit("emcc not found: activate the browser tools first (. ./recomp/web-env.ps1)")
    out.mkdir(parents=True, exist_ok=True)
    cmd = [
        emcc,
        str(HERE / "mock_dreams.c"),
        "-o",
        str(out / "dreams.js"),
        "-O1",
        "-pthread",
        "-sPTHREAD_POOL_SIZE=2",
        "-sMODULARIZE",
        "-sEXPORT_NAME=createDreams",
        "-sENVIRONMENT=web,worker",
        "-sEXPORTED_RUNTIME_METHODS=FS,ENV,addRunDependency,removeRunDependency",
        "-lidbfs.js",
        "-sFORCE_FILESYSTEM",
        "-sALLOW_MEMORY_GROWTH",
        "-sEXIT_RUNTIME=0",
    ]
    subprocess.run(cmd, check=True)


def build_pack(out: Path, chunk: int = 40000, version: int = 1, tag: str = "mock") -> None:
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    rng = random.Random(1234 + version)
    spec = [
        ("DREAMS.DAT", 100_000),
        ("DATA/LEVEL/L1.BIN", 12_345),
        ("SMALL.TXT", 17),
        ("DATA/GAME/GAME1.DAT", 2000),
    ]
    files, total = [], 0
    for path, size in spec:
        data = bytes(rng.getrandbits(8) for _ in range(size))
        chunks = []
        for i in range(0, size, chunk):
            part = data[i : i + chunk]
            name = f"{path.replace('/', '_')}.{i // chunk:03d}"
            (out / name).write_bytes(part)
            chunks.append({"url": name, "size": len(part)})
        files.append(
            {
                "path": path,
                "size": size,
                "sha256": hashlib.sha256(data).hexdigest(),
                "chunks": chunks,
            }
        )
        total += size
    (out / "manifest.json").write_text(
        json.dumps({"version": version, "name": tag, "total": total, "files": files}),
        encoding="utf-8",
    )


def main() -> int:
    build_engine(OUT / "engine")
    build_pack(OUT / "demo")
    print(f"mock engine and pack in {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
