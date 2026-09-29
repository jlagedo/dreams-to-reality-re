"""Drive retail water transitions in an isolated direct-render child process.

Controlled inputs: project fog fields and the water-height plane. Actor mode is
never patched: the lifted ENT_UpdateSwimming controller must change it and call
the production fog boundary. This is not an unmodified gameplay-route claim.
"""

import ctypes as c
import json
import math
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run import read_roots  # noqa: E402

from dreams import paths  # noqa: E402


def main():
    run = recomp_env.out_dir("windream", "run-direct-fog-transitions")
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
        "controlled_inputs": "project fog fields and water-height plane",
        "events": [],
    }
    env = dict(
        os.environ,
        WD_RENDERER="direct",
        WD_READ_ROOTS=read_roots(),
        WD_FPS="25",
        WD_SCALE="1",
        WD_FOCUS="1",
        WD_QUIET="1",
        WD_DUMP="mini",
        WD_SNAP_MS="1000",
        WD_KEYS="2000:ESC,5000:RETURN,8000:ESC,20000:ESC",
    )
    log = run / "stderr.txt"
    with log.open("wb") as err, (run / "stdout.txt").open("wb") as stdout:
        p = subprocess.Popen(
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
        handle = kernel.OpenProcess(0x38, 0, p.pid)
        if not handle:
            p.kill()
            p.wait()
            raise c.WinError(c.get_last_error())
        arena = None
        start = time.monotonic()
        stage = 0
        event_time = 0
        expected = 1

        def read(address, count):
            buffer = c.create_string_buffer(count)
            done = c.c_size_t()
            assert kernel.ReadProcessMemory(
                handle, arena + address, buffer, count, c.byref(done)
            ), c.get_last_error()
            assert done.value == count
            return buffer.raw

        def word(address):
            return int.from_bytes(read(address, 4), "little")

        def write(address, data):
            buffer = c.create_string_buffer(data)
            done = c.c_size_t()
            assert kernel.WriteProcessMemory(
                handle, arena + address, buffer, len(data), c.byref(done)
            ), c.get_last_error()
            assert done.value == len(data)

        def plane(enter):
            y = struct.unpack("<d", read(0x4FBA80, 8))[0]
            height = math.floor(y) + (-1000 if enter else 1000)
            data = struct.pack("<i", height)
            write(project + 0xD4, data)
            write(0x5E5490, data)

        try:
            while time.monotonic() - start < 55:
                text = log.read_text(errors="replace")
                assert p.poll() is None, "child exited; inspect stderr.txt"
                assert not any(s in text for s in ("=== recomp: CRASH", "[direct] FATAL")), (
                    "child failed; inspect stderr.txt"
                )
                match = re.search(r"reserved at host ([0-9A-Fa-f]+)", text)
                if match:
                    arena = int(match[1], 16)
                events = re.findall(
                    r"fog_update=(\d+) colour=([0-9a-f]+) density=([^ ]+) phase=([^ ]+) mode=(\d+)",
                    text,
                )
                elapsed = time.monotonic() - start
                scene_count = max(
                    (int(value) for value in re.findall(r"\[direct\] scene=(\d+)", text)), default=0
                )
                if stage == 0 and arena and elapsed > 24 and "scene=100 " in text:
                    assert len(events) == 1, events
                    assert word(0x4FBAAC) in (1, 3), "player is not in a dry movement mode"
                    assert read(0x4FBB20, 1)[0] & 4, "player lacks retail fog-update flag"
                    project = word(0x661E04)
                    original = [
                        (project + 0xD4, read(project + 0xD4, 4)),
                        (project + 0x1C0, read(project + 0x1C0, 16)),
                        (0x5E5490, read(0x5E5490, 4)),
                    ]
                    write(project + 0x1C0, struct.pack("<4i", 42, 76, 158, 64))
                    plane(True)
                    expected = 2
                    stage = 1
                if stage in (1, 3, 5, 7) and len(events) >= expected:
                    assert len(events) == expected, "unexpected repeated fog updates"
                    _, colour, density, phase, mode = events[-1]
                    density, phase, mode = float(density), float(phase), int(mode)
                    if stage == 1:
                        assert colour == "2a4c9e" and abs(density - 0.0004) < 1e-9 and mode == 2
                        assert phase == 0
                    elif stage == 5:
                        assert colour == "006080" and 0.00014 <= density <= 0.00022 and mode == 2
                        assert phase != 0
                        assert abs(density - (math.sin(phase) * 0.00004 + 0.00018)) < 1e-10
                    else:
                        assert colour == "000000" and density == 0 and mode == 1
                    report["events"].append(
                        dict(
                            stage=stage,
                            elapsed=elapsed,
                            colour=colour,
                            density=density,
                            phase=phase,
                            mode=mode,
                        )
                    )
                    event_time = elapsed
                    scene_at_event = scene_count
                    stage += 1
                if stage in (2, 4, 6, 8):
                    assert len(events) == expected, "fog advanced without another water transition"
                    if (
                        elapsed - event_time > (4 if stage in (2, 6) else 2)
                        and scene_count > scene_at_event
                    ):
                        report["events"][-1]["scene_submissions_during_hold_at_least"] = (
                            scene_count - scene_at_event
                        )
                        if stage == 8:
                            for address, data in original:
                                write(address, data)
                            report["passed"] = True
                            break
                        if stage == 2:
                            write(project + 0x1CC, struct.pack("<i", 0))
                        plane(stage == 4)
                        stage += 1
                        expected += 1
                time.sleep(0.05)
            assert report["passed"], "water transition sequence timed out"
            assert "routine_readbacks=0" in text
            report["fog_updates"] = len(events)
            report["routine_readbacks"] = 0
        finally:
            if p.poll() is None:
                p.kill()
            p.wait()
            kernel.CloseHandle(handle)
            (run / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
