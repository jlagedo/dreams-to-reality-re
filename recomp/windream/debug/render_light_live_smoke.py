"""Controlled radial light/binding in an isolated live direct-render child.

Uses a captured submitted node; does not emulate an unmodified actor-effect
input route. The retained retail view transform sees the same active prefix.
"""

import ctypes as c
import json
import os
import re
import struct
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

from render_scene_smoke import read_snapshot

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run import read_roots  # noqa: E402

from dreams import paths  # noqa: E402


def main():
    run = recomp_env.out_dir("windream", "run-direct-radial-light")
    capture = run / "input.wds"
    capture.unlink(missing_ok=True)
    env = dict(
        os.environ,
        WD_RENDERER="direct",
        WD_READ_ROOTS=read_roots(),
        WD_FPS="25",
        WD_SCALE="1",
        WD_FOCUS="1",
        WD_QUIET="1",
        WD_DUMP="mini",
        WD_SNAP_MS="2000",
        WD_SCENE_CAPTURE=str(capture),
        WD_KEYS="2000:ESC,5000:RETURN,8000:ESC,20000:ESC",
    )
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [c.c_uint32, c.c_int, c.c_uint32]
    kernel.OpenProcess.restype = c.c_void_p
    kernel.CloseHandle.argtypes = [c.c_void_p]
    for name in ("ReadProcessMemory", "WriteProcessMemory"):
        fn = getattr(kernel, name)
        fn.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
        fn.restype = c.c_int
    report = {
        "passed": False,
        "controlled_inputs": "one radial light and one submitted node binding",
    }
    log = run / "stderr.txt"
    with log.open("wb") as err, (run / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(
            [
                str(recomp_env.out_dir("windream", "build-audit") / "windream_recomp.exe"),
                str(paths.disc(1) / "GDIDREAM.EXE"),
                "--run",
            ],
            cwd=run,
            env=env,
            stdout=stdout,
            stderr=err,
        )
        handle = kernel.OpenProcess(0x38, 0, process.pid)
        if not handle:
            process.kill()
            process.wait()
            raise c.WinError(c.get_last_error())
        arena = None
        stage = 0
        start = time.monotonic()
        transition = 0

        def read(address, size):
            buf = c.create_string_buffer(size)
            done = c.c_size_t()
            assert kernel.ReadProcessMemory(handle, arena + address, buf, size, c.byref(done)), (
                c.get_last_error()
            )
            assert done.value == size
            return buf.raw

        def word(address):
            return int.from_bytes(read(address, 4), "little")

        def write(address, data):
            buf = c.create_string_buffer(data)
            done = c.c_size_t()
            assert kernel.WriteProcessMemory(
                handle, arena + address, buf, len(data), c.byref(done)
            ), c.get_last_error()
            assert done.value == len(data)

        try:
            while time.monotonic() - start < 55:
                text = log.read_text(errors="replace")
                elapsed = time.monotonic() - start
                assert process.poll() is None, "child exited"
                assert "[direct] FATAL" not in text and "=== recomp: CRASH" not in text, (
                    "child failed; inspect stderr.txt"
                )
                match = re.search(r"reserved at host ([0-9A-Fa-f]+)", text)
                if match:
                    arena = int(match[1], 16)
                if stage == 0 and arena and elapsed > 24 and capture.exists():
                    _, nodes, _, faces = read_snapshot(capture)
                    counts = Counter(
                        f["owner"]
                        for f in faces
                        if nodes[f["owner"]]["active"]
                        and f["kind"] in (2, 3, 9, -3, -4, -5, -6, -7)
                    )
                    owner = counts.most_common(1)[0][0]
                    node = nodes[owner]["address"]
                    assert word(node + 0xC4) == 0 and word(0x4AC758) == 0 and word(0x672700) == 0
                    assert word(0x4AA704) == 0, "lit frame callback requires separate validation"
                    selected = [
                        f
                        for f in faces
                        if f["owner"] == owner and f["kind"] in (2, 3, 9, -3, -4, -5, -6, -7)
                    ]
                    shade_addresses = [f["address"] + 0x40 for f in selected]
                    normal_addresses = sorted({f["normal_address"] + 12 for f in selected})
                    before_shades = [read(a, 1) for a in shade_addresses]
                    before_dots = [read(a, 4) for a in normal_addresses]
                    original = [
                        (node + 0xC4, read(node + 0xC4, 12)),
                        (0x672700, read(0x672700, 0x94)),
                        (0x4AC758, read(0x4AC758, 4)),
                    ]
                    position = list(struct.unpack("<3i", read(word(0x661EE8) + 0x1C, 12)))
                    record = bytearray(0x94)
                    struct.pack_into("<I3i", record, 0, 1, *position)
                    struct.pack_into("<9i", record, 0x10, 32768, 0, 0, 0, 32768, 0, 0, 0, 32768)
                    struct.pack_into("<3i", record, 0x88, 60000, 60000, 31)
                    write(0x672700, bytes(record))
                    write(0x4AC758, struct.pack("<I", 1))
                    write(node + 0xC8, bytes(8))
                    write(node + 0xC4, struct.pack("<I", 1))
                    report.update(node=hex(node), faces=len(selected))
                    stage = 1
                    transition = elapsed
                elif stage == 1 and elapsed - transition > 5:
                    after_shades = [read(a, 1) for a in shade_addresses]
                    after_dots = [read(a, 4) for a in normal_addresses]
                    changed = sum(a != b for a, b in zip(before_shades, after_shades, strict=True))
                    assert changed > 0 and any(
                        a != b for a, b in zip(before_dots, after_dots, strict=True)
                    ), "no lighting metadata changed"
                    assert "radial_lighting writes=" in text
                    report["changed_face_shades"] = changed
                    position = [
                        value + delta
                        for value, delta in zip(position, (200, 300, 400), strict=True)
                    ]
                    write(0x672704, struct.pack("<3i", *position))
                    stage = 2
                    transition = elapsed
                elif stage == 2 and elapsed - transition > 5:
                    moved = [read(a, 4) for a in normal_addresses]
                    changed = sum(a != b for a, b in zip(after_dots, moved, strict=True))
                    assert changed > 0, "moving light did not update normals"
                    report["changed_normal_dots_after_move"] = changed
                    write(node + 0xC4, original[0][1])
                    for address, data in original[1:]:
                        write(address, data)
                    stage = 3
                    transition = elapsed
                    count_before = text.count("radial_lighting writes=")
                elif stage == 3 and elapsed - transition > 3:
                    assert text.count("radial_lighting writes=") <= count_before + 1, (
                        "lighting persisted after unbind"
                    )
                    assert "routine_readbacks=0" in text
                    report["passed"] = True
                    report["routine_readbacks"] = 0
                    break
                time.sleep(0.1)
            assert report["passed"], "lighting sequence timed out"
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            kernel.CloseHandle(handle)
            (run / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
