"""Inject a source environment flag and normal change into an isolated muted child.

The production adapter must update guest UVs without scene readback. This is
controlled integration evidence, not a claim about natural environment assets.
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
    run = recomp_env.out_dir("windream", "run-direct-environment")
    capture = run / "input.wds"
    capture.unlink(missing_ok=True)
    env = dict(
        os.environ,
        WD_RENDERER="direct",
        WD_MUTE="1",
        WD_HEADLESS="1",
        WD_READ_ROOTS=read_roots(),
        WD_FPS="25",
        WD_SCALE="1",
        WD_FOCUS="1",
        WD_QUIET="1",
        WD_DUMP="mini",
        WD_SNAP_MS="5000",
        WD_SCENE_CAPTURE=str(capture),
        WD_KEYS="2000:ESC,5000:RETURN,8000:ESC,20000:ESC",
    )
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [c.c_uint32, c.c_int, c.c_uint32]
    kernel.OpenProcess.restype = c.c_void_p
    kernel.CloseHandle.argtypes = [c.c_void_p]
    user = c.WinDLL("user32", use_last_error=True)
    window_callback = c.WINFUNCTYPE(c.c_int, c.c_void_p, c.c_ssize_t)
    user.EnumWindows.argtypes = [window_callback, c.c_ssize_t]
    user.GetWindowThreadProcessId.argtypes = [c.c_void_p, c.POINTER(c.c_uint32)]
    user.IsWindowVisible.argtypes = [c.c_void_p]
    for name in ("ReadProcessMemory", "WriteProcessMemory"):
        getattr(kernel, name).argtypes = [
            c.c_void_p,
            c.c_void_p,
            c.c_void_p,
            c.c_size_t,
            c.POINTER(c.c_size_t),
        ]
        getattr(kernel, name).restype = c.c_int
    report = {"passed": False, "controlled_inputs": "node environment flag and one corner normal"}
    log = run / "stderr.txt"
    with log.open("wb") as err, (run / "stdout.txt").open("wb") as out:
        process = subprocess.Popen(
            [
                str(recomp_env.out_dir("windream", "build-audit") / "windream_recomp.exe"),
                str(paths.disc(1) / "GDIDREAM.EXE"),
                "--run",
            ],
            cwd=run,
            env=env,
            stdout=out,
            stderr=err,
        )
        handle = kernel.OpenProcess(0x38, 0, process.pid)
        if not handle:
            process.kill()
            process.wait()
            raise c.WinError(c.get_last_error())
        arena = None

        def read(address, count):
            data, done = c.create_string_buffer(count), c.c_size_t()
            assert kernel.ReadProcessMemory(handle, arena + address, data, count, c.byref(done))
            assert done.value == count
            return data.raw

        def write(address, raw):
            data, done = c.create_string_buffer(raw), c.c_size_t()
            assert kernel.WriteProcessMemory(handle, arena + address, data, len(raw), c.byref(done))
            assert done.value == len(raw)

        def word(address):
            return int.from_bytes(read(address, 4), "little")

        start, transition, stage = time.monotonic(), 0, 0
        try:
            while time.monotonic() - start < 50:
                elapsed = time.monotonic() - start
                text = log.read_text(errors="replace")
                assert process.poll() is None, "child exited"
                assert "[direct] FATAL" not in text and "=== recomp: CRASH" not in text
                match = re.search(r"reserved at host ([0-9A-Fa-f]+)", text)
                if match:
                    arena = int(match[1], 16)
                if stage == 0 and arena and elapsed > 24 and capture.exists():
                    _, nodes, _, faces = read_snapshot(capture)
                    visible = []

                    @window_callback
                    def window_state(hwnd, _context, states=visible):
                        pid = c.c_uint32()
                        user.GetWindowThreadProcessId(hwnd, c.byref(pid))
                        if pid.value == process.pid:
                            states.append(bool(user.IsWindowVisible(hwnd)))
                        return 1

                    assert user.EnumWindows(window_state, 0)
                    assert visible and not any(visible), "headless child has a visible window"
                    report["native_window_visibility_verified"] = True
                    counts = Counter(
                        f["owner"]
                        for f in faces
                        if nodes[f["owner"]]["active"] and f["kind"] in (2, 3)
                    )
                    owner = counts.most_common(1)[0][0]
                    node = nodes[owner]["address"]
                    selected = [f for f in faces if f["owner"] == owner and f["kind"] in (2, 3)]
                    uv_addresses = sorted(
                        {
                            word(f["address"] + 0x34 + corner * 4)
                            for f in selected
                            for corner in range(3)
                        }
                    )
                    before = [read(a, 8) for a in uv_addresses]
                    assert not word(node + 12) & 0x800
                    write(node + 12, struct.pack("<I", word(node + 12) | 0x800))
                    report.update(node=hex(node), faces=len(selected), uv_pairs=len(uv_addresses))
                    transition, stage = elapsed, 1
                elif stage == 1 and elapsed - transition > 4:
                    after = [read(a, 8) for a in uv_addresses]
                    changed = sum(a != b for a, b in zip(before, after, strict=True))
                    assert changed, "environment flag did not update UVs"
                    assert re.search(r"environment_uv_writes=[1-9]", text)
                    report["changed_uv_pairs"] = changed
                    changed_uvs = {
                        address
                        for address, a, b in zip(uv_addresses, before, after, strict=True)
                        if a != b
                    }
                    normal = next(
                        word(f["address"] + 12 + corner * 12)
                        for f in selected
                        for corner in range(3)
                        if word(f["address"] + 0x34 + corner * 4) in changed_uvs
                    )
                    xyz = struct.unpack("<3i", read(normal, 12))
                    rotation = struct.unpack("<9i", read(word(node + 0x10) + 0x58, 36))
                    oracle = c.CDLL(
                        str(
                            recomp_env.out_dir("direct-render", "build")
                            / "WDSceneAdapterOracle.dll"
                        )
                    )
                    uv_fn = oracle.wd_environment_uv
                    uv_fn.argtypes = [
                        c.POINTER(c.c_int32),
                        c.POINTER(c.c_int32),
                        c.c_uint,
                        c.POINTER(c.c_int32),
                    ]

                    def mapped(n, rotation=rotation, uv_fn=uv_fn):
                        uv = (c.c_int32 * 2)()
                        assert uv_fn((c.c_int32 * 9)(*rotation), (c.c_int32 * 3)(*n), 0, uv)
                        return tuple(uv)

                    # Choose a quarter-turn that changes the projected normal;
                    # a normal parallel to the chosen rotation axis is unchanged.
                    rotated = next(
                        n
                        for n in (
                            (-xyz[1], xyz[0], xyz[2]),
                            (xyz[2], xyz[1], -xyz[0]),
                            (xyz[0], -xyz[2], xyz[1]),
                        )
                        if mapped(n) != mapped(xyz)
                    )
                    write(normal, struct.pack("<3i", *rotated))
                    transition, stage = elapsed, 2
                elif stage == 2 and elapsed - transition > 4:
                    moved = [read(a, 8) for a in uv_addresses]
                    changed = sum(a != b for a, b in zip(after, moved, strict=True))
                    assert changed, "normal change did not update environment UVs"
                    report["changed_uv_pairs_after_normal_rotation"] = changed
                    write(node + 12, struct.pack("<I", word(node + 12) & ~0x800))
                    transition, stage = elapsed, 3
                    events = len(re.findall(r"environment_uv_writes=[1-9]", text))
                elif stage == 3 and elapsed - transition > 3:
                    assert len(re.findall(r"environment_uv_writes=[1-9]", text)) <= events + 1
                    assert "routine_readbacks=0" in text
                    assert (
                        "WD_MUTE: mixing silently" in text
                        and "WD_HEADLESS: window remains hidden" in text
                    )
                    report.update(passed=True, routine_readbacks=0, muted=True, hidden=True)
                    break
                time.sleep(0.1)
            assert report["passed"], "environment sequence timed out"
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            kernel.CloseHandle(handle)
            (run / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
