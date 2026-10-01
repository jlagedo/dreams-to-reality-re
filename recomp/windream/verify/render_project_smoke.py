"""Controlled project transitions through retail SCENE_CheckExits, in an isolated child.

Example (run only while owning the live GPU test slot):
  uv run --with unicorn python recomp/windream/verify/render_project_smoke.py \
    --projects 55,39,11,8 --capture-source

Only LINK0 source input is changed: destination and an unconditional exit gate.
The original exit handler selects the project, queues the fade/movie, and runs
the original level loader, relocation and spawn. No project pointer, player pose,
pending-load flag, renderer state or code is written. This is a controlled route,
not a claim that normal player progression through the exit was exercised.
"""

from __future__ import annotations

import argparse
import ctypes as c
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run import read_roots  # noqa: E402

from dreams import paths  # noqa: E402
from dreams.formats import project  # noqa: E402

PROJECT_POINTER = 0x661E04
LOADING = 0x661E08
TRANSITION_TIMER = 0x5E5480
LINK0 = 0x200
LINK_FLAGS = 0x18


def controlled_link(index: int) -> bytes:
    if not 0 <= index < 150:
        raise ValueError("project index must be in [0,149]")
    record = bytearray(0x80)
    record[:6] = b"LINK0\0"
    name = f"Project{index}".encode() + b"\0"
    record[0xC : 0xC + len(name)] = name
    # Leave disabled until the complete record and current-project identity are
    # checked. Flags=1 is published separately. X min/max=0 selects the retail
    # unconditional branch; empty item and other flag bits disable conditions.
    return bytes(record)


def counter(log: str, field: str) -> int:
    matches = re.findall(rf"\b{field}=(\d+)", log)
    return int(matches[-1]) if matches else 0


