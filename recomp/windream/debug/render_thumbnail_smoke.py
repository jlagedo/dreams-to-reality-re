"""Trigger the retail autosave controller in a live, isolated direct-render run.

This is a controlled-state smoke, not an unmodified gameplay-route claim. After
the level has started, set its autosave policy (+0x1f8) to zero and rearm
GAME_StartLevel (0x4a0fd0). All saving, thumbnail rendering and viewport restore
then execute through the production lifted calls. Restore the policy afterward.
Only this child process and its output sandbox are modified.
"""

import argparse
import ctypes as c
import json
import os
import re
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--load", action="store_true", help="also load the generated save")
    args = parser.parse_args()
    root = recomp_env.out_dir("windream")
    run = root / "run-direct-thumbnail-smoke"
    run.mkdir(exist_ok=True)
    env = dict(
        os.environ,
        WD_RENDERER="direct",
        WD_READ_ROOTS=read_roots(),
        WD_KEYS="2000:ESC,5000:RETURN,8000:ESC,20000:ESC,28000:ESC,30000:LEFT,32000:RETURN,34000:RETURN",
        WD_SNAP_MS="5000",
        WD_FPS="25",
        WD_SCALE="1",
        WD_DUMP="mini",
        WD_QUIET="1",
        WD_FOCUS="1",
    )
    if args.load:
        env["WD_KEYS"] += ",42000:RETURN,52000:ESC"
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [c.c_uint32, c.c_int, c.c_uint32]
    kernel.OpenProcess.restype = c.c_void_p
    for name in ("ReadProcessMemory", "WriteProcessMemory"):
        fn = getattr(kernel, name)
        fn.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
        fn.restype = c.c_int
    kernel.CloseHandle.argtypes = [c.c_void_p]
    report = {
        "passed": False,
        "controlled_state": "project+0x1f8=0; rearm GAME_StartLevel",
        "checks": {},
    }
    log_path = run / "stderr.txt"
    with log_path.open("wb") as err, (run / "stdout.txt").open("wb") as out:
        process = subprocess.Popen(
            [
                str(root / "build-audit/windream_recomp.exe"),
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
            raise c.WinError(c.get_last_error())
        start = time.monotonic()
        arena = None
        triggered = restored = False

        def read(address, size):
            data = c.create_string_buffer(size)
            count = c.c_size_t()
            if not kernel.ReadProcessMemory(handle, arena + address, data, size, c.byref(count)):
                raise c.WinError(c.get_last_error())
            assert count.value == size
            return data.raw

        def word(address):
            return int.from_bytes(read(address, 4), "little")

        def write(address, value):
            data = c.c_uint32(value)
            count = c.c_size_t()
            if not kernel.WriteProcessMemory(
                handle, arena + address, c.byref(data), 4, c.byref(count)
            ):
                raise c.WinError(c.get_last_error())
            assert count.value == 4

        try:
            while time.monotonic() - start < (60 if args.load else 43):
                text = log_path.read_text(errors="replace")
                assert process.poll() is None, "game exited early"
                assert "=== recomp: CRASH" not in text, "game crashed; inspect stderr.txt"
                if arena is None:
                    match = re.search(r"reserved at host ([0-9A-Fa-f]+)", text)
                    if match:
                        arena = int(match[1], 16)
                if arena and not triggered and "[direct] scene=50 " in text and word(0x4A0FD0) == 1:
                    project = word(0x661E04)
                    policy = word(project + 0x1F8)
                    before = [word(va) for va in (0x661EBC, 0x661EC8)]
                    report.update(
                        project=hex(project), original_policy=policy, viewport_before=before
                    )
                    write(project + 0x1F8, 0)
                    write(0x4A0FD0, 0)
                    triggered = True
                if (
                    triggered
                    and not restored
                    and "explicit readback reason=thumbnail" in text
                    and word(0x4A0FD0) == 1
                ):
                    write(project + 0x1F8, policy)
                    restored = True
                    raw = read(0x5D6B98, 8192)
                    copy = read(0x5D8B98, 8192)
                    icons = list((run / "sandbox").rglob("*.ico"))
                    matching = [p for p in icons if p.read_bytes() == raw]
                    assert matching, "no serialized icon matches rendered guest bytes"
                    assert raw == copy, "retail thumbnail copy differs"
                    assert len(set(raw)) > 16, "thumbnail is empty or nearly constant"
                    after = [word(va) for va in (0x661EBC, 0x661EC8)]
                    assert before == after, "screen dimensions were not restored"
                    report.update(
                        icon=str(matching[0]), viewport_after=after, packed_format=word(0x49DA1C)
                    )
                    report["checks"].update(
                        serialized_bytes=8192, guest_copy_equal=True, screen_restored=True
                    )
                time.sleep(0.1)
            assert restored, "autosave/export did not complete"
            text = log_path.read_text(errors="replace")
            assert text.count("explicit readback reason=thumbnail") == 1
            assert "routine_readbacks=0" in text
            assert "[direct] scene=100 " in text, "no continued scene submission after export"
            icon_name = Path(report["icon"]).name
            assert any(
                "open R " in line and icon_name in line and "sandbox" in line
                for line in text.splitlines()
            ), "generated icon not read by load menu"
            report["checks"]["menu_reads_generated_icon"] = True
            if args.load:
                save_name = Path(icon_name).with_suffix(".dat").name
                assert any(
                    "open R " in line and save_name in line and "sandbox" in line
                    for line in text.splitlines()
                ), "generated save not read"
                report["checks"]["load_reads_generated_save"] = True
                load_at = next(
                    i
                    for i, line in enumerate(text.splitlines())
                    if "open R " in line and save_name in line and "sandbox" in line
                )
                after_load = "\n".join(text.splitlines()[load_at + 1 :])
                assert re.search(r"\[direct\] scene=\d+ ", after_load), "no scene after load"
                report["checks"]["scene_after_load"] = True
            report["checks"].update(thumbnail_exports=1, resumed_frames=True)
            report["passed"] = True
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            kernel.CloseHandle(handle)
            (run / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
