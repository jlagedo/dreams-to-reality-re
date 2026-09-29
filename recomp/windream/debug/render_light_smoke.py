"""Compare the shared radial flat-shade kernel with retail REND_LightObject.

uv run --with unicorn python recomp/windream/debug/render_light_smoke.py
Controlled owner-local inputs isolate shading from the separate light transform.
"""

import ctypes as c
import json
import random
import struct
import sys
from pathlib import Path

from mdmp import Dump
from render_smoke import ESP, REGS, STOP, Replay

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402


class Light(c.Structure):
    _fields_ = [
        ("position", c.c_int32 * 3),
        ("inner_radius", c.c_int32),
        ("outer_radius", c.c_int32),
        ("intensity", c.c_int32),
    ]


def main():
    out = recomp_env.out_dir("lighting")
    replay = Replay(Dump(Path("out/scratch/retail-gdidream-222659.dmp"), 0), pixels=True)
    base = 0x18000000
    replay.uc.mem_map(base, 0x10000)
    for page in range(base, base + 0x10000, 4096):
        replay.pages[page] = bytes(4096)
    owner, block, face, normal, vertices = (
        base,
        base + 0x1000,
        base + 0x2000,
        base + 0x3000,
        base + 0x4000,
    )
    replay.put(owner + 0xA4, block)
    replay.put(block + 4, 3)
    replay.put(block + 0x24, face)
    replay.put(face + 0x2C, normal)
    for corner in range(3):
        replay.put(face + 8 + corner * 12, vertices + corner * 40)
    dll = c.CDLL(str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll"))
    fn = dll.wd_flat_light_shade
    fn.argtypes = [
        c.POINTER(c.c_int32),
        c.POINTER(c.c_int32),
        c.c_int32,
        c.POINTER(Light),
        c.c_size_t,
        c.POINTER(c.c_uint8),
    ]
    fn.restype = c.c_int
    cases = []
    for z in (0, 1, 49, 50, 51, 75, 99, 100, 101, -1, -50):
        for count in (1, 2, 8):
            cases.append(([0] * 9, [0, 0, 32768], 0, [(0, 0, z, 50, 100, 31)] * count))
    rng = random.Random(606)
    for _ in range(1000):
        points = [rng.randint(-2000, 2000) for _ in range(9)]
        n = [rng.randint(-32768, 32768) for _ in range(3)]
        plane = rng.randint(-2000, 2000)
        lights = [
            tuple(rng.randint(-3000, 3000) for _ in range(3))
            + (rng.choice((0, 50, 1000)), rng.choice((100, 1000, 4000)), rng.randrange(32))
            for _ in range(rng.randint(1, 8))
        ]
        cases.append((points, n, plane, lights))
    results = []
    for index, (points, n, plane, lights) in enumerate(cases):
        replay.put(owner + 0xC4, len(lights))
        replay.write(owner + 0xC8, bytes(range(len(lights))))
        replay.write(normal, struct.pack("<3i", *n))
        replay.put(face + 0x30, plane & 0xFFFFFFFF)
        for corner in range(3):
            replay.write(
                vertices + corner * 40 + 4, struct.pack("<3i", *points[corner * 3 : corner * 3 + 3])
            )
        native_lights = (Light * len(lights))()
        for i, light in enumerate(lights):
            address = 0x672700 + i * 0x94
            replay.put(address, 1)
            replay.write(address + 0x64, struct.pack("<3i", *light[:3]))
            replay.write(address + 0x88, struct.pack("<3i", *light[3:]))
            native_lights[i] = Light((c.c_int32 * 3)(*light[:3]), *light[3:])
        replay.uc.reg_write(REGS["eax"], owner)
        replay.uc.reg_write(REGS["esp"], ESP)
        replay.uc.mem_write(ESP, struct.pack("<I", STOP))
        replay.ensure(0x47B7E0, 1)
        replay.uc.emu_start(0x47B7E0, STOP, count=1000000)
        actual = replay.read(face + 0x40, 1)[0]
        shade = c.c_uint8()
        assert fn(
            (c.c_int32 * 9)(*points),
            (c.c_int32 * 3)(*n),
            plane,
            native_lights,
            len(lights),
            c.byref(shade),
        )
        assert shade.value == actual, (index, points, n, plane, lights, shade.value, actual)
        results.append(actual)
    transform = dll.wd_light_local
    transform.argtypes = [c.POINTER(c.c_float), c.POINTER(c.c_int32), c.POINTER(c.c_int32)]
    transform.restype = c.c_int
    matrices = [
        (32768, 0, 0, 0, 32768, 0, 0, 0, 32768),
        (0, 0, 32768, 0, 32768, 0, -32768, 0, 0),
        (16384, 0, 0, 0, 16384, 0, 0, 0, 16384),
        (65536, 0, 0, 0, 65536, 0, 0, 0, 65536),
        (30000, 1000, -2000, -700, 32000, 200, 1500, -500, 31000),
    ]
    transform_cases = 0
    replay.put(owner + 0xC4, 1)
    replay.write(owner + 0xC8, b"\0")
    for rotation in matrices:
        for _ in range(100):
            origin = [rng.randint(-3000, 3000) for _ in range(3)]
            position = [rng.randint(-3000, 3000) for _ in range(3)]
            replay.write(owner + 0x4C, struct.pack("<3i", *origin))
            replay.write(owner + 0x58, struct.pack("<9i", *rotation))
            replay.write(0x672734, struct.pack("<3i", *position))
            replay.uc.reg_write(REGS["eax"], owner)
            replay.uc.reg_write(REGS["esp"], ESP)
            replay.uc.mem_write(ESP, struct.pack("<I", STOP))
            replay.ensure(0x47B3D0, 1)
            replay.uc.emu_start(0x47B3D0, STOP, count=100000)
            actual = struct.unpack("<3i", replay.read(0x672764, 12))
            world = [
                v
                for row in range(3)
                for v in (*[rotation[row * 3 + k] / 32768 for k in range(3)], origin[row])
            ]
            local = (c.c_int32 * 3)()
            assert transform((c.c_float * 12)(*world), (c.c_int32 * 3)(*position), local)
            assert tuple(local) == actual, (rotation, origin, position, tuple(local), actual)
            transform_cases += 1
    # A culled first list entry retains its shade while the next face is lit.
    replay.write(normal, struct.pack("<3i", 0, 0, 32768))
    replay.put(face + 0x30, 0)
    for corner in range(3):
        replay.write(vertices + corner * 40 + 4, bytes(12))
    replay.write(0x672764, struct.pack("<3i", 0, 0, 25))
    replay.write(0x672788, struct.pack("<3i", 50, 100, 31))
    replay.write(face + 68, replay.read(face, 68))
    replay.put(face + 68, 0)
    replay.put(face + 72, 0)
    replay.put(face, 1)
    replay.put(face + 4, face + 68)
    replay.write(face + 0x40, b"\x13")
    replay.uc.reg_write(REGS["eax"], owner)
    replay.uc.reg_write(REGS["esp"], ESP)
    replay.uc.mem_write(ESP, struct.pack("<I", STOP))
    replay.uc.emu_start(0x47B7E0, STOP, count=100000)
    assert replay.read(face + 0x40, 1)[0] == 19 and replay.read(face + 68 + 0x40, 1)[0] == 31
    pair = dll.wd_lit_pair
    pair.argtypes = [
        c.POINTER(c.c_int32),
        c.POINTER(Light),
        c.POINTER(c.c_uint8),
        c.POINTER(c.c_int32),
        c.POINTER(c.c_int32),
    ]
    pair.restype = c.c_int
    native_shades = (c.c_uint8 * 2)(19, 23)
    native_dots = (c.c_int32 * 2)(91, 92)
    native_local = (c.c_int32 * 3)()
    for i in range(2):
        address = face + i * 0x80
        replay.write(address, bytes(68))
        replay.put(address + 4, face + 0x80 if i == 0 else 0)
        replay.put(address + 0x2C, normal + i * 16)
        replay.put(address + 0x30, (-10) & 0xFFFFFFFF)
        replay.write(normal + i * 16, struct.pack("<4i", 0, 0, -32768, 91 + i))
        replay.write(address + 0x40, bytes([19 if i == 0 else 23]))
        for corner in range(3):
            replay.put(address + 8 + corner * 12, vertices + (i * 3 + corner) * 40)
    frames = []
    for frame_index in range(3):
        points = [-1, -1, 10, 0, 1, 10, 1, -1, 10, 1, -1, 10, 2, 1, 10, 3, -1, 10]
        if frame_index == 1:
            for corner in range(3):
                points[corner * 3] += 100
        replay.put(face, 1 if frame_index == 1 else 0)
        for i in range(6):
            replay.write(vertices + i * 40 + 4, struct.pack("<3i", *points[i * 3 : i * 3 + 3]))
        light = Light(
            (c.c_int32 * 3)(0, 0, 0 if frame_index == 0 else -10),
            1000,
            2000,
            31 if frame_index == 0 else 15,
        )
        replay.write(0x672764, struct.pack("<3i", *light.position))
        replay.write(
            0x672788, struct.pack("<3i", light.inner_radius, light.outer_radius, light.intensity)
        )
        replay.uc.reg_write(REGS["eax"], owner)
        replay.uc.reg_write(REGS["esp"], ESP)
        replay.uc.mem_write(ESP, struct.pack("<I", STOP))
        replay.uc.emu_start(0x47B7E0, STOP, count=100000)
        assert pair(
            (c.c_int32 * 18)(*points), c.byref(light), native_shades, native_dots, native_local
        )
        expected_shades = [replay.read(face + i * 0x80 + 0x40, 1)[0] for i in range(2)]
        expected_dots = [
            struct.unpack("<i", replay.read(normal + i * 16 + 12, 4))[0] for i in range(2)
        ]
        assert list(native_shades) == expected_shades, (
            frame_index,
            list(native_shades),
            expected_shades,
        )
        assert list(native_dots) == expected_dots, (frame_index, list(native_dots), expected_dots)
        assert list(native_local) == list(light.position)
        frames.append({"shades": expected_shades, "normal_dots": expected_dots})
    report = {
        "cases": len(cases),
        "shade_mismatches": 0,
        "transform_cases": transform_cases,
        "transform_mismatches": 0,
        "culled_head_retains_shade": True,
        "lit_culled_lit_metadata": frames,
        "boundary_shades": results[:33],
        "scope": "flat radial-light and local-transform kernels; live integration remains separate",
    }
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