def source_projects() -> list[bytes]:
    candidates = sorted(paths.disc(1).rglob("DREAMS.DAT"))
    if not candidates:
        raise RuntimeError("original disc has no DREAMS.DAT")
    return project.records(candidates[0])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--projects", default="55,39")
    parser.add_argument("--renderer", choices=("direct", "software"), default="direct")
    parser.add_argument("--dwell-seconds", type=float, default=15)
    parser.add_argument("--load-timeout", type=float, default=120)
    parser.add_argument("--startup-timeout", type=float, default=90)
    parser.add_argument("--snap-ms", type=int, default=3000)
    parser.add_argument("--capture-source", action="store_true")
    parser.add_argument("--visible", action="store_true")
    parser.add_argument("--profile", action="store_true")
    parser.add_argument("--tag", default="")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        targets = [int(value) for value in args.projects.split(",")]
        for index in targets:
            controlled_link(index)
    except ValueError as exc:
        parser.error(str(exc))
    if (
        not targets
        or len(targets) > 150
        or any(a == b for a, b in zip([0] + targets, targets, strict=False))
    ):
        parser.error("use 1..150 transitions, each to a project different from the previous one")
    if not 2 <= args.dwell_seconds <= 600 or not 10 <= args.load_timeout <= 600:
        parser.error("dwell must be 2..600 seconds; load timeout 10..600 seconds")
    if not 25 <= args.startup_timeout <= 600 or not 0 <= args.snap_ms <= 60000:
        parser.error("startup timeout must be 25..600 seconds; snapshot interval 0..60000 ms")
    if args.renderer == "software" and not args.snap_ms:
        parser.error("software reference readiness requires periodic GDI snapshots")
    if args.tag and not re.fullmatch(r"[A-Za-z0-9_-]+", args.tag):
        parser.error("tag must contain only letters, digits, hyphens or underscores")
    originals = source_projects()
    report = {
        "passed": False,
        "renderer": args.renderer,
        "progress_unit": "submitted scenes" if args.renderer == "direct" else "GDI presents",
        "controlled_input": "128-byte LINK0 source record; flag1 enabled after bounded write",
        "retail_path": [
            "SCENE_CheckExits@00420b60",
            "DDAT_LoadRecord@00449bf9",
            "GAME_Tick@004240ba",
            "SCENE_LoadLevel@0041f9db",
        ],
        "source_evidence": "out/recomp/render-content-inventory/project-transition-ghidra.txt",
        "limitations": [
            "Controlled destination/gate, not unmodified exit progression",
            "Live source snapshots do not substitute for the independent spawn oracle",
        ],
        "targets": [
            {
                "project": index,
                "spawn": struct.unpack_from("<3i", originals[index], 0xB4),
                "project_movie": originals[index][0x3C:0x4C].split(b"\0", 1)[0].decode("latin-1"),
                "animated_video": originals[index][0x5C:0x6C].split(b"\0", 1)[0].decode("latin-1"),
            }
            for index in targets
        ],
        "writes": [],
        "transitions": [],
    }
    if args.dry_run:
        print(json.dumps(report, indent=2))
        return 0
    if sys.platform != "win32":
        parser.error("live project smoke requires Windows")
    tag = args.tag or "projects-" + "-".join(map(str, targets)) + f"-{int(time.time())}"
    run = recomp_env.out_dir("windream", f"run-{args.renderer}-{tag}")
    binary = recomp_env.out_dir("windream", "build-audit") / "windream_recomp.exe"
    if not binary.is_file():
        parser.error("build the strict --render-audit runtime first")
    env = dict(
        os.environ,
        WD_RENDERER=args.renderer,
        WD_MUTE="1",
        WD_HEADLESS="" if args.visible else "1",
        WD_READ_ROOTS=read_roots(),
        WD_FPS="25",
        WD_SCALE="1",
        WD_FOCUS="1",
        WD_QUIET="1",
        WD_DUMP="mini",
        WD_SNAP_MS=str(args.snap_ms) if args.snap_ms else "",
        WD_RENDER_PROFILE="1" if args.profile else "",
        WD_POKE="",
        WD_SCENE_CAPTURE="",
        WD_WIDTH="",
        WD_HEIGHT="",
        WD_RESIZE="",
        WD_MOUSE="",
        WD_FULLSCREEN="",
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
    log = run / "stderr.txt"
    start = time.monotonic()
    wall_start = time.time()
    with log.open("wb") as stderr, (run / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(
            [str(binary), str(paths.disc(1) / "GDIDREAM.EXE"), "--run"],
            cwd=run,
            env=env,
            stdout=stdout,
            stderr=stderr,
        )
        handle = kernel.OpenProcess(0x38, 0, process.pid)
        arena = None

        def read(address: int, size: int) -> bytes:
            data, done = c.create_string_buffer(size), c.c_size_t()
            if (
                not arena
                or not kernel.ReadProcessMemory(handle, arena + address, data, size, c.byref(done))
                or done.value != size
            ):
                raise RuntimeError(
                    f"guest read failed at {address:#x}/{size}: {c.get_last_error()}"
                )
            return data.raw

        def u32(address: int) -> int:
            return int.from_bytes(read(address, 4), "little")

        def progress(text: str) -> int:
            if args.renderer == "direct":
                return counter(text, "scene")
            presents = re.findall(r"\[gdi\] wrote snap_(\d+)_\d+ms\.bmp", text)
            return int(presents[-1]) if presents else 0

        def write_link(address: int, data: bytes, record: int) -> None:
            if not record + LINK0 <= address or address + len(data) > record + LINK0 + 0x80:
                raise RuntimeError("test attempted a write outside LINK0 source input")
            buffer, done = c.create_string_buffer(data), c.c_size_t()
            if not kernel.WriteProcessMemory(
                handle, arena + address, buffer, len(data), c.byref(done)
            ) or done.value != len(data):
                raise RuntimeError(f"guest LINK input write failed: {c.get_last_error()}")
            report["writes"].append(
                {
                    "address": hex(address),
                    "bytes": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                }
            )

        def capture_source(index: int, phase: str) -> dict:
            from render_scene_smoke import read_snapshot

            dll = c.CDLL(
                str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll")
            )
            reader_type = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.c_uint32, c.c_void_p, c.c_size_t)

            @reader_type
            def reader(_context, address, destination, size):
                try:
                    c.memmove(destination, read(address, size), size)
                    return True
                except RuntimeError:
                    return False

            capture = dll.wd_capture_scene_file
            capture.argtypes = [
                reader_type,
                c.c_void_p,
                c.c_uint32,
                c.c_char_p,
                c.c_void_p,
                c.c_size_t,
            ]
            capture.restype = c.c_int
            destination, error = run / f"project-{index}-{phase}.wds", c.create_string_buffer(1024)
            if not capture(
                reader, None, u32(0x661EE8), str(destination).encode(), error, len(error)
            ):
                raise RuntimeError(f"source capture failed: {error.value.decode(errors='replace')}")
            camera, nodes, _, faces = read_snapshot(destination)
            return {
                "path": str(destination),
                "scope": "Original scene source; external capture omits host-added fog state",
                "nodes": len(nodes),
                "faces": len(faces),
                "submitted_modes": dict(
                    Counter(f["kind"] for f in faces if nodes[f["owner"]]["active"])
                ),
                "submitted_lit_nodes": sum(n["active"] and n["light_count"] > 0 for n in nodes),
                "materials": {
                    str(material["slot"]): {
                        "page": material["page"],
                        "indices_sha256": hashlib.sha256(material["indices"]).hexdigest(),
                        "palette_sha256": hashlib.sha256(
                            struct.pack("<8192I", *material["palette"])
                        ).hexdigest(),
                    }
                    for material in camera["materials"]
                },
            }

        phase, next_index, expected_name = "startup", 0, "Project0"
        phase_start, ready_at, scenes_at_ready = start, None, 0
        try:
            if not handle:
                raise c.WinError(c.get_last_error())
            while True:
                now = time.monotonic()
                text = log.read_text(errors="replace")
                if process.poll() is not None:
                    raise RuntimeError(f"child exited unexpectedly ({process.returncode})")
                if any(
                    marker in text
                    for marker in (
                        "[direct] FATAL",
                        "=== recomp: CRASH",
                        "[render-audit] violation",
                    )
                ):
                    raise RuntimeError("child rendering/runtime failed; inspect stderr.txt")
                match = re.search(r"reserved at host ([0-9A-Fa-f]+)", text)
                if match:
                    arena = int(match[1], 16)
                if not arena or not (
                    counter(text, "frames") if args.renderer == "direct" else progress(text)
                ):
                    if now - start > args.startup_timeout:
                        raise RuntimeError("guest arena did not initialize")
                    time.sleep(0.05)
                    continue
                record = u32(PROJECT_POINTER)
                name = read(record, 16).split(b"\0", 1)[0].decode("latin-1") if record else ""
                ready = bool(
                    record
                    and name == expected_name
                    and u32(LOADING) == 0
                    and u32(TRANSITION_TIMER) & 0x7FFFFFFF == 0
                    and progress(text) > 0
                )
                if phase == "startup":
                    if now - start > args.startup_timeout:
                        raise RuntimeError("initial Project0 did not become ready")
                    if not ready or now - start < 24:
                        time.sleep(0.05)
                        continue
                elif phase == "loading":
                    if now - phase_start > args.load_timeout:
                        raise RuntimeError(f"{expected_name} transition timed out (current {name})")
                    if not ready:
                        time.sleep(0.05)
                        continue
                    index = targets[next_index - 1]
                    spawn = struct.unpack("<3i", read(record + 0xB4, 12))
                    if spawn != struct.unpack_from("<3i", originals[index], 0xB4):
                        raise RuntimeError("loaded spawn source differs from original project")
                    report["transitions"][-1].update(
                        ready_seconds=round(now - start, 3),
                        loaded_record=hex(record),
                        spawn=list(spawn),
                        active_lights=u32(0x4AC758),
                        render_progress_at_ready=progress(text),
                    )
                    ready_at, scenes_at_ready, phase = now, progress(text), "dwell"
                    print(f"{expected_name} loaded via retail exit/loader", flush=True)
                    time.sleep(0.05)
                    continue
                elif phase == "dwell":
                    if name != expected_name or u32(LOADING):
                        raise RuntimeError(f"unexpected project change during dwell: {name}")
                    if (
                        args.capture_source
                        and now - ready_at >= 2
                        and "source_capture_before" not in report["transitions"][-1]
                    ):
                        report["transitions"][-1]["source_capture_before"] = capture_source(
                            targets[next_index - 1], "before"
                        )
                    if now - ready_at < args.dwell_seconds:
                        time.sleep(0.05)
                        continue
                    if progress(text) <= scenes_at_ready:
                        raise RuntimeError("target project stopped scene submission")
                    report["transitions"][-1]["render_progress_after_dwell"] = progress(text)
                    if args.capture_source:
                        after = capture_source(targets[next_index - 1], "after")
                        report["transitions"][-1]["source_capture"] = after
                        before = report["transitions"][-1]["source_capture_before"]
                        common = set(before["materials"]) & set(after["materials"])
                        report["transitions"][-1]["material_versions"] = {
                            "common_slots": len(common),
                            "changed_pages": [
                                slot
                                for slot in sorted(common)
                                if before["materials"][slot]["indices_sha256"]
                                != after["materials"][slot]["indices_sha256"]
                            ],
                            "changed_palettes": [
                                slot
                                for slot in sorted(common)
                                if before["materials"][slot]["palette_sha256"]
                                != after["materials"][slot]["palette_sha256"]
                            ],
                        }
                    if next_index == len(targets):
                        if args.renderer == "direct" and "routine_readbacks=0" not in text:
                            raise RuntimeError("missing strict direct ownership counters")
                        report["passed"] = True
                        break
                target = targets[next_index]
                previous = read(record + LINK0, 0x80)
                write_link(record + LINK0 + LINK_FLAGS, bytes(4), record)
                write_link(record + LINK0, controlled_link(target), record)
                if (
                    u32(PROJECT_POINTER) != record
                    or read(record, 16).split(b"\0", 1)[0].decode() != name
                ):
                    raise RuntimeError("project changed before activating controlled LINK input")
                write_link(record + LINK0 + LINK_FLAGS, struct.pack("<I", 1), record)
                report["transitions"].append(
                    {
                        "from": name,
                        "to": f"Project{target}",
                        "requested_seconds": round(now - start, 3),
                        "source_record": hex(record),
                        "previous_link_sha256": hashlib.sha256(previous).hexdigest(),
                    }
                )
                next_index += 1
                expected_name, phase, phase_start = f"Project{target}", "loading", now
                time.sleep(0.05)
        except Exception as exc:
            report["error"] = str(exc)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            if handle:
                kernel.CloseHandle(handle)
            report["elapsed_seconds"] = round(time.monotonic() - start, 3)
            report["snapshots"] = sorted(
                str(path)
                for path in run.glob("snap_*.*")
                if path.suffix in {".bmp", ".png"} and path.stat().st_mtime >= wall_start
            )
            report["log"] = str(log)
            report["terminated_by_harness"] = True
            (run / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
