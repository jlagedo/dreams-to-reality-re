"""Capture transient retail shadow inputs and compare the direct GPU mask.

Uses the real-shadow dump recorded by render_smoke.py. Readbacks are oracle
exports only; the production shadow producer/consumer stay on the GPU.
"""

import ctypes
import json
import struct
import subprocess
import sys
from pathlib import Path

from mdmp import Dump
from render_smoke import REGS, Replay

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402


def main():
    original_report = json.loads(
        (recomp_env.out_dir("render-smoke") / "shadow-contract.json").read_text()
    )
    dump = Dump(Path(original_report["dump"]), original_report["arena"])
    replay = Replay(dump, pixels=True)
    out = recomp_env.out_dir("shadow-adapter")
    path = out / "retail-shadow.wds"
    build = recomp_env.out_dir("direct-render", "build")
    dll = ctypes.CDLL(str(build / "WDSceneAdapterOracle.dll"))
    reader_type = ctypes.CFUNCTYPE(
        ctypes.c_bool, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t
    )

    @reader_type
    def reader(_context, address, destination, size):
        data = replay.read(address, size)
        ctypes.memmove(destination, bytes(data), size)
        return True

    capture = dll.wd_capture_scene_file
    capture.argtypes = [
        reader_type,
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_char_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
    ]
    capture.restype = ctypes.c_int
    captures = []
    projected_triangles = []

    def at_faces(uc, _address, _size, _data):
        node = uc.reg_read(REGS["eax"])
        block = replay.u32(node + 0xA4)
        while block:
            face = replay.u32(block + 0x24)
            seen = set()
            while face:
                assert face not in seen
                seen.add(face)
                if not replay.u32(face) & 3:
                    projected_triangles.append(
                        [
                            struct.unpack("<2i", replay.read(replay.u32(face + offset) + 0x1C, 8))
                            for offset in (8, 20, 32)
                        ]
                    )
                face = replay.u32(face + 4)
            block = replay.u32(block)

    def at_root(uc, _address, _size, _data):
        root = uc.reg_read(REGS["eax"])
        error = ctypes.create_string_buffer(512)
        assert capture(reader, None, root, str(path).encode(), error, len(error)), error.value
        captures.append(root)

    replay.at(0x4593AA, at_root)
    replay.at(0x473014, at_faces)
    destination = dump.u32(0x62B9A8)
    replay.write(destination, bytes([0x5A]) * 65536)
    replay.run(0x43EB67, 0x4FBA78, 0x62B51C)
    assert len(captures) == 1
    cpu = replay.read(destination, 65536)
    (out / "projected-triangles.json").write_text(json.dumps(projected_triangles))
    (out / "cpu.p8").write_bytes(cpu)
    subprocess.run(
        [str(build / "WDSceneGpuTests.exe"), str(path), str(out / "gpu"), "--shadow"], check=True
    )
    gpu = (out / "gpu.p8").read_bytes()
    assert len(cpu) == len(gpu) == 65536 and set(cpu) == set(gpu) == {0, 1}
    assert all(gpu[i] == gpu[i + 1] for i in range(0, len(gpu), 2))
    mismatch = sum(a != b for a, b in zip(cpu, gpu, strict=True))
    intersection = sum(bool(a and b) for a, b in zip(cpu, gpu, strict=True))
    union = sum(bool(a or b) for a, b in zip(cpu, gpu, strict=True))
    report = {
        "source_dump": original_report["dump"],
        "source_scope": "captured recomp state replayed through original x86",
        "cpu_mask_pixels": sum(cpu),
        "gpu_mask_pixels": sum(gpu),
        "different_bytes": mismatch,
        "intersection_over_union": intersection / union,
        "packing_pairs_equal": True,
        "exact_pixel_parity": mismatch == 0,
    }
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
