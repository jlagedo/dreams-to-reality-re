"""Original-x86 clipped line/ABI oracle; optional shared GPU fixture replay.

uv run --with unicorn python recomp/windream/debug/render_line_smoke.py [--gpu]
Expected pixels come only from original C3D_Line_; no Python rasterizer is used.
This controlled diagnostic does not establish natural debug-route coverage.
"""

import argparse
import ctypes
import hashlib
import json
import random
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from mdmp import Dump
from render_smoke import ESP, Replay

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

from dreams import paths  # noqa: E402

LINE, CLIP = 0x465C80, 0x46569C
MEMORY = 0x18000000
SURFACE = MEMORY + 64
COORDS = MEMORY + 0x18000


def build_hook(output):
    """Compile production hook + current lifted clipper in an isolated CPU DLL."""
    bodies = {}
    for file in recomp_env.out_dir("windream", "gen").glob("recomp_*.c"):
        for match in re.finditer(
            r"void sub_([0-9A-F]{8})\(void\) \{.*?^}", file.read_text(), re.M | re.S
        ):
            bodies[int(match[1], 16)] = match[0]
        for match in re.finditer(
            r"void wd_original_([0-9A-F]{8})\(void\) \{.*?^}", file.read_text(), re.M | re.S
        ):
            bodies[int(match[1], 16)] = match[0].replace("void wd_original_", "void sub_", 1)
    selected, pending = {}, [CLIP, LINE]
    while pending:
        address = pending.pop()
        if address in selected:
            continue
        body = selected[address] = bodies[address]
        pending.extend(int(v, 16) for v in re.findall(r"sub_([0-9A-F]{8})", body))
        pending.extend(
            int(v, 16)
            for v in re.findall(r"RECOMP_(?:ITAIL|ICALL)(?:_RA)?\(0x([0-9A-F]{8})u", body)
        )
    declarations = "\n".join(f"void sub_{v:08X}(void);" for v in sorted(selected))
    dispatch = "recomp_func_t recomp_lookup(uint32_t va) { switch(va) {\n"
    dispatch += "\n".join(f"case 0x{v:08X}: return sub_{v:08X};" for v in sorted(selected))
    dispatch += "\ncase 0x455358: return frame_init; case 0x47e700: return frame_compose;"
    dispatch += "\ncase 0x18002000: return frame_callback; case 0x4610e8: return frame_boxes;"
    dispatch += "\ndefault: abort(); } }\n"
    (output / "render_line_lifted.inc").write_text(
        declarations + "\n" + "\n".join(selected.values()) + "\n" + dispatch, encoding="utf-8"
    )
    source = Path(__file__).with_name("render_line_hook_host.c")
    runtime = source.parent.parent / "runtime"
    headers = list(recomp_env.out_dir("sdl3").glob("*/install/include/SDL3/SDL.h"))
    if len(headers) != 1:
        raise RuntimeError(
            "expected one already-installed SDL header tree; build dependencies first"
        )
    environment = recomp_env.build_env()
    compiler = shutil.which("clang-cl", path=environment.get("PATH"))
    if not compiler:
        raise RuntimeError("clang-cl unavailable")
    library = output / "render_line_hook.dll"
    result = subprocess.run(
        [
            compiler,
            "/nologo",
            "/Od",
            "/w",
            "/LD",
            f"/I{runtime}",
            f"/I{output}",
            f"/I{headers[0].parents[1]}",
            f"/Fe{library}",
            f"/Fo{output}/",
            str(source),
        ],
        cwd=output,
        env=environment,
        capture_output=True,
        text=True,
    )
    (output / "hook-build.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"line-hook build failed; see {output / 'hook-build.log'}")
    dll = ctypes.CDLL(str(library))
    dll.line_hook_init.argtypes = [ctypes.c_void_p]
    dll.line_hook_init.restype = ctypes.c_int
    dll.line_hook_run.argtypes = [
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.c_void_p,
    ]
    dll.line_hook_run.restype = None
    dll.line_hook_close.restype = None
    dll.frame_hook_run.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 3
    dll.frame_hook_run.restype = None
    return dll


def check_frames(hook):
    """Check production order/return sites against retail frame disassembly."""
    assert hook.line_hook_init(bytes(8))
    checks = []
    for alternate in (0, 1):
        for collector in (0, 1):
            for callback in (0, 1):
                for width in (64, 640):
                    inputs = (ctypes.c_uint32 * 4)(alternate, collector, callback, width)
                    output = (ctypes.c_uint32 * 10)()
                    trace = (ctypes.c_uint32 * 256)()
                    hook.frame_hook_run(inputs, output, trace)
                    events = [list(trace[i * 16 : i * 16 + 16]) for i in range(output[9])]
                    expected = ([1] if alternate else []) + [2]
                    if collector:
                        expected += [7]
                    if callback:
                        expected += ([3] if not collector else []) + [4]
                    expected += [5] + ([] if collector else [6])
                    assert [e[0] for e in events] == expected
                    assert list(output[:9]) == [
                        0x4731B8 if collector else SURFACE,
                        SURFACE if alternate else 0xD0D0D0D0,
                        0xB0B0B0B0,
                        0x51515151,
                        0xD1D1D1D1,
                        0xBEBEBEBE,
                        ESP + 4,
                        0x473014,
                        0x4731B8,
                    ]
                    sites = (
                        (
                            (0x4593CD, 0x4593DC, 0x4593E1)
                            if alternate
                            else (0x45934C, 0x45935B, 0x459360)
                        )
                        if collector
                        else (
                            (0x4593FF, 0x45940E, 0x459413)
                            if alternate
                            else (0x459384, 0x459393, 0x459398)
                        )
                    )
                    for e in events:
                        if e[0] in (2, 4, 5):
                            assert e[7] == sites[(2, 4, 5).index(e[0])], (e, sites)
                            assert e[5:7] == [0x478800 if collector else 0x473014, 0]
                            assert e[8] == ESP - (8 if alternate else 12) - (8 if collector else 0)
                            assert e[9:11] == (
                                [0, 0x478800] if collector else [0xB0B0B0B0, 0x51515151]
                            )
                            if collector:
                                saved = [0xB0B0B0B0, 0x51515151, 0xBEBEBEBE]
                                if not alternate:
                                    saved += [0xD0D0D0D0]
                                saved += [0x1000FF00]
                                assert e[11 : 11 + len(saved)] == saved
                        if e[0] in (3, 6):
                            assert e[1:5] == [
                                0x18001000,
                                SURFACE,
                                int(not alternate and width >= 320),
                                0x1000FF00,
                            ]
                        if e[0] == 6:
                            assert e[5:7] == [0x473014, 0x4731B8]
                    checks.append(
                        {
                            "alternate": alternate,
                            "collector": collector,
                            "callback": callback,
                            "width": width,
                            "events": events,
                        }
                    )
    hook.line_hook_close()
    return {
        "checks": checks,
        "case_count": len(checks),
        "evidence": "actual production hooks; mocked callees; return sites from retail disassembly",
        "limitation": "Does not establish callee semantics or game/debug route coverage",
    }


def cases(width, height):
    centre = (width // 2, height // 2)
    result = []
    for dx, dy in (
        (19, 7),
        (7, 19),
        (-19, 7),
        (-7, 19),
        (19, -7),
        (7, -19),
        (-19, -7),
        (-7, -19),
        (16, 8),
        (8, 16),
        (12, 12),
        (0, 12),
        (12, 0),
    ):
        endpoint = (centre[0] + dx, centre[1] + dy)
        result.extend(
            [
                (f"direction-{dx}-{dy}", (*centre, *endpoint)),
                (f"reverse-{dx}-{dy}", (*endpoint, *centre)),
            ]
        )
    for x, y in (
        (0, 0),
        (1, 1),
        (width - 2, height - 2),
        (width - 1, height - 1),
        (width, height),
        (-1, -1),
        centre,
    ):
        result.append((f"point-{x}-{y}", (x, y, x, y)))
    result.extend(
        [
            ("cross-horizontal", (-20, centre[1], width + 20, centre[1])),
            ("cross-vertical", (centre[0], -20, centre[0], height + 20)),
            ("cross-diagonal", (-20, -20, width + 20, height + 20)),
            ("cross-reverse", (width + 20, -20, -20, height + 20)),
            ("reject-left", (-2, 0, -2, height)),
            ("reject-right", (width, 0, width, height)),
            ("reject-top", (0, -2, width, -2)),
            ("reject-bottom", (0, height, width, height)),
            ("edge-top", (0, 0, width - 1, 0)),
            ("edge-bottom", (0, height - 1, width - 1, height - 1)),
            ("edge-left", (0, 0, 0, height - 1)),
            ("edge-right", (width - 1, 0, width - 1, height - 1)),
        ]
    )
    rng = random.Random(6006)
    for index in range(96):
        result.append(
            (
                f"random-{index}",
                (
                    rng.randrange(-width, width * 2),
                    rng.randrange(-height, height * 2),
                    rng.randrange(-width, width * 2),
                    rng.randrange(-height, height * 2),
                ),
            )
        )
    return result


def check_abi(before, after, stack_bytes):
    for register in ("esi", "edi", "ebp"):
        assert after[register] == before[register], (register, before, after)
    assert after["esp"] == ESP + 4 + stack_bytes, after
    assert after["fpcw"] == 0x027F and after["fptop"] == 0, after


def generate(dump_path, destination, hook=None):
    replay = Replay(Dump(dump_path, 0), pixels=True)
    replay.uc.mem_map(MEMORY, 0x20000)
    for page in range(MEMORY, MEMORY + 0x20000, 4096):
        replay.pages[page] = bytes(4096)
    report = {
        "dump": str(dump_path),
        "fixture": str(destination),
        "cases": [],
        "abi": "original x86 ESI/EDI/EBP, callee cleanup, FPU control/top",
        "limitation": "Does not execute replacement hook or prove live debug-route coverage",
    }
    records = []
    if hook:
        assert hook.line_hook_init(replay.read(0x4C6168, 8))
    for width, height in ((64, 48), (63, 47)):
        replay.put(0x661EBC, width)
        replay.put(0x661EC8, height)
        size = width * height * 2
        for fmt in (0, 1):
            for index, (label, endpoints) in enumerate(cases(width, height)):
                colour = (0xFFFF, 0x8000, 0xF81F, 0x7FE0, 0x1234)[index % 5]
                replay.write(SURFACE - 64, bytes([0xA5]) * 64 + bytes(size) + bytes([0x5A]) * 64)
                replay.write(COORDS, struct.pack("<4i", *endpoints))
                before, after = replay.run(
                    CLIP, eax=COORDS, edx=COORDS + 4, ebx=COORDS + 8, ecx=COORDS + 12
                )
                check_abi(before, after, 0)
                clipped = struct.unpack("<4i", replay.read(COORDS, 16))
                accepted = int(
                    bool(after["eax"])
                    and all(
                        0 <= value <= bound
                        for value, bound in zip(
                            clipped, (width, height, width, height), strict=True
                        )
                    )
                )
                before, after = replay.run(
                    LINE,
                    eax=SURFACE,
                    edx=endpoints[0] & 0xFFFFFFFF,
                    ebx=endpoints[1] & 0xFFFFFFFF,
                    ecx=endpoints[2] & 0xFFFFFFFF,
                    stack=(endpoints[3] & 0xFFFFFFFF, colour),
                )
                check_abi(before, after, 8)
                assert replay.u32(0x661EBC) == width and replay.u32(0x661EC8) == height
                assert replay.read(SURFACE - 64, 64) == bytes([0xA5]) * 64, label
                assert replay.read(SURFACE + size, 64) == bytes([0x5A]) * 64, label
                expected = replay.read(SURFACE, size)
                if hook:
                    for owned in (0, 1):
                        inputs = (ctypes.c_uint32 * 8)(width, height, *endpoints, colour, owned)
                        outputs = (ctypes.c_uint32 * 16)()
                        pixels = ctypes.create_string_buffer(size)
                        hook.line_hook_run(inputs, outputs, pixels)
                        assert list(outputs[7:13]) == [
                            after["esi"],
                            after["edi"],
                            after["ebp"],
                            after["esp"],
                            after["fpcw"],
                            after["fptop"],
                        ]
                        assert outputs[13] == replay.u32(ESP + 4), (
                            label,
                            "stack y1",
                            owned,
                            outputs[13],
                            replay.u32(ESP + 4),
                        )
                        assert list(outputs[14:16]) == [width, height]
                        assert outputs[0] == (accepted if owned else 0), (label, "submission")
                        if owned and accepted:
                            assert list(outputs[1:7]) == [
                                SURFACE,
                                *(v & 0xFFFFFFFF for v in clipped),
                                colour,
                            ], (label, "endpoints/colour")
                        assert pixels.raw == (bytes(size) if owned else expected), (label, "pixels")
                if not accepted:
                    assert expected == bytes(size), (label, clipped)
                records.append(
                    struct.pack("<IIIIiiiiI", width, height, fmt, colour, *clipped, accepted)
                    + expected
                )
                report["cases"].append(
                    {
                        "label": label,
                        "width": width,
                        "height": height,
                        "format": fmt,
                        "colour": colour,
                        "input": endpoints,
                        "clipped": clipped,
                        "accepted": bool(accepted),
                        "changed_pixels": sum(
                            v != 0 for (v,) in struct.iter_unpack("<H", expected)
                        ),
                    }
                )
    data = b"WDL1" + struct.pack("<I", len(records)) + b"".join(records)
    destination.write_bytes(data)
    report["sha256"] = hashlib.sha256(data).hexdigest()
    report["case_count"] = len(records)
    if hook:
        hook.line_hook_close()
        report["production_hook_cases"] = len(records) * 2
        report["production_hook"] = "GPU-owned and ordinary-RAM paths; actual render_hooks.c"
        report["production_hook_sha256"] = hashlib.sha256(
            (Path(__file__).parent.parent / "runtime/render_hooks.c").read_bytes()
        ).hexdigest()
        report["lifted_closure_sha256"] = hashlib.sha256(
            (destination.parent / "render_line_lifted.inc").read_bytes()
        ).hexdigest()
        report["limitation"] = "Controlled ABI/GPU-submission evidence, not natural debug coverage"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", action="append", type=Path)
    parser.add_argument("--gpu", action="store_true")
    parser.add_argument("--hook", action="store_true", help="build/check actual hook ABI on CPU")
    args = parser.parse_args()
    dumps = args.dump or [
        paths.REPO_ROOT / f"out/scratch/retail-gdidream-{stamp}.dmp"
        for stamp in ("222659", "223423")
    ]
    output = recomp_env.out_dir("render-line-smoke")
    hook = build_hook(output) if args.hook else None
    if hook:
        frames = check_frames(hook)
        (output / "results-frame-hook.json").write_text(
            json.dumps(frames, indent=2) + "\n", encoding="utf-8"
        )
        print(f"PASS production frame hook: {frames['case_count']} ABI/order cases")
    reports = []
    for dump in dumps:
        fixture = output / ("lines-" + dump.stem.rsplit("-", 1)[-1] + ".bin")
        report = generate(dump, fixture, hook)
        if args.gpu:
            binary = recomp_env.out_dir("direct-render", "build") / "ODDirectGpuTests.exe"
            result = subprocess.run(
                [str(binary), "--lines", str(fixture)],
                capture_output=True,
                text=True,
                errors="replace",
            )
            report["gpu"] = {
                "returncode": result.returncode,
                "stdout": result.stdout,
                "stderr": result.stderr,
            }
        reports.append(report)
        (output / "results.json").write_text(json.dumps(reports, indent=2) + "\n", encoding="utf-8")
        evidence = "results" + ("-hook" if args.hook else "-cpu") + ("-gpu" if args.gpu else "")
        (output / (evidence + ".json")).write_text(
            json.dumps(reports, indent=2) + "\n", encoding="utf-8"
        )
        print(f"PASS original x86: {dump.name}: {report['case_count']} line/clip/ABI cases")
        if args.gpu and report["gpu"]["returncode"]:
            print(report["gpu"])
            return 1
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
