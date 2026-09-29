"""Retail-x86 oracle for mixed radial/oriented flat lighting and local axes.

Original names/layouts: July demo Compute_Face_Illum_, Build_Obj_Lights_, _light.
The executed oracle is retail Windows, not a translation of the demo code.
"""

import ctypes as c
import json
import random
import struct
from pathlib import Path

from mdmp import Dump
from render_light_smoke import Light, recomp_env
from render_smoke import ESP, REGS, STOP, Replay


class LocalLight(c.Structure):
    _fields_ = [("radial", Light), ("type", c.c_uint32), ("axis", c.c_int32 * 3)]


def main():
    replay = Replay(Dump(Path("out/scratch/retail-gdidream-222659.dmp"), 0), pixels=True)
    base = 0x18000000
    replay.uc.mem_map(base, 0x10000)
    for page in range(base, base + 0x10000, 4096):
        replay.pages[page] = bytes(4096)
    owner, block, face, normal, vertices = [base + i * 4096 for i in range(5)]
    replay.put(owner + 0xA4, block)
    replay.put(block + 4, 3)
    replay.put(block + 0x24, face)
    replay.put(face + 0x2C, normal)
    for corner in range(3):
        replay.put(face + 8 + corner * 12, vertices + corner * 40)
    dll = c.CDLL(str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll"))
    shade_fn = dll.wd_flat_lights
    shade_fn.argtypes = [
        c.POINTER(c.c_int32),
        c.POINTER(c.c_int32),
        c.c_int32,
        c.POINTER(LocalLight),
        c.c_size_t,
        c.POINTER(c.c_uint8),
        c.POINTER(c.c_int32),
    ]
    axis_fn = dll.wd_light_axis
    axis_fn.argtypes = [c.POINTER(c.c_float), c.POINTER(c.c_int32), c.POINTER(c.c_int32)]

    def execute(entry):
        replay.uc.reg_write(REGS["eax"], owner)
        replay.uc.reg_write(REGS["esp"], ESP)
        replay.uc.mem_write(ESP, struct.pack("<I", STOP))
        replay.ensure(entry, 1)
        replay.uc.emu_start(entry, STOP, count=1000000)

    rng = random.Random(607)
    cases = []
    for distance in (0, 1, 49, 50, 51, 75, 99, 100, 101):
        for z in (-32768, -32767, -1057, -1, 0, 1, 32768):
            cases.append(
                ([0] * 9, [0, 0, 32768], 0, [(2, (0, 0, distance, 50, 100, 31), (0, 0, z))])
            )
    for _ in range(1000):
        cases.append(
            (
                [rng.randint(-2000, 2000) for _ in range(9)],
                [rng.randint(-32768, 32768) for _ in range(3)],
                rng.randint(-2000, 2000),
                [
                    (
                        rng.choice((1, 2)),
                        tuple(rng.randint(-3000, 3000) for _ in range(3))
                        + (
                            rng.choice((0, 50, 1000)),
                            rng.choice((100, 1000, 4000)),
                            rng.randrange(32),
                        ),
                        tuple(rng.randint(-32768, 32768) for _ in range(3)),
                    )
                    for _ in range(rng.randint(1, 8))
                ],
            )
        )
    for index, (points, n, plane, lights) in enumerate(cases):
        replay.put(owner + 0xC4, len(lights))
        replay.write(owner + 0xC8, bytes(range(len(lights))))
        replay.write(normal, struct.pack("<4i", *n, 0x123456))
        replay.put(face + 0x30, plane & 0xFFFFFFFF)
        for corner in range(3):
            replay.write(
                vertices + corner * 40 + 4, struct.pack("<3i", *points[corner * 3 : corner * 3 + 3])
            )
        native = (LocalLight * len(lights))()
        for i, (kind, light, axis) in enumerate(lights):
            address = 0x672700 + i * 0x94
            replay.put(address, kind)
            replay.write(address + 0x64, struct.pack("<3i", *light[:3]))
            replay.write(address + 0x70, struct.pack("<3i", *axis))
            replay.write(address + 0x88, struct.pack("<3i", *light[3:]))
            native[i] = LocalLight(
                Light((c.c_int32 * 3)(*light[:3]), *light[3:]), kind, (c.c_int32 * 3)(*axis)
            )
        execute(0x47B7E0)
        shade, dot = c.c_uint8(), c.c_int32()
        assert shade_fn(
            (c.c_int32 * 9)(*points),
            (c.c_int32 * 3)(*n),
            plane,
            native,
            len(lights),
            c.byref(shade),
            c.byref(dot),
        )
        actual = (
            replay.read(face + 0x40, 1)[0],
            struct.unpack("<i", replay.read(normal + 12, 4))[0],
        )
        assert (shade.value, dot.value) == actual, (index, lights, shade.value, dot.value, actual)

    # Deliberately include non-unit matrices: retail uses the transpose, not inverse.
    rotations = [
        (32768, 0, 0, 0, 32768, 0, 0, 0, 32768),
        (0, 0, 32768, 0, 32768, 0, -32768, 0, 0),
        (16384, 0, 0, 0, 16384, 0, 0, 0, 16384),
        (30000, 1000, -2000, -700, 32000, 200, 1500, -500, 31000),
    ]
    replay.put(owner + 0xC4, 1)
    replay.write(owner + 0xC8, bytes(8))
    replay.put(0x672700, 2)
    transforms = 0
    for rotation in rotations:
        for _ in range(100):
            orientation = [rng.randint(-32768, 32768) for _ in range(9)]
            replay.write(owner + 0x58, struct.pack("<9i", *rotation))
            replay.write(0x672740, struct.pack("<9i", *orientation))
            execute(0x47B3D0)
            actual = struct.unpack("<3i", replay.read(0x672770, 12))
            world = [
                v
                for row in range(3)
                for v in (*[rotation[row * 3 + k] / 32768 for k in range(3)], 123)
            ]
            axis = (c.c_int32 * 3)()
            assert axis_fn((c.c_float * 12)(*world), (c.c_int32 * 9)(*orientation), axis)
            assert tuple(axis) == actual, (rotation, orientation, tuple(axis), actual)
            transforms += 1
    report = {
        "mixed_flat_cases": len(cases),
        "axis_cases": transforms,
        "shade_or_normal_mismatches": 0,
        "axis_mismatches": 0,
        "scope": "flat-light arithmetic and local axis only; not the full camera chain",
    }
    (recomp_env.out_dir("oriented-lighting") / "results.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
