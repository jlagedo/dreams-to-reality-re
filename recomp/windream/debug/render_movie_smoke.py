"""Original-x86 HNM5 conversion versus production CPU-buffer/upload bridge.

uv run --with unicorn python recomp/windream/debug/render_movie_smoke.py
Add --gpu to replay generated packed-2D fixtures through ODDirectGpuTests.
Only the compiler stack guard is stubbed in both executions. No live game or
GPU is used; synthetic palette/index inputs are converted by original x86.
"""

import argparse
import ctypes
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from mdmp import Dump
from render_smoke import Replay, check_abi
from unicorn import x86_const as xr

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

from dreams import paths  # noqa: E402

WRAPPER, HELPER = 0x42665A, 0x454DE3
SOURCE, TABLE, DEST = 0x18000000, 0x18100000, 0x18200040
SCRATCH = 0x18300040
DIMENSIONS = [(800, 600), (640, 400), (320, 400), (320, 200), (640, 480), (641, 480)]


def build(output):
    bodies = {}
    for file in recomp_env.out_dir("windream", "gen").glob("recomp_*.c"):
        for match in re.finditer(
            r"void (?:sub|wd_original)_([0-9A-F]{8})\(void\) \{.*?^}",
            file.read_text(),
            re.M | re.S,
        ):
            address = int(match[1], 16)
            if address in (WRAPPER, HELPER) and "wd_try_replace" not in match[0]:
                bodies[address] = match[0].replace("void wd_original_", "void sub_", 1)
    assert len(bodies) == 2
    dispatch = "recomp_func_t recomp_lookup(uint32_t address) { switch(address) {"
    dispatch += "".join(f"case 0x{address:x}: return sub_{address:08X};" for address in bodies)
    dispatch += "default: abort(); }}"
    lifted = output / "render_movie_lifted.inc"
    lifted.write_text(
        "\n".join(f"void sub_{address:08X}(void);" for address in bodies)
        + "\n"
        + "\n".join(bodies.values())
        + "\n"
        + dispatch,
        encoding="utf-8",
    )
    runtime = Path(__file__).resolve().parents[1] / "runtime"
    headers = list(recomp_env.out_dir("sdl3").glob("*/install/include/SDL3/SDL.h"))
    assert len(headers) == 1
    env = recomp_env.build_env()
    compiler = shutil.which("clang-cl", path=env["PATH"])
    assert compiler
    dll = output / "movie_hook.dll"
    compiled = subprocess.run(
        [
            compiler,
            "/nologo",
            "/LD",
            "/Od",
            "/w",
            "/I" + str(runtime),
            "/I" + str(output),
            "/I" + str(headers[0].parents[1]),
            str(Path(__file__).with_name("render_movie_hook_host.c")),
            "/Fe:" + str(dll),
        ],
        cwd=output,
        env=env,
        capture_output=True,
        text=True,
    )
    (output / "build.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
    if compiled.returncode:
        raise RuntimeError(f"movie bridge build failed: {output / 'build.log'}")
    hook = ctypes.CDLL(str(dll))
    hook.movie_hook_init.restype = ctypes.c_int
    hook.movie_hook_run.argtypes = [
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.c_void_p,
    ]
    hook.movie_hook_close.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    return hook


def stack_guard(uc, address, size, replay):
    stack = uc.reg_read(xr.UC_X86_REG_ESP)
    target = replay.u32(stack)
    uc.reg_write(xr.UC_X86_REG_ESP, stack + 8)
    uc.reg_write(xr.UC_X86_REG_EIP, target)


def write_gpu_fixture(path, width, height, fmt, initial_pixel, rect, native, expected):
    """D2D1 stream: actual bridge upload payload, independent original-x86 truth."""
    x, y, w, h = rect
    raw = b"".join(
        native[((y + row) * width + x) * 2 : ((y + row) * width + x + w) * 2] for row in range(h)
    )
    payload = b"".join(
        struct.pack("<I", pixel | (64 << 16)) for (pixel,) in struct.iter_unpack("<H", raw)
    )
    with path.open("wb") as stream:
        stream.write(struct.pack("<6I", 0x31443244, width, height, fmt, 1, 1))
        stream.write(struct.pack("<I", initial_pixel) * (width * height))
        # RAW operation, destination/source target zero, actual upload rectangle.
        stream.write(struct.pack("<9I", 9, 0, 0, x, y, w, h, 0, w * h))
        stream.write(payload)
        stream.write(expected)
    return {
        "path": str(path),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "rect": rect,
        "packed_format": fmt,
        "initial_pixel": initial_pixel,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", action="append", type=Path)
    parser.add_argument("--gpu", action="store_true", help="run existing D3D11 packed-2D oracle")
    args = parser.parse_args()
    dumps = args.dump or [
        paths.REPO_ROOT / f"out/scratch/retail-gdidream-{stamp}.dmp"
        for stamp in ("222659", "223423")
    ]
    output = recomp_env.out_dir("render-movie-smoke")
    hook = build(output)
    assert hook.movie_hook_init()
    source = bytes((i * 37 + (i >> 9)) & 255 for i in range(640 * 480 + 2))
    report = {
        "checks": [],
        "gpu_fixtures": [],
        "scope": "original x86 conversion + production bridge ABI/footprints; no live GPU claim",
    }
    for dump in dumps:
        replay = Replay(Dump(dump, 0), pixels=True)
        replay.uc.mem_map(SOURCE, 0x400000)
        for page in range(SOURCE, SOURCE + 0x400000, 4096):
            replay.pages[page] = bytes(4096)
        replay.at(0x454FEB, lambda uc, a, n, _, replay=replay: stack_guard(uc, a, n, replay))
        for variant in (0, 1):
            initial_pixel = 0xFB7B if variant else 0x7B7B
            palette = [((i * 257) ^ (0x8123 if variant else 0x04EF)) & 65535 for i in range(256)]
            table = b"".join(
                struct.pack("<I", palette[i & 255] | (palette[i >> 8] << 16)) for i in range(65536)
            )
            for width, height in DIMENSIONS:
                size = width * height * 2
                replay.write(SOURCE, source)
                replay.write(TABLE, table)
                replay.write(
                    DEST - 64,
                    bytes([0xA5]) * 64
                    + struct.pack("<H", initial_pixel) * (size // 2)
                    + bytes([0x5A]) * 64,
                )
                for address, value in (
                    (0x49D9FC, width),
                    (0x49DA00, height),
                    (0x5E549C, DEST),
                    (0x60CE14, SOURCE),
                    (0x60CE1C, TABLE),
                    (0x4A4B70, 0xDEADBEEF),
                ):
                    replay.put(address, value)
                before, after = replay.run(WRAPPER)
                check_abi(before, after)
                flags = replay.uc.reg_read(xr.UC_X86_REG_EFLAGS)
                expected = replay.read(DEST, size)
                assert replay.read(DEST - 64, 64) == bytes([0xA5]) * 64
                assert replay.read(DEST + size, 64) == bytes([0x5A]) * 64
                for owned in (0, 1):
                    inputs = (ctypes.c_uint32 * 4)(width, height, owned, initial_pixel)
                    state = (ctypes.c_uint32 * 21)()
                    pixels = ctypes.create_string_buffer(size)
                    hook.movie_hook_run(inputs, source, table, state, pixels)
                    assert list(state[:8]) == [
                        after[name]
                        for name in ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp")
                    ]
                    assert list(state[8:12]) == [
                        after["fpcw"],
                        after["fptop"],
                        replay.u32(0x4A4B70),
                        DEST,
                    ]
                    assert state[13] & 0x8D5 == flags & 0x8D5
                    assert pixels.raw == expected, (width, height, variant, owned)
                    should_upload = int(bool(owned) and width != 641)
                    assert state[12] == should_upload
                    if should_upload:
                        x, y, w, h = (80, 60, 640, 480) if width == 800 else (0, 0, width, height)
                        assert list(state[14:21]) == [
                            SCRATCH + y * width * 2 + x * 2,
                            DEST,
                            x,
                            y,
                            w,
                            h,
                            width * 2,
                        ]
                        fixture = (
                            output
                            / f"movie-{len(report['checks']):02}-{width}x{height}-f{variant}.bin"
                        )
                        report["gpu_fixtures"].append(
                            write_gpu_fixture(
                                fixture,
                                width,
                                height,
                                variant,
                                initial_pixel,
                                (x, y, w, h),
                                pixels.raw,
                                expected,
                            )
                        )
                report["checks"].append(
                    {
                        "dump": str(dump),
                        "width": width,
                        "height": height,
                        "palette": variant,
                        "sha256": hashlib.sha256(expected).hexdigest(),
                        "native_paths": 2,
                        "loop_metadata": state[10],
                    }
                )
    counts = (ctypes.c_uint32 * 2)()
    hook.movie_hook_close(counts)
    assert list(counts) == [1, 1]
    report["scratch_allocations"], report["scratch_releases"] = counts
    report["passed"] = True
    if args.gpu:
        binary = recomp_env.out_dir("direct-render", "build") / "ODDirectGpuTests.exe"
        if not binary.is_file():
            raise RuntimeError("build the existing direct renderer validation target first")
        result = subprocess.run(
            [str(binary), *(fixture["path"] for fixture in report["gpu_fixtures"])],
            capture_output=True,
            text=True,
            errors="replace",
        )
        report["gpu"] = {
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
        }
        report["passed"] = result.returncode == 0
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.gpu:
        (output / "results-gpu.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8"
        )
        if not report["passed"]:
            raise RuntimeError(f"GPU movie oracle failed: {output / 'results-gpu.json'}")
    print(
        f"PASS: {len(report['checks'])} original-x86 cases; "
        f"{len(report['checks']) * 2} production bridge paths"
    )


if __name__ == "__main__":
    main()
