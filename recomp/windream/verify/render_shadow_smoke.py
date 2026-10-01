"""Capture transient retail shadow inputs and compare the direct GPU mask.

Uses the real-shadow dump recorded by render_smoke.py. Readbacks are oracle
exports only; the production shadow producer/consumer stay on the GPU.
"""

import argparse
import ctypes
import json
import struct
import subprocess
import sys
from pathlib import Path

from mdmp import Dump
from render_scene_smoke import read_snapshot
from render_smoke import REGS, Replay

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--require-parity",
        action="store_true",
        help="fail unless every original shadow-mask byte matches",
    )
    args = parser.parse_args()
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
    retail_nodes = {}
    retail_faces = {}

    def at_faces(uc, _address, _size, _data):
        node = uc.reg_read(REGS["eax"])
        retail_nodes[node] = {
            "composed": struct.unpack("<12i", replay.read(node + 0x4C, 48)),
            "source_position": struct.unpack("<3i", replay.read(node + 0x1C, 12)),
            "vertices": {},
        }
        block = replay.u32(node + 0xA4)
        while block:
            face = replay.u32(block + 0x24)
            seen = set()
            while face:
                assert face not in seen
                seen.add(face)
                retail_faces[face] = {
                    "flags": replay.u32(face),
                    "normal_dot": struct.unpack("<i", replay.read(replay.u32(face + 0x2C) + 12, 4))[
                        0
                    ],
                }
                if not replay.u32(face) & 3:
                    for offset in (8, 20, 32):
                        address = replay.u32(face + offset)
                        retail_nodes[node]["vertices"][address] = {
                            "view": struct.unpack("<3f", replay.read(address + 0x10, 12)),
                            "screen": struct.unpack("<2i", replay.read(address + 0x1C, 8)),
                        }
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
    (out / "retail-nodes.json").write_text(json.dumps(retail_nodes))
    (out / "retail-faces.json").write_text(json.dumps(retail_faces))
    _camera, nodes, vertices, _faces = read_snapshot(path)
    projection = dll.wd_shadow_projection_file
    projection.argtypes = [
        ctypes.c_char_p,
        ctypes.POINTER(ctypes.c_int32),
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_int32),
    ]
    poses = (ctypes.c_int32 * (len(nodes) * 12))()
    views = (ctypes.c_float * (len(vertices) * 3))()
    screens = (ctypes.c_int32 * (len(vertices) * 2))()
    assert projection(str(path).encode(), poses, views, screens)
    vertex_indices = {v[0]: i for i, v in enumerate(vertices)}
    compared = 0
    for i, node in enumerate(nodes):
        actual = retail_nodes.get(node["address"])
        if actual is None:
            continue
        assert list(poses[i * 12 : i * 12 + 12]) == list(actual["composed"])
        for address, expected in actual["vertices"].items():
            index = vertex_indices[address]
            assert list(views[index * 3 : index * 3 + 3]) == list(expected["view"])
            assert list(screens[index * 2 : index * 2 + 2]) == list(expected["screen"])
            compared += 1
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
        "source_pose_comparisons": len(retail_nodes),
        "source_vertex_projection_comparisons": compared,
    }
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return int(args.require_parity and mismatch != 0)


if __name__ == "__main__":
    sys.exit(main())
