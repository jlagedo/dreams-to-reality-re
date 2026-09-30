"""Trigger the retail autosave controller in a live, isolated direct-render run.

This is a controlled-state smoke, not an unmodified gameplay-route claim. After
the level has started, set its autosave policy (+0x1f8) to zero and rearm
GAME_StartLevel (0x4a0fd0). All saving, thumbnail rendering and viewport restore
then execute through the production lifted calls. Restore the policy afterward.
Only this child process and its output sandbox are modified.

--reload-count 10 uses the real L load-menu hotkey and Return, waits for actual
save reads and resumed scene/resource checkpoints, and cancels only an observed
voice-caption phase with Escape. Keys are normal Win32 messages to this child's
SDL window; no load function is invoked or rearmed by the test. Use --snap-ms 0
for capture-free lifetime checks. Every invocation has a fresh output directory.
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
from uuid import uuid4

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run import read_roots  # noqa: E402

from dreams import paths  # noqa: E402


def reload_schedule(count):
    keys = "2000:ESC,5000:RETURN,8000:ESC,20000:ESC,28000:ESC,30000:LEFT,32000:RETURN,34000:RETURN"
    # Repeated load keys are sent only after observed menu/load checkpoints.
    if count:
        return "2000:ESC,5000:RETURN,8000:ESC,20000:ESC", 60 + count * 20
    return keys, 43


def child_key_sender(pid):
    """Post ordinary keyboard events only to this isolated child's SDL window."""
    user = c.WinDLL("user32", use_last_error=True)
    callback_type = c.WINFUNCTYPE(c.c_int, c.c_void_p, c.c_ssize_t)
    user.EnumWindows.argtypes = [callback_type, c.c_ssize_t]
    user.GetWindowThreadProcessId.argtypes = [c.c_void_p, c.POINTER(c.c_uint32)]
    user.GetClassNameW.argtypes = [c.c_void_p, c.c_wchar_p, c.c_int]
    user.PostMessageW.argtypes = [c.c_void_p, c.c_uint32, c.c_size_t, c.c_ssize_t]
    user.PostMessageW.restype = c.c_int
    user.MapVirtualKeyW.argtypes = [c.c_uint32, c.c_uint32]
    user.MapVirtualKeyW.restype = c.c_uint32

    def send(vk, down=True):
        windows = []

        @callback_type
        def visit(window, _):
            owner = c.c_uint32()
            user.GetWindowThreadProcessId(window, c.byref(owner))
            if owner.value == pid:
                name = c.create_unicode_buffer(256)
                user.GetClassNameW(window, name, len(name))
                if name.value == "SDL_app":
                    windows.append(window)
            return 1

        user.EnumWindows(visit, 0)
        assert windows, "isolated child has no native SDL window"
        # The runtime supports exactly one native game window.
        assert len(windows) == 1, f"ambiguous child windows: {windows}"
        scan = user.MapVirtualKeyW(vk, 0)
        assert scan, "key has no scan code"
        message = 0x100 if down else 0x101
        flags = 1 | (scan << 16) | (0 if down else 0xC0000000)
        assert user.PostMessageW(windows[0], message, vk, flags)

    return send


def caption_depth(text):
    events = re.findall(r"\[direct\] caption_scope=(?:begin|end) depth=(\d+)", text)
    return int(events[-1]) if events else 0


def load_page_ready(handler, page, slots, active_captions):
    return handler == 0x40E75C and page == 2 and slots == 1 and active_captions == 0


def caption_needs_cancel(depth, token, previous):
    return depth > 0 and token != previous


def caption_interrupts_navigation(phase, save_read):
    return phase in ("initial", "slots", "confirm", "recover") or (
        phase == "loading" and not save_read
    )


def resumed_reload(samples, caption_active, scene_floor):
    return (
        len(samples) >= 3
        and int(samples[-1][0]) - int(samples[0][0]) >= 25
        and samples[-1][1] == samples[-2][1]
        and caption_active == 0
        and int(samples[-1][0]) >= scene_floor + 25
    )


def reload_evidence(text, save_name, expected):
    """A menu/file read alone is insufficient: require resumed scene progression."""
    lines = text.splitlines()
    reads = [
        i
        for i, line in enumerate(lines)
        if line.startswith("[save] open R ")
        and save_name.casefold() in line.casefold()
        and "sandbox" in line
        and "(FAILED)" not in line
    ]
    assert len(reads) == expected, f"expected {expected} actual save reads, found {len(reads)}"
    cycles = []
    for number, begin in enumerate(reads):
        end = reads[number + 1] if number + 1 < len(reads) else len(lines)
        samples = [
            (int(match[1]), int(match[2]))
            for line in lines[begin + 1 : end]
            if (match := re.search(r"\[direct\] scene=(\d+) .*resources=(\d+)", line))
        ]
        assert len(samples) >= 2 and samples[-1][0] - samples[0][0] >= 25, (
            f"reload {number + 1} did not resume at least 25 scene submissions"
        )
        cycles.append(
            {
                "reload": number + 1,
                "save_read_log_line": begin + 1,
                "first_scene": samples[0][0],
                "last_scene": samples[-1][0],
                "resumed_scene_delta": samples[-1][0] - samples[0][0],
                "settled_resources": samples[-1][1],
                "peak_resources": max(value for _, value in samples),
            }
        )
    baseline = cycles[0]["settled_resources"] if cycles else None
    assert all(cycle["settled_resources"] <= baseline for cycle in cycles), (
        "settled resources grew beyond the first completed reload baseline",
        cycles,
    )
    return {
        "completed_reloads": len(cycles),
        "warmup": "first full reload",
        "resource_baseline": baseline,
        "cycles": cycles,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--load", action="store_true", help="also load the generated save")
    parser.add_argument(
        "--reload-count",
        type=int,
        choices=range(1, 11),
        help="perform 1..10 actual menu save loads in this process; implies --load",
    )
    parser.add_argument("--tag", help="fresh isolated run suffix; existing directories are refused")
    parser.add_argument("--snap-ms", type=int, default=5000, help="capture interval; 0 disables")
    args = parser.parse_args()
    if args.snap_ms < 0:
        parser.error("--snap-ms cannot be negative")
    if args.tag and not re.fullmatch(r"[A-Za-z0-9_-]+", args.tag):
        parser.error("--tag must contain only letters, numbers, underscores or hyphens")
    reload_count = args.reload_count or int(args.load)
    keys, duration = reload_schedule(reload_count)
    root = recomp_env.out_dir("windream")
    tag = args.tag or "direct-thumbnail-" + time.strftime("%Y%m%dT%H%M%S") + "-" + uuid4().hex[:8]
    run = root / ("run-" + tag)
    if run.exists():
        parser.error(f"refusing stale run directory: {run}; choose a new --tag")
    run.mkdir()
    env = dict(
        os.environ,
        WD_RENDERER="direct",
        WD_MUTE="1",
        WD_HEADLESS="1",
        WD_READ_ROOTS=read_roots(),
        WD_KEYS=keys,
        WD_SNAP_MS=str(args.snap_ms) if args.snap_ms else "",
        WD_FPS="25",
        WD_SCALE="1",
        WD_WIDTH="640",
        WD_HEIGHT="480",
        WD_RESIZE="",
        WD_MOUSE="",
        WD_POKE="",
        WD_DUMP="mini",
        WD_QUIET="1",
        WD_FOCUS="1",
    )
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
        "requested_reloads": reload_count,
        "run": str(run),
        "key_schedule": keys,
        "snapshot_interval_ms": args.snap_ms,
        "input_route": "native L hotkey -> observed load-slot page -> native Return",
        "input_events": [],
        "key_acknowledgements": [],
        "observed_completed_reloads": 0,
        "reload_checkpoints": [],
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
        send_key = child_key_sender(process.pid)
        phase, completed, marker = "initial", 0, 0
        cancelled_caption = None
        resume_scene_floor = 0
        recovery_scene_floor = 0
        gameplay_handler = None
        held = set()
        phase_start = start

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

        def press(vk):
            began = time.monotonic()
            send_key(vk)
            held.add(vk)
            while not (read(0x6308D8 + vk, 1)[0] & 1):
                assert process.poll() is None, "game exited during key acknowledgement"
                assert time.monotonic() - began < 15, f"guest did not acknowledge key {vk:x} down"
                time.sleep(0.01)
            down_ack = time.monotonic()
            remaining = 0.15 - (down_ack - began)
            if remaining > 0:
                time.sleep(remaining)
            release(vk)
            while read(0x6308D8 + vk, 1)[0] & 1:
                assert process.poll() is None, "game exited during key release"
                assert time.monotonic() - began < 15, f"guest did not acknowledge key {vk:x} up"
                time.sleep(0.01)
            report["key_acknowledgements"].append(
                {
                    "vk": vk,
                    "seconds": began - start,
                    "down_ack_ms": (down_ack - began) * 1000,
                    "up_ack_ms": (time.monotonic() - began) * 1000,
                }
            )

        def release(vk):
            if vk in held:
                send_key(vk, False)
                held.remove(vk)

        try:
            while time.monotonic() - start < duration:
                text = log_path.read_text(errors="replace")
                assert process.poll() is None, "game exited early"
                assert "=== recomp: CRASH" not in text, "game crashed; inspect stderr.txt"
                assert "unclassified CPU" not in text, "surface ownership violation"
                assert "[direct] FATAL:" not in text, "unsupported/invalid rendering path"
                assert not re.search(r"routine_readbacks=[1-9]", text), "routine readback"
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
                if restored and reload_count:
                    elapsed = time.monotonic() - start
                    menu = word(0x4A1533)
                    page, slots = word(0x4A1537), word(0x4A155F)
                    handler = word(0x626F74)
                    active_captions = caption_depth(text)
                    caption_token = (text.count("[direct] caption_scope=begin"), active_captions)
                    save_started = phase == "loading" and bool(
                        re.search(r"\[save\] open R .*game6\.dat.*sandbox", text[marker:], re.I)
                    )
                    report["last_state"] = {
                        "phase": phase,
                        "seconds": elapsed,
                        "handler": hex(handler),
                        "caption_phase": word(0x4A2F09),
                        "caption_scope_depth": active_captions,
                        "menu_exit_flag": menu,
                        "page": page,
                        "slots": slots,
                        "key_l": read(0x630924, 1)[0],
                        "key_return": read(0x6308E5, 1)[0],
                        "key_escape": read(0x6308F3, 1)[0],
                    }
                    if (
                        caption_needs_cancel(active_captions, caption_token, cancelled_caption)
                        and handler == 0x40E75C
                        and caption_interrupts_navigation(phase, save_started)
                        and (phase != "initial" or elapsed >= 24)
                    ):
                        # A scripted voice event can begin between a gameplay checkpoint
                        # and L handling. Close the observed callback, then re-enter the menu.
                        scenes = re.findall(r"\[direct\] scene=(\d+) ", text)
                        recovery_scene_floor = int(scenes[-1]) if scenes else 0
                        press(0x1B)
                        cancelled_caption = caption_token
                        report["input_events"].append(
                            {
                                "key": "ESC",
                                "seconds": elapsed,
                                "reload": completed + 1,
                                "caption_scope_depth": active_captions,
                                "reason": "caption interrupted load-menu entry",
                            }
                        )
                        phase, phase_start = "recover", time.monotonic()
                        time.sleep(0.1)
                        continue
                    if phase == "initial" and elapsed >= 24:
                        if active_captions or handler == 0x40E75C:
                            time.sleep(0.1)
                            continue
                        gameplay_handler = handler
                        report["gameplay_handler"] = hex(handler)
                        marker = len(text)
                        press(ord("L"))
                        phase, phase_start = "slots", time.monotonic()
                        report["input_events"].append(
                            {"key": "L", "seconds": elapsed, "reload": completed + 1}
                        )
                    elif phase == "recover":
                        assert time.monotonic() - phase_start < 20, (
                            "caption recovery did not resume"
                        )
                        scenes = re.findall(r"\[direct\] scene=(\d+) ", text)
                        if (
                            not active_captions
                            and scenes
                            and int(scenes[-1]) >= recovery_scene_floor + 25
                            and handler != 0x40E75C
                        ):
                            if gameplay_handler is None:
                                gameplay_handler = handler
                                report["gameplay_handler"] = hex(handler)
                            assert handler == gameplay_handler, "unexpected post-caption handler"
                            marker = len(text)
                            press(ord("L"))
                            phase, phase_start = "slots", time.monotonic()
                            report["input_events"].append(
                                {
                                    "key": "L",
                                    "seconds": elapsed,
                                    "reload": completed + 1,
                                    "reason": "gameplay resumed after interrupting caption",
                                }
                            )
                    elif phase == "slots":
                        assert time.monotonic() - phase_start < 15, (
                            "L did not open the expected load page",
                            menu,
                            page,
                            slots,
                        )
                        if load_page_ready(handler, page, slots, active_captions):
                            phase, phase_start = "confirm", time.monotonic()
                    elif phase == "confirm":
                        # CTRL is installed before menu fade/initialization completes.
                        # Let the released L reach the guest poll before confirming.
                        assert time.monotonic() - phase_start < 10, "load menu did not settle"
                        if time.monotonic() - phase_start >= 0.75 and not (
                            read(0x630924, 1)[0] & 1
                        ):
                            marker = len(text)
                            press(0x0D)
                            phase, phase_start = "loading", time.monotonic()
                            resume_scene_floor = 0
                            report["input_events"].append(
                                {
                                    "key": "RETURN",
                                    "seconds": elapsed,
                                    "menu_exit_flag": menu,
                                    "page": page,
                                    "slots": slots,
                                    "handler": hex(handler),
                                    "reload": completed + 1,
                                }
                            )
                    elif phase == "loading":
                        fresh = text[marker:]
                        assert time.monotonic() - phase_start < 20, "load did not resume in time"
                        save = re.search(r"\[save\] open R .*game6\.dat.*sandbox", fresh, re.I)
                        caption_phase = word(0x4A2F09)
                        if (
                            save
                            and handler == 0x40E75C
                            and caption_needs_cancel(
                                active_captions, caption_token, cancelled_caption
                            )
                            and time.monotonic() - phase_start >= 3
                        ):
                            # Scope logs bracket the actual callback, including its Again loop
                            # after visual phase 0. Cancel through the ordinary keyboard path.
                            press(0x1B)
                            cancelled_caption = caption_token
                            rendered = re.findall(r"\[direct\] scene=(\d+) ", fresh)
                            resume_scene_floor = int(rendered[-1]) if rendered else 0
                            report["input_events"].append(
                                {
                                    "key": "ESC",
                                    "seconds": elapsed,
                                    "caption_phase": caption_phase,
                                    "caption_scope_depth": active_captions,
                                    "reload": completed + 1,
                                }
                            )
                        samples = re.findall(
                            r"\[direct\] scene=(\d+) .*resources=(\d+)",
                            fresh[save.end() :] if save else "",
                        )
                        if (
                            resumed_reload(samples, active_captions, resume_scene_floor)
                            and handler == gameplay_handler
                        ):
                            completed += 1
                            report["observed_completed_reloads"] = completed
                            report["reload_checkpoints"].append(
                                {
                                    "reload": completed,
                                    "seconds": elapsed,
                                    "first_scene": int(samples[0][0]),
                                    "last_scene": int(samples[-1][0]),
                                    "settled_resources": int(samples[-1][1]),
                                    "handler": hex(handler),
                                    "post_caption_scene_floor": resume_scene_floor,
                                }
                            )
                            print(
                                f"reload {completed}/{reload_count}: save read, scene resumed, "
                                f"resources={samples[-1][1]}",
                                flush=True,
                            )
                            if completed == reload_count:
                                break
                            marker = len(text)
                            press(ord("L"))
                            phase, phase_start = "slots", time.monotonic()
                            report["input_events"].append(
                                {"key": "L", "seconds": elapsed, "reload": completed + 1}
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
            if reload_count:
                save_name = Path(icon_name).with_suffix(".dat").name
                assert any(
                    line.startswith("[save] open R ") and save_name in line and "sandbox" in line
                    for line in text.splitlines()
                ), "generated save not read"
                report["checks"]["load_reads_generated_save"] = True
                load_at = next(
                    i
                    for i, line in enumerate(text.splitlines())
                    if line.startswith("[save] open R ") and save_name in line and "sandbox" in line
                )
                after_load = "\n".join(text.splitlines()[load_at + 1 :])
                assert re.search(r"\[direct\] scene=\d+ ", after_load), "no scene after load"
                report["checks"]["scene_after_load"] = True
                report["reloads"] = reload_evidence(text, save_name, reload_count)
                report["checks"]["ten_full_reloads"] = reload_count == 10
                report["checks"]["settled_resources_bounded"] = True
            report["checks"]["audit_violations"] = 0
            report["checks"].update(thumbnail_exports=1, resumed_frames=True)
            report["passed"] = True
        except Exception as error:
            report["failure"] = str(error)
            if arena and process.poll() is None:
                try:
                    report["failure_state"] = {
                        "handler": hex(word(0x626F74)),
                        "caption_phase": word(0x4A2F09),
                        "caption_scope_depth": caption_depth(log_path.read_text(errors="replace")),
                        "menu_exit_flag": word(0x4A1533),
                        "page": word(0x4A1537),
                        "slots": word(0x4A155F),
                        "key_l": read(0x630924, 1)[0],
                        "key_return": read(0x6308E5, 1)[0],
                        "key_escape": read(0x6308F3, 1)[0],
                    }
                except OSError as state_error:
                    report["failure_state_error"] = str(state_error)
            raise
        finally:
            if process.poll() is None:
                for vk in list(held):
                    release(vk)
            if process.poll() is None:
                process.kill()
            process.wait()
            kernel.CloseHandle(handle)
            (run / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
